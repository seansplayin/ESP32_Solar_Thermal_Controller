#include "ThirdWebpage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "RTCManager.h"
#include "TemperatureLogging.h"
#include "Config.h"
#include "WebServerManager.h"
#include "FileSystemManager.h"
#include <esp_heap_caps.h>  // For MALLOC_CAP_SPIRAM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "TaskManager.h"
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>  



extern AsyncWebServer server;



// ===== On-the-fly tar.gz streaming support (PSRAM ring buffer) =====
class TgzRingStream : public Stream {
public:
  explicit TgzRingStream(size_t capacityBytes)
  : _cap(capacityBytes) {

    _buf = (uint8_t*)heap_caps_malloc(_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_buf) memset(_buf, 0, _cap);

    _dataSem  = xSemaphoreCreateBinary(); // signaled when data becomes available
    _spaceSem = xSemaphoreCreateBinary(); // signaled when space becomes available
  }

  ~TgzRingStream() override {
    if (_buf) heap_caps_free(_buf);
    if (_dataSem) vSemaphoreDelete(_dataSem);
    if (_spaceSem) vSemaphoreDelete(_spaceSem);
  }

  bool ok() const { return _buf && _dataSem && _spaceSem; }

  void cancel() {
    _cancel = true;
    // wake any waiters
    if (_dataSem)  xSemaphoreGive(_dataSem);
    if (_spaceSem) xSemaphoreGive(_spaceSem);
  }

  void finish(bool success) {
    _success = success;
    _done = true;
    if (_dataSem) xSemaphoreGive(_dataSem); // wake reader so it can return 0 (EOF)
  }

  bool done() const { return _done; }
  bool success() const { return _success; }
  bool canceled() const { return _cancel; }

  // ---- Stream write side (TarGzPacker writes here) ----
  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t *data, size_t len) override {
    if (!ok() || !data || len == 0) return 0;

    size_t written = 0;
    while (written < len) {
      if (_cancel) return 0;

      // Wait until there's at least 1 byte of space
      while (!_cancel && freeSpaceUnsafe() == 0) {
        // wait for reader to free space
        xSemaphoreTake(_spaceSem, pdMS_TO_TICKS(25));
      }
      if (_cancel) return 0;

      // Copy chunk into ring (may wrap)
      portENTER_CRITICAL(&_mux);
      size_t freeNow = _cap - _used;
      size_t want = len - written;
      size_t n = (want < freeNow) ? want : freeNow;

      // first segment to end of buffer
      size_t toEnd = _cap - _wpos;
      size_t first = (n < toEnd) ? n : toEnd;

      memcpy(_buf + _wpos, data + written, first);
      _wpos = (_wpos + first) % _cap;
      _used += first;
      written += first;

      // second segment wrap
      size_t remaining = n - first;
      if (remaining) {
        memcpy(_buf + _wpos, data + written, remaining);
        _wpos = (_wpos + remaining) % _cap;
        _used += remaining;
        written += remaining;
      }
      portEXIT_CRITICAL(&_mux);

      // notify reader
      xSemaphoreGive(_dataSem);
    }

    return len;
  }

  // ---- Stream read side (AsyncWebServer chunk callback reads here) ----
  int available() override {
    portENTER_CRITICAL(&_mux);
    int a = (int)_used;
    portEXIT_CRITICAL(&_mux);
    return a;
  }

  int read() override {
    uint8_t b;
    return (readBytes(&b, 1) == 1) ? b : -1;
  }

  int peek() override { return -1; }
  void flush() override {}

  size_t readBytes(uint8_t *out, size_t maxLen) override {
    if (!ok() || !out || maxLen == 0) return 0;

    while (true) {
      if (_cancel) return 0;

      portENTER_CRITICAL(&_mux);
      size_t usedNow = _used;
      portEXIT_CRITICAL(&_mux);

      if (usedNow > 0) break;

      // No data available:
      // - If producer finished, we're done (EOF)
      if (_done) return 0;

      // Wait briefly for producer to write data
      xSemaphoreTake(_dataSem, pdMS_TO_TICKS(25));
    }

    // Pull up to maxLen from ring (may wrap)
    portENTER_CRITICAL(&_mux);
    size_t n = (_used < maxLen) ? _used : maxLen;

    size_t toEnd = _cap - _rpos;
    size_t first = (n < toEnd) ? n : toEnd;

    memcpy(out, _buf + _rpos, first);
    _rpos = (_rpos + first) % _cap;
    _used -= first;

    size_t remaining = n - first;
    if (remaining) {
      memcpy(out + first, _buf + _rpos, remaining);
      _rpos = (_rpos + remaining) % _cap;
      _used -= remaining;
    }
    portEXIT_CRITICAL(&_mux);

    // notify producer space is available
    xSemaphoreGive(_spaceSem);
    return n;
  }

