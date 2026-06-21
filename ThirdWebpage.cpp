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
#include "RawTar.h"
#include <esp_task_wdt.h>
#include "DiagLog.h"
#include "Logging.h"


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

// Normalize LittleFS paths used by the browser/delete routes.
static String normalizeFsPath(String path, bool wantDir) {
  path.trim();
  if (path.length() == 0) path = "/";
  if (!path.startsWith("/")) path = "/" + path;

  // Collapse trailing slashes except for root.  LittleFS.rmdir() is
  // noticeably less forgiving about "/folder/" than "/folder" on some builds.
  while (path.length() > 1 && path.endsWith("/")) {
    path.remove(path.length() - 1);
  }

  if (wantDir && path != "/") path += "/";
  return path;
}

static bool jsonListAlreadyHasName(JsonArray arr, const String& name) {
  for (JsonObject obj : arr) {
    const char* n = obj["name"] | "";
    if (name == n) return true;
  }
  return false;
}

static String directChildPathFromEntryName(const String& parentNoTrailingSlash, const String& entryName) {
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

// Recursive delete helper (caller must hold fileSystemMutex!)
static bool deletePathRecursiveUnlocked(String path) {
  path = normalizeFsPath(path, false);

  // Never allow deleting root
  if (path == "/") return false;

  File node = LittleFS.open(path, "r");
  if (!node) {
    // If it doesn't exist, treat as "already gone"
    return !LittleFS.exists(path);
  }

  bool isDir = node.isDirectory();
  node.close();

  if (!isDir) {
    return LittleFS.remove(path);
  }

  bool ok = true;

  // Delete one child per pass, then reopen the directory.  This is more robust
  // on LittleFS when deleting while iterating and also handles builds where
  // openNextFile() returns recursive names instead of direct children only.
  for (;;) {
    File dir = LittleFS.open(path, "r");
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      break;
    }

    String childToDelete;
    File entry = dir.openNextFile();
    while (entry) {
      String entryName = String(entry.name());
      entry.close();

      String childPath = directChildPathFromEntryName(path, entryName);
      childPath = normalizeFsPath(childPath, false);

      if (childPath.length() > 1 && childPath != path) {
        childToDelete = childPath;
        break;
      }

      entry = dir.openNextFile();
    }
    dir.close();

    if (childToDelete.length() == 0) break;

    ok = deletePathRecursiveUnlocked(childToDelete) && ok;

    // Safety valve: Guaranteed 2ms gap between every file/folder removal pass.
    vTaskDelay(pdMS_TO_TICKS(2));
  }

  ok = LittleFS.rmdir(path) && ok;
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

    h3.top-heading {
      color:#459;
      font-size: 35px;
      font-weight:bold;
      text-align:center;
      margin: 2px 0 3px 0; // Margin sets vertacle spacing
      padding: 0;
      line-height: 1.30;
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
      margin-top: 20px; /* Pushes the controls row down by 10px */
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
      height: 630px; /* Increased by 50% to spread out Y-axis values */
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
      margin: 8px auto 0;
      border: 1px solid #b8c7d9;
      background: #fbfdff;
      padding: 8px;
      display: none;
      width: 100%;
      max-width: 980px;
      text-align: left;
      }

    #fileBrowser .browserHeader {
      text-align: center;
      color: #459;
      font-weight: bold;
      margin-bottom: 4px;
    }

    #fileBrowser .browserToolbar {
      text-align: center;
      margin-bottom: 5px;
    }

    #fileBrowser .browserPath {
      text-align: center;
      font-size: 12px;
      color: purple;
      margin: 3px 0 5px 0;
      overflow-wrap: anywhere;
    }

    #fileBrowser .browserRow {
      display: flex;
      align-items: center;
      gap: 6px;
      padding: 3px 0;
      font-size: 12px;
      border-bottom: 1px solid #eef4fb;
    }

    #fileBrowser .browserRow:last-child { border-bottom: 0; }

    #fileBrowser .browserRow .name {
      flex: 1 1 auto;
      overflow-wrap: anywhere;
      cursor: pointer;
    }

    #fileBrowser .browserRow.dir .name {
      color: #0066cc;
      font-weight: bold;
    }

    #fileBrowser .browserRow.file .name { color: blue; }

    #fileBrowser .emptyNote {
      text-align: center;
      color: #777;
      font-size: 12px;
      padding: 8px;
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
      <a id="temperatureLogsLink" target="_blank" title="Open the full Temperature Logs page">Temperature Logs</a>
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

    <button id="browserToggleBtn" class="blue-button">View Temperature Logs</button>
    <button id="downloadSelectedTempLogsBtn" class="blue-button" style="display:none;">Download Selected</button>
    <button id="downloadAllTempLogsBtn" class="blue-button" style="display:none;">Download All</button>

    <div id="fileBrowser">
      <div id="tempLogFileList"></div>
      <div id="tempArchiveStatusBox" class="emptyNote" style="display:none; border:1px solid #b8c7d9; margin:4px 0; padding:5px; white-space:pre-wrap;"></div>
    </div>

  </div> 



  <script>



