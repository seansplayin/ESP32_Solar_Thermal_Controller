// TarGZ.cpp
#include "TarGZ.h"
#include "FileSystemManager.h"
#include "TaskManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "DiagLog.h"
#include "Logging.h"





// ===== On-the-fly tar.gz streaming support (ring buffer in Internal Heap or PSRAM) =====
static const bool kTgzDebug = false;
TaskHandle_t thTgzProducer = NULL;
volatile uint32_t tgzLastStackWords = 0;
volatile uint32_t tgzLastHwmWords   = 0;
static volatile bool tgzInProgress = false;

// Phase 2 archive telemetry / cancellation state.
// This is intentionally lightweight; it does not make the current ESP32-targz
// producer fully cooperative yet, but it gives the web UI a reliable way to
// estimate, monitor, and request cancellation of long archive downloads.
enum TgzJobState : uint8_t {
  TGZ_STATE_IDLE = 0,
  TGZ_STATE_ESTIMATING,
  TGZ_STATE_STARTING,
  TGZ_STATE_COMPRESSING,
  TGZ_STATE_STREAMING,
  TGZ_STATE_DRAINING_AFTER_CANCEL,
  TGZ_STATE_CANCELLED,
  TGZ_STATE_COMPLETE,
  TGZ_STATE_FAILED
};

static const char* tgzStateName(uint8_t state) {
  switch (state) {
    case TGZ_STATE_ESTIMATING:             return "estimating";
    case TGZ_STATE_STARTING:               return "starting";
    case TGZ_STATE_COMPRESSING:            return "compressing";
    case TGZ_STATE_STREAMING:              return "streaming";
    case TGZ_STATE_DRAINING_AFTER_CANCEL:  return "draining-after-cancel";
    case TGZ_STATE_CANCELLED:              return "cancelled";
    case TGZ_STATE_COMPLETE:               return "complete";
    case TGZ_STATE_FAILED:                 return "failed";
    case TGZ_STATE_IDLE:
    default:                               return "idle";
  }
}

static String u64ToString(uint64_t v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
  return String(buf);
}

static String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

static uint32_t tgzRecommendedNoDataTimeoutMs(uint64_t sourceBytes, uint32_t fileCount) {
  // Large directories can spend noticeable time walking/opening files before
  // the next gzip bytes are emitted.  Keep this bounded so the async TCP task
  // is not held forever, but scale it above the old fixed 30-second timeout.
  uint64_t byFiles = ((uint64_t)fileCount / 1000ULL) * 1000ULL;        // 1s per 1000 files
  uint64_t bySize  = (sourceBytes / (10ULL * 1024ULL * 1024ULL)) * 1000ULL; // 1s per 10 MiB
  uint64_t total   = 30000ULL + byFiles + bySize;
  if (total < 30000ULL) total = 30000ULL;
  if (total > 120000ULL) total = 120000ULL;
  return (uint32_t)total;
}

struct TgzArchiveEstimate {
  uint64_t bytes = 0;
  uint32_t files = 0;
  uint32_t dirs = 0;
  uint32_t entries = 0;
  bool truncated = false;
};

static String normalizeTgzPath(String path, bool wantDir) {
  path.trim();
  if (path.length() == 0) path = "/";
  if (!path.startsWith("/")) path = "/" + path;
  while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
  if (wantDir && path != "/") path += "/";
  return path;
}

static String tgzDirectChildPath(const String& parentNoTrailingSlash, const String& entryName) {
  String parent = parentNoTrailingSlash;
  if (parent.length() == 0) parent = "/";
  while (parent.length() > 1 && parent.endsWith("/")) parent.remove(parent.length() - 1);

  String full = entryName;
  if (!full.startsWith("/")) {
    full = (parent == "/") ? ("/" + full) : (parent + "/" + full);
  }

  String prefix = (parent == "/") ? "/" : (parent + "/");
  if (!full.startsWith(prefix)) return full;

  String rel = full.substring(prefix.length());
  if (rel.length() == 0) return full;

  int slash = rel.indexOf('/');
  if (slash < 0) return full;

  String firstSegment = rel.substring(0, slash);
  return prefix + firstSegment;
}

static bool tgzSeenPath(const String& seen, const String& path) {
  String marker = "|";
  marker += path;
  marker += "|";
  return seen.indexOf(marker) >= 0;
}

