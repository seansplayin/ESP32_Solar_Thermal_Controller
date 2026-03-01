// FirstWebpage.cpp
#include "WebServerManager.h"
#include "FirstWebpage.h"
#include "Config.h"
#include "FileSystemManager.h"
#include "DiagLog.h"


#define VERSION_INFO " -AsyncWebServer123_ESP_V5_IDE_2.3.6- "

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
      width: 100%;
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
      font-size:14px;
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
    #editTimeConfigBtn {
      position: absolute;
      left: 4px;
      bottom: 4px;
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
  width: 100%;
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
</head>

<body>
  <table border="10" cellpadding="4" cellspacing="5" bgcolor="white">
    <tr>
      <td valign="top" align="left" bgcolor="white" id="timeCell">

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

          <button id="editTimeConfigBtn" class="blue-button">Edit Time Config</button>
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

      <td valign="top" align="center" bgcolor="white">
        <div><h1>Solar Thermal</h1></div>
        <div><h1>System Controller</h1></div>
        <div><h6>Thermal Collection & Distribution with logging</h6></div>
      </td>

      <td valign="top" align="center" bgcolor="white" id="statusCell">
        <div class="statusRows">
          <div>Alarm state = <span id="alarmState" style="color:blue;">OK</span></div>
          <div>Version = <span style="color:blue">%VERSION_INFO%</span></div>
          <div>Heap (Internal RAM): <span id="heapUsage" style="color:blue">--</span></div>
          <div>PSRAM: <span id="psramUsage" style="color:blue">--</span></div>
          <div>File System (Flash Storeage): <span id="fsUsage" style="color:blue">--</span></div>

        </div>

        <button id="alarmLogBtn" class="blue-button">Alarm Log</button>
      </td>
    </tr>




    <tr>
      <td valign="top" id="configCell">
      <div id="SectionHeader" class="configContent">      
        <h3>System Temperatures</h3>
        <h2>Outside Temperatures</h2>

        <p class="temp-item">Outside Ambient (DTemp3Average): <span id="outsideT">--</span></p>
        <p class="temp-item">600 Gal Storage (DTemp2Average): <span id="storageT">--</span></p>
        <p class="temp-item">Collector Manifold (PT1000Average): <span id="panelT">--</span></p>
        <p class="temp-item">Collector Supply (DTemp1Average): <span id="CSupplyT">--</span></p>
        <p class="temp-item">Collector Return (DTemp6Average): <span id="CreturnT">--</span></p>
        <p class="temp-item">Circ Loop Supply (DTemp4Average): <span id="supplyT">--</span></p>
        <p class="temp-item">Circ Loop Return (DTemp5Average): <span id="CircReturnT">--</span></p>

        <br>
        <h2>Inside Temperatures</h2>

        <p class="temp-item">DHW Glycol Supply (DTemp7Average): <span id="DhwSupplyT">--</span></p>
        <p class="temp-item">DHW Glycol Return (DTemp8Average): <span id="DhwReturnT">--</span></p>
        <p class="temp-item">Furance Glycol Loop Supply (DTemp9Average): <span id="HeatingSupplyT">--</span></p>
        <p class="temp-item">Furance Glycol Loop Return (DTemp10Average): <span id="HeatingReturnT">--</span></p>
        <p class="temp-item">DHW Pot EXCH In (DTemp12Average): <span id="PotHeatXinletT">--</span></p>
        <p class="temp-item">DHW Pot EXCH Out (DTemp13Average): <span id="PotHeatXoutletT">--</span></p>
        <p class="temp-item">DHW Pot Inline heater Out (DTemp11Average): <span id="dhwT">--</span></p>
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
          <iframe src="/second-page?ts=%UNIXTIME%" id="pumpRuntimesIframe" scrolling="no"></iframe>
        </div>
      </td>
    </tr>


    <tr>
      <td valign="top" id="configCell">
        <div id="SectionHeader">
          <h3>Temperature Values</h3>

          <p class="temp-item">pt1000Current: <span id="pt1000Current">--</span>  pt1000Average: <span id="pt1000Average">--</span></p>
          <p class="temp-item">DTemp1: <span id="DTemp1">--</span>  DTempAverage1: <span id="DTempAverage1">--</span></p>
          <p class="temp-item">DTemp2: <span id="DTemp2">--</span>  DTempAverage2: <span id="DTempAverage2">--</span></p>
          <p class="temp-item">DTemp3: <span id="DTemp3">--</span>  DTempAverage3: <span id="DTempAverage3">--</span></p>
          <p class="temp-item">DTemp4: <span id="DTemp4">--</span>  DTempAverage4: <span id="DTempAverage4">--</span></p>
          <p class="temp-item">DTemp5: <span id="DTemp5">--</span>  DTempAverage5: <span id="DTempAverage5">--</span></p>
          <p class="temp-item">DTemp6: <span id="DTemp6">--</span>  DTempAverage6: <span id="DTempAverage6">--</span></p>
          <p class="temp-item">DTemp7: <span id="DTemp7">--</span>  DTempAverage7: <span id="DTempAverage7">--</span></p>
          <p class="temp-item">DTemp8: <span id="DTemp8">--</span>  DTempAverage8: <span id="DTempAverage8">--</span></p>
          <p class="temp-item">DTemp9: <span id="DTemp9">--</span>  DTempAverage9: <span id="DTempAverage9">--</span></p>
          <p class="temp-item">DTemp10: <span id="DTemp10">--</span>  DTempAverage10: <span id="DTempAverage10">--</span></p>
          <p class="temp-item">DTemp11: <span id="DTemp11">--</span>  DTempAverage11: <span id="DTempAverage11">--</span></p>
          <p class="temp-item">DTemp12: <span id="DTemp12">--</span>  DTempAverage12: <span id="DTempAverage12">--</span></p>
          <p class="temp-item">DTemp13: <span id="DTemp13">--</span>  DTempAverage13: <span id="DTempAverage13">--</span></p>
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
              <iframe src="/third-page?ts=%UNIXTIME%" id="tempLogsIframe" scrolling="no"
      style="width:108.7%; border:0; transform:scale(0.92); transform-origin: top left;"></iframe>

        </div>
      </td>
    </tr>
  </table>




  <script>



  window.addEventListener("message", (event) => {
    if (!event.data || event.data.type !== "thirdPageHeight") return;
    if (event.origin !== window.location.origin) return;
    const iframe = document.getElementById("tempLogsIframe");
    if (!iframe) return;
    iframe.style.height = (event.data.height + 10) + "px";
  });


    document.addEventListener('DOMContentLoaded', function () {
    // WebSocket with auto-reconnect
    var ws = null;
    var wsReconnectTimer = null;
    var wsBackoffMs = 1000;           // starts at 1s
    var wsBackoffMaxMs = 30000;       // max 30s

    function wsConnect() {
      // Clear any pending reconnect
      if (wsReconnectTimer) { clearTimeout(wsReconnectTimer); wsReconnectTimer = null; }

      ws = new WebSocket('ws://' + window.location.hostname + '/ws');

      ws.onopen = function () {
        console.log('WebSocket connected');
        wsBackoffMs = 1000; // reset backoff on success

        ws.send('hello:FirstWebpage');
        ws.send('init');

        // One-time sync; browser ticks locally after this
        if (ws.readyState === WebSocket.OPEN) ws.send('getUptime');
      };

      ws.onmessage = function (event) {
        handleWebSocketMessage(event.data);
      };

      ws.onclose = function () {
        console.log('WebSocket closed - scheduling reconnect');
        // Exponential backoff
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

    const pumpStates = Array(11).fill(null);

    // ---- Time config state (timezone + DST) ----
    let timeConfig = {
      timeZoneId: 'US_MOUNTAIN',
      dstEnabled: 1
    };

    // ✅ ENFORCE 1–10 INDEXING (NO PUMP 0)
    for (let i = 1; i <= 10; i++) {
          pumpStates[i] = { state: '--', mode: 'Auto', name: 'Pump ' + i };
    }

        // ---- Local ticking (reduces WS traffic massively) ----
    var uptimeSecondsBase = null;
    var uptimeTickTimer   = null;

    var controllerClockBase = null;   // JS Date object
    var clockTickTimer      = null;

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
  // IMPORTANT: declare these vars (your console error is from missing "hours")
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

      if (uptimeTickTimer) clearInterval(uptimeTickTimer);
      uptimeTickTimer = setInterval(function () {
        if (uptimeSecondsBase == null) return;
        uptimeSecondsBase++;
        var el = document.getElementById('uptime');
        if (el) el.textContent = formatUptimeFromSeconds(uptimeSecondsBase);
      }, 1000);
    }

    function startClockTicker(dateStr, timeStr) {
      // dateStr: "YYYY-MM-DD", timeStr: "HH:MM:SS"
      // Use an ISO-ish string; we tick locally afterwards
      var iso = dateStr + "T" + timeStr;
      var d = new Date(iso);
      if (isNaN(d.getTime())) return;

      controllerClockBase = d;

      if (clockTickTimer) clearInterval(clockTickTimer);
      clockTickTimer = setInterval(function () {
        if (!controllerClockBase) return;
        controllerClockBase = new Date(controllerClockBase.getTime() + 1000);

        var yyyy = controllerClockBase.getFullYear();
        var mm   = String(controllerClockBase.getMonth() + 1).padStart(2, '0');
        var dd   = String(controllerClockBase.getDate()).padStart(2, '0');
        var hh   = String(controllerClockBase.getHours()).padStart(2, '0');
        var mi   = String(controllerClockBase.getMinutes()).padStart(2, '0');
        var ss   = String(controllerClockBase.getSeconds()).padStart(2, '0');

        var dateEl = document.getElementById('currentDate');
        var timeEl = document.getElementById('currentTime');
        if (dateEl) dateEl.textContent = yyyy + "-" + mm + "-" + dd;
        if (timeEl) timeEl.textContent = hh + ":" + mi + ":" + ss;
      }, 1000);
    }


    ws.onopen = function () {
      console.log('WebSocket connected');
      ws.send('hello:FirstWebpage');
      ws.send('init');

      // One-time sync; browser ticks locally after this
      if (ws.readyState === WebSocket.OPEN) ws.send('getUptime');
    };

    setInterval(function () {
      if (ws.readyState === WebSocket.OPEN) ws.send('ping');
    }, 30000);

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
      "Circ Loop Return Temperature (DTemp5)",
      "Circ Loop Supply Temperature (DTemp4)",
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


    ws.onmessage = function (event) {
      handleWebSocketMessage(event.data);
    };

    document.getElementById('allAutoButton').addEventListener('click', function () {
      ws.send('setAllPumps:auto');
    });

    document.getElementById('allOffButton').addEventListener('click', function () {
      ws.send('setAllPumps:off');
    });

    // ==========================================================
    // ✅ MAIN MESSAGE DISPATCHER
    // ==========================================================
    function handleWebSocketMessage(data) {
      console.log('Processing:', data);

      // -------- TEMPERATURES --------
      if (data.startsWith('Temperatures:')) {
        var tempData = data.substring('Temperatures:'.length).split(',');
        tempData.forEach(function (item) {
          var kv = item.split(':');
          var el = document.getElementById(kv[0]);
          if (el) {
            var v = kv.slice(1).join(':').trim();
            el.textContent = (v === "N/A") ? v : v + '°F';
          }
        });
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
          var pctStr = isNaN(pct) ? '--' : pct.toFixed(1) + '%';
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

      for (let i = 1; i <= 10; i++) {
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
    function setupAutoScaledIframe(containerId, iframeId, maxScale) {
      var container = document.getElementById(containerId);
      var iframe = document.getElementById(iframeId);
      if (!container || !iframe) return;

      var designW = 1024;
      var designH = 768;
      var rafPending = false;

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

          if (w > 200 && h > 200) {
            designW = Math.min(Math.max(w, 200), 6000);
            designH = Math.min(Math.max(h, 200), 6000);
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

    setupAutoScaledIframe('pumpRuntimesContainer', 'pumpRuntimesIframe', 2);
    setupAutoScaledIframe('tempLogsContainer', 'tempLogsIframe', 2);

  }); // end DOMContentLoaded
  </script>

</body>
</html>
)rawliteral";

String processor(const String& var) {
  if (var == "VERSION_INFO") {
    return VERSION_INFO;
  }
  if (var == "UNIXTIME") {
    return String(millis());
  }
  return String();
}

void setupFirstPageRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=UTF-8", firstPageHtml, processor);
  });
}