const tempLogRoot = '/Temperature_Logs/';
let tempLogCurrentPath = tempLogRoot;

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

  // High-DPI (Retina) scaling
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  
  // Set actual physical memory size
  canvas.width  = rect.width * dpr;
  canvas.height = rect.height * dpr;
  
  // Normalize coordinate system to use CSS pixels
  ctx.scale(dpr, dpr);

  ctx.clearRect(0, 0, rect.width, rect.height);
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

  // CRITICAL: Use rect.width/height instead of canvas.width/height to match CSS pixels
  const width  = rect.width  - marginLeft - marginRight;
  const height = rect.height - marginTop  - marginBottom;

  function xFromMinutes(m) {
    return marginLeft + ((m - dayStartMinutes) / totalMinutes) * width;
  }
  function yFromValue(v) {
    return canvas.height - marginBottom
           - ((v - minV) / (maxV - minV)) * height;
  }

  // Reduced font size to 10px to prevent labels from overlapping
  ctx.font = "10px Arial";
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

        // Dropped the ":00" to save horizontal space and prevent overlap
        const label =
          h === 0 || h === 24
            ? "12AM"
            : h === 12
              ? "12PM"
              : (h % 12 || 12) + (h < 12 ? "AM" : "PM");

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

  // --- NEW: Standalone Size Boost ---
  // If the user views this page directly (not inside the FirstWebpage iframe),
  // increase the width and height by 75% for a massive, clear graph.
  if (window === window.parent) {
    const wrap = document.getElementById('pageWrap');
    const graph = document.getElementById('graphContainer');
    if (wrap) wrap.style.maxWidth = '1750px';
    if (graph) graph.style.height = '1100px';
  }

  setTimeout(postHeightToParent, 50);

  await fetchSensors();

  tooltipEl = document.getElementById('tooltip');


  // Set date input default to today
  const input = document.getElementById('daySelect');
  
  function updateDateBounds() {
    const d = new Date();
    const year  = d.getFullYear();
    const month = String(d.getMonth() + 1).padStart(2, '0');
    const day   = String(d.getDate()).padStart(2, '0');
    input.max = `${year}-${month}-${day}`;
  }

  // Initialize bounds on page load
  updateDateBounds();
  input.value = input.max;

  // Silently shift the maximum allowed date forward whenever the user clicks/taps the box
  input.addEventListener('click', updateDateBounds);
  input.addEventListener('focus', updateDateBounds);

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


  // Toggle scoped Temperature_Logs browser
  const browserBtn = document.getElementById('browserToggleBtn');
  const selectedBtn = document.getElementById('downloadSelectedTempLogsBtn');
  const allBtn = document.getElementById('downloadAllTempLogsBtn');
  if (browserBtn) {
    browserBtn.addEventListener('click', () => {
      const fb = document.getElementById('fileBrowser');
      if (!fb) return;

      const opening = (fb.style.display === 'none' || fb.style.display === '');
      if (opening) {
        fb.style.display = 'block';
        browserBtn.textContent = 'Hide Temperature Logs';
        if (selectedBtn) selectedBtn.style.display = 'inline';
        if (allBtn) allBtn.style.display = 'inline';
        tempLogCurrentPath = tempLogRoot;
        listTemperatureLogFiles();
      } else {
        fb.style.display = 'none';
        browserBtn.textContent = 'View Temperature Logs';
        if (selectedBtn) selectedBtn.style.display = 'none';
        if (allBtn) allBtn.style.display = 'none';
        requestAnimationFrame(() => requestAnimationFrame(postHeightToParent));
      }
    });
  }

  if (selectedBtn) {
    selectedBtn.addEventListener('click', downloadSelectedTemperatureLogs);
  }
  if (allBtn) {
    allBtn.addEventListener('click', () => {
      prepareArchiveDownload('/Temperature_Logs');
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


function humanBytes(bytes) {
  bytes = Number(bytes || 0);
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let unit = 0;
  while (bytes >= 1024 && unit < units.length - 1) {
    bytes = bytes / 1024;
    unit++;
  }
  return (unit === 0 ? bytes.toFixed(0) : bytes.toFixed(1)) + ' ' + units[unit];
}

function humanDuration(seconds) {
  seconds = Math.max(1, Math.round(Number(seconds || 1)));
  if (seconds < 90) return seconds + ' seconds';
  const minutes = Math.round(seconds / 60);
  if (minutes < 90) return minutes + ' minutes';
  const hours = Math.round(minutes / 60);
  return hours + ' hours';
}

function archiveTimeRange(bytes) {
  bytes = Number(bytes || 0);
  const slow = bytes / (40 * 1024);
  const fast = bytes / (200 * 1024);
  return humanDuration(fast) + ' to ' + humanDuration(slow);
}

function archivePathNoTrailing(path) {
  path = String(path || '').trim();
  if (!path.startsWith('/')) path = '/' + path;
  while (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
  return path;
}

function isTemperatureLogsRootArchive(path) {
  return archivePathNoTrailing(path) === '/Temperature_Logs';
}

function launchArchiveDownloadWindow(url) {
  const sep = url.indexOf('?') >= 0 ? '&' : '?';
  const finalUrl = url + sep + 'ts=' + Date.now();
  window.open(finalUrl, '_blank');
}
function launchHiddenArchiveDownload(url) {
  // Phase2V: directory archive downloads are launched as a real browser
  // navigation/new-tab request instead of a hidden iframe. Some browsers
  // will stream the response to a hidden iframe without saving the file.
  launchArchiveDownloadWindow(url);
}

function archiveRawTarUrl(path) {
  path = archivePathNoTrailing(path);
  return '/fs/download_raw_tar?dir=' + encodeURIComponent(path);
}
function openArchiveRawTar(path) {
  const url = archiveRawTarUrl(path);
  window.open(url + (url.indexOf('?') >= 0 ? '&' : '?') + 'ts=' + Date.now(), '_blank');
}
function archiveClickDebug(path, source) {
  fetch('/fs/archive_click?source=' + encodeURIComponent(source || 'third') + '&path=' + encodeURIComponent(path) + '&ts=' + Date.now(), { cache:'no-store' }).catch(console.log);
}
async function scanArchiveDir(path, source) {
  archiveClickDebug(path, source || 'temperature-section-scan');
  const box = archiveStatusBox();
  box.textContent = 'Estimating directory. No archive is being created...';
  try {
    const r = await fetch('/fs/tar_info?verbose=1&max=1500&dir=' + encodeURIComponent(path) + '&ts=' + Date.now(), { cache:'no-store' });
    const info = await r.json();
    box.textContent = 'Scan ' + (info.ok ? 'OK' : 'FAILED') + ' for ' + path + ': entries=' + (info.entries || 0) + ', dirs=' + (info.dirs || 0) + ', files=' + (info.files || 0) + ', source=' + humanBytes(info.bytes || 0) + ', maxPathLen=' + (info.maxPathLen || 0) + ', longPaths=' + (info.longPaths || 0) + ', zeroFiles=' + (info.zeroFiles || 0) + (info.error ? ', error=' + info.error : '');
  } catch (e) {
    box.textContent = 'Scan request failed: ' + (e && e.message ? e.message : e);
    console.log(e);
  }
  setTimeout(postHeightToParent, 50);
}


let archivePollTimer = null;
function archiveStatusBox() {
  let box = document.getElementById('tempArchiveStatusBox');
  if (!box) {
    box = document.createElement('div');
    box.id = 'tempArchiveStatusBox';
    box.className = 'emptyNote';
    box.style.border = '1px solid #b8c7d9';
    box.style.margin = '4px 0';
    box.style.padding = '5px';
    box.style.whiteSpace = 'pre-wrap';
  }
  const pathLine = document.getElementById('tempLogCurrentPathLine');
  if (pathLine && pathLine.parentNode) {
    pathLine.parentNode.insertBefore(box, pathLine.nextSibling);
  } else {
    const container = document.getElementById('fileBrowser');
    if (container && box.parentNode !== container) container.appendChild(box);
  }
  box.style.display = 'block';
  setTimeout(postHeightToParent, 50);
  return box;
}

function archiveCompleteLabel(failed) {
  return (failed ? 'Download Failed' : 'Download Complete') + ' - ' + new Date().toLocaleString();
}



function archiveBuildBeginLine(path, info) {
  info = info || {};
  return '[RAW-TAR] stream begin dir=' + path +
    ' entries=' + (info.entries || 0) +
    ' files=' + (info.files || 0) +
    ' dirs=' + (info.dirs || 0) +
    ' sourceBytes=' + (info.bytes || info.sourceBytes || 0) +
    ' estTarBytes=' + (info.tarBytes || info.estTarBytes || 0) +
    ' filename=' + (info.filename || 'download.tar');
}
function archiveCompleteFallback(path, st) {
  st = st || {};
  return '[RAW-TAR] stream complete root=' + path +
    ' bytesOut=' + (st.bytesOut || 0) +
    ' filesSent=' + (st.filesSent || 0) +
    ' dirsSent=' + (st.dirsSent || 0) +
    ' err=' + (st.error || '');
}
function archiveAppendToken(url, token) {
  const sep = url.indexOf('?') >= 0 ? '&' : '?';
  return url + sep + 'token=' + encodeURIComponent(token);
}
function archiveStartStatusPoll(box, path, token, beginLine) {
  if (archivePollTimer) clearInterval(archivePollTimer);
  let tries = 0;
  archivePollTimer = setInterval(async () => {
    tries++;
    try {
      const r = await fetch('/fs/archive_status?token=' + encodeURIComponent(token) + '&ts=' + Date.now(), { cache:'no-store' });
      const st = await r.json();
      if (!st || !st.hasStatus || st.token !== token) {
        if (tries > 900) { clearInterval(archivePollTimer); archivePollTimer = null; }
        return;
      }
      const started = st.beginLine || beginLine;
      box.style.whiteSpace = 'pre-wrap';
      if (st.completed) {
        clearInterval(archivePollTimer);
        archivePollTimer = null;
        box.textContent = archiveCompleteLabel(st.failed) + '\n' +
          started + '\n' +
          (st.completeLine || archiveCompleteFallback(path, st));
      } else {
        box.textContent = 'Archive download in progress...\n' +
          started + '\n' +
          'Transferred so far: ' + humanBytes(st.bytesOut || 0) +
          '   Files sent: ' + (st.filesSent || 0) + '/' + (st.files || 0) +
          '   Folders sent: ' + (st.dirsSent || 0) + '/' + (st.dirs || 0);
      }
      setTimeout(postHeightToParent, 50);
    } catch (e) {
      console.log(e);
      if (tries > 900) { clearInterval(archivePollTimer); archivePollTimer = null; }
    }
  }, 1000);
}
function showArchiveStartButton(box, path, url, source, info) {
  if (!box) box = archiveStatusBox();
  const beginLine = archiveBuildBeginLine(path, info || {});
  const token = 'rawtar_' + Date.now() + '_' + Math.floor(Math.random() * 1000000);
  archiveClickDebug(path, source || 'temperature-section-start-button');
  box.style.whiteSpace = 'pre-wrap';
  box.textContent = 'Starting archive download in a new browser tab/window...\n' + beginLine + '\nWaiting for completion...';
  archiveStartStatusPoll(box, path, token, beginLine);
  launchHiddenArchiveDownload(archiveAppendToken(url, token));
  setTimeout(postHeightToParent, 50);
}

async function prepareArchiveDownload(path) {
  archiveClickDebug(path, 'temperature-section-prepare');
  const box = archiveStatusBox();
  box.textContent = 'Estimating archive size...';
  try {
    const r = await fetch('/fs/tar_info?dir=' + encodeURIComponent(path) + '&ts=' + Date.now(), { cache:'no-store' });
    const info = await r.json();
    if (!info.ok) {
      box.textContent = 'Archive estimate failed: ' + (info.error || 'unknown error');
      alert(box.textContent);
      return;
    }
    const bytes = Number(info.bytes || 0);
    let msg = 'Archive download for ' + path + '\n';
    msg += 'Source data: ' + humanBytes(bytes) + '\n';
    if (info.tarBytes) msg += 'Estimated TAR size: ' + humanBytes(info.tarBytes) + '\n';
    if (info.fsPctUsed !== undefined) msg += 'Filesystem used: ' + Number(info.fsPctUsed).toFixed(1) + '% (cleanup starts at ' + Number(info.cleanupStartLimit || 0).toFixed(1) + '%)\n';
    if (info.cleanupRisk) msg += 'WARNING: filesystem cleanup may be close to triggering. The cleanup task will be deferred for protected Temperature_Logs files while this archive is active.\n';
    msg += 'Files: ' + (info.files || 0) + '  Folders: ' + (info.dirs || 0) + '\n';
    msg += 'Estimated download time: ' + archiveTimeRange(bytes) + '\n';
    if (info.truncated) msg += 'Estimate was truncated because the folder is very large.\n';
    msg += '\nSelect "OK" to begin the archive download in a new browser tab/window.';
    if (!confirm(msg)) {
      box.textContent = 'Archive download cancelled before start.';
      setTimeout(postHeightToParent, 50);
      return;
    }
    showArchiveStartButton(box, path, '/fs/download_raw_tar?dir=' + encodeURIComponent(path), 'temperature-section-start-button', info);
  } catch (e) {
    box.textContent = 'Archive estimate failed: ' + (e && e.message ? e.message : e);
    alert(box.textContent);
    console.log(e);
  }
  setTimeout(postHeightToParent, 50);
}

// === Scoped Temperature_Logs Browser Functions ===
function normalizeTempLogDir(path) {
  if (!path || path[0] !== '/') path = '/' + (path || '');
  if (!path.endsWith('/')) path += '/';
  if (!path.startsWith(tempLogRoot)) path = tempLogRoot;
  return path;
}

function tempLogJoin(path, name, isDir) {
  const base = normalizeTempLogDir(path);
  return base + name + (isDir ? '/' : '');
}

function tempLogCanGoUp() {
  return tempLogCurrentPath !== tempLogRoot;
}

async function listTemperatureLogFiles() {
  const list = document.getElementById('tempLogFileList');
  list.innerHTML = '<div class="emptyNote">Loading temperature logs...</div>';

  try {
    const r = await fetch(`/fs/list?dir=${encodeURIComponent(tempLogCurrentPath)}&ts=${Date.now()}`, { cache: 'no-store' });
    const items = await r.json();
    list.innerHTML = '';

    const header = document.createElement('div');
    header.className = 'browserHeader';
    header.textContent = 'Temperature Logs';
    list.appendChild(header);

    const toolbar = document.createElement('div');
    toolbar.className = 'browserToolbar';

    const rootBtn = document.createElement('button');
    rootBtn.className = 'blue-button';
    rootBtn.textContent = 'Temperature Logs Root';
    rootBtn.onclick = () => { tempLogCurrentPath = tempLogRoot; listTemperatureLogFiles(); };
    toolbar.appendChild(rootBtn);

    const upBtn = document.createElement('button');
    upBtn.className = 'blue-button';
    upBtn.textContent = '.. (Up)';
    upBtn.disabled = !tempLogCanGoUp();
    upBtn.onclick = () => {
      if (!tempLogCanGoUp()) return;
      tempLogCurrentPath = tempLogCurrentPath.substring(0, tempLogCurrentPath.lastIndexOf('/', tempLogCurrentPath.length - 2) + 1);
      tempLogCurrentPath = normalizeTempLogDir(tempLogCurrentPath);
      listTemperatureLogFiles();
    };
    toolbar.appendChild(upBtn);
    list.appendChild(toolbar);

    const pathLine = document.createElement('div');
    pathLine.className = 'browserPath';
    pathLine.id = 'tempLogCurrentPathLine';
    pathLine.textContent = 'Current path: ' + tempLogCurrentPath;
    list.appendChild(pathLine);
    const existingArchiveBox = document.getElementById('tempArchiveStatusBox');
    if (existingArchiveBox && existingArchiveBox.textContent.trim()) {
      existingArchiveBox.style.display = 'block';
      list.appendChild(existingArchiveBox);
    }

    if (!Array.isArray(items) || items.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'emptyNote';
      empty.textContent = 'No temperature logs found.';
      list.appendChild(empty);
      setTimeout(postHeightToParent, 50);
      return;
    }

    items.forEach(item => {
      const row = document.createElement('div');
      row.className = 'browserRow ' + (item.isDir ? 'dir' : 'file');
      const fullPath = tempLogJoin(tempLogCurrentPath, item.name, item.isDir);

      const checkbox = document.createElement('input');
      checkbox.type = 'checkbox';
      checkbox.value = fullPath;
      checkbox.dataset.isdir = item.isDir ? '1' : '0';
      row.appendChild(checkbox);

      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = (item.isDir ? '[DIR] ' : '') + item.name;
      name.onclick = () => {
        if (item.isDir) {
          tempLogCurrentPath = fullPath;
          listTemperatureLogFiles();
        }
      };
      row.appendChild(name);

      const dlBtn = document.createElement('button');
      dlBtn.className = 'blue-button';
      dlBtn.textContent = item.isDir ? 'Download .tar' : 'Download';
      dlBtn.onclick = () => {
        if (item.isDir) {
          prepareArchiveDownload(fullPath);
        } else {
          window.open('/fs/download?path=' + encodeURIComponent(fullPath), '_blank');
        }
      };
      row.appendChild(dlBtn);

      list.appendChild(row);
    });
  } catch (e) {
    list.innerHTML = '<div class="emptyNote">Failed to list temperature logs.</div>';
    console.log(e);
  }

  setTimeout(postHeightToParent, 50);
}

function downloadSelectedTemperatureLogs() {
  const selected = document.querySelectorAll('#fileBrowser input[type="checkbox"]:checked');
  selected.forEach(checkbox => {
    if (checkbox.dataset.isdir === '1') {
      prepareArchiveDownload(checkbox.value);
    } else {
      window.open('/fs/download?path=' + encodeURIComponent(checkbox.value), '_blank');
    }
  });
}

  </script>
</body>
</html>
)rawliteral";



static const char *fsBrowserHtml = R"rawliteral(
<!doctype html>
<html>
<head>
  <link rel="icon" href="/static/favicon.png">
  <title>Flash Memory Browser</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { margin:0; padding:0; overflow-x:hidden; }
    *, *::before, *::after { box-sizing:border-box; }
    body { font-family: Arial, sans-serif; background:white; text-align:center; font-size:13px; }
    #pageWrap { max-width: 1000px; margin: 0 auto; padding: 4px 8px 10px; }
    h3 { color:#459; font-size:26px; line-height:1.1; margin:2px 0 6px; }
    .blue-button { background:white; color:blue; padding:0 4px; font-size:14px; cursor:pointer; border:1px solid blue; border-radius:3px; margin:1px; }
    .blue-button:hover { background:darkblue; color:white; }
    #browserBox { border:1px solid #b8c7d9; background:#fbfdff; padding:8px; text-align:left; }
    #toolbar { text-align:center; margin-bottom:6px; }
    #pathLine { text-align:center; color:purple; font-size:12px; margin:4px 0 8px; overflow-wrap:anywhere; }
    .row { display:flex; align-items:center; gap:6px; border-bottom:1px solid #eef4fb; padding:3px 0; }
    .row:last-child { border-bottom:0; }
    .name { flex:1 1 auto; overflow-wrap:anywhere; cursor:pointer; }
    .dir .name { color:#0066cc; font-weight:bold; }
    .file .name { color:blue; }
    #fileContent { white-space:pre-wrap; background:#f8f8f8; padding:8px; margin-top:8px; border:1px solid #ccc; display:none; text-align:left; max-height:420px; overflow:auto; }
    .emptyNote { text-align:center; color:#777; padding:8px; }
  </style>
</head>
<body>
<div id="pageWrap">
  <h3 id="browserTitle">Flash Memory Browser</h3>
  <div id="browserBox">
    <div id="toolbar">
      <button class="blue-button" onclick="navigate('/')">Root</button>
      <button class="blue-button" onclick="goUp()">.. (Up)</button>
      <button class="blue-button" onclick="document.getElementById('uploadInput').click()">Upload to Current</button>
      <input type="file" id="uploadInput" style="display:none;" onchange="uploadFile(this.files)">
      <button class="blue-button" onclick="createFolder()">Create Folder</button>
      <button class="blue-button" style="font-weight:bold;" onclick="downloadCurrentDir()">Download .tar</button>
    </div>
    <div id="pathLine">Current path: <span id="currentPath">/</span></div>
    <div id="fileList"></div>
    <div id="fileContent"></div>
  </div>
</div>
<script>
const params = new URLSearchParams(window.location.search);
const postType = params.get('postType') || 'fsBrowserHeight';
const title = params.get('title') || 'Flash Memory Browser';
let currentPath = normalizeDir(params.get('start') || '/');
document.getElementById('browserTitle').textContent = title;

let lastPostedHeight = 0;
function postHeightToParent() {
  if (window === window.parent) return;
  const wrap = document.getElementById('pageWrap');
  if (!wrap) return;
  const h = Math.ceil(wrap.getBoundingClientRect().height);
  if (Math.abs(h - lastPostedHeight) < 2) return;
  lastPostedHeight = h;
  window.parent.postMessage({ type: postType, height: h }, window.location.origin);
}


function fsHumanBytes(bytes) {
  bytes = Number(bytes || 0);
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let unit = 0;
  while (bytes >= 1024 && unit < units.length - 1) {
    bytes = bytes / 1024;
    unit++;
  }
  return (unit === 0 ? bytes.toFixed(0) : bytes.toFixed(1)) + ' ' + units[unit];
}
function fsHumanDuration(seconds) {
  seconds = Math.max(1, Math.round(Number(seconds || 1)));
  if (seconds < 90) return seconds + ' seconds';
  const minutes = Math.round(seconds / 60);
  if (minutes < 90) return minutes + ' minutes';
  const hours = Math.round(minutes / 60);
  return hours + ' hours';
}
function fsArchiveTimeRange(bytes) {
  bytes = Number(bytes || 0);
  const slow = bytes / (40 * 1024);
  const fast = bytes / (200 * 1024);
  return fsHumanDuration(fast) + ' to ' + fsHumanDuration(slow);
}
function fsArchivePathNoTrailing(path) {
  path = String(path || '').trim();
  if (!path.startsWith('/')) path = '/' + path;
  while (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
  return path;
}
function fsIsTemperatureLogsRootArchive(path) {
  return fsArchivePathNoTrailing(path) === '/Temperature_Logs';
}
function fsLaunchArchiveDownloadWindow(url) {
  const sep = url.indexOf('?') >= 0 ? '&' : '?';
  const finalUrl = url + sep + 'ts=' + Date.now();
  window.open(finalUrl, '_blank');
}
function fsLaunchHiddenArchiveDownload(url) {
  // Phase2V: full Flash Memory Browser archive downloads are launched
  // as real browser download navigations instead of hidden iframe streams.
  fsLaunchArchiveDownloadWindow(url);
}
function fsArchiveRawTarUrl(path) {
  path = fsArchivePathNoTrailing(path);
  return '/fs/download_raw_tar?dir=' + encodeURIComponent(path);
}
function fsOpenArchiveRawTar(path) {
  const url = fsArchiveRawTarUrl(path);
  window.open(url + (url.indexOf('?') >= 0 ? '&' : '?') + 'ts=' + Date.now(), '_blank');
}
function fsArchiveClickDebug(path, source) {
  fetch('/fs/archive_click?source=' + encodeURIComponent(source || 'fs-browser') + '&path=' + encodeURIComponent(path) + '&ts=' + Date.now(), { cache:'no-store' }).catch(console.log);
}
async function fsScanArchiveDir(path, source) {
  fsArchiveClickDebug(path, source || 'fs-browser-scan');
  const box = fsArchiveStatusBox();
  box.textContent = 'Estimating directory. No archive is being created...';
  try {
    const r = await fetch('/fs/tar_info?verbose=1&max=1500&dir=' + encodeURIComponent(path) + '&ts=' + Date.now(), { cache:'no-store' });
    const info = await r.json();
    box.textContent = 'Scan ' + (info.ok ? 'OK' : 'FAILED') + ' for ' + path + ': entries=' + (info.entries || 0) + ', dirs=' + (info.dirs || 0) + ', files=' + (info.files || 0) + ', source=' + fsHumanBytes(info.bytes || 0) + ', maxPathLen=' + (info.maxPathLen || 0) + ', longPaths=' + (info.longPaths || 0) + ', zeroFiles=' + (info.zeroFiles || 0) + (info.error ? ', error=' + info.error : '');
  } catch (e) {
    box.textContent = 'Scan request failed: ' + (e && e.message ? e.message : e);
    console.log(e);
  }
  setTimeout(postHeightToParent, 50);
}


let fsArchivePollTimer = null;
function fsArchiveStatusBox() {
  let box = document.getElementById('fsArchiveStatusBox');
  if (!box) {
    box = document.createElement('div');
    box.id = 'fsArchiveStatusBox';
    box.className = 'emptyNote';
    box.style.border = '1px solid #b8c7d9';
    box.style.margin = '4px 0';
    box.style.padding = '5px';
    box.style.whiteSpace = 'pre-wrap';
    const toolbar = document.getElementById('toolbar');
    toolbar.parentNode.insertBefore(box, toolbar.nextSibling);
  }
  box.style.display = 'block';
  setTimeout(postHeightToParent, 50);
  return box;
}
function fsArchiveCompleteLabel(failed) {
  return (failed ? 'Download Failed' : 'Download Complete') + ' - ' + new Date().toLocaleString();
}
function fsArchiveBuildBeginLine(path, info) {
  info = info || {};
  return '[RAW-TAR] stream begin dir=' + path +
    ' entries=' + (info.entries || 0) +
    ' files=' + (info.files || 0) +
    ' dirs=' + (info.dirs || 0) +
    ' sourceBytes=' + (info.bytes || info.sourceBytes || 0) +
    ' estTarBytes=' + (info.tarBytes || info.estTarBytes || 0) +
    ' filename=' + (info.filename || 'download.tar');
}
function fsArchiveCompleteFallback(path, st) {
  st = st || {};
  return '[RAW-TAR] stream complete root=' + path +
    ' bytesOut=' + (st.bytesOut || 0) +
    ' filesSent=' + (st.filesSent || 0) +
    ' dirsSent=' + (st.dirsSent || 0) +
    ' err=' + (st.error || '');
}
function fsArchiveAppendToken(url, token) {
  const sep = url.indexOf('?') >= 0 ? '&' : '?';
  return url + sep + 'token=' + encodeURIComponent(token);
}
function fsArchiveStartStatusPoll(box, path, token, beginLine) {
  if (fsArchivePollTimer) clearInterval(fsArchivePollTimer);
  let tries = 0;
  fsArchivePollTimer = setInterval(async () => {
    tries++;
    try {
      const r = await fetch('/fs/archive_status?token=' + encodeURIComponent(token) + '&ts=' + Date.now(), { cache:'no-store' });
      const st = await r.json();
      if (!st || !st.hasStatus || st.token !== token) {
        if (tries > 900) { clearInterval(fsArchivePollTimer); fsArchivePollTimer = null; }
        return;
      }
      const started = st.beginLine || beginLine;
      box.style.whiteSpace = 'pre-wrap';
      if (st.completed) {
        clearInterval(fsArchivePollTimer);
        fsArchivePollTimer = null;
        box.textContent = fsArchiveCompleteLabel(st.failed) + '\n' +
          started + '\n' +
          (st.completeLine || fsArchiveCompleteFallback(path, st));
      } else {
        box.textContent = 'Archive download in progress...\n' +
          started + '\n' +
          'Transferred so far: ' + fsHumanBytes(st.bytesOut || 0) +
          '   Files sent: ' + (st.filesSent || 0) + '/' + (st.files || 0) +
          '   Folders sent: ' + (st.dirsSent || 0) + '/' + (st.dirs || 0);
      }
      setTimeout(postHeightToParent, 50);
    } catch (e) {
      console.log(e);
      if (tries > 900) { clearInterval(fsArchivePollTimer); fsArchivePollTimer = null; }
    }
  }, 1000);
}
function fsShowArchiveStartButton(box, path, url, source, info) {
  if (!box) box = fsArchiveStatusBox();
  const beginLine = fsArchiveBuildBeginLine(path, info || {});
  const token = 'rawtar_' + Date.now() + '_' + Math.floor(Math.random() * 1000000);
  fsArchiveClickDebug(path, source || 'fs-browser-start-button');
  box.style.whiteSpace = 'pre-wrap';
  box.textContent = 'Starting archive download in a new browser tab/window...\n' + beginLine + '\nWaiting for completion...';
  fsArchiveStartStatusPoll(box, path, token, beginLine);
  fsLaunchHiddenArchiveDownload(fsArchiveAppendToken(url, token));
  setTimeout(postHeightToParent, 50);
}
async function fsPrepareArchiveDownload(path) {
  fsArchiveClickDebug(path, 'fs-browser-prepare');
  const box = fsArchiveStatusBox();
  box.textContent = 'Estimating archive size...';
  try {
    const r = await fetch('/fs/tar_info?dir=' + encodeURIComponent(path) + '&ts=' + Date.now(), { cache:'no-store' });
    const info = await r.json();
    if (!info.ok) {
      box.textContent = 'Archive estimate failed: ' + (info.error || 'unknown error');
      alert(box.textContent);
      return;
    }
    const bytes = Number(info.bytes || 0);
    let msg = 'Archive download for ' + path + '\n';
    msg += 'Source data: ' + fsHumanBytes(bytes) + '\n';
    if (info.tarBytes) msg += 'Estimated TAR size: ' + fsHumanBytes(info.tarBytes) + '\n';
    if (info.fsPctUsed !== undefined) msg += 'Filesystem used: ' + Number(info.fsPctUsed).toFixed(1) + '% (cleanup starts at ' + Number(info.cleanupStartLimit || 0).toFixed(1) + '%)\n';
    if (info.cleanupRisk) msg += 'WARNING: filesystem cleanup may be close to triggering. The cleanup task will be deferred for protected Temperature_Logs files while this archive is active.\n';
    msg += 'Files: ' + (info.files || 0) + '  Folders: ' + (info.dirs || 0) + '\n';
    msg += 'Estimated download time: ' + fsArchiveTimeRange(bytes) + '\n';
    if (info.truncated) msg += 'Estimate was truncated because the folder is very large.\n';
    msg += '\nSelect "OK" to begin the archive download in a new browser tab/window.';
    if (!confirm(msg)) {
      box.textContent = 'Archive download cancelled before start.';
      setTimeout(postHeightToParent, 50);
      return;
    }
    fsShowArchiveStartButton(box, path, '/fs/download_raw_tar?dir=' + encodeURIComponent(path), 'fs-browser-start-button', info);
  } catch (e) {
    box.textContent = 'Archive estimate failed: ' + (e && e.message ? e.message : e);
    alert(box.textContent);
    console.log(e);
  }
  setTimeout(postHeightToParent, 50);
}

function normalizeDir(path) {
  if (!path || path[0] !== '/') path = '/' + (path || '');
  if (!path.endsWith('/')) path += '/';
  return path;
}
function joinPath(path, name, isDir) {
  return normalizeDir(path) + name + (isDir ? '/' : '');
}
async function listFiles(path) {
  currentPath = normalizeDir(path);
  const list = document.getElementById('fileList');
  const content = document.getElementById('fileContent');
  content.style.display = 'none';
  content.textContent = '';
  list.innerHTML = '<div class="emptyNote">Loading...</div>';
  try {
    const r = await fetch(`/fs/list?dir=${encodeURIComponent(currentPath)}&ts=${Date.now()}`, { cache:'no-store' });
    const data = await r.json();
    list.innerHTML = '';
    if (!Array.isArray(data) || data.length === 0) {
      list.innerHTML = '<div class="emptyNote">No files or folders.</div>';
    } else {
      data.forEach(item => {
        const row = document.createElement('div');
        row.className = 'row ' + (item.isDir ? 'dir' : 'file');
        const fullPath = joinPath(currentPath, item.name, item.isDir);

        const name = document.createElement('span');
        name.className = 'name';
        name.textContent = (item.isDir ? '[DIR] ' : '') + item.name;
        name.onclick = () => item.isDir ? navigate(fullPath) : viewFile(fullPath);
        row.appendChild(name);

        const dl = document.createElement('button');
        dl.className = 'blue-button';
        dl.textContent = item.isDir ? 'Download .tar' : 'Download';
        dl.onclick = () => {
          if (item.isDir) {
            fsPrepareArchiveDownload(fullPath);
          } else {
            window.open('/fs/download?path=' + encodeURIComponent(fullPath), '_blank');
          }
        };
        row.appendChild(dl);

        const del = document.createElement('button');
        del.className = 'blue-button';
        del.textContent = 'Delete';
        del.onclick = () => {
          if (confirm('Delete ' + item.name + '?')) {
            fetch('/fs/delete?path=' + encodeURIComponent(fullPath), { method:'DELETE' }).then(() => listFiles(currentPath));
          }
        };
        row.appendChild(del);
        list.appendChild(row);
      });
    }
  } catch (e) {
    list.innerHTML = '<div class="emptyNote">Failed to list files.</div>';
    console.log(e);
  }
  document.getElementById('currentPath').textContent = currentPath;
  setTimeout(postHeightToParent, 50);
}
function navigate(path) { listFiles(path); }
function goUp() {
  if (currentPath === '/') return;
  currentPath = currentPath.substring(0, currentPath.lastIndexOf('/', currentPath.length - 2) + 1);
  listFiles(currentPath);
}
async function viewFile(path) {
  try {
    const r = await fetch('/fs/view?path=' + encodeURIComponent(path));
    const text = await r.text();
    const content = document.getElementById('fileContent');
    content.textContent = text;
    content.style.display = 'block';
  } catch(e) { console.log(e); }
  setTimeout(postHeightToParent, 50);
}
function uploadFile(files) {
  if (!files.length) return;
  const form = new FormData();
  form.append('file', files[0]);
  fetch('/fs/upload?dir=' + encodeURIComponent(currentPath), { method:'POST', body:form }).then(() => listFiles(currentPath));
}
function createFolder() {
  const name = prompt('New folder name:');
  if (!name) return;
  fetch('/fs/mkdir?path=' + encodeURIComponent(normalizeDir(currentPath) + name), { method:'POST' }).then(() => listFiles(currentPath));
}
function downloadCurrentDir() {
  fsPrepareArchiveDownload(currentPath === '/' ? '/' : currentPath.replace(/\/$/, ''));
}
window.addEventListener('resize', () => setTimeout(postHeightToParent, 50), { passive:true });
listFiles(currentPath);
</script>
</body>
</html>
)rawliteral";

void setupThirdPageRoutes() {
  server.on("/third-page", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *response = req->beginResponse(200, "text/html", thirdPageHtml);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    req->send(response);
  });


  server.on("/fs-browser", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *response = req->beginResponse(200, "text/html", fsBrowserHtml);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    req->send(response);
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
      if ((lineCount % 25) == 0) { // Safety valve: Yield to prevent Watchdog panic
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    f.close();
    xSemaphoreGive(fileSystemMutex);

    // Safety valve before heavy JSON serialization
    vTaskDelay(pdMS_TO_TICKS(1)); 

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });


  // === LittleFS Browser Routes ===
  // List directory
  server.on("/fs/list", HTTP_GET, [](AsyncWebServerRequest *req) {
    String dir = req->hasParam("dir") ? req->getParam("dir")->value() : "/";
    if (!dir.startsWith("/")) dir = "/" + dir;
    dir = normalizeFsPath(dir, false);

    if (!isSafePath(dir)) {
      req->send(400, "application/json", "[]");
      return;
    }

    // If the user is browsing Pump_Logs, flush RAM-cached START/STOP events first
    // so newly-created/updated pump log files are visible before listing/downloading.
    if (dir == "/Pump_Logs" || dir.startsWith("/Pump_Logs/")) {
      (void)flushPendingPumpLogEvents(pdMS_TO_TICKS(1500), 2);
    }

    if (!takeFileSystemMutexWithRetry("[FS] /fs/list",
                                      pdMS_TO_TICKS(2000), 3)) {
      req->send(503, "application/json", "[]");
      return;
    }

    File root = LittleFS.open(dir, "r");
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
      vTaskDelay(pdMS_TO_TICKS(1)); // Safety valve: Yield on every file

      String full = String(entry.name());
      bool entryIsDir = entry.isDirectory();
      entry.close();  // ✅ IMPORTANT: close each entry before doing anything else

      // LittleFS/openNextFile() can return recursive children depending on core/build.
      // The browser needs a normal directory view, so only emit the immediate child
      // for the requested directory and synthesize direct child directories when needed.
      String parent = normalizeFsPath(dir, false);
      String prefix = (parent == "/") ? "/" : (parent + "/");

      if (!full.startsWith("/")) {
        full = (parent == "/") ? ("/" + full) : (parent + "/" + full);
      }

      if (!full.startsWith(prefix)) {
        entry = root.openNextFile();
        continue;
      }

      String rel = full.substring(prefix.length());
      if (rel.length() == 0) {
        entry = root.openNextFile();
        continue;
      }

      int slash = rel.indexOf('/');
      String displayName;
      bool displayIsDir = entryIsDir;

      if (slash >= 0) {
        displayName = rel.substring(0, slash);
        displayIsDir = true;
      } else {
        displayName = rel;
      }

      if (displayName.length() > 0 && !jsonListAlreadyHasName(arr, displayName)) {
        JsonObject obj = arr.createNestedObject();
        obj["name"] = displayName;
        obj["isDir"] = displayIsDir;
      }

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
    path = normalizeFsPath(path, false);

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
    path = normalizeFsPath(path, false);

    if (!isSafePath(path) || path == "/") {
      req->send(400, "text/plain", "bad path");
      return;
    }

    // Flush RAM-cached pump events before direct pump-log file downloads.
    if (path.startsWith("/Pump_Logs/")) {
      (void)flushPendingPumpLogEvents(pdMS_TO_TICKS(1500), 2);
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


  
  // Download directories as streamed raw TAR
  RawTar::registerRoutes(server);





  // Delete file/dir
  server.on("/fs/delete", HTTP_DELETE, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("path")) {
      req->send(400, "text/plain", "path missing");
      return;
    }

    String path = req->getParam("path")->value();
    if (!path.startsWith("/")) path = "/" + path;
    path = normalizeFsPath(path, false);

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