private:
  size_t freeSpaceUnsafe() const {
    // call only when not in critical section if you can tolerate minor race,
    // otherwise wrap with critical section externally
    size_t usedNow;
    portENTER_CRITICAL(&_mux);
    usedNow = _used;
    portEXIT_CRITICAL(&_mux);
    return _cap - usedNow;
  }

  uint8_t *_buf = nullptr;
  const size_t _cap;

  // Ring state
  size_t _rpos = 0;
  size_t _wpos = 0;
  size_t _used = 0;

  // Signaling
  SemaphoreHandle_t _dataSem  = nullptr;
  SemaphoreHandle_t _spaceSem = nullptr;

  // Concurrency
  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

  // State flags
  volatile bool _done = false;
  volatile bool _success = false;
  volatile bool _cancel = false;
};

struct TgzStreamSession {
  explicit TgzStreamSession(size_t ringBytes)
  : stream(ringBytes) {}

  TgzRingStream stream;
  String srcPath;                 // directory (ending with '/')
  TaskHandle_t producerTask = nullptr;

  volatile bool producerFinished = false;
  volatile bool deleteWhenProducerDone = false;
};

static void tgzProducerTask(void *pv) {
  TgzStreamSession *s = (TgzStreamSession*)pv;

  // Hold FS mutex during compression so LittleFS access is serialized
  if (!takeFileSystemMutexWithRetry("[FS] tgzProducer",
                                    pdMS_TO_TICKS(60000), 1)) {
    s->stream.finish(false);
    s->producerFinished = true;
    if (s->deleteWhenProducerDone) delete s;

    // Capture last-run stack watermark just before task exits
    tgzLastStackWords = (uint32_t)TGZ_PRODUCER_TASK_STACK_BYTES;
    tgzLastHwmWords   = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    // Clear exported handle (only if it still points to us)
      if (thTgzProducer == xTaskGetCurrentTaskHandle()) thTgzProducer = NULL;
      vTaskDelete(nullptr);
          return;
  }

  // Compress directory to ring stream (writer blocks if ring is full)
  size_t outBytes = TarGzPacker::compress(&tarGzFS, s->srcPath.c_str(), &s->stream);

  xSemaphoreGive(fileSystemMutex);

  // Mark done (even if client disconnected)
  s->stream.finish(outBytes > 0 && !s->stream.canceled());

    s->producerFinished = true;

    // Capture last-run stack watermark just before task exits
    tgzLastStackWords = (uint32_t)TGZ_PRODUCER_TASK_STACK_BYTES;
    tgzLastHwmWords   = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

  // Clear monitor handle if it points to this task
  if (thTgzProducer == xTaskGetCurrentTaskHandle()) {
    thTgzProducer = NULL;
  }

  if (s->deleteWhenProducerDone) delete s;
  vTaskDelete(nullptr);
}



static const char* thirdPageHtml = R"rawliteral(
<!doctype html>
<html>
<head>
  <link rel="icon" href="/favicon.png">
  <title>Temperature Logs</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial;
      text-align: center;
      margin: 0;
      padding: 0;
    }

    h3 {
      color: #459;
      font-size: 40px;
      line-height: 100%;
      font-weight: bold;
      text-align: center;
    }
    h3.top-heading {
      margin-top: 0.3em;
      margin-bottom: 0.7em;
    }
    h3.top-heading a {
      color: #459;
      text-decoration: none;
    }
    h3.top-heading a:hover {
      text-decoration: underline;
    }

    .blue-button {
      background-color: white;
      color: blue;
      padding: 0px 4px;
      font-size: 14px;
      cursor: pointer;
      border: 1px solid blue;
      border-radius: 3px;
    }
    .blue-button:hover {
      background-color: darkblue;
      color: white;
    }
    .blue-button:focus {
      outline: 2px solid rgba(0,0,255,0.6);
      outline-offset: 2px;
    }

    select, button { padding: 6px; margin: 4px; }

    #graphContainer {
      width: 100%;
      height: 600px;
      margin: 20px 0;
      position: relative;
    }
    #graphCanvas {
      width: 100%;
      height: 100%;
      border: 1px solid #ccc;
      background: #fff;
      display: block;
      margin: 0 auto;
    }

    #fileBrowser {
      margin-top: 20px;
      border: 1px solid #ccc;
      padding: 10px;
      display: none;
      width: fit-content;
      margin: 0 auto;
    }
    #fileBrowser ul {
      list-style-type: none;
      padding: 0;
      margin: 0;
    }
    #fileBrowser li {
      padding: 5px;
      display: flex;
      align-items: center;
    }
    #fileBrowser li.dir {
      font-weight: bold;
      color: #0066cc;
      cursor: pointer;
    }
    #fileBrowser li.file {
      color: blue;
      cursor: pointer;
    }
    #fileBrowser li.file span:hover {
      text-decoration: underline;
    }
    #fileBrowser button {
      margin-left: 10px;
      font-size: 12px;
      padding: 3px 6px;
    }
    #fileContent {
      white-space: pre-wrap;
      background: #f8f8f8;
      padding: 10px;
      margin-top: 10px;
      border: 1px solid #ccc;
      display: none;
    }

    #tooltip {
      position: absolute;
      background: white;
      border: 1px solid #999;
      padding: 4px 8px;
      pointer-events: none;
      font-size: 14px;
      display: none;
      z-index: 10;
      box-shadow: 2px 2px 5px rgba(0,0,0,0.3);
    }
  </style>
