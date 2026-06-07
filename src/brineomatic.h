/*
  Yarrboard

  Author: Zach Hoeken <hoeken@gmail.com>
  Website: https://github.com/hoeken/yarrboard
  License: GPLv3
*/

#ifndef YARR_BRINEOMATIC_H
#define YARR_BRINEOMATIC_H

#include "config.h"

#include "Flowmeter.h"
#include "GD20Modbus.h"
#include "adchelper.h"
#include "brineomatic_config.h"
#include "etl/deque.h"
#include <ADS1X15.h>
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <GravityTDS.h>
#include <OneWire.h>
#include <controllers/AuthTypes.h>

// #include <QuickPID.h>

class YarrboardApp;
class RelayChannel;
class ServoChannel;
class StepperChannel;
class RelayController;
class ServoController;
class StepperController;

class Brineomatic
{
  public:
    enum class Status {
      STARTUP,
      MANUAL,
      IDLE,
      RUNNING,
      STOPPING,
      FLUSHING,
      PICKLING,
      PICKLED,
      DEPICKLING
    };

// Master list of all Result enum entries (single source of truth)
#define BOM_RESULT_LIST            \
  X(STARTUP)                       \
  X(SUCCESS)                       \
  X(SUCCESS_TIME)                  \
  X(SUCCESS_VOLUME)                \
  X(SUCCESS_TANK_LEVEL)            \
  X(SUCCESS_SALINITY)              \
  X(USER_STOP)                     \
  X(ERR_BATTERY_LEVEL)             \
  X(ERR_FILTER_PRESSURE_TIMEOUT)   \
  X(ERR_FILTER_PRESSURE_LOW)       \
  X(ERR_FILTER_PRESSURE_HIGH)      \
  X(ERR_MEMBRANE_PRESSURE_TIMEOUT) \
  X(ERR_MEMBRANE_PRESSURE_LOW)     \
  X(ERR_MEMBRANE_PRESSURE_HIGH)    \
  X(ERR_PRODUCT_FLOWRATE_TIMEOUT)  \
  X(ERR_PRODUCT_FLOWRATE_LOW)      \
  X(ERR_PRODUCT_FLOWRATE_HIGH)     \
  X(ERR_FLUSH_FLOWRATE_LOW)        \
  X(ERR_FLUSH_FILTER_PRESSURE_LOW) \
  X(ERR_FLUSH_VALVE_ON)            \
  X(ERR_FLUSH_TANK_LEVEL_LOW)      \
  X(ERR_FLUSH_TIMEOUT)             \
  X(ERR_BRINE_FLOWRATE_LOW)        \
  X(ERR_TOTAL_FLOWRATE_LOW)        \
  X(ERR_DIVERTER_VALVE_OPEN)       \
  X(ERR_PRODUCT_SALINITY_TIMEOUT)  \
  X(ERR_PRODUCT_SALINITY_HIGH)     \
  X(ERR_PRODUCTION_TIMEOUT)        \
  X(ERR_MOTOR_TEMPERATURE_HIGH)

    enum class Result {
#define X(name) name,
      BOM_RESULT_LIST
#undef X
    };

    // Static lookup tables
    static constexpr const char* const AUTOFLUSH_MODES[] = {"NONE", "TIME", "SALINITY", "VOLUME"};
    static constexpr const char* const TEMPERATURE_UNITS[] = {"celsius", "fahrenheit"};
    static constexpr const char* const PRESSURE_UNITS[] = {"kilopascal", "psi", "bar"};
    static constexpr const char* const VOLUME_UNITS[] = {"liters", "gallons"};
    static constexpr const char* const FLOWRATE_UNITS[] = {"lph", "gph"};

    static constexpr const char* BOOST_PUMP_CONTROLS[] = {"NONE", "MANUAL", "RELAY"};
    static constexpr const char* HIGH_PRESSURE_PUMP_CONTROLS[] = {"NONE", "MANUAL", "RELAY", "MODBUS"};
    static constexpr const char* HIGH_PRESSURE_PUMP_MODBUS_DEVICES[] = {"GD20"};
    static constexpr const char* HIGH_PRESSURE_VALVE_CONTROLS[] = {"NONE", "MANUAL", "STEPPER"};
    static constexpr const char* DIVERTER_VALVE_CONTROLS[] = {"NONE", "MANUAL", "RELAY", "SERVO", "DUAL_RELAYS"};
    static constexpr const char* FLUSH_VALVE_CONTROLS[] = {"NONE", "MANUAL", "RELAY", "SERVO"};
    static constexpr const char* COOLING_FAN_CONTROLS[] = {"NONE", "MANUAL", "RELAY"};
    static constexpr const char* MOTOR_TEMPERATURE_TYPES[] = {"NONE", "EXTERNAL", "DS18B20", "MQTT"};
    static constexpr const char* WATER_TEMPERATURE_TYPES[] = {"NONE", "EXTERNAL", "DS18B20", "MQTT"};
    static constexpr const char* TANK_LEVEL_SENSOR_TYPES[] = {"NONE", "EXTERNAL", "MQTT"};
    static constexpr const char* BATTERY_LEVEL_SENSOR_TYPES[] = {"NONE", "EXTERNAL", "MQTT"};

