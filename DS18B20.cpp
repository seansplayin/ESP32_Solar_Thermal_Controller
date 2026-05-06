// DS18B20.cpp 
#include "DS18B20.h"
#include "Config.h"
#include "DiagLog.h"
#include "AlarmManager.h"

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
static bool sensorFaultLatched[NUM_SENSORS] = {false};

// Expected sensor list (exact addresses from your diagnostic run)
// ── Test Bennch SENSORS  ───────
static const SensorMapping expectedMappings[NUM_SENSORS] = {
/*
// ── Test Bench SENSORS  ───────
    {0x3f3c910457bbd028ULL, 0},
    {0xa13c690457350428ULL, 1},
    {0x13cf60457fee428ULL,  2},
    {0x770722b2275a8c28ULL, 3},
    {0xe23c350457fddc28ULL, 4},
    {0xa13ca704574f5d28ULL, 5},
    {0x2b3c54045745c028ULL, 6},
    {0x3c6fe381c97c28ULL,   7},
    {0xfa3cc80457e29e28ULL, 8},
    {0x753ccdf64815f128ULL, 9},
    {0xa23c330457d1fb28ULL, 10},
    {0x963cf2045776e728ULL, 11},
    {0xe80722b24856bf28ULL, 12}
*/   
    
// ── SOLAR HOUSE SENSORS  ───────
    {0x2D3C0DF649163728ULL, 0},
    {0xAD3C7AF6489A6928ULL, 1},
    {0x023C01F096165228ULL, 2},
    {0xF23C2BE381EA8528ULL, 3},
    {0x2B0722B20EA9F628ULL, 4},
    {0x0F3C01F096842A28ULL, 5},
    {0xB53C53E3811A2928ULL, 6},
    {0x923C17E381C62228ULL, 7},
    {0xA53CD2E381F16628ULL, 8},
    {0xD83CDCE381142228ULL, 9},
    {0xC70922B208BD0328ULL, 10},
    {0xEB3C01F0969BDD28ULL, 11},
    {0x4D3CE6F648E23728ULL, 12}


};



static int findExpectedLogicalIndex(uint64_t address) {
    for (int e = 0; e < NUM_SENSORS; e++) {
        if (expectedMappings[e].address == address) {
            return expectedMappings[e].arrayIndex;
        }
    }
    return -1;
}

// House controller physical bus layout:
// logical indices 0..5  = outside sensors on Bus 2
// logical indices 6..12 = inside sensors on Bus 1
static const bool expectedOnBus1[NUM_SENSORS] = {
    false, false, false, false, false, false,
    true,  true,  true,  true,  true,  true,  true
};

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
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (expectedOnBus1[i] == wantBus1 && !sensorPresent[i]) {
            return true;
        }
    }
    return false;
}

static void logMissingSensorsOnBus(bool wantBus1, bool logFoundDevices, bool emitMissingAlarms) {
    for (int e = 0; e < NUM_SENSORS; e++) {
        int logicalIndex = expectedMappings[e].arrayIndex;
        uint64_t expectedAddr = expectedMappings[e].address;

        if (expectedOnBus1[logicalIndex] != wantBus1) continue;

        if (!sensorPresent[logicalIndex] && expectedAddr != 0ULL) {
            int nameIndex = logicalIndex + 2; // DTemp1 starts at SENSOR_NAMES[2]

            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "DS18B20 Sensor Missing on Bus %d: address 0x%llxULL, SOURCE_MAP=%d, SENSOR_NAME=%s\n",
                        wantBus1 ? 1 : 2,
                        expectedAddr,
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

        int logicalIndex = findExpectedLogicalIndex(rom);

        if (logicalIndex < 0 || logicalIndex >= NUM_SENSORS) {
            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "[DS18B20] Unexpected valid sensor found on Bus %d: 0x%llxULL\n",
                        wantBus1 ? 1 : 2,
                        rom);
            }
            continue;
        }

        if (expectedOnBus1[logicalIndex] != wantBus1) {
            if (logFoundDevices) {
                LOG_CAT(DBG_1WIRE,
                        "[DS18B20] Sensor 0x%llxULL belongs on Bus %d but was found on Bus %d\n",
                        rom,
                        expectedOnBus1[logicalIndex] ? 1 : 2,
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
                        "[DS18B20] Added missing sensor on Bus %d: logical=%d SOURCE_MAP=%d SENSOR_NAME=%s\n",
                        wantBus1 ? 1 : 2,
                        logicalIndex,
                        SOURCE_MAP[nameIndex],
                        SENSOR_NAMES[nameIndex]);
            }
        }
    }

    logMissingSensorsOnBus(wantBus1, logFoundDevices, emitMissingAlarms);
}

void initDS18B20Sensors() {
    // Give the long outside bus a little time to settle after boot.
    vTaskDelay(pdMS_TO_TICKS(DS18_BOOT_SETTLE_MS));

    // Clear runtime state once at boot.
    for (int i = 0; i < NUM_SENSORS; i++) {
        sensorMappings[i] = {0ULL, i};
        sensorPresent[i] = false;
        sensorOnBus1[i] = false;
        sensorFaultLatched[i] = false;
        DTempLastGoodReadMs[i] = 0;
    }

    bool needBus1 = true; // inside sensors
    bool needBus2 = true; // outside sensors

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

    LOG_CAT(DBG_1WIRE,
            "[DS18B20] Init complete – runtime mapping built additively from expected address matches\n");
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
                LOG_CAT(DBG_1WIRE, "[DS18B20] Rescanning Bus 1 (inside sensors)\n");
                scanAndAddMissingSensorsOnBus(true, true, false);
            }

            if (needBus2) {
                LOG_CAT(DBG_1WIRE, "[DS18B20] Rescanning Bus 2 (outside sensors)\n");
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

        // Skip unassigned / missing sensors entirely.
        if (!sensorPresent[i] || sensorMappings[i].address == 0ULL) {
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