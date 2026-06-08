#ifndef YB_SENSOR_STATS_TABLE_H
#define YB_SENSOR_STATS_TABLE_H

#include "SensorStatistics.h"
#include "etl/map.h"
#include "etl/string.h"
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

    // Begin / end tracking for every sensor in one cycle.
    void startCycle(const char* cycle)
    {
      auto it = _cycles.find(Key(cycle));
      if (it != _cycles.end())
        for (auto& s : it->second)
          s.second.start();
    }

    void stopCycle(const char* cycle)
    {
      auto it = _cycles.find(Key(cycle));
      if (it != _cycles.end())
        for (auto& s : it->second)
          s.second.stop();
    }

    // Stop tracking for every sensor in every cycle.
    void stopCycle()
    {
      for (auto& c : _cycles)
        for (auto& s : c.second)
          s.second.stop();
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

  private:
    CycleMap _cycles;
};

#endif