    bool isPickled;
    int64_t pickledOnTimestamp = 0;

    RelayChannel* flushValve = NULL;
    ServoChannel* flushValveServo = NULL;
    RelayChannel* boostPump = NULL;
    RelayChannel* highPressurePump = NULL;
    RelayChannel* coolingFan = NULL;
    RelayChannel* diverterValveRelay = NULL;
    ServoChannel* diverterValveServo = NULL;
    RelayChannel* diverterValveTankRelay = NULL;
    RelayChannel* diverterValveOverboardRelay = NULL;
    StepperChannel* highPressureValveStepper = NULL;

    OneWire motorTemperatureOneWire;
    DallasTemperature motorTemperatureSensor;
    DeviceAddress motorTemperatureAddress;

    OneWire waterTemperatureOneWire;
    DallasTemperature waterTemperatureSensor;
    DeviceAddress waterTemperatureAddress;

    Flowmeter productFlowmeter;
    Flowmeter brineFlowmeter;

    GravityTDS gravityTds;

    ADS1115 _adc;
    ADS1115Helper* adcHelper;

    GD20Modbus* gd20;

    // float membranePressurePIDOutput;
    // QuickPID membranePressurePID;
    // float KpRamp = 0;
    // float KiRamp = 0;
    // float KdRamp = 0;
    // float KpMaintain = 0;
    // float KiMaintain = 0;
    // float KdMaintain = 0;

    float currentVolume;
    float currentFlushVolume;

    uint32_t totalCycles;
    float totalVolume;
    uint32_t totalRuntime; // seconds

    Brineomatic(YarrboardApp& app, RelayController& relays, ServoController& servos, StepperController& steppers);
    void init();
    void initChannels();
    void initModbus();

    void loop();

    void measureMotorTemperature();
    void measureWaterTemperature();
    void measureProductFlowmeter();
    void measureBrineFlowmeter();
    void measureProductSalinity();
    void measureBrineSalinity();
    void measureFilterPressure();
    void measureMembranePressure();

    void setMembranePressureTarget(float pressure);
    void setMotorTemperature(float temp);
    void setWaterTemperature(float temp);
    void setTankLevel(float level);
    void setBatteryLevel(float level);

    void idle();
    void manual();
    void start();
    void startDuration(uint32_t duration);
    void startVolume(float volume);
    void flush();
    void flushDuration(uint32_t duration);
    void flushVolume(float volume);
    void pickle(uint32_t duration);
    void depickle(uint32_t duration);
    void stop();

    bool initializeHardware(bool emergencyStop = false);

    bool autoflushEnabled();

    bool isBoostPumpOn();
    bool hasBoostPump();
    void enableBoostPump();
    void disableBoostPump();

    bool isHighPressurePumpOn();
    bool hasHighPressurePump();
    void enableHighPressurePump();
    void disableHighPressurePump();
    void modbusEnableHighPressurePump();
    void modbusDisableHighPressurePump();

    bool isDiverterValveOpen();
    bool hasDiverterValve();
    void openDiverterValve();
    void closeDiverterValve();

    bool isFlushValveOpen();
    bool hasFlushValve();
    void openFlushValve();
    void closeFlushValve();

    bool isCoolingFanOn();
    bool hasCoolingFan();
    void enableCoolingFan();
    void disableCoolingFan();
    void manageCoolingFan();

    bool hasHighPressureValve();
    void manageHighPressureValve();

    bool hasMotorTemperature();
    bool hasWaterTemperature();
    bool hasFilterPressure();
    bool hasMembranePressure();
    bool hasProductFlow();
    bool hasBrineFlow();
    bool hasProductTDS();
    bool hasBrineTDS();

    const char* getStatus();
    const char* getStatus(Status status);
    const char* resultToString(Result result);
    Result getRunResult();
    Result getFlushResult();
    Result getPickleResult();
    Result getDepickleResult();

    uint32_t getNextFlushCountdown();
    uint32_t getRuntimeElapsed();
    uint32_t getFinishCountdown();
    uint32_t getFlushElapsed();
    uint32_t getFlushCountdown();
    uint32_t getPickleElapsed();
    uint32_t getPickleCountdown();
    uint32_t getDepickleElapsed();
    uint32_t getDepickleCountdown();

