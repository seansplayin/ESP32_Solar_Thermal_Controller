#include "WebServerManager.h"
#include "FirstWebpage.h"
#include "Config.h"
#include "FileSystemManager.h"

#define VERSION_INFO " -AsyncWebServer090_ESP_V5_IDE_2.3.6- "

extern AsyncWebServer server;

const char firstPageHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<link rel="icon" type="image/png" sizes="48x48" href="/favicon.png">
    <title>Solar Control System</title>
    <meta charset="UTF-8">
    <style type="text/css">
        h1{
          color:purple;
          font-size:20px;
          line-height:70%%;
          font-weight:bold;
        }
        h2{
         color:#459;
         font-size:12px;
         line-height:100%%;
         font-weight:bold;
         text-align:center;
         }
         h3{
          color:#459;
          font-size:18px;
          line-height:100%%;
          font-weight:bold;
          text-align:center;
         }
         h4{
          color:purple;
          font-size:11px;
          line-height:100%%;
          font-weight:bold;
          text-align:center;
          { margin: 0; padding: 0; }
         }
         h5{
          color:purple;
          font-size:11px;
          line-height:100%%;
          font-weight:bold;
          text-align:center;
          { margin: 0; padding: 0; }
         }
         h6{
          color:purple;
          font-size:12px;
          line-height:70%%;
          font-weight:bold;
          text-align:center;
         }
         h7{
          color:#459;
          font-size:11px;
          line-height:50%%;
          font-weight:bold;
          text-align:left;
         }
         h8{
          color:purple;
          font-size:14px;
          line-height:100%%;
          font-weight:bold;
          text-align:center;
         }
         h9{
          color:#000000;
          font-size:14px;
          line-height:100%%;
          
          text-align:left;
         }
         h10{
          color:#000000;
          font-size:14px;
          line-height:100%%;
          
          text-align:right;
         }
        body{
             
            font-family:'Lucida Sans Unicode', 'Lucida Grande', sans-serif, Helvetica;
            font-size:14px;
            line-height:100%%;
            text-align:left;
         }
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
            padding-right: 20px; /* Adjust as needed flex: 2 2 auto;
            display: flex;*/
            flex-wrap: nowrap;
        }
        .pump-mode label {
        margin-right: 1px;
    }
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
    .pump select:hover {
        background-color: darkblue;
    }
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
    .blue-button:hover {
        background-color: darkblue;
    }
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
    #configCell {
        position: relative;
    }

    #configCell .configContent {
    padding-bottom: 24px;   /* leave room so text doesn't sit under the buttons */
    }

    #configButtons {
        position: absolute;
        left: 4px;
        bottom: 4px;
    }

    #configButtons .blue-button {
        margin-right: 4px;
    }

        #timeInfoView .timeRows {
        display: flex;
        flex-direction: column;
        gap: 8px;                    /* “blank line” spacing between rows */
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
    
    #timeCell {
        position: relative;
    }

    #timeCell .timeContent {
        padding-bottom: 26px;   /* leave room so content never overlaps the button */
    }

    #editTimeConfigBtn {
    position: absolute;
    left: 4px;
    bottom: 4px;
    margin: 0;
    }

    #statusCell {
        position: relative;
        padding: 4px 4px 26px 4px;   /* match time cell exactly */
    }

    #statusCell .statusRows {
        display: flex;
        flex-direction: column;
        gap: 8px;                    /* match time cell */
        align-items: flex-end;        /* right-edge alignment */
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

    #heatingCalls p {
    color: purple;
    font-size: 14px;
    }

    #heatingCalls span {
        font-weight: normal;
    }

    #TemperatureValues {
        font-size: 14px;
        color: #000000;
    }

    .temperature-row {
        display: flex;
        justify-content: space-between;
        line-height: 1.5;
    }

    .temperature-row .left {
        text-align: left;
        width: 50%;
    }

    .temperature-row .right {
        text-align: right;
        width: 50%;
    }

    #InformationBox {
        font-size: 14px;
        color: #000000;
        line-height: 0.5;
        margin: 15px 0;
        justify-content: center;
    }

    .alarm-active { color:red !important; font-weight:bold; }
    .alarm-blink  { animation: blinker 1s linear infinite; }
    @keyframes blinker { 50% { opacity: 0; } }


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
            <div>
                <h1>Advanced</h1>
                </div><div>
                <h1>Solar Thermal System</h1>
                </div><div>
                <h6>Energy Collecting & Distribution Control system with logging</h6></div>
            </td>
            

            <td valign="top" align="center" bgcolor="white" id="statusCell">

  <div class="statusRows">
    <div>Alarm state = <span id="alarmState" style="color:blue;">OK</span></div>
    <div>Version = <span style="color:blue">%VERSION_INFO%</span></div>
    <div>Heap (RAM Usage): <span id="heapUsage" style="color:blue">--</span></div>
    <div>File System (Flash Storeage): <span id="fsUsage" style="color:blue">--</span></div>
  </div>

  <button id="alarmLogBtn" class="blue-button">Alarm Log</button>

