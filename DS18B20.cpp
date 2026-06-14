// DS18B20.cpp 
#include "DS18B20.h"
#include "Config.h"
#include "DiagLog.h"
#include "AlarmManager.h"
#include <ArduinoJson.h>

OneWire32 sensors1(ONE_WIRE_BUS_1);
OneWire32 sensors2(ONE_WIRE_BUS_2);

float DTemp[NUM_SENSORS] = {0};
float DTempAverage[NUM_SENSORS] = {0.0};
uint32_t DTempLastGoodReadMs[NUM_SENSORS] = {0};

float sensorOffsets[NUM_SENSORS] = {
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

const int numReadings = 3;
float DTempValues[NUM_SENSORS][numReadings] = {0};
int readingsIndex[NUM_SENSORS] = {0};

SensorMapping sensorMappings[NUM_SENSORS];  // runtime mapping, indexed by logical sensor index

static bool sensorPresent[NUM_SENSORS] = {false};
static bool sensorOnBus1[NUM_SENSORS] = {false};
static bool sensorObserved[NUM_SENSORS] = {false};
static uint8_t sensorObservedBus[NUM_SENSORS] = {0};
static bool sensorFaultLatched[NUM_SENSORS] = {false};

static volatile bool g_ds18b20Ready = false;

static bool assignmentMatchesBus(uint8_t slot, bool wantBus1) {
    if (slot >= NUM_SENSORS) return false;
    const Ds18B20SensorAssignment& a = g_ds18b20Config.assignments[slot];
    if (!a.enabled) return false;
    return (a.bus == (wantBus1 ? 1 : 2));
}

static bool isAssignmentEnabled(uint8_t slot) {
    if (slot >= NUM_SENSORS) return false;
    return g_ds18b20Config.assignments[slot].enabled &&
           g_ds18b20Config.assignments[slot].rom != 0ULL;
}

static constexpr uint32_t DS18_BOOT_SETTLE_MS      = 1200;
static constexpr uint8_t  DS18_BOOT_SCAN_RETRIES   = 5;
static constexpr uint32_t DS18_BOOT_RETRY_GAP_MS   = 250;
static constexpr uint32_t DS18_MISSING_RESCAN_MS   = 60000;

static uint32_t s_lastMissingRescanMs = 0;

static bool isLikelyValidDs18Rom(uint64_t rom) {
    // DS18B20 family code is 0x28 in the low byte of the 64-bit ROM value
    // as printed by this sketch.
    if (rom == 0ULL) return false;
    if ((rom & 0xFFULL) != 0x28ULL) return false;
    return true;
}

static bool anySensorsMissingOnBus(bool wantBus1) {
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        if (assignmentMatchesBus(i, wantBus1) && !sensorPresent[i]) {
            return true;
        }
    }
    return false;
}

static void logMissingSensorsOnBus(bool wantBus1, bool logFoundDevices, bool emitMissingAlarms) {
    for (uint8_t slot = 0; slot < NUM_SENSORS; slot++) {
        const Ds18B20SensorAssignment& expected = g_ds18b20Config.assignments[slot];

        if (!assignmentMatchesBus(slot, wantBus1)) continue;

        if (!sensorPresent[slot] && expected.rom != 0ULL) {
            int nameIndex = slot + 2; // DTemp1 starts at SENSOR_NAMES[2]

            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "DS18B20 Sensor Missing on Bus %d: address 0x%llxULL, SYSTEM_TEMP=%s, SOURCE_MAP=%d, SENSOR_NAME=%s\n",
                        wantBus1 ? 1 : 2,
                        expected.rom,
                        expected.systemTemp,
                        SOURCE_MAP[nameIndex],
                        SENSOR_NAMES[nameIndex]);
            }

            if (emitMissingAlarms) {
                AlarmManager_event(ALM_SENSOR_FAULT, ALM_WARN,
                    "DS18B20 Missing: %s (Map:%d)",
                    SENSOR_NAMES[nameIndex],
                    SOURCE_MAP[nameIndex]);
            }
        }
    }
}

