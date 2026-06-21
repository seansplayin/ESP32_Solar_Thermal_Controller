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
static const char secondPageTemplate[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <link rel="icon" href="/static/favicon.png">
  <title>Pump Runtimes & Logs</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { margin: 0; padding: 0; }

    body {
      font-family:'Lucida Sans Unicode', 'Lucida Grande', sans-serif, Helvetica;
      font-size:13px;
      line-height:1.0;
      text-align:center;
      box-sizing: border-box;
      background: white;
      padding-bottom: 15px;
    }

    *, *:before, *:after { box-sizing: inherit; }

    .pumpRuntimeGridWrap {
      overflow-x: auto;
      padding-bottom: 0;
    }

    #pumpGrid {
      table-layout: fixed;
      border-collapse: separate;
      /* for border-spacing Left/Right gap is the first number (4px), Top/Bottom gap is the second number (8px) */
      border-spacing: 4px 6px;
    }

    #pumpGrid th, #pumpGrid td {
      border: 1px solid #b8c7d9;
      /* for padding Top/Bottom gap is the first number (1px), Left/Right gap is the second number (2px) */
      padding: 1px 2px;
      font-size: 11.5px;
      vertical-align: middle;
      color: #000000;
    }

    #pumpGrid th {
      color: #459;
      font-weight: bold;
      text-align: center;
      background: #eef4fb;
      white-space: normal;
    }

    #pumpGrid .pump-name {
      text-align: left;
      color: purple;
      font-weight: bold;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    #pumpGrid .runtime-value {
      color: blue;
      white-space: normal;
      overflow-wrap: anywhere;
    }

    .header-with-date { white-space: normal; }
    #buttonContainer { margin: 4px 0 0 0; }
    #filesContainer {
      display: none;
      text-align: left;
      width: auto;
      margin: 6px auto 0;
      border: 1px solid #b8c7d9;
      background: #fbfdff;
      padding: 6px;
    }
    #filesContainer .browserHeader {
      text-align: center;
      color: #459;
      font-weight: bold;
      margin-bottom: 4px;
    }
    #filesContainer .browserToolbar {
      text-align: center;
      margin-bottom: 5px;
    }
    #filesContainer .browserPath {
      text-align: center;
      font-size: 11px;
      color: purple;
      margin: 3px 0 5px 0;
      overflow-wrap: anywhere;
    }
    #filesContainer .browserRow {
      display: flex;
      align-items: center;
      gap: 5px;
      padding: 2px 0;
      font-size: 12px;
      border-bottom: 1px solid #eef4fb;
    }
    #filesContainer .browserRow:last-child { border-bottom: 0; }
    #filesContainer .browserRow .name {
      flex: 1 1 auto;
      overflow-wrap: anywhere;
      cursor: pointer;
    }
    #filesContainer .browserRow.dir .name {
      color: #0066cc;
      font-weight: bold;
    }
    #filesContainer .browserRow.file .name { color: blue; }
    #filesContainer .emptyNote {
      text-align: center;
      color: #777;
      font-size: 12px;
      padding: 8px;
    }

    h3.top-heading {
      color:#459;
      font-size:22px;
      font-weight:bold;
      text-align:center;
      margin: -1px 0 3px 0; // Margin sets vertacle spacing
      padding: 0;
      line-height: 1.3;
    }

    h3.top-heading a {
      color: #459;
      text-decoration: none;
    }

    h3.top-heading a:hover {
      text-decoration: underline;
    }

    /* Remove default browser margin to free up horizontal space */
    body { margin: 2px; }
    #pageWrap { max-width: 1000px; margin: 0 auto; padding: 0; }
    #updateAllButton { margin-top: 10px; padding: 4px 12px; } /* Added padding for better clickability */

    .blue-button {
      background-color: white;
      color: blue;
      padding: 0px 4px;
      font-size: 14px;
      cursor: pointer;
      border: 1px solid blue;
      border-radius: 3px;
    }
    .blue-button:hover { background-color: darkblue; color:white; }
    .blue-button:focus {
      outline: 2px solid rgba(0,0,255,0.6);
      outline-offset: 2px;
    }
  </style>

  <script>
    // Safely inject percent CSS rules without confusing the ESPAsyncWebServer template processor
    document.addEventListener('DOMContentLoaded', function() {
      const p = String.fromCharCode(37);
      const style = document.createElement('style');
      style.innerHTML = `
        .pumpRuntimeGridWrap { width: 100${p}; }
        #pumpGrid { width: 100${p}; }
        #pumpGrid col:nth-child(1) { width: 13${p}; }   /* Shrunk from 18 to give dates more room */
        #pumpGrid col:nth-child(2) { width: 12.4${p}; } /* Expanded */
        #pumpGrid col:nth-child(3) { width: 12.4${p}; }
        #pumpGrid col:nth-child(4) { width: 12.4${p}; }
        #pumpGrid col:nth-child(5) { width: 12.4${p}; }
        #pumpGrid col:nth-child(6) { width: 12.4${p}; }
        #pumpGrid col:nth-child(7) { width: 12.4${p}; }
        #pumpGrid col:nth-child(8) { width: 12.6${p}; }
      `;
      document.head.appendChild(style);
    });
  </script>
