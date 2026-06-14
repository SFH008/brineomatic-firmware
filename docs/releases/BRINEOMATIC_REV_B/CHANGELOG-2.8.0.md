# Version 2.8.0

## 🚀 New Features

- **Historical Sensor Graphs**
  - Brand new sensor graphing system using uPlot.js for better performance
  - New `SensorHistory` class buffering historical sensor data (up to 20k points, averaged over a fixed window)
  - Time window selector with multiple ranges (Last Minute, Last 15/30 Minutes, Last Hour, etc.)
  - Time is relative to "now" so the correct data loads regardless of when the page opens
  - Graphs lazy-load: only the currently selected graph fetches historical data, not all of them at once
  - Download button to export graph data as JSON
  - Using to ESP `micros()` for timestamping - no rollover issues
  - Graphs hidden in MFD view (`!isMFD()` guard)

## 🛠️ Improvements & Enhancements

- **Gauges**
  - Added tick marks on gauges reflecting safety cutoffs, respecting the current run mode (fixes #9)
  - Refactored gauge logic into a dedicated `SensorGauges.js`
  - Single source of truth shared between gauges and graphs for ranges
- Added fixed-decimal formatting to all displayed values
- Bumped run cycle stabilization to 15s, with a stabilization period before sensor stats tracking begins
- `stats.stopCycle()` is now the first thing that happens at the end of a cycle
- Centered download buttons with icons for a consistent look

## 🐛 Bug Fixes

- Fixed the ELF file location in `parse_coredump.py`

---