#ifndef YB_SENSOR_STATISTICS_H
#define YB_SENSOR_STATISTICS_H

#include <Arduino.h>
#include "Statistic.h"

//
// Tracks start / end / min / max / average for a single sensor value over the
// course of a run.  Sampling is gated by an active flag: start() begins a fresh
// tracking session (resetting all stats) and stop() ends it, leaving the
// collected statistics intact for reporting.  add() is a no-op while inactive.
//
// Backed by RobTillaart/Statistic, so memory is O(1) per variable (running
// sum/count/min/max) rather than buffering every sample.
//
class SensorStatistics
{
  public:
    SensorStatistics();

    void start(); // reset and begin tracking
    void stop();  // stop tracking, retaining collected statistics
    void reset(); // clear all collected statistics

    void add(float value); // record a sample (ignored while inactive)

    bool isActive();
    uint32_t count();

    float getStart();
    float getEnd();
    float getMinimum();
    float getMaximum();
    float getAverage();

  private:
    statistic::Statistic<float, uint32_t, false> _stats;
    bool _active = false;
    float _start = NAN;
    float _end = NAN;
};

#endif
