/*
  Yarrboard

  Author: Zach Hoeken <hoeken@gmail.com>
  Website: https://github.com/hoeken/yarrboard
  License: GPLv3
*/

#include "config.h"

#include "BrineomaticController.h"
#include "UnitConversion.h"
#include "controllers/RelayController.h"
#include "controllers/ServoController.h"
#include "controllers/StepperController.h"
#include "validate.h"
#include <YarrboardApp.h>
#include <YarrboardDebug.h>

// Define the static member variable
BrineomaticController* BrineomaticController::_instance = nullptr;

BrineomaticController::BrineomaticController(YarrboardApp& app, RelayController& relays, ServoController& servos, StepperController& steppers) : BaseController(app, "brineomatic"),
                                                                                                                                                 wm(app, relays, servos, steppers),
                                                                                                                                                 _relays(relays),
                                                                                                                                                 _servos(servos),
                                                                                                                                                 _steppers(steppers)
{
}

bool BrineomaticController::setup()
{
  _instance = this; // Capture the instance for callbacks

  PsychicHttpServer* server = _app.http.getServer();
  if (server) {
    server->serveStatic("/run_log.json", LittleFS, "/run_log.json");

    server->on("/api/sensor_history", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
      return handleSensorHistoryRequest(request, response);
    });
  }

  _app.protocol.registerCommand(GUEST, "start_watermaker", this, &BrineomaticController::handleStartWatermaker);
  _app.protocol.registerCommand(GUEST, "flush_watermaker", this, &BrineomaticController::handleFlushWatermaker);
  _app.protocol.registerCommand(GUEST, "pickle_watermaker", this, &BrineomaticController::handlePickleWatermaker);
  _app.protocol.registerCommand(GUEST, "depickle_watermaker", this, &BrineomaticController::handleDepickleWatermaker);
  _app.protocol.registerCommand(GUEST, "stop_watermaker", this, &BrineomaticController::handleStopWatermaker);
  _app.protocol.registerCommand(GUEST, "idle_watermaker", this, &BrineomaticController::handleIdleWatermaker);
  _app.protocol.registerCommand(GUEST, "manual_watermaker", this, &BrineomaticController::handleManualWatermaker);
  _app.protocol.registerCommand(GUEST, "set_watermaker", this, &BrineomaticController::handleSetWatermaker);
  _app.protocol.registerCommand(GUEST, "brineomatic_save_ui_config", this, &BrineomaticController::handleSaveUIConfig);
  _app.protocol.registerCommand(GUEST, "brineomatic_save_general_config", this, &BrineomaticController::handleSaveGeneralConfig);
  _app.protocol.registerCommand(GUEST, "brineomatic_save_hardware_config", this, &BrineomaticController::handleSaveHardwareConfig);
  _app.protocol.registerCommand(GUEST, "brineomatic_save_safeguards_config", this, &BrineomaticController::handleSaveSafeguardsConfig);

  _app.protocol.registerCommand(ADMIN, "brineomatic_delete_logs", this, &BrineomaticController::handleDeleteLogs);

  wm.init();

  // Create a FreeRTOS task for the state machine
  xTaskCreatePinnedToCore(
    BrineomaticController::stateMachineTask, // Task function
    "brineomatic_sm",                        // Name of the task
    4096,                                    // Stack size
    this,                                    // Task input parameters
    2,                                       // Priority of the task
    NULL,                                    // Task handle
    1                                        // Core where the task should run
  );

  return true;
}

void BrineomaticController::loop()
{
  wm.loop();
}

// Dump one sensor's history as a raw binary blob (application/octet-stream).
// JSON would balloon 128KB of points into 500KB+ of text and exhaust SRAM, so
// the format is a 16-byte preamble followed by tightly packed 8-byte points
// (uint32 uptime seconds + float32 value, little-endian), streamed in small
// stack-buffered chunks.  Timestamps are device uptime; the preamble carries
// the current uptime so the browser can anchor them to wall-clock time.
esp_err_t BrineomaticController::handleSensorHistoryRequest(PsychicRequest* request, PsychicResponse* response)
{
  String sensor = request->getParam("sensor", "");
  int idx = wm.history.indexOf(sensor.c_str());
  if (idx < 0)
    return response->send(400, "text/plain", "Unknown or missing sensor parameter.");

  // snapshot the count up front; points added mid-transfer wait for next fetch
  size_t count = wm.history.count(idx);

  struct __attribute__((packed)) {
      uint32_t magic;
      uint16_t version;
      uint16_t pointSize;
      uint32_t uptime; // device uptime in seconds, for timestamp anchoring
      uint32_t count;
  } preamble = {0x484D4F42 /* "BOMH" */, 1, sizeof(SensorHistoryPoint), millis() / 1000, (uint32_t)count};

  response->setCode(200);
  response->setContentType("application/octet-stream");
  response->sendHeaders();

  esp_err_t err = response->sendChunk((uint8_t*)&preamble, sizeof(preamble));
  if (err != ESP_OK)
    return err;

  // iterate the circular buffer logically (oldest to newest) through a small
  // stack buffer; 64 points = 512 byte chunks on the wire.
  SensorHistoryPoint points[64];
  for (size_t start = 0; start < count; start += 64) {
    size_t n = wm.history.copy(idx, start, points, 64);
    if (n == 0)
      break;

    err = response->sendChunk((uint8_t*)points, n * sizeof(SensorHistoryPoint));
    if (err != ESP_OK)
      return err; // sendChunk already aborted the response
  }

  return response->finishChunking();
}

void BrineomaticController::stateMachineTask(void* pvParameters)
{
  // Cast the void pointer back to our class type
  BrineomaticController* instance = static_cast<BrineomaticController*>(pvParameters);

  // Call the actual member function
  instance->stateMachine();
}

