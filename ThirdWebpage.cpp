// ThirdWebpage.cpp
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
#include "TarGZ.h"
#include <esp_task_wdt.h>
#include "DiagLog.h"


extern AsyncWebServer server;

struct FsLockedStreamSession {
  File f;
  volatile bool finished = false;
};

static void finishFsLockedStream(FsLockedStreamSession *s) {
  if (!s || s->finished) return;
  s->finished = true;

  if (s->f) s->f.close();

  // IMPORTANT: we hold fileSystemMutex across the entire stream
  xSemaphoreGive(fileSystemMutex);
}

// Recursive delete helper (caller must hold fileSystemMutex!)
static bool deletePathRecursiveUnlocked(const String &path) {
  // Never allow deleting root
  if (path == "/") return false;

  File node = LittleFS.open(path);
  if (!node) {
    // If it doesn't exist, treat as "already gone"
    return !LittleFS.exists(path);
  }

  bool isDir = node.isDirectory();
  node.close();

  if (!isDir) {
    return LittleFS.remove(path);
  }

  // Directory: delete children first
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    // last-ditch attempt
    return LittleFS.rmdir(path) || LittleFS.remove(path);
  }

  bool ok = true;
  File entry = dir.openNextFile();
  while (entry) {
    String childPath = String(entry.name());  // full path
    bool childIsDir = entry.isDirectory();
    entry.close();

    if (childIsDir) {
      ok = deletePathRecursiveUnlocked(childPath) && ok;
    } else {
      ok = LittleFS.remove(childPath) && ok;
    }

    vTaskDelay(1);
    entry = dir.openNextFile();
  }
  dir.close();

  ok = (LittleFS.rmdir(path) || LittleFS.remove(path)) && ok;
  return ok;
}




static const char *thirdPageHtml = R"rawliteral(
<!doctype html>
<html>
<head>
  <link rel="icon" href="/static/favicon.png">
  <title>Temperature Logs</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body {
      margin: 0;
      padding: 0;
      overflow-x: hidden;
      }

    *, *::before, *::after {
      box-sizing: border-box;
      }

    body {
      font-family: Arial;
      text-align: center;
      }

    #pageWrap {
      max-width: 1000px;
      margin: 0 auto;
      padding: 0 8px;
     }
   
    h3 {
      color: #459;
      font-size: 18px;
      line-height: 1.2;
      font-weight: bold;
      text-align: center;
      margin: 8px 0;
      }

    h3.top-heading{
      color:#459;
      font-size: 28px;     /* <<< adjust this to taste */
      font-weight:bold;
      text-align:center;
      margin: 2px 0 6px 0;
      padding: 0;
      line-height: 1.0;
      white-space: nowrap;
      }

    h3.top-heading a,
      h3.top-heading a:visited {
      color: #459;
      text-decoration: none;
      }

    h3.top-heading a:hover,
      h3.top-heading a:focus {
      text-decoration: underline;
      }
      
        #controlsRow{
      display: flex;
      justify-content: center;
      align-items: center;
      gap: 10px;
      flex-wrap: wrap; /* allow wrap inside narrow iframe */
      }

          #sensorSelect { min-width: 220px; max-width: 100%; }
    #daySelect    { max-width: 100%; }

    @media (max-width: 520px){
      #controlsRow{
        display: grid;
        grid-template-columns: auto auto; /* label + control */
        gap: 6px 8px;
        justify-content: center;
        align-items: center;
      }
      #controlsRow label{
        white-space: nowrap;
        text-align: right;
      }
      #sensorSelect, #daySelect{
        width: 240px;        /* keeps it compact in the iframe */
        max-width: 92vw;
        min-width: 0;
      }
    }



    #graphContainer {
      width: 100%;
      height: 420px;
      margin: 8px 0;
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

    #fileBrowser {
      margin: 20px auto 0;
      border: 1px solid #ccc;
      padding: 10px;
      display: none;
      width: fit-content;
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
  <div id="pageWrap">

    <h3 class="top-heading">
      <a id="temperatureLogsLink" target="_blank">Temperature Logs</a>
    </h3>

    <div id="controlsRow">
      <label for="sensorSelect">Sensor:</label>
      <select id="sensorSelect"></select>

      <label for="daySelect">Day:</label>
      <input type="date" id="daySelect">
    </div>

    <button id="resetGraphButton" class="blue-button">Reset Graph</button>

    <div id="graphContainer">
      <canvas id="graphCanvas"></canvas>
      <div id="tooltip"></div>
    </div>

    <button id="browserToggleBtn" class="blue-button">Flash Memory Browser</button>

    <div id="fileBrowser">
    <h4>Flash Memory Browser</h4>
    <button onclick="navigate('/')">Root</button>
    <button onclick="goUp()">.. (Up)</button>
    <button onclick="document.getElementById('uploadInput').click()">Upload to Current</button>
    <input type="file" id="uploadInput" style="display:none;" onchange="uploadFile(this.files)">
    <button onclick="createFolder()">Create Folder</button>
    <p>Current path: <span id="currentPath">/</span></p>
    <ul id="fileList"></ul>
    <div id="fileContent"></div>
  
  </div>

  </div> 



  <script>



