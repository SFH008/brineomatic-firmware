# v2.7

* split autoflush into two modes:
    * Post Run Flush -> run after every run cycle
        * none, time, volume, salinity
        * all autoflush configuration settings become post_run_flush settings
        * post run flush is triggered by the state machine after a run cycle.
    * Scheduled Flush -> run every X hours
        * none, time, volume. no salinity
        * unless it was set to salinity mode, autoflush configuration settings become scheduled flush
            * otherwise fall back to defaults
        * schedule flush is triggered by the state machine from idle state after a certain interval
    * autoflush_use_high_pressure_motor stays as a common/shared configuration option
    * autoflushEnabled() will need to be split into two functions: scheduled and post_run
    * we dont need separate the default variables, eg:
        * YB_AUTOFLUSH_MODE
        * YB_AUTOFLUSH_SALINITY
        * YB_AUTOFLUSH_DURATION
        * YB_AUTOFLUSH_VOLUME
        * YB_AUTOFLUSH_INTERVAL
    * FLUSHING portion of the state machine should be relatively unchanged as both flush cycles are the same mechanically, just the trigger method is different.  also, either flush should reset the time for scheduled flushes.

# v2.8

* configurable thresholds for gauges - issue #3
* threshold indication on gauges - issue #9
* add graphs (stored in psram)

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