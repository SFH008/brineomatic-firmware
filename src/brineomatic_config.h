#ifndef BRINEOMATIC_CONFIG_H
#define BRINEOMATIC_CONFIG_H

#include <Arduino.h>

#ifndef YB_TANK_LEVEL_FULL
  #define YB_TANK_LEVEL_FULL 0.99
#endif

#ifndef YB_TANK_CAPACITY
  #define YB_TANK_CAPACITY 780
#endif

#ifndef YB_COOLING_FAN_ON_TEMPERATURE
  #define YB_COOLING_FAN_ON_TEMPERATURE 35.0
#endif

#ifndef YB_COOLING_FAN_OFF_TEMPERATURE
  #define YB_COOLING_FAN_OFF_TEMPERATURE 34.0
#endif

#ifndef YB_AUTOFLUSH_MODE
  #define YB_AUTOFLUSH_MODE "TIME"
#endif

#ifndef YB_AUTOFLUSH_SALINITY
  #define YB_AUTOFLUSH_SALINITY 700.0
#endif

#ifndef YB_AUTOFLUSH_DURATION
  #define YB_AUTOFLUSH_DURATION (5 * 60 * 1000)
#endif

#ifndef YB_AUTOFLUSH_VOLUME
  #define YB_AUTOFLUSH_VOLUME 15.0
#endif

#ifndef YB_AUTOFLUSH_INTERVAL
  #define YB_AUTOFLUSH_INTERVAL (3 * 24 * 60 * 60 * 1000)
#endif

#ifndef YB_AUTOFLUSH_USE_HIGH_PRESSURE_MOTOR
  #define YB_AUTOFLUSH_USE_HIGH_PRESSURE_MOTOR false
#endif

#ifndef YB_TEMPERATURE_UNITS
  #define YB_TEMPERATURE_UNITS "celsius"
#endif

#ifndef YB_PRESSURE_UNITS
  #define YB_PRESSURE_UNITS "bar"
#endif

#ifndef YB_VOLUME_UNITS
  #define YB_VOLUME_UNITS "liters"
#endif

#ifndef YB_FLOWRATE_UNITS
  #define YB_FLOWRATE_UNITS "lph"
#endif

#ifndef YB_SUCCESS_MELODY
  #define YB_SUCCESS_MELODY "SUCCESS"
#endif

#ifndef YB_ERROR_MELODY
  #define YB_ERROR_MELODY "ERROR"
#endif

#ifndef YB_BOOST_PUMP_CONTROL
  #define YB_BOOST_PUMP_CONTROL "MANUAL"
#endif

#ifndef YB_BOOST_PUMP_RELAY_ID
  #define YB_BOOST_PUMP_RELAY_ID 1
#endif

#ifndef YB_BOOST_PUMP_RELAY_INVERTED
  #define YB_BOOST_PUMP_RELAY_INVERTED false
#endif

#ifndef YB_BOOST_PUMP_DELAY_MS
  #define YB_BOOST_PUMP_DELAY_MS 0
#endif

#ifndef YB_HIGH_PRESSURE_PUMP_CONTROL
  #define YB_HIGH_PRESSURE_PUMP_CONTROL "MANUAL"
#endif

#ifndef YB_HIGH_PRESSURE_RELAY_ID
  #define YB_HIGH_PRESSURE_RELAY_ID 2
#endif

#ifndef YB_HIGH_PRESSURE_RELAY_INVERTED
  #define YB_HIGH_PRESSURE_RELAY_INVERTED false
#endif

#ifndef YB_HIGH_PRESSURE_PUMP_MODBUS_DEVICE
  #define YB_HIGH_PRESSURE_PUMP_MODBUS_DEVICE "GD20"
#endif

#ifndef YB_HIGH_PRESSURE_PUMP_MODBUS_SLAVE_ID
  #define YB_HIGH_PRESSURE_PUMP_MODBUS_SLAVE_ID 0
#endif

#ifndef YB_HIGH_PRESSURE_PUMP_MODBUS_FREQUENCY
  #define YB_HIGH_PRESSURE_PUMP_MODBUS_FREQUENCY 25.0
#endif

#ifndef YB_HIGH_PRESSURE_PUMP_DELAY_MS
  #define YB_HIGH_PRESSURE_PUMP_DELAY_MS 0
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_CONTROL
  #define YB_HIGH_PRESSURE_VALVE_CONTROL "MANUAL"
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_ID
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_ID 1
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_STEP_ANGLE
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_STEP_ANGLE 1.8
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_GEAR_RATIO
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_GEAR_RATIO 3.0
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_CLOSE_ANGLE
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_CLOSE_ANGLE 1660.0
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_CLOSE_SPEED
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_CLOSE_SPEED 10.0
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_OPEN_ANGLE
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_OPEN_ANGLE 0.0
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_OPEN_SPEED
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_OPEN_SPEED 40.0
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_RUN_CURRENT
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_RUN_CURRENT 50
#endif

