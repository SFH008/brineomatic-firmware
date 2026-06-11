/*
  Yarrboard

  Author: Zach Hoeken <hoeken@gmail.com>
  Website: https://github.com/hoeken/yarrboard
  License: GPLv3
*/

#include "config.h"

#include "brineomatic.h"
#include "channels/RelayChannel.h"
#include "channels/ServoChannel.h"
#include "channels/StepperChannel.h"
#include "controllers/RelayController.h"
#include "controllers/ServoController.h"
#include "controllers/StepperController.h"
#include "etl/deque.h"
#include <Arduino.h>
#include <ConfigManager.h>
#include <YarrboardApp.h>
#include <YarrboardDebug.h>

Brineomatic::Brineomatic(YarrboardApp& app, RelayController& relays, ServoController& servos, StepperController& steppers) : _app(app),
                                                                                                                             _relays(relays),
                                                                                                                             _servos(servos),
                                                                                                                             _steppers(steppers),
                                                                                                                             motorTemperatureOneWire(),
                                                                                                                             motorTemperatureSensor(),
                                                                                                                             waterTemperatureOneWire(),
                                                                                                                             waterTemperatureSensor(),
                                                                                                                             _adc(YB_ADS1115_ADDRESS)
{
  // _config is value-initialized to the factory defaults via BrineomaticConfig's
  // in-class member initializers; BrineomaticController overlays any saved settings.
}

void Brineomatic::init()
{
  // enabled or no
  if (_app.config.preferences.isKey("bomPickled"))
    isPickled = _app.config.preferences.getBool("bomPickled");
  else
    isPickled = false;

  if (_app.config.preferences.isKey("bomPickledOn"))
    pickledOnTimestamp = _app.config.preferences.getLong64("bomPickledOn");
  else
    pickledOnTimestamp = 0;

  if (_app.config.preferences.isKey("bomTotVolume"))
    totalVolume = _app.config.preferences.getFloat("bomTotVolume");
  else
    totalVolume = 0.0;

  if (_app.config.preferences.isKey("bomTotRuntime"))
    totalRuntime = _app.config.preferences.getULong("bomTotRuntime");
  else
    totalRuntime = 0;

  if (_app.config.preferences.isKey("bomTotCycles"))
    totalCycles = _app.config.preferences.getUInt("bomTotCycles");
  else
    totalCycles = 0;

  if (scheduledFlushEnabled()) {
    lastAutoflushTimeMillis = millis();
    lastAutoflushTimeNTP = _app.config.preferences.getLong64("lastautoflush");
  }

  boostPumpOnState = false;
  highPressurePumpOnState = false;
  diverterValveOpenState = true;
  flushValveOpenState = false;
  coolingFanOnState = false;

  currentTankLevel = -1;
  currentBatteryLevel = -1;
  currentWaterTemperature = 25.0;
  currentMotorTemperature = 0.0;
  currentProductFlowrate = 0.0;
  currentBrineFlowrate = 0.0;
  currentVolume = 0.0;
  currentFlushVolume = 0.0;
  currentProductSalinity = 0.0;
  currentBrineSalinity = 0.0;
  currentFilterPressure = 0.0;
  currentMembranePressure = 0.0;
  currentMembranePressureTarget = -1;

  // graph history buffers live in PSRAM
  if (!history.init())
    YBP.println("⚠️ Unable to allocate sensor history buffers in PSRAM.");

  // declare which sensors each cycle tracks
  stats.defineCycle("run", {"water_temperature", "motor_temperature", "product_flowrate", "brine_flowrate", "product_salinity", "brine_salinity", "filter_pressure", "membrane_pressure"});
  stats.defineCycle("flush", {"water_temperature", "motor_temperature", "product_flowrate", "brine_flowrate", "product_salinity", "brine_salinity", "filter_pressure", "membrane_pressure"});
  stats.defineCycle("pickle", {"water_temperature", "motor_temperature", "product_flowrate", "brine_flowrate", "product_salinity", "brine_salinity", "filter_pressure", "membrane_pressure"});
  stats.defineCycle("depickle", {"water_temperature", "motor_temperature", "product_flowrate", "brine_flowrate", "product_salinity", "brine_salinity", "filter_pressure", "membrane_pressure"});

  currentStatus = Status::STARTUP;
  runResult = Result::STARTUP;
  flushResult = Result::STARTUP;
  pickleResult = Result::STARTUP;
  depickleResult = Result::STARTUP;

  // PID settings - Ramp Up
  // KpRamp = 2.2;
  // KiRamp = 0;
  // KdRamp = 0.55;

  // PID Settings - Maintain
  // KpMaintain = 1.50;
  // KiMaintain = 0.02;
  // KdMaintain = 0;

  // PID controller
  // membranePressurePID = QuickPID(&currentMembranePressure, &membranePressurePIDOutput, &currentMembranePressureTarget);
  // membranePressurePID.SetMode(QuickPID::Control::automatic);
  // membranePressurePID.SetAntiWindupMode(QuickPID::iAwMode::iAwClamp);
  // membranePressurePID.SetTunings(KpRamp, KiRamp, KdRamp);
  // membranePressurePID.SetControllerDirection(QuickPID::Action::direct);
  // membranePressurePID.SetOutputLimits(YB_BOM_PID_OUTPUT_MIN, YB_BOM_PID_OUTPUT_MAX);

  this->initChannels();

// DS18B20 Sensor
#if YB_DS18B20_MOTOR_PIN
  if (_config.motorTemperatureSensorType == "DS18B20") {
    motorTemperatureOneWire.begin(YB_DS18B20_MOTOR_PIN);
    motorTemperatureSensor.setOneWire(&motorTemperatureOneWire);
    motorTemperatureSensor.begin();

    // lookup our address
    if (!motorTemperatureSensor.getAddress(motorTemperatureAddress, 0))
      YBP.println("⚠️ Unable to find motor temperature sensor.");
    else {
      motorTemperatureSensor.setResolution(motorTemperatureAddress, 9);
      motorTemperatureSensor.setWaitForConversion(false);
      motorTemperatureSensor.requestTemperatures();
    }
  }
#endif

#if YB_DS18B20_WATER_PIN
  if (_config.waterTemperatureSensorType == "DS18B20") {
    waterTemperatureOneWire.begin(YB_DS18B20_WATER_PIN);
    waterTemperatureSensor.setOneWire(&waterTemperatureOneWire);
    waterTemperatureSensor.begin();

    // lookup our address
    if (!waterTemperatureSensor.getAddress(waterTemperatureAddress, 0))
      YBP.println("⚠️ Unable to find water temperature sensor.");
    else {
      waterTemperatureSensor.setResolution(waterTemperatureAddress, 9);
      waterTemperatureSensor.setWaitForConversion(false);
      waterTemperatureSensor.requestTemperatures();
    }
  }
#endif

#ifdef YB_PRODUCT_FLOWMETER_PIN
  productFlowmeter.begin(YB_PRODUCT_FLOWMETER_PIN, _config.productFlowmeterPPL);
#endif

#ifdef YB_BRINE_FLOWMETER_PIN
  brineFlowmeter.begin(YB_BRINE_FLOWMETER_PIN, _config.brineFlowmeterPPL);
#endif

  gravityTds.setAref(YB_ADS1115_VREF); // reference voltage on ADC
  gravityTds.setAdcRange(15);          // 16 bit ADC, but its differential, so lose 1 bit.
  gravityTds.begin();                  // initialization

  Wire.begin(YB_I2C_SDA_PIN, YB_I2C_SCL_PIN);
  Wire.setClock(YB_I2C_SPEED);
  _adc.begin();
  if (!_adc.isConnected())
    YBP.println("⚠️ ADS1115 Not Found");

  _adc.setMode(1);     // SINGLE SHOT MODE
  _adc.setDataRate(3); // 64 samples per second.

  adcHelper = new ADS1115Helper(YB_ADC_VREF, YB_ADC_GAIN, &_adc, YB_ADS1115_SAMPLES, YB_ADS1115_WINDOW);
  adcHelper->attachReadyPinInterrupt(YB_ADS1115_READY_PIN, FALLING);

  initModbus();
}

void Brineomatic::initModbus()
{
#ifdef YB_HAS_MODBUS
  if (_config.highPressurePumpControl == "MODBUS") {
    if (_config.highPressurePumpModbusDevice == "GD20") {
      gd20 = new GD20Modbus(YB_MODBUS_SERIAL, YB_MODBUS_RX, YB_MODBUS_TX);
      gd20->begin(_config.highPressurePumpModbusSlaveId);

      uint16_t status = gd20->readStatusWord();
      gd20->decodeStatus(status);
    }
  }
#endif
}

