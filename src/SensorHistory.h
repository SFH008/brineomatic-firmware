#ifndef YB_SENSOR_HISTORY_H
#define YB_SENSOR_HISTORY_H

#include "etl/circular_buffer.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

//
// Rolling per-sensor history of raw readings, used by the web UI graphs.
//
// Each sensor gets a fixed circular buffer of tightly packed 8-byte points
// (uptime seconds + float value).  The buffers total ~1.3MB so they cannot
// live in internal RAM: init() carves them out of the PSRAM heap
// (MALLOC_CAP_SPIRAM) and hands the storage to etl::circular_buffer_ext.
// If PSRAM allocation fails, history quietly stays disabled and add()/copy()
// no-op.
//
// add() is called from the Brineomatic sensor setters (the same place that
// feeds SensorStatsTable) and rate-limits itself to one point per second per
// sensor, so a full buffer covers ~4.5 hours.  Timestamps are device uptime
// rather than epoch so the data stays coherent without NTP; the HTTP handler
// reports the current uptime alongside the dump so the browser can anchor
// the series to wall-clock time.  Uptime comes from esp_timer_get_time()
// (64-bit microseconds) rather than millis(), whose 32-bit millisecond counter
// wraps after ~49 days and would corrupt the anchoring on long-running rigs.
//
// add() runs on the main loop task while the HTTP server streams from its own
// task, so all buffer access goes through a mutex.  Readers pull bounded
// chunks via copy() rather than holding the lock for a whole dump.
//

struct SensorHistoryPoint {
    uint32_t time; // device uptime, seconds
    float value;
};
static_assert(sizeof(SensorHistoryPoint) == 8, "SensorHistoryPoint must pack to 8 bytes");

class SensorHistory
{
  public:
    static constexpr size_t MAX_POINTS = 20000;
    static constexpr uint32_t SAMPLE_INTERVAL_MS = (12 * 60 * 60 * 1000) / MAX_POINTS;

    using Buffer = etl::circular_buffer_ext<SensorHistoryPoint>;

    // every sensor we track, in storage order
    static constexpr const char* const SENSOR_NAMES[] = {
      "water_temperature",
      "motor_temperature",
      "product_flowrate",
      "brine_flowrate",
      "product_salinity",
      "brine_salinity",
      "filter_pressure",
      "membrane_pressure",
      "battery_level",
      "tank_level",
    };
    static constexpr size_t SENSOR_COUNT = sizeof(SENSOR_NAMES) / sizeof(SENSOR_NAMES[0]);

    // Allocate the buffers from PSRAM.  Call once during setup; returns false
    // (leaving history disabled) if the allocation fails.
    bool init()
    {
      if (_buffers[0])
        return true;

      _mutex = xSemaphoreCreateMutex();

      for (size_t i = 0; i < SENSOR_COUNT; i++) {
        // circular_buffer_ext keeps one spare slot, hence MAX_POINTS + 1
        void* storage = heap_caps_malloc((MAX_POINTS + 1) * sizeof(SensorHistoryPoint), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!storage)
          return false;
        _buffers[i] = new Buffer(storage, MAX_POINTS);
      }

      return true;
    }

    // index of a sensor name, or -1 if unknown
    int indexOf(const char* sensor) const
    {
      for (size_t i = 0; i < SENSOR_COUNT; i++)
        if (!strcmp(sensor, SENSOR_NAMES[i]))
          return (int)i;
      return -1;
    }

    // Accumulate readings and record their average once per SAMPLE_INTERVAL_MS.
    void add(const char* sensor, float value)
    {
      int idx = indexOf(sensor);
      if (idx < 0 || !_buffers[idx])
        return;

      // Fold every reading into the running average for this interval.
      _total[idx] += value;
      _count[idx]++;

      // millis() drives the sample cadence: it's only ever read as a delta, and
      // unsigned subtraction stays correct across its 32-bit wrap.  The stored
      // timestamp, however, is an absolute value the browser anchors against, so
      // it comes from the 64-bit esp_timer instead to avoid the ~49-day wrap.
      uint32_t now = millis();
      if (_lastSample[idx] != 0 && now - _lastSample[idx] < SAMPLE_INTERVAL_MS)
        return;
      _lastSample[idx] = now;

      float average = _total[idx] / _count[idx];
      _total[idx] = 0;
      _count[idx] = 0;

      uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);

      xSemaphoreTake(_mutex, portMAX_DELAY);
      _buffers[idx]->push(SensorHistoryPoint{uptime, average});
      xSemaphoreGive(_mutex);
    }

    size_t count(int idx)
    {
      if (!_buffers[idx])
        return 0;

      xSemaphoreTake(_mutex, portMAX_DELAY);
      size_t n = _buffers[idx]->size();
      xSemaphoreGive(_mutex);
      return n;
    }

    // Copy up to maxPoints points starting at logical index `start` (0 =
    // oldest) into `out`, in oldest-to-newest order.  Returns the number
    // copied.  Indexing logically instead of dumping raw memory keeps the
    // points ordered across the circular buffer's wrap point.
    size_t copy(int idx, size_t start, SensorHistoryPoint* out, size_t maxPoints)
    {
      if (!_buffers[idx])
        return 0;

      xSemaphoreTake(_mutex, portMAX_DELAY);
      Buffer& buf = *_buffers[idx];
      size_t n = 0;
      for (; n < maxPoints && start + n < buf.size(); n++)
        out[n] = buf[start + n];
      xSemaphoreGive(_mutex);
      return n;
    }

    // Clear the history for a single sensor (by name), discarding its buffered
    // points and any in-progress interval average.
    void reset(const char* sensor)
    {
      int idx = indexOf(sensor);
      if (idx < 0)
        return;
      reset(idx);
    }

    // Clear the history for a single sensor (by index).
    void reset(int idx)
    {
      if (idx < 0 || (size_t)idx >= SENSOR_COUNT || !_buffers[idx])
        return;

      xSemaphoreTake(_mutex, portMAX_DELAY);
      _buffers[idx]->clear();
      xSemaphoreGive(_mutex);

      _total[idx] = 0;
      _count[idx] = 0;
      _lastSample[idx] = 0;
    }

    // Clear the history for every sensor.
    void reset()
    {
      for (size_t i = 0; i < SENSOR_COUNT; i++)
        reset((int)i);
    }

  private:
    SemaphoreHandle_t _mutex = NULL;
    uint32_t _lastSample[SENSOR_COUNT] = {0};
    double _total[SENSOR_COUNT] = {0};
    uint32_t _count[SENSOR_COUNT] = {0};
    Buffer* _buffers[SENSOR_COUNT] = {nullptr};
};

#endif