#ifndef YB_HIGH_PRESSURE_VALVE_STEPPER_HOME_CURRENT
  #define YB_HIGH_PRESSURE_VALVE_STEPPER_HOME_CURRENT 30
#endif

#ifndef YB_HIGH_PRESSURE_STEPPER_INVERTED
  #define YB_HIGH_PRESSURE_STEPPER_INVERTED false
#endif

#ifndef YB_MEMBRANE_PRESSURE_TARGET
  // Bar
  #define YB_MEMBRANE_PRESSURE_TARGET 55
#endif

#ifndef YB_DIVERTER_VALVE_CONTROL
  #define YB_DIVERTER_VALVE_CONTROL "MANUAL"
#endif

#ifndef YB_DIVERTER_VALVE_RELAY_ID
  #define YB_DIVERTER_VALVE_RELAY_ID 3
#endif

#ifndef YB_DIVERTER_VALVE_RELAY_INVERTED
  #define YB_DIVERTER_VALVE_RELAY_INVERTED false
#endif

#ifndef YB_DIVERTER_VALVE_SERVO_ID
  #define YB_DIVERTER_VALVE_SERVO_ID 1
#endif

#ifndef YB_DIVERTER_VALVE_OPEN_ANGLE
  #define YB_DIVERTER_VALVE_OPEN_ANGLE 35
#endif

#ifndef YB_DIVERTER_VALVE_CLOSE_ANGLE
  #define YB_DIVERTER_VALVE_CLOSE_ANGLE 125
#endif

#ifndef YB_DIVERTER_VALVE_TANK_RELAY_ID
  #define YB_DIVERTER_VALVE_TANK_RELAY_ID 3
#endif

#ifndef YB_DIVERTER_VALVE_TANK_RELAY_INVERTED
  #define YB_DIVERTER_VALVE_TANK_RELAY_INVERTED false
#endif

#ifndef YB_DIVERTER_VALVE_OVERBOARD_RELAY_ID
  #define YB_DIVERTER_VALVE_OVERBOARD_RELAY_ID 4
#endif

#ifndef YB_DIVERTER_VALVE_OVERBOARD_RELAY_INVERTED
  #define YB_DIVERTER_VALVE_OVERBOARD_RELAY_INVERTED false
#endif

#ifndef YB_DIVERTER_VALVE_RELAY_CHANGE_INTERVAL
  #define YB_DIVERTER_VALVE_RELAY_CHANGE_INTERVAL 1000
#endif

#ifndef YB_FLUSH_VALVE_CONTROL
  #define YB_FLUSH_VALVE_CONTROL "MANUAL"
#endif

#ifndef YB_FLUSH_VALVE_RELAY_ID
  #define YB_FLUSH_VALVE_RELAY_ID 4
#endif

#ifndef YB_FLUSH_VALVE_RELAY_INVERTED
  #define YB_FLUSH_VALVE_RELAY_INVERTED false
#endif

#ifndef YB_FLUSH_VALVE_SERVO_ID
  #define YB_FLUSH_VALVE_SERVO_ID 2
#endif

#ifndef YB_FLUSH_VALVE_OPEN_ANGLE
  #define YB_FLUSH_VALVE_OPEN_ANGLE 35
#endif

#ifndef YB_FLUSH_VALVE_CLOSE_ANGLE
  #define YB_FLUSH_VALVE_CLOSE_ANGLE 125
#endif

#ifndef YB_COOLING_FAN_CONTROL
  #define YB_COOLING_FAN_CONTROL "MANUAL"
#endif

#ifndef YB_COOLING_FAN_RELAY_ID
  #define YB_COOLING_FAN_RELAY_ID 5
#endif

#ifndef YB_COOLING_FAN_RELAY_INVERTED
  #define YB_COOLING_FAN_RELAY_INVERTED false
#endif

#ifndef YB_HAS_MEMBRANE_PRESSURE_SENSOR
  #define YB_HAS_MEMBRANE_PRESSURE_SENSOR false
#endif

#ifndef YB_MEMBRANE_PRESSURE_SENSOR_MIN
  #define YB_MEMBRANE_PRESSURE_SENSOR_MIN 0.0
#endif

#ifndef YB_MEMBRANE_PRESSURE_SENSOR_MAX
  // Bar
  #define YB_MEMBRANE_PRESSURE_SENSOR_MAX 68.9476
#endif

#ifndef YB_HAS_FILTER_PRESSURE_SENSOR
  #define YB_HAS_FILTER_PRESSURE_SENSOR false
#endif

#ifndef YB_FILTER_PRESSURE_SENSOR_MIN
  #define YB_FILTER_PRESSURE_SENSOR_MIN 0.0
#endif

#ifndef YB_FILTER_PRESSURE_SENSOR_MAX
  // Bar
  #define YB_FILTER_PRESSURE_SENSOR_MAX 3.44738
#endif

