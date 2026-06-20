// RawTar.cpp
#include "RawTar.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <esp_task_wdt.h>
#include "FileSystemManager.h"
#include "Logging.h"
#include "TemperatureLogging.h"
#include "DiagLog.h"
#include "DiagConfig.h"
#include "Config.h"

TaskHandle_t thRawTarProducer = NULL;
volatile uint32_t rawTarLastStackWords = 0;
volatile uint32_t rawTarLastHwmWords   = 0;

static volatile bool g_rawTarInProgress = false;
static bool g_rawTarBegun = false;

static String u64ToString(uint64_t v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
  return String(buf);
}

struct RawTarEntry {
  String path;
  String tarName;
  String dateKey;
  uint32_t size = 0;
  bool isDir = false;
  bool sent = false;
};

struct RawTarEstimate {
  uint64_t sourceBytes = 0;
  uint64_t tarBytes = 1024; // final two zero blocks
  uint32_t files = 0;
  uint32_t dirs = 0;
  uint32_t entries = 0;
  uint16_t maxPathLen = 0;
  bool truncated = false;
  String error;
};

static String rawTarNormalizePath(String path, bool keepTrailingSlash = false) {
  path.trim();
  if (path.length() == 0) path = "/";
  path.replace("\\", "/");
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  if (!path.startsWith("/")) path = "/" + path;
  if (!keepTrailingSlash) {
    while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
  } else if (!path.endsWith("/")) {
    path += "/";
  }
  return path;
}

static String rawTarChildPath(const String& parent, const String& entryName) {
  String p = rawTarNormalizePath(parent, false);
  String e = entryName;
  e.replace("\\", "/");

  // Arduino LittleFS can return either a base name or an absolute path.
  if (e.startsWith("/")) return rawTarNormalizePath(e, false);

  int slash = e.lastIndexOf('/');
  if (slash >= 0) e = e.substring(slash + 1);
  if (p == "/") return "/" + e;
  return p + "/" + e;
}

static bool rawTarSeenPath(const String& seen, const String& path) {
  return seen.indexOf("|" + path + "|") >= 0;
}

static String rawTarNameFromPath(String path, bool isDir) {
  path = rawTarNormalizePath(path, false);
  while (path.startsWith("/")) path.remove(0, 1);
  while (path.endsWith("/") && path.length() > 1) path.remove(path.length() - 1);
  if (path.length() == 0) path = "root";
  if (isDir && !path.endsWith("/")) path += "/";
  return path;
}

static String rawTarDateFromPath(const String& fullPath) {
  int slash = fullPath.lastIndexOf('/');
  String name = (slash >= 0) ? fullPath.substring(slash + 1) : fullPath;
  int len = name.length();
  if (len < 8) return String("9999-99-99");

  for (int i = 0; i <= len - 10; ++i) {
    char c4 = name[i + 4];
    char c7 = name[i + 7];
    if (!((c4 == '-') || (c4 == '_') || (c4 == '.'))) continue;
    if (!((c7 == '-') || (c7 == '_') || (c7 == '.'))) continue;
    bool ok = true;
    const int idxs[8] = {0,1,2,3,5,6,8,9};
    for (int k = 0; k < 8; ++k) {
      int j = idxs[k];
      char c = name[i + j];
      if (c < '0' || c > '9') { ok = false; break; }
    }
    if (ok) return name.substring(i, i + 4) + "-" + name.substring(i + 5, i + 7) + "-" + name.substring(i + 8, i + 10);
  }

  for (int i = 0; i <= len - 8; ++i) {
    bool ok = true;
    for (int j = 0; j < 8; ++j) {
      char c = name[i + j];
      if (c < '0' || c > '9') { ok = false; break; }
    }
    if (ok) return name.substring(i, i + 4) + "-" + name.substring(i + 4, i + 6) + "-" + name.substring(i + 6, i + 8);
  }
  return String("9999-99-99");
}

