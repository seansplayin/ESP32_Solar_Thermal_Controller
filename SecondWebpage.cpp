// SecondWebpage.cpp
#include "Config.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "RTCManager.h"
#include "WebServerManager.h"
#include "FileSystemManager.h"
#include "PumpManager.h"
#include "DiagLog.h"


extern AsyncWebServer server;  // declared in WebServerManager.cpp

// --- 1) Immutable HTML template with placeholders ---
static const char *secondPageTemplate = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <link rel="icon" href="/static/favicon.png">
  <title>Pump Runtimes & Logs</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { margin: 0; padding: 0; }

    body { 
    font-family: Arial; 
    text-align: center; 
    }

    #pumpGrid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 5px; max-width: 1000px; margin: auto; }
    .grid-header { font-weight: bold; }
    .header-with-date { white-space: nowrap; }
    #buttonContainer { margin: 20px; }
    #filesContainer { display: none; text-align: left; max-width: 400px; margin: auto; }
  
    h3.top-heading {
    color:#459;
    font-size:40px;
    font-weight:bold;
    text-align:center;

    margin: 2px 0 6px 0;   /* ✅ pulls title up; adds a tiny gap below */
    padding: 0;
    line-height: 1.0;
    }

    h3.top-heading a {
    color: #459;
    text-decoration: none;
    }

    h3.top-heading a:hover {
    text-decoration: underline;
    }

#updateAllButton { margin-top: 0px; }

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
     }
    .blue-button:focus {
     outline: 2px solid rgba(0,0,255,0.6);
     outline-offset: 2px;
     }
  </style>
</head>
<body>
  <h3 class="top-heading">
  <a id="pumpRuntimesLink" target="_blank">Pump Runtimes</a>