#ifndef YB_HAS_PRODUCT_TDS_SENSOR
  #define YB_HAS_PRODUCT_TDS_SENSOR false
#endif

#ifndef YB_PRODUCT_TDS_SENSOR_OFFSET
  #define YB_PRODUCT_TDS_SENSOR_OFFSET 0.0
#endif

#ifndef YB_HAS_BRINE_TDS_SENSOR
  #define YB_HAS_BRINE_TDS_SENSOR false
#endif

#ifndef YB_BRINE_TDS_SENSOR_OFFSET
  #define YB_BRINE_TDS_SENSOR_OFFSET 0.0
#endif

#ifndef YB_HAS_PRODUCT_FLOW_SENSOR
  #define YB_HAS_PRODUCT_FLOW_SENSOR false
#endif

#ifndef YB_PRODUCT_FLOWMETER_PPL
  #define YB_PRODUCT_FLOWMETER_PPL 1260.0
#endif

#ifndef YB_HAS_BRINE_FLOW_SENSOR
  #define YB_HAS_BRINE_FLOW_SENSOR false
#endif

#ifndef YB_BRINE_FLOWMETER_PPL
  #define YB_BRINE_FLOWMETER_PPL 1260.0
#endif

#ifndef YB_MOTOR_TEMPERATURE_SENSOR_TYPE
  #define YB_MOTOR_TEMPERATURE_SENSOR_TYPE "NONE"
#endif

#ifndef YB_MOTOR_TEMPERATURE_MQTT_PATH
  #define YB_MOTOR_TEMPERATURE_MQTT_PATH ""
#endif

#ifndef YB_WATER_TEMPERATURE_SENSOR_TYPE
  #define YB_WATER_TEMPERATURE_SENSOR_TYPE "NONE"
#endif

#ifndef YB_WATER_TEMPERATURE_MQTT_PATH
  #define YB_WATER_TEMPERATURE_MQTT_PATH ""
#endif

#ifndef YB_TANK_LEVEL_SENSOR_TYPE
  #define YB_TANK_LEVEL_SENSOR_TYPE "EXTERNAL"
#endif

#ifndef YB_TANK_LEVEL_MQTT_PATH
  #define YB_TANK_LEVEL_MQTT_PATH ""
#endif

#ifndef YB_BATTERY_LEVEL_SENSOR_TYPE
  #define YB_BATTERY_LEVEL_SENSOR_TYPE "EXTERNAL"
#endif

#ifndef YB_BATTERY_LEVEL_MQTT_PATH
  #define YB_BATTERY_LEVEL_MQTT_PATH ""
#endif

#ifndef YB_ENABLE_MEMBRANE_PRESSURE_HIGH_CHECK
  #define YB_ENABLE_MEMBRANE_PRESSURE_HIGH_CHECK true
#endif

#ifndef YB_MEMBRANE_PRESSURE_HIGH_THRESHOLD
  // Bar
  #define YB_MEMBRANE_PRESSURE_HIGH_THRESHOLD 62.0
#endif

#ifndef YB_MEMBRANE_PRESSURE_HIGH_DELAY
  #define YB_MEMBRANE_PRESSURE_HIGH_DELAY 2000
#endif

#ifndef YB_ENABLE_MEMBRANE_PRESSURE_LOW_CHECK
  #define YB_ENABLE_MEMBRANE_PRESSURE_LOW_CHECK true
#endif

#ifndef YB_MEMBRANE_PRESSURE_LOW_THRESHOLD
  // Bar
  #define YB_MEMBRANE_PRESSURE_LOW_THRESHOLD 48.25
#endif

#ifndef YB_MEMBRANE_PRESSURE_LOW_DELAY
  #define YB_MEMBRANE_PRESSURE_LOW_DELAY 2000
#endif

#ifndef YB_ENABLE_FILTER_PRESSURE_HIGH_CHECK
  #define YB_ENABLE_FILTER_PRESSURE_HIGH_CHECK true
#endif

#ifndef YB_FILTER_PRESSURE_HIGH_THRESHOLD
  // Bar
  #define YB_FILTER_PRESSURE_HIGH_THRESHOLD 4.0
#endif

#ifndef YB_FILTER_PRESSURE_HIGH_DELAY
  #define YB_FILTER_PRESSURE_HIGH_DELAY 2000
#endif

#ifndef YB_ENABLE_FILTER_PRESSURE_LOW_CHECK
  #define YB_ENABLE_FILTER_PRESSURE_LOW_CHECK true
#endif

#ifndef YB_FILTER_PRESSURE_LOW_THRESHOLD
  // Bar
  #define YB_FILTER_PRESSURE_LOW_THRESHOLD 0.20
#endif

#ifndef YB_FILTER_PRESSURE_LOW_DELAY
  #define YB_FILTER_PRESSURE_LOW_DELAY 2000
#endif

#ifndef YB_ENABLE_PRODUCT_FLOWRATE_HIGH_CHECK
  #define YB_ENABLE_PRODUCT_FLOWRATE_HIGH_CHECK true
