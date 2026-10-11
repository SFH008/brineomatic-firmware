"""Dependency-free state and command model. Units here are explicit, not GX keywords."""

import json
import math
import time
from collections import deque

SERVICE = "com.victronenergy.brineomatic"
STOP_PATH = "/SwitchableOutput/stop/State"
REQUEST_PATH = "/Commands/Request"
STATES = ("STARTUP", "MANUAL", "IDLE", "RUNNING", "STOPPING", "FLUSHING",
          "PICKLING", "DEPICKLING", "PICKLED", "UNKNOWN")
ACTIVE = {"RUNNING", "FLUSHING", "PICKLING", "DEPICKLING"}
# JSON updates are in base units; MQTT updates are NOT interchangeable with these.
SENSORS = {
    "product_flowrate": ("Product flow", "L/h", 1),
    "product_salinity": ("Product salinity", "ppm", 0),
    "membrane_pressure": ("Membrane pressure", "bar", 1),
    "filter_pressure": ("Filter pressure", "bar", 1),
    "volume": ("Produced this run", "L", 1),
    "motor_temperature": ("Motor temperature", "°C", 1),
}


def number(value):
    if isinstance(value, bool) or not isinstance(value, (float, int)):
        return None
    try:
        return value if math.isfinite(value) else None
    except OverflowError:
        return None


class Model:
    def __init__(self, commands_enabled=False, stale_after=5, clock=time.monotonic):
        self.commands_enabled = commands_enabled
        self.stale_after = stale_after
        self.clock = clock
        self.last_update = None
        self.pending = None
        self.seen = deque(maxlen=256)
        self.values = {
            "/Connected": 0, "/Status": "DISCONNECTED", "/DataAge": None,
            "/Commands/Enabled": int(commands_enabled),
            "/Commands/Request": "", "/Commands/Result": "",
            "/Connection/Error": "", "/RunResult": "", "/NextFlushSeconds": None,
            "/SwitchableOutput/Capabilities": 0,
            STOP_PATH: 0,
            "/SwitchableOutput/stop/Name": "Stop watermaker",
            "/SwitchableOutput/stop/Status": 0x20,
            "/SwitchableOutput/stop/Settings/Adjustable": 0,
            "/SwitchableOutput/stop/Settings/Group": "Brineomatic",
            "/SwitchableOutput/stop/Settings/CustomName": "Stop watermaker",
            "/SwitchableOutput/stop/Settings/ShowUIControl": 1 if commands_enabled else 0,
            "/SwitchableOutput/stop/Settings/Type": 0,
            "/SwitchableOutput/stop/Settings/ValidTypes": 1,
        }
        for key, (name, unit, decimals) in SENSORS.items():
            base = "/GenericInput/" + key
            self.values.update({
                base + "/Value": None, base + "/Status": 1, base + "/Name": name,
                base + "/Settings/Group": "Brineomatic",
                base + "/Settings/CustomName": name,
                base + "/Settings/ShowUIInput": 6,
                base + "/Settings/Type": 1, base + "/Settings/ValidTypes": 2,
                base + "/Settings/Unit": unit, base + "/Settings/Decimals": decimals,
                base + "/Settings/PrimaryLabel": name,
            })

    def update(self, payload):
        if not isinstance(payload, dict) or payload.get("brineomatic") is not True:
            raise ValueError("Not a Brineomatic update")
        state = payload.get("status")
        if state not in STATES:
            raise ValueError("Unknown watermaker status")
        self.last_update = self.clock()
        self.values.update({"/Connected": 1, "/Status": state, "/DataAge": 0,
                            "/Connection/Error": "",
                            "/RunResult": str(payload.get("run_result", ""))[:128]})
        countdown = number(payload.get("next_flush_countdown"))
        # Firmware countdowns are milliseconds.
        self.values["/NextFlushSeconds"] = countdown / 1000 if countdown is not None and countdown > 0 else None
        for key in SENSORS:
            base = "/GenericInput/" + key
            value = number(payload.get(key))
            self.values[base + "/Value"] = value
            self.values[base + "/Status"] = 0 if value is not None else 1
        self.values["/SwitchableOutput/stop/Status"] = (
            0 if self.commands_enabled and state in ACTIVE else 0x20)

    def disconnect(self, reason="Connection unavailable"):
        self.values.update({"/Connected": 0, "/Status": "DISCONNECTED",
                            "/Connection/Error": reason[:128],
                            "/NextFlushSeconds": None})
        self.values["/SwitchableOutput/stop/Status"] = 0x20
        for key in SENSORS:
            self.values["/GenericInput/" + key + "/Value"] = None
            self.values["/GenericInput/" + key + "/Status"] = 1

    def tick(self):
        age = None if self.last_update is None else max(0, self.clock() - self.last_update)
        self.values["/DataAge"] = int(age) if age is not None else None
        if age is not None and age >= self.stale_after:
            self.disconnect("Telemetry is stale")

    def request(self, raw):
        """Accept one complete request; never persist or automatically replay it."""
        if not isinstance(raw, str) or len(raw.encode("utf-8")) > 1024:
            raise ValueError("Request must be a JSON string of at most 1024 bytes")
        request = json.loads(raw)
        if not isinstance(request, dict) or set(request) - {"id", "operation", "volume_l", "duration_ms", "expires_at"}:
            raise ValueError("Invalid request fields")
        request_id = request.get("id")
        if not isinstance(request_id, str) or not 1 <= len(request_id) <= 96:
            raise ValueError("A request id is required")
        if request_id in self.seen:
            raise ValueError("Duplicate request")
        if not self.commands_enabled:
            raise ValueError("Commands are disabled")
        expiry = number(request.get("expires_at"))
        if expiry is None or not time.time() < expiry <= time.time() + 30:
            raise ValueError("Request expired or expiration is invalid")
        self.tick()
        if not self.values["/Connected"]:
            raise ValueError("Watermaker is disconnected")
        if self.pending:
            raise ValueError("Another command is pending")
        operation = request.get("operation")
        state = self.values["/Status"]
        allowed = {"start": {"IDLE"}, "flush": {"IDLE", "PICKLED"}, "stop": ACTIVE}
        if operation not in allowed or state not in allowed[operation]:
            raise ValueError("Operation is unavailable in current state")
        params = [key for key in ("volume_l", "duration_ms") if key in request]
        if len(params) > 1 or (operation == "stop" and params):
            raise ValueError("Use at most one target and no target for Stop")
        command = {"cmd": {"start": "start_watermaker", "flush": "flush_watermaker",
                           "stop": "stop_watermaker"}[operation]}
        for key in params:
            value = number(request[key])
            limit = 86400000 if key == "duration_ms" else 100000
            if value is None or not 0 < value <= limit:
                raise ValueError("Target is outside scaffold limits")
            if key == "duration_ms" and int(value) != value:
                raise ValueError("Duration must be whole milliseconds")
            command["duration" if key == "duration_ms" else "volume"] = value
        self.seen.append(request_id)
        self.pending = (request_id, command, expiry)
        self.result(request_id, "pending")
        return command

    def result(self, request_id, status):
        self.values["/Commands/Result"] = json.dumps({"id": request_id, "status": status})

    def finish(self, status):
        if self.pending:
            self.result(self.pending[0], status)
        self.pending = None
        self.values[STOP_PATH] = 0