</h3>
  <button id="updateAllButton" class="blue-button">Update All</button>

  <div id="pumpGrid">
    <div class="grid-header">Pump</div>
    <div class="grid-header header-with-date">Today<br>(%CURRENT_DAY%)</div>
    <div class="grid-header header-with-date">Yesterday<br>(%PREVIOUS_DAY%)</div>
    <div class="grid-header header-with-date">This Month<br>(%CURRENT_MONTH%)</div>
    <div class="grid-header header-with-date">Last Month<br>(%PREVIOUS_MONTH%)</div>
    <div class="grid-header header-with-date">This Year<br>(%CURRENT_YEAR%)</div>
    <div class="grid-header header-with-date">Last Year<br>(%PREVIOUS_YEAR%)</div>
    <div class="grid-header">Total</div>
    %PUMP_ROWS%
  </div>

  <div id="buttonContainer">
    <button id="listFilesButton"    class="blue-button">List Files</button>
    <button id="downloadFilesButton" class="blue-button" style="display:none;">Download Selected</button>
  </div>
  <div id="filesContainer"></div>





  <script>
    // Format seconds -> Hh Mm Ss
    function formatRuntime(rt) {
      const h = Math.floor(rt/3600),
            m = Math.floor((rt%3600)/60),
            s = rt % 60;
      return `${h}h ${m}m ${s}s`;
    }

            document.addEventListener('DOMContentLoaded', ()=> {

      const sleep = (ms) => new Promise(r => setTimeout(r, ms));

      const updateBtn = document.getElementById('updateAllButton');
      let isUpdating = false;
      const updateBtnDefaultText = updateBtn.textContent;

      function applyRuntimes(obj) {
        if (!obj || !obj.data) return;
        obj.data.forEach(p => {

          document.getElementById(`pump${p.pumpIndex}-day`).textContent       = formatRuntime(p.day);
          document.getElementById(`pump${p.pumpIndex}-prevDay`).textContent   = formatRuntime(p.prevDay);
          document.getElementById(`pump${p.pumpIndex}-month`).textContent     = formatRuntime(p.month);
          document.getElementById(`pump${p.pumpIndex}-prevMonth`).textContent = formatRuntime(p.prevMonth);
          document.getElementById(`pump${p.pumpIndex}-year`).textContent      = formatRuntime(p.year);
          document.getElementById(`pump${p.pumpIndex}-prevYear`).textContent  = formatRuntime(p.prevYear);
          document.getElementById(`pump${p.pumpIndex}-total`).textContent     = formatRuntime(p.total);
        });
      }

          async function fetchLatestJson() {
        const r = await fetch(`/api/pump-runtimes?ts=${Date.now()}`, { cache: 'no-store' });
        if (!r.ok) throw new Error('Failed /api/pump-runtimes');
        return await r.json();
      }

      async function updateAllRuntimesViaFetch() {
        if (isUpdating) return;               // prevent stacking
        isUpdating = true;
        updateBtn.disabled = true;

        // UX timer: show elapsed seconds while updating
        let timerId = null;
        const startMs = Date.now();

        const updateButtonLabel = () => {
          const elapsedS = Math.max(0, Math.floor((Date.now() - startMs) / 1000));
          updateBtn.textContent = `Updating... (${elapsedS}s)`;
        };

        updateButtonLabel();                  // set immediately
        timerId = setInterval(updateButtonLabel, 500);  // refresh label twice/sec

        try {
          // 1) request refresh (kicks background task)
          const r = await fetch(`/api/pump-runtimes?refresh=1&ts=${Date.now()}`, { cache: 'no-store' });
          if (!r.ok) throw new Error('Failed refresh request');
          const meta = await r.json();
          const targetVersion = meta.requestedVersion;

          // 2) poll until the built JSON version matches what we requested
          const deadline = startMs + 15000; // 15s safety

          while (Date.now() < deadline) {
            const obj = await fetchLatestJson();
            if (obj && obj.version === targetVersion) {
              applyRuntimes(obj);
              return;
            }

            // Backoff: 0-2s => 250ms, 2-6s => 500ms, 6s+ => 1000ms
            const elapsed = Date.now() - startMs;
            const delayMs = (elapsed < 2000) ? 250 : (elapsed < 6000) ? 500 : 1000;
            await sleep(delayMs);
          }

          throw new Error('Timeout waiting for runtimes');
        } finally {
          if (timerId) { clearInterval(timerId); timerId = null; }
          updateBtn.disabled = false;
          updateBtn.textContent = updateBtnDefaultText;
          isUpdating = false;
        }
      }

      // Load once on page open
      updateAllRuntimesViaFetch().catch(console.log);



      // Update All button
      updateBtn.onclick = () => {
        updateAllRuntimesViaFetch().catch(console.log);
      };

      // —— File listing & download —— 
      document.getElementById('listFilesButton').addEventListener('click', function() {

                const container = document.getElementById('filesContainer');
                const downloadButton = document.getElementById('downloadFilesButton');
                // Toggle visibility
                if (container.style.display === 'none' || container.style.display === '') {
                    fetchFileList();  // Call fetchFileList to update the file list
                    container.style.display = 'block';
                    downloadButton.style.display = 'inline'; // Show download button when listing files
                } else {
                    container.style.display = 'none';
                    downloadButton.style.display = 'none'; // Hide download button when not listing files
                }
            });

            function fetchFileList() {
                fetch('/list-logs')
                .then(response => response.json())
                .then(files => {
                    const list = document.getElementById('filesContainer');
                    list.innerHTML = ''; // Clear current list
                    files.forEach(file => {
                        const label = document.createElement('label');
                        const checkbox = document.createElement('input');
                        checkbox.type = 'checkbox';
                        checkbox.value = file;
                        label.appendChild(checkbox);
                        label.appendChild(document.createTextNode(file));
                        list.appendChild(label);
                        list.appendChild(document.createElement('br'));
                    });
                });
            }

            document.getElementById('downloadFilesButton').addEventListener('click', function() {
                downloadSelected();
            });

            function downloadSelected() {
                const checkboxes = document.querySelectorAll('#filesContainer input[type="checkbox"]:checked');
                checkboxes.forEach(checkbox => {
                    const file = checkbox.value;
                    window.open('/download-log?file=' + encodeURIComponent(file), '_blank');
                });
            }
        });
     document
    .getElementById('pumpRuntimesLink')
    .href = window.location.origin + '/second-page';
  </script>