</td>



        </tr>
        <tr>
            <td valign="top" width="33%%" > 
                <h3>System Temperatures</h3>
                <div>
                    <h2>Outside Temperatures</h2>
                    </div>
                    <div>
                        Outside Ambient (DTemp3Average): <span id="outsideT">--</span>
                        <br>
                        600 Gal Storage (DTemp2Average): <span id="storageT">--</span>
                        <br>
                        Collector Manifold (PT1000Average): <span id="panelT">--</span>
                        <br>
                        Collector Supply (DTemp1Average): <span id="CSupplyT">--</span>
                        <br>
                        Collector Return (DTemp6Average): <span id="CreturnT">--</span>
                        <br>
                        Circ Loop Supply (DTemp4Average): <span id="supplyT">--</span>
                        <br>
                        Circ Loop Return (DTemp5Average): <span id="CircReturnT">--</span>
                        <br>
                                               
                    </div>
                    <h2>Inside Temperatures</h2>
                    <div>
                        DHW Glycol Supply (DTemp7Average): <span id="DhwSupplyT">--</span>
                        <br>
                        DHW Glycol Return (DTemp8Average): <span id="DhwReturnT">--</span>
                        <br>
                        Furance Glycol Loop Supply (DTemp9Average): <span id="HeatingSupplyT">--</span>
                        <br>
                        Furance Glycol Loop Return (DTemp10Average): <span id="HeatingReturnT">--</span>
                        <br>
                        DHW Pot EXCH In (DTemp12Average): <span id="PotHeatXinletT">--</span>
                        <br>
                        DHW Pot EXCH Out (DTemp13Average): <span id="PotHeatXoutletT">--</span>
                        <br>
                        DHW Pot Inline heater Out (DTemp11Average): <span id="dhwT">--</span>
                        <br>
                </div>
            </td>
            
        

    <!-- Update your 'Relay Status & Control' section to include the 'pumps' div -->
    <!-- Ensure that the 'pumps' div has an ID of 'pumps' -->
    <td valign="top" width="33%%" >
        <h3>Status & Control: Pumps, Valves, Heat Tape</h3>
        
        <h8><div id="globalControls2" style="text-align:center; margin-top:20px;">
    <p>Set All Pumps:  <button id="allAutoButton" class="blue-button">AUTO</button></div></p>
    
    <p>Set All Pumps:  <button id="allOffButton" class="blue-button">OFF</button></p></div></h8>
    
    <div id="heatingCalls">
        <p>Call for DHW Heating: <span id="dhwHeatingCallStatus">--</span></p>
        <p>Call for Heating: <span id="heatingCallStatus">--</span></p>
        
        <div id="pumps">
            <!-- Pump controls will be generated by JavaScript -->
        </div>
    </td>

<td valign="top" width="33%%" style="background-color:white" align="center">
    <div style="width:450px; height:400px; overflow:hidden; position:relative;">
  <iframe src="/third-page"
    style="width:900px; height:900px; border:none; transform:scale(0.5); transform-origin:0 0; position:absolute; top:0; left:0;">
  </iframe>
</div>

