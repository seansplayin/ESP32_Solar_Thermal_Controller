#include "AlarmWebpage.h"
#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include "AlarmHistory.h"
#include <ArduinoJson.h>
#include "DiagLog.h"


// Provided by WebServerManager.cpp
extern AsyncWebServer server;

static const char alarmLogHtml[] PROGMEM = R"rawliteral(
<!doctype html><html><head>
<meta charset="utf-8">
<title>Alarm Log</title>
<style>
body{font-family:Arial; padding:12px;}
table{border-collapse:collapse; width:100%;}
td,th{border:1px solid #ccc; padding:6px; font-size:13px; vertical-align:top;}
th{background:#f3f3f3;}
.badge{font-weight:bold;}
.alarm{color:red;}
.warn{color:#b36b00;}
.small{font-size:12px; color:#555;}
button{padding:6px 10px; margin-right:8px;}
.expand{cursor:pointer; user-select:none; font-weight:bold;}
.hidden{display:none;}
.rowChild td{background:#fafafa;}
</style>
</head><body>

<h2>Alarm Log</h2>

<div id="summary" class="small">Loading...</div>
<div style="margin:10px 0;">
  <label><input type="checkbox" id="selectAll"> Select All</label>
  <button id="deleteBtn">Delete Selected</button>
  <button id="refreshBtn">Refresh</button>
</div>

<table>
<thead>
<tr>
  <th style="width:40px;"></th>
  <th style="width:170px;">Time</th>
  <th style="width:70px;">Sev</th>
  <th style="width:70px;">Code</th>
  <th style="width:70px;">Act</th>
  <th>Detail</th>
  <th style="width:60px;">Dupes</th>
  <th style="width:40px;">▼</th>
</tr>
</thead>
<tbody id="rows"></tbody>
</table>

<script>
function fmtTs(ts){
  if(!ts) return "N/A";
  try { return new Date(ts*1000).toLocaleString(); } catch(e){ return String(ts); }
}
function sevName(s){ return s==2?"ALARM":(s==1?"WARN":"INFO"); }
function actName(a){ return a==0?"SET":(a==1?"CLEAR":"EVENT"); }


function collectCheckedIds(){
  const ids = [];
  document.querySelectorAll('input[data-id]:checked').forEach(cb=>{
    ids.push(parseInt(cb.getAttribute('data-id')));
  });
  return ids;
}

function setAllCheckboxes(on){
  document.querySelectorAll('input[data-id]').forEach(cb=>cb.checked=on);
  document.querySelectorAll('input[data-group]').forEach(cb=>cb.checked=on);
}

const LS_KEY = 'alarmExpandedGroups';

function getExpandedGroups(){
  const str = localStorage.getItem(LS_KEY);
  return str ? JSON.parse(str) : [];
}

function setExpandedGroups(groups){
  localStorage.setItem(LS_KEY, JSON.stringify(groups));
}

async function load(){
  const r = await fetch('/api/alarm-log', {cache:'no-store'});
  const j = await r.json();

  document.getElementById('summary').textContent =
    `Active: ${j.activeCount} | Groups: ${j.groups.length}`;

  const tb = document.getElementById('rows');
  tb.innerHTML = '';

  const expanded = getExpandedGroups();

  j.groups.forEach((g, idx)=>{
    const tr = document.createElement('tr');
    const sev = sevName(g.sev);
    const act = actName(g.act);
    const dupCount = g.dupes.length;

    const groupCb = document.createElement('input');
    groupCb.type='checkbox';
    groupCb.setAttribute('data-group', idx);

    // When group checkbox is toggled, toggle all child checkboxes
    groupCb.addEventListener('change', ()=>{
      const children = tb.querySelectorAll(`input[data-group-child="${idx}"]`);
      children.forEach(c=>c.checked = groupCb.checked);
      // also check newest id
      const newest = tb.querySelector(`input[data-id="${g.latest.id}"]`);
      if(newest) newest.checked = groupCb.checked;
    });

    const newestCb = document.createElement('input');
    newestCb.type='checkbox';
    newestCb.setAttribute('data-id', g.latest.id);

    // expand toggle
    const exp = document.createElement('span');
    exp.className='expand';
    exp.textContent = dupCount ? '▼' : '';
    exp.addEventListener('click', ()=>{
      const rows = tb.querySelectorAll(`tr[data-child-of="${idx}"]`);
      const isExpanded = exp.textContent === '▲';
      rows.forEach(rr=>rr.classList.toggle('hidden', isExpanded));
      exp.textContent = isExpanded ? '▼' : '▲';

      // Update localStorage
      let expGroups = getExpandedGroups();
      if (isExpanded) {
        expGroups = expGroups.filter(i=>i!==idx);
      } else {
        if (!expGroups.includes(idx)) expGroups.push(idx);
      }
      setExpandedGroups(expGroups);
    });

      tr.innerHTML =
      `<td></td>
      <td>${fmtTs(g.latest.ts)}</td>
      <td>${sev}</td>
      <td>${g.code}</td>
      <td>${act}</td>
      <td>${(g.latest && g.latest.detail) ? g.latest.detail : ''}</td>
      <td>${dupCount}</td>
      <td></td>`;


    // left column: group checkbox + newest checkbox stacked
    const cell0 = tr.children[0];
    cell0.appendChild(groupCb);
    cell0.appendChild(document.createElement('br'));
    cell0.appendChild(newestCb);

    // color row
    if(g.sev==2) tr.classList.add('alarm');
    else if(g.sev==1) tr.classList.add('warn');

    // expand icon cell
    tr.children[7].appendChild(exp);

    tb.appendChild(tr);

    // child rows (duplicates)
    g.dupes.forEach(d=>{
      const tr2 = document.createElement('tr');
      tr2.className = 'rowChild hidden';
      tr2.setAttribute('data-child-of', idx);

      const cb = document.createElement('input');
      cb.type='checkbox';
      cb.setAttribute('data-id', d.id);
      cb.setAttribute('data-group-child', idx);

      tr2.innerHTML =
      `<td></td>
      <td>${fmtTs(d.ts)}</td>
      <td>${sev}</td>
      <td>${g.code}</td>
      <td>${act}</td>
      <td>${d.detail || ''}</td>
      <td></td>
      <td></td>`;

      tr2.children[0].appendChild(cb);
      tb.appendChild(tr2);
    });

    // Auto-expand if in localStorage
    if (expanded.includes(idx)) {
      exp.click();  // Simulate click to expand
    }
  });
}

document.getElementById('refreshBtn').onclick = ()=>load().catch(console.log);

document.getElementById('selectAll').addEventListener('change', (e)=>{
  setAllCheckboxes(e.target.checked);
});

document.getElementById('deleteBtn').onclick = async ()=>{
  const ids = collectCheckedIds();
  const all = document.getElementById('selectAll').checked && ids.length>0;

  if(ids.length===0) return;

  const body = JSON.stringify({ ids, all:false });
  const r = await fetch('/api/alarm-log/delete', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body
  });

  await r.text(); // ignore response body
  document.getElementById('selectAll').checked = false;
  await load();
};

load().catch(console.log);
setInterval(()=>load().catch(()=>{}), 5000);
</script>
</body></html>
)rawliteral";

static void handleDeleteBody(AsyncWebServerRequest* request,
                            uint8_t* data, size_t len, size_t index, size_t total)
{
  // accumulate body in request->_tempObject
  if (index == 0) {
    request->_tempObject = new String();
    ((String*)request->_tempObject)->reserve(total);
  }

  String* body = (String*)request->_tempObject;
  body->concat((const char*)data, len);

  if (index + len != total) return;

  // final chunk: parse JSON
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }

  bool all = doc["all"] | false;
  if (all) {
    bool ok = AlarmHistory_clearAll();
    request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return;
  }

  JsonArray idsArr = doc["ids"].as<JsonArray>();
  if (idsArr.isNull() || idsArr.size() == 0) {
    request->send(400, "application/json", "{\"error\":\"no_ids\"}");
    return;
  }

  const size_t n = idsArr.size();
  uint32_t* ids = (uint32_t*)malloc(n * sizeof(uint32_t));
  if (!ids) {
    request->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }

  for (size_t i=0;i<n;i++) ids[i] = (uint32_t)(idsArr[i] | 0);

  bool ok = AlarmHistory_deleteIds(ids, n);
  free(ids);

  request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void setupAlarmRoutes() {
  server.on("/alarm-log", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html; charset=UTF-8", alarmLogHtml);
  });

  server.on("/api/alarm-log", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncResponseStream* resp = req->beginResponseStream("application/json; charset=UTF-8");
    resp->addHeader("Cache-Control", "no-store");
    AlarmHistory_writeJson(*resp);
    req->send(resp);
  });

  server.on("/api/alarm-log/delete", HTTP_POST,
    [](AsyncWebServerRequest* req){ /* handled in body */ },
    NULL,
    handleDeleteBody
  );
}