static void scanAndAddMissingSensorsOnBus(bool wantBus1, bool logFoundDevices, bool emitMissingAlarms) {
    uint64_t foundAddrs[20];
    uint8_t count = wantBus1 ? sensors1.search(foundAddrs, 20)
                             : sensors2.search(foundAddrs, 20);

    if (logFoundDevices) {
        LOG_CAT(DBG_1WIRE, "[DS18B20] Bus %d found %d devices\n", wantBus1 ? 1 : 2, count);
        for (uint8_t i = 0; i < count; i++) {
            LOG_CAT(DBG_1WIRE, "  Bus%d[%d] = 0x%llxULL\n",
                    wantBus1 ? 1 : 2, i, foundAddrs[i]);
        }
    }

    // Track unique valid ROMs returned by THIS search pass only.
    uint64_t acceptedThisPass[20];
    uint8_t acceptedCount = 0;

    // ADDITIVE ONLY:
    // never clear an already-found sensor from the runtime map here.
    // only add missing sensors when they are successfully found.
    for (uint8_t i = 0; i < count; i++) {
        uint64_t rom = foundAddrs[i];

        // Reject obviously bad / noisy ROM values first.
        if (!isLikelyValidDs18Rom(rom)) {
            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "[DS18B20] Ignoring invalid ROM on Bus %d: 0x%llxULL\n",
                        wantBus1 ? 1 : 2,
                        rom);
            }
            continue;
        }

        // Reject duplicates returned by the same search pass.
        bool duplicateThisPass = false;
        for (uint8_t j = 0; j < acceptedCount; j++) {
            if (acceptedThisPass[j] == rom) {
                duplicateThisPass = true;
                break;
            }
        }
        if (duplicateThisPass) {
            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "[DS18B20] Ignoring duplicate ROM on Bus %d: 0x%llxULL\n",
                        wantBus1 ? 1 : 2,
                        rom);
            }
            continue;
        }
        acceptedThisPass[acceptedCount++] = rom;

        int logicalIndex = findDs18B20AssignmentByRom(rom);

        if (logicalIndex < 0 || logicalIndex >= NUM_SENSORS) {
            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "[DS18B20] Unexpected valid sensor found on Bus %d: 0x%llxULL\n",
                        wantBus1 ? 1 : 2,
                        rom);
            }
            continue;
        }

        const Ds18B20SensorAssignment& expected = g_ds18b20Config.assignments[logicalIndex];

        // Record where the configured ROM was actually observed even if it
        // is on the wrong bus. This lets the web UI show a clear wrong-bus
        // condition instead of only saying the sensor is missing.
        sensorObserved[logicalIndex] = true;
        sensorObservedBus[logicalIndex] = wantBus1 ? 1 : 2;

        if (expected.bus != (wantBus1 ? 1 : 2)) {
            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "[DS18B20] Sensor 0x%llxULL (%s) belongs on Bus %d but was found on Bus %d\n",
                        rom,
                        expected.systemTemp,
                        expected.bus,
                        wantBus1 ? 1 : 2);
            }
            continue;
        }

        // If already found earlier, leave it alone.
        if (!sensorPresent[logicalIndex]) {
            sensorMappings[logicalIndex] = {rom, logicalIndex};
            sensorPresent[logicalIndex] = true;
            sensorOnBus1[logicalIndex] = wantBus1;

            if (logFoundDevices) {
                int nameIndex = logicalIndex + 2;
                LOG_CAT(DBG_1WIRE,
                        "[DS18B20] Added missing sensor on Bus %d: logical=%d SYSTEM_TEMP=%s SOURCE_MAP=%d SENSOR_NAME=%s\n",
                        wantBus1 ? 1 : 2,
                        logicalIndex,
                        expected.systemTemp,
                        SOURCE_MAP[nameIndex],
                        SENSOR_NAMES[nameIndex]);
            }
        }
    }

    logMissingSensorsOnBus(wantBus1, logFoundDevices, emitMissingAlarms);
}

static void addScanResultToJson(JsonArray& arr, uint8_t bus, uint64_t rom) {
    JsonObject obj = arr.createNestedObject();
    obj["bus"] = bus;
    obj["rom"] = ds18b20RomToString(rom);

    int slot = findDs18B20AssignmentByRom(rom);
    obj["assigned"] = (slot >= 0 && slot < NUM_SENSORS);
    obj["slot"] = slot;
    obj["dtemp"] = (slot >= 0 && slot < NUM_SENSORS) ? (slot + 1) : 0;

    if (slot >= 0 && slot < NUM_SENSORS) {
        obj["systemTemp"] = g_ds18b20Config.assignments[slot].systemTemp;
    } else {
        obj["systemTemp"] = "";
    }
}