    float getFilterPressure();
    float getFilterPressureMinimum();
    float getMembranePressure();
    float getMembranePressureMinimum();
    float getProductFlowrate();
    float getProductFlowrateMinimum();
    float getBrineFlowrate();
    float getTotalFlowrate();
    float getTotalFlowrateMinimum();
    uint32_t getTotalCycles();
    float getVolume();
    float getFlushVolume();
    float getTotalVolume();
    uint32_t getTotalRuntime();
    float getMotorTemperature();
    float getWaterTemperature();
    float getProductSalinity();
    float getProductSalinityMaximum();
    float getBrineSalinity();
    float getTankLevel();
    float getTankCapacity();
    float getBatteryLevel();
    float getMotorTemperatureMaximum();

    const char* getTemperatureUnits();
    const char* getPressureUnits();
    const char* getVolumeUnits();
    const char* getFlowrateUnits();

    void runStateMachine();

    void generateUpdateJSON(JsonVariant output);

    // Live configuration.  Validation, loading, and saving of the config JSON
    // lives in BrineomaticController; it operates directly on this struct so
    // both classes reference the same data.
    BrineomaticConfig& getConfig() { return _config; }

    void updateMQTT();

    void logResult(Status status, Result result);

  private:
    YarrboardApp& _app;
    RelayController& _relays;
    ServoController& _servos;
    StepperController& _steppers;

    Status currentStatus;
    Result runResult;
    Result flushResult;
    Result pickleResult;
    Result depickleResult;

    bool stopFlag = false;

    float desiredFlushVolume = 0;
    uint32_t desiredFlushDuration = 0;
    uint32_t desiredRuntime = 0;
    float desiredVolume = 0;

    // all these times are in milliseconds
    uint32_t runtimeStart = 0;
    uint32_t runtimeElapsed = 0;
    uint32_t flushStart = 0;
    uint32_t lastAutoflushTimeMillis = 0;
    int64_t lastAutoflushTimeNTP = 0;
    uint32_t pickleStart = 0;
    uint32_t pickleDuration;
    uint32_t depickleStart = 0;
    uint32_t depickleDuration;

    bool boostPumpOnState;
    bool highPressurePumpOnState;
    bool diverterValveOpenState;
    bool flushValveOpenState;
    bool coolingFanOnState;

    float currentTankLevel;
    float currentBatteryLevel;
    float currentWaterTemperature;
    float currentMotorTemperature;
    float currentProductFlowrate;
    float currentBrineFlowrate;
    float currentProductSalinity;
    float currentBrineSalinity;
    float currentFilterPressure;
    float currentMembranePressure;
    float currentMembranePressureTarget;

    // tracking when we first saw the error condition
    uint32_t membranePressureHighStart = 0;
    uint32_t membranePressureLowStart = 0;
    uint32_t filterPressureHighStart = 0;
    uint32_t filterPressureLowStart = 0;
    uint32_t productFlowrateLowStart = 0;
    uint32_t productFlowrateHighStart = 0;
    uint32_t brineFlowrateLowStart = 0;
    uint32_t totalFlowrateLowStart = 0;
    uint32_t flushFilterPressureLowStart = 0;
    uint32_t flushFlowrateLowStart = 0;
    uint32_t flushTankLevelLowStart = 0;
    uint32_t diverterValveOpenStart = 0;
    uint32_t productSalinityHighStart = 0;
    uint32_t motorTemperatureStart = 0;
    uint32_t tankLevelFullStart = 0;

    //
    // Configuration.  `defaults` (declared public above) holds the factory
    // defaults; `_config` holds the settings as they are actually used,
    // loaded from / saved to the configuration JSON.
    //
    BrineomaticConfig _config;

    float tankLevelFull = 0.99; // 0 = empty, 1 = full (runtime, not persisted)

    void resetErrorTimers();

    bool checkStopFlag(Result& result);
    bool checkTankLevel();
    bool checkBatteryLevel(Result& result);
    bool checkMembranePressureHigh();
    bool checkMembranePressureLow();
    bool checkFilterPressureHigh();
    bool checkFilterPressureLow();
    bool checkProductFlowrateLow();
    bool checkProductFlowrateHigh();
    bool checkPickleTotalFlowrateLow(Result& result);
    bool checkRunTotalFlowrateLow();
    bool checkFlushFilterPressureLow();
    bool checkFlushFlowrateLow();
    bool checkFlushTankLevelLow();
    bool checkDiverterValveClosed();
    bool checkProductSalinityHigh();
    bool checkMotorTemperature(Result& result);
    bool waitForMembranePressure();
    bool waitForProductFlowrate();
    bool waitForProductSalinity();
    bool waitForFlushValveOff();

    bool checkTimedError(bool condition,
      uint32_t& startTime,
      uint32_t timeout,
      Result errorResult,
      Result& result);
};

template <class X, class M, class N, class O, class Q>
X map_generic(X x, M in_min, N in_max, O out_min, Q out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#endif /* !YARR_BRINEOMATIC_H */