</td>

  
        </tr>
        
        <tr>
     
<td valign="top" width="33%" > 
    <div id="TemperatureValues">
        <h3>Temperature Values</h3>

        <!-- PT1000 Temperatures -->
        <div class="temperature-row">
            <span class="left">pt1000Current: <span id="pt1000Current">--</span></span>
            <span class="right">pt1000Average: <span id="pt1000Average">--</span></span>
        </div>

        <!-- DTemp Sensors -->
        <!-- DTemp1 -->
        <div class="temperature-row">
            <span class="left">DTemp1: <span id="DTemp1">--</span></span>
            <span class="right">DTempAverage1: <span id="DTempAverage1">--</span></span>
        </div>

        <!-- DTemp2 -->
        <div class="temperature-row">
            <span class="left">DTemp2: <span id="DTemp2">--</span></span>
            <span class="right">DTempAverage2: <span id="DTempAverage2">--</span></span>
        </div>

        <!-- DTemp3 -->
        <div class="temperature-row">
            <span class="left">DTemp3: <span id="DTemp3">--</span></span>
            <span class="right">DTempAverage3: <span id="DTempAverage3">--</span></span>
        </div>

        <!-- DTemp4 -->
        <div class="temperature-row">
            <span class="left">DTemp4: <span id="DTemp4">--</span></span>
            <span class="right">DTempAverage4: <span id="DTempAverage4">--</span></span>
        </div>

        <!-- DTemp5 -->
        <div class="temperature-row">
            <span class="left">DTemp5: <span id="DTemp5">--</span></span>
            <span class="right">DTempAverage5: <span id="DTempAverage5">--</span></span>
        </div>

        <!-- DTemp6 -->
        <div class="temperature-row">
            <span class="left">DTemp6: <span id="DTemp6">--</span></span>
            <span class="right">DTempAverage6: <span id="DTempAverage6">--</span></span>
        </div>

        <!-- DTemp7 -->
        <div class="temperature-row">
            <span class="left">DTemp7: <span id="DTemp7">--</span></span>
            <span class="right">DTempAverage7: <span id="DTempAverage7">--</span></span>
        </div>

        <!-- DTemp8 -->
        <div class="temperature-row">
            <span class="left">DTemp8: <span id="DTemp8">--</span></span>
            <span class="right">DTempAverage8: <span id="DTempAverage8">--</span></span>
        </div>

        <!-- DTemp9 -->
        <div class="temperature-row">
            <span class="left">DTemp9: <span id="DTemp9">--</span></span>
            <span class="right">DTempAverage9: <span id="DTempAverage9">--</span></span>
        </div>

        <!-- DTemp10 -->
        <div class="temperature-row">
            <span class="left">DTemp10: <span id="DTemp10">--</span></span>
            <span class="right">DTempAverage10: <span id="DTempAverage10">--</span></span>
        </div>

        <!-- DTemp11 -->
        <div class="temperature-row">
            <span class="left">DTemp11: <span id="DTemp11">--</span></span>
            <span class="right">DTempAverage11: <span id="DTempAverage11">--</span></span>
        </div>

        <!-- DTemp12 -->
        <div class="temperature-row">
            <span class="left">DTemp12: <span id="DTemp12">--</span></span>
            <span class="right">DTempAverage12: <span id="DTempAverage12">--</span></span>
        </div>

        <!-- DTemp13 -->
        <div class="temperature-row">
            <span class="left">DTemp13: <span id="DTemp13">--</span></span>
            <span class="right">DTempAverage13: <span id="DTempAverage13">--</span></span>
        </div>
    </div>