String buildDS18B20ScanJson() {
    DynamicJsonDocument doc(4096);
    doc["ok"] = true;

    JsonArray sensors = doc.createNestedArray("sensors");

    bool locked = false;
    if (temperatureMutex != nullptr) {
        locked = (xSemaphoreTake(temperatureMutex, pdMS_TO_TICKS(2500)) == pdTRUE);
        if (!locked) {
            doc["ok"] = false;
            doc["error"] = "temperatureMutex busy";
            String busyJson;
            serializeJson(doc, busyJson);
            return busyJson;
        }
    }

    uint64_t foundAddrs[20];

    uint8_t count1 = sensors1.search(foundAddrs, 20);
    JsonArray bus1 = doc.createNestedArray("bus1");
    for (uint8_t i = 0; i < count1; i++) {
        uint64_t rom = foundAddrs[i];
        if (!isLikelyValidDs18Rom(rom)) continue;
        addScanResultToJson(bus1, 1, rom);
        addScanResultToJson(sensors, 1, rom);
    }

    uint8_t count2 = sensors2.search(foundAddrs, 20);
    JsonArray bus2 = doc.createNestedArray("bus2");
    for (uint8_t i = 0; i < count2; i++) {
        uint64_t rom = foundAddrs[i];
        if (!isLikelyValidDs18Rom(rom)) continue;
        addScanResultToJson(bus2, 2, rom);
        addScanResultToJson(sensors, 2, rom);
    }

    // Restart conversions because a manual bus search may interrupt the normal
    // conversion/read cadence.
    sensors1.request();
    sensors2.request();

    if (locked) {
        xSemaphoreGive(temperatureMutex);
    }

    doc["count"] = sensors.size();

    String json;
    serializeJson(doc, json);
    return json;
}

void initDS18B20Sensors() {
    g_ds18b20Ready = false;

    // Give the long outside bus a little time to settle after boot.
    vTaskDelay(pdMS_TO_TICKS(DS18_BOOT_SETTLE_MS));

    // Clear runtime state once at boot.
    for (int i = 0; i < NUM_SENSORS; i++) {
        sensorMappings[i] = {0ULL, i};
        sensorPresent[i] = false;
        sensorOnBus1[i] = false;
        sensorObserved[i] = false;
        sensorObservedBus[i] = 0;
        sensorFaultLatched[i] = false;
        DTempLastGoodReadMs[i] = 0;
        sensorOffsets[i] = g_ds18b20Config.assignments[i].offsetF;
    }

    bool needBus1 = true;
    bool needBus2 = true;

    for (uint8_t attempt = 1; attempt <= DS18_BOOT_SCAN_RETRIES; attempt++) {
        LOG_CAT(DBG_1WIRE,
                "[DS18B20] Boot scan attempt %u/%u\n",
                (unsigned)attempt,
                (unsigned)DS18_BOOT_SCAN_RETRIES);

        bool finalAttempt = (attempt == DS18_BOOT_SCAN_RETRIES);

        if (needBus1) {
            scanAndAddMissingSensorsOnBus(true, true, finalAttempt);
        }

        if (needBus2) {
            scanAndAddMissingSensorsOnBus(false, true, finalAttempt);
        }

        needBus1 = anySensorsMissingOnBus(true);
        needBus2 = anySensorsMissingOnBus(false);

        if (!needBus1 && !needBus2) {
            LOG_CAT(DBG_1WIRE,
                    "[DS18B20] All expected sensors found on attempt %u\n",
                    (unsigned)attempt);
            break;
        }

        if (!finalAttempt) {
            vTaskDelay(pdMS_TO_TICKS(DS18_BOOT_RETRY_GAP_MS));
        }
    }

    // Kick the first non-blocking conversion.
    sensors1.request();
    sensors2.request();

    s_lastMissingRescanMs = millis();

    g_ds18b20Ready = true;

    LOG_CAT(DBG_1WIRE,
            "[DS18B20] Init complete – runtime mapping built additively from configured address matches\n");
}

