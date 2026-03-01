/* Arduino IDE 2.3.8 settings:
- Flash Model QIO 80Mhz
- Slash Size = 8MB(64Mb)
- Flash Mode = QIO 80MHz
- Partition Scheme = 8M with Spiffs (3MB App/1.5MB SPIFFS)
- PSRAM = OSPI
// 12-16-25 updated ESP Async WebServer from 3.9.2 to 3.9.3

// list logs, read log "filename"
// partition.csv edited to 1.2 mb for each app partition and 
// working : Webserver, pumps On/Off,Auto with simulated temperature T,1,25, NTP>RTC sync. 
// In serial monitor Enter ' T,1,25 ' to increase simulated temperature to 25. with Temperature threshold set to 24.5 this will turn on the pump 1 if it's mode is set to "auto" 
// Enter ' P, 1, 0 ' to set Pump1 Mode to Off, enter P, 1, 1 to set Pump1 Mode to On and enter P, 1, 2 to set Pump1 Mode to Auto.
// transfered RTClib-NTP_Sync4 code to ESPAsyncWebServer46-10Pumps7.6 
// Removed duplicate logging functions, 'logPumpStart(pumpIndex);' and 'logPumpStop(pumpIndex);' and now logPumpEvent function does both when 
// specifying "START" or "STOP".  replaced calls for 'logPumpStart(pumpIndex);' and 'logPumpStop(pumpIndex);' in 'setPumpState' function with
// 'logPumpEvent(pumpIndex, "START");' and 'logPumpEvent(pumpIndex, "STOP");'
// Verified log files for pumps
// update ESPAsyncWebServer46-10Pumps7.6 : RTC is no longer updated if NTP update fails. NTP function "void initNTP()" 
// now updates RTC but not if NTP update fails and logging will continue using the time inside the DS3231 RTC.
// ESPAsyncWebServer46-10Pumps7.7 : updated initNTP and tryNtpUpdate functions so no longer uses blocking code and 
// the rest of the setup code can continue while the time attempts to sync. 
// update ESPAsyncWebServer46-10Pumps7.8 : Code split up and put into siloh's. Logging is not adding timestamp
// update ESPAsyncWebServer46-10Pumps7.9 : Logs now properly display the date and time
// update ESPAsyncWebServer46-10Pumps8.0 : Added three logging functions,
// aggregatePumptoDailyLogs to read PumpX_Log.txt and calculate run times then record runtime total into PumpX_Daily.txt
// and then delete the original start and stop times in the source PumpX_Log.txt file. 
// If a value already exists in the PumpX_Daily.txt file the new value needs to be aggregated with the existing runtime value. but is not happening.
// update ESPAsyncWebServer46-10Pumps8.1 : resolved, existing time values from the same date in PumpX_Daily.txt are now added to the new values before being overwritten.
// update ESPAsyncWebServer46-10Pumps8.2 : resolved, existing time values from the same month in PumpX_Monthly are now added to the new values before being overwritten
// String currentYear = String(getCurrentYear());
// update ESPAsyncWebServer46-10Pumps8.4 : All three logging aggregation functions now work. 
// ESPAsyncWebServer46-10Pumps8.5 : using 'aggAll' from serial input calls performLogAggregation()function which successfully aggregates data from 
// pumpX_Log.txt to pumpX_Daily.txt for the entire array. This is the first function of the three functions inside,
// "aggregatePumptoDailyLogs(i);" "aggregateDailyToMonthlyLogs(i);" "aggregateMonthlyToYearlyLogs(i);" verified each works when called independently. 
// ESPAsyncWebServer46-10Pumps8.6 : added functions checkAndSetFlag(); to set Elapsed_Day flag to 'true' & checkAndPerformAggregation(); so if daily flag is true it will call the 
// performLogAggregation(); and then set the Elapsed_Day flag back to 'false'
// ESPAsyncWebServer46-10Pumps8.7 : time is now sent out every second through websocket. 
// Modified "on event" in the java script portion of WebServerManager.cpp to display time and reformat it to MM-DD-YYYY 00:00:00AM/PM
// ESPAsyncWebServer46-10Pumps8.8 : moved webpage code from WebServerManager.cpp to FirstWebpage.cpp and SecondWebpage.cpp files. second webpage is at /second
// ESPAsyncWebServer46-10Pumps8.9 : modified handleWebSocketMessage function to accept "requestLogData" messages 
// ESPAsyncWebServer46-10Pumps8.9 : added functions for String prepareLogData(int pumpIndex, String timeframe), aggregateDailyLogsReport, aggregateMonthlyLogsReport, aggregateYearlyLogsReport, and aggregateDecadeLogsReport
// ESPAsyncWebServer46-10Pumps9.0 : Modified secondWebpage.cpp so it now shows the pumps and has a drop down selector box as well as a graph. still need to finish linking the functions we built in 8.9
// ESPAsyncWebServer46-10Pumps9.1 : Modified all time functions in the RTCManager.cpp file so they now reference the system variable "CurrentTime" to eliminate duplicate rtc.now(); calls to the RTC.
// ESPAsyncWebServer46-10Pumps9.2 : Fixed automatic log aggregation with just three functions (checkTimeAndAct();, void setElapsed_Day(), void setperformLogAggregation()in the Logging.cpp file using the
// refreshCurrentTime()function being called every second form the RTCManager.cpp file to call checkTimeAndAct();
// ESPAsyncWebServer46-10Pumps9.3 : Modified log function "aggregatePumptoDailyLogs" function in logging.cpp file so runtime from ongoing pump operations before and after midnight will be handled. 
// ESPAsyncWebServer46-10Pumps9.3 : Modified Report functions in WebServerManager for: aggregateDailyLogsReport, aggregateMonthlyLogsReport, aggregateYearlyLogsReport, aggregateDecadeLogsReport
// ESPAsyncWebServer46-10Pumps9.4 : modified the ws.onmessage function in FirstWebpage.cpp file in the script and webpage pump state works again.
// ESPAsyncWebServer46-10Pumps9.5 : Modified initWebSocket()function in WebServerManager.cpp so when a new client connects it trigers the WS_EVT_CONNECT and refreshes the pump state/Mode through ws so new clients display the current pump status
// ESPAsyncWebServer46-10Pumps9.6 : renamed "Decade" to "Total" in log dropdown menu of /second-page, removed TimeSync ticker and added 'checkAndSyncTime()' in TimeSync.cpp, function called every second by 'refreshCurrentTime()' in RTCManager.cpp
// ESPAsyncWebServer46-10Pumps9.7 : CheckAutoMode removed from ticker and now called every second via 'executeEverySecond()' which is called by 'refreshCurrentTime()' in RTCManager.cpp
// ESPAsyncWebServer46-10Pumps9.8 : Changed File System from using SPIFFS to using LittleFS. Modified InitializeFileSystem() function to support LittleFS and added option to Automatically format if LittleFS Mount Fail if '// LittleFSformat();' is uncommented.
// ESPAsyncWebServer46-10Pumps9.9 : rebuilt functions 'aggregateDailyLogsReport, aggregateMonthlyLogsReport, aggregateYearlyLogsReport, aggregateDecadeLogsReport' but decided stratagy was inefficient and abandoned this sketch.
// ESPAsyncWebServer46-10Pumps10.0 : built 'updateAllRuntimes()' function and rebuilt second page functions 'aggregateDailyLogsReport, aggregateMonthlyLogsReport, aggregateYearlyLogsReport, aggregateDecadeLogsReport' to efficiently report pump runtimes.
// ESPAsyncWebServer46-10Pumps10.1 : second webpage reporting of pump runtimes appears to work properly. Discovered the functions aggregateDailyToMonthlyLogs and aggregateMonthlyToYearlyLogs are not working
// after switching to LittleFS. rewrote both of these functions and they are now working properly.
// ESPAsyncWebServer46-10Pumps10.2 : Added ws.send('updateAllRuntimes'): into addEventListener function in secondwebpage.cpp so log data display updates when client connects.
// ESPAsyncWebServer46-10Pumps10.3 : added log file downloads to second webpage
// ESPAsyncWebServer46-10Pumps10.4 : modification to html and java script function in SecondWebpage.cpp. no change visible when using webpage
// ESPAsyncWebServer46-10Pumps10.5 : again modified the same html and java script function in SecondWebpage.cpp and moved the 'Downloads Files' button to below the 'List Files' button
// ESPAsyncWebServer46-10Pumps10.6 : oraganized file declarations
// ESPAsyncWebServer46-10Pumps10.7 : Adding RTOS Tasks which now call all functions
// ESPAsyncWebServer46-10Pumps10.8 : modified tasks to include execution time into the delay time to accurately repeat and then verified stack size is adequate for all repeating tasks
// ESPAsyncWebServer46-10Pumps11.0 : modified calculateTotalLogRuntime function in WebServerManager.cpp file so elapsed runtime for pumps currently operating is adding into calculation for current day.
// ESPAsyncWebServer46-10Pumps11.1 : Modified the second webpage column headers so it will parse the date from the pump1_LOG.txt file and put the date next to the column header "Today", "Current Month" and "Current Year" doubled second webpage task memory
// ESPAsyncWebServer46-10Pumps11.2 : Second Webpage now searches through pump1_Log.txt through pump10_Log.txt looking for the date starting with pump1_Log.txt. 
// ESPAsyncWebServer46-10Pumps11.2 : Modified WS code in SecondWebpage and modified aggregateYearlyLogsReport and aggregateDecadeLogsReport in WebServerManager.cpp to add all elapsed and on going runtimes into "Current Year" and "Total" columns of secondwebpage 
// ESPAsyncWebServer46-10Pumps11.2.1 : increased server memory size from 2048 to 4096 in TaskManager.cpp   
// ESPAsyncWebServer46-10Pumps11.3 : added new functions aggregatePreviousDailyLogsReport, aggregatePreviousMonthlyLogsReport, aggregatePreviousYearlyLogsReport into WebServerManager.CPP and .h files. 
// ESPAsyncWebServer46-10Pumps11.3.1 : not all the column headers in second webpage.cpp pull the date from the log file and compare with the actual date. all are working now except Previous Day. Added log files with data back back 
// to 1960 for yearly and full 12 months of 2024 data along with 1300 start/stop events in the pumpx_Log.txt files and WatchDogTask restarts controller after exceeding 5 seconds since scheduler handoff whenever LittleFS has more than three 
// pumpx_Log.txt files uploaded. Reduced start/stop entries to 500 in eachc log and now wdt does not alert on async_tcp until I add more than 9 of the pumpx_Log.txt files into the file system. Added new task just to update
// the Pump Runtimes so this is no longer automatically loading when connected to second webpage. Task still tripped when manually clicking "Update All" from SecondWebpage from wdt from asnyc_tcp likely waiting for data from websocket. 
// Increased the wdt reset from 5 second to 10 second for the setupNetwork task and now it completes running the Pump Runtime update all task without wdt reset happening. On start up only the Month and year log files are found so the daily
// is not. Also on the Pump Runtimes page the header for the Previous Day is not properly  fiding the date and is showing N/A. Also the columns for previous day and previous month are not populating. Still need to address the previous 
// month data edge case when at the end of the year when decembers pump run time gets deleted after being aggregated into the annual runtime amount.
// ESPAsyncWebServer46-10Pumps11.3.3.1 : secondwebpage column headers now report the date the log under the column header name
// ESPAsyncWebServer46-10Pumps11.3.3.2 : attempting to resolve secondWebpage not reporting values for Yesterday and Last Month colums resulted in significant changes to RTC function and calls adding "currentTime" as an argument into functions vs repeating rtc.now calls 
// ESPAsyncWebServer46-10Pumps11.3.3.2 : Inside WebServerManager.cpp I modified functions "aggregatePreviousMonthlyLogsReport" with a - between YYYY-MM. Also modified like functions due to duplicate counting of current runtime in log.
// ESPAsyncWebServer46-10Pumps11.4 : lowered secondwebpage task memory back to 4096 and lower and it crashes. returned update all command on connection to secondwebpage. Removed debugging statements.  
// ESPAsyncWebServer46-10Pumps11.5 : made function 'processLogsForSecondWebpage' in SecondWebpage that is called by task in TaskManager.cpp file.
// ESPAsyncWebServer46-10Pumps11.6 : Added void turnOnAllPumpsFor10Minutes() and turnPumpsBackToAuto() in PumpManager.cpp and added 'turnOnAllPumpsFor10Minutes()' into TimeSync.cpp function 'checkAndSyncTime()' so at 3am the pumps will come on for 10 minutes validate pump runtime logging
// ESPAsyncWebServer46-10Pumps11.6 : Modified task for SetupNetwork and heavily modified the SetupNetwork() function in NetworkManager.cpp file to aid in network cable disconnect causing wdt reboots.
// ESPAsyncWebServer46-10Pumps11.7 : Added Max31865-PT1000 Code 
// ESPAsyncWebServer46-10Pumps11.8 : added serial print file and serial print task to print temperatures to serial monitor
// ESPAsyncWebServer46-10Pumps11.9 : modified the aggregateMonthlyLogsReport in WebServerManager.cpp file to properly add pumpX_Daily.log time. Also increased WDT value to 20 seconds for startNetwork
// ESPAsyncWebServer46-10Pumps12.0 : removed serial print commands.
// ESPAsyncWebServer46-10Pumps12.1 : Modified WebserverManager.cpp functions for aggregateYearlyLogsReport and aggregateDecadeLogsReport and reported time is now correct on secondwebpage
// ESPAsyncWebServer46-10Pumps12.2 : updated pin declarations and pin map
// ESPAsyncWebServer46-10Pumps12.3 : No significant changes
// AsyncWebServer : replaced simulated temperatures and pump functions with actual temperatures and PumpControl() function from webserver14
// AsyncWebServer001 : updated tasks, removing ds18b2/pt1000 into UpdateTemperatures, added  Task memory counters to serial pint memory usage
// AsyncWebServer002 : updated tasks  Increased setupnetwork task memory, modified broadcastPumpStates and added PrintPumpStates. reworked monitorStacks.
// AsyncWebServer003 : broadcastPumpState no longer directly prints to Serial Monitor. 
 AsyncWebServer004 :
   began troubleshooting error 'E (42955492) w5500.mac: emac_w5500_transmit(727): Free size (75) < send length (0)'
     Added MonitorStacks() function to display task memory usage, added a conditional check to broadcastMessageOverWebSocket() function inside WebServerManager.cpp and then updated functions  
     updated broadcastTemperatures(), broadcastPumpState(),broadcastCurrentTime() so they call the function broadcastMessageOverWebSocket(). broadcastCurrentTime() is now called by RTOS Task 
     instead of directly createing and sending messages to websocket using ws.send. Currently FirstWebpage and SecondWebpage still both directly call ws.send.
AsyncWebServer005 :
    Implemented Mutex to inhibit race conditions for shared values like temperatures and other.
AsyncWebServer006 : 
    got variable temperature values reading in the PumpControl.cpp file as well as SerialPrint.cpp using #define and including TemperatureControl.h No global access to temp variables and Not ideal but works temprarily 
AsyncWebServer007 : 
    properly aligned pump arrays with temperature arrays and Auto Mode now correctly controls the proper pumps. Also the aligned the logging so pump time is again properly logged. turned off Task memory usage monitorstacks and SerialPrint commands.
AsyncWebServer008 : 
    Replaced missing probe. Reverted functions 'void updateAllRuntimes()' and 'void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)' to earlier version and SecondWebpage works again. note WebSocket message send length (0) protections removed
    AsyncWebServer009 : 
    AsyncWebServer010 : 
    AsyncWebServer011 : 
    AsyncWebServer012 : 
    Fixed time and day display on first webpage. Reverted code on second page to version 009 to mitigate wdt reset occurring when conneccting to 2nd webpage after connecting to 1st webpage.
AsyncWebServer013 : 
    Version is now properly displaying version # on First Webpage. Added new function to process log files so WDT will not occure if current pump operations are ongoing. 
AsyncWebServer014 : 
    Reworked many functions related to SecondWebpage and the Pump runtime Reporting. Added duplicate task 'UpdatePumpRuntimes' which is called by handleWebSocketMessage in WebServerManager and calls updateAllRuntimes, logging now done through openLogFile which is added to FileSystemManager. Removed task and assocaited functions 'processLogsForSecondWebpage' 
AsyncWebServer015 : 
    Modified firstwebpage formatting
AsyncWebServer016 : 
    removed hardcoded pump controls (Pump1 - Pump10) on webpage and now they are generated from Pump Names in PumpManager.cpp and dynamically sent via websocket message.
AsyncWebServer017 : 
    Added 'Heating Calls' to FirstWebPage to directly read the status of the heating and DHW pins. int DHW_Heating_Call = digitalRead(DHW_HEATING_PIN); & int Furnace_Heating_Call = digitalRead(FURNACE_HEATING_PIN);
AsyncWebServer018 : 
    Modified functions in TemperatureControl.cpp, WebServerManager.cpp and FirstWebpage.cpp so only different temperature values are sent through websocket.
AsyncWebServer019 : 
    pt1000, pt1000Average, DTemp1-13, DTempAverage1-13 are now displayed on webpage being sent through ws and only if the value changes. 
AsyncWebServer020 : 
    Added System Configuration to webpage
AsyncWebServer021 : 
    Modified webpage formatting 
AsyncWebServer022 : 
    removed 'if (pumpStates[pumpIndex] == PUMP_OFF &&' from DHW and Storage Heating pump control functions and now call for heating (storatge & DHW) will initiate at controller boot up from an existing call for heat (FURNACE_HEATING_PIN = 48 & DHW_HEATING_PIN = 36)
AsyncWebServer023 : 
    readded 'if (pumpStates[pumpIndex] == PUMP_OFF &&' from DHW and Storage Heating pump control functions in PumpManager.cpp and then resolved failure to detect existing heating calls on MCU startup. Then resolved 'Heating Calls' issue so webpage will properly display existing heating calls on webpage. Modified TimeSync.cpp to account to DST
AsyncWebServer024 : 
    moved pinMode '(DHW_HEATING_PIN, INPUT_PULLUP); & pinMode (FURNACE_HEATING_PIN, INPUT_PULLUP);' from setup in initializePumps(); function. Modified aggregatePumptoDailyLogs function so ongoing pump operations at time of aggregation are properly accounted for on both days.
AsyncWebServer025 : 
    significant update to secondwebpage.ccp, Pump Runtime table is no longer created in java and is now created in C++ and just sent to the webpage. Table now references the pump names and no longer pump numbers.
AsyncWebServer026 : 
    Added pump runtimes into first page.
AsyncWebServer027 : 
    increased WDT to 20 seconds for all repeating tasks. diabled Monitorstacks in TaskManager.cpp. reworked updatePumpRuntimes in WebServerManager.cpp so to yield after each aggregator call. This ensures no single aggregator call can block the system for too long.
 AsyncWebServer028 : 
    Version modified for outdoor controller usage with ip address 10.20.90.14 via MAC address change in NetworkManager.cpp file Router uses static IP based on MAC address, outdoor DS18B20 sensors selected in DS18B20.cpp file. due to air leak at top of tank outlet change circ on to 1 and off to -100 so circ will run constantly unless call for heating is present.
 AsyncWebServer029 : 
    updated libraries (AsyncTCP and others) and now compiles successfully on IDE 2.3.6 unfortunately wdt resets on boot with update temperature task. increased WDT to 15 seconds and added delays and esp_task_reset's into log running tasks. Completely disabled wdt using 'esp_task_wdt_delete' in tasks TaskSetupNetwork, TasksetupPumpBroadcasting, TaskbroadcastTemperatures. Increased task stack sizes and these will need to be lowered in the future. everything appears to work properly now, even the webpages. need to rewrite tasks and then reenable wdt and then simulate failures of DS3231 RTC, W5500 Ethernet Adapter, Max31865, PT1000, DS18B20 to ensure graceful failures with Serial Monitor outputs to assist future troubleshooting.
AsyncWebServer030 : 
    Reenabled WDT's on tasks and then modified tasks using esp_task_wdt_delete/add/reset and deployed other clever code so wdt is used. Added CPU core usage code to print on serial Monitor. System is stable if WDT is removed from PumpControl
AsyncWebServer030.1 : 
    Reverted this this version after discovering the global wdt time out code inside the Setup() previously used was incorrect and not changing the wdt reset value from the default of 5 seconds. MCU is stable as long as webpage is not being used and if used w5500 tcpip causes a wdt reset. moved all the tasks back to rtos core selection so they are not longer pinned to a specific core and system is stable. pump runtimes are showing 24/hr/day runtimes. 
AsyncWebServer030.2 : 
    added new function closeAllOpenPumpLogs() to handle stray start events in pumpX_Log.txt files. 
AsyncWebServer031 :  
    Compiles and runs PumpControl still trips 5 second wdt, found bug in wdt code in setup that left wdt reset to default of 5 seconds. increased wdt to 15 seconds and sketch is running with all wdt enabled. reset/crashes occur every couple of days. 
AsyncWebServer032 : 
    rewrite PumpControl function so it no longer calls UpdateTemperatures grabbing temperatureMutex momemtarily to grab global temperature values from temperature arrays to reduce computation load. UpdateTemperatures function is still dominating cpu usage.  
AsyncWebServer033 : 
    replaced missing list files and download selected html code. 
AsyncWebServer034 : 
    Significant rewrite to logging funcion logPumpEvent in Logging.cpp where pump states are now just added to a loging quoue when called by setPumpState in PumpManager.cpp. Now a dedicated task TaskLogger is responsible to write the pump states from the Logging Queue to each individual PumpX_Log.txt file. 
AsyncWebServer035 : 
    Optimized DS18B20 sensor readings adding non blocking code and lowering sensor precision from 12 to 10 bit. Dropping CPU usage from 85 to 78%
AsyncWebServer035 : 
    Optimized PT1000 sensor dropping UpdateTemperatures CPU usage from 78 to 77%
AsyncWebServer036 : 
    Lowered TemperatureUpdate task frequency from 1 to 5 seconds, UpdateTemperature now using 46% of CPU. Modified UpdateTemperatures() so acquisition of temperatures and computations for averages and temperature reading validations logic happens outside the Mutex so time intensive tasks do not block/starve cpu, then Mutex is taken only to update global variables. UpdateTemperature task is now only taking 3% of CPU time.
AsyncWebServer036 : 
    added 'appendTemperature("supplyT", supplyT, prev_supplyT);' to declaration inside UpdateTemperatures.cpp so webpage value would update with the rest of the temperature values. Also corrected hard coded webpage sensor lobles for Circ Loop Supply DTemp4Average and Circ Loop Return to DTemp5Average. 
AsyncWebServer037 : 
    Uptime is now updated every second on the webpage. Controller continued to reset intermittently after10+ hours have passed somtimes. asthetic changes to webpages.
AsyncWebServer038 : 
    On state of FirstWebpage is now highlighted in blue
AsyncWebServer039 : 
    resolved pump state highlight issues on FirstWebpage. 
AsyncWebServer040 : 
    pump mode selector on firstwebpage works again. 
AsyncWebServer041 : 
    added 1000ms delay into startserver task. 
AsyncWebServer041 : 
    highly computational work loads being executed in websockets or websocket call backs are likely responsible for webpage connection failures. Building gueue and webpage data will be propigated from queue.
AsyncWebServer042 : 
    disabled cpu usage task, removed delay and vtaskdelay from callbacks as this causes instability in AsyncTCP/ESPAsyncWebServer
AsyncWebServer043 : 
    added websocket timeout mitigation into handleWebSocketMessage function inside WebServerManager.cpp and into FirstWebpage.cpp file.
AsyncWebServer044 : 
    begun adding temperature logging functions and webpage 
AsyncWebServer045 : 
    Added Temperature logging and Third Webpage files. Temperature logs visibility on the thirdwebpage is broken.
AsyncWebServer046 : 
    Significant code changes in FirstWebpage.cpp, pump name and sensor associations are now in Config.h, ram and file system usage are now displayed on first webpage but Heap numbers are inverted.
AsyncWebServer047 : 
    FirstWebpage.cpp updates, Pump runtimes and pump states now reporting properly
AsyncWebServer048 : 
    TemperatureLogging.cpp is close but not perfect. Avergaing function for temperature sensors is not removing -196.6
AsyncWebServer049 : 
    Some of the TemperatureLogging is working. 
AsyncWebServer050 : 
    WDT resets during cache flush, updated TemperatureLogging.cpp. Graph Fixed.
AsyncWebServer051 : 
    Temperature logs reading Graph improvements
AsyncWebServer052 : 
    Temperature logs reading Graph improvements, Reset Graph button working
AsyncWebServer053 : 
    Temperature logs reading Graph improvements, crosshair + tooltips
AsyncWebServer054 : 
    Temperature logs reading Graph improvements, symbol replacements for Degrees Min Max tooltips
AsyncWebServer055 : 
    Modified conditions for adding Temp Values to Cache
AsyncWebServer056 : 
    Modified Heap usage and IFrame window on FirstWebpage. Flash compile size increased above 1200000 so had to increase flash size to 8MB and changed the partition to 8MB (3MB App, 1.5MB SPIFFS). Adjusted Iframe and Div size of FirstWebpage Temperature Logs section to remove scroll bars. Also  Inside HandleWebsocket() I commented out the Serial.print ping.
AsyncWebServer057 : 
    Temp logging updates. possible Temp logging attempting to load before LittleFS mounted is causing core panics at boot time. Updated Temp Logging functions with global switches so it will not start until after file system is mounted and NTE is sync'd. Unfortunately this means without a NTP sync there is no temp logging. system utilizes the RTC DS3231 and Temp logging functions were not built to use. 
AsyncWebServer058 : 
    finished updating Temp Logging functions to use RTC Time. getCurrentTimeAttomic(). added enforceTemperatureLogDiskLimit(); into TaskTemperatureLogging_Run function to be called after hourly cache flush and at boot after LittleFS mount. Flash memory usage set to 95% and completely rewrote enforceTemperatureLogDiskLimit() function so only the oldest month directory inside the oldest year directory along with it's contents will be deleted and not the oldest year directory along with it's contents.
AsyncWebServer059 : 
    added date range 2024-2099 into void TaskTemperatureLogging_Run() to validate RTC time when NTP fails before allowing Temperature Logging. 
AsyncWebServer060 : 
    Discovered temp log files time is 7 hours behind. replaced all references to system time in Temp Cacheing / Logging with RTC calls and verified proper operation.
AsyncWebServer061 : 
    Temp Sensor 13 now showing on ThirdWebpage graph
AsyncWebServer062 : 
    Fixed broken handleWebSocketMessage(data) function on FirstWebpage.cpp so System Configuration values are again displayed on webpage.
AsyncWebServer063 : 
    Added user editable functionality to update system configuration through webpage with values that are persistent after reboot. 
AsyncWebServer064 : 
    Updated PumpManager.cpp replacing static Macro names panelTminimum > g_config.panelTminimumValue, PanelOnDifferential > g_config.panelOnDifferential, panelLowDifferential > g_config.panelOffDifferential, Boiler_Circ_On > g_config.boilerCircOn, Boiler_Circ_Off > g_config.boilerCircOff, Heat_Tape_On > g_config.heatTapeOn, Heat_Tape_Off > g"_config.heatTapeOff. system compiled and I'm not really sur why becaluse I did not fix the compile errors, perhaps something with the 
AsyncWebServer065 : 
    Renamed the 11 system configuration values in Config.h from using #define "#define PanelOnDifferential" to using inline constexpr"inline constexpr float DEFAULT_PanelOnDifferential" and adjusted initSystemConfigDefaults() function in Config.cpp to use the new names "inline constexpr float DEFAULT_PanelOnDifferential" Everything works!
AsyncWebServer066 : 
    added Restore Defaults functionality into System Configuration edit menu on First Webpage.
AsyncWebServer067 : 
    significant refactor adding user selectable Time Zones / DST and storing values to non volitile memory as single source of truth for entire system right from the NTP Sync.
AsyncWebServer068 : 
    complete rewrite of temp log file system clean up functions and with dedicated rtos task called at boot and after Temperature Logging cache flush every 60 minutes. fileSystemMutex contention mitigation resolved with a new function "takeFileSystemMutexWithRetry" Replaced xSemaphoreTake with "takeFileSystemMutexWithRetry" in functions: loadSystemConfigFromFS(),  
AsyncWebServer069 : 
    Updated every function system wide to use the new takeFileSystemMutexWithRetry function for stability.
AsyncWebServer070 : 
    updated checkTimeAndAct() add if true condition -seconds. For task FileSystemCleanup it is now called on the 30 minute make via TaskcheckTimeAndAct task. 
AsyncWebServer071 : 
    Moved pump logs from root into /Pump_Logs/ Updated SecondWebpage.cpp, Logging.cpp, Webservermanager.cpp and Tasklogger to target new location. Moved all existing logs inot /Pump_Logs/
AsyncWebServer072 : 
    continued updating pump log functions in SecondWebpage/cpp and WebserverManager.cpp. pump logs now in /Pump_Logs and working.
01-04-26 Updated Async TCP library from 3.4.9 to 3.4.10 
01-04-26 Updated ESP Async WebServer Library from 3.9.3 to 3.9.4
hopefully Websocket Disconnects are resolved, nope! 
AsyncWebServer073 : 
    Modified flushCache and TaskTemperatureLogging_Run function adding gates on the flush cache
AsyncWebServer074 : 
    Added ESP32-targz library and enabled compressed directory downloads from third webpage.Fuck miniz library, not compatable.
AsyncWebServer075 : 
    for downloading large directories from webpage I enabled additional 8MB of PSRAM. Enabling PSRAM requires GPIO pins 35,36,37 to not be in use. moved pump 8 Recirculation Valve from pin 35 to pin 8 and moved call for DHW_HEATING_PIN from pin 36 to pin 3. When downloding folders from webpage necessitates the usage of targz to compress into a single file to not be blocked by browser. The file system must have enough free space for the compressed.tar.gz file to be created before it can be sent to web client. 
AsyncWebServer076 : 
    discovered temp.tar.gz files created when compressing directories for download to web clients were being left on the file system even after successfully downloading to the web client. created delete task to run 5 seconds after sending file to webclient.
AsyncWebServer077 : 
    Significant rewrite/addition of the functions for downloading directories in a  .tar archive from the ThirdWebpage. Directory_Name.tar is no longer saved to either the LittleFS or the PSRAM memory and now uses on-the-go streaming from a ring buffer and compression calls a RTOS task producer task to handle compression. Also modified the file system mutex usage so only taken when actually needed. if get canary trips increase TGZ_PRODUCER_TASK_STACK_BYTES size currently at 12288 to 16384 or more needed. Also compressing larger directories will require increasing the TGZ_RING_BYTES size currentely at 256KB.
AsyncWebServer078 : 
    added tgzProducer task into MonitorStacks() to verify adequate stack size for downloading .tar compressed directories. Updated monitorStacks function to properly display stack size memory usage.
AsyncWebServer079 : 
    tgzProducer task completing to quick to be captured by monitorStacks. tgzProducer stack usage now cached and picked up by monitorStacks also taskmonitorStacks is no longer pined to core 1.
AsyncWebServer080 : 
    Implemented syste wide Alarm State and added many conditions and will add more in the future. Alarm file is viewable from frist webpage.
AsyncWebServer081 : 
    Alarm Log when opened from FirstWebpage is no longer crashing or opening in a new tab. Made changes to the functions handleWebsocketmessage and handleWebsocketconnection and new web clients are now identified in the Serial Monitor. SecondWebpage uses WebSocket to get pump runtime data while ThirdWebpage uses json fetch(). 
AsyncWebServer082 : 
    Code Cleanup. removed developement /testing calls from Setup() and created EndofBootup Task. Reordered RTOS Tasks to resolve startup errors.
AsyncWebServer083 : 
    Refined layout of the FirstWebpage
AsyncWebServer084 : 
    SecondWebpage.cpp now uses fetch rather than WebSocket. in comparing pump runtimes in table noticed Total does not include todays runtimes. 
AsyncWebServer085 : 
    Removed ws.textAll from updateAllRuntimes() and removed the no longer needed WebSocket message handler requestLogData,updateAllRuntimes,setPumpMode:, setAllPumps:auto.
AsyncWebServer086 : 
    small cosmedic upgrade to the second webpage Update All button
AsyncWebServer087 : 
    Large update to system Alarm, adding files AlarmWebpage.h, AlarmWebpage.cpp, HealthCheck.cpp, AlarmHistory.cpp, AlarmHistory.h and made substantial changes to how we call alarms in AlarmManager.cpp. 
    Added entire Alarm Log with persistent history living in LittleFs file system. There are now two seperate alarms with the first being storred in RAM and is for a failure that is currently ongoing "RAM: and the second Alarm provides a history of events that previously happened and is storred in LittleFS.
1- AlarmManager = live brain (Ram):
AlarmManager keeps active states + a small recent event ring (fast UI, no FS dependency)." State alarms (persistent condition): AlarmManager_set(...) / AlarmManager_clear(...), 

2- AlarmWebpage/Storage = long-term memory (LittleFS): 
The Alarm “history log” persists (what users manage/delete/group on the Alarm page)." Event-only log entries (something happened, but not a persistent “active” fault): AlarmManager_event(...). 

AsyncWebServer088 : 
    Added RTC into Alarm system and resolved boot loop if RTC missing. Fixed many compile errors and successfully compiles now. Added Freeze Protection for Collector Supply pipe if temp >=33º lead/lag on for 10 minutes. Addedd freeze protection for Circ supply/Return if temp >=33º for 10 minutes circ pump runs for 10 minutes. Added Tank Freeze Protection if tank temp >=33º for 10 minutes circ pump on until tank temp <34º. 
AsyncWebServer089 : Large rewrite of Alarm functions because chatgpt is schitzophrenic. reworked all the functions again.
AsyncWebServer090 : working on AlarmWebpage. not finished yet
AsyncWebServer091 : Alarm Log is working, still need to rename details field.
AsyncWebServer092 : Alarm Log now shows Details and groups properly
AsyncWebServer093 : Fixed several stability issues and continued Alarm Log work
AsyncWebServer094 : Webpage layout changes.
AsyncWebServer095 : Webpage layout changes I frames now scaled using js to webpage width.
AsyncWebServer096 : Multiple Temp Sensors can now be selected for Freeze protection.
AsyncWebServer097 : adding Pump Runtimes Iframe scaling to FirstWebpage
AsyncWebServer098 : Pump Runtimes now scales properly on FirstWebpage
AsyncWebServer099 : Adding Temperature Logs Iframe scaling to FirstWebpage
AsyncWebServer099 : Temperature log saling on First Webpage
AsyncWebServer100 : Iframe window now restores browser to original dimentions
AsyncWebServer101 : Modified size Temperature Logs Iframe, auto loads graph
AsyncWebServer102 : Added to FirstWebpage Heap updates every second, File System/ Flash Storage every 30 seconds. Fixed TGZ erros and moved to FileSystemManager.cpp file.
AsyncWebServer103 : High Heap usage, Updated FirstWebpage & HealthCheck so Heap and PSRAM reporting are Separated. Added MemoryStats.cpp/.h. runHourlyHealthCheck(); now runs every at 20 minute mark to report mem usage.
    ESP Async WebServer v3.9.4 > 3.9.5
    ESP32-targz V1.2.9 > 1.3.0
    esp322 v3.3.5 > 3.3.6
AsyncWebServer104 : Memory reduced TaskSetupNetwork 8192>4096
                    TaskLoadSystemConfigFromFS 8192>4096. Added more specific reporting into Alarm Log for memory over-utilization errors. 
                    Implemented new mutexes into everything that access LittleFS in the SecondWebpage, 
                    ThirdWebpage and WebServerManager.cpp to combat race conditions. 
AsyncWebServer105 : Crashing has been present since loading Temp Logs and Pump Runtimes automatically on FirstWebpage load. deployed new mutex system wide to protect everything that touches the LittleFs. Crashing present after compressed file downloading.
AsyncWebServer106 : Rebuilt targz functions and relocated task and code to FileSystemManager.cpp. added "vTaskDelete(NULL);" to many of the setup tasks that are not reoccurring free memeory 90% > 69% now. Further reduction is possible by eliminating additional task that are running. currently checkTimeAndAct() is reoccurring and calling these tasks. 1 - 11:59PM Set Log Aggregation Flag. 2- perform "setperformLogAggregation();" at 12:00AM. 4- Sets a flag for TaskFileSystemCleanup enable however proper rework could call this directly and allow for eliminating another running tash. 5- calls runHourlyHealthCheck hourly at xx:20. Further consolidation of functions like checkTimeAndSync, temperature logging, logzeroLengthMessages  
AsyncWebServer107 : Continued troubleshooting heap memory usage from targz. with caching buffer happening on PSRAM system mem (heap) still consumes 39KB of heap memory. 
AsyncWebServer108 : Created TarGZ.cpp and TarGZ.h and moved targz code from thirdwebpage to TarGZ.cpp. Implemented switch in Config.h for Ring Buffer Location (Internal Heap / PSRAM)
AsyncWebServer109 : Modified function checkTimeAndAct function makeing it more of a scheduler which calls time specific periodicly running tasks & functions for setperformLogAggregation, maybeRunHourlyHealthCheck, setenforceTemperatureLogLimit and discontinued using TaskFileSystemCleanup task. Added 2 second window activation time and single runt "guards" into funcitons runHourlyHealthCheck, setperformLogAggregation, maybeRunHealthCheckHourly. Lowered Task priority for PumpControl & UpdateTemperature 5 → 4    
AsyncWebServer110 : created DiagLog.h and Started deploying a new global Serial Monitor output toggle
AsyncWebServer111 : Created File DiagCoonfig.h and replaced previous Serial Monitor output so now there is a single global switch "dev default" or "field default" masks. Dev enables Serial Monitor outputs, Field means deployed and not connected to a computer therefore Serial Monitor outputs are disabled. Disabled is default. This can now be integrated with FirstWebpage. If Dev is enabled categories are now present to enable/disable serial monitor outputs for DBG_MEM       = 1u << 0,  // heap/psram/stack snapshots
                    DBG_FS        = 1u << 1,  // filesystem cleanup, file ops // Transitioned to new Serial Monitor Toggle 
                    DBG_PUMPLOG   = 1u << 2,  // pump runtime log writing/aggregation // Transitioned to new Serial Monitor Toggle
                    DBG_TEMPLOG   = 1u << 3,  // temperature logging cache/flush // Transitioned to new Serial Monitor Toggle
                    DBG_ALARMLOG  = 1u << 4,  // alarm history / ndjson
                    DBG_1WIRE     = 1u << 5,  // DS18B20 / OneWire discovery/read // Transitioned to new Serial Monitor Toggle
                    DBG_RTD       = 1u << 6,  // MAX31865 / PT1000 path // Transitioned to new Serial Monitor Toggle
                    DBG_NET       = 1u << 7,  // W5500/WiFi/NTP/DNS/etc. // Transitioned to new Serial Monitor Toggle
                    DBG_PUMP      = 1u << 8,  // pump control decisions, mode changes. // Transitioned to new Serial Monitor Toggle
                    DBG_RTC       = 1u << 9,  // DS3231 / time validity / drift. // Transitioned to new Serial Monitor Toggle
                    DBG_TARGZ     = 1u << 10, // tar.gz streaming + ringbuf
                    DBG_TIMESYNC  = 1u << 11, // NTP/time sync events
                    DBG_WEB       = 1u << 12, // WebSocket, HTTP handlers
                    DBG_TASK      = 1u << 13, // task create/stack/wdt/traces
                    DBG_SENSOR    = 1u << 14, // generic sensor pipeline
                    DBG_CONFIG    = 1u << 15, // config load/save parsing
                    DBG_PERF      = 1u << 16, // timing, long loop warnings

                    // Convenience masks
                    DBG_ALL       = 0xFFFFFFFFu 
AsyncWebServer112 : all files updated to use the new serial monitor categories except WebServerManager.cpp.
AsyncWebServer113 : Finished deploying new Serial Monitor Categories and global toggle. 
AsyncWebServer114 : Significant rewrite of the NetworkManager.cpp file and now we detect the errors "received frame was truncated" "invalid frame length""unexpected error" and intercept the "E (3047838) w5500.mac: received frame was truncated" storem that results WDT carary reboots. Still receiving "E (3047838) w5500.mac: received frame was truncated" errors at this time but mcu is no longer crashing.
AsyncWebServer115 : Modified WS; events combining HEAP and PSRAM into a single message at a 5 second interval and only if the data has changed since the last message. Also merged the Uptime and Time WS messages together into a single WS message.
AsyncWebServer116 : W5500 truncated frame errors still happening. Highly possible client Backpressure / queue buildup durring broadcast events causing truncated frame errors from multiple simulteneous ws broadcasts occurring at the same time. Heavily modified broadcastMessagesOverWebsocket and many tasks like TaskbroadcastTemperatures as well as TaskSystemStatsBroadcaster in WebServerManager.cpp. Replaced vTaskDelay(pdMS_TO_TICKS(1000)); inside many periodic tasks with vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000)); to increase stability. 
AsyncWebServer117 : Continued troubleshooting truncate frame errors. Modified FirstWebpage so it auto reconnects. Crash happended after 16 hours of runtime traced back to stack overflow form snprintf usage inside "static String formatUsage" inside MemoryStats.cpp. updated function to use "string" class which allocates stack memory.  
AsyncWebServer118 : Continued troubleshooting truncate frame errors. Added in 'saveDiagSerialConfigToFS()' function and in future after adding into webpage debugging masks (serial prints) can we toggled. Added in crash detector to enable DBG_ALL after a crash is detected to enable all serial print messages.  
AsyncWebServer119 : small changes made to tasks and task order to finish implementing the Debug serial prints, FS loads first before user configured system perameters using json files (TaskLoadSystemConfigFromFS, TaskInitTimeConfigDefaults, TaskInitSystemConfigDefaults)
AsyncWebServer120 : Replaced network recovery task so no ticker/event paths and now uses notify only. During recovery task no longer called ETH.end() unless it already exists. Also no blidn ISR uninstall. Network Recovery loop no longer crashes but after a WebSocket client disconnects it is not reconnecting.  
AsyncWebServer121 : continued troubleshooting WebSocket client disconnect errors rewired ESP and Preferphrils to use external power supply to rule out vdroop and poor ground as cause. adding electylidic capacitor tomorrow.  Replaced all functions that directly called WebSocket textAll with a system scheduler (TaskWebSocketTranmitter) that reads flags and then prints the messaages to ws if possible and prioritizes Pump state changes higher take priority.  
AsyncWebServer122 : Still troubleshooting Ethernet disconnects. Adjusted SPI frequency from 2 to 60mhz which made not different. Added 220 ohm resistor in series with SPI CS at ESP32 and added 470uf electrolytic capacitor into 3.3v + - wires at W5500 but made no difference. Commented out checkStacks90(); and MemoryStats_printSnapshot("HC_0020"); in runHourlyHealthCheck(); because these pause interrupts breifly and may cause the W5500 to not respond in time to an inbound packet from the router and therefore may cause a disconnect. This made no difference to the Network disconnect inverals. Two core panic's occurred tonight while runHourlyHealthCheck(); was running. Also attempted to download the Temperature_Logs directly and the first 2 attempts resulted in core panics but the third attempt was successful.
 calling TaskUpdateTemperatures executes a read sensor for the onewire sensors which pauses everything else on that core breifly to provide very accurate time reading and is cabable of causing the W5500 to miss the respond widown on an inbound packet from the router resulting in an ethernet disconnect. increased TaskUpdateTemperatures frequency from 2 seconds to 5 seconds and this enabled 21 minute intervals between ethernet disconnects. 
 Executed core pinning moving Netowrk and Server to core 0 and FS operations and Onewire Sensor reads to Core 1 but this only made it 15 minutes until a network disconnect occurred. 
increased vataskdelay intervals for TaskTemperatureLogging 1->5 seconds, TasksetupPumpBroadcasting 1->5 seconds, TaskBroadcastTemperatures 1->5 seconds as a test.
 Also we have recently added many FS related functions and accessing the FS also causes small pauses to the interrupts and could be responsible for the W5500 Disconnects. I suspect the W5500 might also be starving for computational resources, perhaps will try core pinning W5500. SPI bus left at 20MHZ. For some reason the serial print messages are not being intercepted and it is again causing core panic after <one serial print every second. Added new interceptor code to look the character length of the last4 messages and not print a duplicate message. 
AsyncWebServer123 : It appears the previous changes helped but mcu is still crashing twice a day. Network recovery task possibly causing reboots "E (41135260) gpio: gpio_isr_handler_remove(572): GPIO isr service is not installed, call gpio_install_isr_service() first" "CORRUPT HEAP: Bad tail at 0x3fcdba84. Expected 0xbaad5678 got 0xbaad5602" "assert failed: multi_heap_free multi_heap_poisoning.c:279 (head != NULL)" "Backtrace: 0x4037eb29:0x3fcce510 0x4037eaf1:0x3fcce530 0x403859aa:0x3fcce550 0x403845c7:0x3fcce690 0x40376aaf:0x3fcce6b0 0x40385a49:0x3fcce6d0 0x420e6cb9:0x3fcce6f0 0x420e6d5e:0x3fcce710 0x420e75e9:0x3fcce750 0x420e7706:0x3fcce790 0x420e6c0f:0x3fcce7c0 0x4202b7f6:0x3fcce7e0 0x4202a098:0x3fcce800 0x4202a14b:0x3fcce820 0x42032667:0x3fcce840 0x42030c49:0x3fcce860 0x4200d17d:0x3fcce8a0 0x4200d4ec:0x3fcce8c0 0x4037f9e5:0x3fcce8e0"
Removed network recovery code from NetworkManager.cpp file. 
Updated ESP board manager package 3.3.6 to 3.3.7


// sprintf is used for formatting outputs and can cause a stack overflow from writing outside it's alloted memory buffer. replace this function with string which uses heap instead of stack memory. Never use sprintf.
// future remove TasksetupPumpBroadcasting() task and call setupPumpBroadcasting(); as part of updateTemperatures() function.
// future add format littleFS option to webpage
// future to eliminate running tasks move more intermitent runing functions into the checkTimeAndAct function. 
// Link task handles and task names in HealthCheck.cpp to actual declared handles/names in TaskManager.cpp. Also Producer task is different and does not use a fixed block size so I left it out for now. 
// potentially rename all pump logs to match pump function. 
// Verify RTC failure will not block boot
// resolve 07:00:47.898 -> E (20187561) w5500.mac: received frame was truncated

// Ready to be added to Webpage. Add Serial Print toggle for Developement / Field Deployment along with categories, TarGZ.

// other future user editable values i might add into the webpage such as DST, Time Zone, 
IP Address Static/Dynamic, One Wire Buss search/Hex addresses/bit resolution/ for DS18B20 temperature sensors, 
Rolling average num reading for PT1000 & DS18B20 temperature sensors, enable/disabling of Temperature or Pump Logging, 
enabling/disabling pumps, Add toggle for Developement / Field Deployment along with categories for Serial Print, TarGZ



Message to chatgpt on 02-15-26

We have unsuccessfully made many changes some helpful and some not helpful over the last two weeks of attempting to resolve the ws client disconnects and the W5500 truncated frame errors that cause wdt reboots caused by truncated frame messages going out every millisecond. In a previous analysis you chatgpt said the disconnects were related to the W5500 truncated frame errors and we began troubleshooting. You said you believed websocket "pressure" was responsible for the disconnects due to multiple ws messages being generated from different sources. Because of this we set out to reduce the ws message quantities as well as the possible size of the ws messages and the first thing we did was to remove huge ws messages from the Pump Runtime table on the secondwebpage and temperature values from the ThirdWebpage. The Pump Runtime table sent a huge ws message that was tens of times larger than all other ws messages combined and made zero difference. Truthfully I didn't believe this had anything to do with the WS client disconnects because it would have caused client disconnect at the time of the messages which did not happen due to the table only loading on the initial webpage connection or when a user clicked the Update All button which NEVER happened and this made no difference and the ws client disconnects continued to happen. We also removed the Temperature Sensor ws messages from the ThirdWebpage which I also believed like the pump runtime data had nothing to do with the ws disconnects due to this also only ever transmitting a ws message when the webpage was initially loaded or a webpage user selected a temperature sensor to view the data and I've never had a single client disconnect when interacting with the temperature log graph and this made no difference and the ws client disconnects continued. We continued trying to lower ws traffic by combining ws messages that were sent out every second from the heap and the psram ws messages into a single message that is sent out every 5 seconds rather than every second and even then will only be sent out if the memory usage has changed and this made no difference. Next we combined the date time ws message and the Uptime ws messages together and also gave the FirstWebpage the ability to keep time even when the ws messages go interrupted and not only did this not reduce the ws client disconnects but it also had the unintended consequence of making it difficult to easily determine if the ws connection had dropped because I could no longer just look at the Time value on the webpage. Because reboots were caused by serial print storms we started attacking the serial print storm and managed to eliminate the serial print storm and at some point during all these changes we managed to silence all the Serial Monitor outputs which has made additional troubleshooting difficult due to the inability to determine if a disconnect has happened. During the process of eliminating the Serial Print "Storm" we implemented a new global toggle which allows individual serial print messages categories to be enabled or disabled when in developer mode or to have all serial print messages turned off when the controller is installed in the field and not connected to a computer. We also made changes to the sendPumpStatuses function which is called when a pump state (on / off) changes so the messages only go out when pump state changes occur rather than every second as was previously done. Now that we had stopped the mcu from WDT reboots we pivoted to hardening the system rewriting much of the NetworkManager.cpp file so the W5500 will reset using the rst pin on the W5500 and reconnect when a ws client disconnect occurs. Although we had not resolved the root cause of the WebSocket connection problems in fact the ws disconnects have never been more prevalent at any point in the last 9 months since migrating from the Windows 10 based Arduino IDE v1.8x which used an earlier of Espressif's IDF to developing on MacOS using Arduino IDE v2.3.7 which uses Espressif IDF v5. This change necessitated migrating the W5500 Ethernet library from <WebServer_ESP32_SC_W5500.h>  to the integrated ETH.h and Network.h libraries and has never been stable with crashes happening from days to weeks. Ultimately we have made thousands of changes to dozens of functions adding queues, fail state logic, ws message volume reduction, ws message data size reduction, RTOS task priority, task interval changes ect ect ect the WebSocket connection drops more than once a minute and after 30 seconds (likely our retry loop) reconnects and I cannot even tell if the truncated frame error is happening or anything else because apparently every Serial Print Message is now disabled. 


*/