static uint64_t rawTarPaddedFileBytes(uint32_t sz) {
  return (uint64_t)sz + ((512UL - (sz % 512UL)) % 512UL);
}

static void rawTarPutOctal(char* dest, size_t len, uint64_t value) {
  memset(dest, '0', len);
  if (len == 0) return;
  dest[len - 1] = '\0';
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%llo", (unsigned long long)value);
  size_t tmpLen = strlen(tmp);
  size_t fieldDigits = len - 1;
  size_t copyLen = (tmpLen > fieldDigits) ? fieldDigits : tmpLen;
  size_t start = fieldDigits - copyLen;
  memcpy(dest + start, tmp + (tmpLen - copyLen), copyLen);
}

static bool rawTarBuildHeader(const RawTarEntry& e, uint8_t* h, String& err) {
  String name = e.tarName;
  if (e.isDir && !name.endsWith("/")) name += "/";
  if (name.length() == 0 || name.length() > 99) {
    err = "path too long for simple USTAR header: " + name;
    return false;
  }

  memset(h, 0, 512);
  memcpy(h + 0, name.c_str(), name.length());
  rawTarPutOctal((char*)h + 100, 8, e.isDir ? 0755 : 0644);
  rawTarPutOctal((char*)h + 108, 8, 0);
  rawTarPutOctal((char*)h + 116, 8, 0);
  rawTarPutOctal((char*)h + 124, 12, e.isDir ? 0 : e.size);
  rawTarPutOctal((char*)h + 136, 12, (uint64_t)time(nullptr));
  memset(h + 148, ' ', 8);
  h[156] = e.isDir ? '5' : '0';
  memcpy(h + 257, "ustar", 5);
  memcpy(h + 263, "00", 2);
  memcpy(h + 265, "esp32", 5);
  memcpy(h + 297, "esp32", 5);

  unsigned int sum = 0;
  for (size_t i = 0; i < 512; ++i) sum += h[i];
  char chk[16];
  snprintf(chk, sizeof(chk), "%06o", sum);
  memcpy(h + 148, chk, 6);
  h[154] = '\0';
  h[155] = ' ';
  return true;
}

static bool rawTarCollectManifestUnlocked(const String& path, std::vector<RawTarEntry>& entries,
                                          RawTarEstimate& est, String& err, uint8_t depth) {
  if (depth > 32) { err = "recursion depth exceeded"; return false; }
  if (entries.size() >= RAW_TAR_MAX_MANIFEST_ENTRIES) { est.truncated = true; err = "manifest entry limit exceeded"; return false; }

  File node = LittleFS.open(path, "r");
  if (!node) { err = "open failed: " + path; return false; }
  bool isDir = node.isDirectory();
  uint32_t sz = isDir ? 0 : (uint32_t)node.size();
  node.close();

  RawTarEntry e;
  e.path = rawTarNormalizePath(path, false);
  e.isDir = isDir;
  e.size = sz;
  e.tarName = rawTarNameFromPath(e.path, isDir);
  e.dateKey = isDir ? String("0000-00-00") : rawTarDateFromPath(e.path);
  entries.push_back(e);

  est.entries++;
  if (isDir) est.dirs++; else { est.files++; est.sourceBytes += sz; }
  est.tarBytes += 512ULL + (isDir ? 0ULL : rawTarPaddedFileBytes(sz));
  if (e.path.length() > est.maxPathLen) est.maxPathLen = (uint16_t)e.path.length();

  if (!isDir) return true;

  File dir = LittleFS.open(path, "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    err = "dir reopen failed: " + path;
    return false;
  }

  String seen;
  File child = dir.openNextFile();
  while (child) {
    String childName = String(child.name());
    child.close();
    String childPath = rawTarChildPath(path, childName);
    if (childPath.length() > 1 && childPath != path && !rawTarSeenPath(seen, childPath)) {
      seen += "|"; seen += childPath; seen += "|";
      if (!rawTarCollectManifestUnlocked(childPath, entries, est, err, depth + 1)) {
        dir.close();
        return false;
      }
    }
    esp_task_wdt_reset();
    vTaskDelay(1);
    child = dir.openNextFile();
  }
  dir.close();
  return true;
}

