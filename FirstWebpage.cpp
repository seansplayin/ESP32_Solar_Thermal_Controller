// FirstWebpage.cpp
#include "WebServerManager.h"
#include "FirstWebpage.h"
#include "Config.h"
#include "FileSystemManager.h"
#include "DiagLog.h"


#define VERSION_INFO " - ESP32_Solar_Thermal_Controller_20260516110516  - "

extern AsyncWebServer server;

const char firstPageHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <link rel="icon" type="image/png" sizes="48x48" href="/static/favicon.png">
  <title>Solar Control System</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <style type="text/css">
    /* --- Layout stability --- */
    table {
      /* Width dynamically injected via JS to protect the C++ parser */
      table-layout: fixed;
      border-collapse: separate;
      border-spacing: 5px; /* mimic cellspacing */
    }
    td { vertical-align: top; min-width: 320px; }

    h1{
      color:purple;
      font-size:40px;
      line-height:1.05;
      font-weight:bold;
      margin: 0;
      padding: 0;
      }
    h2{
      color:#459;
      font-size:12px;
      line-height:1.0;
      font-weight:bold;
      text-align:center;
      margin: 0;
      padding: 0;
    }
    h3{
      color:#459;
      font-size:30px;
      line-height:1.0;
      font-weight:bold;
      text-align:center;
      margin: 0;
      padding: 0;
    }
    h4{
      color:purple;
      font-size:11px;
      line-height:1.0;
      font-weight:bold;
      text-align:center;
      margin: 0;
      padding: 0;
    }
    h5{
      color:purple;
      font-size:11px;
      line-height:1.0;
      font-weight:bold;
      text-align:center;
      margin: 0;
      padding: 0;
    }
    h6{
      color:purple;
      font-size:12px;
      line-height:1.05;
      font-weight:bold;
      text-align:center;
      margin: 0;
      padding: 0;
    }
    h7{
      color:#459;
      font-size:11px;
      line-height:0.5;
      font-weight:bold;
      text-align:left;
      margin: 0;
      padding: 0;
    }
    h8{
      color:purple;
      font-size:14px;
      line-height:1.0;
      font-weight:bold;
      text-align:center;
      margin: 0;
      padding: 0;
    }
    h9{
      color:#000000;
      font-size:14px;
      line-height:1.0;
      text-align:left;
      margin: 0;
      padding: 0;
    }
    h10{
      color:#000000;
      font-size:14px;
      line-height:1.0;
      text-align:right;
      margin: 0;
      padding: 0;
    }

    body{
      font-family:'Lucida Sans Unicode', 'Lucida Grande', sans-serif, Helvetica;
      font-size:15px;
      line-height:1.0;
      text-align:left;
      box-sizing: border-box;
    }
    *, *:before, *:after { box-sizing: inherit; }

    .pump {
      display: flex;
      align-items: center;
      margin-bottom: 10px;
      color: purple;
      flex-wrap: nowrap;
    }
    .pump-title {
      flex: 1 1 auto;
      text-align: left;
      margin-left: 10px;
      color: purple;
      white-space: nowrap;
    }
    .pump-mode {
      align-items: right;
      justify-content: flex-start;
      padding-right: 20px;
      flex-wrap: nowrap;
    }
    .pump-mode label { margin-right: 1px; }
    .pump-state {
      flex: 0 0 auto;
      text-align: right;
      margin-right: 10px;
      color: purple;
    }
    .pump select {
      margin-left: 5px;
      background-color: white;
      color: blue;
    }
    .pump select:hover { background-color: darkblue; }
    .pump select:focus {
      outline: 2px solid rgba(0,0,255,0.6);
      outline-offset: 2px;
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
    .blue-button:hover { background-color: darkblue; color:white; }
    .blue-button:focus {
      outline: 2px solid rgba(0,0,255,0.6);
      outline-offset: 2px;
    }

    #alarmLogBtn {
      position: absolute;
      right: 4px;
      bottom: 4px;
      margin: 0;
    }

    /* --- System Configuration cell: anchor config buttons bottom-left --- */
    #configCell { position: relative; }
    #configCell .configContent { padding-bottom: 24px; }
    #configButtons {
      position: absolute;
      left: 4px;
      bottom: 4px;
    }
    #configButtons .blue-button { margin-right: 4px; }

    #timeInfoView .timeRows {
      display: flex;
      flex-direction: column;
      gap: 8px;
      font-size: 11px;
      font-weight: bold;
      color: purple;
      text-align: center;
    }
    #timeInfoView .dualRow {
      display: flex;
      justify-content: space-between;
      gap: 10px;
    }
    #timeInfoView .dualRow > span:first-child { text-align: left;  flex: 1 1 0; }
    #timeInfoView .dualRow > span:last-child  { text-align: right; flex: 1 1 0; }

    #timeCell { position: relative; }
    #timeCell .timeContent { padding-bottom: 26px; }
    #timeConfigFooter {
      position: absolute;
      left: 4px;
      right: 4px; /* Stretches the container to the right edge */
      bottom: 4px;
      display: flex;
      justify-content: space-between; /* Pushes the button Left, and text Right */
      align-items: center;
    }
    #editTimeConfigBtn {
      margin: 0;
    }

    #statusCell {
      position: relative;
      padding: 4px 4px 26px 4px;
    }
    #statusCell .statusRows {
      display: flex;
      flex-direction: column;
      gap: 8px;
      align-items: flex-end;
      font-size: 11px;
      font-weight: bold;
      color: purple;
      text-align: right;
    }

    .pump-state .on {
      color: blue !important;
      font-weight: bold !important;
      background-color: #e6f3ff !important;
      border-radius: 3px;
    }
    .pump-state .off {
      color: black !important;
      font-weight: normal !important;
      background: none !important;
    }

    #heatingCalls p { color: purple; font-size: 14px; }
    #heatingCalls span { font-weight: normal; }

    #TemperatureValues {
      font-size: 14px;
      color: #000000;
    }

    #SectionHeader {
      font-size: 14px;
      color: #000000;
      line-height: 0.5;
      margin: 0;
      padding-top: 2px;
      justify-content: center;
    }

    .alarm-active { color:red !important; font-weight:bold; }

    /* blink WITHOUT percent keyframes (avoids template parser weirdness) */
    .alarm-blink { animation: blinker 1s steps(2, start) infinite; }
    @keyframes blinker { to { visibility: hidden; } }

    .sensor-list { display: none; margin-top: 10px; }
    .sensor-item { display: block; margin-bottom: 5px; }
    .sensor-item input[type="checkbox"] { margin-right: 5px; }

    .temp-item {
      font-size: 14px;
      color: #000000;
      line-height: 1.0;
    }

    #temperatureGridCell {
      position: relative;
    }

    #temperatureGridCell .configContent {
      padding-bottom: 0;
    }

    /* Unify all Section Headers so they perfectly align across columns */
    .configContent h3 {
      margin: 1px 0 3px 0 !important;
      padding: 0 !important;
      line-height: 1.30 !important;
    }

    .temperatureGridWrap {
      overflow-x: auto;
      padding-bottom: 0;
    }

    #temperatureGrid {
      table-layout: fixed;
      border-collapse: separate;
      /* Left/Right gap is the first number (8px), Top/Bottom gap is the second number (8px) */
      border-spacing: 8px 8px;
      font-size: 14px;
      line-height: 1.25;
      margin-top: 2px;
    }

    #temperatureGrid th,
    #temperatureGrid td {
      min-width: 0;
      border: 1px solid #b8c7d9;
      /* for padding Top/Bottom gap is the first number (1px), Left/Right gap is the second number (2px) */
      padding: 1px 2px;
      vertical-align: middle;
      text-align: right;
    }

    #temperatureGrid th {
      color: #459;
      font-weight: bold;
      text-align: center;
      background: #eef4fb;
      white-space: normal;
    }

    #temperatureGrid .temp-name {
      text-align: left;
      color: purple;
      font-weight: bold;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    #temperatureGrid .temp-sensor-name {
      text-align: center;
      color: purple;
      font-weight: bold;
      white-space: nowrap;
    }

    #temperatureGrid .temp-value {
      white-space: nowrap;
      color: blue;
    }

    #temperatureGrid .temp-age {
      white-space: nowrap;
      color: #000000;
    }

    #collectorFreezeSensors, #lineFreezeSensors{
  display:inline;
  white-space: normal;
  overflow-wrap: anywhere;
  line-height: 1.2;
}


   /* ==========================================================
   ✅ IFRAME CONTAINERS (stable + never escape)
   ========================================================== */
.scaledFrame{
  position: relative;   /* CRITICAL: makes absolute iframe stay in this box */
  overflow: hidden;
  background: white;
}

/* iframe is positioned by JS (left/top), scaled by JS */
.scaledFrame iframe{
  position: absolute;
  top: 0;
  left: 0;
  border: 0;
  display: block;
  transform-origin: 0 0;  /* scale from top-left */
  background: white;
}

/* ✅ Iframe Heights for Pump Runtimes and Temperature Logs 
#pumpRuntimesContainer { height: 560px; min-height: 560px; }
#tempLogsContainer     { height: 560px; min-height: 560px; }*/
/* JS will set the height dynamically */
#pumpRuntimesContainer { height: auto; min-height: 200px; }
#tempLogsContainer     { height: auto; min-height: 200px; }


/* ✅ Call status (left) + Set All Pumps (right) in 2 compact rows */
#callAndGlobal {
  display: grid;
  grid-template-columns: 1fr auto;   /* left text grows, right controls stay tight */
  column-gap: 12px;
  row-gap: 6px;
  align-items: center;
}

