#ifndef YB_SENSOR_STATS_TABLE_H
#define YB_SENSOR_STATS_TABLE_H

#include "SensorStatistics.h"
#include "etl/map.h"
#include "etl/string.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <initializer_list>

//
// Nested fixed-capacity map of per-cycle, per-sensor statistics.
//   table["run"]["water_temperature"].getAverage()
//
// Each cycle is declared up front with exactly the sensors it tracks via
// defineCycle(); nothing is allocated at runtime.  The table exposes
// begin()/end() so callers (e.g. BrineomaticController) can walk it as
// cycle -> (sensor -> stats) key/value pairs to generate JSON; the table
// itself stays ignorant of any serialization format.
//
class SensorStatsTable
{
  public:
    static constexpr size_t MAX_CYCLES = 4;  // RUN, FLUSH, PICKLE, DEPICKLE
    static constexpr size_t MAX_SENSORS = 8; // most any one cycle tracks
    static constexpr size_t KEY_LEN = 24;

    using Key = etl::string<KEY_LEN>;
    using SensorMap = etl::map<Key, SensorStatistics, MAX_SENSORS>;
    using CycleMap = etl::map<Key, SensorMap, MAX_CYCLES>;

    // Register a cycle and the sensors it tracks.  Call once during init().
    void defineCycle(const char* cycle, std::initializer_list<const char*> sensors)
    {
      SensorMap& sm = _cycles[Key(cycle)]; // inserts an empty sensor map
      for (const char* s : sensors)
        sm[Key(s)]; // default-constructs an inactive SensorStatistics
    }

    // Direct access: table["run"]["water_temperature"]
    SensorMap& operator[](const char* cycle) { return _cycles[Key(cycle)]; }

    // Begin tracking for every sensor in one cycle, after a stabilization
    // delay.  Call this repeatedly from inside the cycle's run loop: the first
    // call arms an internal timer, and tracking only actually begins once
    // `delay` ms have elapsed.  This skips the readings taken while pressures,
    // flows and temperatures are still settling at the start of a cycle.
    void startCycle(const char* cycle, uint32_t delay = 5000)
    {
      // Arm the timer on the first call of a cycle.
      if (_startTime == 0)
        _startTime = millis();

      // Already tracking, or still inside the stabilization window.
      if (_started || millis() - _startTime < delay)
        return;

      auto it = _cycles.find(Key(cycle));
      if (it != _cycles.end())
        for (auto& s : it->second)
          s.second.start();
      _started = true;
    }

    void stopCycle(const char* cycle)
    {
      auto it = _cycles.find(Key(cycle));
      if (it != _cycles.end())
        for (auto& s : it->second)
          s.second.stop();
      _startTime = 0;
      _started = false;
    }

    // Stop tracking for every sensor in every cycle.
    void stopCycle()
    {
      for (auto& c : _cycles)
        for (auto& s : c.second)
          s.second.stop();
      _startTime = 0;
      _started = false;
    }

    // Route a sample to whichever cycle is currently active.  add() is gated on
    // each SensorStatistics' own active flag, so only the running cycle's slot
    // records; the rest no-op.  find() means a sensor only lands in the cycles
    // that actually declared it, so a misspelled name records nowhere rather
    // than silently creating a junk entry.
    void add(const char* sensor, float value)
    {
      Key k(sensor);
      for (auto& c : _cycles) {
        auto it = c.second.find(k);
        if (it != c.second.end())
          it->second.add(value);
      }
    }

    // Iteration for serialization.  Yields pairs of (cycle name, SensorMap);
    // each SensorMap in turn yields (sensor name, SensorStatistics).
    CycleMap::iterator begin() { return _cycles.begin(); }
    CycleMap::iterator end() { return _cycles.end(); }

    // Serialize a single cycle's sensors into `out` as sensor -> stats objects.
    // Sensors with no samples are skipped.  Returns the number of sensors
    // written, so callers can drop an empty parent (e.g. log.remove("stats")).
    size_t cycleToJson(const char* cycle, JsonObject out)
    {
      auto it = _cycles.find(Key(cycle));
      if (it == _cycles.end())
        return 0;
      return sensorMapToJson(it->second, out);
    }

    // Serialize every cycle into `out` as cycle -> (sensor -> stats).  Cycles
    // whose sensors all lack samples are dropped entirely.
    void toJson(JsonObject out)
    {
      for (auto& cycle : _cycles) {
        JsonObject cycleObj = out[cycle.first.c_str()].to<JsonObject>();
        if (sensorMapToJson(cycle.second, cycleObj) == 0)
          out.remove(cycle.first.c_str());
      }
    }

  private:
    // Write each sampled sensor in `sm` as a stats object under `out`; returns
    // the count written so callers can decide whether the parent is empty.
    static size_t sensorMapToJson(SensorMap& sm, JsonObject out)
    {
      size_t written = 0;
      for (auto& sensor : sm) {
        SensorStatistics& st = sensor.second;
        if (st.count() == 0)
          continue;

        JsonObject s = out[sensor.first.c_str()].to<JsonObject>();
        s["start"] = st.getStart();
        s["end"] = st.getEnd();
        s["min"] = st.getMinimum();
        s["max"] = st.getMaximum();
        s["avg"] = st.getAverage();
        s["stddev"] = st.getStdDev();
        written++;
      }
      return written;
    }

    CycleMap _cycles;

    // Shared stabilization-delay state for whichever cycle is currently active.
    uint32_t _startTime = 0; // millis() when the current cycle armed (0 = idle)
    bool _started = false;   // have we passed the delay and begun tracking?
};

#endif