static void rawTarSortManifest(std::vector<RawTarEntry>& entries) {
  std::sort(entries.begin(), entries.end(), [](const RawTarEntry& a, const RawTarEntry& b) {
    if (a.isDir != b.isDir) return a.isDir && !b.isDir; // directories first
    if (!a.isDir && !b.isDir && a.dateKey != b.dateKey) return a.dateKey < b.dateKey; // oldest first
    return a.path < b.path;
  });
}

static String rawTarEstimateJson(const String& dir, bool ok, const RawTarEstimate& est, const String& err = "") {
  size_t total = g_fileSystemReady ? LittleFS.totalBytes() : 0;
  size_t used  = g_fileSystemReady ? LittleFS.usedBytes() : 0;
  float pct = (total > 0) ? (100.0f * (float)used / (float)total) : 0.0f;
  bool cleanupRisk = (pct >= (FS_Cleaning_START_LIMIT - 5.0f));

  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false");
  json += ",\"dir\":\"" + dir + "\"";
  json += ",\"bytes\":" + u64ToString(est.sourceBytes);
  json += ",\"tarBytes\":" + u64ToString(est.tarBytes);
  json += ",\"files\":" + String((unsigned)est.files);
  json += ",\"dirs\":" + String((unsigned)est.dirs);
  json += ",\"entries\":" + String((unsigned)est.entries);
  json += ",\"maxPathLen\":" + String((unsigned)est.maxPathLen);
  json += ",\"truncated\":" + String(est.truncated ? "true" : "false");
  json += ",\"fsUsedBytes\":" + u64ToString((uint64_t)used);
  json += ",\"fsTotalBytes\":" + u64ToString((uint64_t)total);
  json += ",\"fsPctUsed\":" + String(pct, 1);
  json += ",\"cleanupStartLimit\":" + String(FS_Cleaning_START_LIMIT, 1);
  json += ",\"cleanupStopLimit\":" + String(FS_Cleaning_STOP_LIMIT, 1);
  json += ",\"cleanupRisk\":" + String(cleanupRisk ? "true" : "false");
  if (err.length()) {
    String esc = err;
    esc.replace("\\", "\\\\");
    esc.replace("\"", "\\\"");
    json += ",\"error\":\"" + esc + "\"";
  }
  json += "}";
  return json;
}

static bool rawTarBuildManifest(const String& dir, std::vector<RawTarEntry>& entries, RawTarEstimate& est, String& err) {
  entries.clear();
  est = RawTarEstimate();

  if (!takeFileSystemMutexWithRetry("[RAW-TAR] manifest", pdMS_TO_TICKS(10000), 3)) {
    err = "filesystem busy while building manifest";
    return false;
  }
  if (!LittleFS.exists(dir)) {
    xSemaphoreGive(fileSystemMutex);
    err = "path not found";
    return false;
  }
  File root = LittleFS.open(dir, "r");
  bool isDir = root && root.isDirectory();
  if (root) root.close();
  if (!isDir) {
    xSemaphoreGive(fileSystemMutex);
    err = "not a directory";
    return false;
  }

  bool ok = rawTarCollectManifestUnlocked(dir, entries, est, err, 0);
  xSemaphoreGive(fileSystemMutex);
  if (!ok) return false;
  rawTarSortManifest(entries);
  return true;
}