#endif

#ifndef YB_PRODUCT_FLOWRATE_HIGH_THRESHOLD
  #define YB_PRODUCT_FLOWRATE_HIGH_THRESHOLD 165.0
#endif

#ifndef YB_PRODUCT_FLOWRATE_HIGH_DELAY
  #define YB_PRODUCT_FLOWRATE_HIGH_DELAY (10 * 1000)
#endif

#ifndef YB_ENABLE_PRODUCT_FLOWRATE_LOW_CHECK
  #define YB_ENABLE_PRODUCT_FLOWRATE_LOW_CHECK true
#endif

#ifndef YB_PRODUCT_FLOWRATE_LOW_THRESHOLD
  #define YB_PRODUCT_FLOWRATE_LOW_THRESHOLD 120.0
#endif

#ifndef YB_PRODUCT_FLOWRATE_LOW_DELAY
  #define YB_PRODUCT_FLOWRATE_LOW_DELAY (10 * 1000)
#endif

#ifndef YB_ENABLE_RUN_TOTAL_FLOWRATE_LOW_CHECK
  #define YB_ENABLE_RUN_TOTAL_FLOWRATE_LOW_CHECK true
#endif

#ifndef YB_RUN_TOTAL_FLOWRATE_LOW_THRESHOLD
  #define YB_RUN_TOTAL_FLOWRATE_LOW_THRESHOLD 300.0
#endif

#ifndef YB_RUN_TOTAL_FLOWRATE_LOW_DELAY
  #define YB_RUN_TOTAL_FLOWRATE_LOW_DELAY 2500
#endif

#ifndef YB_ENABLE_PICKLE_TOTAL_FLOWRATE_LOW_CHECK
  #define YB_ENABLE_PICKLE_TOTAL_FLOWRATE_LOW_CHECK true
#endif

#ifndef YB_PICKLE_TOTAL_FLOWRATE_LOW_THRESHOLD
  #define YB_PICKLE_TOTAL_FLOWRATE_LOW_THRESHOLD 300.0
#endif

#ifndef YB_PICKLE_TOTAL_FLOWRATE_LOW_DELAY
  #define YB_PICKLE_TOTAL_FLOWRATE_LOW_DELAY 5000
#endif

#ifndef YB_ENABLE_DIVERTER_VALVE_CLOSED_CHECK
  #define YB_ENABLE_DIVERTER_VALVE_CLOSED_CHECK true
#endif

#ifndef YB_DIVERTER_VALVE_CLOSED_FLOWRATE_HIGH_THRESHOLD
  #define YB_DIVERTER_VALVE_CLOSED_FLOWRATE_HIGH_THRESHOLD 400
#endif

#ifndef YB_DIVERTER_VALVE_CLOSED_DELAY
  #define YB_DIVERTER_VALVE_CLOSED_DELAY 5000
#endif

#ifndef YB_ENABLE_PRODUCT_SALINITY_HIGH_CHECK
  #define YB_ENABLE_PRODUCT_SALINITY_HIGH_CHECK true
#endif

#ifndef YB_PRODUCT_SALINITY_HIGH_THRESHOLD
  #define YB_PRODUCT_SALINITY_HIGH_THRESHOLD 500.0
#endif

#ifndef YB_PRODUCT_SALINITY_HIGH_DELAY
  #define YB_PRODUCT_SALINITY_HIGH_DELAY 1000
#endif

#ifndef YB_ENABLE_MOTOR_TEMPERATURE_CHECK
  #define YB_ENABLE_MOTOR_TEMPERATURE_CHECK true
#endif

#ifndef YB_MOTOR_TEMPERATURE_HIGH_THRESHOLD
  #define YB_MOTOR_TEMPERATURE_HIGH_THRESHOLD 65.0
#endif

#ifndef YB_MOTOR_TEMPERATURE_HIGH_DELAY
  #define YB_MOTOR_TEMPERATURE_HIGH_DELAY 1000
#endif

#ifndef YB_ENABLE_FLUSH_FLOWRATE_LOW_CHECK
  #define YB_ENABLE_FLUSH_FLOWRATE_LOW_CHECK true
#endif

#ifndef YB_FLUSH_FLOWRATE_LOW_THRESHOLD
  #define YB_FLUSH_FLOWRATE_LOW_THRESHOLD 100.0
#endif

#ifndef YB_FLUSH_FLOWRATE_LOW_DELAY
  #define YB_FLUSH_FLOWRATE_LOW_DELAY 2500
#endif

#ifndef YB_ENABLE_FLUSH_FILTER_PRESSURE_LOW_CHECK
  #define YB_ENABLE_FLUSH_FILTER_PRESSURE_LOW_CHECK true
#endif

#ifndef YB_FLUSH_FILTER_PRESSURE_LOW_THRESHOLD
  // Bar
  #define YB_FLUSH_FILTER_PRESSURE_LOW_THRESHOLD 1.0