</body>
</html>
)rawliteral";


// --- 2) Generate pump rows HTML once ---
static String pumpRowsHtml = []() {
  String rows;
  for (int i = 0; i < 10; ++i) {
    rows += "<div class=\"grid-cell\">" + String(pumpNames[i]) + "</div>";
    rows += "<div class=\"grid-cell\" id=\"pump" + String(i + 1) + "-day\">--</div>";
    rows += "<div class=\"grid-cell\" id=\"pump" + String(i + 1) + "-prevDay\">--</div>";
    rows += "<div class=\"grid-cell\" id=\"pump" + String(i + 1) + "-month\">--</div>";
    rows += "<div class=\"grid-cell\" id=\"pump" + String(i + 1) + "-prevMonth\">--</div>";
    rows += "<div class=\"grid-cell\" id=\"pump" + String(i + 1) + "-year\">--</div>";
    rows += "<div class=\"grid-cell\" id=\"pump" + String(i + 1) + "-prevYear\">--</div>";
    rows += "<div class=\"grid-cell\" id=\"pump" + String(i + 1) + "-total\">--</div>";
  }
  return rows;
}();

// --- 3) Helpers ---
String monthToString(int m) {
  static const char *mon[] = { "January", "February", "March", "April", "May", "June",
                               "July", "August", "September", "October", "November", "December" };
  return (m >= 1 && m <= 12) ? mon[m - 1] : "Unknown";
}

void getCurrentDateInfo(
  DateTime now,
  String &cd, String &cm, String &cy,
  String &pd, String &pm, String &py) {
  cd = String(now.year()) + "-" + (now.month() < 10 ? "0" : "") + String(now.month()) + "-" + (now.day() < 10 ? "0" : "") + String(now.day());
  cm = String(now.year()) + "-" + (now.month() < 10 ? "0" : "") + String(now.month());
  cy = String(now.year());
  DateTime y = now - TimeSpan(1, 0, 0, 0);
  pd = String(y.year()) + "-" + (y.month() < 10 ? "0" : "") + String(y.month()) + "-" + (y.day() < 10 ? "0" : "") + String(y.day());
  pm = String(y.year()) + "-" + (y.month() < 10 ? "0" : "") + String(y.month());
  py = String(now.year() - 1);
}