</td>


       <td valign="top" width="33%%" id="configCell">

  <div id="InformationBox" class="configContent">
    <h3>System Configuration</h3>

    <!-- Simple view/edit pair: span (view) + input (edit) -->
    <p>
      Min Lead Start Temp(PT1000):
      <span id="panelTminimum">--</span>
      <input type="number" step="0.1" id="panelTminimumInput"
             style="width:70px; display:none;">
    </p>

    <p>
      Lead On Diff.(PT1000 vs DTemp5):
      <span id="PanelOnDifferential">--</span>
      <input type="number" step="0.1" id="PanelOnDifferentialInput"
             style="width:70px; display:none;">
    </p>

    <p>
      Lag On Diff.(DTemp6 vs DTemp11):
      <span id="PanelLowDifferential">--</span>
      <input type="number" step="0.1" id="PanelLowDifferentialInput"
             style="width:70px; display:none;">
    </p>

    <p>
      Lead Off Diff.(PT1000 vs DTemp5):
      <span id="PanelOffDifferential">--</span>
      <input type="number" step="0.1" id="PanelOffDifferentialInput"
             style="width:70px; display:none;">
    </p>

    <br>

    <p>
      Boiler On Temperature:
      <span id="Boiler_Circ_On">--</span>
      <input type="number" step="0.1" id="Boiler_Circ_OnInput"
             style="width:70px; display:none;">
    </p>

    <p>
      Boiler Off Temperature:
      <span id="Boiler_Circ_Off">--</span>
      <input type="number" step="0.1" id="Boiler_Circ_OffInput"
             style="width:70px; display:none;">
    </p>

    <br>

    <p>
      600 Gallon High Temperature Limit:
      <span id="StorageHeatingLimit">--</span>
      <input type="number" step="0.1" id="StorageHeatingLimitInput"
             style="width:70px; display:none;">
    </p>

    <br>

    <p>
      Circ Loop On Diff.(DTemp5 vs DTemp6):
      <span id="Circ_Pump_On">--</span>
      <input type="number" step="0.1" id="Circ_Pump_OnInput"
             style="width:70px; display:none;">
    </p>

    <p>
      Circ Loop Off Diff.(DTemp5 vs DTemp6):
      <span id="Circ_Pump_Off">--</span>
      <input type="number" step="0.1" id="Circ_Pump_OffInput"
             style="width:70px; display:none;">
    </p>

    <br>

    <p>
      Heat Tape On Temperature:
      <span id="Heat_Tape_On">--</span>
      <input type="number" step="0.1" id="Heat_Tape_OnInput"
             style="width:70px; display:none;">
    </p>

            <p>
      Heat Tape Off Temperature:
      <span id="Heat_Tape_Off">--</span>
      <input type="number" step="0.1" id="Heat_Tape_OffInput"
             style="width:70px; display:none;">
    </p>

    <br>
    <h2>Freeze Protection</h2>

    <p>
      Collector Freeze Temp:
      <span id="collectorFreezeTempF">--</span>
      <input type="number" step="0.1" id="collectorFreezeTempFInput"
             style="width:70px; display:none;">
    </p>
    <p>
        Collector Confirm Time:
        <span id="collectorFreezeConfirmMin">--</span>
        <input type="number" step="1" min="1" max="120" id="collectorFreezeConfirmMinInput"
        style="width:70px; display:none;">
    </p>

    <p>
      Collector Run Time:
      <span id="collectorFreezeRunMin">--</span>
      <input type="number" step="1" min="1" max="120" id="collectorFreezeRunMinInput"
      style="width:70px; display:none;">
    </p>


    <br>

    <p>
      Circ Freeze Temp:
      <span id="circFreezeTempF">--</span>
      <input type="number" step="0.1" id="circFreezeTempFInput"
             style="width:70px; display:none;">
    </p>
    <p>
      Circ Confirm Time:
      <span id="circFreezeConfirmMin">--</span>
      <input type="number" step="1" min="1" max="120" id="circFreezeConfirmMinInput"
       style="width:70px; display:none;">

    </p>
    <p>
      Circ Run Time:
      <span id="circFreezeRunMin">--</span>
      <input type="number" step="1" min="1" max="120" id="circFreezeRunMinInput"
       style="width:70px; display:none;">

    </p>

    <br>

    <p>
      Heat Tape Bad Temp:
      <span id="heatTapeBadF">--</span>
      <input type="number" step="0.1" id="heatTapeBadFInput"
             style="width:70px; display:none;">
    </p>
    <p>
      Heat Tape Clear Temp:
      <span id="heatTapeClearF">--</span>
      <input type="number" step="0.1" id="heatTapeClearFInput"
             style="width:70px; display:none;">
    </p>
    <p>
      Heat Tape Eval Time:
      <span id="heatTapeEvalMin">--</span>
      <input type="number" step="1" min="1" max="120" id="heatTapeEvalMinInput"
       style="width:70px; display:none;">
    </p>

    <br>

    <p>
      Tank Freeze Temp:
      <span id="tankFreezeTempF">--</span>
      <input type="number" step="0.1" id="tankFreezeTempFInput"
             style="width:70px; display:none;">
    </p>
    <p>
      Tank Clear Temp:
      <span id="tankFreezeClearF">--</span>
      <input type="number" step="0.1" id="tankFreezeClearFInput"
             style="width:70px; display:none;">
    </p>
    <p>
      Tank Confirm Time:
      <span id="tankFreezeConfirmMin">--</span>
      <input type="number" step="1" min="1" max="240" id="tankFreezeConfirmMinInput"
       style="width:70px; display:none;">

    </p>

  </div>



  <div id="configButtons">
    <button id="editConfigBtn"   class="blue-button">Edit Config</button>
    <button id="saveConfigBtn"   class="blue-button" style="display:none;">Save</button>
    <button id="cancelConfigBtn" class="blue-button" style="display:none;">Cancel</button>
    <button id="resetConfigBtn"  class="blue-button" style="display:none;">Restore Defaults</button>
  </div>