#endif

#ifndef YB_FLUSH_FILTER_PRESSURE_LOW_DELAY
  #define YB_FLUSH_FILTER_PRESSURE_LOW_DELAY 2500
#endif

#ifndef YB_ENABLE_FLUSH_VALVE_OFF_CHECK
  #define YB_ENABLE_FLUSH_VALVE_OFF_CHECK true
#endif

#ifndef YB_FLUSH_VALVE_OFF_THRESHOLD
  // Bar
  #define YB_FLUSH_VALVE_OFF_THRESHOLD 0.13
#endif

#ifndef YB_FLUSH_VALVE_OFF_DELAY
  #define YB_FLUSH_VALVE_OFF_DELAY (15 * 1000)
#endif

#ifndef YB_ENABLE_FLUSH_TANK_LEVEL_LOW_CHECK
  #define YB_ENABLE_FLUSH_TANK_LEVEL_LOW_CHECK true
#endif

#ifndef YB_FLUSH_TANK_LEVEL_LOW_THRESHOLD
  // 0 = empty, 1 = full
  #define YB_FLUSH_TANK_LEVEL_LOW_THRESHOLD 0.10
#endif

#ifndef YB_FLUSH_TANK_LEVEL_LOW_DELAY
  #define YB_FLUSH_TANK_LEVEL_LOW_DELAY 1000
#endif

#ifndef YB_ENABLE_TANK_LEVEL_FULL_CHECK
  #define YB_ENABLE_TANK_LEVEL_FULL_CHECK true
#endif

#ifndef YB_TANK_LEVEL_FULL_THRESHOLD
  // 0 = empty, 1 = full
  #define YB_TANK_LEVEL_FULL_THRESHOLD 0.99
#endif

#ifndef YB_TANK_LEVEL_FULL_DELAY
  #define YB_TANK_LEVEL_FULL_DELAY 1000
#endif

#ifndef YB_ENABLE_BATTERY_LEVEL_LOW_CHECK
  #define YB_ENABLE_BATTERY_LEVEL_LOW_CHECK true
#endif

#ifndef YB_BATTERY_LEVEL_LOW_THRESHOLD
  // 0 = empty, 1 = full
  #define YB_BATTERY_LEVEL_LOW_THRESHOLD 0.35
#endif

#ifndef YB_FLUSH_TIMEOUT
  #define YB_FLUSH_TIMEOUT (10 * 60 * 1000)
#endif

#ifndef YB_MEMBRANE_PRESSURE_TIMEOUT
  #define YB_MEMBRANE_PRESSURE_TIMEOUT (60 * 1000)
#endif

#ifndef YB_PRODUCT_FLOWRATE_TIMEOUT
  #define YB_PRODUCT_FLOWRATE_TIMEOUT (2 * 60 * 1000)
#endif

#ifndef YB_PRODUCT_SALINITY_TIMEOUT
  #define YB_PRODUCT_SALINITY_TIMEOUT (5 * 60 * 1000)
#endif

#ifndef YB_MEMBRANE_PRESSURE_STABILIZATION_TIME
  #define YB_MEMBRANE_PRESSURE_STABILIZATION_TIME 5000
#endif

#ifndef YB_PRODUCT_FLOWRATE_STABILIZATION_TIME
  #define YB_PRODUCT_FLOWRATE_STABILIZATION_TIME 5000
#endif

#ifndef YB_PRODUCT_SALINITY_STABILIZATION_TIME
  #define YB_PRODUCT_SALINITY_STABILIZATION_TIME 5000
#endif

#ifndef YB_PRODUCTION_RUNTIME_TIMEOUT
  #define YB_PRODUCTION_RUNTIME_TIMEOUT (12 * 60 * 60 * 1000)
#endif

//
// All persisted Brineomatic settings live here.  The macros above are the
// single source of truth for the factory defaults, which are baked into each
// member's initializer below.  Brineomatic exposes a public `defaults` instance
// (used as the fallback when loading config JSON) and a private `_config`
// instance which holds the settings as they are actually used.
//
struct BrineomaticConfig {
  String gaugeOrder = "";

  String autoflushMode = YB_AUTOFLUSH_MODE;
  float autoflushSalinity = YB_AUTOFLUSH_SALINITY;
  uint32_t autoflushDuration = YB_AUTOFLUSH_DURATION;
  float autoflushVolume = YB_AUTOFLUSH_VOLUME;
  uint32_t autoflushInterval = YB_AUTOFLUSH_INTERVAL;
  bool autoflushUseHighPressureMotor = YB_AUTOFLUSH_USE_HIGH_PRESSURE_MOTOR;

  float tankCapacity = YB_TANK_CAPACITY; // Liters

  String temperatureUnits = YB_TEMPERATURE_UNITS;
  String pressureUnits = YB_PRESSURE_UNITS;
  String volumeUnits = YB_VOLUME_UNITS;
  String flowrateUnits = YB_FLOWRATE_UNITS;

