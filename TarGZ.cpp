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
  LOG_CAT(DBG_TARGZ, "[TGZ] producer start: src=%s\n", s->srcPath.c_str());
  tgzPrintMem("producer start");

  bool ok = false;

  if (!takeFileSystemMutexWithRetry("[FS] tgzProducer", 5000, 3)) {
    LOG_ERR("[TGZ] producer failed to lock FS mutex\n");
    ok = false;
  } else {
    LOG_CAT(DBG_TARGZ, "[TGZ] producer compress begin\n");
    ok = tarGzStreamFolder(s->srcPath.c_str(), s->stream);
    LOG_CAT(DBG_TARGZ, "[TGZ] producer compress end ok=%d\n", ok ? 1 : 0);
    xSemaphoreGive(fileSystemMutex);
  }

  s->producerOk = ok;
  s->producerDone = true;
  s->stream->finish();

  tgzLastStackWords = TGZ_PRODUCER_TASK_STACK_WORDS;
  tgzLastHwmWords   = uxTaskGetStackHighWaterMark(nullptr);

  LOG_CAT(DBG_TARGZ, "[TGZ] producer done: hwm_words=%u\n", (unsigned)tgzLastHwmWords);

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