#callAndGlobal .callRow {
  color: purple;
  font-size: 14px;
  line-height: 1.0;
  white-space: nowrap;
}

#callAndGlobal .globalLabel {
  color: purple;
  font-size: 14px;
  font-weight: bold;
  justify-self: end;
  white-space: nowrap;
}

#callAndGlobal .globalButtons {
  justify-self: end;
  white-space: nowrap;
}

#callAndGlobal .globalButtons .blue-button {
  margin-left: 6px;
}

#pumps {
    margin-top: 8px;
  }

  </style>

  <script>
    // ----------------------------------------------------------------------
    // THE BULLETPROOF % INJECTOR
    // ----------------------------------------------------------------------
    // The ESPAsyncWebServer template engine crashes or truncates HTML if it 
    // processes literal percent signs incorrectly. 
    // To protect the firmware, we inject all percentage-based CSS dynamically 
    // using Javascript's fromCharCode(37). The ESP32 never sees a percent sign!
    document.addEventListener('DOMContentLoaded', function() {
      const p = String.fromCharCode(37);
      const style = document.createElement('style');
      style.innerHTML = `
        table { width: 100${p}; }
        .main-col-left   { width: 40${p}; }
        .main-col-center { width: 20${p}; min-width: 200px !important; }
        .main-col-right  { width: 40${p}; }
        .temperatureGridWrap { width: 100${p}; }
        #temperatureGrid { width: 100${p}; }
        .scaledFrame { width: 100${p}; }
        #tempLogsIframe { width: 108.7${p} !important; }
        
        #temperatureGrid col:nth-child(1) { width: 22${p}; }
        #temperatureGrid col:nth-child(2) { width: 12${p}; }
        #temperatureGrid col:nth-child(3) { width: 14${p}; } /* Enlarged Sensor Name */
        #temperatureGrid col:nth-child(4) { width: 12${p}; }
        #temperatureGrid col:nth-child(5) { width: 12${p}; }
        #temperatureGrid col:nth-child(6) { width: 14${p}; }
        #temperatureGrid col:nth-child(7) { width: 14${p}; }
      `;
      document.head.appendChild(style);});
  </script>
</head>