struct RawTarStreamSession {
  std::vector<RawTarEntry> entries;
  String root;
  String filename;
  size_t index = 0;
  enum Phase : uint8_t { PHASE_HEADER, PHASE_DATA, PHASE_PADDING, PHASE_TRAILER, PHASE_DONE, PHASE_FAILED } phase = PHASE_HEADER;
  uint8_t header[512];
  size_t headerPos = 512;
  File file;
  bool fileOpen = false;
  uint32_t fileRemaining = 0;
  uint32_t padRemaining = 0;
  uint32_t trailerRemaining = 1024;
  uint64_t bytesOut = 0;
  uint32_t filesSent = 0;
  uint32_t dirsSent = 0;
  String error;
  bool finished = false;
  bool deleteScheduled = false;
};

static void rawTarCloseFile(RawTarStreamSession* s) {
  if (!s) return;
  if (s->fileOpen) {
    s->file.close();
    s->fileOpen = false;
  }
}

static void rawTarClearArchiveState() {
  fsSetArchiveDownloadActive(false, "");
  g_rawTarInProgress = false;
}

static void scheduleRawTarSessionDelete(RawTarStreamSession* s) {
  if (!s || s->deleteScheduled) return;
  s->deleteScheduled = true;
  xTaskCreate([](void* pv) {
    RawTarStreamSession* ss = static_cast<RawTarStreamSession*>(pv);
    vTaskDelay(pdMS_TO_TICKS(RAW_TAR_DELETE_DELAY_MS));
    rawTarCloseFile(ss);
    delete ss;
    vTaskDelete(nullptr);
  }, "rawTarDel", 2048, s, 1, nullptr);
}

static void rawTarFinishSession(RawTarStreamSession* s, bool failed = false, const String& err = "") {
  if (!s || s->finished) return;
  rawTarCloseFile(s);
  s->finished = true;
  s->phase = failed ? RawTarStreamSession::PHASE_FAILED : RawTarStreamSession::PHASE_DONE;
  if (err.length()) s->error = err;
  rawTarClearArchiveState();
  LOG_CAT(DBG_ARCHIVE,
          "[RAW-TAR] stream %s root=%s bytesOut=%llu filesSent=%u dirsSent=%u err=%s\n",
          failed ? "failed" : "complete", s->root.c_str(),
          (unsigned long long)s->bytesOut, (unsigned)s->filesSent, (unsigned)s->dirsSent,
          s->error.c_str());
  scheduleRawTarSessionDelete(s);
}

static size_t rawTarWriteZerosToBuffer(uint8_t* buffer, size_t maxLen, size_t& pos, uint32_t& remaining) {
  size_t n = maxLen - pos;
  if (n > remaining) n = remaining;
  if (n > 0) {
    memset(buffer + pos, 0, n);
    pos += n;
    remaining -= n;
  }
  return n;
}

