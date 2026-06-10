# Version 2.7.0

## 🚀 New Features

- **Cycle Sensor Statistics**
  - New `SensorStatistics` class tracks min, max, average, and standard deviation for each sensor over a cycle
  - Per-cycle tracking for all eight sensors: water/motor temperature, product/brine flowrate, product/brine salinity, and filter/membrane pressure
  - Statistics start/stop tracking wired into the RUN / FLUSH / PICKLE / DEPICKLE cycles
  - Stats displayed in the UI for the most recent run/flush/etc. cycle and for individual log entries
  - Sensor statistics wired into JSON generation for both the UI and logging
  - Unit conversions applied to the sensor statistics UI
  - Pressure error sentinel (-999) excluded so it doesn't pollute min/average stats

- **Split Autoflush into Post Run Flush and Scheduled Flush**
  - Post Run Flush runs after every run cycle (NONE/TIME/SALINITY/VOLUME)
  - Scheduled Flush runs from idle on an interval (NONE/TIME/VOLUME)
  - High-pressure-motor option remains shared between both modes
  - Either flush completing resets the scheduled-flush timer
  - Legacy `autoflush_*` config keys are migrated automatically (scheduled flush falls back to default mode if it inherited the now-invalid SALINITY mode)

## 🛠️ Improvements & Enhancements

- Updated control visibility during the various run modes:
  - Boost pump / HP pump shown only during running / pickle / depickle / stop
  - Diverter valve shown only during running / stop
  - Flush valve shown only during flush / stop
  - Cooling fan shown during any run mode
- Restored a gradual stop / pressure release after successful runs
- Exiting MANUAL mode now triggers a hardware init to return everything to a normal state (eg. if a pump was left on)
- Status table cell height fix

## 🧹 Refactoring

- Introduced a `BrineomaticConfig` struct holding all config options saved/loaded from JSON
- Moved all JSON config parsing from `Brineomatic` into `BrineomaticController`
- Moved all JSON and MQTT generation out of `Brineomatic` and into `BrineomaticController`

## Infrastructure

- Bumped YarrboardFramework to v3.0.1

---