void BrineomaticController::stateMachine()
{
  while (true) {
    wm.runStateMachine();

    // Add a delay to prevent task starvation
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool BrineomaticController::sanitizeConfigHook(JsonVariant config, char* error, size_t len)
{
  // validate prunes invalid entries, so it's safe to load even on error.
  // we don't want a single bad config option to nuke the whole config loading.
  return validateConfigJSON(config, error, len);
}

void BrineomaticController::loadConfigHook(JsonVariantConst config)
{
  loadConfigJSON(config);

  // todo: move to separate function
  if (_app.mqtt.isEnabled()) {
    MQTTController* mqtt = (MQTTController*)_app.getController("mqtt");
    String type;

    // todo: unsubscribe from these topics before we subscribe (in case of multiple config loads)

    type = config["motor_temperature_sensor_type"].as<String>();
    if (type.equals("MQTT")) {
      mqtt->onTopic(config["motor_temperature_mqtt_path"], 0, &BrineomaticController::handleMotorTemperatureCallbackStatic);
    }

    type = config["water_temperature_sensor_type"].as<String>();
    if (type.equals("MQTT")) {
      mqtt->onTopic(config["water_temperature_mqtt_path"], 0, &BrineomaticController::handleWaterTemperatureCallbackStatic);
    }

    type = config["tank_level_sensor_type"].as<String>();
    if (type.equals("MQTT")) {
      mqtt->onTopic(config["tank_level_mqtt_path"], 0, &BrineomaticController::handleTankLevelCallbackStatic);
    }

    type = config["battery_level_sensor_type"].as<String>();
    if (type.equals("MQTT")) {
      mqtt->onTopic(config["battery_level_mqtt_path"], 0, &BrineomaticController::handleBatteryLevelCallbackStatic);
    }
  }
}

void BrineomaticController::generateConfigHook(JsonVariant output, UserRole role, ConfigPurpose purpose)
{
  generateConfigJSON(output, role, purpose);
};

void BrineomaticController::generateCapabilitiesHook(JsonVariant config)
{
#ifdef YB_DS18B20_MOTOR_PIN
  config["motor_temperature"] = true;
#endif

#ifdef YB_DS18B20_WATER_PIN
  config["water_temperature"] = true;
#endif

#ifdef YB_PRODUCT_FLOWMETER_PIN
  config["product_flowmeter"] = true;
#endif

#ifdef YB_BRINE_FLOWMETER_PIN
  config["brine_flowmeter"] = true;
#endif

#ifdef YB_BRINE_TDS_CHANNEL
  config["brine_tds"] = true;
#endif

#ifdef YB_PRODUCT_TDS_CHANNEL
  config["product_tds"] = true;
#endif

#ifdef YB_LP_SENSOR_CHANNEL
  config["lp_sensor"] = true;
#endif

#ifdef YB_HP_SENSOR_CHANNEL
  config["hp_sensor"] = true;
#endif

#ifdef YB_HAS_MODBUS
  config["modbus"] = true;
#endif
}

void BrineomaticController::generateUpdateHook(JsonVariant output)
{
  output["brineomatic"] = true;
  output["status"] = wm.getStatus();
  output["run_result"] = wm.resultToString(wm.getRunResult());
  output["flush_result"] = wm.resultToString(wm.getFlushResult());
  output["pickle_result"] = wm.resultToString(wm.getPickleResult());
  output["depickle_result"] = wm.resultToString(wm.getDepickleResult());
  output["motor_temperature"] = wm.getMotorTemperature();
  output["water_temperature"] = wm.getWaterTemperature();
  output["product_flowrate"] = wm.getProductFlowrate();
  output["brine_flowrate"] = wm.getBrineFlowrate();
  output["total_flowrate"] = wm.getTotalFlowrate();
  output["volume"] = wm.getVolume();
  output["flush_volume"] = wm.getFlushVolume();
  output["product_salinity"] = wm.getProductSalinity();
  output["brine_salinity"] = wm.getBrineSalinity();
  output["filter_pressure"] = wm.getFilterPressure();
  output["membrane_pressure"] = wm.getMembranePressure();
  output["tank_level"] = wm.getTankLevel();
  output["battery_level"] = wm.getBatteryLevel();

  if (wm.hasBoostPump())
    output["boost_pump_on"] = wm.isBoostPumpOn();
  if (wm.hasHighPressurePump())
    output["high_pressure_pump_on"] = wm.isHighPressurePumpOn();
  if (wm.hasDiverterValve())
    output["diverter_valve_open"] = wm.isDiverterValveOpen();
  if (wm.hasFlushValve())
    output["flush_valve_open"] = wm.isFlushValveOpen();
  if (wm.hasCoolingFan())
    output["cooling_fan_on"] = wm.isCoolingFanOn();

  output["next_flush_countdown"] = wm.getNextFlushCountdown();

  if (!strcmp(wm.getStatus(), "RUNNING")) {
    output["runtime_elapsed"] = wm.getRuntimeElapsed();
    output["finish_countdown"] = wm.getFinishCountdown();
  }

  if (!strcmp(wm.getStatus(), "FLUSHING")) {
    output["flush_elapsed"] = wm.getFlushElapsed();
    output["flush_countdown"] = wm.getFlushCountdown();
  }

  if (!strcmp(wm.getStatus(), "PICKLING")) {
    output["pickle_elapsed"] = wm.getPickleElapsed();
    output["pickle_countdown"] = wm.getPickleCountdown();
  }

  if (!strcmp(wm.getStatus(), "DEPICKLING")) {
    output["depickle_elapsed"] = wm.getDepickleElapsed();
    output["depickle_countdown"] = wm.getDepickleCountdown();
  }

  if (!strcmp(wm.getStatus(), "PICKLED")) {
    if (wm.pickledOnTimestamp > 1700000000)
      output["pickled_on"] = wm.pickledOnTimestamp;
  }
};

void BrineomaticController::mqttUpdateHook(MQTTController* mqtt)
{
  JsonDocument output;
  generateUpdateHook(output);

  // Convert temperature fields
  output["motor_temperature"] = convertTemperature(output["motor_temperature"], "celsius", wm.getTemperatureUnits());
  output["water_temperature"] = convertTemperature(output["water_temperature"], "celsius", wm.getTemperatureUnits());

  // Convert pressure fields
  output["filter_pressure"] = convertPressure(output["filter_pressure"], "bar", wm.getPressureUnits());
  output["membrane_pressure"] = convertPressure(output["membrane_pressure"], "bar", wm.getPressureUnits());

  // Convert volume fields
  output["volume"] = convertVolume(output["volume"], "liters", wm.getVolumeUnits());
  output["flush_volume"] = convertVolume(output["flush_volume"], "liters", wm.getVolumeUnits());

  // Convert flowrate fields
  output["product_flowrate"] = convertFlowrate(output["product_flowrate"], "lph", wm.getFlowrateUnits());
  output["brine_flowrate"] = convertFlowrate(output["brine_flowrate"], "lph", wm.getFlowrateUnits());
  output["total_flowrate"] = convertFlowrate(output["total_flowrate"], "lph", wm.getFlowrateUnits());

  mqtt->traverseJSON(output, "");
}

void BrineomaticController::haUpdateHook(MQTTController* mqtt)
{
  mqtt->publish(ha_topic_avail, "online", false);

  if (strcmp(wm.getStatus(), "IDLE") == 0 || strcmp(wm.getStatus(), "STOPPING") == 0 || strcmp(wm.getStatus(), "STARTUP") == 0 || strcmp(wm.getStatus(), "MANUAL") == 0)
    mqtt->publish(ha_topic_state_state, "OFF", false);
  else
    mqtt->publish(ha_topic_state_state, "ON", false);
}

void BrineomaticController::haGenerateDiscoveryHook(JsonVariant components, const char* uuid, MQTTController* mqtt)
{
  sprintf(ha_uuid, "yarrboard/%s", _app.network.getLocalHostname());
  sprintf(ha_topic_avail, "%s/ha/availability", ha_uuid);
  sprintf(ha_topic_cmd_state, "%s/ha/set", ha_uuid);
  sprintf(ha_topic_state_state, "%s/ha/state", ha_uuid);

  if (!_haCallbacksRegistered) {
    mqtt->onTopic(ha_topic_cmd_state, 0, &BrineomaticController::handleHACommandCallbackStatic);
    _haCallbacksRegistered = true;
  }

  // configuration object for the individual channel
  JsonObject obj = components[ha_uuid].to<JsonObject>();
  obj["platform"] = "switch";
  obj["name"] = _app.config.getBoardName();
  obj["unique_id"] = ha_uuid;
  obj["state_topic"] = ha_topic_state_state;
  obj["command_topic"] = ha_topic_cmd_state;
  obj["payload_on"] = "ON";
  obj["payload_off"] = "OFF";
  obj["icon"] = "mdi:water-sync";

  // availability is an array of objects
  JsonArray availability = obj["availability"].to<JsonArray>();
  JsonObject avail = availability.add<JsonObject>();
  avail["topic"] = ha_topic_avail;

  if (wm.hasMotorTemperature())
    haGenerateMotorTemperatureDiscovery(components);
  if (wm.hasWaterTemperature())
    haGenerateWaterTemperatureDiscovery(components);

  haGenerateStatusDiscovery(components);
  haGenerateRunResultDiscovery(components);
  haGenerateFlushResultDiscovery(components);
  haGeneratePickleResultDiscovery(components);
  haGenerateDepickleResultDiscovery(components);
  haGenerateNextFlushCountdownDiscovery(components);
  haGenerateRuntimeElapsedDiscovery(components);
  haGenerateFinishCountdownDiscovery(components);

  if (wm.hasFilterPressure())
    haGenerateFilterPressureDiscovery(components);
  if (wm.hasMembranePressure())
    haGenerateMembranePressureDiscovery(components);
  if (wm.hasProductTDS())
    haGenerateProductSalinityDiscovery(components);
  if (wm.hasBrineTDS())
    haGenerateBrineSalinityDiscovery(components);
  if (wm.hasProductFlow())
    haGenerateProductFlowrateDiscovery(components);
  if (wm.hasBrineFlow())
    haGenerateBrineFlowrateDiscovery(components);
  if (wm.hasProductFlow() || wm.hasBrineFlow())
    haGenerateTotalFlowrateDiscovery(components);

  haGenerateTankLevelDiscovery(components);
  haGenerateBatteryLevelDiscovery(components);
  haGenerateVolumeDiscovery(components);
  haGenerateFlushVolumeDiscovery(components);

  if (wm.hasBoostPump())
    haGenerateBoostPumpDiscovery(components);
  if (wm.hasHighPressurePump())
    haGenerateHighPressurePumpDiscovery(components);
  if (wm.hasDiverterValve())
    haGenerateDiverterValveDiscovery(components);
  if (wm.hasFlushValve())
    haGenerateFlushValveDiscovery(components);
  if (wm.hasCoolingFan())
    haGenerateCoolingFanDiscovery(components);
}

void BrineomaticController::haGenerateMotorTemperatureDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_motor_temperature", ha_uuid);
  sprintf(ha_topic_motor_temperature, "%s/motor_temperature", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Motor Temperature";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_motor_temperature;
  obj["device_class"] = "temperature";
  if (!strcmp(wm.getTemperatureUnits(), "celsius"))
    obj["unit_of_measurement"] = "°C";
  else
    obj["unit_of_measurement"] = "°F";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateWaterTemperatureDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_water_temperature", ha_uuid);
  sprintf(ha_topic_water_temperature, "%s/water_temperature", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Water Temperature";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_water_temperature;
  obj["device_class"] = "temperature";
  if (!strcmp(wm.getTemperatureUnits(), "celsius"))
    obj["unit_of_measurement"] = "°C";
  else
    obj["unit_of_measurement"] = "°F";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateStatusDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_status", ha_uuid);
  sprintf(ha_topic_status, "%s/status", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Status";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_status;
  obj["icon"] = "mdi:water-sync";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateRunResultDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_run_result", ha_uuid);
  sprintf(ha_topic_run_result, "%s/run_result", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Run Result";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_run_result;
  obj["icon"] = "mdi:water-sync";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateFlushResultDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_flush_result", ha_uuid);
  sprintf(ha_topic_flush_result, "%s/flush_result", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Flush Result";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_flush_result;
  obj["icon"] = "mdi:water-sync";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGeneratePickleResultDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_pickle_result", ha_uuid);
  sprintf(ha_topic_pickle_result, "%s/pickle_result", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Pickle Result";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_pickle_result;
  obj["icon"] = "mdi:water-sync";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateDepickleResultDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_depickle_result", ha_uuid);
  sprintf(ha_topic_depickle_result, "%s/depickle_result", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Depickle Result";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_depickle_result;
  obj["icon"] = "mdi:water-sync";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateNextFlushCountdownDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_next_flush_countdown", ha_uuid);
  sprintf(ha_topic_next_flush_countdown, "%s/next_flush_countdown", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Next Flush Countdown";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_next_flush_countdown;
  obj["device_class"] = "duration";
  obj["unit_of_measurement"] = "h";
  obj["value_template"] = "{{ (value | int / 3600000) | round(2) }}";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateRuntimeElapsedDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_runtime_elapsed", ha_uuid);
  sprintf(ha_topic_runtime_elapsed, "%s/runtime_elapsed", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Runtime Elapsed";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_runtime_elapsed;
  obj["device_class"] = "duration";
  obj["unit_of_measurement"] = "min";
  obj["value_template"] = "{{ (value | int / 60000) | round(2) }}";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateFinishCountdownDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_finish_countdown", ha_uuid);
  sprintf(ha_topic_finish_countdown, "%s/finish_countdown", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Finish Countdown";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_finish_countdown;
  obj["device_class"] = "duration";
  obj["unit_of_measurement"] = "min";
  obj["value_template"] = "{{ (value | int / 60000) | round(2) }}";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateFilterPressureDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_filter_pressure", ha_uuid);
  sprintf(ha_topic_filter_pressure, "%s/filter_pressure", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Filter Pressure";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_filter_pressure;
  obj["device_class"] = "pressure";
  if (strcmp(wm.getPressureUnits(), "kilopascal") == 0)
    obj["unit_of_measurement"] = "kPa";
  else if (strcmp(wm.getPressureUnits(), "psi") == 0)
    obj["unit_of_measurement"] = "psi";
  else
    obj["unit_of_measurement"] = "bar";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateMembranePressureDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_membrane_pressure", ha_uuid);
  sprintf(ha_topic_membrane_pressure, "%s/membrane_pressure", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Membrane Pressure";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_membrane_pressure;
  obj["device_class"] = "pressure";
  if (strcmp(wm.getPressureUnits(), "kilopascal") == 0)
    obj["unit_of_measurement"] = "kPa";
  else if (strcmp(wm.getPressureUnits(), "psi") == 0)
    obj["unit_of_measurement"] = "psi";
  else
    obj["unit_of_measurement"] = "bar";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateProductSalinityDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_product_salinity", ha_uuid);
  sprintf(ha_topic_product_salinity, "%s/product_salinity", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Product Salinity";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_product_salinity;
  obj["unit_of_measurement"] = "ppm";
  obj["suggested_display_precision"] = 0;
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateBrineSalinityDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_brine_salinity", ha_uuid);
  sprintf(ha_topic_brine_salinity, "%s/brine_salinity", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Brine Salinity";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_brine_salinity;
  obj["unit_of_measurement"] = "ppm";
  obj["suggested_display_precision"] = 0;
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateProductFlowrateDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_product_flowrate", ha_uuid);
  sprintf(ha_topic_product_flowrate, "%s/product_flowrate", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Product Flowrate";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_product_flowrate;
  obj["device_class"] = "volume_flow_rate";
  if (strcmp(wm.getFlowrateUnits(), "lph") == 0)
    obj["unit_of_measurement"] = "L/h";
  else
    obj["unit_of_measurement"] = "gal/h";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateBrineFlowrateDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_brine_flowrate", ha_uuid);
  sprintf(ha_topic_brine_flowrate, "%s/brine_flowrate", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Brine Flowrate";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_brine_flowrate;
  obj["device_class"] = "volume_flow_rate";
  if (strcmp(wm.getFlowrateUnits(), "lph") == 0)
    obj["unit_of_measurement"] = "L/h";
  else
    obj["unit_of_measurement"] = "gal/h";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateTotalFlowrateDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_total_flowrate", ha_uuid);
  sprintf(ha_topic_total_flowrate, "%s/total_flowrate", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Total Flowrate";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_total_flowrate;
  obj["device_class"] = "volume_flow_rate";
  if (strcmp(wm.getFlowrateUnits(), "lph") == 0)
    obj["unit_of_measurement"] = "L/h";
  else
    obj["unit_of_measurement"] = "gal/h";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateTankLevelDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_tank_level", ha_uuid);
  sprintf(ha_topic_tank_level, "%s/tank_level", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Tank Level";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_tank_level;
  obj["unit_of_measurement"] = "%";
  obj["value_template"] = "{{ (value | float * 100) | round(1) }}";
  obj["state_class"] = "measurement";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateBatteryLevelDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_battery_level", ha_uuid);
  sprintf(ha_topic_battery_level, "%s/battery_level", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Battery Level";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_battery_level;
  obj["unit_of_measurement"] = "%";
  obj["value_template"] = "{{ (value | float * 100) | round(1) }}";
  obj["state_class"] = "measurement";
  obj["device_class"] = "battery";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateVolumeDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_volume", ha_uuid);
  sprintf(ha_topic_volume, "%s/volume", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Product Volume";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_volume;
  obj["device_class"] = "volume";
  if (strcmp(wm.getVolumeUnits(), "liters") == 0)
    obj["unit_of_measurement"] = "L";
  else
    obj["unit_of_measurement"] = "gal";
  obj["state_class"] = "total_increasing";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateFlushVolumeDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_flush_volume", ha_uuid);
  sprintf(ha_topic_flush_volume, "%s/flush_volume", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "sensor";
  obj["name"] = "Flush Volume";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_flush_volume;
  obj["device_class"] = "volume";
  if (strcmp(wm.getVolumeUnits(), "liters") == 0)
    obj["unit_of_measurement"] = "L";
  else
    obj["unit_of_measurement"] = "gal";
  obj["state_class"] = "total_increasing";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateBoostPumpDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_boost_pump_on", ha_uuid);
  sprintf(ha_topic_boost_pump_on, "%s/boost_pump_on", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "binary_sensor";
  obj["name"] = "Boost Pump";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_boost_pump_on;
  obj["payload_on"] = "true";
  obj["payload_off"] = "false";
  obj["icon"] = "mdi:water-pump";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateHighPressurePumpDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_high_pressure_pump_on", ha_uuid);
  sprintf(ha_topic_high_pressure_pump_on, "%s/high_pressure_pump_on", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "binary_sensor";
  obj["name"] = "High Pressure Pump";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_high_pressure_pump_on;
  obj["payload_on"] = "true";
  obj["payload_off"] = "false";
  obj["icon"] = "mdi:water-pump";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateDiverterValveDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_diverter_valve_open", ha_uuid);
  sprintf(ha_topic_diverter_valve_open, "%s/diverter_valve_open", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "binary_sensor";
  obj["name"] = "Diverter Valve";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_diverter_valve_open;
  obj["payload_on"] = "false"; // invert... false = overboard
  obj["payload_off"] = "true"; // invert... true = tanks
  obj["icon"] = "mdi:valve";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateFlushValveDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_flush_valve_open", ha_uuid);
  sprintf(ha_topic_flush_valve_open, "%s/flush_valve_open", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "binary_sensor";
  obj["name"] = "Flush Valve";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_flush_valve_open;
  obj["payload_on"] = "true";
  obj["payload_off"] = "false";
  obj["icon"] = "mdi:valve";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::haGenerateCoolingFanDiscovery(JsonVariant doc)
{
  char unique_id[128];
  sprintf(unique_id, "%s_cooling_fan_on", ha_uuid);
  sprintf(ha_topic_cooling_fan_on, "%s/cooling_fan_on", ha_uuid);

  JsonObject obj = doc[unique_id].to<JsonObject>();
  obj["platform"] = "binary_sensor";
  obj["name"] = "Cooling Fan";
  obj["unique_id"] = unique_id;
  obj["state_topic"] = ha_topic_cooling_fan_on;
  obj["payload_on"] = "true";
  obj["payload_off"] = "false";
  obj["icon"] = "mdi:fan";
  obj["availability_topic"] = ha_topic_avail;
}

void BrineomaticController::handleHACommandCallbackStatic(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  if (_instance) {
    _instance->handleHACommandCallback(topic, payload, retain, qos, dup);
  }
}

void BrineomaticController::handleHACommandCallback(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  // start and stop internally handle if we're allowed to do it.
  if (!strcmp(payload, "ON"))
    wm.start();
  else
    wm.stop();
}

void BrineomaticController::handleWaterTemperatureCallbackStatic(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  if (_instance) {
    _instance->handleWaterTemperatureCallback(topic, payload, retain, qos, dup);
  }
}

void BrineomaticController::handleWaterTemperatureCallback(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  float temp = atof(payload);

  // convert from obvious kelvins
  if (temp > 150)
    temp += -273.15;
  // convert from obvious fahrenheit
  else if (temp > 50)
    temp = (temp * 9 / 5) + 32;

  wm.setWaterTemperature(temp);
}

void BrineomaticController::handleMotorTemperatureCallbackStatic(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  if (_instance) {
    _instance->handleMotorTemperatureCallback(topic, payload, retain, qos, dup);
  }
}

void BrineomaticController::handleMotorTemperatureCallback(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  float temp = atof(payload);

  // convert from obvious kelvins
  if (temp > 200)
    temp -= -273.15;
  // convert from obvious fahrenheit
  else if (temp > 100)
    temp = (temp * 9 / 5) + 32;

  wm.setMotorTemperature(temp);
}

void BrineomaticController::handleTankLevelCallbackStatic(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  if (_instance) {
    _instance->handleTankLevelCallback(topic, payload, retain, qos, dup);
  }
}

void BrineomaticController::handleTankLevelCallback(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  float level = atof(payload);

  if (level >= 0.0 && level <= 1.0)
    wm.setTankLevel(level);
  else if (level > 1.0 && level <= 100.0)
    wm.setTankLevel(level / 100.0);
}

void BrineomaticController::handleBatteryLevelCallbackStatic(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  if (_instance) {
    _instance->handleBatteryLevelCallback(topic, payload, retain, qos, dup);
  }
}

void BrineomaticController::handleBatteryLevelCallback(const char* topic, const char* payload, int retain, int qos, bool dup)
{
  float level = atof(payload);

  if (level >= 0.0 && level <= 1.0)
    wm.setBatteryLevel(level);
  else if (level > 1.0 && level <= 100.0)
    wm.setBatteryLevel(level / 100.0);
}

void BrineomaticController::generateStatsHook(JsonVariant output)
{
  output["brineomatic"] = true;
  output["total_cycles"] = wm.getTotalCycles();
  output["total_volume"] = wm.getTotalVolume();
  output["total_runtime"] = wm.getTotalRuntime();

  wm.stats.toJson(output["cycle_stats"].to<JsonObject>());
};

void BrineomaticController::handleStartWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (strcmp(wm.getStatus(), "IDLE"))
    return _app.protocol.generateErrorJSON(output, "Watermaker is not in IDLE mode.");

  uint64_t duration = input["duration"];
  float volume = input["volume"];

  if (duration > 0)
    wm.startDuration(duration);
  else if (volume > 0)
    wm.startVolume(volume);
  else
    wm.start();
}

void BrineomaticController::handleFlushWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  uint64_t duration = input["duration"];
  float volume = input["volume"];

  if (!strcmp(wm.getStatus(), "IDLE") || !strcmp(wm.getStatus(), "PICKLED")) {
    if (duration > 0)
      wm.flushDuration(duration);
    else if (volume > 0)
      wm.flushVolume(volume);
    else
      wm.flush();
  } else
    return _app.protocol.generateErrorJSON(output, "Watermaker is not in IDLE or PICKLED modes.");
}

void BrineomaticController::handlePickleWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (!input["duration"].is<JsonVariantConst>())
    return _app.protocol.generateErrorJSON(output, "'duration' is a required parameter");

  uint64_t duration = input["duration"];

  if (!duration)
    return _app.protocol.generateErrorJSON(output, "'duration' must be non-zero");

  if (!strcmp(wm.getStatus(), "IDLE"))
    wm.pickle(duration);
  else
    return _app.protocol.generateErrorJSON(output, "Watermaker is not in IDLE mode.");
}

void BrineomaticController::handleDepickleWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (!input["duration"].is<JsonVariantConst>())
    return _app.protocol.generateErrorJSON(output, "'duration' is a required parameter");

  uint64_t duration = input["duration"];

  if (!duration)
    return _app.protocol.generateErrorJSON(output, "'duration' must be non-zero");

  if (!strcmp(wm.getStatus(), "PICKLED"))
    wm.depickle(duration);
  else
    return _app.protocol.generateErrorJSON(output, "Watermaker is not in PICKLED mode.");
}

void BrineomaticController::handleStopWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (!strcmp(wm.getStatus(), "RUNNING") || !strcmp(wm.getStatus(), "FLUSHING") || !strcmp(wm.getStatus(), "PICKLING") || !strcmp(wm.getStatus(), "DEPICKLING"))
    wm.stop();
  else
    return _app.protocol.generateErrorJSON(output, "Watermaker must be in RUNNING, FLUSHING, or PICKLING mode to stop.");
}

void BrineomaticController::handleIdleWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (!strcmp(wm.getStatus(), "MANUAL"))
    wm.idle();
  else
    return _app.protocol.generateErrorJSON(output, "Watermaker must be in MANUAL mode to IDLE.");
}

void BrineomaticController::handleManualWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (!strcmp(wm.getStatus(), "IDLE"))
    wm.manual();
  else
    return _app.protocol.generateErrorJSON(output, "Watermaker must be in IDLE mode to switch to MANUAL.");
}

void BrineomaticController::handleSetWatermaker(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (input["motor_temperature"].is<float>()) {
    float temp = input["motor_temperature"];

    if (temp < -50 || temp > 200.0)
      return _app.protocol.generateErrorJSON(output, "Motor temperature must be between -50C and 200C");

    wm.setMotorTemperature(temp);
    return;
  }

  if (input["water_temperature"].is<float>()) {
    float temp = input["water_temperature"];

    if (temp < 0.0 || temp > 50.0)
      return _app.protocol.generateErrorJSON(output, "Water temperature must be between 0C and 50C");

    wm.setWaterTemperature(temp);
    return;
  }

  if (input["tank_level"].is<float>()) {
    float level = input["tank_level"];

    if (level < 0.0 || level > 100.0)
      return _app.protocol.generateErrorJSON(output, "Tank level must be between 0.0 and 100.0");
    if (level > 1.0)
      level /= 100.0;

    wm.setTankLevel(level);
    return;
  }

  if (input["battery_level"].is<float>()) {
    float level = input["battery_level"];

    if (level < 0.0 || level > 100.0)
      return _app.protocol.generateErrorJSON(output, "Battery level must be between 0.0 and 100.0");
    if (level > 1.0)
      level /= 100.0;

    wm.setBatteryLevel(level);
    return;
  }

  if (strcmp(wm.getStatus(), "MANUAL"))
    return _app.protocol.generateErrorJSON(output, "Watermaker must be in MANUAL mode.");

  String state;

  if (input["boost_pump"]) {
    if (wm.hasBoostPump()) {
      state = input["boost_pump"] | "OFF";

      if (state.equals("TOGGLE")) {
        if (!wm.isBoostPumpOn())
          state = "ON";
      }

      if (state.equals("ON"))
        wm.enableBoostPump();
      else
        wm.disableBoostPump();
    } else
      return _app.protocol.generateErrorJSON(output, "Watermaker does not have a boost pump.");
  }

  if (input["high_pressure_pump"]) {
    if (wm.hasHighPressurePump()) {
      state = input["high_pressure_pump"] | "OFF";

      if (state.equals("TOGGLE")) {
        if (!wm.isHighPressurePumpOn())
          state = "ON";
      }

      if (state.equals("ON"))
        wm.enableHighPressurePump();
      else
        wm.disableHighPressurePump();
    } else
      return _app.protocol.generateErrorJSON(output, "Watermaker does not have a high pressure pump.");
  }

  if (input["diverter_valve"]) {
    if (wm.hasDiverterValve()) {
      state = input["diverter_valve"] | "CLOSE";

      if (state.equals("TOGGLE")) {
        if (!wm.isDiverterValveOpen())
          state = "OPEN";
      }

      if (state.equals("OPEN"))
        wm.openDiverterValve();
      else
        wm.closeDiverterValve();
    } else
      return _app.protocol.generateErrorJSON(output, "Watermaker does not have a diverter valve.");
  }

  if (input["flush_valve"]) {
    if (wm.hasFlushValve()) {
      state = input["flush_valve"] | "CLOSE";

      if (state.equals("TOGGLE")) {
        if (!wm.isFlushValveOpen())
          state = "OPEN";
      }

      if (state.equals("OPEN"))
        wm.openFlushValve();
      else
        wm.closeFlushValve();
    } else
      return _app.protocol.generateErrorJSON(output, "Watermaker does not have a flush valve.");
  }

  if (input["cooling_fan"]) {
    if (wm.hasCoolingFan()) {
      state = input["cooling_fan"] | "ON";

      if (state.equals("TOGGLE")) {
        if (!wm.isCoolingFanOn())
          state = "ON";
      }

      if (state.equals("ON"))
        wm.enableCoolingFan();
      else
        wm.disableCoolingFan();
    } else
      return _app.protocol.generateErrorJSON(output, "Watermaker does not have a cooling fan.");
  }
}

void BrineomaticController::handleSaveUIConfig(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  // we need a mutable format for the validation
  JsonDocument doc;
  doc.set(input);

  char error[128];
  if (!validateUIConfigJSON(doc, error, sizeof(error)))
    return _app.protocol.generateErrorJSON(output, error);

  loadUIConfigJSON(doc);

  if (!_cfg.saveConfig(error, sizeof(error)))
    return _app.protocol.generateErrorJSON(output, error);
}

void BrineomaticController::handleSaveGeneralConfig(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  // we need a mutable format for the validation
  JsonDocument doc;
  doc.set(input);

  char error[128];
  if (!validateGeneralConfigJSON(doc, error, sizeof(error)))
    return _app.protocol.generateErrorJSON(output, error);

  loadGeneralConfigJSON(doc);

  if (!_cfg.saveConfig(error, sizeof(error)))
    return _app.protocol.generateErrorJSON(output, error);
}

void BrineomaticController::handleSaveHardwareConfig(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  // we need a mutable format for the validation
  JsonDocument doc;
  doc.set(input);

  if (strcmp(wm.getStatus(), "IDLE") && strcmp(wm.getStatus(), "MANUAL"))
    return _app.protocol.generateErrorJSON(output, "Must be in IDLE or MANUAL mode to update hardware config.");

  char error[128];
  if (!validateHardwareConfigJSON(doc, error, sizeof(error)))
    return _app.protocol.generateErrorJSON(output, error);

  loadHardwareConfigJSON(doc);

  if (!_cfg.saveConfig(error, sizeof(error)))
    return _app.protocol.generateErrorJSON(output, error);

  // easiest to just restart - lots of init.
  ESP.restart();
}

void BrineomaticController::handleSaveSafeguardsConfig(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  // we need a mutable format for the validation
  JsonDocument doc;
  doc.set(input);

  char error[128];
  if (!validateSafeguardsConfigJSON(doc, error, sizeof(error))) {
    return _app.protocol.generateErrorJSON(output, error);
  }

  loadSafeguardsConfigJSON(doc);

  if (!_cfg.saveConfig(error, sizeof(error)))
    return _app.protocol.generateErrorJSON(output, error);
}

void BrineomaticController::handleDeleteLogs(JsonVariantConst input, JsonVariant output, ProtocolContext context)
{
  if (!LittleFS.remove("/run_log.json"))
    return _app.protocol.generateErrorJSON(output, "Error deleting logs.");
}

//
// Configuration JSON: validation, loading, and saving.  Moved out of the
// Brineomatic class; these operate on wm.getConfig() so both classes share
// the same live config data, with `defaults` as the load-time fallback.
//

void BrineomaticController::generateConfigJSON(JsonVariant output, UserRole role, ConfigPurpose purpose)
{
  BrineomaticConfig& _config = wm.getConfig();

  // shortcuts for the UI
  if (purpose == ConfigPurpose::UI_CONFIG) {
    output["has_boost_pump"] = wm.hasBoostPump();
    output["has_high_pressure_pump"] = wm.hasHighPressurePump();
    output["has_diverter_valve"] = wm.hasDiverterValve();
    output["has_flush_valve"] = wm.hasFlushValve();
    output["has_cooling_fan"] = wm.hasCoolingFan();
  }

  output["gauge_order"] = _config.gaugeOrder;

  output["post_run_flush_mode"] = _config.postRunFlushMode;
  output["post_run_flush_salinity"] = _config.postRunFlushSalinity;
  output["post_run_flush_duration"] = _config.postRunFlushDuration;
  output["post_run_flush_volume"] = _config.postRunFlushVolume;
  output["scheduled_flush_mode"] = _config.scheduledFlushMode;
  output["scheduled_flush_duration"] = _config.scheduledFlushDuration;
  output["scheduled_flush_volume"] = _config.scheduledFlushVolume;
  output["scheduled_flush_interval"] = _config.scheduledFlushInterval;
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

bool BrineomaticController::validateConfigJSON(JsonVariant config, char* error, size_t err_size)
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

bool BrineomaticController::validateUIConfigJSON(JsonVariant config, char* error, size_t err_size)
{
  bool ok = true;
  return ok;
}

bool BrineomaticController::validateGeneralConfigJSON(JsonVariant config, char* error, size_t err_size)
{
  bool ok = true;

  // post run flush
  if (config["post_run_flush_mode"]) {
    if (!checkInclusion(config, "post_run_flush_mode", Brineomatic::POST_RUN_FLUSH_MODES, error, err_size)) {
      ok = false;
      config.remove("post_run_flush_mode");
    }
  }

  // post_run_flush_salinity (integer >= 0)
  if (config["post_run_flush_salinity"]) {
    if (!checkIsNumber(config, "post_run_flush_salinity", error, err_size) ||
        !checkNumGT(config, "post_run_flush_salinity", 0, error, err_size)) {
      config.remove("post_run_flush_salinity");
      ok = false;
    }
  }

  // post_run_flush_duration (number >= 0)
  if (config["post_run_flush_duration"]) {
    if (!checkIsNumber(config, "post_run_flush_duration", error, err_size) ||
        !checkNumGT(config, "post_run_flush_duration", 0, error, err_size)) {
      config.remove("post_run_flush_duration");
      ok = false;
    }
  }

  // post_run_flush_volume (number >= 0)
  if (config["post_run_flush_volume"]) {
    if (!checkIsNumber(config, "post_run_flush_volume", error, err_size) ||
        !checkNumGT(config, "post_run_flush_volume", 0, error, err_size)) {
      config.remove("post_run_flush_volume");
      ok = false;
    }
  }

  // scheduled flush
  if (config["scheduled_flush_mode"]) {
    if (!checkInclusion(config, "scheduled_flush_mode", Brineomatic::SCHEDULED_FLUSH_MODES, error, err_size)) {
      ok = false;
      config.remove("scheduled_flush_mode");
    }
  }

  // scheduled_flush_duration (number >= 0)
  if (config["scheduled_flush_duration"]) {
    if (!checkIsNumber(config, "scheduled_flush_duration", error, err_size) ||
        !checkNumGT(config, "scheduled_flush_duration", 0, error, err_size)) {
      config.remove("scheduled_flush_duration");
      ok = false;
    }
  }

  // scheduled_flush_volume (number >= 0)
  if (config["scheduled_flush_volume"]) {
    if (!checkIsNumber(config, "scheduled_flush_volume", error, err_size) ||
        !checkNumGT(config, "scheduled_flush_volume", 0, error, err_size)) {
      config.remove("scheduled_flush_volume");
      ok = false;
    }
  }

  // scheduled_flush_interval (number >= 0)
  if (config["scheduled_flush_interval"]) {
    if (!checkIsNumber(config, "scheduled_flush_interval", error, err_size) ||
        !checkNumGT(config, "scheduled_flush_interval", 0, error, err_size)) {
      config.remove("scheduled_flush_interval");
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

bool BrineomaticController::validateHardwareConfigJSON(JsonVariant config,
  char* error,
  size_t err_size)
{
  bool ok = true;
  String control;

  // ---------------------------------------------------------
  // Boost Pump
  // ---------------------------------------------------------

  if (config["boost_pump_control"]) {
    if (!checkInclusion(config, "boost_pump_control", Brineomatic::BOOST_PUMP_CONTROLS, error, err_size)) {
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
    if (!checkInclusion(config, "high_pressure_pump_control", Brineomatic::HIGH_PRESSURE_PUMP_CONTROLS, error, err_size)) {
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
    if (!checkInclusion(config, "high_pressure_modbus_device", Brineomatic::HIGH_PRESSURE_PUMP_MODBUS_DEVICES, error, err_size)) {
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
    if (!checkInclusion(config, "high_pressure_valve_control", Brineomatic::HIGH_PRESSURE_VALVE_CONTROLS, error, err_size)) {
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
    if (!checkInclusion(config, "diverter_valve_control", Brineomatic::DIVERTER_VALVE_CONTROLS, error, err_size)) {
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
    if (!checkInclusion(config, "flush_valve_control", Brineomatic::FLUSH_VALVE_CONTROLS, error, err_size)) {
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
    if (!checkInclusion(config, "cooling_fan_control", Brineomatic::COOLING_FAN_CONTROLS, error, err_size)) {
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
    if (!checkInclusion(config, "motor_temperature_sensor_type", Brineomatic::MOTOR_TEMPERATURE_TYPES, error, err_size)) {
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
    if (!checkInclusion(config, "water_temperature_sensor_type", Brineomatic::WATER_TEMPERATURE_TYPES, error, err_size)) {
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
    if (!checkInclusion(config, "tank_level_sensor_type", Brineomatic::TANK_LEVEL_SENSOR_TYPES, error, err_size)) {
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
    if (!checkInclusion(config, "battery_level_sensor_type", Brineomatic::BATTERY_LEVEL_SENSOR_TYPES, error, err_size)) {
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

bool BrineomaticController::validateSafeguardsConfigJSON(JsonVariant config,
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

void BrineomaticController::loadConfigJSON(JsonVariantConst config)
{
  this->loadUIConfigJSON(config);
  this->loadGeneralConfigJSON(config);
  this->loadHardwareConfigJSON(config);
  this->loadSafeguardsConfigJSON(config);
}

void BrineomaticController::loadUIConfigJSON(JsonVariantConst config)
{
  BrineomaticConfig& _config = wm.getConfig();

  _config.gaugeOrder = config["gauge_order"] | defaults.gaugeOrder;
}

void BrineomaticController::loadGeneralConfigJSON(JsonVariantConst config)
{
  BrineomaticConfig& _config = wm.getConfig();

  _config.temperatureUnits = config["temperature_units"] | defaults.temperatureUnits;
  _config.pressureUnits = config["pressure_units"] | defaults.pressureUnits;
  _config.volumeUnits = config["volume_units"] | defaults.volumeUnits;
  _config.flowrateUnits = config["flowrate_units"] | defaults.flowrateUnits;
  _config.successMelody = config["success_melody"] | defaults.successMelody;
  _config.errorMelody = config["error_melody"] | defaults.errorMelody;
}

void BrineomaticController::loadHardwareConfigJSON(JsonVariantConst config)
{
  BrineomaticConfig& _config = wm.getConfig();

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

  // Flush settings.  Legacy "autoflush_*" keys are migrated for backward
  // compatibility: post run flush inherits them directly; scheduled flush
  // inherits them too, except SALINITY mode (not valid for scheduled) which
  // falls back to the default mode.
  String legacyFlushMode = config["autoflush_mode"] | defaults.postRunFlushMode;

  _config.postRunFlushMode = config["post_run_flush_mode"] | legacyFlushMode;
  _config.postRunFlushSalinity = config["post_run_flush_salinity"] | (float)(config["autoflush_salinity"] | defaults.postRunFlushSalinity);
  _config.postRunFlushDuration = config["post_run_flush_duration"] | (uint32_t)(config["autoflush_duration"] | defaults.postRunFlushDuration);
  _config.postRunFlushVolume = config["post_run_flush_volume"] | (float)(config["autoflush_volume"] | defaults.postRunFlushVolume);

  String scheduledLegacyMode = legacyFlushMode.equals("SALINITY") ? String(defaults.scheduledFlushMode) : legacyFlushMode;
  _config.scheduledFlushMode = config["scheduled_flush_mode"] | scheduledLegacyMode;
  _config.scheduledFlushDuration = config["scheduled_flush_duration"] | (uint32_t)(config["autoflush_duration"] | defaults.scheduledFlushDuration);
  _config.scheduledFlushVolume = config["scheduled_flush_volume"] | (float)(config["autoflush_volume"] | defaults.scheduledFlushVolume);
  _config.scheduledFlushInterval = config["scheduled_flush_interval"] | (uint32_t)(config["autoflush_interval"] | defaults.scheduledFlushInterval);

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

void BrineomaticController::loadSafeguardsConfigJSON(JsonVariantConst config)
{
  BrineomaticConfig& _config = wm.getConfig();

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