</head>
<body>
  <h3 class="top-heading">
    <a id="temperatureLogsLink" target="_blank">Temperature Logs</a>
  </h3>

  <div>
    <label>Sensor:</label>
    <select id="sensorSelect"></select>

    <label>Day:</label>
    <input type="date" id="daySelect">
  </div>

  <!-- Reset button ABOVE graph -->
  <button id="resetGraphButton" class="blue-button">Reset Graph</button>

  <div id="graphContainer">
    <canvas id="graphCanvas"></canvas>
    <div id="tooltip"></div>
  </div>

  <!-- Flash browser button BELOW graph -->
  <button id="browserToggleBtn" class="blue-button">Flash Memory Browser</button>

  <div id="fileBrowser">
    <h3>Flash Memory Browser</h3>
    <button onclick="navigate('/')">Root</button>
    <button onclick="goUp()">.. (Up)</button>
    <button onclick="document.getElementById('uploadInput').click()">Upload to Current</button>
    <input type="file" id="uploadInput" style="display:none;" onchange="uploadFile(this.files)">
    <button onclick="createFolder()">Create Folder</button>
    <p>Current path: <span id="currentPath">/</span></p>
    <ul id="fileList"></ul>
    <div id="fileContent"></div>
  </div>



  <script>

let currentPath = '/';

fetch('/hello?from=ThirdWebpage').catch(()=>{});

// Stores everything needed for hover crosshair/tooltip
let graphState = null;
let tooltipEl = null;

async function fetchSensors() {
  const r = await fetch('/temperature-logs/sensors');
  const arr = await r.json();
  const sel = document.getElementById('sensorSelect');
  sel.innerHTML = '';
  arr.forEach(s => {
    const opt = document.createElement('option');
    opt.value = s;
    opt.textContent = s;
    sel.appendChild(opt);
  });
}

async function loadGraph() {
  const sensor = document.getElementById('sensorSelect').value;
  const day    = document.getElementById('daySelect').value;

  if (!sensor || !day) {
    alert("Select sensor and day");
    return;
  }

  const r = await fetch(
    `/temperature-logs/graph?sensor=${encodeURIComponent(sensor)}&day=${day}`
  );
  const data = await r.json();

  drawGraph(data);
}