void Brineomatic::loop()
{
  // get NTP time when ready.
  if (_app.ntp.isReady() && lastAutoflushTimeNTP == 0) {
    lastAutoflushTimeNTP = _app.ntp.getTime();
    _app.config.preferences.putLong64("lastautoflush", lastAutoflushTimeNTP);
  }

  adcHelper->onLoop();

  measureBrineSalinity();
  measureProductSalinity();
  measureFilterPressure();
  measureMembranePressure();
  measureProductFlowmeter();
  measureBrineFlowmeter();
  measureMotorTemperature();
  measureWaterTemperature();
  manageHighPressureValve();
  manageCoolingFan();
}

void Brineomatic::measureProductFlowmeter()
{
  if (!_config.hasProductFlowSensor)
    return;

#ifdef YB_PRODUCT_FLOWMETER_PIN
  if (productFlowmeter.measure()) {
    float flowrate = productFlowmeter.getFlowrate();
    float volume = productFlowmeter.getVolume();

    if ((hasDiverterValve() && !isDiverterValveOpen()) || !hasDiverterValve()) {
      currentVolume += volume;
      totalVolume += volume;
    }

    setProductFlowrate(flowrate);
  }
#endif
}

void Brineomatic::measureBrineFlowmeter()
{
  if (!_config.hasBrineFlowSensor)
    return;

#ifdef YB_BRINE_FLOWMETER_PIN
  if (brineFlowmeter.measure()) {
    float flowrate = brineFlowmeter.getFlowrate();
    float volume = brineFlowmeter.getVolume();

    // update our volume
    if (isFlushValveOpen())
      currentFlushVolume += volume;

    setBrineFlowrate(flowrate);
  }
#endif
}

void Brineomatic::measureMotorTemperature()
{
#if YB_DS18B20_MOTOR_PIN
  if (_config.motorTemperatureSensorType != "DS18B20")
    return;

  if (motorTemperatureSensor.isConversionComplete()) {
    setMotorTemperature(motorTemperatureSensor.getTempC(motorTemperatureAddress));
    motorTemperatureSensor.requestTemperatures();
  }
#endif
}

void Brineomatic::measureWaterTemperature()
{
#if YB_DS18B20_WATER_PIN
  if (_config.waterTemperatureSensorType != "DS18B20")
    return;

  if (waterTemperatureSensor.isConversionComplete()) {
    setWaterTemperature(waterTemperatureSensor.getTempC(waterTemperatureAddress));
    waterTemperatureSensor.requestTemperatures();
  }
#endif
}

void Brineomatic::measureProductSalinity()
{
  int16_t reading = adcHelper->getAverageReading(YB_PRODUCT_TDS_CHANNEL);
  gravityTds.setTemperature(getWaterTemperature());
  gravityTds.update(reading);
  setProductSalinity(gravityTds.getTdsValue() + _config.productTDSSensorOffset);
}

void Brineomatic::measureBrineSalinity()
{
  int16_t reading = adcHelper->getAverageReading(YB_BRINE_TDS_CHANNEL);
  gravityTds.setTemperature(getWaterTemperature());
  gravityTds.update(reading);
  setBrineSalinity(gravityTds.getTdsValue() + _config.brineTDSSensorOffset);
}

void Brineomatic::measureFilterPressure()
{
  float voltage = adcHelper->getAverageVoltage(YB_LP_SENSOR_CHANNEL);
  float amperage = (voltage / YB_420_RESISTOR) * 1000;

  if (amperage < 3.5) {
    currentFilterPressure = -999;
    return;
  }

  if (amperage < 4.0)
    amperage = 4.0;

  setFilterPressure(map_generic(amperage, 4.0, 20.0, _config.filterPressureSensorMin, _config.filterPressureSensorMax));
}

void Brineomatic::measureMembranePressure()
{
  float voltage = adcHelper->getAverageVoltage(YB_HP_SENSOR_CHANNEL);
  float amperage = (voltage / YB_420_RESISTOR) * 1000;

  if (amperage < 3.5) {
    currentMembranePressure = -999;
    return;
  }

  if (amperage < 4.0)
    amperage = 4.0;

  setMembranePressure(map_generic(amperage, 4.0, 20.0, _config.membranePressureSensorMin, _config.membranePressureSensorMax));
}

void Brineomatic::initChannels()
{
  for (auto& ch : _relays.getChannels()) {
    ch.init(ch.id);
    ch.isEnabled = false;
    ch.defaultState = false;
  }

  for (auto& ch : _servos.getChannels()) {
    ch.init(ch.id);
    ch.isEnabled = false;
  }

  for (auto& ch : _steppers.getChannels()) {
    ch.init(ch.id);
    ch.isEnabled = false;
  }

  if (_config.boostPumpControl.equals("RELAY")) {
    boostPump = _relays.getChannelById(_config.boostPumpRelayId);
    if (boostPump) {
      boostPump->isEnabled = true;
      boostPump->inverted = _config.boostPumpRelayInverted;
      boostPump->setName("Boost Pump");
      boostPump->setKey("boost_pump");
      strncpy(boostPump->type, "water_pump", sizeof(boostPump->type));
    } else
      YBP.printf("Couldnt load bp relay %d\n", _config.boostPumpRelayId);
  }

  if (_config.flushValveControl.equals("SERVO")) {
    flushValveServo = _servos.getChannelById(_config.flushValveServoId);
    flushValveServo->isEnabled = true;
    flushValveServo->setName("Flush Valve");
    flushValveServo->setKey("flush_valve");
  } else if (_config.flushValveControl.equals("RELAY")) {
    flushValve = _relays.getChannelById(_config.flushValveRelayId);
    flushValve->isEnabled = true;
    flushValve->inverted = _config.flushValveRelayInverted;
    flushValve->setName("Flush Valve");
    flushValve->setKey("flush_valve");
    strncpy(flushValve->type, "solenoid", sizeof(flushValve->type));
  }

  if (_config.coolingFanControl.equals("RELAY")) {
    coolingFan = _relays.getChannelById(_config.coolingFanRelayId);
    coolingFan->isEnabled = true;
    coolingFan->inverted = _config.coolingFanRelayInverted;
    coolingFan->setName("Cooling Fan");
    coolingFan->setKey("cooling_fan");
    strncpy(coolingFan->type, "fan", sizeof(coolingFan->type));
  }

  if (_config.highPressurePumpControl.equals("RELAY")) {
    highPressurePump = _relays.getChannelById(_config.highPressureRelayId);
    highPressurePump->isEnabled = true;
    highPressurePump->inverted = _config.highPressureRelayInverted;
    highPressurePump->setName("High Pressure Pump");
    highPressurePump->setKey("hp_pump");
    strncpy(highPressurePump->type, "water_pump", sizeof(highPressurePump->type));
  }

  if (_config.diverterValveControl.equals("SERVO")) {
    diverterValveServo = _servos.getChannelById(_config.diverterValveServoId);
    diverterValveServo->isEnabled = true;
    diverterValveServo->setName("Diverter Valve");
    diverterValveServo->setKey("diverter_valve");
  } else if (_config.diverterValveControl.equals("RELAY")) {
    diverterValveRelay = _relays.getChannelById(_config.diverterValveRelayId);
    diverterValveRelay->isEnabled = true;
    diverterValveRelay->inverted = _config.diverterValveRelayInverted;
    diverterValveRelay->defaultState = true; // diverter valve on = overboard.
    diverterValveRelay->setName("Diverter Valve");
    diverterValveRelay->setKey("diverter_valve");
  } else if (_config.diverterValveControl.equals("DUAL_RELAYS")) {
    diverterValveTankRelay = _relays.getChannelById(_config.diverterValveTankRelayId);
    diverterValveTankRelay->isEnabled = true;
    diverterValveTankRelay->inverted = _config.diverterValveTankRelayInverted;
    diverterValveTankRelay->setName("Diverter Valve Tank");
    diverterValveTankRelay->setKey("diverter_valve_tank");
    diverterValveOverboardRelay = _relays.getChannelById(_config.diverterValveOverboardRelayId);
    diverterValveOverboardRelay->isEnabled = true;
    diverterValveOverboardRelay->inverted = _config.diverterValveOverboardRelayInverted;
    diverterValveOverboardRelay->setName("Diverter Valve Overboard");
    diverterValveOverboardRelay->setKey("diverter_valve_overboard");
  }

  if (_config.highPressureValveControl.equals("STEPPER")) {
    highPressureValveStepper = _steppers.getChannelById(_config.highPressureValveStepperId);
    if (highPressureValveStepper) {
      highPressureValveStepper->isEnabled = true;
      highPressureValveStepper->setName("High Pressure Valve");
      highPressureValveStepper->setKey("hp_valve");

      float stepsPerDegree =
        (YB_STEPPER_MICROSTEPS * _config.highPressureValveStepperGearRatio) /
        _config.highPressureValveStepperStepAngle;
      highPressureValveStepper->setStepsPerDegree(stepsPerDegree);
      highPressureValveStepper->setRunCurrent(_config.highPressureValveStepperRunCurrent);
      highPressureValveStepper->setHomeCurrent(_config.highPressureValveStepperHomeCurrent);
      highPressureValveStepper->setDirectionInverted(_config.highPressureStepperInverted);
    } else {
      YBP.printf("Error: high pressure valve stepper %d not found\n", _config.highPressureValveStepperId);
      _config.highPressureValveControl = "NONE";
    }
  }
}