static size_t rawTarStreamCallback(RawTarStreamSession* s, uint8_t* buffer, size_t maxLen) {
  if (!s || s->finished || !buffer || maxLen == 0) return 0;
  size_t pos = 0;

  while (pos < maxLen && !s->finished) {
    esp_task_wdt_reset();

    if (s->phase == RawTarStreamSession::PHASE_HEADER) {
      if (s->index >= s->entries.size()) {
        s->phase = RawTarStreamSession::PHASE_TRAILER;
        s->trailerRemaining = 1024;
        continue;
      }

      RawTarEntry& e = s->entries[s->index];
      if (s->headerPos >= 512) {
        String err;
        if (!rawTarBuildHeader(e, s->header, err)) {
          rawTarFinishSession(s, true, err);
          break;
        }
        s->headerPos = 0;
      }

      size_t n = 512 - s->headerPos;
      if (n > maxLen - pos) n = maxLen - pos;
      memcpy(buffer + pos, s->header + s->headerPos, n);
      s->headerPos += n;
      pos += n;
      s->bytesOut += n;

      if (s->headerPos >= 512) {
        if (e.isDir) {
          e.sent = true;
          s->dirsSent++;
          s->index++;
          s->headerPos = 512;
        } else {
          s->fileRemaining = e.size;
          s->padRemaining = (512UL - (e.size % 512UL)) % 512UL;
          s->phase = RawTarStreamSession::PHASE_DATA;
        }
      }
      continue;
    }

    if (s->phase == RawTarStreamSession::PHASE_DATA) {
      if (s->index >= s->entries.size()) {
        rawTarFinishSession(s, true, "manifest index overrun");
        break;
      }
      RawTarEntry& e = s->entries[s->index];

      if (s->fileRemaining == 0) {
        rawTarCloseFile(s);
        e.sent = true;
        s->filesSent++;
        s->phase = RawTarStreamSession::PHASE_PADDING;
        continue;
      }

      if (!s->fileOpen) {
        if (!takeFileSystemMutexWithRetry("[RAW-TAR] open", pdMS_TO_TICKS(1000), 2)) {
          rawTarFinishSession(s, true, "filesystem busy opening " + e.path);
          break;
        }
        s->file = LittleFS.open(e.path, "r");
        xSemaphoreGive(fileSystemMutex);
        if (!s->file) {
          rawTarFinishSession(s, true, "open failed after manifest: " + e.path);
          break;
        }
        s->fileOpen = true;
      }

      size_t want = maxLen - pos;
      if (want > RAW_TAR_READ_CHUNK_BYTES) want = RAW_TAR_READ_CHUNK_BYTES;
      if (want > s->fileRemaining) want = s->fileRemaining;

      if (!takeFileSystemMutexWithRetry("[RAW-TAR] read", pdMS_TO_TICKS(1000), 2)) {
        rawTarFinishSession(s, true, "filesystem busy reading " + e.path);
        break;
      }
      int n = s->file.read(buffer + pos, want);
      xSemaphoreGive(fileSystemMutex);

      if (n <= 0) {
        rawTarFinishSession(s, true, "short read: " + e.path);
        break;
      }
      pos += (size_t)n;
      s->fileRemaining -= (uint32_t)n;
      s->bytesOut += (size_t)n;
      continue;
    }

    if (s->phase == RawTarStreamSession::PHASE_PADDING) {
      rawTarWriteZerosToBuffer(buffer, maxLen, pos, s->padRemaining);
      if (s->padRemaining == 0) {
        s->index++;
        s->headerPos = 512;
        s->phase = RawTarStreamSession::PHASE_HEADER;
      }
      continue;
    }

    if (s->phase == RawTarStreamSession::PHASE_TRAILER) {
      rawTarWriteZerosToBuffer(buffer, maxLen, pos, s->trailerRemaining);
      if (s->trailerRemaining == 0) {
        rawTarFinishSession(s, false);
      }
      continue;
    }

    break;
  }

  if (pos == 0 && s && !s->finished && s->phase != RawTarStreamSession::PHASE_FAILED) {
    vTaskDelay(1);
  }
  return pos;
}

static String rawTarFilenameForDir(const String& dir) {
  String d = rawTarNormalizePath(dir, false);
  if (d == "/") return String("drive.download.tar");
  String base = d.substring(d.lastIndexOf('/') + 1);
  if (base.length() == 0) base = "download";
  return base + ".tar";
}

static bool rawTarPreFlushForPath(const String& dir) {
  bool ok = true;
  if (dir == "/Pump_Logs" || dir.startsWith("/Pump_Logs/")) {
    ok = flushPendingPumpLogEvents(pdMS_TO_TICKS(3000), 3) && ok;
  }
  if (dir == "/Temperature_Logs" || dir.startsWith("/Temperature_Logs/")) {
    ok = requestTemperatureLogCacheFlush(pdMS_TO_TICKS(7000)) && ok;
  }
  return ok;
}

static void handleRawTarInfo(AsyncWebServerRequest* request) {
  RawTar::begin();
  if (!g_fileSystemReady) { request->send(503, "application/json", "{\"ok\":false,\"error\":\"File system not ready\"}"); return; }
  if (!request->hasParam("dir")) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing dir\"}"); return; }
  String dir = rawTarNormalizePath(request->getParam("dir")->value(), false);
  if (!isSafePath(dir)) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Unsafe path\"}"); return; }

  std::vector<RawTarEntry> entries;
  RawTarEstimate est;
  String err;
  bool ok = rawTarBuildManifest(dir, entries, est, err);
  request->send(ok ? 200 : 500, "application/json", rawTarEstimateJson(dir, ok, est, err));
}

