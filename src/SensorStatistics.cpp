#include "SensorStatistics.h"

SensorStatistics::SensorStatistics()
{
  reset();
}

void SensorStatistics::start()
{
  reset();
  _active = true;
}

void SensorStatistics::stop()
{
  _active = false;
}

void SensorStatistics::reset()
{
  _stats.clear();
  _start = NAN;
  _end = NAN;
}

void SensorStatistics::add(float value)
{
  if (!_active)
    return;

  if (_stats.count() == 0)
    _start = value;
  _end = value;

  _stats.add(value);
}

bool SensorStatistics::isActive()
{
  return _active;
}

uint32_t SensorStatistics::count()
{
  return _stats.count();
}

float SensorStatistics::getStart()
{
  return _start;
}

float SensorStatistics::getEnd()
{
  return _end;
}

float SensorStatistics::getMinimum()
{
  return _stats.count() ? _stats.minimum() : NAN;
}

float SensorStatistics::getMaximum()
{
  return _stats.count() ? _stats.maximum() : NAN;
}

float SensorStatistics::getAverage()
{
  return _stats.count() ? _stats.average() : NAN;
}