  String successMelody = YB_SUCCESS_MELODY;
  String errorMelody = YB_ERROR_MELODY;

  String boostPumpControl = YB_BOOST_PUMP_CONTROL;
  uint8_t boostPumpRelayId = YB_BOOST_PUMP_RELAY_ID;
  bool boostPumpRelayInverted = YB_BOOST_PUMP_RELAY_INVERTED;
  uint32_t boostPumpDelay = YB_BOOST_PUMP_DELAY_MS;

  String highPressurePumpControl = YB_HIGH_PRESSURE_PUMP_CONTROL;
  uint8_t highPressureRelayId = YB_HIGH_PRESSURE_RELAY_ID;
  bool highPressureRelayInverted = YB_HIGH_PRESSURE_RELAY_INVERTED;
  String highPressurePumpModbusDevice = YB_HIGH_PRESSURE_PUMP_MODBUS_DEVICE;
  uint8_t highPressurePumpModbusSlaveId = YB_HIGH_PRESSURE_PUMP_MODBUS_SLAVE_ID;
  float highPressurePumpModbusFrequency = YB_HIGH_PRESSURE_PUMP_MODBUS_FREQUENCY;
  uint32_t highPressurePumpDelay = YB_HIGH_PRESSURE_PUMP_DELAY_MS;

  String highPressureValveControl = YB_HIGH_PRESSURE_VALVE_CONTROL;
  uint8_t highPressureValveStepperId = YB_HIGH_PRESSURE_VALVE_STEPPER_ID;
  float highPressureValveStepperStepAngle = YB_HIGH_PRESSURE_VALVE_STEPPER_STEP_ANGLE;
  float highPressureValveStepperGearRatio = YB_HIGH_PRESSURE_VALVE_STEPPER_GEAR_RATIO;
  float highPressureValveStepperCloseAngle = YB_HIGH_PRESSURE_VALVE_STEPPER_CLOSE_ANGLE;
  float highPressureValveStepperCloseSpeed = YB_HIGH_PRESSURE_VALVE_STEPPER_CLOSE_SPEED;
  float highPressureValveStepperOpenAngle = YB_HIGH_PRESSURE_VALVE_STEPPER_OPEN_ANGLE;
  float highPressureValveStepperOpenSpeed = YB_HIGH_PRESSURE_VALVE_STEPPER_OPEN_SPEED;
  uint8_t highPressureValveStepperRunCurrent = YB_HIGH_PRESSURE_VALVE_STEPPER_RUN_CURRENT;
  uint8_t highPressureValveStepperHomeCurrent = YB_HIGH_PRESSURE_VALVE_STEPPER_HOME_CURRENT;
  bool highPressureStepperInverted = YB_HIGH_PRESSURE_STEPPER_INVERTED;
  float membranePressureTarget = YB_MEMBRANE_PRESSURE_TARGET;

  String diverterValveControl = YB_DIVERTER_VALVE_CONTROL;
  uint8_t diverterValveRelayId = YB_DIVERTER_VALVE_RELAY_ID;
  bool diverterValveRelayInverted = YB_DIVERTER_VALVE_RELAY_INVERTED;
  uint8_t diverterValveServoId = YB_DIVERTER_VALVE_SERVO_ID;
  float diverterValveOpenAngle = YB_DIVERTER_VALVE_OPEN_ANGLE;
  float diverterValveCloseAngle = YB_DIVERTER_VALVE_CLOSE_ANGLE;
  uint8_t diverterValveTankRelayId = YB_DIVERTER_VALVE_TANK_RELAY_ID;
  bool diverterValveTankRelayInverted = YB_DIVERTER_VALVE_TANK_RELAY_INVERTED;
  uint8_t diverterValveOverboardRelayId = YB_DIVERTER_VALVE_OVERBOARD_RELAY_ID;
  bool diverterValveOverboardRelayInverted = YB_DIVERTER_VALVE_OVERBOARD_RELAY_INVERTED;
  uint32_t diverterValveRelayChangeInterval = YB_DIVERTER_VALVE_RELAY_CHANGE_INTERVAL;

  String flushValveControl = YB_FLUSH_VALVE_CONTROL;
  uint8_t flushValveRelayId = YB_FLUSH_VALVE_RELAY_ID;
  bool flushValveRelayInverted = YB_FLUSH_VALVE_RELAY_INVERTED;
  uint8_t flushValveServoId = YB_FLUSH_VALVE_SERVO_ID;
  float flushValveOpenAngle = YB_FLUSH_VALVE_OPEN_ANGLE;
  float flushValveCloseAngle = YB_FLUSH_VALVE_CLOSE_ANGLE;

