# v2.7

* move to config struct
    * public config and public defaults.
    * no more huge #define list
    * move config handling to controller
        * how to handle brineomatic class and brineomatic controller both needing the config.
* configurable thresholds for gauges - issue #3
* threshold indication on gauges - issue #9
* add graphs (stored in psram)
* add averages to salinity / pressure / flowrate for runtime - log it.

# v2.8

* pid control of pressure
    * stepper -> stepper fixed angle
    * pid_stepper -> stepper w/ pid
    * use quickpid + sTune
* non-reboot necessary config (add to manual mode?)
    * tds offset
    * not sure i like this.

# LONG TERM:

* custom gauge layout for each state?  idle, running, stopping, pickling, etc?
* update yarrboard client if any changes needed - probably for state
* update signalk plugin - same
* other MFD integrations:
    * garmin?
    * raymarine?

# B&G MFD Dev Info
B&G : User Agent: Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) QtWebEngine/5.12.9 Chrome/69.0.3497.128 Safari/537.36
https://ungoogled-software.github.io/ungoogled-chromium-binaries/releases/linux_portable/64bit/