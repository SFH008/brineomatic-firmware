# v2.8

# Sensor data graphs (stored in psram)

We want to create a graph system for data from each of the important sensors:  "water_temperature", "motor_temperature", "product_flowrate", "brine_flowrate", "product_salinity", "brine_salinity", "filter_pressure", "membrane_pressure", "battery_level", "tank_level"

We will use c3.js / d3.js for the graphing library.

The graphs should be located on a separate graph tab that has its own nav tabs to select which sensor data to display.

## Phase 1: Memory & Storage Strategy (ESP32)
Data Structure: Use an etl::circular_buffer holding a tightly packed struct (e.g., 4-byte timestamp, 4-byte float value) to ensure a predictable 8-byte footprint per entry.

PSRAM Allocation:
    * Static: Use the EXT_RAM_BSS_ATTR macro to force the global ETL buffer into the PSRAM .bss segment.

## Phase 2: Data Transfer Strategy (Network)
Bypass JSON: Do not use ArduinoJson for the historical data dump, as the text overhead (500+ KB) will exhaust SRAM and crash the microcontroller.

Raw Binary: Serve the data as an application/octet-stream binary blob (exactly 128 KB for 16,384 points).

## Phase 3: The Chunked Loop (ESP32 Web Server)
Maintain Order: Do not dump the raw memory pointer directly, as the circular buffer wraps around. Instead, use a C++ range-based for loop to iterate through the ETL buffer logically (oldest to newest).

Stack Buffering: Create a small, temporary array on the fast internal stack (e.g., 64 points / 512 bytes).

Chunking: Fill this temporary stack buffer in the loop, and use request.write() to blast out 512-byte chunks to the network. This keeps memory usage tiny while maximizing TCP efficiency.

## Phase 4: Frontend Parsing (JavaScript)
Browser Fetch: Use fetch('/api/sensor_history?sensor={sensor}') and extract the payload using .arrayBuffer().

Instant Parsing: Use JavaScript's native DataView to parse the 8-byte chunks (uint32 and float32) back into JavaScript objects for your charting library. This is extremely fast and fully supported in older browsers like Chrome 69.

## Phase 5: Graph Update

Extend the update message handler to update the graph with realtime data

## Other Stuff 

* move non-hardware config out of hardware config - add a single tab "Runtime"
    * Flush Mode
        * auto flush configuration
        * use hp pump
        * configurable delays
    * Run Mode
        * boost pump delay
        * hp pump delay
        * hp valve angle + speed
            * not step angle, gear ratio, or current... thats hardware
        * PID vs Fixed Angle
            * pid can have PID values here

* configurable thresholds for gauges - issue #3
* threshold indication on gauges - issue #9

# LONG TERM:

* pid control of pressure
    * stepper -> stepper fixed angle
    * pid_stepper -> stepper w/ pid
    * use quickpid + sTune
* non-reboot necessary config (add to manual mode?)
    * tds offset
    * not sure i like this.

* custom gauge layout for each state?  idle, running, stopping, pickling, etc?
* update yarrboard client if any changes needed - probably for state
* update signalk plugin - same
* other MFD integrations:
    * garmin?
    * raymarine?

# B&G MFD Dev Info
B&G : User Agent: Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) QtWebEngine/5.12.9 Chrome/69.0.3497.128 Safari/537.36
https://ungoogled-software.github.io/ungoogled-chromium-binaries/releases/linux_portable/64bit/