function drawGraph(rawPoints) {
  const canvas = document.getElementById("graphCanvas");
  const ctx    = canvas.getContext("2d");

  // Match drawing size to display size
  const rect = canvas.getBoundingClientRect();
  canvas.width  = rect.width;
  canvas.height = rect.height;

  ctx.clearRect(0, 0, canvas.width, canvas.height);
  graphState = null;  // reset previous hover state

  if (!rawPoints || !rawPoints.length) {
    ctx.font = "16px Arial";
    ctx.fillStyle = "black";
    ctx.fillText("No data available", 20, 30);
    return;
  }

  // Sort by time string "HH:MM:SS"
  const points = rawPoints.slice().sort((a, b) => a.time.localeCompare(b.time));

  // Convert "HH:MM:SS" → minutes since midnight
  points.forEach(p => {
    const parts = p.time.split(':').map(Number);
    const hh = parts[0] || 0;
    const mm = parts[1] || 0;
    p.minutes = hh * 60 + mm;
  });

  // X axis full 24 hours
  const dayStartMinutes = 0;
  const dayEndMinutes   = 24 * 60;
  const totalMinutes    = dayEndMinutes - dayStartMinutes;

  // Y axis auto-scale + padding
  let minV = Math.min(...points.map(p => p.value));
  let maxV = Math.max(...points.map(p => p.value));
  const pad = 1;
  minV = Math.floor(minV - pad);
  maxV = Math.ceil(maxV + pad);
  if (maxV <= minV) maxV = minV + 1;

  const marginLeft   = 70;
  const marginRight  = 20;
  const marginTop    = 30;
  const marginBottom = 60;

  const width  = canvas.width  - marginLeft - marginRight;
  const height = canvas.height - marginTop  - marginBottom;

  function xFromMinutes(m) {
    return marginLeft + ((m - dayStartMinutes) / totalMinutes) * width;
  }
  function yFromValue(v) {
    return canvas.height - marginBottom
           - ((v - minV) / (maxV - minV)) * height;
  }

  ctx.font = "12px Arial";
  ctx.strokeStyle = "black";
  ctx.fillStyle   = "black";

  // Y axis line
  ctx.beginPath();
  ctx.moveTo(marginLeft, marginTop);
  ctx.lineTo(marginLeft, canvas.height - marginBottom);
  ctx.stroke();

  // Vertical "Temperature (°F)" label
  ctx.save();
  ctx.translate(15, canvas.height / 2);
  ctx.rotate(-Math.PI / 2);
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText("Temperature (\u00B0F)", 0, 0);
  ctx.restore();

  // Y ticks & grid
  for (let t = Math.ceil(minV); t <= maxV; t++) {
    const y = yFromValue(t);

    // tick
    ctx.beginPath();
    ctx.moveTo(marginLeft - 5, y);
    ctx.lineTo(marginLeft, y);
    ctx.stroke();

    // grid line
    ctx.strokeStyle = "#dddddd";
    ctx.beginPath();
    ctx.moveTo(marginLeft, y);
    ctx.lineTo(canvas.width - marginRight, y);
    ctx.stroke();
    ctx.strokeStyle = "black";

    // label
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    ctx.fillText(t.toString(), marginLeft - 8, y);
  }

  // Min/Max text
  ctx.textAlign = "left";
  ctx.textBaseline = "alphabetic";
  ctx.fillText(
    "Min: " + minV.toFixed(1) + "\u00B0F",
    marginLeft,
    marginTop - 10
  );
  ctx.fillText(
    "Max: " + maxV.toFixed(1) + "\u00B0F",
    marginLeft + 150,
    marginTop - 10
  );

  // X axis line
  ctx.beginPath();
  ctx.moveTo(marginLeft, canvas.height - marginBottom);
  ctx.lineTo(canvas.width - marginRight, canvas.height - marginBottom);
  ctx.stroke();

  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  ctx.fillText(
    "Time of day",
    marginLeft + width / 2,
    canvas.height - marginBottom + 30
  );

  // Hour marks 0..24
  for (let h = 0; h <= 24; h++) {
    const minutes = h * 60;
    const x = xFromMinutes(minutes);

    const label =
      h === 0 || h === 24
        ? "12:00AM"
        : h === 12
          ? "12:00PM"
          : (h % 12 || 12) + ":00" + (h < 12 ? "AM" : "PM");

    // main tick
    ctx.beginPath();
    ctx.moveTo(x, canvas.height - marginBottom);
    ctx.lineTo(x, canvas.height - marginBottom + 10);
    ctx.stroke();

    // label
    ctx.fillText(label, x, canvas.height - marginBottom + 12);
  }

  // 30-minute ticks
  ctx.strokeStyle = "#888888";
  for (let h = 0.5; h < 24; h++) {
    const minutes = h * 60;
    const x = xFromMinutes(minutes);
    ctx.beginPath();
    ctx.moveTo(x, canvas.height - marginBottom);
    ctx.lineTo(x, canvas.height - marginBottom + 6);
    ctx.stroke();
  }

  // 15-minute ticks
  ctx.strokeStyle = "#cccccc";
  for (let h = 0.25; h < 24; h += 0.5) {
    const minutes = h * 60;
    const x = xFromMinutes(minutes);
    ctx.beginPath();
    ctx.moveTo(x, canvas.height - marginBottom);
    ctx.lineTo(x, canvas.height - marginBottom + 3);
    ctx.stroke();
  }

  // Data polyline (also store screen coords for hover)
  ctx.strokeStyle = "blue";
  ctx.lineWidth = 2;
  ctx.beginPath();
  points.forEach((p, i) => {
    p._x = xFromMinutes(p.minutes);
    p._y = yFromValue(p.value);
    if (i === 0) ctx.moveTo(p._x, p._y);
    else ctx.lineTo(p._x, p._y);
  });
  ctx.stroke();

  // Save base image + geometry for lightweight hover rendering
  graphState = {
    points,
    minV,
    maxV,
    marginLeft,
    marginRight,
    marginTop,
    marginBottom,
    dayStartMinutes,
    totalMinutes,
    canvasWidth: canvas.width,
    canvasHeight: canvas.height,
    baseImage: ctx.getImageData(0, 0, canvas.width, canvas.height)
  };
}