</td>


      <td valign="top" width="33%%" style="background-color:white" align="center">
    <div style="width:408px; height:400px; overflow:hidden; position:relative;">
  <iframe src="/second-page?ts=%UNIXTIME%"

    style="width:800px; height:800px; border:none; transform:scale(0.5); transform-origin:0 0; position:absolute; top:0; left:0;">
  </iframe>
</div>

</td>



        
        
    </table>

  
    <script>
document.addEventListener('DOMContentLoaded', function () {
    var ws = new WebSocket('ws://' + window.location.hostname + '/ws');

    const pumpStates = Array(11).fill(null);

    // ---- Time config state (timezone + DST) ----
    let timeConfig = {
        timeZoneId: 'US_MOUNTAIN', // default; will be overwritten by TimeConfig: message
        dstEnabled: 1              // 1 = Yes, 0 = No
    };

    // ✅ ENFORCE 1–10 INDEXING (NO PUMP 0)
    for (let i = 1; i <= 10; i++) {
        pumpStates[i] = {
            state: '--',
            mode: 'Auto',
            name: 'Pump ' + i
        };
    }

    ws.onopen = function () {
        console.log('WebSocket connected');
        ws.send('hello:FirstWebpage');
        ws.send('init');
    };

    setInterval(function () {
        if (ws.readyState === WebSocket.OPEN) ws.send('getUptime');
    }, 1000);

    setInterval(function () {
        if (ws.readyState === WebSocket.OPEN) ws.send('ping');
    }, 30000);


    let currentConfig = {};      // holds last config from backend
    let configEditMode = false;

        // Units for display (used when showing values, not when editing)
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

        circFreezeTempF: '°F',
        circFreezeConfirmMin: ' min',
        circFreezeRunMin: ' min',

        heatTapeBadF: '°F',
        heatTapeClearF: '°F',
        heatTapeEvalMin: ' min',

        tankFreezeTempF: '°F',
        tankFreezeClearF: '°F',
        tankFreezeConfirmMin: ' min'
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

        // ---- Freeze Protection ----
        'collectorFreezeTempF',
        'collectorFreezeConfirmMin',
        'collectorFreezeRunMin',

        'circFreezeTempF',
        'circFreezeConfirmMin',
        'circFreezeRunMin',

        'heatTapeBadF',
        'heatTapeClearF',
        'heatTapeEvalMin',

        'tankFreezeTempF',
        'tankFreezeClearF',
        'tankFreezeConfirmMin'
    ];


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
           // We expect the controller to send a fresh "Configuration:..." message
           // which will repopulate currentConfig + spans/inputs.
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
            var items = cfgStr.split(',');

            items.forEach(function (item) {
                var kv = item.split(':');
                if (kv.length < 2) return;
                var key = kv[0].trim();
                var valStr = kv.slice(1).join(':').trim();
                var num = parseFloat(valStr);

                // cache numeric value
                if (!isNaN(num)) {
                    currentConfig[key] = num;
                }

                var span = document.getElementById(key);
                if (span) {
                    var unit = configUnits[key] || '';
                    span.textContent = (valStr === "N/A") ? valStr : (valStr + unit);
                }


                // update edit input (raw numeric)
                var input = document.getElementById(key + 'Input');

                if (input) {
                    input.value = valStr;
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
                } else if (key === 'dstEnabled') {
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
        }

        else if (data.match(/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/)) {
            var p = data.split(' ');
            document.getElementById('currentDate').textContent = p[0];
            document.getElementById('currentTime').textContent = p[1];
        }


        // -------- UPTIME --------
        else if (data.startsWith('Uptime:')) {
            document.getElementById('uptime').textContent =
                data.substring('Uptime:'.length).trim();
        }


        // -------- HEAP --------
        else if (data.startsWith('Heap:')) {
            document.getElementById('heapUsage').textContent =
                data.substring('Heap:'.length).trim();
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
                    state = val.toLowerCase();       // "on" / "off"
                }

                if ((m = key.match(/^pump(\d+)Mode$/))) {
                    pumpIndex = parseInt(m[1], 10);
                    modeRaw = val;                    // "auto" / "on" / "off"
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
            // example: AlarmState:ALARM,count=3
            const payload = data.substring('AlarmState:'.length);
            const parts = payload.split(',');
            const state = parts[0].trim(); // "ALARM" or "OK"
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

    } // ← end handleWebSocketMessage


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
        // We stay in edit mode until we get ConfigReset:OK and new Configuration:...
    });
}



    if (cancelBtn) {
        cancelBtn.addEventListener('click', function () {
            // Restore last-known values from currentConfig
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
        if (tzSpan) {
            tzSpan.textContent = timeZoneLabelFromId(timeConfig.timeZoneId);
        }
        if (dstSpan) {
            dstSpan.textContent = (timeConfig.dstEnabled ? 'Yes' : 'No');
        }
    }

    function syncTimeConfigEditorFromState() {
        var tzSelect  = document.getElementById('timeZoneSelect');
        var dstSelect = document.getElementById('dstEnabledSelect');
        if (tzSelect && timeConfig.timeZoneId) {
            tzSelect.value = timeConfig.timeZoneId;
        }
        if (dstSelect) {
            dstSelect.value = timeConfig.dstEnabled ? '1' : '0';
        }
    }

    function setTimeConfigEditMode(on) {
        var viewDiv   = document.getElementById('timeInfoView');
        var editorDiv = document.getElementById('timeConfigEditor');
        if (viewDiv)   viewDiv.style.display   = on ? 'none'  : 'block';
        if (editorDiv) editorDiv.style.display = on ? 'block' : 'none';

        if (on) {
            syncTimeConfigEditorFromState();
        }
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

            // Optimistically update local state; server will confirm via TimeConfig: message
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

            if (!confirm('Restore factory defaults for Time Configuration values?')) {
                return;
            }

            console.log('Requesting TimeConfig reset to defaults');
            ws.send('resetTimeConfig');
            // We stay in edit mode until TimeConfigReset:OK + new TimeConfig: arrive
        });
    }

    // Ensure something sensible is displayed before first TimeConfig message
    updateTimeConfigView();


    // ==========================================================
    // ✅ PUMP STATUS UI BUILDER (UNIFIED, FIXED)
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

    // expose for debugging if needed
    window.changePumpMode = changePumpMode;

        const alarmBtn = document.getElementById('alarmLogBtn');
if (alarmBtn) {
  alarmBtn.addEventListener('click', () => {
    const w = window.open('/alarm-log', '_blank');
    if (w) w.opener = null;
  });
}


}); // ← end DOMContentLoaded
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