bool ds18B20SensorsReady() {
    return g_ds18b20Ready;
}

bool getDS18B20SlotStatus(uint8_t slot, bool* presentOut, uint8_t* observedBusOut, uint32_t* lastGoodMsOut) {
    if (slot >= NUM_SENSORS) return false;

    if (presentOut) *presentOut = sensorPresent[slot];
    if (observedBusOut) *observedBusOut = sensorObserved[slot] ? sensorObservedBus[slot] : 0;
    if (lastGoodMsOut) *lastGoodMsOut = DTempLastGoodReadMs[slot];

    return true;
}

float calculateAverage(float values[], int numReadings) {
    float sum = 0; int count = 0;
    for (int i = 0; i < numReadings; i++) {
        if (values[i] > -100) { sum += values[i]; count++; }
    }
    return count > 0 ? sum / count : -196.60f;
}

void updateDS18B20Readings() {
    // If sensors are still missing, do a low-duty-cycle additive rescan once per minute,
    // and only on the bus(es) that are still missing sensors.
    bool needBus1 = anySensorsMissingOnBus(true);
    bool needBus2 = anySensorsMissingOnBus(false);

    if (needBus1 || needBus2) {
        uint32_t now = millis();
        if ((uint32_t)(now - s_lastMissingRescanMs) >= DS18_MISSING_RESCAN_MS) {
            s_lastMissingRescanMs = now;

            LOG_CAT(DBG_1WIRE, "[DS18B20] Periodic targeted rescan for missing sensors\n");

            if (needBus1) {
                LOG_CAT(DBG_1WIRE, "[DS18B20] Rescanning Bus 1\n");
                scanAndAddMissingSensorsOnBus(true, true, false);
            }

            if (needBus2) {
                LOG_CAT(DBG_1WIRE, "[DS18B20] Rescanning Bus 2\n");
                scanAndAddMissingSensorsOnBus(false, true, false);
            }

            // Start a fresh conversion after rescanning and skip this read cycle.
            sensors1.request();
            sensors2.request();
            return;
        }
    }

    for (int i = 0; i < NUM_SENSORS; i++) {
        // YIELD: Give the network stack time to process W5500 interrupts.
        vTaskDelay(pdMS_TO_TICKS(1));

        // Skip disabled / unassigned / missing sensors entirely.
        if (!isAssignmentEnabled(i) || !sensorPresent[i] || sensorMappings[i].address == 0ULL) {
            continue;
        }

        float tempC = 0.0f;
        uint8_t err = sensorOnBus1[i]
            ? sensors1.getTemp(sensorMappings[i].address, tempC)
            : sensors2.getTemp(sensorMappings[i].address, tempC);

                if (err != 0) {
            if (!sensorFaultLatched[i]) {
                LOG_ERR("[DS18B20] Sensor %d error %d\n", i + 1, err);
                AlarmManager_event(ALM_SENSOR_FAULT, ALM_WARN,
                                   "DS18B20 Sensor %d Offline", i + 1);
                sensorFaultLatched[i] = true;
            }
            continue;
        }

        if (sensorFaultLatched[i]) {
            LOG_CAT(DBG_1WIRE, "[DS18B20] Sensor %d recovered\n", i + 1);
            AlarmManager_event(ALM_SENSOR_FAULT, ALM_WARN,
                               "DS18B20 Sensor %d Online", i + 1);
            sensorFaultLatched[i] = false;
        }

        float rawF = tempC * 1.8f + 32.0f;
        rawF += sensorOffsets[i];
        updateDS18B20Temperature(i, rawF);
    }

    // Start the next non-blocking conversion for both buses.
    sensors1.request();
    sensors2.request();
}

void updateDS18B20Temperature(int sensorIndex, float temperature) {
    if (temperature <= -126.0f) return;
    DTempValues[sensorIndex][readingsIndex[sensorIndex]] = temperature;
    readingsIndex[sensorIndex] = (readingsIndex[sensorIndex] + 1) % numReadings;
    DTemp[sensorIndex] = temperature;
    DTempAverage[sensorIndex] = calculateAverage(DTempValues[sensorIndex], numReadings);
    DTempLastGoodReadMs[sensorIndex] = millis();
}