let currentPath = '/';

let lastPostedHeight = 0;

function postHeightToParent() {
  if (window === window.parent) return; // not inside iframe

    const wrap = document.getElementById('pageWrap');
  if (!wrap) return;

  // Robust content height: bottom of last child relative to #pageWrap top
  const top = wrap.getBoundingClientRect().top;
  const last = wrap.lastElementChild || wrap;
  const bottom = last.getBoundingClientRect().bottom;
  const h = Math.ceil(bottom - top);

  // Prevent feedback loops / repeated posts
  if (Math.abs(h - lastPostedHeight) < 2) return;

  lastPostedHeight = h;

  window.parent.postMessage({ type: "thirdPageHeight", height: h }, "*");
}




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

  // default to first sensor so checkAndLoadGraph() can run
  if (arr.length) sel.selectedIndex = 0;
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

document.addEventListener('DOMContentLoaded', async () => {

  setTimeout(postHeightToParent, 50);

  await fetchSensors();

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
  listFiles(currentPath); // listFiles() will post the height after it renders
} else {
  fb.style.display = 'none';
  requestAnimationFrame(() => requestAnimationFrame(postHeightToParent));
}

  });
}




  // Make header link to this page (like Pump Runtimes)
  const titleLink = document.getElementById('temperatureLogsLink');
  if (titleLink) {
    titleLink.href = window.location.origin + '/third-page';
  }

  // auto-load once date + first sensor exist
  checkAndLoadGraph();
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
          `/fs/download_compressed?dir=${encodeURIComponent(path + item.name)}`,
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

  // If browser is visible, update iframe height after DOM updates
  const fb = document.getElementById('fileBrowser');
  if (fb && fb.style.display === 'block') {
    setTimeout(postHeightToParent, 50);
  }
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
  setTimeout(postHeightToParent, 50);
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
  server.on("/third-page", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", thirdPageHtml);
  });



  // Sensors
  server.on("/temperature-logs/sensors", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "[";
    for (int i = 1; i <= NUM_TEMP_SENSORS; ++i) {
      if (i > 1) json += ",";
      json += "\"" + String(SENSOR_FILE_NAMES[i]) + "\"";
    }
    json += "]";
    req->send(200, "application/json", json);
  });

  // Graph data

  server.on("/temperature-logs/graph", HTTP_GET, [](AsyncWebServerRequest *req) {
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
    int lineCount = 0;
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
      lineCount++;
      if ((lineCount % 200) == 0) {
        vTaskDelay(1);
      }
    }
    f.close();
    xSemaphoreGive(fileSystemMutex);

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });


  // === LittleFS Browser Routes ===
  // List directory
  server.on("/fs/list", HTTP_GET, [](AsyncWebServerRequest *req) {
    String dir = req->hasParam("dir") ? req->getParam("dir")->value() : "/";
    if (!dir.startsWith("/")) dir = "/" + dir;

    if (!isSafePath(dir)) {
      req->send(400, "application/json", "[]");
      return;
    }

    if (!takeFileSystemMutexWithRetry("[FS] /fs/list",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "application/json", "[]");
      return;
    }

    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
      if (root) root.close();
      xSemaphoreGive(fileSystemMutex);
      req->send(200, "application/json", "[]");
      return;
    }

    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    File entry = root.openNextFile();
    while (entry) {
      JsonObject obj = arr.createNestedObject();

      String full = String(entry.name());
      obj["name"] = full.substring(full.lastIndexOf('/') + 1);
      obj["isDir"] = entry.isDirectory();

      entry.close();  // ✅ IMPORTANT: close each entry
      vTaskDelay(1);  // keep system responsive
      entry = root.openNextFile();
    }

    root.close();  // ✅ close the directory handle
    xSemaphoreGive(fileSystemMutex);

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });



  // View file
  server.on("/fs/view", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }

    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;

    if (!isSafePath(path) || path == "/") {
      req->send(400, "text/plain", "bad path");
      return;
    }


    if (!isSafePath(path) || path == "/") {
      req->send(400, "text/plain", "bad path");
      return;
    }

    if (!takeFileSystemMutexWithRetry("[FS] /fs/view(stream)",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "text/plain", "fs busy");
      return;
    }

    if (!LittleFS.exists(path)) {
      xSemaphoreGive(fileSystemMutex);
      req->send(404, "text/plain", "not found");
      return;
    }

    FsLockedStreamSession *s = new FsLockedStreamSession();
    if (!s) {
      xSemaphoreGive(fileSystemMutex);
      req->send(500, "text/plain", "alloc failed");
      return;
    }

    s->f = LittleFS.open(path, "r");
    if (!s->f) {
      delete s;
      xSemaphoreGive(fileSystemMutex);
      req->send(500, "text/plain", "open failed");
      return;
    }

    // Stream while holding the FS mutex until finished/disconnect
    req->onDisconnect([s]() {
      finishFsLockedStream(s);
      delete s;
    });

    AsyncWebServerResponse *response =
      req->beginChunkedResponse("text/plain",
                                [s](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                                  (void)index;
                                  if (!s || s->finished) return 0;

                                  size_t n = s->f.read(buffer, maxLen);
                                  if (n == 0) {
                                    finishFsLockedStream(s);
                                  }
                                  return n;
                                });

    req->send(response);
  });




  // Download file
  server.on("/fs/download", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }

    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;

    if (!isSafePath(path) || path == "/") {
      req->send(400, "text/plain", "bad path");
      return;
    }

    if (!takeFileSystemMutexWithRetry("[FS] /fs/download(stream)",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "text/plain", "fs busy");
      return;
    }

    if (!LittleFS.exists(path)) {
      xSemaphoreGive(fileSystemMutex);
      req->send(404, "text/plain", "not found");
      return;
    }

    FsLockedStreamSession *s = new FsLockedStreamSession();
    if (!s) {
      xSemaphoreGive(fileSystemMutex);
      req->send(500, "text/plain", "alloc failed");
      return;
    }

    s->f = LittleFS.open(path, "r");
    if (!s->f) {
      delete s;
      xSemaphoreGive(fileSystemMutex);
      req->send(500, "text/plain", "open failed");
      return;
    }

    String filename = path.substring(path.lastIndexOf('/') + 1);

    req->onDisconnect([s]() {
      finishFsLockedStream(s);
      delete s;
    });

    AsyncWebServerResponse *response =
      req->beginChunkedResponse("application/octet-stream",
                                [s](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                                  (void)index;
                                  if (!s || s->finished) return 0;

                                  size_t n = s->f.read(buffer, maxLen);
                                  if (n == 0) {
                                    finishFsLockedStream(s);
                                  }
                                  return n;
                                });

    response->addHeader(
      "Content-Disposition",
      "attachment; filename=\"" + filename + "\"");

    req->send(response);
  });


  
  // Download directory as tar.gz (ON-THE-FLY streaming, no temp file)
  TarGZ::registerRoutes(server);





  // Delete file/dir
  server.on("/fs/delete", HTTP_DELETE, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }

    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;

    if (!isSafePath(path) || path == "/") {
      req->send(400, "text/plain", "bad path");
      return;
    }

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

    bool success = deletePathRecursiveUnlocked(path);

    xSemaphoreGive(fileSystemMutex);

    req->send(success ? 200 : 500,
              "text/plain",
              success ? "deleted" : "delete failed");
  });



  // Create directory
  server.on("/fs/mkdir", HTTP_POST, [](AsyncWebServerRequest *req) {
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
  server.on(
    "/fs/upload", HTTP_POST, [](AsyncWebServerRequest *req) {
      req->send(200);
    },
    [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      static File uploadFile;

      String dir = req->hasParam("dir") ? req->getParam("dir")->value() : "/";
      if (!dir.startsWith("/")) dir = "/" + dir;
      if (!dir.endsWith("/")) dir += "/";

            if (!isSafePath(dir) || dir == "//") {
        LOG_CAT(DBG_FS, "[FS] /fs/upload: bad dir\n");
        return;
      }


      // Sanitize filename (strip any path segments)
      String cleanName = filename;
      int s1 = cleanName.lastIndexOf('/');
      if (s1 >= 0) cleanName = cleanName.substring(s1 + 1);
      int s2 = cleanName.lastIndexOf('\\');
      if (s2 >= 0) cleanName = cleanName.substring(s2 + 1);

            if (cleanName.length() == 0 || cleanName.indexOf("..") != -1) {
        LOG_CAT(DBG_FS, "[FS] /fs/upload: bad filename\n");
        return;
      }


      const String fullPath = dir + cleanName;

            if (!takeFileSystemMutexWithRetry("[FS] /fs/upload",
                                        pdMS_TO_TICKS(2000), 3)) {
        LOG_CAT(DBG_FS, "[FS] /fs/upload: dropping chunk due to FS mutex contention\n");
        return;
      }


      if (index == 0) {
        // mkdir expects no trailing slash (unless root)
        String mk = dir;
        if (mk.length() > 1 && mk.endsWith("/")) mk.remove(mk.length() - 1);

        if (!LittleFS.exists(mk)) {
          LittleFS.mkdir(mk);
        }

        uploadFile = LittleFS.open(fullPath, "w");
                if (!uploadFile) {
          LOG_ERR("[FS] /fs/upload: open failed: %s\n", fullPath.c_str());
          xSemaphoreGive(fileSystemMutex);
          return;
        }
else {
          LOG_CAT(DBG_FS, "[FS] /fs/upload: opened '%s' for write\n", fullPath.c_str());
        }
      }

      if (uploadFile && len) {
        uploadFile.write(data, len);
      }

      if (final && uploadFile) {
        uploadFile.close();
        LOG_CAT(DBG_FS, "[FS] /fs/upload: finished '%s'\n", fullPath.c_str());
      }

      xSemaphoreGive(fileSystemMutex);
    });
}
