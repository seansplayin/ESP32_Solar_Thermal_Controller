// TarGZ.cpp
#include "TarGZ.h"
#include "FileSystemManager.h"
#include "TaskManager.h"
#include <LittleFS.h>
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "DiagLog.h"





// ===== On-the-fly tar.gz streaming support (ring buffer in Internal Heap or PSRAM) =====
static const bool kTgzDebug = false;
TaskHandle_t thTgzProducer = NULL;
volatile uint32_t tgzLastStackWords = 0;
volatile uint32_t tgzLastHwmWords   = 0;
static volatile bool tgzInProgress = false;

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

void TarGZ::begin() {
  if (g_beginDone) return;
  g_beginDone = true;

  g_psramAvailable = tgzPsramAvailable();

    // Startup warning if user requested PSRAM but PSRAM isn't enabled/available
    if (TGZ_RING_CACHE_LOCATION == 1 && !g_psramAvailable) {
    LOG_CAT(DBG_TARGZ,
      "[TGZ] WARNING: TGZ_RING_CACHE_LOCATION=1 (PSRAM) but PSRAM is NOT available. Falling back to Internal Heap ring buffer.\n");
  }

  // Warn if ring buffer is below the recommended minimum
  if (TGZ_RINGBUF_BYTES < (256 * 1024)) {
    LOG_CAT(DBG_TARGZ,
      "[TGZ] WARNING: TarGZ using heap (internal memory) ring buffer size smaller that 256 KB downloads may be unstable\n");
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
    if (!ok() || _cancelled) return 0;

    size_t written = 0;
    while (written < len && !_cancelled) {
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
};

struct TgzStreamSession {
  String srcPath;     // directory path to compress
  String outName;     // file name presented to client

  TgzRingStream* stream = nullptr;

  volatile bool producerStarted = false;
  volatile bool producerDone = false;
  volatile bool producerOk = false;

  volatile bool abortRequested = false;

  volatile uint32_t bytesProduced = 0;
};

static void scheduleTgzDelete(TgzStreamSession* s) {
  if (!s) return;

  xTaskCreate([](void* arg){
    TgzStreamSession* ss = (TgzStreamSession*)arg;
    vTaskDelay(pdMS_TO_TICKS(TGZ_DELETE_DELAY_MS));

    while (ss && !ss->producerDone) {
      vTaskDelay(pdMS_TO_TICKS(100));
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
  tgzPrintMem("producer start");

  bool ok = false;

  if (!takeFileSystemMutexWithRetry("[FS] tgzProducer", 5000, 3)) {
    ok = false;
  } else {
    ok = tarGzStreamFolder(s->srcPath.c_str(), s->stream);
    xSemaphoreGive(fileSystemMutex);
  }

  s->producerOk = ok;
  s->producerDone = true;
  s->stream->finish();

  tgzLastStackWords = TGZ_PRODUCER_TASK_STACK_WORDS;
  tgzLastHwmWords   = uxTaskGetStackHighWaterMark(nullptr);

  thTgzProducer = NULL;
  tgzPrintMem("producer done");
  vTaskDelete(NULL);
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
  if (!dir.startsWith("/")) dir = "/" + dir;
  if (!isSafePath(dir)) {
    request->send(400, "text/plain", "Unsafe path");
    return;
  }

  // Validate directory existence under mutex
  bool isDir = false;
  if (!takeFileSystemMutexWithRetry("[FS] tgzCheck", 5000, 3)) {
    request->send(503, "text/plain", "FS busy");
    return;
  } else {
    File df = LittleFS.open(dir, "r");
    isDir = (df && df.isDirectory());
    if (df) df.close();
    xSemaphoreGive(fileSystemMutex);
  }

  if (!isDir) {
    request->send(404, "text/plain", "Directory not found");
    return;
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

  // -------------------------------------------------------------------
  // -------------------- TGZ alloc diagnostics ------------------------
  // -------------------------------------------------------------------
    LOG_CAT(DBG_TARGZ, "[TGZ] request ring=%u bytes (%s), cache_location=%d\n",
          (unsigned)TGZ_RINGBUF_BYTES,
          (TGZ_RING_CACHE_LOCATION == 1) ? "PSRAM" : "INTERNAL HEAP",
          (int)TGZ_RING_CACHE_LOCATION);

  LOG_CAT(DBG_TARGZ, "[TGZ] heap free=%u lfb=%u | psram free=%u lfb=%u\n",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  // --------------------------------------------------------------------

  // --------------------------------------------------------------------

  session->stream = new TgzRingStream(TGZ_RINGBUF_BYTES);
  if (!session->stream || !session->stream->ok()) {
    delete session->stream;
    delete session;
    request->send(500, "text/plain", "Unable to allocate TGZ ring buffer");
    return;
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
        if (want > TGZ_HTTP_CHUNK_BYTES) want = TGZ_HTTP_CHUNK_BYTES;

        size_t got = session->stream->readBytes(outBuf, want);
        if (got > 0) {
          return got;
        }


        if (session->stream->done()) {
          return 0;
        }

        return 0;
      });

  response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");

    // Disconnect cleanup (do NOT delete here; delete is scheduled once after send())
  request->onDisconnect([session](){
    if (!session) return;
    session->abortRequested = true;
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
    if (session->stream) session->stream->cancel();
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
}
