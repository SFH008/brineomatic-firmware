# v2.8

## Graphs

* add a graph time range selector (dropdown) below the nav pills
    * default to 3 hours
    * options: Last 1...12 Hour(s)
    * onchange load fresh data from /api/sensor_history
    * add startTime and endTime parametes to BrineomaticController - /api/sensor_history
        * should be same unix timestamp format as the sensor history data points
    * return any points that are after start, before end, or all points if none specified.


* find out what is causing the sporadic failure with starting a flush.

## Other

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