// --- 4) Helper: process pump log files ---
void readPumpLogFiles(
  DateTime currentTime,
  unsigned long *todayRuntimeArray,
  unsigned long *yesterdayRuntimeArray,
  unsigned long *thisMonthRuntimeArray,
  unsigned long *lastMonthRuntimeArray,
  unsigned long *thisYearRuntimeArray,
  unsigned long *lastYearRuntimeArray,
  unsigned long *totalRuntimeArray
  ) {
  // Initialize all arrays
  for (int i = 0; i < 10; i++) {
    todayRuntimeArray[i] = 0;
    yesterdayRuntimeArray[i] = 0;
    thisMonthRuntimeArray[i] = 0;
    lastMonthRuntimeArray[i] = 0;
    thisYearRuntimeArray[i] = 0;
    lastYearRuntimeArray[i] = 0;
    totalRuntimeArray[i] = 0;
  }

  DateTime yesterdayTime = currentTime - TimeSpan(1, 0, 0, 0);
  int currentMonth = currentTime.month();
  int lastMonth = currentMonth == 1 ? 12 : currentMonth - 1;
  int currentYear = currentTime.year();
  int lastMonthYear = currentMonth == 1 ? currentYear - 1 : currentYear;
  int lastYear = currentYear - 1;

  for (int i = 1; i <= 10; i++) {
    LOG_CAT(DBG_PUMPLOG, "[PumpRuntimes] Processing Pump %d\n", i);


  // Daily log
  String dailyFileName = "/Pump_Logs/pump" + String(i) + "_Daily.txt";
  File dailyFile = openLogFileLocked(dailyFileName, "r");
  if (dailyFile) {
    while (dailyFile.available()) {
      String line = dailyFile.readStringUntil('\n');
      line.trim();
      int ds = line.indexOf(' ');
      int ms = line.indexOf("Total Runtime:");
      int ss = line.indexOf(" seconds");
      if (ds != -1 && ms != -1 && ss != -1) {
        String date = line.substring(0, ds);
        unsigned long rt = line.substring(ms + 14, ss).toInt();
        int y = date.substring(0, 4).toInt();
        int m = date.substring(5, 7).toInt();
        int d = date.substring(8, 10).toInt();
        DateTime logDate(y, m, d);

        if (y == currentYear && m == currentMonth && d == currentTime.day()) {
          todayRuntimeArray[i - 1] += rt;
          thisMonthRuntimeArray[i - 1] += rt;
        }
        if (y == yesterdayTime.year() && m == yesterdayTime.month() && d == yesterdayTime.day()) {
          yesterdayRuntimeArray[i - 1] += rt;
        }
      }
    }
    closeLogFileLocked(dailyFile);
  }


  // Monthly log
  String monthlyFileName = "/Pump_Logs/pump" + String(i) + "_Monthly.txt";
  File monthlyFile = openLogFileLocked(monthlyFileName, "r");
  if (monthlyFile) {
    while (monthlyFile.available()) {
      String line = monthlyFile.readStringUntil('\n');
      line.trim();
      int s = line.indexOf(' ');
      if (s != -1) {
        String date = line.substring(0, s);
        unsigned long rt = line.substring(s + 1).toInt();
        int y = date.substring(0, 4).toInt();
        int m = date.substring(5, 7).toInt();
        if (y == currentYear && m == currentMonth) thisMonthRuntimeArray[i - 1] += rt;
        if (y == lastMonthYear && m == lastMonth) lastMonthRuntimeArray[i - 1] += rt;
      }
    }
    closeLogFileLocked(monthlyFile);
  }


  // Yearly log
  String yearlyFileName = "/Pump_Logs/pump" + String(i) + "_Yearly.txt";
  File yearlyFile = openLogFileLocked(yearlyFileName, "r");
  if (yearlyFile) {
    while (yearlyFile.available()) {
      String line = yearlyFile.readStringUntil('\n');
      line.trim();
      int s = line.indexOf(' ');
      if (s != -1) {
        String date = line.substring(0, s);
        unsigned long rt = line.substring(s + 1).toInt();
        int y = date.substring(0, 4).toInt();
        if (y == currentYear) thisYearRuntimeArray[i - 1] += rt;
        if (y == lastYear) lastYearRuntimeArray[i - 1] += rt;
        totalRuntimeArray[i - 1] += rt;
      }
    }
    closeLogFileLocked(yearlyFile);
  }
  }
  }

  // --- 5) Register the route ---
  void setupSecondPageRoutes() {
    server.on("/second-page", HTTP_GET, [](AsyncWebServerRequest *request) {
      DateTime now = getCurrentTimeAtomic();
      String cd, cm, cy, pd, pm, py;
      getCurrentDateInfo(now, cd, cm, cy, pd, pm, py);
      String page = secondPageTemplate;
      page.replace("%PUMP_ROWS%", pumpRowsHtml);
      page.replace("%CURRENT_DAY%", cd);
      page.replace("%PREVIOUS_DAY%", pd);
      page.replace("%CURRENT_MONTH%", cm);
      page.replace("%PREVIOUS_MONTH%", pm);
      page.replace("%CURRENT_YEAR%", cy);
      page.replace("%PREVIOUS_YEAR%", py);
      request->send_P(200, "text/html", page.c_str());
    });
  }