function clearGraph() {
  const canvas = document.getElementById('graphCanvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  const rect = canvas.getBoundingClientRect();
  canvas.width  = rect.width;
  canvas.height = rect.height;

  ctx.clearRect(0, 0, canvas.width, canvas.height);
  graphState = null;

  if (tooltipEl) {
    tooltipEl.style.display = 'none';
  }
}

// Hover handlers (crosshair + tooltip)
function handleGraphMouseMove(e) {
  const canvas = document.getElementById('graphCanvas');
  if (!canvas || !graphState || !graphState.points.length) {
    if (tooltipEl) tooltipEl.style.display = 'none';
    return;
  }

  const ctx = canvas.getContext('2d');

  const rect = canvas.getBoundingClientRect();
  const mouseX = e.clientX - rect.left;
  const mouseY = e.clientY - rect.top;

  // find nearest point in X
  let nearest = null;
  let closestDist = Infinity;
  graphState.points.forEach(p => {
    const dist = Math.abs(p._x - mouseX);
    if (dist < closestDist) {
      closestDist = dist;
      nearest = p;
    }
  });

  const threshold = 20; // px
  if (!nearest || closestDist > threshold) {
    ctx.putImageData(graphState.baseImage, 0, 0);
    if (tooltipEl) tooltipEl.style.display = 'none';
    return;
  }

  // Restore base graph
  ctx.putImageData(graphState.baseImage, 0, 0);

  // Crosshair lines
  ctx.beginPath();
  ctx.moveTo(nearest._x, graphState.marginTop);
  ctx.lineTo(nearest._x, canvas.height - graphState.marginBottom);
  ctx.moveTo(graphState.marginLeft, nearest._y);
  ctx.lineTo(canvas.width - graphState.marginRight, nearest._y);
  ctx.strokeStyle = "#888888";
  ctx.lineWidth = 1;
  ctx.stroke();

  // Highlight point
  ctx.beginPath();
  ctx.arc(nearest._x, nearest._y, 4, 0, Math.PI * 2);
  ctx.fillStyle = "red";
  ctx.fill();

  // Tooltip
  if (tooltipEl) {
    tooltipEl.style.display = 'block';
    tooltipEl.style.left = (e.clientX + 10) + 'px';
    tooltipEl.style.top  = (e.clientY - 10) + 'px';
    tooltipEl.textContent = `${nearest.time} | ${nearest.value.toFixed(1)}\u00B0F`;
  }
}

function handleGraphMouseLeave() {
  const canvas = document.getElementById('graphCanvas');
  if (!canvas || !graphState) {
    if (tooltipEl) tooltipEl.style.display = 'none';
    return;
  }
  const ctx = canvas.getContext('2d');
  ctx.putImageData(graphState.baseImage, 0, 0);
  if (tooltipEl) tooltipEl.style.display = 'none';
}

document.addEventListener('DOMContentLoaded', () => {
  fetchSensors();

  tooltipEl = document.getElementById('tooltip');

  // Set date input default to today
  const input = document.getElementById('daySelect');
  const d = new Date();
  const year  = d.getFullYear();
  const month = String(d.getMonth() + 1).padStart(2, '0');
  const day   = String(d.getDate()).padStart(2, '0');
  input.value = `${year}-${month}-${day}`;
  input.max   = input.value;

  document.getElementById('sensorSelect').addEventListener('change', checkAndLoadGraph);
  document.getElementById('daySelect').addEventListener('change', checkAndLoadGraph);

  // Reset Graph button: clear selection + clear canvas
  const resetBtn = document.getElementById('resetGraphButton');
  if (resetBtn) {
    resetBtn.addEventListener('click', () => {
      const sel = document.getElementById('sensorSelect');
      if (sel) sel.selectedIndex = -1;   // remove selected sensor
      clearGraph();                      // clear the graph
    });
  }

  // Attach hover handlers once
  const canvas = document.getElementById('graphCanvas');
  if (canvas) {
    canvas.addEventListener('mousemove', handleGraphMouseMove);
    canvas.addEventListener('mouseleave', handleGraphMouseLeave);
  }

  // Toggle flash browser
  const browserBtn = document.getElementById('browserToggleBtn');
  if (browserBtn) {
    browserBtn.addEventListener('click', () => {
      const fb = document.getElementById('fileBrowser');
      if (!fb) return;
      if (fb.style.display === 'none' || fb.style.display === '') {
        fb.style.display = 'block';
        listFiles(currentPath);
      } else {
        fb.style.display = 'none';
      }
    });
  }

  // Make header link to this page (like Pump Runtimes)
  const titleLink = document.getElementById('temperatureLogsLink');
  if (titleLink) {
    titleLink.href = window.location.origin + '/third-page';
  }
});

function checkAndLoadGraph() {
  const sensor = document.getElementById('sensorSelect').value;
  const day    = document.getElementById('daySelect').value;
  if (sensor && day) loadGraph();
}

// === LittleFS Browser Functions ===
async function listFiles(path) {
  const r = await fetch(`/fs/list?dir=${encodeURIComponent(path)}`);
  const data = await r.json();
  const list = document.getElementById('fileList');
  list.innerHTML = '';
  data.forEach(item => {
    const li = document.createElement('li');
    li.className = item.isDir ? 'dir' : 'file';

    const nameSpan = document.createElement('span');
    nameSpan.textContent = item.name;
    if (item.isDir) {
      nameSpan.onclick = () => navigate(path + item.name + '/');
    } else {
      nameSpan.onclick = () => viewFile(path + item.name);
    }
    li.appendChild(nameSpan);

    if (item.isDir) {
      // Download Compressed button for directories
      const compDlBtn = document.createElement('button');
      compDlBtn.textContent = 'Download Compressed';
      compDlBtn.onclick = (e) => {
        e.stopPropagation();
        window.open(
          `/fs/download_compressed?path=${encodeURIComponent(path + item.name + '/')}`,
          '_blank'
        );
      };
      li.appendChild(compDlBtn);
    } else {
      // Download button for files
      const dlBtn = document.createElement('button');
      dlBtn.textContent = 'Download';
      dlBtn.onclick = (e) => {
        e.stopPropagation();
        window.open(
          `/fs/download?path=${encodeURIComponent(path + item.name)}`,
          '_blank'
        );
      };
      li.appendChild(dlBtn);
    }

    // Delete button
    const delBtn = document.createElement('button');
    delBtn.textContent = 'Delete';
    delBtn.onclick = (e) => {
      e.stopPropagation();
      if (confirm(`Delete ${item.name}?`)) {
        fetch(`/fs/delete?path=${encodeURIComponent(path + item.name)}`, {
          method: 'DELETE'
        }).then(() => listFiles(path));
      }
    };
    li.appendChild(delBtn);

    list.appendChild(li);
  });
  document.getElementById('currentPath').textContent = path;
}

function navigate(path) {
  currentPath = path;
  listFiles(path);
}

function goUp() {
  if (currentPath === '/') return;
  currentPath = currentPath.substring(
    0,
    currentPath.lastIndexOf('/', currentPath.length - 2) + 1
  );
  listFiles(currentPath);
}

async function viewFile(path) {
  const r = await fetch(`/fs/view?path=${encodeURIComponent(path)}`);
  const text = await r.text();
  const content = document.getElementById('fileContent');
  content.textContent = text;
  content.style.display = 'block';
}

function uploadFile(files) {
  if (!files.length) return;
  const file = files[0];
  const form = new FormData();
  form.append('file', file);
  fetch(`/fs/upload?dir=${encodeURIComponent(currentPath)}`, {
    method: 'POST',
    body: form
  }).then(() => {
    listFiles(currentPath);
  });
}

function createFolder() {
  const name = prompt("New folder name:");
  if (!name) return;
  fetch(`/fs/mkdir?path=${encodeURIComponent(currentPath + name)}`, {
    method: 'POST'
  }).then(() => listFiles(currentPath));
}

// Initial load (hidden browser populated)
listFiles(currentPath);
  </script>
</body>
</html>
)rawliteral";