<body>
  <table border="10" cellpadding="4" cellspacing="5" bgcolor="white">
    <tr>
      <td valign="top" align="left" bgcolor="white" id="timeCell" class="main-col-left">

        <!-- View mode for time/date/uptime + timezone/DST -->
        <div id="timeInfoView" class="timeContent">
          <div class="timeRows">
            <div class="dualRow">
              <span>Current time: <span id="currentTime" style="color:blue">--:--</span></span>
              <span>Date: <span id="currentDate" style="color:blue">--</span></span>
            </div>

            <div>Uptime: <span id="uptime" style="color:blue">--</span></div>
            <div>Time Zone: <span id="timeZoneDisplay" style="color:blue">--</span></div>
            <div>Daylight Saving: <span id="dstEnabledDisplay" style="color:blue">--</span></div>
          </div>

          <div id="timeConfigFooter">
            <button id="editTimeConfigBtn" class="blue-button">Edit Time Config</button>
            <div style="font-size: 11px; font-weight: bold; color: purple; text-align: right; padding-right: 4px;">
              Last Webpage Update: <span id="lastWebpageUpdate" style="color:blue">--</span>
            </div>
          </div>
        </div>
            

        <!-- Edit mode panel for time configuration -->
        <div id="timeConfigEditor" style="display:none;">
          <div class="timeRows">
            <div><strong>Time Configuration</strong></div>

            <div>
              Time Zone:
              <select id="timeZoneSelect">
                <option value="UTC">UTC</option>
                <option value="US_PACIFIC">US Pacific (PST/PDT)</option>
                <option value="US_MOUNTAIN">US Mountain (MST/MDT)</option>
                <option value="US_CENTRAL">US Central (CST/CDT)</option>
                <option value="US_EASTERN">US Eastern (EST/EDT)</option>
              </select>
            </div>

            <div>
              Daylight Saving:
              <select id="dstEnabledSelect">
                <option value="1">Yes</option>
                <option value="0">No</option>
              </select>
            </div>

            <div>
              <button id="saveTimeConfigBtn"   class="blue-button">Save</button>
              <button id="cancelTimeConfigBtn" class="blue-button">Cancel</button>
              <button id="resetTimeConfigBtn"  class="blue-button">Restore Defaults</button>
            </div>
          </div>
        </div>

      </td>

      <td valign="top" align="center" bgcolor="white" class="main-col-center">
        <div><h1>Solar Thermal</h1></div>
        <div><h1>System Controller</h1></div>
        <div><h6>Thermal Collection & Distribution with logging</h6></div>
      </td>

      <td valign="top" align="center" bgcolor="white" id="statusCell" class="main-col-right">
        <div class="statusRows">
          <div>Alarm state = <span id="alarmState" style="color:blue;">OK</span></div>
          <div>Version = <span id="sysVersion" style="color:blue">Loading...</span></div>
          <div>Heap (Internal RAM): <span id="heapUsage" style="color:blue">--</span></div>
          <div>PSRAM: <span id="psramUsage" style="color:blue">--</span></div>
          <div>File System (Flash Storeage): <span id="fsUsage" style="color:blue">--</span></div>

        </div>

        <button id="alarmLogBtn" class="blue-button">Alarm Log</button>
      </td>
    </tr>




    <tr>
      <td valign="top" id="temperatureGridCell">
        <div id="SectionHeader" class="configContent">
          <h3>Temperatures</h3>

          <div class="temperatureGridWrap">
            <table id="temperatureGrid">
              <colgroup>
                <col><col><col><col><col><col><col>
              </colgroup>
              <thead>
                <tr>
                  <th>System Temperature Name</th>
                  <th>System Temp Value</th>
                  <th>Associated Sensor Name</th>
                  <th>Sensor Average</th>
                  <th>Sensor Raw</th>
                  <th>Last Value Change</th>
                  <th>Last Good Sensor Read</th>
                </tr>
              </thead>
              <tbody>
                <tr><td class="temp-name">Outside Ambient</td><td class="temp-value"><span id="outsideT">--</span></td><td class="temp-sensor-name">DTemp3</td><td class="temp-value"><span id="DTempAverage3">--</span></td><td class="temp-value"><span id="DTemp3">--</span></td><td class="temp-age" id="outsideLastUpdate">--</td><td class="temp-age" id="outsideLastGoodRead">--</td></tr>
                <tr><td class="temp-name">600 Gal Storage</td><td class="temp-value"><span id="storageT">--</span></td><td class="temp-sensor-name">DTemp2</td><td class="temp-value"><span id="DTempAverage2">--</span></td><td class="temp-value"><span id="DTemp2">--</span></td><td class="temp-age" id="storageLastUpdate">--</td><td class="temp-age" id="storageLastGoodRead">--</td></tr>
                <tr><td class="temp-name">Collector Manifold</td><td class="temp-value"><span id="panelT">--</span></td><td class="temp-sensor-name">PT1000</td><td class="temp-value"><span id="pt1000Average">--</span></td><td class="temp-value"><span id="pt1000Current">--</span></td><td class="temp-age" id="panelLastUpdate">--</td><td class="temp-age" id="panelLastGoodRead">--</td></tr>
                <tr><td class="temp-name">Collector Supply</td><td class="temp-value"><span id="CSupplyT">--</span></td><td class="temp-sensor-name">DTemp1</td><td class="temp-value"><span id="DTempAverage1">--</span></td><td class="temp-value"><span id="DTemp1">--</span></td><td class="temp-age" id="collectorSupplyLastUpdate">--</td><td class="temp-age" id="collectorSupplyLastGoodRead">--</td></tr>
                <tr><td class="temp-name">Collector Return</td><td class="temp-value"><span id="CreturnT">--</span></td><td class="temp-sensor-name">DTemp6</td><td class="temp-value"><span id="DTempAverage6">--</span></td><td class="temp-value"><span id="DTemp6">--</span></td><td class="temp-age" id="collectorReturnLastUpdate">--</td><td class="temp-age" id="collectorReturnLastGoodRead">--</td></tr>
                <tr><td class="temp-name">Circ Loop Supply</td><td class="temp-value"><span id="supplyT">--</span></td><td class="temp-sensor-name">DTemp5</td><td class="temp-value"><span id="DTempAverage5">--</span></td><td class="temp-value"><span id="DTemp5">--</span></td><td class="temp-age" id="circSupplyLastUpdate">--</td><td class="temp-age" id="circSupplyLastGoodRead">--</td></tr>
                <tr><td class="temp-name">Circ Loop Return</td><td class="temp-value"><span id="CircReturnT">--</span></td><td class="temp-sensor-name">DTemp4</td><td class="temp-value"><span id="DTempAverage4">--</span></td><td class="temp-value"><span id="DTemp4">--</span></td><td class="temp-age" id="circReturnLastUpdate">--</td><td class="temp-age" id="circReturnLastGoodRead">--</td></tr>
                <tr><td class="temp-name">DHW Glycol Supply</td><td class="temp-value"><span id="DhwSupplyT">--</span></td><td class="temp-sensor-name">DTemp7</td><td class="temp-value"><span id="DTempAverage7">--</span></td><td class="temp-value"><span id="DTemp7">--</span></td><td class="temp-age" id="dhwSupplyLastUpdate">--</td><td class="temp-age" id="dhwSupplyLastGoodRead">--</td></tr>
                <tr><td class="temp-name">DHW Glycol Return</td><td class="temp-value"><span id="DhwReturnT">--</span></td><td class="temp-sensor-name">DTemp8</td><td class="temp-value"><span id="DTempAverage8">--</span></td><td class="temp-value"><span id="DTemp8">--</span></td><td class="temp-age" id="dhwReturnLastUpdate">--</td><td class="temp-age" id="dhwReturnLastGoodRead">--</td></tr>
                <tr><td class="temp-name">Furnace Glycol Supply</td><td class="temp-value"><span id="HeatingSupplyT">--</span></td><td class="temp-sensor-name">DTemp9</td><td class="temp-value"><span id="DTempAverage9">--</span></td><td class="temp-value"><span id="DTemp9">--</span></td><td class="temp-age" id="heatingSupplyLastUpdate">--</td><td class="temp-age" id="heatingSupplyLastGoodRead">--</td></tr>
                <tr><td class="temp-name">Furnace Glycol Return</td><td class="temp-value"><span id="HeatingReturnT">--</span></td><td class="temp-sensor-name">DTemp10</td><td class="temp-value"><span id="DTempAverage10">--</span></td><td class="temp-value"><span id="DTemp10">--</span></td><td class="temp-age" id="heatingReturnLastUpdate">--</td><td class="temp-age" id="heatingReturnLastGoodRead">--</td></tr>
                <tr><td class="temp-name">DHW Pot Exchange In</td><td class="temp-value"><span id="PotHeatXinletT">--</span></td><td class="temp-sensor-name">DTemp12</td><td class="temp-value"><span id="DTempAverage12">--</span></td><td class="temp-value"><span id="DTemp12">--</span></td><td class="temp-age" id="potHxInLastUpdate">--</td><td class="temp-age" id="potHxInLastGoodRead">--</td></tr>
                <tr><td class="temp-name">DHW Pot Exchange Out</td><td class="temp-value"><span id="PotHeatXoutletT">--</span></td><td class="temp-sensor-name">DTemp13</td><td class="temp-value"><span id="DTempAverage13">--</span></td><td class="temp-value"><span id="DTemp13">--</span></td><td class="temp-age" id="potHxOutLastUpdate">--</td><td class="temp-age" id="potHxOutLastGoodRead">--</td></tr>
                <tr><td class="temp-name">DHW Pot Inline Heater Out</td><td class="temp-value"><span id="dhwT">--</span></td><td class="temp-sensor-name">DTemp11</td><td class="temp-value"><span id="DTempAverage11">--</span></td><td class="temp-value"><span id="DTemp11">--</span></td><td class="temp-age" id="dhwInlineLastUpdate">--</td><td class="temp-age" id="dhwInlineLastGoodRead">--</td></tr>
              </tbody>
            </table>
          </div>
        </div>
       </td>
        
        
        
       

      




      <td valign="top" id="configCell">
      <div id="SectionHeader" class="configContent">
        <h3>Relay Status & Control</h3>
        <h2>Pumps, Valves, Heat Tape</h2>

        <div id="heatingCalls" style="margin-top:12px;">

        <div id="callAndGlobal">
        <div class="callRow">Call for DHW Heating: <span id="dhwHeatingCallStatus">--</span></div>
        <div class="globalLabel">Set All Pumps:</div>

        <div class="callRow">Call for Heating: <span id="heatingCallStatus">--</span></div>
        <div class="globalButtons">
        <button id="allAutoButton" class="blue-button">AUTO</button>
        <button id="allOffButton"  class="blue-button">OFF</button>
        </div>
      </div>
      </div>

         <div id="pumps">
         <!-- Pump controls will be generated by JavaScript -->
         </div>

      </div>

      </td>

      <!-- ✅ Row 2, Col 3: Pump Runtimes -->
      <td valign="top" bgcolor="white" align="center">
        <div class="scaledFrame" id="pumpRuntimesContainer">
          <iframe id="pumpRuntimesIframe" scrolling="no"></iframe>
        </div>
      </td>
    </tr>


    <tr>

     <td valign="top" id="configCell">
            <div id="SectionHeader" class="configContent">
             <h3>Placeholder Section</h3>
             <h2>Future Stuff</h2>
            <div id="emptySectionPlaceholder" style="min-height: 100px; border: 1px dashed #ccc; margin-top: 0px;">
            </div>
        
          </div>
        </td>

        
      <td valign="top" id="configCell">
        <div id="SectionHeader" class="configContent">
          <h3>Auto Pump Configuration</h3>

          <p>
            Min Lead Start Temp(PT1000):
            <span id="panelTminimum">--</span>
            <input type="number" step="0.1" id="panelTminimumInput" style="width:70px; display:none;">
          </p>

          <p>
            Lead On Diff.(PT1000 vs DTemp5):
            <span id="PanelOnDifferential">--</span>
            <input type="number" step="0.1" id="PanelOnDifferentialInput" style="width:70px; display:none;">
          </p>

          <p>
            Lag On Diff.(DTemp6 vs DTemp11):
            <span id="PanelLowDifferential">--</span>
            <input type="number" step="0.1" id="PanelLowDifferentialInput" style="width:70px; display:none;">
          </p>

          <p>
            Lead Off Diff.(PT1000 vs DTemp5):
            <span id="PanelOffDifferential">--</span>
            <input type="number" step="0.1" id="PanelOffDifferentialInput" style="width:70px; display:none;">
          </p>

          <br>

          <p>
            Boiler On Temperature:
            <span id="Boiler_Circ_On">--</span>
            <input type="number" step="0.1" id="Boiler_Circ_OnInput" style="width:70px; display:none;">
          </p>

          <p>
            Boiler Off Temperature:
            <span id="Boiler_Circ_Off">--</span>
            <input type="number" step="0.1" id="Boiler_Circ_OffInput" style="width:70px; display:none;">
          </p>

          <br>

          <p>
            600 Gallon High Temperature Limit:
            <span id="StorageHeatingLimit">--</span>
            <input type="number" step="0.1" id="StorageHeatingLimitInput" style="width:70px; display:none;">
          </p>

          <br>

          <p>
            Circ Loop On Diff.(DTemp5 vs DTemp6):
            <span id="Circ_Pump_On">--</span>
            <input type="number" step="0.1" id="Circ_Pump_OnInput" style="width:70px; display:none;">
          </p>

          <p>
            Circ Loop Off Diff.(DTemp5 vs DTemp6):
            <span id="Circ_Pump_Off">--</span>
            <input type="number" step="0.1" id="Circ_Pump_OffInput" style="width:70px; display:none;">
          </p>

          <br>

          <p>
            Heat Tape On Temperature:
            <span id="Heat_Tape_On">--</span>
            <input type="number" step="0.1" id="Heat_Tape_OnInput" style="width:70px; display:none;">
          </p>

          <p>
            Heat Tape Off Temperature:
            <span id="Heat_Tape_Off">--</span>
            <input type="number" step="0.1" id="Heat_Tape_OffInput" style="width:70px; display:none;">
          </p>

          <br>
          <h2>Solar Collector Freeze Protection</h2>

          <p>
            Freeze Alarm - Temperature Threshold:
            <span id="collectorFreezeTempF">--</span>
            <input type="number" step="0.1" id="collectorFreezeTempFInput" style="width:70px; display:none;">
          </p>

          <p>
            Freeze Alarm - If Below Threshold Initiate Alarm After:
            <span id="collectorFreezeConfirmMin">--</span>
            <input type="number" step="1" min="1" max="120" id="collectorFreezeConfirmMinInput" style="width:70px; display:none;">
          </p>

          <p>
            Freeze Alarm - After Alarm Run Lead/Lag Pumps For:
            <span id="collectorFreezeRunMin">--</span>
            <input type="number" step="1" min="1" max="120" id="collectorFreezeRunMinInput" style="width:70px; display:none;">
          </p>

          <p>
            
            <span id="collectorFreezeSensors">--</span>
            <div id="collectorFreezeSensorsInput" class="sensor-list">
              <div class="sensor-item"><input type="checkbox" value="1"> Panel Manifold Temperature (PT1000)</div>
              <div class="sensor-item"><input type="checkbox" value="2"> Collector Supply Temperature (DTemp1)</div>
              <div class="sensor-item"><input type="checkbox" value="3"> 600 Gal Storage Tank Temperature (DTemp2)</div>
              <div class="sensor-item"><input type="checkbox" value="4"> Outside Ambient Temperature (DTemp3)</div>
              <div class="sensor-item"><input type="checkbox" value="5"> Circ Loop Return Temperature (DTemp4)</div>
              <div class="sensor-item"><input type="checkbox" value="6"> Circ Loop Supply Temperature (DTemp5)</div>
              <div class="sensor-item"><input type="checkbox" value="7"> Collector Return Temperature (DTemp6)</div>
              <div class="sensor-item"><input type="checkbox" value="8"> DHW Glycol Supply Temperature (DTemp7)</div>
              <div class="sensor-item"><input type="checkbox" value="9"> DHW Glycol Return Temperature (DTemp8)</div>
              <div class="sensor-item"><input type="checkbox" value="10"> Furnace Glycol Supply Temperature (DTemp9)</div>
              <div class="sensor-item"><input type="checkbox" value="11"> Furnace Glycol Return Temperature (DTemp10)</div>
              <div class="sensor-item"><input type="checkbox" value="12"> Potable Inline Heater Outlet (DTemp11)</div>
              <div class="sensor-item"><input type="checkbox" value="13"> Potable Heat Exchanger Inlet (DTemp12)</div>
              <div class="sensor-item"><input type="checkbox" value="14"> Potable Heat Exchanger Outlet (DTemp13)</div>
            </div>
          </p>

          <br>
          <h2>Tank & Circ Loop Freeze Protection</h2>

          <p>
            Freeze Alarm - Temp Threshold:
            <span id="lineFreezeTempF">--</span>
            <input type="number" step="0.1" id="lineFreezeTempFInput" style="width:70px; display:none;">
          </p>

          <p>
            Freeze Alarm - If Below Threshold Initiate Alarm After:
            <span id="lineFreezeConfirmMin">--</span>
            <input type="number" step="1" min="1" max="120" id="lineFreezeConfirmMinInput" style="width:70px; display:none;">
          </p>

          <p>
            Freeze Alarm - After Alarm Run Circ Pump For:
            <span id="lineFreezeRunMin">--</span>
            <input type="number" step="1" min="1" max="120" id="lineFreezeRunMinInput" style="width:70px; display:none;">
          </p>

          <p>
            
            <span id="lineFreezeSensors">--</span>
            <div id="lineFreezeSensorsInput" class="sensor-list">
              <div class="sensor-item"><input type="checkbox" value="1"> Panel Manifold Temperature (PT1000)</div>
              <div class="sensor-item"><input type="checkbox" value="2"> Collector Supply Temperature (DTemp1)</div>
              <div class="sensor-item"><input type="checkbox" value="3"> 600 Gal Storage Tank Temperature (DTemp2)</div>
              <div class="sensor-item"><input type="checkbox" value="4"> Outside Ambient Temperature (DTemp3)</div>
              <div class="sensor-item"><input type="checkbox" value="5"> Circ Loop Return Temperature (DTemp5)</div>
              <div class="sensor-item"><input type="checkbox" value="6"> Circ Loop Supply Temperature (DTemp4)</div>
              <div class="sensor-item"><input type="checkbox" value="7"> Collector Return Temperature (DTemp6)</div>
              <div class="sensor-item"><input type="checkbox" value="8"> DHW Glycol Supply Temperature (DTemp7)</div>
              <div class="sensor-item"><input type="checkbox" value="9"> DHW Glycol Return Temperature (DTemp8)</div>
              <div class="sensor-item"><input type="checkbox" value="10"> Furnace Glycol Supply Temperature (DTemp9)</div>
              <div class="sensor-item"><input type="checkbox" value="11"> Furnace Glycol Return Temperature (DTemp10)</div>
              <div class="sensor-item"><input type="checkbox" value="12"> Potable Inline Heater Outlet (DTemp11)</div>
              <div class="sensor-item"><input type="checkbox" value="13"> Potable Heat Exchanger Inlet (DTemp12)</div>
              <div class="sensor-item"><input type="checkbox" value="14"> Potable Heat Exchanger Outlet (DTemp13)</div>
            </div>
          </p>
        </div>

        <div id="configButtons">
          <button id="editConfigBtn"   class="blue-button">Edit Auto Pump Config</button>
          <button id="saveConfigBtn"   class="blue-button" style="display:none;">Save</button>
          <button id="cancelConfigBtn" class="blue-button" style="display:none;">Cancel</button>
          <button id="resetConfigBtn"  class="blue-button" style="display:none;">Restore Defaults</button>
        </div>
      </td>

      <!-- ✅ Row 3, Col 3: Temperature Logs -->
      <td valign="top" bgcolor="white" align="center">
        <div class="scaledFrame" id="tempLogsContainer">
              <iframe id="tempLogsIframe" scrolling="no"
      style="border:0; transform:scale(0.92); transform-origin: top left;"></iframe>

        </div>
      </td>
    </tr>
  </table>




  <script>



  window.addEventListener("message", (event) => {
    if (!event.data || event.origin !== window.location.origin) return;
    
    if (event.data.type === "thirdPageHeight") {
      const iframe = document.getElementById("tempLogsIframe");
      if (iframe) iframe.style.height = (event.data.height + 10) + "px";
    }
    else if (event.data.type === "secondPageHeight") {
      const iframe = document.getElementById("pumpRuntimesIframe");
      if (iframe) iframe.style.height = (event.data.height + 10) + "px";
    }
  });


    document.addEventListener('DOMContentLoaded', function () {
    // WebSocket with auto-reconnect
    var ws = null;
    var wsReconnectTimer = null;
    var wsBackoffMs = 1000;           // starts at 1s
    var wsBackoffMaxMs = 30000;       // max 30s

    // WS liveness / wake handling
    var wsLastRxMs = 0;
    var wsHealthTimer = null;
    var wsResyncTimer = null;

    function scheduleFreshClockAndUptime(delayMs) {
      if (wsResyncTimer) {
        clearTimeout(wsResyncTimer);
        wsResyncTimer = null;
      }

      wsResyncTimer = setTimeout(function () {
        wsResyncTimer = null;
        requestFreshClockAndUptime();
      }, delayMs || 150);
    }

    function startWsHealthWatchdog() {
      if (wsHealthTimer) {
        clearInterval(wsHealthTimer);
        wsHealthTimer = null;
      }

      wsHealthTimer = setInterval(function () {
        if (!ws) return;

        // If the socket is OPEN but we have received nothing for too long,
        // assume the session is stale/zombie and force a reconnect.
        if (ws.readyState === WebSocket.OPEN) {
          var age = Date.now() - wsLastRxMs;

          if (wsLastRxMs !== 0 && age > 95000) {
            console.log('WS watchdog: stale connection detected, forcing reconnect');
            try { ws.close(); } catch (e) {}
          }
        }
      }, 15000);
    }

    function wsConnect() {
      // Clear any pending reconnect
      if (wsReconnectTimer) { clearTimeout(wsReconnectTimer); wsReconnectTimer = null; }

      ws = new WebSocket('ws://' + window.location.hostname + '/ws');

      ws.onopen = function () {
        console.log('WebSocket connected');
        wsBackoffMs = 1000; // reset backoff on success
        wsLastRxMs = Date.now();
        startWsHealthWatchdog();

        // Keep browser->ESP startup minimal:
        // 1) identify the page
        // 2) request one consolidated init sequence
        const initMsgs = [
          [0,   'hello:FirstWebpage'],
          [300, 'initAll']
        ];

        initMsgs.forEach(function(item) {
          setTimeout(function () {
            if (ws && ws.readyState === WebSocket.OPEN) {
              ws.send(item[1]);
            }
          }, item[0]);
        });
      };

      ws.onmessage = function (event) {
        wsLastRxMs = Date.now();
        markWebpageUpdated();
        handleWebSocketMessage(event.data);
      };

      ws.onclose = function () {
        console.log('WebSocket closed - scheduling reconnect');

        if (wsHealthTimer) {
          clearInterval(wsHealthTimer);
          wsHealthTimer = null;
        }

        var delay = wsBackoffMs;
        wsBackoffMs = Math.min(wsBackoffMs * 2, wsBackoffMaxMs);

        wsReconnectTimer = setTimeout(function () {
          wsConnect();
        }, delay);
      };

      ws.onerror = function () {
        // Force close => triggers reconnect path
        try { ws.close(); } catch (e) {}
      };
    }

    // Start WS
    wsConnect();

    // Dynamically set iframes to avoid C++ template parsing lockups
    document.getElementById('pumpRuntimesIframe').src = "/second-page?ts=" + Date.now();
    document.getElementById('tempLogsIframe').src = "/third-page?ts=" + Date.now();
    
    // Fetch version text
    fetch('/api/version')
      .then(response => response.text())
      .then(text => {
        const el = document.getElementById('sysVersion');
        if (el) el.textContent = text;
      }).catch(err => console.log(err));

    let activePumpCount = 10; // Will scale down dynamically based on JSON payload
    let pumpStates = Array(15).fill(null);

    // ---- Time config state (timezone + DST) ----
    let timeConfig = {
      timeZoneId: 'US_MOUNTAIN',
      dstEnabled: 1
    };

    // ✅ ENFORCE 1–10 INDEXING (NO PUMP 0)
    for (let i = 1; i <= 10; i++) {
          pumpStates[i] = { state: '--', mode: 'Auto', name: 'Pump ' + i };
    }

    // ---- Local ticking (sleep/wake resilient) ----
    var uptimeSecondsBase = null;
    var uptimeTickTimer   = null;
    var uptimePerfBaseMs  = null;

    var controllerClockBaseMs = null;   // controller snapshot in epoch ms
    var clockTickTimer        = null;
    var clockPerfBaseMs       = null;

    function requestFreshClockAndUptime() {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('initDateTime');
        ws.send('getUptime');
      } else if (!ws || ws.readyState === WebSocket.CLOSED) {
        wsConnect();
      }
    }



    function parseUptimeToSeconds(s) {
      // "0 days, 12 hours, 52 minutes, 59 seconds"
      var days = 0, hours = 0, minutes = 0, seconds = 0;
      var m;

      m = s.match(/(\d+)\s*days?/i);    if (m) days = parseInt(m[1], 10);
      m = s.match(/(\d+)\s*hours?/i);   if (m) hours = parseInt(m[1], 10);
      m = s.match(/(\d+)\s*minutes?/i); if (m) minutes = parseInt(m[1], 10);
      m = s.match(/(\d+)\s*seconds?/i); if (m) seconds = parseInt(m[1], 10);

      return (((days * 24 + hours) * 60 + minutes) * 60 + seconds);
    }

    function formatUptimeFromSeconds(total) {
      var days = Math.floor(total / 86400);
      total -= (days * 86400);

      var hours = Math.floor(total / 3600);
      total -= (hours * 3600);

      var minutes = Math.floor(total / 60);
      total -= (minutes * 60);

      var seconds = total;

      return days + " days, " + hours + " hours, " + minutes + " minutes, " + seconds + " seconds";
    }

    function startUptimeTickerFromString(uptimeStr) {
      uptimeSecondsBase = parseUptimeToSeconds(uptimeStr);
      uptimePerfBaseMs  = performance.now();

      if (uptimeTickTimer) clearInterval(uptimeTickTimer);
      uptimeTickTimer = setInterval(function () {
        if (uptimeSecondsBase == null || uptimePerfBaseMs == null) return;

        var elapsedSec = Math.floor((performance.now() - uptimePerfBaseMs) / 1000);
        var totalSec = uptimeSecondsBase + elapsedSec;

        var el = document.getElementById('uptime');
        if (el) el.textContent = formatUptimeFromSeconds(totalSec);
      }, 1000);
    }

    function startClockTicker(dateStr, timeStr) {
      // dateStr: "YYYY-MM-DD", timeStr: "HH:MM:SS"
      var iso = dateStr + "T" + timeStr;
      var d = new Date(iso);
      if (isNaN(d.getTime())) return;

      controllerClockBaseMs = d.getTime();
      clockPerfBaseMs = performance.now();

      if (clockTickTimer) clearInterval(clockTickTimer);
      clockTickTimer = setInterval(function () {
        if (controllerClockBaseMs == null || clockPerfBaseMs == null) return;

        var nowMs = controllerClockBaseMs + (performance.now() - clockPerfBaseMs);
        var current = new Date(nowMs);

        var yyyy = current.getFullYear();
        var mm   = String(current.getMonth() + 1).padStart(2, '0');
        var dd   = String(current.getDate()).padStart(2, '0');
        var hh   = String(current.getHours()).padStart(2, '0');
        var mi   = String(current.getMinutes()).padStart(2, '0');
        var ss   = String(current.getSeconds()).padStart(2, '0');

        var dateEl = document.getElementById('currentDate');
        var timeEl = document.getElementById('currentTime');
        if (dateEl) dateEl.textContent = yyyy + "-" + mm + "-" + dd;
        if (timeEl) timeEl.textContent = hh + ":" + mi + ":" + ss;
      }, 1000);
    }

    setInterval(function () {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('ping');
      }
    }, 15000);

   // --- WAKE LOCK API (Prevents Browser Sleep & TCP Choking) ---
    let wakeLock = null;
    const requestWakeLock = async () => {
      try {
        if ('wakeLock' in navigator) {
          wakeLock = await navigator.wakeLock.request('screen');
          console.log('Wake Lock active: Browser background throttling prevented.');
        }
      } catch (err) {
        console.log('Wake Lock error: ', err.message);
      }
    };
    
    // Request immediately on page load
    requestWakeLock();

   document.addEventListener('visibilitychange', function () {
      if (!document.hidden) {
        // Re-request wake lock if tab was minimized and brought back
        requestWakeLock();
        scheduleFreshClockAndUptime(150);
        
        // Visibility API Reconnect Resilience
        if (!ws || ws.readyState === WebSocket.CLOSED || ws.readyState === WebSocket.CLOSING) {
            console.log("Tab focused, reviving dead WebSocket...");
            wsConnect();
        }
      }
    });


    window.addEventListener('focus', function () {
      scheduleFreshClockAndUptime(150);
    });

    window.addEventListener('pageshow', function () {
      scheduleFreshClockAndUptime(150);
    });

    window.addEventListener('online', function () {
      scheduleFreshClockAndUptime(150);
    });

    let currentConfig = {};
    let configEditMode = false;

    let currentCollectorSensors = [];
    let currentLineSensors = [];

    const configUnits = {
      panelTminimum: '°F',
      PanelOnDifferential: '°F',
      PanelLowDifferential: '°F',
      PanelOffDifferential: '°F',
      Boiler_Circ_On: '°F',
      Boiler_Circ_Off: '°F',
      StorageHeatingLimit: '°F',
      Circ_Pump_On: '°F',
      Circ_Pump_Off: '°F',
      Heat_Tape_On: '°F',
      Heat_Tape_Off: '°F',

      collectorFreezeTempF: '°F',
      collectorFreezeConfirmMin: ' min',
      collectorFreezeRunMin: ' min',

      lineFreezeTempF: '°F',
      lineFreezeConfirmMin: ' min',
      lineFreezeRunMin: ' min'
    };

    const configKeys = [
      'panelTminimum',
      'PanelOnDifferential',
      'PanelLowDifferential',
      'PanelOffDifferential',
      'Boiler_Circ_On',
      'Boiler_Circ_Off',
      'StorageHeatingLimit',
      'Circ_Pump_On',
      'Circ_Pump_Off',
      'Heat_Tape_On',
      'Heat_Tape_Off',

      'collectorFreezeTempF',
      'collectorFreezeConfirmMin',
      'collectorFreezeRunMin',

      'lineFreezeTempF',
      'lineFreezeConfirmMin',
      'lineFreezeRunMin'
    ];

    const sensorNames = [
      "",
      "Panel Manifold Temperature (PT1000)",
      "Collector Supply Temperature (DTemp1)",
      "600 Gal Storage Tank Temperature (DTemp2)",
      "Outside Ambient Temperature (DTemp3)",
      "Circ Loop Return Temperature (DTemp4)",
      "Circ Loop Supply Temperature (DTemp5)",
      "Collector Return Temperature (DTemp6)",
      "DHW Glycol Supply Temperature (DTemp7)",
      "DHW Glycol Return Temperature (DTemp8)",
      "Furnace Glycol Supply Temperature (DTemp9)",
      "Furnace Glycol Return Temperature (DTemp10)",
      "Potable Inline Heater Outlet (DTemp11)",
      "Potable Heat Exchanger Inlet (DTemp12)",
      "Potable Heat Exchanger Outlet (DTemp13)"
    ];  

    
       // ✅ Format Freeze Sensor display: one per line, each line prefixed
      function formatFreezeSensorLines(arr) {
       if (!Array.isArray(arr) || arr.length === 0) return '';
       return arr
        .map(v => 'Freeze Alarm - Sensor: ' + (sensorNames[v] || v))
        .join('<br>');
      }


        document.getElementById('allAutoButton').addEventListener('click', function () {
      ws.send('setAllPumps:auto');
    });

    document.getElementById('allOffButton').addEventListener('click', function () {
      ws.send('setAllPumps:off');
    });

    // ==========================================================
    // ✅ MAIN MESSAGE DISPATCHER (With DOM Throttling)
    // ==========================================================
    let liveTempState = {};
    let changedTempKeys = {};
    let isTempDrawPending = false;
    let lastWebpageUpdateMs = null;

    const temperatureRowDefinitions = [
      { lastId: 'outsideLastUpdate',         goodId: 'outsideLastGoodRead',         keys: ['outsideT', 'DTempAverage3',  'DTemp3'],       readKeys: ['DTempGoodAge3'] },
      { lastId: 'storageLastUpdate',         goodId: 'storageLastGoodRead',         keys: ['storageT', 'DTempAverage2',  'DTemp2'],       readKeys: ['DTempGoodAge2'] },
      { lastId: 'panelLastUpdate',           goodId: 'panelLastGoodRead',           keys: ['panelT', 'pt1000Average',    'pt1000Current'], readKeys: ['pt1000GoodAge'] },
      { lastId: 'collectorSupplyLastUpdate', goodId: 'collectorSupplyLastGoodRead', keys: ['CSupplyT', 'DTempAverage1',  'DTemp1'],       readKeys: ['DTempGoodAge1'] },
      { lastId: 'collectorReturnLastUpdate', goodId: 'collectorReturnLastGoodRead', keys: ['CreturnT', 'DTempAverage6',  'DTemp6'],       readKeys: ['DTempGoodAge6'] },
      { lastId: 'circSupplyLastUpdate',      goodId: 'circSupplyLastGoodRead',      keys: ['supplyT', 'DTempAverage5',   'DTemp5'],       readKeys: ['DTempGoodAge5'] },
      { lastId: 'circReturnLastUpdate',      goodId: 'circReturnLastGoodRead',      keys: ['CircReturnT', 'DTempAverage4', 'DTemp4'],      readKeys: ['DTempGoodAge4'] },
      { lastId: 'dhwSupplyLastUpdate',       goodId: 'dhwSupplyLastGoodRead',       keys: ['DhwSupplyT', 'DTempAverage7', 'DTemp7'],      readKeys: ['DTempGoodAge7'] },
      { lastId: 'dhwReturnLastUpdate',       goodId: 'dhwReturnLastGoodRead',       keys: ['DhwReturnT', 'DTempAverage8', 'DTemp8'],      readKeys: ['DTempGoodAge8'] },
      { lastId: 'heatingSupplyLastUpdate',   goodId: 'heatingSupplyLastGoodRead',   keys: ['HeatingSupplyT', 'DTempAverage9',  'DTemp9'],  readKeys: ['DTempGoodAge9'] },
      { lastId: 'heatingReturnLastUpdate',   goodId: 'heatingReturnLastGoodRead',   keys: ['HeatingReturnT', 'DTempAverage10', 'DTemp10'], readKeys: ['DTempGoodAge10'] },
      { lastId: 'potHxInLastUpdate',         goodId: 'potHxInLastGoodRead',         keys: ['PotHeatXinletT', 'DTempAverage12',  'DTemp12'], readKeys: ['DTempGoodAge12'] },
      { lastId: 'potHxOutLastUpdate',        goodId: 'potHxOutLastGoodRead',        keys: ['PotHeatXoutletT', 'DTempAverage13', 'DTemp13'], readKeys: ['DTempGoodAge13'] },
      { lastId: 'dhwInlineLastUpdate',       goodId: 'dhwInlineLastGoodRead',       keys: ['dhwT', 'DTempAverage11', 'DTemp11'],          readKeys: ['DTempGoodAge11'] }
    ];

    const tempKeyToLastUpdateId = {};
    const sensorGoodAgeKeyToId = {};
    const tempRowLastUpdateMs = {};
    const tempRowLastGoodReadMs = {};

    temperatureRowDefinitions.forEach(function (row) {
      row.keys.forEach(function (key) {
        tempKeyToLastUpdateId[key] = row.lastId;
      });
      row.readKeys.forEach(function (key) {
        sensorGoodAgeKeyToId[key] = row.goodId;
      });
    });

    function formatElapsedAge(ms) {
      if (!ms) return '--';

      var seconds = Math.max(0, Math.floor((performance.now() - ms) / 1000));
      return seconds + 's';
    }

    function refreshElapsedUpdateCounters() {
      var webpageEl = document.getElementById('lastWebpageUpdate');
      if (webpageEl) webpageEl.textContent = formatElapsedAge(lastWebpageUpdateMs);

      for (let lastId in tempRowLastUpdateMs) {
        var rowEl = document.getElementById(lastId);
        if (rowEl) rowEl.textContent = formatElapsedAge(tempRowLastUpdateMs[lastId]);
      }

      for (let goodId in tempRowLastGoodReadMs) {
        var goodEl = document.getElementById(goodId);
        if (goodEl) goodEl.textContent = formatElapsedAge(tempRowLastGoodReadMs[goodId]);
      }
    }

    function markWebpageUpdated() {
      lastWebpageUpdateMs = performance.now();
      refreshElapsedUpdateCounters();
    }

    function markTemperatureRowUpdated(key) {
      var lastId = tempKeyToLastUpdateId[key];
      if (lastId) tempRowLastUpdateMs[lastId] = performance.now();
    }

    function markSensorGoodReadAge(key, value) {
      var goodId = sensorGoodAgeKeyToId[key];
      if (!goodId) return;

      if (value === 'N/A') {
        tempRowLastGoodReadMs[goodId] = null;
        var goodEl = document.getElementById(goodId);
        if (goodEl) goodEl.textContent = '--';
        return;
      }

      var ageSeconds = parseInt(value, 10);
      if (isNaN(ageSeconds) || ageSeconds < 0) return;

      tempRowLastGoodReadMs[goodId] = performance.now() - (ageSeconds * 1000);
    }

    setInterval(refreshElapsedUpdateCounters, 1000);

    function paintTemperatures() {
      for (let key in changedTempKeys) {
        var el = document.getElementById(key);
        if (el) {
          var v = liveTempState[key];
          el.textContent = (v === "N/A") ? v : v + '°F';
          markTemperatureRowUpdated(key);
        }
      }

      changedTempKeys = {};
      refreshElapsedUpdateCounters();
      isTempDrawPending = false;
    }

    function handleWebSocketMessage(data) {
      // Disabled console log to prevent browser console spam on high-frequency UI paints
      // console.log('Processing:', data);

      // -------- TEMPERATURES (THROTTLED) --------
      if (data.startsWith('Temperatures:')) {
        var tempData = data.substring('Temperatures:'.length).split(',');
        tempData.forEach(function (item) {
          var kv = item.split(':');
          if (kv.length >= 2) {
            var key = kv[0].trim();
            var value = kv.slice(1).join(':').trim();

            if (sensorGoodAgeKeyToId[key]) {
              markSensorGoodReadAge(key, value);
            } else {
              liveTempState[key] = value;
              changedTempKeys[key] = true;
            }
          }
        });

        // Schedule a screen paint synced to the monitor's refresh rate
        if (!isTempDrawPending) {
          isTempDrawPending = true;
          requestAnimationFrame(paintTemperatures);
        }
      }

      // -------- CONFIG SAVE RESPONSE --------
      else if (data === 'ConfigSave:OK') {
        console.log('Config saved successfully');
      }
      else if (data === 'ConfigSave:FAIL') {
        alert('Failed to save configuration on controller');
      }
      else if (data === 'ConfigReset:OK') {
        console.log('Config reset to defaults');
        setConfigEditMode(false);
      }
      else if (data === 'ConfigReset:FAIL') {
        alert('Failed to reset configuration to defaults on controller');
      }

      // -------- TIME CONFIG SAVE / RESET RESPONSE --------
      else if (data === 'TimeConfigSave:OK') {
        console.log('Time configuration saved successfully');
      }
      else if (data === 'TimeConfigSave:FAIL') {
        alert('Failed to save time configuration on controller');
      }
      else if (data === 'TimeConfigReset:OK') {
        console.log('Time configuration reset to defaults');
        setTimeConfigEditMode(false);
      }
      else if (data === 'TimeConfigReset:FAIL') {
        alert('Failed to reset time configuration to defaults on controller');
      }

      // -------- SYSTEM CONFIGURATION (view + edit) --------
      else if (data.startsWith('Configuration:')) {
        var cfgStr = data.substring('Configuration:'.length);

        // Split on commas, BUT re-join tokens that belong to the previous key
        var raw = cfgStr.split(',');
        var items = [];
        var cur = null;

        raw.forEach(function(tok){
          tok = tok.trim();
          if (!tok) return;

          if (tok.includes(':')) {
            if (cur !== null) items.push(cur);
            cur = tok;
          } else {
            if (cur !== null) cur += ',' + tok;
          }
        });
        if (cur !== null) items.push(cur);

        items.forEach(function (item) {
          var kv = item.split(':');
          if (kv.length < 2) return;

          var key = kv[0].trim();
          var valStr = kv.slice(1).join(':').trim();
          var num = parseFloat(valStr);

          if (!isNaN(num)) {
            currentConfig[key] = num;
          }

          var span = document.getElementById(key);
          if (span) {
            var unit = configUnits[key] || '';
            span.textContent = (valStr === "N/A") ? valStr : (valStr + unit);
          }

          var input = document.getElementById(key + 'Input');
          if (input) {
            input.value = valStr;
          }

                    if (key === 'collectorFreezeSensors') {
            currentCollectorSensors = valStr.split(/[\|,]/).map(Number).filter(v => v > 0);

            var sensorSpan = document.getElementById('collectorFreezeSensors');
            if (sensorSpan) {
              sensorSpan.style.display = 'inline';     // ✅ force correct layout immediately
              sensorSpan.style.lineHeight = '1.2';
              sensorSpan.innerHTML = formatFreezeSensorLines(currentCollectorSensors) || '--';
            }

            document.querySelectorAll('#collectorFreezeSensorsInput .sensor-item input').forEach(cb => {
              cb.checked = currentCollectorSensors.includes(Number(cb.value));
            });
          }
           
                      else if (key === 'lineFreezeSensors') {
            currentLineSensors = valStr.split(/[\|,]/).map(Number).filter(v => v > 0);

            var sensorSpan2 = document.getElementById('lineFreezeSensors');
            if (sensorSpan2) {
              sensorSpan2.style.display = 'inline';     // ✅ force correct layout immediately
              sensorSpan2.style.lineHeight = '1.2';
              sensorSpan2.innerHTML = formatFreezeSensorLines(currentLineSensors) || '--';
            }

            document.querySelectorAll('#lineFreezeSensorsInput .sensor-item input').forEach(cb => {
              cb.checked = currentLineSensors.includes(Number(cb.value));
            });
          }

        });
      }

      // -------- TIME CONFIGURATION (timezone + DST) --------
      else if (data.startsWith('TimeConfig:')) {
        var tStr = data.substring('TimeConfig:'.length);
        var items = tStr.split(',');

        items.forEach(function (item) {
          var kv = item.split('=');
          if (kv.length < 2) return;
          var key = kv[0].trim();
          var val = kv.slice(1).join('=').trim();

          if (key === 'timeZoneId') {
            timeConfig.timeZoneId = val;
          } else if (key == 'dstEnabled') {
            timeConfig.dstEnabled = parseInt(val, 10) ? 1 : 0;
          }
        });

        updateTimeConfigView();
                syncTimeConfigEditorFromState();
      }

      // -------- DATE & TIME --------
      else if (data.startsWith('DateTime:')) {
        var parts = data.substring('DateTime:'.length).split(',');
        var dateStr = '', timeStr = '';
        parts.forEach(function (p) {
          var kv = p.split(':');
          if (kv.length >= 2) {
            if (kv[0].trim() === 'currentDate') dateStr = kv.slice(1).join(':').trim();
            if (kv[0].trim() === 'currentTime') timeStr = kv.slice(1).join(':').trim();
          }
        });
        if (dateStr) document.getElementById('currentDate').textContent = dateStr;
        if (timeStr) document.getElementById('currentTime').textContent = timeStr;

        // Start local ticking once we have both
        if (dateStr && timeStr) startClockTicker(dateStr, timeStr);
      }
      else if (data.match(/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/)) {
        var p = data.split(' ');
        document.getElementById('currentDate').textContent = p[0];
        document.getElementById('currentTime').textContent = p[1];

        startClockTicker(p[0], p[1]);
      }

      // -------- UPTIME --------
      else if (data.startsWith('Uptime:')) {
        var u = data.substring('Uptime:'.length).trim();
        document.getElementById('uptime').textContent = u;

        // Start local ticking from the controller's uptime
        startUptimeTickerFromString(u);
      }

      // -------- COMBINED HEAP + PSRAM (NEW) --------
      else if (data.startsWith('SysStats:')) {
        var payload = data.substring('SysStats:'.length).trim(); // "heap=...|psram=..."
        var parts = payload.split('|');

        parts.forEach(function (p) {
          var kv = p.split('=');
          if (kv.length < 2) return;

          var k = kv[0].trim();
          var v = kv.slice(1).join('=').trim();

          if (k === 'heap')  document.getElementById('heapUsage').textContent  = v;
          if (k === 'psram') document.getElementById('psramUsage').textContent = v;
        });
      }

      // -------- HEAP (legacy, still accepted) --------
      else if (data.startsWith('Heap:')) {
        document.getElementById('heapUsage').textContent =
          data.substring('Heap:'.length).trim();
      }

      // -------- PSRAM (legacy, still accepted) --------
      else if (data.startsWith('PSRAM:')) {
        document.getElementById('psramUsage').textContent =
          data.substring('PSRAM:'.length).trim();
      }


      // -------- FILE SYSTEM --------

      else if (data.startsWith('FSStats:')) {
        var payload = data.substring('FSStats:'.length).trim();
        var fsEl = document.getElementById('fsUsage');

        if (payload.startsWith("FS")) {
          if (fsEl) fsEl.textContent = payload;
          return;
        }

        try {
          var stats = JSON.parse(payload);
          var pct = parseFloat(stats.pctUsed);
          var pctStr = isNaN(pct) ? '--' : pct.toFixed(1) + String.fromCharCode(37);
          fsEl.textContent =
            stats.usedLabel + ' used of ' + stats.totalLabel + ' (' + pctStr + ')';
        } catch (e) {
          console.error('FSStats parse error:', payload);
        }
      }

      // -------- PUMP STATUS (JSON) --------
      else if (data.startsWith('PumpStatus:')) {
        try {
          var pumpStatusData =
            JSON.parse(data.substring('PumpStatus:'.length));
          updatePumpStatuses(pumpStatusData);
        } catch (e) {
          console.error('PumpStatus parse error:', e);
        }
      }

      // -------- LEGACY SINGLE PUMP UPDATE --------
      else if (data.includes("State") && data.includes("Mode")) {
        var parts2 = data.split(',');
        var pumpIndex = null, state = null, modeRaw = null;

        parts2.forEach(function (p2) {
          var kv = p2.split(':');
          if (kv.length < 2) return;
          var key = kv[0].trim();
          var val = kv[1].trim();
          var m;

          if ((m = key.match(/^pump(\d+)State$/))) {
            pumpIndex = parseInt(m[1], 10);
            state = val.toLowerCase();
          }

          if ((m = key.match(/^pump(\d+)Mode$/))) {
            pumpIndex = parseInt(m[1], 10);
            modeRaw = val;
          }
        });

        if (pumpIndex && state && modeRaw) {
          var mode =
            modeRaw.charAt(0).toUpperCase() +
            modeRaw.slice(1).toLowerCase();

          pumpStates[pumpIndex] = {
            state: state,
            mode: mode,
            name: pumpStates[pumpIndex]?.name || ('Pump ' + pumpIndex)
          };

          var stateEl = document.getElementById('pump' + pumpIndex + 'State');
          if (stateEl) {
            stateEl.className = state;
            stateEl.textContent = state;
          }

          var selectEl = document.getElementById('pump' + pumpIndex + 'Mode');
          if (selectEl) {
            selectEl.value = mode;
          }
        }
      }

      // ---------------- HEATING CALLS ----------------
      else if (data.startsWith('HeatingCalls:')) {
        var heatingCallData = data.substring('HeatingCalls:'.length).split(',');
        heatingCallData.forEach(function (item) {
          var keyValue = item.split(':');
          if (keyValue.length >= 2) {
            var key = keyValue[0].trim();
            var value = keyValue[1].trim();

            var statusElement = null;
            if (key === 'DHW') {
              statusElement = document.getElementById('dhwHeatingCallStatus');
            } else if (key === 'Heating') {
              statusElement = document.getElementById('heatingCallStatus');
            }

            if (statusElement) {
              statusElement.textContent = value;
              if (value === 'ACTIVE') {
                statusElement.style.color = 'blue';
                statusElement.style.fontWeight = 'bold';
              } else {
                statusElement.style.color = 'black';
                statusElement.style.fontWeight = 'normal';
              }
            }
          }
        });
      }

      else if (data.startsWith('AlarmState:')) {
        const payload = data.substring('AlarmState:'.length);
        const parts = payload.split(',');
        const state = parts[0].trim();
        let count = 0;
        parts.forEach(p => {
          const kv = p.split('=');
          if (kv[0] && kv[0].trim() === 'count') count = parseInt(kv[1],10) || 0;
        });

        const el = document.getElementById('alarmState');
        if (!el) return;

        if (state === 'ALARM') {
          el.textContent = `ALARM (${count})`;
          el.classList.add('alarm-active','alarm-blink');
        } else {
          el.textContent = 'OK';
          el.classList.remove('alarm-active','alarm-blink');
        }
      }
    } // end handleWebSocketMessage

    const editBtn   = document.getElementById('editConfigBtn');
    const saveBtn   = document.getElementById('saveConfigBtn');
    const cancelBtn = document.getElementById('cancelConfigBtn');
    const resetBtn  = document.getElementById('resetConfigBtn');

    // ------------- Config edit/save UI -------------
    function setConfigEditMode(on) {
      configEditMode = on;

      configKeys.forEach(function (key) {
        var span  = document.getElementById(key);
        var input = document.getElementById(key + 'Input');
        if (!span || !input) return;

        span.style.display  = on ? 'none'  : 'inline';
        input.style.display = on ? 'inline' : 'none';
      });

      ['collectorFreezeSensors', 'lineFreezeSensors'].forEach(k => {
        var span = document.getElementById(k);
        var listDiv = document.getElementById(k + 'Input');
        if (span && listDiv) {
          span.style.display = on ? 'none' : 'inline';
          listDiv.style.display = on ? 'block' : 'none';
        }
      });

      if (editBtn)   editBtn.style.display   = on ? 'none'         : 'inline-block';
      if (saveBtn)   saveBtn.style.display   = on ? 'inline-block' : 'none';
      if (cancelBtn) cancelBtn.style.display = on ? 'inline-block' : 'none';
      if (resetBtn)  resetBtn.style.display  = on ? 'inline-block' : 'none';
    }

    if (editBtn) {
      editBtn.addEventListener('click', function () {
        setConfigEditMode(true);
      });
    }

    if (resetBtn) {
      resetBtn.addEventListener('click', function () {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          alert('WebSocket not connected; cannot reset config');
          return;
        }

        if (!confirm('Restore factory defaults for System Configuration values? This will overwrite current settings.')) {
          return;
        }

        console.log('Requesting SystemConfig reset to defaults');
        ws.send('resetConfig');
      });
    }

    if (cancelBtn) {
      cancelBtn.addEventListener('click', function () {
        configKeys.forEach(function (key) {
          var span  = document.getElementById(key);
          var input = document.getElementById(key + 'Input');
          if (!span || !input) return;

          if (currentConfig.hasOwnProperty(key)) {
            var v = currentConfig[key];
            var unit = configUnits[key] || '';
            span.textContent = String(v) + unit;
            input.value = v;
          }
        });

        document.querySelectorAll('#collectorFreezeSensorsInput .sensor-item input').forEach(cb => {
          cb.checked = currentCollectorSensors.includes(Number(cb.value));
        });
        document.querySelectorAll('#lineFreezeSensorsInput .sensor-item input').forEach(cb => {
          cb.checked = currentLineSensors.includes(Number(cb.value));
        });

        var cfSpan = document.getElementById('collectorFreezeSensors');
        if (cfSpan) cfSpan.innerHTML = formatFreezeSensorLines(currentCollectorSensors) || '--';
        var lfSpan = document.getElementById('lineFreezeSensors');
        if (lfSpan) lfSpan.innerHTML = formatFreezeSensorLines(currentLineSensors) || '--';


        setConfigEditMode(false);
      });
    }

    if (saveBtn) {
      saveBtn.addEventListener('click', function () {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          alert('WebSocket not connected; cannot save config');
          return;
        }

        var parts = [];
        configKeys.forEach(function (key) {
          var input = document.getElementById(key + 'Input');
          if (!input) return;
          var v = input.value.trim();
          if (v.length === 0) return;
          parts.push(key + '=' + v);
        });

        ['collectorFreezeSensors', 'lineFreezeSensors'].forEach(k => {
  const checked = [];
  document.querySelectorAll('#' + k + 'Input .sensor-item input:checked').forEach(cb => {
    checked.push(cb.value);
  });
  if (checked.length > 0) parts.push(k + '=' + checked.join('|'));  // <-- pipe
});

        if (parts.length === 0) {
          alert('No values to save');
          return;
        }

        var msg = 'setConfig:' + parts.join(',');
        console.log('Sending config:', msg);
        ws.send(msg);

        setConfigEditMode(false);
      });
    }

    // ------------- Time config edit/save UI (timezone + DST) -------------
    const editTimeBtn   = document.getElementById('editTimeConfigBtn');
    const saveTimeBtn   = document.getElementById('saveTimeConfigBtn');
    const cancelTimeBtn = document.getElementById('cancelTimeConfigBtn');
    const resetTimeBtn  = document.getElementById('resetTimeConfigBtn');

    function timeZoneLabelFromId(id) {
      switch (id) {
        case 'UTC':         return 'UTC';
        case 'US_PACIFIC':  return 'US Pacific (PST/PDT)';
        case 'US_MOUNTAIN': return 'US Mountain (MST/MDT)';
        case 'US_CENTRAL':  return 'US Central (CST/CDT)';
        case 'US_EASTERN':  return 'US Eastern (EST/EDT)';
        default:            return id || '--';
      }
    }

    function updateTimeConfigView() {
      var tzSpan  = document.getElementById('timeZoneDisplay');
      var dstSpan = document.getElementById('dstEnabledDisplay');
      if (tzSpan) tzSpan.textContent = timeZoneLabelFromId(timeConfig.timeZoneId);
      if (dstSpan) dstSpan.textContent = (timeConfig.dstEnabled ? 'Yes' : 'No');
    }

    function syncTimeConfigEditorFromState() {
      var tzSelect  = document.getElementById('timeZoneSelect');
      var dstSelect = document.getElementById('dstEnabledSelect');
      if (tzSelect && timeConfig.timeZoneId) tzSelect.value = timeConfig.timeZoneId;
      if (dstSelect) dstSelect.value = timeConfig.dstEnabled ? '1' : '0';
    }

    function setTimeConfigEditMode(on) {
      var viewDiv   = document.getElementById('timeInfoView');
      var editorDiv = document.getElementById('timeConfigEditor');
      if (viewDiv)   viewDiv.style.display   = on ? 'none'  : 'block';
      if (editorDiv) editorDiv.style.display = on ? 'block' : 'none';
      if (on) syncTimeConfigEditorFromState();
    }

    if (editTimeBtn) {
      editTimeBtn.addEventListener('click', function () {
        setTimeConfigEditMode(true);
      });
    }
    if (cancelTimeBtn) {
      cancelTimeBtn.addEventListener('click', function () {
        setTimeConfigEditMode(false);
      });
    }
    if (saveTimeBtn) {
      saveTimeBtn.addEventListener('click', function () {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          alert('WebSocket not connected; cannot save time config');
          return;
        }

        var tzSelect  = document.getElementById('timeZoneSelect');
        var dstSelect = document.getElementById('dstEnabledSelect');

        var tzId   = tzSelect ? tzSelect.value : '';
        var dstVal = dstSelect ? dstSelect.value : '1';

        if (!tzId) {
          alert('Please select a time zone');
          return;
        }

        var msg = 'setTimeConfig:timeZoneId=' + tzId + ',dstEnabled=' + dstVal;
        console.log('Sending time config:', msg);
        ws.send(msg);

        timeConfig.timeZoneId = tzId;
        timeConfig.dstEnabled = (dstVal === '1');
        updateTimeConfigView();

        setTimeConfigEditMode(false);
      });
    }
    if (resetTimeBtn) {
      resetTimeBtn.addEventListener('click', function () {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          alert('WebSocket not connected; cannot reset time config');
          return;
        }

        if (!confirm('Restore factory defaults for Time Configuration values?')) return;

        console.log('Requesting TimeConfig reset to defaults');
        ws.send('resetTimeConfig');
      });
    }

    updateTimeConfigView();

    // ==========================================================
    // ✅ PUMP STATUS UI BUILDER
    // ==========================================================
    function updatePumpStatuses(pumpStatusData) {
      if (pumpStatusData && pumpStatusData.length > 0) {
          activePumpCount = pumpStatusData.length; // Dynamically scale UI to match backend numPumps
      }
      
      pumpStatusData.forEach(function (pump) {
        var pumpIndex = pump.pumpIndex;
        var pumpState = pump.state.toLowerCase().trim();
        var pumpMode =
          pump.mode.charAt(0).toUpperCase() +
          pump.mode.slice(1).toLowerCase();

        pumpStates[pumpIndex] = {
          state: pumpState,
          mode: pumpMode,
          name: pump.name
        };
      });

      var pumpsContainer = document.getElementById('pumps');
      pumpsContainer.innerHTML = '';

      for (let i = 1; i <= activePumpCount; i++) {
        let pump = pumpStates[i] || { state: '--', mode: 'Auto', name: 'Pump ' + i };

        var pumpDiv = document.createElement('div');
        pumpDiv.className = 'pump';

        var titleSpan = document.createElement('span');
        titleSpan.className = 'pump-title';
        titleSpan.textContent = pump.name;

        var modeSpan = document.createElement('span');
        modeSpan.className = 'pump-mode';

        var label = document.createElement('label');
        label.textContent = 'Mode:';

        var select = document.createElement('select');
        select.id = 'pump' + i + 'Mode';

        select.onchange = function () {
          changePumpMode(i, this.value);
        };

        ['Auto', 'On', 'Off'].forEach(function (v) {
          var opt = document.createElement('option');
          opt.value = v;
          opt.textContent = v;
          if (pump.mode === v) opt.selected = true;
          select.appendChild(opt);
        });

        modeSpan.appendChild(label);
        modeSpan.appendChild(select);

        var stateSpan = document.createElement('span');
        stateSpan.className = 'pump-state';
        stateSpan.innerHTML =
          'State: <span id="pump' + i + 'State" class="' + pump.state + '">' +
          pump.state +
          '</span>';

        pumpDiv.appendChild(titleSpan);
        pumpDiv.appendChild(modeSpan);
        pumpDiv.appendChild(stateSpan);
        pumpsContainer.appendChild(pumpDiv);
      }
    }

    function changePumpMode(pumpIndex, mode) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('setPumpMode:' + pumpIndex + ':' + mode.toLowerCase());
      }
      if (pumpStates[pumpIndex]) {
        pumpStates[pumpIndex].mode = mode;
      }
      var selectEl = document.getElementById('pump' + pumpIndex + 'Mode');
      if (selectEl) selectEl.value = mode;
    }
    window.changePumpMode = changePumpMode;

    const alarmBtn = document.getElementById('alarmLogBtn');
    if (alarmBtn) {
      alarmBtn.addEventListener('click', () => {
        const w = window.open('/alarm-log', '_blank');
        if (w) w.opener = null;
      });
    }

    // ==========================================================
    // ✅ AUTO-SCALE IFRAMES TO FIT THEIR CONTENT (NO SCROLLBARS)
    // ==========================================================
    function setupAutoScaledIframe(containerId, iframeId, maxScale, initialDesignW, initialDesignH) {
      var container = document.getElementById(containerId);
      var iframe = document.getElementById(iframeId);
      if (!container || !iframe) return;

      var designW = initialDesignW || 1024;
      var designH = initialDesignH || 768;
      var rafPending = false;

      iframe.style.width  = designW + 'px';
      iframe.style.height = designH + 'px';

      function measure() {
        try {
          var win = iframe.contentWindow;
          if (!win) return;
          var doc = win.document;
          if (!doc) return;

          var de = doc.documentElement;
          var body = doc.body;

          var w = Math.max(
            de ? de.scrollWidth : 0,
            body ? body.scrollWidth : 0,
            de ? de.clientWidth : 0,
            body ? body.clientWidth : 0
          );

          var h = Math.max(
            de ? de.scrollHeight : 0,
            body ? body.scrollHeight : 0,
            de ? de.clientHeight : 0,
            body ? body.clientHeight : 0
          );

          if (w > 200 && h > 50) {
            designW = Math.min(Math.max(w, 200), 6000);
            designH = Math.min(Math.max(h, 50), 6000);
          }
        } catch (e) {
          // If cross-origin ever happens, we just keep defaults
        }
      }

      function apply() {
        rafPending = false;

        var cw = container.clientWidth;
        if (!cw) return;

        measure();

        // ✅ scale based on width (container height is derived from scaled content)
        var s = cw / designW;
        if (typeof maxScale === 'number') s = Math.min(s, maxScale);

        // safety clamp
        s = Math.max(s, 0.05);

        // ✅ set container height to match scaled iframe height
        var usedH = Math.ceil(designH * s);
        container.style.height = usedH + 'px';


        var usedH = Math.ceil(designH * s);
        container.style.height = usedH + 'px';


        // size the iframe at "design" size, then scale it
        iframe.style.width  = designW + 'px';
        iframe.style.height = designH + 'px';
        iframe.style.top    = '0px';

        // ✅ center horizontally inside container (but stay top aligned)
        var usedW = designW * s;
        var x = Math.max(0, (cw - usedW) / 2);
        iframe.style.left = x + 'px';

        iframe.style.transform = 'scale(' + s + ')';



      }

      function schedule() {
        if (rafPending) return;
        rafPending = true;
        requestAnimationFrame(apply);
      }

      iframe.addEventListener('load', function () {
        measure();
        schedule();
        setTimeout(function(){ measure(); schedule(); }, 80);
        setTimeout(function(){ measure(); schedule(); }, 250);
        setTimeout(function(){ measure(); schedule(); }, 800);
      });

      if (window.ResizeObserver) {
        var ro = new ResizeObserver(function(){ schedule(); });
        ro.observe(container);
      }

      window.addEventListener('resize', schedule, { passive: true });

      schedule();
      setInterval(schedule, 2000);
    }

    // Increased base width from 640 to 700 to force text onto a single line before scaling down
    setupAutoScaledIframe('pumpRuntimesContainer', 'pumpRuntimesIframe', 2, 700, 220);
    setupAutoScaledIframe('tempLogsContainer', 'tempLogsIframe', 2, 1024, 768);

  }); // end DOMContentLoaded
  </script>

</body>
</html>
)rawliteral";

void setupFirstPageRoutes() {
  // New lightweight endpoint for the Javascript to fetch the version
  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", VERSION_INFO);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Sent directly from PROGMEM via high-speed DMA. ZERO CPU scanning!
    request->send_P(200, "text/html; charset=UTF-8", firstPageHtml);
  });
}