void Brineomatic::setMembranePressureTarget(float pressure)
{
  currentMembranePressureTarget = pressure;

  // we got a real pressure
  if (pressure >= 0) {
    if (_config.highPressureValveControl.equals("STEPPER")) {
      // static angle mode for now.
      if (pressure > 0) {
        highPressureValveStepper->gotoAngle(
          _config.highPressureValveStepperCloseAngle,
          _config.highPressureValveStepperCloseSpeed);
      } else {
        highPressureValveStepper->gotoAngle(
          _config.highPressureValveStepperOpenAngle,
          _config.highPressureValveStepperOpenSpeed);
      }
    }

    // if (_config.highPressureValveControl.equals("SERVO")) {
    //   membranePressurePID.Initialize();
    //   membranePressurePID.Reset();

    //   // header for debugging.
    //   YBP.println("Membrane Pressure Target,Current Membrane Pressure,Pterm,Iterm,Kterm,Output Sum, PID Output, Servo Angle");
    // }
  }
  // negative target, we're done
  else {
    if (_config.highPressureValveControl.equals("STEPPER")) {
      YBP.println("target <= 0, disable our stepper");
      highPressureValveStepper->gotoAngle(_config.highPressureValveStepperOpenAngle, _config.highPressureValveStepperOpenSpeed);
      highPressureValveStepper->waitUntilStopped();
      highPressureValveStepper->disable();
    }
  }
}

void Brineomatic::idle()
{
  if (currentStatus == Status::MANUAL)
    stopFlag = true;
}

void Brineomatic::manual()
{
  if (currentStatus == Status::IDLE) {
    stopFlag = false;
    currentStatus = Status::MANUAL;
  }
}

void Brineomatic::start()
{
  if (currentStatus == Status::IDLE) {
    desiredRuntime = 0;
    desiredVolume = 0;
    currentStatus = Status::RUNNING;
  }
}

void Brineomatic::startDuration(uint32_t duration)
{
  if (currentStatus == Status::IDLE) {
    desiredRuntime = duration;
    desiredVolume = 0;
    currentStatus = Status::RUNNING;
  }
}

void Brineomatic::startVolume(float volume)
{
  if (currentStatus == Status::IDLE) {
    desiredRuntime = 0;
    desiredVolume = volume;
    currentStatus = Status::RUNNING;
  }
}

void Brineomatic::flush()
{
  if (currentStatus == Status::IDLE || currentStatus == Status::PICKLED || currentStatus == Status::STOPPING) {
    desiredFlushDuration = 0;
    desiredFlushVolume = 0;
    currentStatus = Status::FLUSHING;
  }
}

void Brineomatic::flushDuration(uint32_t duration)
{
  if (currentStatus == Status::IDLE || currentStatus == Status::PICKLED || currentStatus == Status::STOPPING) {
    desiredFlushDuration = duration;
    desiredFlushVolume = 0;
    currentStatus = Status::FLUSHING;
  }
}

void Brineomatic::flushVolume(float volume)
{
  if (currentStatus == Status::IDLE || currentStatus == Status::PICKLED || currentStatus == Status::STOPPING) {
    desiredFlushDuration = 0;
    desiredFlushVolume = volume;
    currentStatus = Status::FLUSHING;
  }
}

void Brineomatic::pickle(uint32_t duration)
{
  if (currentStatus == Status::IDLE) {
    pickleDuration = duration;
    currentStatus = Status::PICKLING;
  }
}

void Brineomatic::depickle(uint32_t duration)
{
  if (currentStatus == Status::PICKLED) {
    depickleDuration = duration;
    currentStatus = Status::DEPICKLING;
  }
}

void Brineomatic::stop()
{
  if (currentStatus == Status::RUNNING || currentStatus == Status::FLUSHING || currentStatus == Status::PICKLING || currentStatus == Status::DEPICKLING) {
    stopFlag = true;
  }
}