  String coolingFanControl = YB_COOLING_FAN_CONTROL;
  uint8_t coolingFanRelayId = YB_COOLING_FAN_RELAY_ID;
  bool coolingFanRelayInverted = YB_FLUSH_VALVE_RELAY_INVERTED;
  float coolingFanOnTemperature = YB_COOLING_FAN_ON_TEMPERATURE;   // Celcius
  float coolingFanOffTemperature = YB_COOLING_FAN_OFF_TEMPERATURE; // Celcius

  bool hasMembranePressureSensor = YB_HAS_MEMBRANE_PRESSURE_SENSOR;
  float membranePressureSensorMin = YB_MEMBRANE_PRESSURE_SENSOR_MIN;
  float membranePressureSensorMax = YB_MEMBRANE_PRESSURE_SENSOR_MAX;

  bool hasFilterPressureSensor = YB_HAS_FILTER_PRESSURE_SENSOR;
  float filterPressureSensorMin = YB_FILTER_PRESSURE_SENSOR_MIN;
  float filterPressureSensorMax = YB_FILTER_PRESSURE_SENSOR_MAX;

  bool hasProductTDSSensor = YB_HAS_PRODUCT_TDS_SENSOR;
  float productTDSSensorOffset = YB_PRODUCT_TDS_SENSOR_OFFSET;

  bool hasBrineTDSSensor = YB_HAS_BRINE_TDS_SENSOR;
  float brineTDSSensorOffset = YB_BRINE_TDS_SENSOR_OFFSET;

  bool hasProductFlowSensor = YB_HAS_PRODUCT_FLOW_SENSOR;
  float productFlowmeterPPL = YB_PRODUCT_FLOWMETER_PPL;

  bool hasBrineFlowSensor = YB_HAS_BRINE_FLOW_SENSOR;
  float brineFlowmeterPPL = YB_BRINE_FLOWMETER_PPL;

  String motorTemperatureSensorType = YB_MOTOR_TEMPERATURE_SENSOR_TYPE;
  String motorTemperatureMqttPath = YB_MOTOR_TEMPERATURE_MQTT_PATH;
  String waterTemperatureSensorType = YB_WATER_TEMPERATURE_SENSOR_TYPE;
  String waterTemperatureMqttPath = YB_WATER_TEMPERATURE_MQTT_PATH;
  String tankLevelSensorType = YB_TANK_LEVEL_SENSOR_TYPE;
  String tankLevelMqttPath = YB_TANK_LEVEL_MQTT_PATH;
  String batteryLevelSensorType = YB_BATTERY_LEVEL_SENSOR_TYPE;
  String batteryLevelMqttPath = YB_BATTERY_LEVEL_MQTT_PATH;

  uint32_t flushTimeout = YB_FLUSH_TIMEOUT;                                              // timeout for flush cycle in ms
  uint32_t membranePressureTimeout = YB_MEMBRANE_PRESSURE_TIMEOUT;                       // timeout for membrane pressure to stabilize in ms
  uint32_t productFlowrateTimeout = YB_PRODUCT_FLOWRATE_TIMEOUT;                         // timeout for product flowrate to stabilize in ms
  uint32_t productSalinityTimeout = YB_PRODUCT_SALINITY_TIMEOUT;                         // timeout for salinity to stabilize in ms
  uint32_t membranePressureStabilizationTime = YB_MEMBRANE_PRESSURE_STABILIZATION_TIME; // time at target to be considered stable in ms
  uint32_t productFlowrateStabilizationTime = YB_PRODUCT_FLOWRATE_STABILIZATION_TIME;    // time at target to be considered stable in ms
  uint32_t productSalinityStabilizationTime = YB_PRODUCT_SALINITY_STABILIZATION_TIME;    // time at target to be considered stable in ms
  uint32_t productionRuntimeTimeout = YB_PRODUCTION_RUNTIME_TIMEOUT;                     // maximum length of run in ms

  bool enableMembranePressureHighCheck = YB_ENABLE_MEMBRANE_PRESSURE_HIGH_CHECK;
  float membranePressureHighThreshold = YB_MEMBRANE_PRESSURE_HIGH_THRESHOLD;
  uint32_t membranePressureHighDelay = YB_MEMBRANE_PRESSURE_HIGH_DELAY;

  bool enableMembranePressureLowCheck = YB_ENABLE_MEMBRANE_PRESSURE_LOW_CHECK;
  float membranePressureLowThreshold = YB_MEMBRANE_PRESSURE_LOW_THRESHOLD;
  uint32_t membranePressureLowDelay = YB_MEMBRANE_PRESSURE_LOW_DELAY;

  bool enableFilterPressureHighCheck = YB_ENABLE_FILTER_PRESSURE_HIGH_CHECK;
  float filterPressureHighThreshold = YB_FILTER_PRESSURE_HIGH_THRESHOLD;
  uint32_t filterPressureHighDelay = YB_FILTER_PRESSURE_HIGH_DELAY;

