/*
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
    Modified flushCache and TashTemperatureLogging_Run function adding gates on the flush cache
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



// Add freeze protection to collector supply and Circ loop 
// potentially rename all pump logs to match pump function. 
// Verify RTC failure will not block boot
// resolve 07:00:47.898 -> E (20187561) w5500.mac: received frame was truncated


// other future user editable values i might add into the webpage such as DST, Time Zone, IP Address Static/Dynamic, One Wire Buss search/Hex addresses/bit resolution/ for DS18B20 temperature sensors, Rolling average num reading for PT1000 & DS18B20 temperature sensors, enable/disabling of Temperature or Pump Logging, enabling/disabling pumps



*/