void setupThirdPageRoutes() {
  server.on("/third-page", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "text/html", thirdPageHtml);
  });
  


  // Sensors
  server.on("/temperature-logs/sensors", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "[";
   for (int i = 1; i <= NUM_TEMP_SENSORS; ++i) {
    if (i > 1) json += ",";
    json += "\"" + String(SENSOR_FILE_NAMES[i]) + "\"";
}
    json += "]";
    req->send(200, "application/json", json);
  });

  // Graph data
    
  server.on("/temperature-logs/graph", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->hasParam("sensor") || !req->hasParam("day")) {
      req->send(400, "application/json", "{\"error\":\"missing params\"}");
      return;
    }
    String sensor = req->getParam("sensor")->value();
    String day = req->getParam("day")->value();
    String year = day.substring(0, 4);
    String month = day.substring(5, 7);
    String filePath =
      "/Temperature_Logs/" + year + "/" + month + "/" + sensor + "/" + day + ".txt";

    if (!takeFileSystemMutexWithRetry("[TempLogs] /temperature-logs/graph",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "application/json", "{\"error\":\"fs busy\"}");
      return;
    }

    if (!LittleFS.exists(filePath)) {
      xSemaphoreGive(fileSystemMutex);
      req->send(200, "application/json", "[]");
      return;
    }

    File f = LittleFS.open(filePath, "r");
    if (!f) {
      xSemaphoreGive(fileSystemMutex);
      req->send(500, "application/json", "{\"error\":\"open failed\"}");
      return;
    }

    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() < 10) continue;
      int first = line.indexOf(',');
      int second = line.lastIndexOf(',');
      if (first < 0 || second <= first) continue;
      String time = line.substring(first + 1, second);
      float val = line.substring(second + 1).toFloat();
      JsonObject obj = arr.createNestedObject();
      obj["time"] = time;
      obj["value"] = val;
    }
    f.close();
    xSemaphoreGive(fileSystemMutex);

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });


    // === LittleFS Browser Routes ===
  // List directory
  server.on("/fs/list", HTTP_GET, [](AsyncWebServerRequest *req){
    String dir = req->hasParam("dir") ? req->getParam("dir")->value() : "/";
    if (!dir.startsWith("/")) dir = "/" + dir;

    if (!takeFileSystemMutexWithRetry("[FS] /fs/list",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "application/json", "[]");
      return;
    }

    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
      xSemaphoreGive(fileSystemMutex);
      req->send(200, "application/json", "[]");
      return;
    }
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    File entry = root.openNextFile();
    while (entry) {
      JsonObject obj = arr.createNestedObject();
      obj["name"] =
        String(entry.name()).substring(String(entry.name()).lastIndexOf('/') + 1);
      obj["isDir"] = entry.isDirectory();
      entry = root.openNextFile();
    }
    String out;
    serializeJson(doc, out);
    xSemaphoreGive(fileSystemMutex);
    req->send(200, "application/json", out);
  });


    // View file
  server.on("/fs/view", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }
    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;

    if (!takeFileSystemMutexWithRetry("[FS] /fs/view",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "text/plain", "fs busy");
      return;
    }

    if (!LittleFS.exists(path)) {
      xSemaphoreGive(fileSystemMutex);
      req->send(404, "text/plain", "not found");
      return;
    }
    xSemaphoreGive(fileSystemMutex);

    AsyncWebServerResponse *response =
      req->beginResponse(LittleFS, path, "text/plain");
    req->send(response);
  });



  // Download file
  server.on("/fs/download", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }
    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;

    if (!takeFileSystemMutexWithRetry("[FS] /fs/download",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "text/plain", "fs busy");
      return;
    }

    if (!LittleFS.exists(path)) {
      xSemaphoreGive(fileSystemMutex);
      req->send(404, "text/plain", "not found");
      return;
    }
    xSemaphoreGive(fileSystemMutex);

    AsyncWebServerResponse *response =
      req->beginResponse(LittleFS, path, "application/octet-stream");
    response->addHeader(
      "Content-Disposition",
      "attachment; filename=\"" +
      path.substring(path.lastIndexOf('/') + 1) + "\""
    );
    req->send(response);
  });

  // Download directory as tar.gz
   // Download directory as tar.gz (ON-THE-FLY streaming, no temp file)
  server.on("/fs/download_compressed", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }

    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith("/")) path += "/";

    // Quick directory validity check (short mutex hold)
    if (!takeFileSystemMutexWithRetry("[FS] /fs/download_compressed(check)",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "text/plain", "fs busy");
      return;
    }

    File dirCheck = LittleFS.open(path);
    if (!dirCheck || !dirCheck.isDirectory()) {
      xSemaphoreGive(fileSystemMutex);
      req->send(404, "text/plain", "not found or not directory");
      return;
    }
    dirCheck.close();
    xSemaphoreGive(fileSystemMutex);

    // Filename
    String dirName = path.substring(0, path.length() - 1);
    dirName = dirName.substring(dirName.lastIndexOf('/') + 1);
    if (dirName.isEmpty()) dirName = "root";

    // Create streaming session (ring buffer in PSRAM)


    TgzStreamSession *session = new TgzStreamSession(TGZ_RING_BYTES);
    if (!session || !session->stream.ok()) {
      if (session) delete session;
      req->send(500, "text/plain", "PSRAM ring allocation failed");
      return;
    }
    session->srcPath = path;

    // Start producer task (compresses into ring)
        BaseType_t ok = spawnTaskOptionalCore(
      tgzProducerTask,
      "tgzProducer",
      TGZ_PRODUCER_TASK_STACK_BYTES,
      session,
      TGZ_PRODUCER_TASK_PRIORITY,
      &session->producerTask,
      TGZ_PRODUCER_TASK_CORE
    );


        if (ok != pdPASS) {
            delete session;
              req->send(500, "text/plain", "cannot start tgz producer task");
                return;
        }

    // Make tgzProducer visible to monitorStacks() while it is alive
    thTgzProducer = session->producerTask;



    // If client disconnects early, stop producer & free session safely
    req->onDisconnect([session]() {
      session->stream.cancel();
      if (session->producerFinished) {
        delete session;
      } else {
        // Producer will delete it when done
        session->deleteWhenProducerDone = true;
      }
    });

    // Stream from ring via chunked response
    AsyncWebServerResponse *response =
      req->beginChunkedResponse("application/gzip",
        [session](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
          (void)index;
          // Blocks briefly until data is available or producer finishes
          return session->stream.readBytes(buffer, maxLen);
        }
      );

    response->addHeader("Content-Disposition",
                        "attachment; filename=\"" + dirName + ".tar.gz\"");

    response->addHeader("Connection", "close");   // <<< ensures onDisconnect runs reliably
    // Do NOT add Content-Length for chunked responses.
    req->send(response);
  });




    // Delete file/dir
  server.on("/fs/delete", HTTP_DELETE, [](AsyncWebServerRequest *req){
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }
    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;

    if (!takeFileSystemMutexWithRetry("[FS] /fs/delete",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "text/plain", "fs busy");
      return;
    }

    if (!LittleFS.exists(path)) {
      xSemaphoreGive(fileSystemMutex);
      req->send(404, "text/plain", "not found");
      return;
    }
    bool success = LittleFS.remove(path) || LittleFS.rmdir(path);
    xSemaphoreGive(fileSystemMutex);

    req->send(success ? 200 : 500,
              "text/plain",
              success ? "deleted" : "delete failed");
  });


    // Create directory
  server.on("/fs/mkdir", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }
    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;

    if (!takeFileSystemMutexWithRetry("[FS] /fs/mkdir",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "text/plain", "fs busy");
      return;
    }

    if (LittleFS.exists(path)) {
      xSemaphoreGive(fileSystemMutex);
      req->send(409, "text/plain", "already exists");
      return;
    }

    bool created = LittleFS.mkdir(path);
    xSemaphoreGive(fileSystemMutex);

    if (created) {
      req->send(200, "text/plain", "created");
    } else {
      req->send(500, "text/plain", "create failed");
    }
  });


    // Upload file
  server.on("/fs/upload", HTTP_POST, [](AsyncWebServerRequest *req){
    req->send(200);
  }, [](AsyncWebServerRequest *req,
        String filename,
        size_t index,
        uint8_t *data,
        size_t len,
        bool final){
    static File uploadFile;
    String dir = req->hasParam("dir") ? req->getParam("dir")->value() : "/";
    if (!dir.startsWith("/")) dir = "/" + dir;

    if (!takeFileSystemMutexWithRetry("[FS] /fs/upload",
                                      pdMS_TO_TICKS(2000), 3)) {
      Serial.println("[FS] /fs/upload: dropping chunk due to FS mutex contention");
      return;
    }

    if (index == 0) {
      if (!LittleFS.exists(dir)) {
        LittleFS.mkdir(dir);
      }
      uploadFile = LittleFS.open(dir + filename, "w");
    }

    if (uploadFile && len) {
      uploadFile.write(data, len);
    }

    if (final && uploadFile) {
      uploadFile.close();
    }

    xSemaphoreGive(fileSystemMutex);
  });

}