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
#include "validate.h"
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
  // start from the factory defaults; loadConfigJSON() overlays any saved settings
  _config = defaults;
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

  if (autoflushEnabled()) {
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

    currentProductFlowrate = flowrate;
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

    currentBrineFlowrate = flowrate;
  }
#endif
}

void Brineomatic::measureMotorTemperature()
{
#if YB_DS18B20_MOTOR_PIN
  if (_config.motorTemperatureSensorType != "DS18B20")
    return;

  if (motorTemperatureSensor.isConversionComplete()) {
    currentMotorTemperature = motorTemperatureSensor.getTempC(motorTemperatureAddress);
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
    currentWaterTemperature = waterTemperatureSensor.getTempC(waterTemperatureAddress);
    waterTemperatureSensor.requestTemperatures();
  }
#endif
}

void Brineomatic::measureProductSalinity()
{
  int16_t reading = adcHelper->getAverageReading(YB_PRODUCT_TDS_CHANNEL);
  gravityTds.setTemperature(getWaterTemperature());
  gravityTds.update(reading);
  currentProductSalinity = gravityTds.getTdsValue() + _config.productTDSSensorOffset;
}

void Brineomatic::measureBrineSalinity()
{
  int16_t reading = adcHelper->getAverageReading(YB_BRINE_TDS_CHANNEL);
  gravityTds.setTemperature(getWaterTemperature());
  gravityTds.update(reading);
  currentBrineSalinity = gravityTds.getTdsValue() + _config.brineTDSSensorOffset;
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

  currentFilterPressure = map_generic(amperage, 4.0, 20.0, _config.filterPressureSensorMin, _config.filterPressureSensorMax);
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

  currentMembranePressure = map_generic(amperage, 4.0, 20.0, _config.membranePressureSensorMin, _config.membranePressureSensorMax);
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

bool Brineomatic::autoflushEnabled()
{
  if (!hasFlushValve())
    return false;

  return !_config.autoflushMode.equals("NONE");
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

float Brineomatic::getFilterPressureMinimum()
{
  return _config.filterPressureLowThreshold;
}

float Brineomatic::getMembranePressure()
{
  return currentMembranePressure;
}

float Brineomatic::getMembranePressureMinimum()
{
  return _config.membranePressureLowThreshold;
}

float Brineomatic::getProductFlowrate()
{
  return currentProductFlowrate;
}

float Brineomatic::getBrineFlowrate()
{
  return currentBrineFlowrate;
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
}

void Brineomatic::setTankLevel(float level)
{
  currentTankLevel = level;
}

void Brineomatic::setBatteryLevel(float level)
{
  currentBatteryLevel = level;
}

void Brineomatic::setMotorTemperature(float temp)
{
  currentMotorTemperature = temp;
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

float Brineomatic::getBrineSalinity()
{
  return currentBrineSalinity;
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
  if (currentStatus == Status::IDLE && autoflushEnabled()) {
    uint32_t elapsed;
    if (_app.ntp.isReady() && lastAutoflushTimeNTP > 1700000000)
      elapsed = (_app.ntp.getTime() - lastAutoflushTimeNTP) * 1000;
    else
      elapsed = millis() - lastAutoflushTimeMillis;

    return _config.autoflushInterval - elapsed;
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
      if (autoflushEnabled()) {
        uint32_t elapsed;
        if (_app.ntp.isReady() && lastAutoflushTimeNTP > 1700000000)
          elapsed = (_app.ntp.getTime() - lastAutoflushTimeNTP) * 1000;
        else
          elapsed = millis() - lastAutoflushTimeMillis;

        if (elapsed > _config.autoflushInterval) {
          if (_config.autoflushMode.equals("TIME"))
            flushDuration(_config.autoflushDuration);
          else if (_config.autoflushMode.equals("VOLUME"))
            flushVolume(_config.autoflushVolume);
          else if (_config.autoflushMode.equals("SALINITY"))
            flush();
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

        if (_config.autoflushMode.equals("TIME"))
          flushDuration(_config.autoflushDuration);
        else if (_config.autoflushMode.equals("VOLUME"))
          flushVolume(_config.autoflushVolume);
        else if (_config.autoflushMode.equals("SALINITY"))
          flush();
        else if (_config.autoflushMode.equals("NONE"))
          currentStatus = Status::IDLE;
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
          if (getBrineSalinity() < _config.autoflushSalinity) {
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

      if (autoflushEnabled()) {
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
        if (stopFlag)
          break;

        if (checkPickleTotalFlowrateLow(pickleResult)) {
          currentStatus = Status::IDLE;
          initializeHardware(true);
          return logResult(Status::PICKLING, pickleResult);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (initializeHardware(stopFlag)) {
        currentStatus = Status::IDLE;
        return logResult(Status::PICKLING, pickleResult);
      }

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
        if (stopFlag)
          break;

        if (checkPickleTotalFlowrateLow(depickleResult)) {
          currentStatus = Status::IDLE;
          initializeHardware(true);
          return logResult(Status::DEPICKLING, depickleResult);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (initializeHardware(stopFlag)) {
        currentStatus = Status::IDLE;
        return logResult(Status::DEPICKLING, depickleResult);
      }

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

void Brineomatic::generateUpdateJSON(JsonVariant output)
{
  output["brineomatic"] = true;
  output["status"] = getStatus();
  output["run_result"] = resultToString(getRunResult());
  output["flush_result"] = resultToString(getFlushResult());
  output["pickle_result"] = resultToString(getPickleResult());
  output["depickle_result"] = resultToString(getDepickleResult());
  output["motor_temperature"] = getMotorTemperature();
  output["water_temperature"] = getWaterTemperature();
  output["product_flowrate"] = getProductFlowrate();
  output["brine_flowrate"] = getBrineFlowrate();
  output["total_flowrate"] = getTotalFlowrate();
  output["volume"] = getVolume();
  output["flush_volume"] = getFlushVolume();
  output["product_salinity"] = getProductSalinity();
  output["brine_salinity"] = getBrineSalinity();
  output["filter_pressure"] = getFilterPressure();
  output["membrane_pressure"] = getMembranePressure();
  output["tank_level"] = getTankLevel();
  output["battery_level"] = getBatteryLevel();

  if (hasBoostPump())
    output["boost_pump_on"] = isBoostPumpOn();
  if (hasHighPressurePump())
    output["high_pressure_pump_on"] = isHighPressurePumpOn();
  if (hasDiverterValve())
    output["diverter_valve_open"] = isDiverterValveOpen();
  if (hasFlushValve())
    output["flush_valve_open"] = isFlushValveOpen();
  if (hasCoolingFan())
    output["cooling_fan_on"] = isCoolingFanOn();

  output["next_flush_countdown"] = getNextFlushCountdown();

  if (!strcmp(getStatus(), "RUNNING")) {
    output["runtime_elapsed"] = getRuntimeElapsed();
    output["finish_countdown"] = getFinishCountdown();
  }

  if (!strcmp(getStatus(), "FLUSHING")) {
    output["flush_elapsed"] = getFlushElapsed();
    output["flush_countdown"] = getFlushCountdown();
  }

  if (!strcmp(getStatus(), "PICKLING")) {
    output["pickle_elapsed"] = getPickleElapsed();
    output["pickle_countdown"] = getPickleCountdown();
  }

  if (!strcmp(getStatus(), "DEPICKLING")) {
    output["depickle_elapsed"] = getDepickleElapsed();
    output["depickle_countdown"] = getDepickleCountdown();
  }

  if (!strcmp(getStatus(), "PICKLED")) {
    if (pickledOnTimestamp > 1700000000)
      output["pickled_on"] = pickledOnTimestamp;
  }
}

void Brineomatic::generateConfigJSON(JsonVariant output, UserRole role, ConfigPurpose purpose)
{
  // shortcuts for the UI
  if (purpose == ConfigPurpose::UI_CONFIG) {
    output["has_boost_pump"] = this->hasBoostPump();
    output["has_high_pressure_pump"] = this->hasHighPressurePump();
    output["has_diverter_valve"] = this->hasDiverterValve();
    output["has_flush_valve"] = this->hasFlushValve();
    output["has_cooling_fan"] = this->hasCoolingFan();
  }

  output["gauge_order"] = _config.gaugeOrder;

  output["autoflush_mode"] = _config.autoflushMode;
  output["autoflush_salinity"] = _config.autoflushSalinity;
  output["autoflush_duration"] = _config.autoflushDuration;
  output["autoflush_volume"] = _config.autoflushVolume;
  output["autoflush_interval"] = _config.autoflushInterval;
  output["autoflush_use_high_pressure_motor"] = _config.autoflushUseHighPressureMotor;

  output["flush_timeout"] = _config.flushTimeout;
  output["membrane_pressure_timeout"] = _config.membranePressureTimeout;
  output["product_flowrate_timeout"] = _config.productFlowrateTimeout;
  output["product_salinity_timeout"] = _config.productSalinityTimeout;
  output["membrane_pressure_stabilization_time"] = _config.membranePressureStabilizationTime;
  output["product_flowrate_stabilization_time"] = _config.productFlowrateStabilizationTime;
  output["product_salinity_stabilization_time"] = _config.productSalinityStabilizationTime;
  output["production_runtime_timeout"] = _config.productionRuntimeTimeout;

  output["tank_capacity"] = _config.tankCapacity;
  output["temperature_units"] = _config.temperatureUnits;
  output["pressure_units"] = _config.pressureUnits;
  output["volume_units"] = _config.volumeUnits;
  output["flowrate_units"] = _config.flowrateUnits;
  output["success_melody"] = _config.successMelody;
  output["error_melody"] = _config.errorMelody;

  output["boost_pump_control"] = _config.boostPumpControl;
  output["boost_pump_relay_id"] = _config.boostPumpRelayId;
  output["boost_pump_relay_inverted"] = _config.boostPumpRelayInverted;
  output["boost_pump_delay"] = _config.boostPumpDelay;

  output["high_pressure_pump_control"] = _config.highPressurePumpControl;
  output["high_pressure_relay_id"] = _config.highPressureRelayId;
  output["high_pressure_relay_inverted"] = _config.highPressureRelayInverted;
  output["high_pressure_modbus_device"] = _config.highPressurePumpModbusDevice;
  output["high_pressure_modbus_slave_id"] = _config.highPressurePumpModbusSlaveId;
  output["high_pressure_modbus_frequency"] = _config.highPressurePumpModbusFrequency;
  output["high_pressure_pump_delay"] = _config.highPressurePumpDelay;

  output["high_pressure_valve_control"] = _config.highPressureValveControl;
  output["membrane_pressure_target"] = _config.membranePressureTarget;
  output["high_pressure_valve_stepper_id"] = _config.highPressureValveStepperId;
  output["high_pressure_stepper_step_angle"] = _config.highPressureValveStepperStepAngle;
  output["high_pressure_stepper_gear_ratio"] = _config.highPressureValveStepperGearRatio;
  output["high_pressure_stepper_close_angle"] = _config.highPressureValveStepperCloseAngle;
  output["high_pressure_stepper_close_speed"] = _config.highPressureValveStepperCloseSpeed;
  output["high_pressure_stepper_open_angle"] = _config.highPressureValveStepperOpenAngle;
  output["high_pressure_stepper_open_speed"] = _config.highPressureValveStepperOpenSpeed;
  output["high_pressure_stepper_run_current"] = _config.highPressureValveStepperRunCurrent;
  output["high_pressure_stepper_home_current"] = _config.highPressureValveStepperHomeCurrent;
  output["high_pressure_stepper_inverted"] = _config.highPressureStepperInverted;

  output["diverter_valve_control"] = _config.diverterValveControl;
  output["diverter_valve_servo_id"] = _config.diverterValveServoId;
  output["diverter_valve_relay_id"] = _config.diverterValveRelayId;
  output["diverter_valve_relay_inverted"] = _config.diverterValveRelayInverted;
  output["diverter_valve_open_angle"] = _config.diverterValveOpenAngle;
  output["diverter_valve_close_angle"] = _config.diverterValveCloseAngle;
  output["diverter_valve_tank_relay_id"] = _config.diverterValveTankRelayId;
  output["diverter_valve_tank_relay_inverted"] = _config.diverterValveTankRelayInverted;
  output["diverter_valve_overboard_relay_id"] = _config.diverterValveOverboardRelayId;
  output["diverter_valve_overboard_relay_inverted"] = _config.diverterValveOverboardRelayInverted;
  output["diverter_valve_relay_change_interval"] = _config.diverterValveRelayChangeInterval;

  output["flush_valve_control"] = _config.flushValveControl;
  output["flush_valve_relay_id"] = _config.flushValveRelayId;
  output["flush_valve_relay_inverted"] = _config.flushValveRelayInverted;
  output["flush_valve_servo_id"] = _config.flushValveServoId;
  output["flush_valve_open_angle"] = _config.flushValveOpenAngle;
  output["flush_valve_close_angle"] = _config.flushValveCloseAngle;

  output["cooling_fan_control"] = _config.coolingFanControl;
  output["cooling_fan_relay_id"] = _config.coolingFanRelayId;
  output["cooling_fan_relay_inverted"] = _config.coolingFanRelayInverted;
  output["cooling_fan_on_temperature"] = _config.coolingFanOnTemperature;
  output["cooling_fan_off_temperature"] = _config.coolingFanOffTemperature;

  output["has_membrane_pressure_sensor"] = _config.hasMembranePressureSensor;
  output["membrane_pressure_sensor_min"] = _config.membranePressureSensorMin;
  output["membrane_pressure_sensor_max"] = _config.membranePressureSensorMax;

  output["has_filter_pressure_sensor"] = _config.hasFilterPressureSensor;
  output["filter_pressure_sensor_min"] = _config.filterPressureSensorMin;
  output["filter_pressure_sensor_max"] = _config.filterPressureSensorMax;

  output["has_product_tds_sensor"] = _config.hasProductTDSSensor;
  output["product_tds_sensor_offset"] = _config.productTDSSensorOffset;

  output["has_brine_tds_sensor"] = _config.hasBrineTDSSensor;
  output["brine_tds_sensor_offset"] = _config.brineTDSSensorOffset;

  output["has_product_flow_sensor"] = _config.hasProductFlowSensor;
  output["product_flowmeter_ppl"] = _config.productFlowmeterPPL;

  output["has_brine_flow_sensor"] = _config.hasBrineFlowSensor;
  output["brine_flowmeter_ppl"] = _config.brineFlowmeterPPL;

  output["motor_temperature_sensor_type"] = _config.motorTemperatureSensorType;
  output["motor_temperature_mqtt_path"] = _config.motorTemperatureMqttPath;
  output["water_temperature_sensor_type"] = _config.waterTemperatureSensorType;
  output["water_temperature_mqtt_path"] = _config.waterTemperatureMqttPath;
  output["tank_level_sensor_type"] = _config.tankLevelSensorType;
  output["tank_level_mqtt_path"] = _config.tankLevelMqttPath;
  output["battery_level_sensor_type"] = _config.batteryLevelSensorType;
  output["battery_level_mqtt_path"] = _config.batteryLevelMqttPath;

  output["enable_membrane_pressure_high_check"] = _config.enableMembranePressureHighCheck;
  output["membrane_pressure_high_threshold"] = _config.membranePressureHighThreshold;
  output["membrane_pressure_high_delay"] = _config.membranePressureHighDelay;

  output["enable_membrane_pressure_low_check"] = _config.enableMembranePressureLowCheck;
  output["membrane_pressure_low_threshold"] = _config.membranePressureLowThreshold;
  output["membrane_pressure_low_delay"] = _config.membranePressureLowDelay;

  output["enable_filter_pressure_high_check"] = _config.enableFilterPressureHighCheck;
  output["filter_pressure_high_threshold"] = _config.filterPressureHighThreshold;
  output["filter_pressure_high_delay"] = _config.filterPressureHighDelay;

  output["enable_filter_pressure_low_check"] = _config.enableFilterPressureLowCheck;
  output["filter_pressure_low_threshold"] = _config.filterPressureLowThreshold;
  output["filter_pressure_low_delay"] = _config.filterPressureLowDelay;

  output["enable_product_flowrate_high_check"] = _config.enableProductFlowrateHighCheck;
  output["product_flowrate_high_threshold"] = _config.productFlowrateHighThreshold;
  output["product_flowrate_high_delay"] = _config.productFlowrateHighDelay;

  output["enable_product_flowrate_low_check"] = _config.enableProductFlowrateLowCheck;
  output["product_flowrate_low_threshold"] = _config.productFlowrateLowThreshold;
  output["product_flowrate_low_delay"] = _config.productFlowrateLowDelay;

  output["enable_run_total_flowrate_low_check"] = _config.enableRunTotalFlowrateLowCheck;
  output["run_total_flowrate_low_threshold"] = _config.runTotalFlowrateLowThreshold;
  output["run_total_flowrate_low_delay"] = _config.runTotalFlowrateLowDelay;

  output["enable_pickle_total_flowrate_low_check"] = _config.enablePickleTotalFlowrateLowCheck;
  output["pickle_total_flowrate_low_threshold"] = _config.pickleTotalFlowrateLowThreshold;
  output["pickle_total_flowrate_low_delay"] = _config.pickleTotalFlowrateLowDelay;

  output["enable_diverter_valve_closed_check"] = _config.enableDiverterValveClosedCheck;
  output["diverter_valve_closed_flowrate_high_threshold"] = _config.diverterValveClosedFlowrateHighThreshold;
  output["diverter_valve_closed_delay"] = _config.diverterValveClosedDelay;

  output["enable_product_salinity_high_check"] = _config.enableProductSalinityHighCheck;
  output["product_salinity_high_threshold"] = _config.productSalinityHighThreshold;
  output["product_salinity_high_delay"] = _config.productSalinityHighDelay;

  output["enable_motor_temperature_check"] = _config.enableMotorTemperatureCheck;
  output["motor_temperature_high_threshold"] = _config.motorTemperatureHighThreshold;
  output["motor_temperature_high_delay"] = _config.motorTemperatureHighDelay;

  output["enable_flush_flowrate_low_check"] = _config.enableFlushFlowrateLowCheck;
  output["flush_flowrate_low_threshold"] = _config.flushFlowrateLowThreshold;
  output["flush_flowrate_low_delay"] = _config.flushFlowrateLowDelay;

  output["enable_flush_filter_pressure_low_check"] = _config.enableFlushFilterPressureLowCheck;
  output["flush_filter_pressure_low_threshold"] = _config.flushFilterPressureLowThreshold;
  output["flush_filter_pressure_low_delay"] = _config.flushFilterPressureLowDelay;

  output["enable_flush_valve_off_check"] = _config.enableFlushValveOffCheck;
  output["flush_valve_off_threshold"] = _config.flushValveOffThreshold;
  output["flush_valve_off_delay"] = _config.flushValveOffDelay;

  output["enable_flush_tank_level_low_check"] = _config.enableFlushTankLevelLowCheck;
  output["flush_tank_level_low_threshold"] = _config.flushTankLevelLowThreshold;
  output["flush_tank_level_low_delay"] = _config.flushTankLevelLowDelay;

  output["enable_tank_level_full_check"] = _config.enableTankLevelFullCheck;
  output["tank_level_full_threshold"] = _config.tankLevelFullThreshold;
  output["tank_level_full_delay"] = _config.tankLevelFullDelay;

  output["enable_battery_level_low_check"] = _config.enableBatteryLevelLowCheck;
  output["battery_level_low_threshold"] = _config.batteryLevelLowThreshold;
}

bool Brineomatic::validateConfigJSON(JsonVariant config, char* error, size_t err_size)
{
  bool ok = true;

  if (!validateUIConfigJSON(config, error, err_size))
    ok = false;
  if (!validateGeneralConfigJSON(config, error, err_size))
    ok = false;
  if (!validateHardwareConfigJSON(config, error, err_size))
    ok = false;
  if (!validateSafeguardsConfigJSON(config, error, err_size))
    ok = false;

  return true;
}

bool Brineomatic::validateUIConfigJSON(JsonVariant config, char* error, size_t err_size)
{
  bool ok = true;
  return ok;
}

bool Brineomatic::validateGeneralConfigJSON(JsonVariant config, char* error, size_t err_size)
{
  bool ok = true;

  if (config["autoflush_mode"]) {
    if (!checkInclusion(config, "autoflush_mode", Brineomatic::AUTOFLUSH_MODES, error, err_size)) {
      ok = false;
      config.remove("autoflush_mode");
    }
  }

  // autoflush_salinity (integer >= 0)
  if (config["autoflush_salinity"]) {
    if (!checkIsNumber(config, "autoflush_salinity", error, err_size) ||
        !checkNumGT(config, "autoflush_salinity", 0, error, err_size)) {
      config.remove("autoflush_salinity");
      ok = false;
    }
  }

  // autoflush_duration (number >= 0)
  if (config["autoflush_duration"]) {
    if (!checkIsNumber(config, "autoflush_duration", error, err_size) ||
        !checkNumGT(config, "autoflush_duration", 0, error, err_size)) {
      config.remove("autoflush_duration");
      ok = false;
    }
  }

  // autoflush_volume (number >= 0)
  if (config["autoflush_volume"]) {
    if (!checkIsNumber(config, "autoflush_volume", error, err_size) ||
        !checkNumGT(config, "autoflush_volume", 0, error, err_size)) {
      config.remove("autoflush_volume");
      ok = false;
    }
  }

  // autoflush_interval (number >= 0)
  if (config["autoflush_interval"]) {
    if (!checkIsNumber(config, "autoflush_interval", error, err_size) ||
        !checkNumGT(config, "autoflush_interval", 0, error, err_size)) {
      config.remove("autoflush_interval");
      ok = false;
    }
  }

  // autoflush_use_high_pressure_motor (bool)
  if (config["autoflush_use_high_pressure_motor"]) {
    if (!checkIsBool(config, "autoflush_use_high_pressure_motor", error, err_size)) {
      config.remove("autoflush_use_high_pressure_motor");
      ok = false;
    }
  }

  // tank_capacity (number > 0)
  if (config["tank_capacity"]) {
    if (!checkIsNumber(config, "tank_capacity", error, err_size) ||
        !checkNumGT(config, "tank_capacity", 0, error, err_size)) {
      config.remove("tank_capacity");
      ok = false;
    }
  }

  // temperature_units (enum-like)
  if (config["temperature_units"]) {
    if (!checkInclusion(config, "temperature_units", Brineomatic::TEMPERATURE_UNITS, error, err_size)) {
      config.remove("temperature_units");
      ok = false;
    }
  }

  // pressure_units (enum-like)
  if (config["pressure_units"]) {
    if (!checkInclusion(config, "pressure_units", Brineomatic::PRESSURE_UNITS, error, err_size)) {
      config.remove("pressure_units");
      ok = false;
    }
  }

  // volume_units (enum-like)
  if (config["volume_units"]) {
    if (!checkInclusion(config, "volume_units", Brineomatic::VOLUME_UNITS, error, err_size)) {
      config.remove("volume_units");
      ok = false;
    }
  }

  // flowrate_units (enum-like)
  if (config["flowrate_units"]) {
    if (!checkInclusion(config, "flowrate_units", Brineomatic::FLOWRATE_UNITS, error, err_size)) {
      config.remove("flowrate_units");
      ok = false;
    }
  }

  return ok;
}

bool Brineomatic::validateHardwareConfigJSON(JsonVariant config,
  char* error,
  size_t err_size)
{
  bool ok = true;
  String control;

  // ---------------------------------------------------------
  // Boost Pump
  // ---------------------------------------------------------

  if (config["boost_pump_control"]) {
    if (!checkInclusion(config, "boost_pump_control", BOOST_PUMP_CONTROLS, error, err_size)) {
      config.remove("boost_pump_control");
      ok = false;
    }
  }

  if (config["boost_pump_relay_id"]) {
    if (!checkIsInteger(config, "boost_pump_relay_id", error, err_size) ||
        !checkIntGE(config, "boost_pump_relay_id", 0, error, err_size)) {
      config.remove("boost_pump_relay_id");
      ok = false;
    }
  }

  if (config["boost_pump_control"]) {
    control = config["boost_pump_control"].as<String>();
    if (control.equals("RELAY")) {
      auto* ch = _relays.getChannelById(config["boost_pump_relay_id"]);
      if (!ch) {
        snprintf(error, err_size, "boost_pump_relay_id %d not found", config["boost_pump_relay_id"].as<int>());
        config.remove("boost_pump_relay_id");
        ok = false;
      }
    }
  }

  if (config["boost_pump_delay"]) {
    if (!checkIsInteger(config, "boost_pump_delay", error, err_size) ||
        !checkIntGE(config, "boost_pump_delay", 0, error, err_size)) {
      config.remove("boost_pump_delay");
      ok = false;
    }
  }

  // ---------------------------------------------------------
  // High Pressure Pump
  // ---------------------------------------------------------

  if (config["high_pressure_pump_control"]) {
    if (!checkInclusion(config, "high_pressure_pump_control", HIGH_PRESSURE_PUMP_CONTROLS, error, err_size)) {
      config.remove("high_pressure_pump_control");
      ok = false;
    }
  }

  if (config["high_pressure_relay_id"]) {
    if (!checkIsInteger(config, "high_pressure_relay_id", error, err_size) ||
        !checkIntGE(config, "high_pressure_relay_id", 0, error, err_size)) {
      config.remove("high_pressure_relay_id");
      ok = false;
    }
  }

  if (config["high_pressure_pump_control"]) {
    control = config["high_pressure_pump_control"].as<String>();
    if (control.equals("RELAY")) {
      auto* ch = _relays.getChannelById(config["high_pressure_relay_id"]);
      if (!ch) {
        snprintf(error, err_size, "high_pressure_relay_id %d not found", config["high_pressure_relay_id"].as<int>());
        config.remove("high_pressure_relay_id");
        ok = false;
      }
    }
  }

  // modbus device selection
  if (config["high_pressure_modbus_device"]) {
    if (!checkInclusion(config, "high_pressure_modbus_device", HIGH_PRESSURE_PUMP_MODBUS_DEVICES, error, err_size)) {
      config.remove("high_pressure_modbus_device");
      ok = false;
    }
  }

  if (config["high_pressure_pump_delay"]) {
    if (!checkIsInteger(config, "high_pressure_pump_delay", error, err_size) ||
        !checkIntGE(config, "high_pressure_pump_delay", 0, error, err_size)) {
      config.remove("high_pressure_pump_delay");
      ok = false;
    }
  }

  // ---------------------------------------------------------
  // High Pressure Valve
  // ---------------------------------------------------------

  if (config["high_pressure_valve_control"]) {
    if (!checkInclusion(config, "high_pressure_valve_control", HIGH_PRESSURE_VALVE_CONTROLS, error, err_size)) {
      config.remove("high_pressure_valve_control");
      ok = false;
    }
  }

  if (config["membrane_pressure_target"]) {
    if (!checkIsNumber(config, "membrane_pressure_target", error, err_size) ||
        !checkNumGT(config, "membrane_pressure_target", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_target");
      ok = false;
    }
  }

  if (config["high_pressure_valve_stepper_id"]) {
    if (!checkIsInteger(config, "high_pressure_valve_stepper_id", error, err_size) ||
        !checkIntGE(config, "high_pressure_valve_stepper_id", 0, error, err_size)) {
      config.remove("high_pressure_valve_stepper_id");
      ok = false;
    }
  }

  if (config["high_pressure_valve_control"]) {
    control = config["high_pressure_valve_control"].as<String>();
    if (control.equals("STEPPER")) {
      auto* ch = _steppers.getChannelById(config["high_pressure_valve_stepper_id"]);
      if (!ch) {
        snprintf(error, err_size, "high_pressure_valve_stepper_id %d not found", config["high_pressure_valve_stepper_id"].as<int>());
        config.remove("high_pressure_valve_stepper_id");
        ok = false;
      }
    }
  }

  // Stepper numeric ranges
  if (config["high_pressure_stepper_step_angle"]) {
    if (!checkIsNumber(config, "high_pressure_stepper_step_angle", error, err_size) ||
        !checkNumRange(config, "high_pressure_stepper_step_angle", 0.0001f, 90.0f, error, err_size)) {
      config.remove("high_pressure_stepper_step_angle");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_gear_ratio"]) {
    if (!checkIsNumber(config, "high_pressure_stepper_gear_ratio", error, err_size) ||
        !checkNumGT(config, "high_pressure_stepper_gear_ratio", 0.0f, error, err_size)) {
      config.remove("high_pressure_stepper_gear_ratio");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_close_angle"]) {
    if (!checkIsNumber(config, "high_pressure_stepper_close_angle", error, err_size) ||
        !checkNumRange(config, "high_pressure_stepper_close_angle", 0.0f, 5000.0f, error, err_size)) {
      config.remove("high_pressure_stepper_close_angle");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_close_speed"]) {
    if (!checkIsNumber(config, "high_pressure_stepper_close_speed", error, err_size) ||
        !checkNumRange(config, "high_pressure_stepper_close_speed", 0.0001f, 200.0f, error, err_size)) {
      config.remove("high_pressure_stepper_close_speed");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_open_angle"]) {
    if (!checkIsNumber(config, "high_pressure_stepper_open_angle", error, err_size) ||
        !checkNumRange(config, "high_pressure_stepper_open_angle", 0.0f, 5000.0f, error, err_size)) {
      config.remove("high_pressure_stepper_open_angle");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_open_speed"]) {
    if (!checkIsNumber(config, "high_pressure_stepper_open_speed", error, err_size) ||
        !checkNumRange(config, "high_pressure_stepper_open_speed", 0.0001f, 200.0f, error, err_size)) {
      config.remove("high_pressure_stepper_open_speed");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_run_current"]) {
    if (!checkIsInteger(config, "high_pressure_stepper_run_current", error, err_size) ||
        !checkNumRange(config, "high_pressure_stepper_run_current", 0.0f, 100.0f, error, err_size)) {
      config.remove("high_pressure_stepper_run_current");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_home_current"]) {
    if (!checkIsInteger(config, "high_pressure_stepper_home_current", error, err_size) ||
        !checkNumRange(config, "high_pressure_stepper_home_current", 0.0f, 100.0f, error, err_size)) {
      config.remove("high_pressure_stepper_home_current");
      ok = false;
    }
  }

  if (config["high_pressure_stepper_inverted"]) {
    if (!checkIsBool(config, "high_pressure_stepper_inverted", error, err_size)) {
      config.remove("high_pressure_stepper_inverted");
      ok = false;
    }
  }

  // ---------------------------------------------------------
  // Diverter Valve
  // ---------------------------------------------------------

  if (config["diverter_valve_control"]) {
    if (!checkInclusion(config, "diverter_valve_control", DIVERTER_VALVE_CONTROLS, error, err_size)) {
      config.remove("diverter_valve_control");
      ok = false;
    }
  }

  if (config["diverter_valve_servo_id"]) {
    if (!checkIsInteger(config, "diverter_valve_servo_id", error, err_size) ||
        !checkIntGE(config, "diverter_valve_servo_id", 0, error, err_size)) {
      config.remove("diverter_valve_servo_id");
      ok = false;
    }
  }

  if (config["diverter_valve_relay_id"]) {
    if (!checkIsInteger(config, "diverter_valve_relay_id", error, err_size) ||
        !checkIntGE(config, "diverter_valve_relay_id", 0, error, err_size)) {
      config.remove("diverter_valve_relay_id");
      ok = false;
    }
  }

  if (config["diverter_valve_control"]) {
    control = config["diverter_valve_control"].as<String>();
    if (control.equals("SERVO")) {
      auto* ch = _servos.getChannelById(config["diverter_valve_servo_id"]);
      if (!ch) {
        snprintf(error, err_size, "diverter_valve_servo_id %d not found", config["diverter_valve_servo_id"].as<int>());
        config.remove("diverter_valve_servo_id");
        ok = false;
      }
    } else if (control.equals("RELAY")) {
      auto* ch = _relays.getChannelById(config["diverter_valve_relay_id"]);
      if (!ch) {
        snprintf(error, err_size, "diverter_valve_relay_id %d not found", config["diverter_valve_relay_id"].as<int>());
        config.remove("diverter_valve_relay_id");
        ok = false;
      }
    } else if (control.equals("DUAL_RELAYS")) {
      auto* tankCh = _relays.getChannelById(config["diverter_valve_tank_relay_id"]);
      if (!tankCh) {
        snprintf(error, err_size, "diverter_valve_tank_relay_id %d not found", config["diverter_valve_tank_relay_id"].as<int>());
        config.remove("diverter_valve_tank_relay_id");
        ok = false;
      }
      auto* overboardCh = _relays.getChannelById(config["diverter_valve_overboard_relay_id"]);
      if (!overboardCh) {
        snprintf(error, err_size, "diverter_valve_overboard_relay_id %d not found", config["diverter_valve_overboard_relay_id"].as<int>());
        config.remove("diverter_valve_overboard_relay_id");
        ok = false;
      }
    }
  }

  if (config["diverter_valve_tank_relay_id"]) {
    if (!checkIsInteger(config, "diverter_valve_tank_relay_id", error, err_size) ||
        !checkIntGE(config, "diverter_valve_tank_relay_id", 0, error, err_size)) {
      config.remove("diverter_valve_tank_relay_id");
      ok = false;
    }
  }

  if (config["diverter_valve_overboard_relay_id"]) {
    if (!checkIsInteger(config, "diverter_valve_overboard_relay_id", error, err_size) ||
        !checkIntGE(config, "diverter_valve_overboard_relay_id", 0, error, err_size)) {
      config.remove("diverter_valve_overboard_relay_id");
      ok = false;
    }
  }

  if (config["diverter_valve_relay_change_interval"]) {
    if (!checkIsInteger(config, "diverter_valve_relay_change_interval", error, err_size) ||
        !checkIntGE(config, "diverter_valve_relay_change_interval", 0, error, err_size)) {
      config.remove("diverter_valve_relay_change_interval");
      ok = false;
    }
  }

  if (config["diverter_valve_open_angle"]) {
    if (!checkIsNumber(config, "diverter_valve_open_angle", error, err_size) ||
        !checkNumRange(config, "diverter_valve_open_angle", 0.0f, 180.0f, error, err_size)) {
      config.remove("diverter_valve_open_angle");
      ok = false;
    }
  }

  if (config["diverter_valve_close_angle"]) {
    if (!checkIsNumber(config, "diverter_valve_close_angle", error, err_size) ||
        !checkNumRange(config, "diverter_valve_close_angle", 0.0f, 180.0f, error, err_size)) {
      config.remove("diverter_valve_close_angle");
      ok = false;
    }
  }

  // ---------------------------------------------------------
  // Flush Valve
  // ---------------------------------------------------------

  if (config["flush_valve_control"]) {
    if (!checkInclusion(config, "flush_valve_control", FLUSH_VALVE_CONTROLS, error, err_size)) {
      config.remove("flush_valve_control");
      ok = false;
    }
  }

  if (config["flush_valve_relay_id"]) {
    if (!checkIsInteger(config, "flush_valve_relay_id", error, err_size) ||
        !checkIntGE(config, "flush_valve_relay_id", 0, error, err_size)) {
      config.remove("flush_valve_relay_id");
      ok = false;
    }
  }

  if (config["flush_valve_control"]) {
    control = config["flush_valve_control"].as<String>();
    if (control.equals("RELAY")) {
      auto* ch = _relays.getChannelById(config["flush_valve_relay_id"]);
      if (!ch) {
        snprintf(error, err_size, "flush_valve_relay_id %d not found", config["flush_valve_relay_id"].as<int>());
        config.remove("flush_valve_relay_id");
        ok = false;
      }
    }
  }

  // ---------------------------------------------------------
  // Cooling Fan
  // ---------------------------------------------------------

  if (config["cooling_fan_control"]) {
    if (!checkInclusion(config, "cooling_fan_control", COOLING_FAN_CONTROLS, error, err_size)) {
      config.remove("cooling_fan_control");
      ok = false;
    }
  }

  if (config["cooling_fan_relay_id"]) {
    if (!checkIsInteger(config, "cooling_fan_relay_id", error, err_size) ||
        !checkIntGE(config, "cooling_fan_relay_id", 0, error, err_size)) {
      config.remove("cooling_fan_relay_id");
      ok = false;
    }
  }

  if (config["cooling_fan_control"]) {
    control = config["cooling_fan_control"].as<String>();
    if (control.equals("RELAY")) {
      auto* ch = _relays.getChannelById(config["cooling_fan_relay_id"]);
      if (!ch) {
        snprintf(error, err_size, "cooling_fan_relay_id %d not found", config["cooling_fan_relay_id"].as<int>());
        config.remove("cooling_fan_relay_id");
        ok = false;
      }
    }
  }

  if (config["cooling_fan_on_temperature"]) {
    if (!checkIsNumber(config, "cooling_fan_on_temperature", error, err_size) ||
        !checkNumRange(config, "cooling_fan_on_temperature", 0.0f, 100.0f, error, err_size)) {
      config.remove("cooling_fan_on_temperature");
      ok = false;
    }
  }

  if (config["cooling_fan_off_temperature"]) {
    if (!checkIsNumber(config, "cooling_fan_off_temperature", error, err_size) ||
        !checkNumRange(config, "cooling_fan_off_temperature", 0.0f, 100.0f, error, err_size)) {
      config.remove("cooling_fan_off_temperature");
      ok = false;
    }
  }

  // ---------------------------------------------------------
  // Sensor Flags and Ranges
  // ---------------------------------------------------------

  if (config["has_membrane_pressure_sensor"]) {
    if (!checkIsBool(config, "has_membrane_pressure_sensor", error, err_size)) {
      config.remove("has_membrane_pressure_sensor");
      ok = false;
    }
  }

  if (config["membrane_pressure_sensor_min"]) {
    if (!checkIsNumber(config, "membrane_pressure_sensor_min", error, err_size) ||
        !checkNumGE(config, "membrane_pressure_sensor_min", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_sensor_min");
      ok = false;
    }
  }

  if (config["membrane_pressure_sensor_max"]) {
    if (!checkIsNumber(config, "membrane_pressure_sensor_max", error, err_size) ||
        !checkNumGT(config, "membrane_pressure_sensor_max", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_sensor_max");
      ok = false;
    }
  }

  if (config["has_filter_pressure_sensor"]) {
    if (!checkIsBool(config, "has_filter_pressure_sensor", error, err_size)) {
      config.remove("has_filter_pressure_sensor");
      ok = false;
    }
  }

  if (config["filter_pressure_sensor_min"]) {
    if (!checkIsNumber(config, "filter_pressure_sensor_min", error, err_size) ||
        !checkNumGE(config, "filter_pressure_sensor_min", 0.0f, error, err_size)) {
      config.remove("filter_pressure_sensor_min");
      ok = false;
    }
  }

  if (config["filter_pressure_sensor_max"]) {
    if (!checkIsNumber(config, "filter_pressure_sensor_max", error, err_size) ||
        !checkNumGT(config, "filter_pressure_sensor_max", 0.0f, error, err_size)) {
      config.remove("filter_pressure_sensor_max");
      ok = false;
    }
  }

  if (config["has_product_tds_sensor"]) {
    if (!checkIsBool(config, "has_product_tds_sensor", error, err_size)) {
      config.remove("has_product_tds_sensor");
      ok = false;
    }
  }

  if (config["product_tds_sensor_offset"]) {
    if (!checkIsNumber(config, "product_tds_sensor_offset", error, err_size) ||
        !checkNumRange(config, "product_tds_sensor_offset", -1000.0f, 1000.0f, error, err_size)) {
      config.remove("product_tds_sensor_offset");
      ok = false;
    }
  }

  if (config["has_brine_tds_sensor"]) {
    if (!checkIsBool(config, "has_brine_tds_sensor", error, err_size)) {
      config.remove("has_brine_tds_sensor");
      ok = false;
    }
  }

  if (config["brine_tds_sensor_offset"]) {
    if (!checkIsNumber(config, "brine_tds_sensor_offset", error, err_size) ||
        !checkNumRange(config, "brine_tds_sensor_offset", -1000.0f, 1000.0f, error, err_size)) {
      config.remove("brine_tds_sensor_offset");
      ok = false;
    }
  }

  if (config["has_product_flow_sensor"]) {
    if (!checkIsBool(config, "has_product_flow_sensor", error, err_size)) {
      config.remove("has_product_flow_sensor");
      ok = false;
    }
  }

  if (config["product_flowmeter_ppl"]) {
    if (!checkIsNumber(config, "product_flowmeter_ppl", error, err_size) ||
        !checkNumGT(config, "product_flowmeter_ppl", 0.0f, error, err_size)) {
      config.remove("product_flowmeter_ppl");
      ok = false;
    }
  }

  if (config["has_brine_flow_sensor"]) {
    if (!checkIsBool(config, "has_brine_flow_sensor", error, err_size)) {
      config.remove("has_brine_flow_sensor");
      ok = false;
    }
  }

  if (config["brine_flowmeter_ppl"]) {
    if (!checkIsNumber(config, "brine_flowmeter_ppl", error, err_size) ||
        !checkNumGT(config, "brine_flowmeter_ppl", 0.0f, error, err_size)) {
      config.remove("brine_flowmeter_ppl");
      ok = false;
    }
  }

  if (config["motor_temperature_sensor_type"]) {
    if (!checkInclusion(config, "motor_temperature_sensor_type", MOTOR_TEMPERATURE_TYPES, error, err_size)) {
      config.remove("motor_temperature_sensor_type");
      ok = false;
    }
  }

  if (config["motor_temperature_mqtt_path"]) {
    const char* path = config["motor_temperature_mqtt_path"];
    if (strlen(path) > 255) {
      snprintf(error, err_size, "motor_temperature_mqtt_path must be 255 characters or fewer");
      config.remove("motor_temperature_mqtt_path");
      ok = false;
    }
  }

  if (config["water_temperature_sensor_type"]) {
    if (!checkInclusion(config, "water_temperature_sensor_type", WATER_TEMPERATURE_TYPES, error, err_size)) {
      config.remove("water_temperature_sensor_type");
      ok = false;
    }
  }

  if (config["water_temperature_mqtt_path"]) {
    const char* path = config["water_temperature_mqtt_path"];
    if (strlen(path) > 255) {
      snprintf(error, err_size, "water_temperature_mqtt_path must be 255 characters or fewer");
      config.remove("water_temperature_mqtt_path");
      ok = false;
    }
  }

  if (config["tank_level_sensor_type"]) {
    if (!checkInclusion(config, "tank_level_sensor_type", TANK_LEVEL_SENSOR_TYPES, error, err_size)) {
      config.remove("tank_level_sensor_type");
      ok = false;
    }
  }

  if (config["tank_level_mqtt_path"]) {
    const char* path = config["tank_level_mqtt_path"];
    if (strlen(path) > 255) {
      snprintf(error, err_size, "tank_level_mqtt_path must be 255 characters or fewer");
      config.remove("tank_level_mqtt_path");
      ok = false;
    }
  }

  if (config["battery_level_sensor_type"]) {
    if (!checkInclusion(config, "battery_level_sensor_type", BATTERY_LEVEL_SENSOR_TYPES, error, err_size)) {
      config.remove("battery_level_sensor_type");
      ok = false;
    }
  }

  if (config["battery_level_mqtt_path"]) {
    const char* path = config["battery_level_mqtt_path"];
    if (strlen(path) > 255) {
      snprintf(error, err_size, "battery_level_mqtt_path must be 255 characters or fewer");
      config.remove("battery_level_mqtt_path");
      ok = false;
    }
  }

  return ok;
}

bool Brineomatic::validateSafeguardsConfigJSON(JsonVariant config,
  char* error,
  size_t err_size)
{
  bool ok = true;

  // ---------------------------------------------------------
  // Basic timeout fields (number > 0)
  // ---------------------------------------------------------

  if (config["flush_timeout"]) {
    if (!checkIsNumber(config, "flush_timeout", error, err_size) ||
        !checkNumGT(config, "flush_timeout", 0.0f, error, err_size)) {
      config.remove("flush_timeout");
      ok = false;
    }
  }

  if (config["membrane_pressure_timeout"]) {
    if (!checkIsNumber(config, "membrane_pressure_timeout", error, err_size) ||
        !checkNumGT(config, "membrane_pressure_timeout", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_timeout");
      ok = false;
    }
  }

  if (config["product_flowrate_timeout"]) {
    if (!checkIsNumber(config, "product_flowrate_timeout", error, err_size) ||
        !checkNumGT(config, "product_flowrate_timeout", 0.0f, error, err_size)) {
      config.remove("product_flowrate_timeout");
      ok = false;
    }
  }

  if (config["product_salinity_timeout"]) {
    if (!checkIsNumber(config, "product_salinity_timeout", error, err_size) ||
        !checkNumGT(config, "product_salinity_timeout", 0.0f, error, err_size)) {
      config.remove("product_salinity_timeout");
      ok = false;
    }
  }

  if (config["membrane_pressure_stabilization_time"]) {
    if (!checkIsNumber(config, "membrane_pressure_stabilization_time", error, err_size) ||
        !checkNumGT(config, "membrane_pressure_stabilization_time", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_stabilization_time");
      ok = false;
    }
  }

  if (config["product_flowrate_stabilization_time"]) {
    if (!checkIsNumber(config, "product_flowrate_stabilization_time", error, err_size) ||
        !checkNumGT(config, "product_flowrate_stabilization_time", 0.0f, error, err_size)) {
      config.remove("product_flowrate_stabilization_time");
      ok = false;
    }
  }

  if (config["product_salinity_stabilization_time"]) {
    if (!checkIsNumber(config, "product_salinity_stabilization_time", error, err_size) ||
        !checkNumGT(config, "product_salinity_stabilization_time", 0.0f, error, err_size)) {
      config.remove("product_salinity_stabilization_time");
      ok = false;
    }
  }

  if (config["production_runtime_timeout"]) {
    if (!checkIsNumber(config, "production_runtime_timeout", error, err_size) ||
        !checkNumGT(config, "production_runtime_timeout", 0.0f, error, err_size)) {
      config.remove("production_runtime_timeout");
      ok = false;
    }
  }

  // ---------------------------------------------------------
  // Repeated patterns: boolean enable + threshold/delay
  // ---------------------------------------------------------

  // enable_membrane_pressure_high_check
  if (config["enable_membrane_pressure_high_check"]) {
    if (!checkIsBool(config, "enable_membrane_pressure_high_check", error, err_size)) {
      config.remove("enable_membrane_pressure_high_check");
      ok = false;
    }
  }
  if (config["membrane_pressure_high_threshold"]) {
    if (!checkIsNumber(config, "membrane_pressure_high_threshold", error, err_size) ||
        !checkNumGT(config, "membrane_pressure_high_threshold", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_high_threshold");
      ok = false;
    }
  }
  if (config["membrane_pressure_high_delay"]) {
    if (!checkIsNumber(config, "membrane_pressure_high_delay", error, err_size) ||
        !checkNumGE(config, "membrane_pressure_high_delay", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_high_delay");
      ok = false;
    }
  }

  // enable_membrane_pressure_low_check
  if (config["enable_membrane_pressure_low_check"]) {
    if (!checkIsBool(config, "enable_membrane_pressure_low_check", error, err_size)) {
      config.remove("enable_membrane_pressure_low_check");
      ok = false;
    }
  }
  if (config["membrane_pressure_low_threshold"]) {
    if (!checkIsNumber(config, "membrane_pressure_low_threshold", error, err_size) ||
        !checkNumGT(config, "membrane_pressure_low_threshold", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_low_threshold");
      ok = false;
    }
  }
  if (config["membrane_pressure_low_delay"]) {
    if (!checkIsNumber(config, "membrane_pressure_low_delay", error, err_size) ||
        !checkNumGE(config, "membrane_pressure_low_delay", 0.0f, error, err_size)) {
      config.remove("membrane_pressure_low_delay");
      ok = false;
    }
  }

  // enable_filter_pressure_high_check
  if (config["enable_filter_pressure_high_check"]) {
    if (!checkIsBool(config, "enable_filter_pressure_high_check", error, err_size)) {
      config.remove("enable_filter_pressure_high_check");
      ok = false;
    }
  }
  if (config["filter_pressure_high_threshold"]) {
    if (!checkIsNumber(config, "filter_pressure_high_threshold", error, err_size) ||
        !checkNumGT(config, "filter_pressure_high_threshold", 0.0f, error, err_size)) {
      config.remove("filter_pressure_high_threshold");
      ok = false;
    }
  }
  if (config["filter_pressure_high_delay"]) {
    if (!checkIsNumber(config, "filter_pressure_high_delay", error, err_size) ||
        !checkNumGE(config, "filter_pressure_high_delay", 0.0f, error, err_size)) {
      config.remove("filter_pressure_high_delay");
      ok = false;
    }
  }

  // enable_filter_pressure_low_check
  if (config["enable_filter_pressure_low_check"]) {
    if (!checkIsBool(config, "enable_filter_pressure_low_check", error, err_size)) {
      config.remove("enable_filter_pressure_low_check");
      ok = false;
    }
  }
  if (config["filter_pressure_low_threshold"]) {
    if (!checkIsNumber(config, "filter_pressure_low_threshold", error, err_size) ||
        !checkNumGT(config, "filter_pressure_low_threshold", 0.0f, error, err_size)) {
      config.remove("filter_pressure_low_threshold");
      ok = false;
    }
  }
  if (config["filter_pressure_low_delay"]) {
    if (!checkIsNumber(config, "filter_pressure_low_delay", error, err_size) ||
        !checkNumGE(config, "filter_pressure_low_delay", 0.0f, error, err_size)) {
      config.remove("filter_pressure_low_delay");
      ok = false;
    }
  }

  // enable_product_flowrate_high_check
  if (config["enable_product_flowrate_high_check"]) {
    if (!checkIsBool(config, "enable_product_flowrate_high_check", error, err_size)) {
      config.remove("enable_product_flowrate_high_check");
      ok = false;
    }
  }
  if (config["product_flowrate_high_threshold"]) {
    if (!checkIsNumber(config, "product_flowrate_high_threshold", error, err_size) ||
        !checkNumGT(config, "product_flowrate_high_threshold", 0.0f, error, err_size)) {
      config.remove("product_flowrate_high_threshold");
      ok = false;
    }
  }
  if (config["product_flowrate_high_delay"]) {
    if (!checkIsNumber(config, "product_flowrate_high_delay", error, err_size) ||
        !checkNumGE(config, "product_flowrate_high_delay", 0.0f, error, err_size)) {
      config.remove("product_flowrate_high_delay");
      ok = false;
    }
  }

  // enable_product_flowrate_low_check
  if (config["enable_product_flowrate_low_check"]) {
    if (!checkIsBool(config, "enable_product_flowrate_low_check", error, err_size)) {
      config.remove("enable_product_flowrate_low_check");
      ok = false;
    }
  }
  if (config["product_flowrate_low_threshold"]) {
    if (!checkIsNumber(config, "product_flowrate_low_threshold", error, err_size) ||
        !checkNumGT(config, "product_flowrate_low_threshold", 0.0f, error, err_size)) {
      config.remove("product_flowrate_low_threshold");
      ok = false;
    }
  }
  if (config["product_flowrate_low_delay"]) {
    if (!checkIsNumber(config, "product_flowrate_low_delay", error, err_size) ||
        !checkNumGE(config, "product_flowrate_low_delay", 0.0f, error, err_size)) {
      config.remove("product_flowrate_low_delay");
      ok = false;
    }
  }

  // enable_run_total_flowrate_low_check
  if (config["enable_run_total_flowrate_low_check"]) {
    if (!checkIsBool(config, "enable_run_total_flowrate_low_check", error, err_size)) {
      config.remove("enable_run_total_flowrate_low_check");
      ok = false;
    }
  }
  if (config["run_total_flowrate_low_threshold"]) {
    if (!checkIsNumber(config, "run_total_flowrate_low_threshold", error, err_size) ||
        !checkNumGT(config, "run_total_flowrate_low_threshold", 0.0f, error, err_size)) {
      config.remove("run_total_flowrate_low_threshold");
      ok = false;
    }
  }
  if (config["run_total_flowrate_low_delay"]) {
    if (!checkIsNumber(config, "run_total_flowrate_low_delay", error, err_size) ||
        !checkNumGE(config, "run_total_flowrate_low_delay", 0.0f, error, err_size)) {
      config.remove("run_total_flowrate_low_delay");
      ok = false;
    }
  }

  // enable_pickle_total_flowrate_low_check
  if (config["enable_pickle_total_flowrate_low_check"]) {
    if (!checkIsBool(config, "enable_pickle_total_flowrate_low_check", error, err_size)) {
      config.remove("enable_pickle_total_flowrate_low_check");
      ok = false;
    }
  }
  if (config["pickle_total_flowrate_low_threshold"]) {
    if (!checkIsNumber(config, "pickle_total_flowrate_low_threshold", error, err_size) ||
        !checkNumGT(config, "pickle_total_flowrate_low_threshold", 0.0f, error, err_size)) {
      config.remove("pickle_total_flowrate_low_threshold");
      ok = false;
    }
  }
  if (config["pickle_total_flowrate_low_delay"]) {
    if (!checkIsNumber(config, "pickle_total_flowrate_low_delay", error, err_size) ||
        !checkNumGE(config, "pickle_total_flowrate_low_delay", 0.0f, error, err_size)) {
      config.remove("pickle_total_flowrate_low_delay");
      ok = false;
    }
  }

  // enable_diverter_valve_closed_check
  if (config["enable_diverter_valve_closed_check"]) {
    if (!checkIsBool(config, "enable_diverter_valve_closed_check", error, err_size)) {
      config.remove("enable_diverter_valve_closed_check");
      ok = false;
    }
  }
  if (config["diverter_valve_closed_high_threshold"]) {
    if (!checkIsNumber(config, "diverter_valve_closed_high_threshold", error, err_size) ||
        !checkNumGE(config, "diverter_valve_closed_high_threshold", 0.0f, error, err_size)) {
      config.remove("diverter_valve_closed_high_threshold");
      ok = false;
    }
  }
  if (config["diverter_valve_closed_delay"]) {
    if (!checkIsNumber(config, "diverter_valve_closed_delay", error, err_size) ||
        !checkNumGE(config, "diverter_valve_closed_delay", 0.0f, error, err_size)) {
      config.remove("diverter_valve_closed_delay");
      ok = false;
    }
  }

  // enable_product_salinity_high_check
  if (config["enable_product_salinity_high_check"]) {
    if (!checkIsBool(config, "enable_product_salinity_high_check", error, err_size)) {
      config.remove("enable_product_salinity_high_check");
      ok = false;
    }
  }
  if (config["product_salinity_high_threshold"]) {
    if (!checkIsNumber(config, "product_salinity_high_threshold", error, err_size) ||
        !checkNumGT(config, "product_salinity_high_threshold", 0.0f, error, err_size)) {
      config.remove("product_salinity_high_threshold");
      ok = false;
    }
  }
  if (config["product_salinity_high_delay"]) {
    if (!checkIsNumber(config, "product_salinity_high_delay", error, err_size) ||
        !checkNumGE(config, "product_salinity_high_delay", 0.0f, error, err_size)) {
      config.remove("product_salinity_high_delay");
      ok = false;
    }
  }

  // enable_motor_temperature_check
  if (config["enable_motor_temperature_check"]) {
    if (!checkIsBool(config, "enable_motor_temperature_check", error, err_size)) {
      config.remove("enable_motor_temperature_check");
      ok = false;
    }
  }
  if (config["motor_temperature_high_threshold"]) {
    if (!checkIsNumber(config, "motor_temperature_high_threshold", error, err_size) ||
        !checkNumGT(config, "motor_temperature_high_threshold", 0.0f, error, err_size)) {
      config.remove("motor_temperature_high_threshold");
      ok = false;
    }
  }
  if (config["motor_temperature_high_delay"]) {
    if (!checkIsNumber(config, "motor_temperature_high_delay", error, err_size) ||
        !checkNumGE(config, "motor_temperature_high_delay", 0.0f, error, err_size)) {
      config.remove("motor_temperature_high_delay");
      ok = false;
    }
  }

  // enable_flush_flowrate_low_check
  if (config["enable_flush_flowrate_low_check"]) {
    if (!checkIsBool(config, "enable_flush_flowrate_low_check", error, err_size)) {
      config.remove("enable_flush_flowrate_low_check");
      ok = false;
    }
  }
  if (config["flush_flowrate_low_threshold"]) {
    if (!checkIsNumber(config, "flush_flowrate_low_threshold", error, err_size) ||
        !checkNumGT(config, "flush_flowrate_low_threshold", 0.0f, error, err_size)) {
      config.remove("flush_flowrate_low_threshold");
      ok = false;
    }
  }
  if (config["flush_flowrate_low_delay"]) {
    if (!checkIsNumber(config, "flush_flowrate_low_delay", error, err_size) ||
        !checkNumGE(config, "flush_flowrate_low_delay", 0.0f, error, err_size)) {
      config.remove("flush_flowrate_low_delay");
      ok = false;
    }
  }

  // enable_flush_filter_pressure_low_check
  if (config["enable_flush_filter_pressure_low_check"]) {
    if (!checkIsBool(config, "enable_flush_filter_pressure_low_check", error, err_size)) {
      config.remove("enable_flush_filter_pressure_low_check");
      ok = false;
    }
  }
  if (config["flush_filter_pressure_low_threshold"]) {
    if (!checkIsNumber(config, "flush_filter_pressure_low_threshold", error, err_size) ||
        !checkNumGT(config, "flush_filter_pressure_low_threshold", 0.0f, error, err_size)) {
      config.remove("flush_filter_pressure_low_threshold");
      ok = false;
    }
  }
  if (config["flush_filter_pressure_low_delay"]) {
    if (!checkIsNumber(config, "flush_filter_pressure_low_delay", error, err_size) ||
        !checkNumGE(config, "flush_filter_pressure_low_delay", 0.0f, error, err_size)) {
      config.remove("flush_filter_pressure_low_delay");
      ok = false;
    }
  }

  // enable_flush_valve_off_check
  if (config["enable_flush_valve_off_check"]) {
    if (!checkIsBool(config, "enable_flush_valve_off_check", error, err_size)) {
      config.remove("enable_flush_valve_off_check");
      ok = false;
    }
  }

  if (config["flush_valve_off_threshold"]) {
    if (!checkIsNumber(config, "flush_valve_off_threshold", error, err_size) ||
        !checkNumGT(config, "flush_valve_off_threshold", 0.0f, error, err_size)) {
      config.remove("flush_valve_off_threshold");
      ok = false;
    }
  }

  if (config["flush_valve_off_delay"]) {
    if (!checkIsNumber(config, "flush_valve_off_delay", error, err_size) ||
        !checkNumGE(config, "flush_valve_off_delay", 0.0f, error, err_size)) {
      config.remove("flush_valve_off_delay");
      ok = false;
    }
  }

  if (config["enable_flush_tank_level_low_check"]) {
    if (!checkIsBool(config, "enable_flush_tank_level_low_check", error, err_size)) {
      config.remove("enable_flush_tank_level_low_check");
      ok = false;
    }
  }

  if (config["flush_tank_level_low_threshold"]) {
    if (!checkIsNumber(config, "flush_tank_level_low_threshold", error, err_size) ||
        !checkNumGT(config, "flush_tank_level_low_threshold", 0.0f, error, err_size)) {
      config.remove("flush_tank_level_low_threshold");
      ok = false;
    }
  }

  if (config["flush_tank_level_low_delay"]) {
    if (!checkIsNumber(config, "flush_tank_level_low_delay", error, err_size) ||
        !checkNumGE(config, "flush_tank_level_low_delay", 0.0f, error, err_size)) {
      config.remove("flush_tank_level_low_delay");
      ok = false;
    }
  }

  // enable_battery_level_low_check
  if (config["enable_battery_level_low_check"]) {
    if (!checkIsBool(config, "enable_battery_level_low_check", error, err_size)) {
      config.remove("enable_battery_level_low_check");
      ok = false;
    }
  }

  if (config["battery_level_low_threshold"]) {
    if (!checkIsNumber(config, "battery_level_low_threshold", error, err_size) ||
        !checkNumGT(config, "battery_level_low_threshold", 0.0f, error, err_size)) {
      config.remove("battery_level_low_threshold");
      ok = false;
    }
  }

  return ok;
}

void Brineomatic::loadConfigJSON(JsonVariantConst config)
{
  this->loadUIConfigJSON(config);
  this->loadGeneralConfigJSON(config);
  this->loadHardwareConfigJSON(config);
  this->loadSafeguardsConfigJSON(config);
}

void Brineomatic::loadUIConfigJSON(JsonVariantConst config)
{
  _config.gaugeOrder = config["gauge_order"] | defaults.gaugeOrder;
}

void Brineomatic::loadGeneralConfigJSON(JsonVariantConst config)
{
  _config.temperatureUnits = config["temperature_units"] | defaults.temperatureUnits;
  _config.pressureUnits = config["pressure_units"] | defaults.pressureUnits;
  _config.volumeUnits = config["volume_units"] | defaults.volumeUnits;
  _config.flowrateUnits = config["flowrate_units"] | defaults.flowrateUnits;
  _config.successMelody = config["success_melody"] | defaults.successMelody;
  _config.errorMelody = config["error_melody"] | defaults.errorMelody;
}

void Brineomatic::loadHardwareConfigJSON(JsonVariantConst config)
{
  _config.boostPumpControl = config["boost_pump_control"] | defaults.boostPumpControl;
  _config.boostPumpRelayId = config["boost_pump_relay_id"] | defaults.boostPumpRelayId;
  _config.boostPumpRelayInverted = config["boost_pump_relay_inverted"] | defaults.boostPumpRelayInverted;
  _config.boostPumpDelay = config["boost_pump_delay"] | defaults.boostPumpDelay;

  _config.highPressurePumpControl = config["high_pressure_pump_control"] | defaults.highPressurePumpControl;
  _config.highPressureRelayId = config["high_pressure_relay_id"] | defaults.highPressureRelayId;
  _config.highPressureRelayInverted = config["high_pressure_relay_inverted"] | defaults.highPressureRelayInverted;
  _config.highPressurePumpModbusDevice = config["high_pressure_modbus_device"] | defaults.highPressurePumpModbusDevice;
  _config.highPressurePumpModbusSlaveId = config["high_pressure_modbus_slave_id"] | defaults.highPressurePumpModbusSlaveId;
  _config.highPressurePumpModbusFrequency = config["high_pressure_modbus_frequency"] | defaults.highPressurePumpModbusFrequency;
  _config.highPressurePumpDelay = config["high_pressure_pump_delay"] | defaults.highPressurePumpDelay;

  _config.highPressureValveControl = config["high_pressure_valve_control"] | defaults.highPressureValveControl;
  _config.membranePressureTarget = config["membrane_pressure_target"] | defaults.membranePressureTarget;
  _config.highPressureValveStepperId = config["high_pressure_valve_stepper_id"] | defaults.highPressureValveStepperId;
  _config.highPressureValveStepperStepAngle = config["high_pressure_stepper_step_angle"] | defaults.highPressureValveStepperStepAngle;
  _config.highPressureValveStepperGearRatio = config["high_pressure_stepper_gear_ratio"] | defaults.highPressureValveStepperGearRatio;
  _config.highPressureValveStepperCloseAngle = config["high_pressure_stepper_close_angle"] | defaults.highPressureValveStepperCloseAngle;
  _config.highPressureValveStepperCloseSpeed = config["high_pressure_stepper_close_speed"] | defaults.highPressureValveStepperCloseSpeed;
  _config.highPressureValveStepperOpenAngle = config["high_pressure_stepper_open_angle"] | defaults.highPressureValveStepperOpenAngle;
  _config.highPressureValveStepperOpenSpeed = config["high_pressure_stepper_open_speed"] | defaults.highPressureValveStepperOpenSpeed;
  _config.highPressureValveStepperRunCurrent = config["high_pressure_stepper_run_current"] | defaults.highPressureValveStepperRunCurrent;
  _config.highPressureValveStepperHomeCurrent = config["high_pressure_stepper_home_current"] | defaults.highPressureValveStepperHomeCurrent;
  _config.highPressureStepperInverted = config["high_pressure_stepper_inverted"] | defaults.highPressureStepperInverted;

  _config.diverterValveControl = config["diverter_valve_control"] | defaults.diverterValveControl;
  _config.diverterValveRelayId = config["diverter_valve_relay_id"] | defaults.diverterValveRelayId;
  _config.diverterValveRelayInverted = config["diverter_valve_relay_inverted"] | defaults.diverterValveRelayInverted;
  _config.diverterValveServoId = config["diverter_valve_servo_id"] | defaults.diverterValveServoId;
  _config.diverterValveOpenAngle = config["diverter_valve_open_angle"] | defaults.diverterValveOpenAngle;
  _config.diverterValveCloseAngle = config["diverter_valve_close_angle"] | defaults.diverterValveCloseAngle;
  _config.diverterValveTankRelayId = config["diverter_valve_tank_relay_id"] | defaults.diverterValveTankRelayId;
  _config.diverterValveTankRelayInverted = config["diverter_valve_tank_relay_inverted"] | defaults.diverterValveTankRelayInverted;
  _config.diverterValveOverboardRelayId = config["diverter_valve_overboard_relay_id"] | defaults.diverterValveOverboardRelayId;
  _config.diverterValveOverboardRelayInverted = config["diverter_valve_overboard_relay_inverted"] | defaults.diverterValveOverboardRelayInverted;
  _config.diverterValveRelayChangeInterval = config["diverter_valve_relay_change_interval"] | defaults.diverterValveRelayChangeInterval;

  _config.flushValveControl = config["flush_valve_control"] | defaults.flushValveControl;
  _config.flushValveRelayId = config["flush_valve_relay_id"] | defaults.flushValveRelayId;
  _config.flushValveRelayInverted = config["flush_valve_relay_inverted"] | defaults.flushValveRelayInverted;
  _config.flushValveServoId = config["flush_valve_servo_id"] | defaults.flushValveServoId;
  _config.flushValveOpenAngle = config["flush_valve_open_angle"] | defaults.flushValveOpenAngle;
  _config.flushValveCloseAngle = config["flush_valve_close_angle"] | defaults.flushValveCloseAngle;

  _config.autoflushMode = config["autoflush_mode"] | defaults.autoflushMode;
  _config.autoflushSalinity = config["autoflush_salinity"] | defaults.autoflushSalinity;
  _config.autoflushDuration = config["autoflush_duration"] | defaults.autoflushDuration;
  _config.autoflushVolume = config["autoflush_volume"] | defaults.autoflushVolume;
  _config.autoflushInterval = config["autoflush_interval"] | defaults.autoflushInterval;
  _config.autoflushUseHighPressureMotor = config["autoflush_use_high_pressure_motor"] | defaults.autoflushUseHighPressureMotor;

  _config.coolingFanControl = config["cooling_fan_control"] | defaults.coolingFanControl;
  _config.coolingFanRelayId = config["cooling_fan_relay_id"] | defaults.coolingFanRelayId;
  _config.coolingFanRelayInverted = config["cooling_fan_relay_inverted"] | defaults.coolingFanRelayInverted;
  _config.coolingFanOnTemperature = config["cooling_fan_on_temperature"] | defaults.coolingFanOnTemperature;
  _config.coolingFanOffTemperature = config["cooling_fan_off_temperature"] | defaults.coolingFanOffTemperature;

  _config.hasMembranePressureSensor = config["has_membrane_pressure_sensor"] | defaults.hasMembranePressureSensor;
  _config.membranePressureSensorMin = config["membrane_pressure_sensor_min"] | defaults.membranePressureSensorMin;
  _config.membranePressureSensorMax = config["membrane_pressure_sensor_max"] | defaults.membranePressureSensorMax;

  _config.hasFilterPressureSensor = config["has_filter_pressure_sensor"] | defaults.hasFilterPressureSensor;
  _config.filterPressureSensorMin = config["filter_pressure_sensor_min"] | defaults.filterPressureSensorMin;
  _config.filterPressureSensorMax = config["filter_pressure_sensor_max"] | defaults.filterPressureSensorMax;

  _config.hasProductTDSSensor = config["has_product_tds_sensor"] | defaults.hasProductTDSSensor;
  _config.productTDSSensorOffset = config["product_tds_sensor_offset"] | defaults.productTDSSensorOffset;

  _config.hasBrineTDSSensor = config["has_brine_tds_sensor"] | defaults.hasBrineTDSSensor;
  _config.brineTDSSensorOffset = config["brine_tds_sensor_offset"] | defaults.brineTDSSensorOffset;

  _config.hasProductFlowSensor = config["has_product_flow_sensor"] | defaults.hasProductFlowSensor;
  _config.productFlowmeterPPL = config["product_flowmeter_ppl"] | defaults.productFlowmeterPPL;

  _config.hasBrineFlowSensor = config["has_brine_flow_sensor"] | defaults.hasBrineFlowSensor;
  _config.brineFlowmeterPPL = config["brine_flowmeter_ppl"] | defaults.brineFlowmeterPPL;

  _config.motorTemperatureSensorType = config["motor_temperature_sensor_type"] | defaults.motorTemperatureSensorType;
  _config.motorTemperatureMqttPath = config["motor_temperature_mqtt_path"] | defaults.motorTemperatureMqttPath;
  _config.waterTemperatureSensorType = config["water_temperature_sensor_type"] | defaults.waterTemperatureSensorType;
  _config.waterTemperatureMqttPath = config["water_temperature_mqtt_path"] | defaults.waterTemperatureMqttPath;

  _config.tankLevelSensorType = config["tank_level_sensor_type"] | defaults.tankLevelSensorType;
  _config.tankLevelMqttPath = config["tank_level_mqtt_path"] | defaults.tankLevelMqttPath;
  _config.tankCapacity = config["tank_capacity"] | defaults.tankCapacity;

  _config.batteryLevelSensorType = config["battery_level_sensor_type"] | defaults.batteryLevelSensorType;
  _config.batteryLevelMqttPath = config["battery_level_mqtt_path"] | defaults.batteryLevelMqttPath;

  // smart backup of the old boolean style
  if (_config.motorTemperatureSensorType.equals("NONE") && config["has_motor_temperature_sensor"])
    _config.motorTemperatureSensorType = "DS18B20";
  if (_config.waterTemperatureSensorType.equals("NONE") && config["has_water_temperature_sensor"])
    _config.waterTemperatureSensorType = "DS18B20";
}

void Brineomatic::loadSafeguardsConfigJSON(JsonVariantConst config)
{
  _config.flushTimeout = config["flush_timeout"] | defaults.flushTimeout;
  _config.membranePressureTimeout = config["membrane_pressure_timeout"] | defaults.membranePressureTimeout;
  _config.productFlowrateTimeout = config["product_flowrate_timeout"] | defaults.productFlowrateTimeout;
  _config.productSalinityTimeout = config["product_salinity_timeout"] | defaults.productSalinityTimeout;
  _config.membranePressureStabilizationTime = config["membrane_pressure_stabilization_time"] | defaults.membranePressureStabilizationTime;
  _config.productFlowrateStabilizationTime = config["product_flowrate_stabilization_time"] | defaults.productFlowrateStabilizationTime;
  _config.productSalinityStabilizationTime = config["product_salinity_stabilization_time"] | defaults.productSalinityStabilizationTime;
  _config.productionRuntimeTimeout = config["production_runtime_timeout"] | defaults.productionRuntimeTimeout;

  _config.enableMembranePressureHighCheck = config["enable_membrane_pressure_high_check"] | defaults.enableMembranePressureHighCheck;
  _config.membranePressureHighThreshold = config["membrane_pressure_high_threshold"] | defaults.membranePressureHighThreshold;
  _config.membranePressureHighDelay = config["membrane_pressure_high_delay"] | defaults.membranePressureHighDelay;

  _config.enableMembranePressureLowCheck = config["enable_membrane_pressure_low_check"] | defaults.enableMembranePressureLowCheck;
  _config.membranePressureLowThreshold = config["membrane_pressure_low_threshold"] | defaults.membranePressureLowThreshold;
  _config.membranePressureLowDelay = config["membrane_pressure_low_delay"] | defaults.membranePressureLowDelay;

  _config.enableFilterPressureHighCheck = config["enable_filter_pressure_high_check"] | defaults.enableFilterPressureHighCheck;
  _config.filterPressureHighThreshold = config["filter_pressure_high_threshold"] | defaults.filterPressureHighThreshold;
  _config.filterPressureHighDelay = config["filter_pressure_high_delay"] | defaults.filterPressureHighDelay;

  _config.enableFilterPressureLowCheck = config["enable_filter_pressure_low_check"] | defaults.enableFilterPressureLowCheck;
  _config.filterPressureLowThreshold = config["filter_pressure_low_threshold"] | defaults.filterPressureLowThreshold;
  _config.filterPressureLowDelay = config["filter_pressure_low_delay"] | defaults.filterPressureLowDelay;

  _config.enableProductFlowrateHighCheck = config["enable_product_flowrate_high_check"] | defaults.enableProductFlowrateHighCheck;
  _config.productFlowrateHighThreshold = config["product_flowrate_high_threshold"] | defaults.productFlowrateHighThreshold;
  _config.productFlowrateHighDelay = config["product_flowrate_high_delay"] | defaults.productFlowrateHighDelay;

  _config.enableProductFlowrateLowCheck = config["enable_product_flowrate_low_check"] | defaults.enableProductFlowrateLowCheck;
  _config.productFlowrateLowThreshold = config["product_flowrate_low_threshold"] | defaults.productFlowrateLowThreshold;
  _config.productFlowrateLowDelay = config["product_flowrate_low_delay"] | defaults.productFlowrateLowDelay;

  _config.enableRunTotalFlowrateLowCheck = config["enable_run_total_flowrate_low_check"] | defaults.enableRunTotalFlowrateLowCheck;
  _config.runTotalFlowrateLowThreshold = config["run_total_flowrate_low_threshold"] | defaults.runTotalFlowrateLowThreshold;
  _config.runTotalFlowrateLowDelay = config["run_total_flowrate_low_delay"] | defaults.runTotalFlowrateLowDelay;

  _config.enablePickleTotalFlowrateLowCheck = config["enable_pickle_total_flowrate_low_check"] | defaults.enablePickleTotalFlowrateLowCheck;
  _config.pickleTotalFlowrateLowThreshold = config["pickle_total_flowrate_low_threshold"] | defaults.pickleTotalFlowrateLowThreshold;
  _config.pickleTotalFlowrateLowDelay = config["pickle_total_flowrate_low_delay"] | defaults.pickleTotalFlowrateLowDelay;

  _config.enableDiverterValveClosedCheck = config["enable_diverter_valve_closed_check"] | defaults.enableDiverterValveClosedCheck;
  _config.diverterValveClosedFlowrateHighThreshold = config["diverter_valve_closed_flowrate_high_threshold"] | defaults.diverterValveClosedFlowrateHighThreshold;
  _config.diverterValveClosedDelay = config["diverter_valve_closed_delay"] | defaults.diverterValveClosedDelay;

  _config.enableProductSalinityHighCheck = config["enable_product_salinity_high_check"] | defaults.enableProductSalinityHighCheck;
  _config.productSalinityHighThreshold = config["product_salinity_high_threshold"] | defaults.productSalinityHighThreshold;
  _config.productSalinityHighDelay = config["product_salinity_high_delay"] | defaults.productSalinityHighDelay;

  _config.enableMotorTemperatureCheck = config["enable_motor_temperature_check"] | defaults.enableMotorTemperatureCheck;
  _config.motorTemperatureHighThreshold = config["motor_temperature_high_threshold"] | defaults.motorTemperatureHighThreshold;
  _config.motorTemperatureHighDelay = config["motor_temperature_high_delay"] | defaults.motorTemperatureHighDelay;

  _config.enableFlushFlowrateLowCheck = config["enable_flush_flowrate_low_check"] | defaults.enableFlushFlowrateLowCheck;
  _config.flushFlowrateLowThreshold = config["flush_flowrate_low_threshold"] | defaults.flushFlowrateLowThreshold;
  _config.flushFlowrateLowDelay = config["flush_flowrate_low_delay"] | defaults.flushFlowrateLowDelay;

  _config.enableFlushFilterPressureLowCheck = config["enable_flush_filter_pressure_low_check"] | defaults.enableFlushFilterPressureLowCheck;
  _config.flushFilterPressureLowThreshold = config["flush_filter_pressure_low_threshold"] | defaults.flushFilterPressureLowThreshold;
  _config.flushFilterPressureLowDelay = config["flush_filter_pressure_low_delay"] | defaults.flushFilterPressureLowDelay;

  _config.enableFlushValveOffCheck = config["enable_flush_valve_off_check"] | defaults.enableFlushValveOffCheck;
  _config.flushValveOffThreshold = config["flush_valve_off_threshold"] | defaults.flushValveOffThreshold;
  _config.flushValveOffDelay = config["flush_valve_off_delay"] | defaults.flushValveOffDelay;

  _config.enableFlushTankLevelLowCheck = config["enable_flush_tank_level_low_check"] | defaults.enableFlushTankLevelLowCheck;
  _config.flushTankLevelLowThreshold = config["flush_tank_level_low_threshold"] | defaults.flushTankLevelLowThreshold;
  _config.flushTankLevelLowDelay = config["flush_tank_level_low_delay"] | defaults.flushTankLevelLowDelay;

  _config.enableTankLevelFullCheck = config["enable_tank_level_full_check"] | defaults.enableTankLevelFullCheck;
  _config.tankLevelFullThreshold = config["tank_level_full_threshold"] | defaults.tankLevelFullThreshold;
  _config.tankLevelFullDelay = config["tank_level_full_delay"] | defaults.tankLevelFullDelay;

  _config.enableBatteryLevelLowCheck = config["enable_battery_level_low_check"] | defaults.enableBatteryLevelLowCheck;
  _config.batteryLevelLowThreshold = config["battery_level_low_threshold"] | defaults.batteryLevelLowThreshold;
}

void Brineomatic::updateMQTT()
{
  JsonDocument output;
  this->generateUpdateJSON(output);
  output.remove("brineomatic");

  _app.mqtt.traverseJSON(output, "watermaker");
}

void Brineomatic::logResult(Status status, Result result)
{
  JsonDocument log;

  log["timestamp"] = (uint32_t)_app.ntp.getTime();
  log["mode"] = getStatus(status);
  log["result"] = resultToString(result);
  log["total_runtime"] = totalRuntime;

  if (status == Status::RUNNING) {
    log["elapsed"] = getRuntimeElapsed();
    log["volume"] = getVolume();
  } else if (status == Status::FLUSHING) {
    log["elapsed"] = getFlushElapsed();
    log["volume"] = getFlushVolume();
  } else if (status == Status::PICKLING) {
    log["elapsed"] = getPickleElapsed();
    log["volume"] = getTotalVolume();
  } else if (status == Status::DEPICKLING) {
    log["elapsed"] = getDepickleElapsed();
    log["volume"] = getTotalVolume();
  }

  File f = LittleFS.open("/run_log.json", "a");
  if (f) {
    serializeJson(log, f);
    f.println();
    f.close();
  }
}