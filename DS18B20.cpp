// DS18B20.cpp 
#include "DS18B20.h"
#include "Config.h"
#include "DiagLog.h"
#include "AlarmManager.h"

OneWire32 sensors1(ONE_WIRE_BUS_1);
OneWire32 sensors2(ONE_WIRE_BUS_2);

float DTemp[NUM_SENSORS] = {0};
float DTempAverage[NUM_SENSORS] = {0.0};

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
   
    /*
// ── SOLAR HOUSE SENSORS  ───────
SensorMapping sensorMappings[NUM_SENSORS] = {
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
*/

};

static int findExpectedLogicalIndex(uint64_t address) {
    for (int e = 0; e < NUM_SENSORS; e++) {
        if (expectedMappings[e].address == address) {
            return expectedMappings[e].arrayIndex;
        }
    }
    return -1;
}



void initDS18B20Sensors() {
    uint64_t foundAddrs[2][20];
    uint8_t count1 = sensors1.search(foundAddrs[0], 20);
    uint8_t count2 = sensors2.search(foundAddrs[1], 20);

    LOG_CAT(DBG_1WIRE, "[DS18B20] Bus 1 found %d devices\n", count1);
    for (uint8_t i = 0; i < count1; i++) {
        LOG_CAT(DBG_1WIRE, "  Bus1[%d] = 0x%llxULL\n", i, foundAddrs[0][i]);
    }
    LOG_CAT(DBG_1WIRE, "[DS18B20] Bus 2 found %d devices\n", count2);
    for (uint8_t i = 0; i < count2; i++) {
        LOG_CAT(DBG_1WIRE, "  Bus2[%d] = 0x%llxULL\n", i, foundAddrs[1][i]);
    }

    // Reset runtime mapping/state.
    // Runtime arrays are indexed by LOGICAL sensor index, not discovery order.
    for (int i = 0; i < NUM_SENSORS; i++) {
        sensorMappings[i] = {0ULL, i};
        sensorPresent[i] = false;
        sensorOnBus1[i] = false;
        sensorFaultLatched[i] = false;
    }

    // Match found addresses against expectedMappings so logical identity stays stable.
    for (uint8_t i = 0; i < count1; i++) {
        int logicalIndex = findExpectedLogicalIndex(foundAddrs[0][i]);
        if (logicalIndex >= 0 && logicalIndex < NUM_SENSORS) {
            sensorMappings[logicalIndex] = {foundAddrs[0][i], logicalIndex};
            sensorPresent[logicalIndex] = true;
            sensorOnBus1[logicalIndex] = true;
        } else {
            LOG_CAT(DBG_1WIRE,
                    "[DS18B20] Unexpected sensor found on Bus 1: 0x%llxULL\n",
                    foundAddrs[0][i]);
        }
    }

    for (uint8_t i = 0; i < count2; i++) {
        int logicalIndex = findExpectedLogicalIndex(foundAddrs[1][i]);
        if (logicalIndex >= 0 && logicalIndex < NUM_SENSORS) {
            sensorMappings[logicalIndex] = {foundAddrs[1][i], logicalIndex};
            sensorPresent[logicalIndex] = true;
            sensorOnBus1[logicalIndex] = false;
        } else {
            LOG_CAT(DBG_1WIRE,
                    "[DS18B20] Unexpected sensor found on Bus 2: 0x%llxULL\n",
                    foundAddrs[1][i]);
        }
    }

    // Report any expected sensors that are missing.
    for (int e = 0; e < NUM_SENSORS; e++) {
        int logicalIndex = expectedMappings[e].arrayIndex;
        uint64_t expectedAddr = expectedMappings[e].address;

        if (!sensorPresent[logicalIndex] && expectedAddr != 0ULL) {
            LOG_CAT(DBG_1WIRE,
                    "DS18B20 Sensor Missing: address 0x%llxULL, SOURCE_MAP=%s, SENSOR_NAME=%s\n",
                    expectedAddr,
                    SOURCE_MAP[logicalIndex],
                    SENSOR_NAMES[logicalIndex]);

            AlarmManager_event(ALM_SENSOR_FAULT, ALM_WARN,
                "DS18B20 Missing: %s (%s)",
                SENSOR_NAMES[logicalIndex],
                SOURCE_MAP[logicalIndex]);
        }
    }

    // Kick the first non-blocking conversion.
    sensors1.request();
    sensors2.request();

    LOG_CAT(DBG_1WIRE,
            "[DS18B20] Init complete – runtime mapping built from expected address matches\n");
}

float calculateAverage(float values[], int numReadings) {
    float sum = 0; int count = 0;
    for (int i = 0; i < numReadings; i++) {
        if (values[i] > -100) { sum += values[i]; count++; }
    }
    return count > 0 ? sum / count : -196.60f;
}

void updateDS18B20Readings() {
    for (int i = 0; i < NUM_SENSORS; i++) {
        // YIELD: Give the network stack time to process W5500 interrupts.
        // Reading a 1-Wire scratchpad takes ~5ms. Yielding here prevents the 
        // loop from blocking the FreeRTOS scheduler for ~65ms+ straight.
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
}