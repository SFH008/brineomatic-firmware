# v2.7

* statistics
    * add BrineomaticController->generateRunStatsJSON(JsonVariant output)
        * add get{variable}Stats() getters to Brineomatic.
        * add a "stats" member to output with the following format for each variable:
            *   SensorStatistics waterTemperatureStats;
                SensorStatistics motorTemperatureStats;
                SensorStatistics productFlowrateStats;
                SensorStatistics brineFlowrateStats;
                SensorStatistics productSalinityStats;
                SensorStatistics brineSalinityStats;
                SensorStatistics filterPressureStats;
                SensorStatistics membranePressureStats;
        * waterTemperatureStats -> {variable}Stats
        "stats": {
            {variable}: {
                    "start":  getStart();
                    "end": getEnd();
                    "min": getMinimum();
                    "max": getMaximum();
                    "avg": getAverage();
            }
        }
        * check if count() > 0, otherwise do not add

    * logging
        * add stats member to the logging call via generateRunStatsJSON()
        * add stats link as last link in the table
            * on click, should open a modal that contains all of the stats for each variable from that run
    * add stats member to the generateStatsJSON call via generateRunStatsJSON()
        * on the stats page, show the stats for each variable in a table
        * make a shared generateRunStatsHTML() function to generate both

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