static void handleRawTarDownload(AsyncWebServerRequest* request) {
  RawTar::begin();

  if (!g_fileSystemReady) { request->send(503, "text/plain", "File system not ready"); return; }
  if (g_rawTarInProgress) { request->send(429, "text/plain", "Another archive download is already in progress"); return; }
  if (!request->hasParam("dir")) { request->send(400, "text/plain", "Missing dir"); return; }

  String dir = rawTarNormalizePath(request->getParam("dir")->value(), false);
  LOG_CAT(DBG_ARCHIVE, "[RAW-TAR] streaming request dir=%s\n", dir.c_str());
  if (!isSafePath(dir)) { request->send(400, "text/plain", "Unsafe path"); return; }

  rawTarPreFlushForPath(dir);

  RawTarStreamSession* s = new RawTarStreamSession();
  if (!s) { request->send(500, "text/plain", "session allocation failed"); return; }
  s->root = dir;
  s->filename = rawTarFilenameForDir(dir);

  RawTarEstimate est;
  String err;
  if (!rawTarBuildManifest(dir, s->entries, est, err)) {
    delete s;
    request->send(500, "text/plain", "manifest failed: " + err);
    return;
  }

  if (s->entries.empty()) {
    delete s;
    request->send(404, "text/plain", "no archive entries");
    return;
  }

  g_rawTarInProgress = true;
  fsSetArchiveDownloadActive(true, dir);

  LOG_CAT(DBG_ARCHIVE,
          "[RAW-TAR] stream begin dir=%s entries=%u files=%u dirs=%u sourceBytes=%llu estTarBytes=%llu filename=%s\n",
          dir.c_str(), (unsigned)est.entries, (unsigned)est.files, (unsigned)est.dirs,
          (unsigned long long)est.sourceBytes, (unsigned long long)est.tarBytes,
          s->filename.c_str());

  request->onDisconnect([s]() {
    rawTarFinishSession(s, false);
  });

  AsyncWebServerResponse *response = request->beginChunkedResponse(
    "application/x-tar",
    [s](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      (void)index;
      return rawTarStreamCallback(s, buffer, maxLen);
    });

  response->addHeader("Content-Disposition", "attachment; filename=\"" + s->filename + "\"");
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Pragma", "no-cache");
  request->send(response);
}

static void handleArchiveClick(AsyncWebServerRequest *request) {
  String source = request->hasParam("source") ? request->getParam("source")->value() : "unknown";
  String path   = request->hasParam("path") ? request->getParam("path")->value() : "";
  LOG_CAT(DBG_ARCHIVE, "[ARCHIVE] browser click source=%s path=%s\n", source.c_str(), path.c_str());
  request->send(200, "application/json", "{\"ok\":true}");
}

void RawTar::begin() {
  if (g_rawTarBegun) return;
  g_rawTarBegun = true;
  LOG_CAT(DBG_ARCHIVE, "[RAW-TAR] Raw TAR streaming archive routes initialized\n");
}

void RawTar::registerRoutes(AsyncWebServer &server) {
  RawTar::begin();
  server.on("/fs/archive_click", HTTP_GET, handleArchiveClick);
  server.on("/fs/tar_info", HTTP_GET, handleRawTarInfo);
  server.on("/fs/raw_tar_info", HTTP_GET, handleRawTarInfo);
  server.on("/fs/download_raw_tar", HTTP_GET, handleRawTarDownload);
  server.on("/fs/tar_raw", HTTP_GET, handleRawTarDownload);
  server.on("/fs/download_tar", HTTP_GET, handleRawTarDownload);
}