bool Brineomatic::initializeHardware(bool emergencyStop)
{
  bool isFailure = false;

  YBP.println("Hardware Init Start");

  // immediate turn off here
  if (emergencyStop) {
    disableHighPressurePump();
    disableBoostPump();
  }

  // these arent so important.
  openDiverterValve();
  closeFlushValve();
  disableCoolingFan();

  // actively running, zero out our pressure
  if (currentMembranePressureTarget > 0) {
    setMembranePressureTarget(0);

    if (_config.hasMembranePressureSensor) {
      uint32_t membranePressureStart = millis();
      YBP.println("Waiting for zero pressure.");
      while (getMembranePressure() > 4.5) {
        if (INTERVAL(250))
          YBP.print(".");

        if (millis() - membranePressureStart > _config.membranePressureTimeout) {
          YBP.println("Membrane pressure timeout.");
          isFailure = true;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      YBP.println("\nMembrane Pressure off");
    }

    // turns our high pressure valve controller off
    setMembranePressureTarget(-1);
  }

  // turn off after for a gradual release of pressure
  if (!emergencyStop) {
    disableHighPressurePump();
    disableBoostPump();
  }

  if (_config.highPressureValveControl.equals("STEPPER")) {
    if (highPressureValveStepper->home(_config.highPressureValveStepperOpenSpeed)) {
      YBP.println("Stepper homing OK");
    } else {
      isFailure = true;
      YBP.println("Stepper homing failed.");
    }
  }

  if (isFailure)
    YBP.println("Hardware Init Failed");
  else
    YBP.println("Hardware Init OK");

  return isFailure;
}

bool Brineomatic::postRunFlushEnabled()
{
  if (!hasFlushValve())
    return false;

  return !_config.postRunFlushMode.equals("NONE");
}

bool Brineomatic::scheduledFlushEnabled()
{
  if (!hasFlushValve())
    return false;

  return !_config.scheduledFlushMode.equals("NONE");
}

bool Brineomatic::hasBoostPump()
{
  return !_config.boostPumpControl.equals("NONE");
}

bool Brineomatic::isBoostPumpOn()
{
  return boostPumpOnState;
}

void Brineomatic::enableBoostPump()
{
  if (hasBoostPump()) {
    YBP.println("Boost Pump ON");
    if (_config.boostPumpControl.equals("RELAY"))
      boostPump->setState(true);
  }
  boostPumpOnState = true;
}

void Brineomatic::disableBoostPump()
{
  if (hasBoostPump()) {
    YBP.println("Boost Pump OFF");
    if (_config.boostPumpControl.equals("RELAY"))
      boostPump->setState(false);
  }
  boostPumpOnState = false;
}

bool Brineomatic::hasHighPressurePump()
{
  return !_config.highPressurePumpControl.equals("NONE");
}

bool Brineomatic::isHighPressurePumpOn()
{
  return highPressurePumpOnState;
}

void Brineomatic::enableHighPressurePump()
{
  if (hasHighPressurePump()) {
    YBP.println("High Pressure Pump ON");
    if (_config.highPressurePumpControl.equals("RELAY"))
      highPressurePump->setState(true);
    else if (_config.highPressurePumpControl.equals("MODBUS"))
      modbusEnableHighPressurePump();
  }
  highPressurePumpOnState = true;
}

void Brineomatic::disableHighPressurePump()
{
  if (hasHighPressurePump()) {
    YBP.println("High Pressure Pump OFF");
    if (_config.highPressurePumpControl.equals("RELAY"))
      highPressurePump->setState(false);
    else if (_config.highPressurePumpControl.equals("MODBUS"))
      modbusDisableHighPressurePump();
  }
  highPressurePumpOnState = false;
}

void Brineomatic::modbusEnableHighPressurePump()
{
#ifdef YB_HAS_MODBUS
  if (_config.highPressurePumpModbusDevice.equals("GD20")) {
    YBP.println("GD20 Pump Enable");
    gd20->setFrequency(_config.highPressurePumpModbusFrequency);
    gd20->runMotor();
  }
#endif
}

void Brineomatic::modbusDisableHighPressurePump()
{
#ifdef YB_HAS_MODBUS
  if (_config.highPressurePumpModbusDevice.equals("GD20")) {
    YBP.println("GD20 Pump Disable");
    gd20->stopMotor();
  }
#endif
}

bool Brineomatic::hasDiverterValve()
{
  return !_config.diverterValveControl.equals("NONE");
}

bool Brineomatic::isDiverterValveOpen()
{
  return diverterValveOpenState;
}

void Brineomatic::openDiverterValve()
{
  if (hasDiverterValve()) {
    YBP.println("Diverter Valve Open");
    if (_config.diverterValveControl.equals("SERVO"))
      diverterValveServo->write(_config.diverterValveOpenAngle);
    else if (_config.diverterValveControl.equals("RELAY"))
      diverterValveRelay->setState(true);
    else if (_config.diverterValveControl.equals("DUAL_RELAYS")) {
      diverterValveOverboardRelay->setState(true);
      delay(_config.diverterValveRelayChangeInterval);
      diverterValveTankRelay->setState(false);
    }
  }
  diverterValveOpenState = true;
}

void Brineomatic::closeDiverterValve()
{
  if (hasDiverterValve()) {
    YBP.println("Diverter Valve Closed");
    if (_config.diverterValveControl.equals("SERVO"))
      diverterValveServo->write(_config.diverterValveCloseAngle);
    else if (_config.diverterValveControl.equals("RELAY"))
      diverterValveRelay->setState(false);
    else if (_config.diverterValveControl.equals("DUAL_RELAYS")) {
      diverterValveTankRelay->setState(true);
      delay(_config.diverterValveRelayChangeInterval);
      diverterValveOverboardRelay->setState(false);
    }
  }
  diverterValveOpenState = false;
}

bool Brineomatic::hasFlushValve()
{
  return !_config.flushValveControl.equals("NONE");
}

bool Brineomatic::isFlushValveOpen()
{
  return flushValveOpenState;
}

void Brineomatic::openFlushValve()
{
  if (hasFlushValve()) {
    YBP.println("Flush Valve Open");
    if (_config.flushValveControl.equals("SERVO"))
      flushValveServo->write(_config.flushValveOpenAngle);
    else if (_config.flushValveControl.equals("RELAY"))
      flushValve->setState(true);
  }
  flushValveOpenState = true;
}

void Brineomatic::closeFlushValve()
{
  if (hasFlushValve()) {
    YBP.println("Flush Valve Closed");
    if (_config.flushValveControl.equals("SERVO"))
      flushValveServo->write(_config.flushValveCloseAngle);
    else if (_config.flushValveControl.equals("RELAY"))
      flushValve->setState(false);
  }
  flushValveOpenState = false;
}

bool Brineomatic::hasCoolingFan()
{
  return !_config.coolingFanControl.equals("NONE");
}

bool Brineomatic::isCoolingFanOn()
{
  return coolingFanOnState;
}

void Brineomatic::enableCoolingFan()
{
  if (hasCoolingFan()) {
    // YBP.println("Cooling Fan ON");
    if (_config.coolingFanControl.equals("RELAY"))
      coolingFan->setState(true);
  }
  coolingFanOnState = true;
}

void Brineomatic::disableCoolingFan()
{
  if (hasCoolingFan()) {
    // YBP.println("Cooling Fan OFF");
    if (_config.coolingFanControl.equals("RELAY"))
      coolingFan->setState(false);
  }
  coolingFanOnState = false;
}

void Brineomatic::manageCoolingFan()
{
  if (currentStatus != Status::MANUAL) {
    if (hasCoolingFan() && hasMotorTemperature()) {
      if (getMotorTemperature() >= _config.coolingFanOnTemperature)
        enableCoolingFan();
      else if (getMotorTemperature() <= _config.coolingFanOffTemperature)
        disableCoolingFan();
    }
  }
}

float Brineomatic::getFilterPressure()
{
  return currentFilterPressure;
}

void Brineomatic::setFilterPressure(float pressure)
{
  currentFilterPressure = pressure;
  stats.add("filter_pressure", pressure);
  history.add("filter_pressure", pressure);
}

float Brineomatic::getFilterPressureMinimum()
{
  return _config.filterPressureLowThreshold;
}

float Brineomatic::getMembranePressure()
{
  return currentMembranePressure;
}

void Brineomatic::setMembranePressure(float pressure)
{
  currentMembranePressure = pressure;
  stats.add("membrane_pressure", pressure);
  history.add("membrane_pressure", pressure);
}

float Brineomatic::getMembranePressureMinimum()
{
  return _config.membranePressureLowThreshold;
}

float Brineomatic::getProductFlowrate()
{
  return currentProductFlowrate;
}

void Brineomatic::setProductFlowrate(float flowrate)
{
  currentProductFlowrate = flowrate;
  stats.add("product_flowrate", flowrate);
  history.add("product_flowrate", flowrate);
}

float Brineomatic::getBrineFlowrate()
{
  return currentBrineFlowrate;
}

void Brineomatic::setBrineFlowrate(float flowrate)
{
  currentBrineFlowrate = flowrate;
  stats.add("brine_flowrate", flowrate);
  history.add("brine_flowrate", flowrate);
}

float Brineomatic::getProductFlowrateMinimum()
{
  return _config.productFlowrateLowThreshold;
}

float Brineomatic::getTotalFlowrate()
{
  if (isDiverterValveOpen())
    return getBrineFlowrate();
  else
    return getProductFlowrate() + getBrineFlowrate();
}

float Brineomatic::getVolume()
{
  return currentVolume;
}

float Brineomatic::getFlushVolume()
{
  return currentFlushVolume;
}

float Brineomatic::getTotalVolume()
{
  return totalVolume;
}

uint32_t Brineomatic::getTotalRuntime()
{
  return totalRuntime;
}

uint32_t Brineomatic::getTotalCycles()
{
  return totalCycles;
}

float Brineomatic::getWaterTemperature()
{
  return currentWaterTemperature;
}

void Brineomatic::setWaterTemperature(float temp)
{
  currentWaterTemperature = temp;
  stats.add("water_temperature", temp);
  history.add("water_temperature", temp);
}

void Brineomatic::setTankLevel(float level)
{
  currentTankLevel = level;
  history.add("tank_level", level);
}

void Brineomatic::setBatteryLevel(float level)
{
  currentBatteryLevel = level;
  history.add("battery_level", level);
}

void Brineomatic::setMotorTemperature(float temp)
{
  currentMotorTemperature = temp;
  stats.add("motor_temperature", temp);
  history.add("motor_temperature", temp);
}

float Brineomatic::getMotorTemperature()
{
  return currentMotorTemperature;
}

float Brineomatic::getMotorTemperatureMaximum()
{
  return _config.motorTemperatureHighThreshold;
}

float Brineomatic::getProductSalinity()
{
  return currentProductSalinity;
}

void Brineomatic::setProductSalinity(float salinity)
{
  currentProductSalinity = salinity;
  stats.add("product_salinity", salinity);
  history.add("product_salinity", salinity);
}

float Brineomatic::getBrineSalinity()
{
  return currentBrineSalinity;
}

void Brineomatic::setBrineSalinity(float salinity)
{
  currentBrineSalinity = salinity;
  stats.add("brine_salinity", salinity);
  history.add("brine_salinity", salinity);
}

float Brineomatic::getProductSalinityMaximum()
{
  return _config.productSalinityHighThreshold;
}

float Brineomatic::getTankLevel()
{
  return currentTankLevel;
}

float Brineomatic::getTankCapacity()
{
  return _config.tankCapacity;
}

float Brineomatic::getBatteryLevel()
{
  return currentBatteryLevel;
}

const char* Brineomatic::getTemperatureUnits()
{
  return _config.temperatureUnits.c_str();
}

const char* Brineomatic::getPressureUnits()
{
  return _config.pressureUnits.c_str();
}

const char* Brineomatic::getVolumeUnits()
{
  return _config.volumeUnits.c_str();
}

const char* Brineomatic::getFlowrateUnits()
{
  return _config.flowrateUnits.c_str();
}

const char* Brineomatic::getStatus()
{
  return getStatus(currentStatus);
}

const char* Brineomatic::getStatus(Status status)
{
  if (status == Status::STARTUP)
    return "STARTUP";
  else if (status == Status::MANUAL)
    return "MANUAL";
  else if (status == Status::IDLE)
    return "IDLE";
  else if (status == Status::RUNNING)
    return "RUNNING";
  else if (status == Status::STOPPING)
    return "STOPPING";
  else if (status == Status::FLUSHING)
    return "FLUSHING";
  else if (status == Status::PICKLING)
    return "PICKLING";
  else if (status == Status::DEPICKLING)
    return "DEPICKLING";
  else if (status == Status::PICKLED)
    return "PICKLED";
  else
    return "UNKNOWN";
}

Brineomatic::Result Brineomatic::getRunResult()
{
  return runResult;
}

Brineomatic::Result Brineomatic::getFlushResult()
{
  return flushResult;
}

Brineomatic::Result Brineomatic::getPickleResult()
{
  return pickleResult;
}

Brineomatic::Result Brineomatic::getDepickleResult()
{
  return depickleResult;
}

const char* Brineomatic::resultToString(Result result)
{
  switch (result) {
#define X(name)      \
  case Result::name: \
    return #name;
    BOM_RESULT_LIST
#undef X
    default:
      return "UNKNOWN";
  }
}

uint32_t Brineomatic::getNextFlushCountdown()
{
  if (currentStatus == Status::IDLE && scheduledFlushEnabled()) {
    uint32_t elapsed;
    if (_app.ntp.isReady() && lastAutoflushTimeNTP > 1700000000)
      elapsed = (_app.ntp.getTime() - lastAutoflushTimeNTP) * 1000;
    else
      elapsed = millis() - lastAutoflushTimeMillis;

    return _config.scheduledFlushInterval - elapsed;
  }

  return 0;
}

uint32_t Brineomatic::getRuntimeElapsed()
{
  return millis() - runtimeStart;
}

uint32_t Brineomatic::getFinishCountdown()
{
  if (currentStatus == Status::RUNNING) {
    // are we on a timer?
    if (desiredRuntime > 0) {
      int32_t countdown = desiredRuntime - (millis() - runtimeStart);
      if (countdown > 0)
        return countdown;
    } else if (desiredVolume > 0) {
      float flowrate = getProductFlowrate();
      if (flowrate > 0) {
        float remainingVolume = desiredVolume - currentVolume;
        uint32_t remainingMillis = (remainingVolume / flowrate) * 3600 * 1000;
        return remainingMillis;
      }
    }
    // if we have tank capacity and a flowrate, we can estimate.
    else if (getTankCapacity() > 0 && getProductFlowrate() > 0) {
      float remainingVolume = getTankCapacity() * (1.0 - getTankLevel());
      float flowrate = getProductFlowrate();
      if (flowrate > 0) {
        uint32_t remainingMillis = (remainingVolume / flowrate) * (3600 * 1000);
        return remainingMillis;
      }
    }
  }

  return 0;
}

uint32_t Brineomatic::getFlushElapsed()
{
  return millis() - flushStart;
}

uint32_t Brineomatic::getFlushCountdown()
{
  if (currentStatus != Status::FLUSHING)
    return 0;

  if (desiredFlushDuration) {
    int32_t countdown = desiredFlushDuration - (millis() - flushStart);
    if (countdown > 0)
      return countdown;
  } else if (desiredFlushVolume) {
    float flowrate = getBrineFlowrate();
    if (flowrate > 0) {
      float remainingVolume = desiredFlushVolume - getFlushVolume();
      uint32_t remainingMillis = (remainingVolume / flowrate) * 3600 * 1000;
      return remainingMillis;
    }
  } else {
    int32_t countdown = _config.flushTimeout - (millis() - flushStart);
    if (countdown > 0)
      return countdown;
  }

  return 0;
}

uint32_t Brineomatic::getPickleElapsed()
{
  return millis() - pickleStart;
}

uint32_t Brineomatic::getPickleCountdown()
{
  if (currentStatus == Status::PICKLING) {
    int32_t countdown = pickleDuration - (millis() - pickleStart);
    if (countdown > 0)
      return countdown;
  }

  return 0;
}

uint32_t Brineomatic::getDepickleElapsed()
{
  return millis() - depickleStart;
}

uint32_t Brineomatic::getDepickleCountdown()
{
  if (currentStatus == Status::DEPICKLING) {
    int32_t countdown = depickleDuration - (millis() - depickleStart);
    if (countdown > 0)
      return countdown;
  }

  return 0;
}

bool Brineomatic::hasMotorTemperature()
{
  return _config.motorTemperatureSensorType != "NONE";
}

bool Brineomatic::hasWaterTemperature()
{
  return _config.waterTemperatureSensorType != "NONE";
}

bool Brineomatic::hasHighPressureValve()
{
  return !_config.highPressureValveControl.equals("NONE");
}

bool Brineomatic::hasFilterPressure()
{
  return _config.hasFilterPressureSensor;
}

bool Brineomatic::hasMembranePressure()
{
  return _config.hasMembranePressureSensor;
}

bool Brineomatic::hasProductFlow()
{
  return _config.hasProductFlowSensor;
}

bool Brineomatic::hasBrineFlow()
{
  return _config.hasBrineFlowSensor;
}

bool Brineomatic::hasProductTDS()
{
  return _config.hasProductTDSSensor;
}

bool Brineomatic::hasBrineTDS()
{
  return _config.hasBrineTDSSensor;
}

void Brineomatic::manageHighPressureValve()
{
  //
  // TODO: putting all of this on hold until its time to re-implement PID
  //

  // float angle;

  // if (currentStatus != Status::IDLE) {
  //   if (hasHighPressureValve()) {
  //     if (currentMembranePressureTarget >= 0) {
  //       // only use Ki for tuning once we are close to our target.
  //       if (abs(currentMembranePressureTarget - currentMembranePressure) / currentMembranePressureTarget > 0.05)
  //         membranePressurePID.SetTunings(KpRamp, KiRamp, KdRamp);
  //       else
  //         membranePressurePID.SetTunings(KpMaintain, KpMaintain, KdMaintain);

  //       // run our PID calculations
  //       if (membranePressurePID.Compute()) {
  //         // different max values for the ramp
  //         if (abs(currentMembranePressureTarget - currentMembranePressure) / currentMembranePressureTarget > 0.05)
  //           angle = map(membranePressurePIDOutput, YB_BOM_PID_OUTPUT_MIN, YB_BOM_PID_OUTPUT_MAX, highPressureValveOpenMax, highPressureValveCloseMax);
  //         // smaller max values for maintain.
  //         else
  //           angle = map(membranePressurePIDOutput, YB_BOM_PID_OUTPUT_MIN, YB_BOM_PID_OUTPUT_MAX, highPressureValveMaintainOpenMax, highPressureValveMaintainCloseMax);

  //         // YBP.printf("HP PID | current: %.0f / target: %.0f | p: % .3f / i: % .3f / d: % .3f / sum: % .3f | output: %.0f / angle: %.0f\n", round(currentMembranePressure), round(currentMembranePressureTarget), membranePressurePID.GetPterm(), membranePressurePID.GetIterm(), membranePressurePID.GetDterm(), membranePressurePID.GetOutputSum(), membranePressurePIDOutput, angle);
  //       }
  //     }
  //   }
  // }
}

void Brineomatic::runStateMachine()
{
  switch (currentStatus) {

    //
    // STARTUP
    //
    case Status::STARTUP:
      YBP.println("STARTUP");
      initializeHardware(false);

      if (isPickled)
        currentStatus = Status::PICKLED;
      else
        currentStatus = Status::IDLE;
      break;

    //
    // PICKLED
    //
    case Status::PICKLED:
      break;

    //
    // MANUAL
    //
    case Status::MANUAL:
      if (stopFlag) {
        initializeHardware(false);
        currentStatus = Status::IDLE;
      }

      break;

    //
    // IDLE
    //
    case Status::IDLE:
      if (scheduledFlushEnabled()) {
        uint32_t elapsed;
        if (_app.ntp.isReady() && lastAutoflushTimeNTP > 1700000000)
          elapsed = (_app.ntp.getTime() - lastAutoflushTimeNTP) * 1000;
        else
          elapsed = millis() - lastAutoflushTimeMillis;

        if (elapsed > _config.scheduledFlushInterval) {
          if (_config.scheduledFlushMode.equals("TIME"))
            flushDuration(_config.scheduledFlushDuration);
          else if (_config.scheduledFlushMode.equals("VOLUME"))
            flushVolume(_config.scheduledFlushVolume);
        }
      }
      break;

    //
    // RUNNING
    //
    case Status::RUNNING: {
      YBP.println("RUNNING");

      pickleResult = Result::STARTUP;
      depickleResult = Result::STARTUP;

      resetErrorTimers();
      runtimeStart = millis();
      uint32_t lastRuntimeUpdate = runtimeStart;

      currentVolume = 0;
      currentFlushVolume = 0;

      // error out early for low battery
      if (checkBatteryLevel(runResult))
        return logResult(Status::RUNNING, runResult);

      if (initializeHardware(false)) {
        currentStatus = Status::IDLE;
        return logResult(Status::RUNNING, runResult);
      }

      uint32_t boostPumpStart = millis();
      if (hasBoostPump()) {
        YBP.println("Boost Pump Started");
        enableBoostPump();
        vTaskDelay(pdMS_TO_TICKS(_config.boostPumpDelay));

        if (_config.hasFilterPressureSensor && _config.enableFilterPressureLowCheck) {
          while (getFilterPressure() < getFilterPressureMinimum()) {
            if (checkStopFlag(runResult))
              return logResult(Status::RUNNING, runResult);

            if (checkBatteryLevel(runResult))
              return logResult(Status::RUNNING, runResult);

            if (checkFilterPressureLow())
              return logResult(Status::RUNNING, runResult);

            vTaskDelay(pdMS_TO_TICKS(100));
          }
        }
        YBP.println("Boost Pump OK");
      }

      enableHighPressurePump();
      vTaskDelay(pdMS_TO_TICKS(_config.highPressurePumpDelay));

      setMembranePressureTarget(_config.membranePressureTarget);

      if (waitForMembranePressure()) {
        YBP.println("Membrane Pressure Error");
        return logResult(Status::RUNNING, runResult);
      }

      if (waitForProductFlowrate()) {
        YBP.println("Product Flowrate Error");
        return logResult(Status::RUNNING, runResult);
      }

      if (waitForProductSalinity()) {
        YBP.println("Product Salinity Error");
        return logResult(Status::RUNNING, runResult);
      }

      closeDiverterValve();

      uint32_t productionStart = millis();
      while (true) {
        stats.startCycle("run");

        if (checkBatteryLevel(runResult))
          return logResult(Status::RUNNING, runResult);

        if (checkDiverterValveClosed())
          return logResult(Status::RUNNING, runResult);

        if (checkFilterPressureLow())
          return logResult(Status::RUNNING, runResult);

        if (checkFilterPressureHigh())
          return logResult(Status::RUNNING, runResult);

        if (checkMembranePressureLow())
          return logResult(Status::RUNNING, runResult);

        if (checkMembranePressureHigh())
          return logResult(Status::RUNNING, runResult);

        if (checkRunTotalFlowrateLow())
          return logResult(Status::RUNNING, runResult);

        if (checkProductFlowrateLow())
          return logResult(Status::RUNNING, runResult);

        if (checkProductFlowrateHigh())
          return logResult(Status::RUNNING, runResult);

        if (checkProductSalinityHigh())
          return logResult(Status::RUNNING, runResult);

        if (checkMotorTemperature(runResult))
          return logResult(Status::RUNNING, runResult);

        if (checkStopFlag(runResult))
          return logResult(Status::RUNNING, runResult);

        if (millis() - productionStart > _config.productionRuntimeTimeout) {
          currentStatus = Status::STOPPING;
          runResult = Result::ERR_PRODUCTION_TIMEOUT;
          return logResult(Status::RUNNING, runResult);
        }

        // are we going for time?
        if (desiredRuntime > 0 && getRuntimeElapsed() >= desiredRuntime) {
          runResult = Result::SUCCESS_TIME;
          break;
        }

        // are we going for volume?
        if (desiredVolume > 0 && getVolume() >= desiredVolume) {
          runResult = Result::SUCCESS_VOLUME;
          break;
        }

        // tank level means we're finished successfully
        if (checkTankLevel())
          break;

        // save our total runtime occasionally
        if (millis() - lastRuntimeUpdate > 15 * 60 * 1000) {
          totalRuntime += (millis() - lastRuntimeUpdate) / 1000; // store as seconds
          _app.config.preferences.putULong("bomTotRuntime", totalRuntime);
          lastRuntimeUpdate = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      stats.stopCycle();

      // save our total volume produced
      _app.config.preferences.putFloat("bomTotVolume", totalVolume);

      // save our runtime too.
      totalRuntime += (millis() - lastRuntimeUpdate) / 1000; // store as seconds
      _app.config.preferences.putULong("bomTotRuntime", totalRuntime);

      // save our total number of cycles
      totalCycles++;
      _app.config.preferences.putUInt("bomTotCycles", totalCycles);

      // save it!
      logResult(Status::RUNNING, runResult);

      // next step... turn it off!
      currentStatus = Status::STOPPING;

      break;
    }

    //
    // STOPPING
    //
    case Status::STOPPING: {
      YBP.println("STOPPING");
      YBP.printf("Run Status: %s\n", resultToString(runResult));

      stats.stopCycle();
      resetErrorTimers();

      // treat anything other than success as a hard stop
      bool success = false;
      if (runResult == Result::SUCCESS_TIME || runResult == Result::SUCCESS_VOLUME || runResult == Result::SUCCESS_TANK_LEVEL)
        success = true;

      // init hardware will handle the stopping.
      if (initializeHardware(!success)) {
        currentStatus = Status::IDLE;
        return;
      } else {
        if (success)
          _app.playMelody(_config.successMelody.c_str());
        else
          _app.playMelody(_config.errorMelody.c_str());

        if (_config.postRunFlushMode.equals("TIME"))
          flushDuration(_config.postRunFlushDuration);
        else if (_config.postRunFlushMode.equals("VOLUME"))
          flushVolume(_config.postRunFlushVolume);
        else if (_config.postRunFlushMode.equals("SALINITY"))
          flush();
        else
          currentStatus = Status::IDLE;
      }

      break;
    }

    //
    // FLUSHING
    //
    case Status::FLUSHING: {
      YBP.println("FLUSHING");

      if (!hasFlushValve()) {
        currentStatus = Status::IDLE;
        return;
      }

      resetErrorTimers();

      depickleResult = Result::STARTUP;
      pickleResult = Result::STARTUP;

      flushStart = millis();
      currentFlushVolume = 0;

      if (initializeHardware(false)) {
        currentStatus = Status::IDLE;
        return logResult(Status::FLUSHING, flushResult);
      }

      // start up our hardware
      openFlushValve();
      if (_config.autoflushUseHighPressureMotor) {
        enableHighPressurePump();
        vTaskDelay(pdMS_TO_TICKS(_config.highPressurePumpDelay));
      }

      // check our sensors while we flush
      while (true) {
        stats.startCycle("flush");

        if (checkFlushFilterPressureLow())
          break;

        if (checkFlushFlowrateLow())
          break;

        if (checkFlushTankLevelLow())
          break;

        if (hasHighPressurePump() && _config.autoflushUseHighPressureMotor && checkMotorTemperature(flushResult))
          break;

        if (checkStopFlag(flushResult))
          break;

        // are we going for time?
        if (desiredFlushDuration > 0 && getFlushElapsed() > desiredFlushDuration) {
          flushResult = Result::SUCCESS_TIME;
          // DUMP("DURATION");
          break;
        }

        // are we going for volume?
        if (desiredFlushVolume > 0 && getFlushVolume() >= desiredFlushVolume) {
          flushResult = Result::SUCCESS_VOLUME;
          DUMP("VOLUME");
          break;
        }

        // how about salinity? (auto)
        if (desiredFlushDuration == 0 && desiredFlushVolume == 0) {
          if (getBrineSalinity() < _config.postRunFlushSalinity) {
            DUMP("SALINITY");
            flushResult = Result::SUCCESS_SALINITY;
            break;
          }
        }

        // did we hit our flush timeout?
        if (getFlushElapsed() > _config.flushTimeout) {
          flushResult = Result::ERR_FLUSH_TIMEOUT;
          break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      stats.stopCycle();

      // either flush (post run or scheduled) resets the scheduled flush timer
      if (scheduledFlushEnabled()) {
        lastAutoflushTimeMillis = millis();
        if (_app.ntp.isReady()) {
          lastAutoflushTimeNTP = _app.ntp.getTime();
          _app.config.preferences.putLong64("lastautoflush", lastAutoflushTimeNTP);
        }
      }

      // keep track over restarts.
      _app.config.preferences.putBool("bomPickled", false);
      pickledOnTimestamp = 0;
      _app.config.preferences.putLong64("bomPickledOn", pickledOnTimestamp);

      // save to our log.
      logResult(Status::FLUSHING, flushResult);

      // normal stop on success, otherwise fast/estop
      bool success = (flushResult == Result::SUCCESS_TIME || flushResult == Result::SUCCESS_VOLUME || flushResult == Result::SUCCESS_SALINITY);
      initializeHardware(!success);
      waitForFlushValveOff();

      currentStatus = Status::IDLE;

      break;
    }

    //
    // PICKLING
    //
    case Status::PICKLING:
      resetErrorTimers();

      pickleStart = millis();

      // error out early for low battery
      if (checkBatteryLevel(pickleResult))
        return logResult(Status::PICKLING, pickleResult);

      if (initializeHardware(false)) {
        currentStatus = Status::IDLE;
        return logResult(Status::PICKLING, pickleResult);
      }

      enableHighPressurePump();
      vTaskDelay(pdMS_TO_TICKS(_config.highPressurePumpDelay));

      while (getPickleElapsed() < pickleDuration) {
        stats.startCycle("pickle");

        if (stopFlag)
          break;

        if (checkPickleTotalFlowrateLow(pickleResult)) {
          stats.stopCycle();
          currentStatus = Status::IDLE;
          initializeHardware(true);
          return logResult(Status::PICKLING, pickleResult);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (initializeHardware(stopFlag)) {
        stats.stopCycle();
        currentStatus = Status::IDLE;
        return logResult(Status::PICKLING, pickleResult);
      }

      stats.stopCycle();

      currentStatus = Status::PICKLED;

      if (stopFlag)
        pickleResult = Result::USER_STOP;
      else
        pickleResult = Result::SUCCESS;

      // keep track over restarts.
      _app.config.preferences.putBool("bomPickled", true);

      if (_app.ntp.isReady()) {
        pickledOnTimestamp = _app.ntp.getTime();
        _app.config.preferences.putLong64("bomPickledOn", pickledOnTimestamp);
      }

      logResult(Status::PICKLING, pickleResult);

      break;

    //
    // DEPICKLING
    //
    case Status::DEPICKLING:
      resetErrorTimers();

      depickleStart = millis();

      // error out early for low battery
      if (checkBatteryLevel(depickleResult))
        return logResult(Status::DEPICKLING, depickleResult);

      if (initializeHardware(false)) {
        currentStatus = Status::IDLE;
        return logResult(Status::DEPICKLING, depickleResult);
      }

      enableHighPressurePump();
      vTaskDelay(pdMS_TO_TICKS(_config.highPressurePumpDelay));

      while (getDepickleElapsed() < depickleDuration) {
        stats.startCycle("depickle");

        if (stopFlag)
          break;

        if (checkPickleTotalFlowrateLow(depickleResult)) {
          stats.stopCycle();
          currentStatus = Status::IDLE;
          initializeHardware(true);
          return logResult(Status::DEPICKLING, depickleResult);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (initializeHardware(stopFlag)) {
        stats.stopCycle();
        currentStatus = Status::IDLE;
        return logResult(Status::DEPICKLING, depickleResult);
      }

      stats.stopCycle();

      currentStatus = Status::IDLE;

      if (stopFlag)
        depickleResult = Result::USER_STOP;
      else
        depickleResult = Result::SUCCESS;

      // keep track over restarts.
      _app.config.preferences.putBool("bomPickled", false);
      pickledOnTimestamp = 0;
      _app.config.preferences.putLong64("bomPickledOn", pickledOnTimestamp);

      logResult(Status::DEPICKLING, depickleResult);

      break;
  }
}

void Brineomatic::resetErrorTimers()
{
  stopFlag = false;
  membranePressureHighStart = 0;
  membranePressureLowStart = 0;
  filterPressureHighStart = 0;
  filterPressureLowStart = 0;
  productFlowrateLowStart = 0;
  productFlowrateHighStart = 0;
  brineFlowrateLowStart = 0;
  totalFlowrateLowStart = 0;
  flushFilterPressureLowStart = 0;
  flushFlowrateLowStart = 0;
  flushTankLevelLowStart = 0;
  diverterValveOpenStart = 0;
  productSalinityHighStart = 0;
  motorTemperatureStart = 0;
}

bool Brineomatic::checkStopFlag(Result& result)
{
  if (stopFlag) {
    currentStatus = Status::STOPPING;
    result = Result::USER_STOP;
    return true;
  }

  return false;
}

bool Brineomatic::checkMembranePressureHigh()
{
  if (!_config.hasMembranePressureSensor)
    return false;

  if (!_config.enableMembranePressureHighCheck)
    return false;

  return checkTimedError(
    getMembranePressure() > _config.membranePressureHighThreshold,
    membranePressureHighStart,
    _config.membranePressureHighDelay,
    Result::ERR_MEMBRANE_PRESSURE_HIGH,
    runResult);
}

bool Brineomatic::checkMembranePressureLow()
{
  if (!_config.hasMembranePressureSensor)
    return false;

  if (!_config.enableMembranePressureLowCheck)
    return false;

  return checkTimedError(
    getMembranePressure() < _config.membranePressureLowThreshold,
    membranePressureLowStart,
    _config.membranePressureLowDelay,
    Result::ERR_MEMBRANE_PRESSURE_LOW,
    runResult);
}

bool Brineomatic::checkFilterPressureHigh()
{
  if (!_config.hasFilterPressureSensor)
    return false;

  if (!_config.enableFilterPressureHighCheck)
    return false;

  return checkTimedError(
    getFilterPressure() > _config.filterPressureHighThreshold,
    filterPressureHighStart,
    _config.filterPressureHighDelay,
    Result::ERR_FILTER_PRESSURE_HIGH,
    runResult);
}

bool Brineomatic::checkFilterPressureLow()
{
  if (!_config.hasFilterPressureSensor)
    return false;

  if (!_config.enableFilterPressureLowCheck)
    return false;

  return checkTimedError(
    getFilterPressure() < _config.filterPressureLowThreshold,
    filterPressureLowStart,
    _config.filterPressureLowDelay,
    Result::ERR_FILTER_PRESSURE_LOW,
    runResult);
}

bool Brineomatic::checkProductFlowrateLow()
{
  if (!_config.hasProductFlowSensor)
    return false;

  if (!_config.enableProductFlowrateLowCheck)
    return false;

  return checkTimedError(
    getProductFlowrate() < getProductFlowrateMinimum(),
    productFlowrateLowStart,
    _config.productFlowrateLowDelay,
    Result::ERR_PRODUCT_FLOWRATE_LOW,
    runResult);
}

bool Brineomatic::checkProductFlowrateHigh()
{
  if (!_config.hasProductFlowSensor)
    return false;

  if (!_config.enableProductFlowrateHighCheck)
    return false;

  return checkTimedError(
    getProductFlowrate() > _config.productFlowrateHighThreshold,
    productFlowrateHighStart,
    _config.productFlowrateHighDelay,
    Result::ERR_PRODUCT_FLOWRATE_HIGH,
    runResult);
}

bool Brineomatic::checkPickleTotalFlowrateLow(Result& result)
{
  if (!_config.hasBrineFlowSensor)
    return false;

  if (!_config.enablePickleTotalFlowrateLowCheck)
    return false;

  return checkTimedError(
    getBrineFlowrate() < _config.pickleTotalFlowrateLowThreshold,
    brineFlowrateLowStart,
    _config.pickleTotalFlowrateLowDelay,
    Result::ERR_BRINE_FLOWRATE_LOW,
    result);
}

bool Brineomatic::checkFlushFilterPressureLow()
{
  if (!_config.hasFilterPressureSensor)
    return false;

  if (!_config.enableFlushFilterPressureLowCheck)
    return false;

  return checkTimedError(
    getFilterPressure() < _config.flushFilterPressureLowThreshold,
    flushFilterPressureLowStart,
    _config.flushFilterPressureLowDelay,
    Result::ERR_FLUSH_FILTER_PRESSURE_LOW,
    flushResult);
}

bool Brineomatic::checkFlushFlowrateLow()
{
  if (!_config.hasBrineFlowSensor)
    return false;

  if (!_config.enableFlushFlowrateLowCheck)
    return false;

  return checkTimedError(
    getBrineFlowrate() < _config.flushFlowrateLowThreshold,
    flushFlowrateLowStart,
    _config.flushFlowrateLowDelay,
    Result::ERR_FLUSH_FLOWRATE_LOW,
    flushResult);
}

bool Brineomatic::checkFlushTankLevelLow()
{
  if (_config.tankLevelSensorType.equals("NONE"))
    return false;

  if (!_config.enableFlushTankLevelLowCheck)
    return false;

  return checkTimedError(
    currentTankLevel < _config.flushTankLevelLowThreshold,
    flushTankLevelLowStart,
    _config.flushTankLevelLowDelay,
    Result::ERR_FLUSH_TANK_LEVEL_LOW,
    flushResult);
}

bool Brineomatic::checkRunTotalFlowrateLow()
{
  if (!_config.hasBrineFlowSensor)
    return false;

  if (!_config.enableRunTotalFlowrateLowCheck)
    return false;

  return checkTimedError(
    getTotalFlowrate() < _config.runTotalFlowrateLowThreshold,
    totalFlowrateLowStart,
    _config.runTotalFlowrateLowDelay,
    Result::ERR_TOTAL_FLOWRATE_LOW,
    runResult);
}

bool Brineomatic::checkDiverterValveClosed()
{
  // if (!_config.hasProductFlowSensor)
  //   return false;

  if (!_config.hasBrineFlowSensor)
    return false;

  if (!_config.enableDiverterValveClosedCheck)
    return false;

  return checkTimedError(
    getBrineFlowrate() > _config.diverterValveClosedFlowrateHighThreshold,
    diverterValveOpenStart,
    _config.diverterValveClosedDelay,
    Result::ERR_DIVERTER_VALVE_OPEN,
    runResult);
}

bool Brineomatic::checkProductSalinityHigh()
{
  if (!_config.hasProductTDSSensor)
    return false;

  if (!_config.enableProductSalinityHighCheck)
    return false;

  return checkTimedError(
    getProductSalinity() > getProductSalinityMaximum(),
    productSalinityHighStart,
    _config.productSalinityHighDelay,
    Result::ERR_PRODUCT_SALINITY_HIGH,
    runResult);
}

bool Brineomatic::checkMotorTemperature(Result& result)
{
  if (!hasMotorTemperature())
    return false;

  if (!_config.enableMotorTemperatureCheck)
    return false;

  return checkTimedError(
    getMotorTemperature() > getMotorTemperatureMaximum(),
    motorTemperatureStart,
    _config.motorTemperatureHighDelay,
    Result::ERR_MOTOR_TEMPERATURE_HIGH,
    result);
}

bool Brineomatic::checkTimedError(bool condition,
  uint32_t& startTime,
  uint32_t timeout,
  Result errorResult,
  Result& result)
{
  if (condition) {
    if (startTime != 0) {
      if (millis() - startTime > timeout) {
        currentStatus = Status::STOPPING;
        result = errorResult;
        return true;
      }
    } else {
      startTime = millis();
    }
  } else {
    startTime = 0;
  }

  return false;
}

// return true on error
// return false on success
bool Brineomatic::waitForMembranePressure()
{
  // skip this if we dont have the sensor
  if (!_config.hasMembranePressureSensor)
    return false;

  YBP.println("Wait for Membrane Pressure");

  uint32_t highPressurePumpStart = millis();
  uint32_t stableStart = 0;

  while (true) {
    // let the spice flow
    if (checkRunTotalFlowrateLow())
      return true;

    // check this here in case our PID goes crazy
    if (checkMembranePressureHigh())
      return true;

    if (checkStopFlag(runResult))
      return true;

    if (millis() - highPressurePumpStart > _config.membranePressureTimeout) {
      currentStatus = Status::STOPPING;
      runResult = Result::ERR_MEMBRANE_PRESSURE_TIMEOUT;
      return true;
    }

    if (getMembranePressure() >= getMembranePressureMinimum()) {
      if (stableStart == 0)
        stableStart = millis();
      else if (millis() - stableStart >= _config.membranePressureStabilizationTime) {
        YBP.println("High Pressure Pump OK");
        return false;
      }
    } else {
      stableStart = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool Brineomatic::waitForProductFlowrate()
{
  if (!_config.hasProductFlowSensor)
    return false;

  YBP.println("Wait for Product Flowrate");

  uint32_t flowCheckStart = millis();
  uint32_t stableStart = 0;

  while (true) {
    if (checkMembranePressureHigh())
      return true;
    if (checkStopFlag(runResult))
      return true;

    if (millis() - flowCheckStart > _config.productFlowrateTimeout) {
      currentStatus = Status::STOPPING;
      runResult = Result::ERR_PRODUCT_FLOWRATE_TIMEOUT;
      return true;
    }

    if (getProductFlowrate() > getProductFlowrateMinimum() && getProductFlowrate() < _config.productFlowrateHighThreshold) {
      if (stableStart == 0)
        stableStart = millis();
      else if (millis() - stableStart >= _config.productFlowrateStabilizationTime) {
        YBP.println("Flowrate OK");
        return false;
      }
    } else {
      stableStart = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool Brineomatic::waitForProductSalinity()
{
  if (!_config.hasProductTDSSensor)
    return false;

  YBP.println("Wait for Product Salinity");

  uint32_t salinityCheckStart = millis();
  uint32_t stableStart = 0;

  while (true) {
    if (checkMembranePressureHigh())
      return true;
    if (checkStopFlag(runResult))
      return true;

    if (millis() - salinityCheckStart > _config.productSalinityTimeout) {
      currentStatus = Status::STOPPING;
      runResult = Result::ERR_PRODUCT_SALINITY_TIMEOUT;
      return true;
    }

    if (getProductSalinity() < getProductSalinityMaximum()) {
      if (stableStart == 0)
        stableStart = millis();
      else if (millis() - stableStart >= _config.productSalinityStabilizationTime) {
        YBP.println("Salinity OK");
        return false;
      }
    } else {
      stableStart = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool Brineomatic::waitForFlushValveOff()
{
  if (!_config.enableFlushValveOffCheck)
    return false;

  if (!_config.hasFilterPressureSensor && !_config.hasBrineFlowSensor)
    return false;

  YBP.println("Wait for Flush Valve Off");

  uint32_t start = millis();

  bool done = false;
  while (!done) {
    if (millis() - start > _config.flushValveOffDelay) {
      currentStatus = Status::IDLE;
      flushResult = Result::ERR_FLUSH_VALVE_ON;
      return true;
    }

    done = true;

    if (_config.hasFilterPressureSensor)
      if (getFilterPressure() > _config.flushValveOffThreshold)
        done = false;

    if (_config.hasBrineFlowSensor)
      if (getBrineFlowrate() > 0)
        done = false;

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  return false;
}

bool Brineomatic::checkTankLevel()
{
  if (_config.tankLevelSensorType.equals("NONE"))
    return false;

  if (currentTankLevel < 0)
    return false;

  if (!_config.enableTankLevelFullCheck)
    return false;

  return checkTimedError(
    currentTankLevel >= _config.tankLevelFullThreshold,
    tankLevelFullStart,
    _config.tankLevelFullDelay,
    Result::SUCCESS_TANK_LEVEL,
    runResult);
}

bool Brineomatic::checkBatteryLevel(Result& result)
{
  if (_config.batteryLevelSensorType.equals("NONE"))
    return false;

  if (!_config.enableBatteryLevelLowCheck)
    return false;

  if (currentBatteryLevel <= _config.batteryLevelLowThreshold) {
    currentStatus = Status::STOPPING;
    result = Result::ERR_BATTERY_LEVEL;
    return true;
  }

  return false;
}

void Brineomatic::logResult(Status status, Result result)
{
  JsonDocument log;

  log["timestamp"] = (uint32_t)_app.ntp.getTime();
  log["mode"] = getStatus(status);
  log["result"] = resultToString(result);
  log["total_runtime"] = totalRuntime;

  JsonObject statsObj = log["stats"].to<JsonObject>();
  if (status == Status::RUNNING) {
    log["elapsed"] = getRuntimeElapsed();
    log["volume"] = getVolume();
    stats.cycleToJson("run", statsObj);
  } else if (status == Status::FLUSHING) {
    log["elapsed"] = getFlushElapsed();
    log["volume"] = getFlushVolume();
    stats.cycleToJson("flush", statsObj);
  } else if (status == Status::PICKLING) {
    log["elapsed"] = getPickleElapsed();
    log["volume"] = getTotalVolume();
    stats.cycleToJson("pickle", statsObj);
  } else if (status == Status::DEPICKLING) {
    log["elapsed"] = getDepickleElapsed();
    log["volume"] = getTotalVolume();
    stats.cycleToJson("depickle", statsObj);
  }

  File f = LittleFS.open("/run_log.json", "a");
  if (f) {
    serializeJson(log, f);
    f.println();
    f.close();
  }
}