</head>
<body>
  <div id="pageWrap">
  <h3 class="top-heading">
  <a id="pumpRuntimesLink" target="_blank" title="Open the full Pump Runtimes page">Pump Runtimes</a>
</h3>
  <button id="updateAllButton" class="blue-button">Update All</button>

  <div class="pumpRuntimeGridWrap">
    <table id="pumpGrid">
      <colgroup>
        <col><col><col><col><col><col><col><col>
      </colgroup>
      <thead>
        <tr>
          <th>Pump</th>
          <th class="header-with-date">Today<br>%CURRENT_DAY%</th>
          <th class="header-with-date">Yesterday<br>%PREVIOUS_DAY%</th>
          <th class="header-with-date">This Month<br>%CURRENT_MONTH%</th>
          <th class="header-with-date">Last Month<br>%PREVIOUS_MONTH%</th>
          <th class="header-with-date">This Year<br>%CURRENT_YEAR%</th>
          <th class="header-with-date">Last Year<br>%PREVIOUS_YEAR%</th>
          <th>Total</th>
        </tr>
      </thead>
      <tbody>
        %PUMP_ROWS%
      </tbody>
    </table>
  </div>

  <div id="buttonContainer">
    <button id="listFilesButton"    class="blue-button">View Pump Logs</button>
    <button id="downloadFilesButton" class="blue-button" style="display:none;">Download Selected</button>
    <button id="downloadAllFilesButton" class="blue-button" style="display:none;">Download All</button>
  </div>
  <div id="filesContainer" style="display:none;">
    <div id="pumpLogFileList"></div>
    <div id="pumpArchiveStatusBox" class="emptyNote" style="display:none; border:1px solid #b8c7d9; margin:4px 0; padding:5px; white-space:pre-wrap;"></div>
  </div>

  </div> <script>
    // Format seconds -> Hh Mm Ss
    function formatRuntime(rt) {
      const h = Math.floor(rt/3600);
      const rem = rt - (h * 3600);
      const m = Math.floor(rem/60);
      const s = rem - (m * 60);
      return `${h}h ${m}m ${s}s`;
    }

            let lastPostedHeight = 0;
            function postHeightToParent() {
              if (window === window.parent) return; // not inside iframe
              const wrap = document.getElementById('pageWrap');
              if (!wrap) return;
              const top = wrap.getBoundingClientRect().top;
              const last = wrap.lastElementChild || wrap;
              const bottom = last.getBoundingClientRect().bottom;
              const h = Math.ceil(bottom - top);
              if (Math.abs(h - lastPostedHeight) < 2) return;
              lastPostedHeight = h;
              window.parent.postMessage({ type: "secondPageHeight", height: h }, "*");
            }

            document.addEventListener('DOMContentLoaded', ()=> {
      setTimeout(postHeightToParent, 50);

      const sleep = (ms) => new Promise(r => setTimeout(r, ms));

      const updateBtn = document.getElementById('updateAllButton');
      let isUpdating = false;
      const updateBtnDefaultText = updateBtn.textContent;

      function setRuntimeCell(id, value) {
        const el = document.getElementById(id);
        if (el) el.textContent = formatRuntime(value || 0);
      }

      function applyRuntimes(obj) {
        if (!obj || !obj.data) return;

        obj.data.forEach(p => {
          if (!p || !p.pumpIndex) return;

          setRuntimeCell(`pump${p.pumpIndex}-day`,       p.day);
          setRuntimeCell(`pump${p.pumpIndex}-prevDay`,   p.prevDay);
          setRuntimeCell(`pump${p.pumpIndex}-month`,     p.month);
          setRuntimeCell(`pump${p.pumpIndex}-prevMonth`, p.prevMonth);
          setRuntimeCell(`pump${p.pumpIndex}-year`,      p.year);
          setRuntimeCell(`pump${p.pumpIndex}-prevYear`,  p.prevYear);
          setRuntimeCell(`pump${p.pumpIndex}-total`,     p.total);
        });
        setTimeout(postHeightToParent, 50);
      }

          async function fetchLatestJson() {
        const r = await fetch(`/api/pump-runtimes?ts=${Date.now()}`, { cache: 'no-store' });
        if (!r.ok) throw new Error('Failed /api/pump-runtimes');
        return await r.json();
      }

      async function updateAllRuntimesViaFetch() {
        if (isUpdating) return;               // prevent stacking within this page instance
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
          // First, paint any previously-built runtime JSON so the grid does not sit blank.
          try {
            const existing = await fetchLatestJson();
            if (existing && Number(existing.version || 0) > 0 && existing.data) {
              applyRuntimes(existing);
            }
          } catch (e) {
            console.log('No existing runtime JSON available yet:', e);
          }

          // 1) request refresh (kicks background task)
          const r = await fetch(`/api/pump-runtimes?refresh=1&ts=${Date.now()}`, { cache: 'no-store' });
          if (!r.ok) throw new Error('Failed refresh request');
          const meta = await r.json();
          const targetVersion = Number(meta.requestedVersion || 0);

          // 2) Poll until the built JSON reaches at least the requested version.
          // Runtime aggregation can take longer than 15s when LittleFS logs are large or FS is busy.
          // Also, if two pages request updates, the server may build a newer version than this page requested.
          const deadline = startMs + 60000; // 60s safety

          while (Date.now() < deadline) {
            const obj = await fetchLatestJson();
            const builtVersion = Number((obj && obj.version) || 0);

            if (obj && obj.data && builtVersion >= targetVersion) {
              applyRuntimes(obj);
              return;
            }

            // Backoff: 0-2s => 250ms, 2-6s => 500ms, 6s+ => 1000ms
            const elapsed = Date.now() - startMs;
            const delayMs = (elapsed < 2000) ? 250 : (elapsed < 6000) ? 500 : 1000;
            await sleep(delayMs);
          }

          // Last chance: apply whatever latest runtime JSON exists, even if the requested refresh
          // did not complete before the UI timeout.
          const latest = await fetchLatestJson();
          if (latest && latest.data && Number(latest.version || 0) > 0) {
            applyRuntimes(latest);
            console.log('Applied latest available runtimes after refresh timeout.');
            return;
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

      // —— Scoped Pump Log browser/downloader ——
      const pumpLogRoot = '/Pump_Logs/';
      let pumpLogCurrentPath = pumpLogRoot;

      function normalizePumpLogDir(path) {
        if (!path || path[0] !== '/') path = '/' + (path || '');
        if (!path.endsWith('/')) path += '/';
        if (!path.startsWith(pumpLogRoot)) path = pumpLogRoot;
        return path;
      }

      function pumpLogJoin(path, name, isDir) {
        const base = normalizePumpLogDir(path);
        return base + name + (isDir ? '/' : '');
      }

      function pumpLogCanGoUp() {
        return pumpLogCurrentPath !== pumpLogRoot;
      }

      function setPumpLogButtonsVisible(visible) {
        document.getElementById('downloadFilesButton').style.display = visible ? 'inline' : 'none';
        document.getElementById('downloadAllFilesButton').style.display = visible ? 'inline' : 'none';
      }


      let archivePollTimer = null;

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

      function launchArchiveDownloadWindow(url) {
        const sep = url.indexOf('?') >= 0 ? '&' : '?';
        const finalUrl = url + sep + 'ts=' + Date.now();
        window.open(finalUrl, '_blank');
      }

      function launchHiddenArchiveDownload(url) {
        // Phase2M: launch archive downloads as real browser
        // new-tab/download navigations instead of hidden iframe streams.
        launchArchiveDownloadWindow(url);
      }

      function archiveClickDebug(path, source) {
        fetch('/fs/archive_click?source=' + encodeURIComponent(source || 'pump-section') + '&path=' + encodeURIComponent(path) + '&ts=' + Date.now(), { cache:'no-store' }).catch(console.log);
      }

      function archiveStatusBox() {
        let box = document.getElementById('pumpArchiveStatusBox');
        if (!box) {
          box = document.createElement('div');
          box.id = 'pumpArchiveStatusBox';
          box.className = 'emptyNote';
          box.style.border = '1px solid #b8c7d9';
          box.style.margin = '4px 0';
          box.style.padding = '5px';
          box.style.whiteSpace = 'pre-wrap';
        }
        const pathLine = document.getElementById('pumpLogCurrentPathLine');
        if (pathLine && pathLine.parentNode) {
          pathLine.parentNode.insertBefore(box, pathLine.nextSibling);
        } else {
          const container = document.getElementById('filesContainer');
          if (container && box.parentNode !== container) container.appendChild(box);
        }
        box.style.display = 'block';
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
        archiveClickDebug(path, source || 'pump-section-start-button');
        box.style.whiteSpace = 'pre-wrap';
        box.textContent = 'Starting archive download in a new browser tab/window...\n' + beginLine + '\nWaiting for completion...';
        archiveStartStatusPoll(box, path, token, beginLine);
        launchHiddenArchiveDownload(archiveAppendToken(url, token));
      }

      async function prepareArchiveDownload(path) {
        archiveClickDebug(path, 'pump-section-prepare');
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
            return;
          }
          showArchiveStartButton(box, path, '/fs/download_raw_tar?dir=' + encodeURIComponent(path), 'pump-section-start-button', info);
        } catch (e) {
          box.textContent = 'Archive estimate failed.';
          console.log(e);
        }
      }

      document.getElementById('listFilesButton').addEventListener('click', function() {
        const container = document.getElementById('filesContainer');
        const opening = (container.style.display === 'none' || container.style.display === '');
        if (opening) {
          container.style.display = 'block';
          this.textContent = 'Hide Pump Logs';
          setPumpLogButtonsVisible(true);
          pumpLogCurrentPath = pumpLogRoot;
          fetchPumpLogFileList();
        } else {
          container.style.display = 'none';
          this.textContent = 'View Pump Logs';
          setPumpLogButtonsVisible(false);
          requestAnimationFrame(() => requestAnimationFrame(postHeightToParent));
        }
      });

      async function fetchPumpLogFileList() {
        const list = document.getElementById('pumpLogFileList');
        list.innerHTML = '<div class="emptyNote">Loading pump logs...</div>';
        try {
          const r = await fetch(`/fs/list?dir=${encodeURIComponent(pumpLogCurrentPath)}&ts=${Date.now()}`, { cache: 'no-store' });
          const items = await r.json();
          list.innerHTML = '';

          const header = document.createElement('div');
          header.className = 'browserHeader';
          header.textContent = 'Pump Logs';
          list.appendChild(header);

          const toolbar = document.createElement('div');
          toolbar.className = 'browserToolbar';
          const rootBtn = document.createElement('button');
          rootBtn.className = 'blue-button';
          rootBtn.textContent = 'Pump Logs Root';
          rootBtn.onclick = () => { pumpLogCurrentPath = pumpLogRoot; fetchPumpLogFileList(); };
          toolbar.appendChild(rootBtn);

          const upBtn = document.createElement('button');
          upBtn.className = 'blue-button';
          upBtn.textContent = '.. (Up)';
          upBtn.disabled = !pumpLogCanGoUp();
          upBtn.onclick = () => {
            if (!pumpLogCanGoUp()) return;
            pumpLogCurrentPath = pumpLogCurrentPath.substring(0, pumpLogCurrentPath.lastIndexOf('/', pumpLogCurrentPath.length - 2) + 1);
            pumpLogCurrentPath = normalizePumpLogDir(pumpLogCurrentPath);
            fetchPumpLogFileList();
          };
          toolbar.appendChild(upBtn);
          list.appendChild(toolbar);

          const pathLine = document.createElement('div');
          pathLine.className = 'browserPath';
          pathLine.id = 'pumpLogCurrentPathLine';
          pathLine.textContent = 'Current path: ' + pumpLogCurrentPath;
          list.appendChild(pathLine);
          const existingArchiveBox = document.getElementById('pumpArchiveStatusBox');
          if (existingArchiveBox && existingArchiveBox.textContent.trim()) {
            existingArchiveBox.style.display = 'block';
            list.appendChild(existingArchiveBox);
          }

          if (!Array.isArray(items) || items.length === 0) {
            const empty = document.createElement('div');
            empty.className = 'emptyNote';
            empty.textContent = 'No pump log files found.';
            list.appendChild(empty);
            setTimeout(postHeightToParent, 50);
            return;
          }

          items.forEach(item => {
            const row = document.createElement('div');
            row.className = 'browserRow ' + (item.isDir ? 'dir' : 'file');
            const fullPath = pumpLogJoin(pumpLogCurrentPath, item.name, item.isDir);

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
                pumpLogCurrentPath = fullPath;
                fetchPumpLogFileList();
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
          list.innerHTML = '<div class="emptyNote">Failed to list pump logs.</div>';
          console.log(e);
        }
        setTimeout(postHeightToParent, 50);
      }

      document.getElementById('downloadFilesButton').addEventListener('click', function() {
        const selected = document.querySelectorAll('#filesContainer input[type="checkbox"]:checked');
        selected.forEach(checkbox => {
          if (checkbox.dataset.isdir === '1') {
            prepareArchiveDownload(checkbox.value);
          } else {
            window.open('/fs/download?path=' + encodeURIComponent(checkbox.value), '_blank');
          }
        });
      });

      document.getElementById('downloadAllFilesButton').addEventListener('click', function() {
        prepareArchiveDownload('/Pump_Logs');
      });
      document
        .getElementById('pumpRuntimesLink')
        .href = window.location.origin + '/second-page';
    });
  </script>
</body>
</html>
)rawliteral";


// --- 2) Generate pump rows HTML once ---
static String pumpRowsHtml = []() {
  String rows;
  for (int i = 0; i < numPumps; ++i) {
    rows += "<tr>";
    rows += "<td class=\"pump-name\">" + String(pumpNames[i]) + "</td>";
    rows += "<td class=\"runtime-value\" id=\"pump" + String(i + 1) + "-day\">--</td>";
    rows += "<td class=\"runtime-value\" id=\"pump" + String(i + 1) + "-prevDay\">--</td>";
    rows += "<td class=\"runtime-value\" id=\"pump" + String(i + 1) + "-month\">--</td>";
    rows += "<td class=\"runtime-value\" id=\"pump" + String(i + 1) + "-prevMonth\">--</td>";
    rows += "<td class=\"runtime-value\" id=\"pump" + String(i + 1) + "-year\">--</td>";
    rows += "<td class=\"runtime-value\" id=\"pump" + String(i + 1) + "-prevYear\">--</td>";
    rows += "<td class=\"runtime-value\" id=\"pump" + String(i + 1) + "-total\">--</td>";
    rows += "</tr>";
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
  
  // Calculate Yesterday
  DateTime y = now - TimeSpan(1, 0, 0, 0);
  pd = String(y.year()) + "-" + (y.month() < 10 ? "0" : "") + String(y.month()) + "-" + (y.day() < 10 ? "0" : "") + String(y.day());
  
  // Calculate Previous Month correctly
  int prevMonth = now.month() - 1;
  int prevMonthYear = now.year();
  if (prevMonth == 0) {
    prevMonth = 12;
    prevMonthYear--;
  }
  pm = String(prevMonthYear) + "-" + (prevMonth < 10 ? "0" : "") + String(prevMonth);
  
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
  for (int i = 0; i < numPumps; i++) {
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

  for (int i = 1; i <= numPumps; i++) {
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

      // We use AsyncWebServer's native template processor to avoid massive String reallocation
      // The variables are captured by value so they safely persist during the async stream
      auto processor = [cd, cm, cy, pd, pm, py](const String& var) -> String {
        if (var == "PUMP_ROWS") return pumpRowsHtml;
        if (var == "CURRENT_DAY") return cd;
        if (var == "PREVIOUS_DAY") return pd;
        if (var == "CURRENT_MONTH") return cm;
        if (var == "PREVIOUS_MONTH") return pm;
        if (var == "CURRENT_YEAR") return cy;
        if (var == "PREVIOUS_YEAR") return py;
        
        return String();
      };

      // Stream directly from PROGMEM, replacing tags on the fly
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html; charset=UTF-8", secondPageTemplate, processor);
      response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
      response->addHeader("Pragma", "no-cache");
      request->send(response);
    });
  }