  bool enableFilterPressureLowCheck = YB_ENABLE_FILTER_PRESSURE_LOW_CHECK;
  float filterPressureLowThreshold = YB_FILTER_PRESSURE_LOW_THRESHOLD;
  uint32_t filterPressureLowDelay = YB_FILTER_PRESSURE_LOW_DELAY;

  bool enableProductFlowrateHighCheck = YB_ENABLE_PRODUCT_FLOWRATE_HIGH_CHECK;
  float productFlowrateHighThreshold = YB_PRODUCT_FLOWRATE_HIGH_THRESHOLD;
  uint32_t productFlowrateHighDelay = YB_PRODUCT_FLOWRATE_HIGH_DELAY;

  bool enableProductFlowrateLowCheck = YB_ENABLE_PRODUCT_FLOWRATE_LOW_CHECK;
  float productFlowrateLowThreshold = YB_PRODUCT_FLOWRATE_LOW_THRESHOLD;
  uint32_t productFlowrateLowDelay = YB_PRODUCT_FLOWRATE_LOW_DELAY;

  bool enableRunTotalFlowrateLowCheck = YB_ENABLE_RUN_TOTAL_FLOWRATE_LOW_CHECK;
  float runTotalFlowrateLowThreshold = YB_RUN_TOTAL_FLOWRATE_LOW_THRESHOLD;
  uint32_t runTotalFlowrateLowDelay = YB_RUN_TOTAL_FLOWRATE_LOW_DELAY;

  bool enablePickleTotalFlowrateLowCheck = YB_ENABLE_PICKLE_TOTAL_FLOWRATE_LOW_CHECK;
  float pickleTotalFlowrateLowThreshold = YB_PICKLE_TOTAL_FLOWRATE_LOW_THRESHOLD;
  uint32_t pickleTotalFlowrateLowDelay = YB_PICKLE_TOTAL_FLOWRATE_LOW_DELAY;

  bool enableDiverterValveClosedCheck = YB_ENABLE_DIVERTER_VALVE_CLOSED_CHECK;
  float diverterValveClosedFlowrateHighThreshold = YB_DIVERTER_VALVE_CLOSED_FLOWRATE_HIGH_THRESHOLD;
  float diverterValveClosedDelay = YB_DIVERTER_VALVE_CLOSED_DELAY;

  bool enableProductSalinityHighCheck = YB_ENABLE_PRODUCT_SALINITY_HIGH_CHECK;
  float productSalinityHighThreshold = YB_PRODUCT_SALINITY_HIGH_THRESHOLD;
  uint32_t productSalinityHighDelay = YB_PRODUCT_SALINITY_HIGH_DELAY;

  bool enableMotorTemperatureCheck = YB_ENABLE_MOTOR_TEMPERATURE_CHECK;
  float motorTemperatureHighThreshold = YB_MOTOR_TEMPERATURE_HIGH_THRESHOLD;
  uint32_t motorTemperatureHighDelay = YB_MOTOR_TEMPERATURE_HIGH_DELAY;

  bool enableFlushFlowrateLowCheck = YB_ENABLE_FLUSH_FLOWRATE_LOW_CHECK;
  float flushFlowrateLowThreshold = YB_FLUSH_FLOWRATE_LOW_THRESHOLD;
  uint32_t flushFlowrateLowDelay = YB_FLUSH_FLOWRATE_LOW_DELAY;

  bool enableFlushFilterPressureLowCheck = YB_ENABLE_FLUSH_FILTER_PRESSURE_LOW_CHECK;
  float flushFilterPressureLowThreshold = YB_FLUSH_FILTER_PRESSURE_LOW_THRESHOLD;
  uint32_t flushFilterPressureLowDelay = YB_FLUSH_FILTER_PRESSURE_LOW_DELAY;

  bool enableFlushValveOffCheck = YB_ENABLE_FLUSH_VALVE_OFF_CHECK;
  float flushValveOffThreshold = YB_FLUSH_VALVE_OFF_THRESHOLD;
  uint32_t flushValveOffDelay = YB_FLUSH_VALVE_OFF_DELAY;

  bool enableFlushTankLevelLowCheck = YB_ENABLE_FLUSH_TANK_LEVEL_LOW_CHECK;
  float flushTankLevelLowThreshold = YB_FLUSH_TANK_LEVEL_LOW_THRESHOLD;
  uint32_t flushTankLevelLowDelay = YB_FLUSH_TANK_LEVEL_LOW_DELAY;

  bool enableTankLevelFullCheck = YB_ENABLE_TANK_LEVEL_FULL_CHECK;
  float tankLevelFullThreshold = YB_TANK_LEVEL_FULL_THRESHOLD;
  uint32_t tankLevelFullDelay = YB_TANK_LEVEL_FULL_DELAY;

  bool enableBatteryLevelLowCheck = YB_ENABLE_BATTERY_LEVEL_LOW_CHECK;
  float batteryLevelLowThreshold = YB_BATTERY_LEVEL_LOW_THRESHOLD;
};

#endif // BRINEOMATIC_CONFIG_H