static bool estimatePathUnlocked(String path, TgzArchiveEstimate& est,
                                 uint32_t maxEntries = 100000UL, uint8_t depth = 0) {
  path = normalizeTgzPath(path, false);
  if (depth > 32) { est.truncated = true; return true; }
  if (est.entries >= maxEntries) { est.truncated = true; return true; }

  File node = LittleFS.open(path, "r");
  if (!node) return false;

  bool isDir = node.isDirectory();
  if (!isDir) {
    est.files++;
    est.entries++;
    est.bytes += (uint64_t)node.size();
    node.close();
    return true;
  }

  est.dirs++;
  est.entries++;
  node.close();

  File dir = LittleFS.open(path, "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  String seen;
  File entry = dir.openNextFile();
  while (entry) {
    String entryName = String(entry.name());
    entry.close();

    String childPath = tgzDirectChildPath(path, entryName);
    childPath = normalizeTgzPath(childPath, false);

    if (childPath.length() > 1 && childPath != path && !tgzSeenPath(seen, childPath)) {
      seen += "|";
      seen += childPath;
      seen += "|";
      if (!estimatePathUnlocked(childPath, est, maxEntries, depth + 1)) {
        dir.close();
        return false;
      }
      if (est.truncated || est.entries >= maxEntries) {
        est.truncated = true;
        dir.close();
        return true;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }

    entry = dir.openNextFile();
  }

  dir.close();
  return true;
}

static SemaphoreHandle_t g_tgzStatusMutex = nullptr;
static uint32_t g_tgzNextJobId = 1;

static void ensureTgzStatusMutex() {
  if (!g_tgzStatusMutex) g_tgzStatusMutex = xSemaphoreCreateMutex();
}

// ===== TGZ memory debug helpers =====
static void tgzPrintMem(const char *tag) {
  if (!kTgzDebug) return;

  const uint32_t intCaps = (uint32_t)(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);


  uint32_t freeHeap  = (uint32_t)ESP.getFreeHeap();

  uint32_t totalInt  = (uint32_t)heap_caps_get_total_size(intCaps);
  uint32_t freeInt   = (uint32_t)heap_caps_get_free_size(intCaps);
  uint32_t lfbInt    = (uint32_t)heap_caps_get_largest_free_block(intCaps);
  uint32_t minInt    = (uint32_t)heap_caps_get_minimum_free_size(intCaps);

  uint32_t freePsram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint32_t lfbPsram  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

  LOG_CAT(
    DBG_TARGZ,
    "[TGZ] %s freeHeap=%u | INT free=%u lfb=%u min=%u total=%u | PSRAM free=%u lfb=%u\n",
    tag,
    freeHeap,
    freeInt, lfbInt, minInt, totalInt,
    freePsram, lfbPsram
  );
}





static bool g_beginDone = false;
static bool g_psramAvailable = false;

static bool tgzPsramAvailable() {
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

static bool tgzRingUsePsramEffective() {
  return (TGZ_RING_CACHE_LOCATION == 1) && tgzPsramAvailable();
}

// Runtime ring size selection:
// - If PSRAM is available, keep the configured TGZ_RINGBUF_BYTES
// - If PSRAM is NOT available, choose a much smaller internal-heap ring
//   based on the current largest free block so fallback actually works.
static size_t tgzEffectiveRingBytes() {
  if (tgzRingUsePsramEffective()) {
    return TGZ_RINGBUF_BYTES;
  }

  // No-PSRAM mode:
  // Be extremely conservative so the compressor and HTTP path still have room.
  const size_t kMinInternalRing        = 4 * 1024;
  const size_t kTargetInternalRing     = 4 * 1024;
  const size_t kLz77WorkspaceReserve   = 16 * 1024;
  const size_t kExtraSafetyReserve     = 8 * 1024;
  const size_t kTotalReserve           = kLz77WorkspaceReserve + kExtraSafetyReserve;

  size_t lfb = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

  // If heap is extremely tight, still try the minimum ring.
  if (lfb <= (kMinInternalRing + kTotalReserve)) {
    return kMinInternalRing;
  }

  size_t candidate = lfb - kTotalReserve;

  // Clamp aggressively for no-PSRAM mode.
  if (candidate > kTargetInternalRing) candidate = kTargetInternalRing;
  if (candidate < kMinInternalRing)    candidate = kMinInternalRing;

  // round down to 1 KB boundary for small internal-heap mode
  candidate &= ~((size_t)1024 - 1);
  if (candidate < kMinInternalRing) candidate = kMinInternalRing;

  return candidate;
}

static const char* tgzEffectiveRingLocationName() {
  return tgzRingUsePsramEffective() ? "PSRAM" : "INTERNAL HEAP";
}

void TarGZ::begin() {
  if (g_beginDone) return;
  g_beginDone = true;

  g_psramAvailable = tgzPsramAvailable();
  const size_t effectiveRingBytes = tgzEffectiveRingBytes();

  // Startup warning if user requested PSRAM but PSRAM isn't enabled/available
  if (TGZ_RING_CACHE_LOCATION == 1 && !g_psramAvailable) {
    LOG_CAT(DBG_TARGZ,
      "[TGZ] WARNING: TGZ_RING_CACHE_LOCATION=1 (PSRAM) but PSRAM is NOT available. "
      "Falling back to compressor-safe Internal Heap ring buffer size=%u bytes.\n",
      (unsigned)effectiveRingBytes);
  }

  // Warn if effective ring buffer is below the recommended minimum
  if (effectiveRingBytes < (256 * 1024)) {
    LOG_CAT(DBG_TARGZ,
      "[TGZ] WARNING: TarGZ effective ring buffer is smaller than 256 KB; downloads may be slower or less stable. size=%u bytes\n",
      (unsigned)effectiveRingBytes);
  }
}



// moved here from FileSystemManager.* (tgz only)
static BaseType_t spawnTaskOptionalCore(
  TaskFunction_t fn,
  const char* name,
  uint32_t stackWords,
  void* arg,
  UBaseType_t priority,
  TaskHandle_t* outHandle,
  int core
) {
  if (core >= 0) {
    return xTaskCreatePinnedToCore(fn, name, stackWords, arg, priority, outHandle, core);
  }
  return xTaskCreate(fn, name, stackWords, arg, priority, outHandle);
}


// Ring buffer stream to bridge producer->HTTP chunk callback
class TgzRingStream : public Stream {
public:
  explicit TgzRingStream(size_t capBytes)
  : _cap(capBytes), _buf(nullptr) {

    if (_cap == 0) return;

    const bool usePsram = tgzRingUsePsramEffective();
    uint32_t caps = MALLOC_CAP_8BIT | (usePsram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);

    _buf = (uint8_t*)heap_caps_malloc(_cap, caps);
    if (!_buf && usePsram) {
      // fallback to internal if PSRAM allocation fails (even if PSRAM exists)
      _buf = (uint8_t*)heap_caps_malloc(_cap, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }

    if (!_buf) {
      _cap = 0;
    } else {
      _semData = xSemaphoreCreateBinary();
      _semSpace = xSemaphoreCreateBinary();
      xSemaphoreGive(_semSpace);
    }
  }

  ~TgzRingStream() {
    if (_buf) heap_caps_free(_buf);
    if (_semData) vSemaphoreDelete(_semData);
    if (_semSpace) vSemaphoreDelete(_semSpace);
  }

  bool ok() const { return _cap > 0 && _buf != nullptr; }

  void setProducedCounter(volatile uint64_t* counter) {
    _producedCounter = counter;
  }

  void finish() {
    _finished = true;
    if (_semData) xSemaphoreGive(_semData);
    if (_semSpace) xSemaphoreGive(_semSpace);
  }

  void cancel() {
    _cancelled = true;
    if (_semData) xSemaphoreGive(_semData);
    if (_semSpace) xSemaphoreGive(_semSpace);
  }

  bool done() const {
    return (_finished && _used == 0) || _cancelled;
  }

  // Stream interface
  int available() override { return (int)_used; }
  int read() override { uint8_t b; return (readBytes(&b, 1) == 1) ? b : -1; }
  int peek() override { return -1; }
  void flush() override {}

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t *data, size_t len) override {
    if (!ok()) return 0;

    // If the browser/client disconnects, do NOT return 0 to ESP32-targz.
    // Some compression paths treat a short/failed Stream::write() as fatal and abort().
    // Instead, become a "black-hole" sink: accept the bytes, discard them, and let the
    // producer finish cleanly so the controller does not crash from a cancelled download.
    if (_cancelled) {
      vTaskDelay(pdMS_TO_TICKS(2));
      if (_producedCounter) (*_producedCounter) += (uint64_t)len;
      return len;
    }

    size_t written = 0;
    while (written < len) {
      if (_cancelled) {
        vTaskDelay(pdMS_TO_TICKS(2));
        if (_producedCounter) (*_producedCounter) += (uint64_t)(len - written);
        return len;
      }
      
      // YIELD: This is the critical injection point!
      // The compression library runs hot. By forcing the producer task to yield 
      // every time it pushes a chunk to the ring buffer, the FreeRTOS scheduler 
      // guarantees the Core 3.x LwIP network stack is never starved.
      vTaskDelay(pdMS_TO_TICKS(2));

      size_t space = _cap - _used;
      if (space == 0) {
        if (_semSpace) xSemaphoreTake(_semSpace, pdMS_TO_TICKS(2000));
        continue;
      }
      size_t n = len - written;
      if (n > space) n = space;

      for (size_t i = 0; i < n; i++) {
        _buf[(_head + _used) % _cap] = data[written + i];
        _used++;
      }

      written += n;
      if (_semData) xSemaphoreGive(_semData);
    }
    if (_producedCounter) (*_producedCounter) += (uint64_t)written;
    return written;
  }

  size_t readBytes(uint8_t *buffer, size_t length) override {
    if (!ok()) return 0;

    size_t got = 0;
    while (got < length && !_cancelled) {
      if (_used == 0) {
        if (_finished) break;
        if (_semData) xSemaphoreTake(_semData, pdMS_TO_TICKS(2000));
        continue;
      }

      size_t n = length - got;
      if (n > _used) n = _used;

      for (size_t i = 0; i < n; i++) {
        buffer[got + i] = _buf[_head];
        _head = (_head + 1) % _cap;
        _used--;
      }

      got += n;
      if (_semSpace) xSemaphoreGive(_semSpace);
    }
    return got;
  }

private:
  size_t _cap;
  uint8_t *_buf;
  size_t _head = 0;
  volatile size_t _used = 0;

  SemaphoreHandle_t _semData = nullptr;
  SemaphoreHandle_t _semSpace = nullptr;

  volatile bool _finished = false;
  volatile bool _cancelled = false;
  volatile uint64_t* _producedCounter = nullptr;
};

struct TgzStreamSession {
  String srcPath;     // directory path to compress
  String outName;     // file name presented to client

  TgzRingStream* stream = nullptr;

  uint32_t jobId = 0;
  uint32_t startedMs = 0;
  uint32_t endedMs = 0;
  uint32_t noDataTimeoutMs = 30000UL;

  uint64_t estimatedBytes = 0;
  uint32_t estimatedFiles = 0;
  uint32_t estimatedDirs = 0;
  bool estimateTruncated = false;

  volatile uint8_t state = TGZ_STATE_STARTING;
  volatile bool producerStarted = false;
  volatile bool producerDone = false;
  volatile bool producerOk = false;

  volatile bool abortRequested = false;
  volatile bool clientDisconnected = false;

  volatile uint64_t bytesProduced = 0;
  volatile uint64_t bytesSent = 0;
};

static TgzStreamSession* g_activeTgzSession = nullptr;

static void scheduleTgzDelete(TgzStreamSession* s) {
  if (!s) return;

  xTaskCreate([](void* arg){
    TgzStreamSession* ss = (TgzStreamSession*)arg;
    vTaskDelay(pdMS_TO_TICKS(TGZ_DELETE_DELAY_MS));

    while (ss && !ss->producerDone) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    ensureTgzStatusMutex();
    if (g_tgzStatusMutex && xSemaphoreTake(g_tgzStatusMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (g_activeTgzSession == ss) {
        g_activeTgzSession = nullptr;
      }
      xSemaphoreGive(g_tgzStatusMutex);
    }

    if (ss) {
      if (ss->stream) { delete ss->stream; ss->stream = nullptr; }
      delete ss;
    }

    tgzInProgress = false;
    vTaskDelete(NULL);
    }, "tgzDel", 2048, s, 1, nullptr);
}

// Wrapper used by tgzProducerTask()
static bool tarGzStreamFolder(const char* srcDir, Stream* out) {
  if (!srcDir || !out) return false;

  // ESP32-targz signature: TarGzPacker::compress(fs::FS*, const char*, Stream*, const char* tar_prefix=nullptr)
  int rc = TarGzPacker::compress(&LittleFS, srcDir, out, nullptr);
  return (rc >= 0);
}

static void tgzProducerTask(void* pv) {
  TgzStreamSession* s = (TgzStreamSession*)pv;
  if (!s || !s->stream) {
    vTaskDelete(NULL);
    return;
  }

  s->producerStarted = true;
  s->state = TGZ_STATE_COMPRESSING;
  LOG_CAT(DBG_TARGZ, "[TGZ] producer start: src=%s\n", s->srcPath.c_str());
  tgzPrintMem("producer start");

  bool ok = false;

  if (!takeFileSystemMutexWithRetry("[FS] tgzProducer", 5000, 3)) {
    LOG_ERR("[TGZ] producer failed to lock FS mutex\n");
    s->state = TGZ_STATE_FAILED;
    ok = false;
  } else {
    LOG_CAT(DBG_TARGZ, "[TGZ] producer compress begin\n");
    ok = tarGzStreamFolder(s->srcPath.c_str(), s->stream);
    LOG_CAT(DBG_TARGZ, "[TGZ] producer compress end ok=%d abort=%d disconnected=%d\n",
            ok ? 1 : 0, s->abortRequested ? 1 : 0, s->clientDisconnected ? 1 : 0);
    xSemaphoreGive(fileSystemMutex);
  }

  s->producerOk = ok;
  s->producerDone = true;
  s->endedMs = millis();
  if (s->abortRequested || s->clientDisconnected) {
    s->state = TGZ_STATE_CANCELLED;
  } else {
    s->state = ok ? TGZ_STATE_COMPLETE : TGZ_STATE_FAILED;
  }
  s->stream->finish();

  tgzLastStackWords = TGZ_PRODUCER_TASK_STACK_WORDS;
  tgzLastHwmWords   = uxTaskGetStackHighWaterMark(nullptr);

  LOG_CAT(DBG_TARGZ, "[TGZ] producer done: hwm_words=%u\n", (unsigned)tgzLastHwmWords);

  thTgzProducer = NULL;
  tgzPrintMem("producer done");
  vTaskDelete(NULL);
}


static String tgzEstimateJson(const String& dir, bool ok, bool isDir, const TgzArchiveEstimate& est, const String& err = "") {
  uint32_t timeoutMs = tgzRecommendedNoDataTimeoutMs(est.bytes, est.files);
  String warning = "ok";
  if (est.bytes >= (1024ULL * 1024ULL * 1024ULL)) warning = "huge";
  else if (est.bytes >= (200ULL * 1024ULL * 1024ULL)) warning = "large";
  else if (est.bytes >= (50ULL * 1024ULL * 1024ULL)) warning = "medium";

  String json;
  json.reserve(320);
  json += "{\"ok\":"; json += ok ? "true" : "false";
  json += ",\"isDir\":"; json += isDir ? "true" : "false";
  json += ",\"path\":\""; json += jsonEscape(dir); json += "\"";
  json += ",\"bytes\":"; json += u64ToString(est.bytes);
  json += ",\"files\":"; json += String(est.files);
  json += ",\"dirs\":"; json += String(est.dirs);
  json += ",\"entries\":"; json += String(est.entries);
  json += ",\"truncated\":"; json += est.truncated ? "true" : "false";
  json += ",\"noDataTimeoutMs\":"; json += String(timeoutMs);
  json += ",\"warning\":\""; json += warning; json += "\"";
  if (err.length()) { json += ",\"error\":\""; json += jsonEscape(err); json += "\""; }
  json += "}";
  return json;
}

static bool estimateArchiveDir(const String& dir, TgzArchiveEstimate& est, bool& isDir, String& err,
                               TickType_t waitTicks = pdMS_TO_TICKS(5000), uint8_t retries = 3) {
  isDir = false;
  err = "";

  if (!takeFileSystemMutexWithRetry("[FS] tgzEstimate", waitTicks, retries)) {
    err = "FS busy";
    return false;
  }

  File df = LittleFS.open(dir, "r");
  isDir = (df && df.isDirectory());
  if (df) df.close();

  bool ok = false;
  if (isDir) {
    ok = estimatePathUnlocked(dir, est);
    if (!ok) err = "Estimate failed";
  } else {
    err = "Directory not found";
  }

  xSemaphoreGive(fileSystemMutex);
  return ok;
}

static void handleDownloadCompressedInfo(AsyncWebServerRequest* request) {
  TarGZ::begin();

  if (!g_fileSystemReady) {
    request->send(503, "application/json", "{\"ok\":false,\"error\":\"File system not ready\"}");
    return;
  }

  if (!request->hasParam("dir")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing dir\"}");
    return;
  }

  String dir = request->getParam("dir")->value();
  dir = normalizeTgzPath(dir, false);
  if (!isSafePath(dir)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Unsafe path\"}");
    return;
  }

  if (dir == "/Pump_Logs" || dir.startsWith("/Pump_Logs/")) {
    (void)flushPendingPumpLogEvents(pdMS_TO_TICKS(3000), 3);
  }

  TgzArchiveEstimate est;
  bool isDir = false;
  String err;
  bool ok = estimateArchiveDir(dir, est, isDir, err);
  request->send(ok ? 200 : 404, "application/json", tgzEstimateJson(dir, ok, isDir, est, err));
}

static void handleDownloadCompressedStatus(AsyncWebServerRequest* request) {
  ensureTgzStatusMutex();

  TgzStreamSession snapshot;
  bool active = false;

  if (g_tgzStatusMutex && xSemaphoreTake(g_tgzStatusMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (g_activeTgzSession) {
      TgzStreamSession* s = g_activeTgzSession;
      snapshot.jobId = s->jobId;
      snapshot.srcPath = s->srcPath;
      snapshot.outName = s->outName;
      snapshot.startedMs = s->startedMs;
      snapshot.endedMs = s->endedMs;
      snapshot.noDataTimeoutMs = s->noDataTimeoutMs;
      snapshot.estimatedBytes = s->estimatedBytes;
      snapshot.estimatedFiles = s->estimatedFiles;
      snapshot.estimatedDirs = s->estimatedDirs;
      snapshot.estimateTruncated = s->estimateTruncated;
      snapshot.state = s->state;
      snapshot.producerStarted = s->producerStarted;
      snapshot.producerDone = s->producerDone;
      snapshot.producerOk = s->producerOk;
      snapshot.abortRequested = s->abortRequested;
      snapshot.clientDisconnected = s->clientDisconnected;
      snapshot.bytesProduced = s->bytesProduced;
      snapshot.bytesSent = s->bytesSent;
      active = true;
    }
    xSemaphoreGive(g_tgzStatusMutex);
  }

  uint32_t now = millis();
  String json;
  json.reserve(520);
  json += "{\"active\":"; json += active ? "true" : "false";
  json += ",\"tgzInProgress\":"; json += tgzInProgress ? "true" : "false";
  if (active) {
    uint32_t elapsed = snapshot.startedMs ? (uint32_t)(now - snapshot.startedMs) : 0;
    json += ",\"jobId\":"; json += String(snapshot.jobId);
    json += ",\"state\":\""; json += String(tgzStateName(snapshot.state)); json += "\"";
    json += ",\"path\":\""; json += jsonEscape(snapshot.srcPath); json += "\"";
    json += ",\"fileName\":\""; json += jsonEscape(snapshot.outName); json += "\"";
    json += ",\"elapsedMs\":"; json += String(elapsed);
    json += ",\"estimatedBytes\":"; json += u64ToString(snapshot.estimatedBytes);
    json += ",\"estimatedFiles\":"; json += String(snapshot.estimatedFiles);
    json += ",\"estimatedDirs\":"; json += String(snapshot.estimatedDirs);
    json += ",\"estimateTruncated\":"; json += snapshot.estimateTruncated ? "true" : "false";
    json += ",\"bytesProduced\":"; json += u64ToString(snapshot.bytesProduced);
    json += ",\"bytesSent\":"; json += u64ToString(snapshot.bytesSent);
    json += ",\"noDataTimeoutMs\":"; json += String(snapshot.noDataTimeoutMs);
    json += ",\"abortRequested\":"; json += snapshot.abortRequested ? "true" : "false";
    json += ",\"clientDisconnected\":"; json += snapshot.clientDisconnected ? "true" : "false";
    json += ",\"producerDone\":"; json += snapshot.producerDone ? "true" : "false";
    json += ",\"producerOk\":"; json += snapshot.producerOk ? "true" : "false";
  } else {
    json += ",\"state\":\"idle\"";
  }
  json += ",\"pendingPumpLogEvents\":"; json += String((unsigned)getPendingPumpLogEventCount());
  json += "}";
  request->send(200, "application/json", json);
}

static void handleDownloadCompressedCancel(AsyncWebServerRequest* request) {
  ensureTgzStatusMutex();
  bool cancelled = false;
  uint32_t jobId = 0;

  if (g_tgzStatusMutex && xSemaphoreTake(g_tgzStatusMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (g_activeTgzSession) {
      TgzStreamSession* s = g_activeTgzSession;
      s->abortRequested = true;
      s->clientDisconnected = true;
      s->state = TGZ_STATE_DRAINING_AFTER_CANCEL;
      if (s->stream) s->stream->cancel();
      cancelled = true;
      jobId = s->jobId;
    }
    xSemaphoreGive(g_tgzStatusMutex);
  }

  String json = "{\"ok\":";
  json += cancelled ? "true" : "false";
  json += ",\"jobId\":"; json += String(jobId);
  json += cancelled ? ",\"message\":\"Archive cancel requested\"}" : ",\"message\":\"No active archive\"}";
  request->send(cancelled ? 200 : 404, "application/json", json);
}

static void handleDownloadCompressed(AsyncWebServerRequest* request) {
  TarGZ::begin();

  if (!g_fileSystemReady) {
    request->send(503, "text/plain", "File system not ready");
    return;
  }

  if (tgzInProgress) {
    request->send(429, "text/plain", "A compressed download is already in progress");
    return;
  }

  if (!request->hasParam("dir")) {
    request->send(400, "text/plain", "Missing dir");
    return;
  }

  String dir = request->getParam("dir")->value();
  dir = normalizeTgzPath(dir, false);
  if (!isSafePath(dir)) {
    request->send(400, "text/plain", "Unsafe path");
    return;
  }

  // Pump log archives must include any RAM-cached START/STOP events before
  // the directory is compressed.  This is intentionally best-effort: if a huge
  // archive is already using the FS, the request will still fail via tgzInProgress/FS busy.
  if (dir == "/Pump_Logs" || dir.startsWith("/Pump_Logs/")) {
    (void)flushPendingPumpLogEvents(pdMS_TO_TICKS(3000), 3);
  }

  TgzArchiveEstimate est;
  bool isDir = false;
  String estErr;
  bool estOk = estimateArchiveDir(dir, est, isDir, estErr);

  if (!isDir) {
    request->send(404, "text/plain", "Directory not found");
    return;
  }

  if (!estOk) {
    LOG_ERR("[TGZ] archive estimate failed for %s: %s. Continuing with safe defaults.\n",
            dir.c_str(), estErr.c_str());
  }

  String base = dir.substring(dir.lastIndexOf('/') + 1);
  if (base.length() == 0) base = "download";
  String filename = base + ".tar.gz";
/*
// Regular function without diagnostic Serial Prints
  auto *session = new TgzStreamSession();
  session->srcPath = dir;
  session->outName = filename;

  session->stream = new TgzRingStream(TGZ_RINGBUF_BYTES);
  if (!session->stream || !session->stream->ok()) {
    delete session->stream;
    delete session;
    request->send(500, "text/plain", "Unable to allocate TGZ ring buffer");
    return;
  }

  tgzInProgress = true;
*/
// includes diagnostic troubleshooting serial prints
  auto *session = new TgzStreamSession();
  session->srcPath = dir;
  session->outName = filename;
  session->jobId = g_tgzNextJobId++;
  session->startedMs = millis();
  session->state = TGZ_STATE_STARTING;
  session->estimatedBytes = est.bytes;
  session->estimatedFiles = est.files;
  session->estimatedDirs = est.dirs;
  session->estimateTruncated = est.truncated;
  session->noDataTimeoutMs = tgzRecommendedNoDataTimeoutMs(est.bytes, est.files);

  const size_t ringBytes = tgzEffectiveRingBytes();
  const size_t heapLfb   = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  const size_t psramLfb  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

  // -------------------------------------------------------------------
  // -------------------- TGZ alloc diagnostics ------------------------
  // -------------------------------------------------------------------
  LOG_CAT(DBG_TARGZ, "[TGZ] request ring=%u bytes (%s), cache_location=%d\n",
          (unsigned)ringBytes,
          tgzEffectiveRingLocationName(),
          (int)TGZ_RING_CACHE_LOCATION);

  LOG_CAT(DBG_TARGZ, "[TGZ] heap free=%u lfb=%u | psram free=%u lfb=%u\n",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
          (unsigned)heapLfb,
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          (unsigned)psramLfb);

  if (!tgzRingUsePsramEffective()) {
    const size_t kExpectedLz77Need = 16 * 1024;
    LOG_CAT(DBG_TARGZ,
            "[TGZ] no-PSRAM mode: reserving compressor workspace ~%u bytes, post-ring headroom estimate=%d bytes\n",
            (unsigned)kExpectedLz77Need,
            (int)((int)heapLfb - (int)ringBytes - (int)kExpectedLz77Need));
  }
  // --------------------------------------------------------------------

  session->stream = new TgzRingStream(ringBytes);
  if (session->stream) session->stream->setProducedCounter(&session->bytesProduced);
  if (!session->stream || !session->stream->ok()) {
    LOG_ERR("[TGZ] Ring allocation failed. requested=%u location=%s heap_lfb=%u psram_lfb=%u\n",
            (unsigned)ringBytes,
            tgzEffectiveRingLocationName(),
            (unsigned)heapLfb,
            (unsigned)psramLfb);

    delete session->stream;
    delete session;

    String err = "Unable to allocate TGZ ring buffer. requested=" + String((unsigned)ringBytes) +
                 " location=" + String(tgzEffectiveRingLocationName()) +
                 " heap_lfb=" + String((unsigned)heapLfb) +
                 " psram_lfb=" + String((unsigned)psramLfb);
    request->send(500, "text/plain", err);
    return;
  }

  ensureTgzStatusMutex();
  if (g_tgzStatusMutex && xSemaphoreTake(g_tgzStatusMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    g_activeTgzSession = session;
    xSemaphoreGive(g_tgzStatusMutex);
  }

  tgzInProgress = true;




  // Stream response
  AsyncWebServerResponse *response =
    request->beginChunkedResponse("application/gzip",
      [session](uint8_t *outBuf, size_t maxLen, size_t index) -> size_t {

        if (!session || !session->stream) return 0;
        if (session->abortRequested) return 0;

                // Read directly into the provided output buffer (no staging allocation).
        size_t want = maxLen;

        // In no-PSRAM mode, use smaller HTTP chunks to reduce internal-heap pressure
        // and make long downloads more tolerant of tight memory.
        size_t effectiveChunk = tgzRingUsePsramEffective() ? TGZ_HTTP_CHUNK_BYTES : 1024;

        if (want > effectiveChunk) want = effectiveChunk;

        // IMPORTANT:
        // Do not return 0 just because the producer has not emitted data yet.
        // Returning 0 tells AsyncWebServer that the response is complete. For large
        // directories, ESP32-targz can spend several seconds walking/opening files before
        // it writes the next gzip bytes. A premature 0 closes the browser connection,
        // triggers onDisconnect(), and can make ESP32-targz abort inside the producer.
        const uint32_t waitStart = millis();
        while (!session->stream->done() && !session->abortRequested) {
          size_t got = session->stream->readBytes(outBuf, want);
          if (got > 0) {
            session->state = TGZ_STATE_STREAMING;
            session->bytesSent += (uint64_t)got;
            return got;
          }

          if (session->stream->done()) {
            return 0;
          }

          if (millis() - waitStart > session->noDataTimeoutMs) {
            LOG_ERR("[TGZ] HTTP chunk timed out waiting for producer data; cancelling archive stream timeout=%u ms\n",
                    (unsigned)session->noDataTimeoutMs);
            session->abortRequested = true;
            session->state = TGZ_STATE_DRAINING_AFTER_CANCEL;
            if (session->stream) session->stream->cancel();
            return 0;
          }

          vTaskDelay(pdMS_TO_TICKS(10));
        }

        return 0;
      });

  response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");

    // Disconnect cleanup (do NOT delete here; delete is scheduled once after send())
  request->onDisconnect([session](){
    if (!session) return;
    session->abortRequested = true;
    session->clientDisconnected = true;
    session->state = TGZ_STATE_DRAINING_AFTER_CANCEL;
    if (session->stream) session->stream->cancel();
  });

  // Spawn producer task
  TaskHandle_t tmpHandle = NULL;

  BaseType_t ok = spawnTaskOptionalCore(
    tgzProducerTask,
    "tgzProducer",
    TGZ_PRODUCER_TASK_STACK_WORDS,
    session,
    3,
    &tmpHandle,
    TGZ_PRODUCER_TASK_CORE
  );

  if (ok != pdPASS) {
    session->abortRequested = true;
    session->state = TGZ_STATE_FAILED;
    if (session->stream) session->stream->cancel();
    ensureTgzStatusMutex();
    if (g_tgzStatusMutex && xSemaphoreTake(g_tgzStatusMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      if (g_activeTgzSession == session) g_activeTgzSession = nullptr;
      xSemaphoreGive(g_tgzStatusMutex);
    }
    scheduleTgzDelete(session);
    request->send(500, "text/plain", "Failed to start TGZ producer task");
    return;
  }

  thTgzProducer = tmpHandle;

  request->send(response);

  // Cleanup after response is done (delay task waits on producerDone)
  scheduleTgzDelete(session);
}

void TarGZ::registerRoutes(AsyncWebServer &server) {
  TarGZ::begin();
  server.on("/fs/download_compressed", HTTP_GET, handleDownloadCompressed);
  server.on("/fs/download_compressed/info", HTTP_GET, handleDownloadCompressedInfo);
  server.on("/fs/download_compressed/status", HTTP_GET, handleDownloadCompressedStatus);
  server.on("/fs/download_compressed/cancel", HTTP_POST, handleDownloadCompressedCancel);
}
