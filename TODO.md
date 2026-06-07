# v2.7

* statistics
    * add SensorStatistics tracker for each of these variables:
        float currentWaterTemperature -> waterTemperatureStats;
        float currentMotorTemperature;
        float currentProductFlowrate;
        float currentBrineFlowrate;
        float currentProductSalinity;
        float currentBrineSalinity;
        float currentFilterPressure;
        float currentMembranePressure;
    * call {variable}Stats->add() in the set{Variable}() function
        * if the set{Variable}() function does not exist, add it.

    * add start() / end() calls to state machine
    * add generateRunStatsJSON(output)
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