"""Cerbo adapter: D-Bus/GLib on the main thread, HTTP on one worker."""

import argparse
import json
import logging
import math
from pathlib import Path
import signal
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor

from . import __version__
from .core import Model, REQUEST_PATH, SERVICE, STOP_PATH
from .transport import HttpTransport, SimulatedTransport


def load_config(path):
    config = json.loads(Path(path).read_text())
    if not isinstance(config, dict):
        raise ValueError("Configuration must be an object")
    if config.get("mode") not in {"simulation", "http"}:
        raise ValueError("mode must be simulation or http")
    if type(config.get("commands_enabled", False)) is not bool:
        raise ValueError("commands_enabled must be boolean")
    stale = config.get("stale_after_seconds", 5)
    if isinstance(stale, bool) or not isinstance(stale, (int, float)) or not math.isfinite(stale) or stale < 3:
        raise ValueError("stale_after_seconds must be finite and at least 3")
    # Single instance scaffold: native QML resolves service type at instance 0.
    if type(config.get("device_instance", 0)) is not int or config.get("device_instance", 0) != 0:
        raise ValueError("Only DeviceInstance 0 is supported by this scaffold")
    return config


def transport_for(config):
    if config["mode"] == "simulation":
        return SimulatedTransport()
    credentials_path = Path(config["credentials_file"])
    if credentials_path.stat().st_mode & 0o077:
        raise ValueError("Credentials file must be private (chmod 600)")
    credentials = json.loads(credentials_path.read_text())
    if not all(isinstance(credentials.get(key), str) and credentials[key] for key in ("user", "pass")):
        raise ValueError("Credentials must contain non-empty user and pass strings")
    return HttpTransport(config["endpoint"], credentials["user"], credentials["pass"],
                         ca_file=config.get("ca_file"))


def check_firmware(path="/opt/victronenergy/version"):
    version = Path(path).read_text().splitlines()[0].strip()
    if version not in {"v3.81", "3.81"}:
        raise ValueError("This scaffold targets Venus OS 3.81 exactly")


def check_demo(config, path="/opt/victronenergy/version"):
    version = Path(path).read_text().splitlines()[0].strip()
    if version != "v99.99~99" or config["mode"] != "simulation":
        raise ValueError("Demo requires venus-docker placeholder firmware and simulation transport")


class Runtime:
    def __init__(self, config):
        # Keep imports optional so model tests and terminal simulation need no GX libraries.
        import dbus
        from dbus.mainloop.glib import DBusGMainLoop
        from gi.repository import GLib
        sys.path.insert(0, "/opt/victronenergy/dbus-systemcalc-py/ext/velib_python")
        from vedbus import VeDbusService

        DBusGMainLoop(set_as_default=True)
        self.GLib = GLib
        self.loop = GLib.MainLoop()
        self.model = Model(config.get("commands_enabled", False),
                           config.get("stale_after_seconds", 5))
        self.transport = transport_for(config)
        self.pool = ThreadPoolExecutor(max_workers=1)
        self.future = None
        self.job = None
        self.next_poll = 0
        self.service = VeDbusService(SERVICE, bus=dbus.SystemBus(), register=False)
        identity = {
            "/Mgmt/ProcessName": __file__, "/Mgmt/ProcessVersion": __version__,
            "/Mgmt/Connection": "Simulation" if config["mode"] == "simulation" else "HTTP JSON",
            "/DeviceInstance": 0, "/ProductId": 0,  # Unassigned; never impersonate Victron hardware.
            "/ProductName": "Brineomatic", "/CustomName": "Brineomatic",
            "/FirmwareVersion": __version__, "/HardwareVersion": "scaffold",
            "/Serial": "brineomatic-scaffold", "/Simulation": int(config["mode"] == "simulation"),
        }
        for path, value in identity.items():
            self.service.add_path(path, value)
        for path, value in self.model.values.items():
            writable = path in {REQUEST_PATH, STOP_PATH}
            self.service.add_path(path, value, writeable=writable,
                                  onchangecallback=self.on_write if writable else None)
        self.service.register()  # Publish only once all paths, including DeviceInstance, exist.
        self.GLib.timeout_add(100, self.tick)

    def on_write(self, path, value):
        if path == STOP_PATH and value == 0:
            self.model.values[STOP_PATH] = 0
            return True  # Momentary release must never send a second command.
        if path == STOP_PATH and value != 1:
            return False
        try:
            raw = (json.dumps({"id": str(uuid.uuid4()), "operation": "stop", "expires_at": time.time() + 10})
                   if path == STOP_PATH else value)
            self.model.request(raw)
            # Do not block D-Bus. tick() sends this once after any ongoing poll finishes.
            self.model.values[path] = value
            return True
        except (ValueError, TypeError):
            return False  # Reject invalid writes; retain prior request/result for correlation.

    def sync(self):
        for path, value in self.model.values.items():
            if self.service[path] != value:
                self.service[path] = value

    def tick(self):
        try:
            return self._tick()
        except Exception:
            # A failed GLib callback must not leave an apparently live frozen service.
            logging.exception("Bridge scheduler failed")
            self.loop.quit()
            return False

    def _tick(self):
        if self.future is not None and self.future.done():
            try:
                result = self.future.result()
                if self.job == "poll":
                    self.model.update(result)
                else:
                    self.model.finish("accepted")  # Firmware ack, not proof of physical completion.
            except Exception:
                if self.job == "command":
                    self.model.finish("unconfirmed")  # Never retry: command may have executed.
                self.model.disconnect("Transport or protocol error")
            self.future = None
            self.job = None
        self.model.tick()
        if self.future is None:
            if self.model.pending:
                if not self.model.values["/Connected"] or self.model.pending[2] <= time.time():
                    self.model.finish("cancelled")
                else:
                    self.job = "command"
                    self.future = self.pool.submit(self.transport.send, self.model.pending[1])
            elif time.monotonic() >= self.next_poll:
                self.job = "poll"
                self.next_poll = time.monotonic() + 1
                self.future = self.pool.submit(self.transport.poll)
        self.sync()
        return True

    def run(self):
        signal.signal(signal.SIGTERM, lambda *_: self.loop.quit())
        signal.signal(signal.SIGINT, lambda *_: self.loop.quit())
        try:
            self.loop.run()
        finally:
            self.pool.shutdown(wait=True, cancel_futures=True)


def main():
    parser = argparse.ArgumentParser(description="Brineomatic Venus OS 3.81 scaffold")
    parser.add_argument("--config", required=True)
    parser.add_argument("--preview", action="store_true", help="Print one simulated snapshot; no network or D-Bus")
    parser.add_argument("--demo-venus-docker", action="store_true",
                        help="Allow only simulation transport on venus-docker placeholder firmware")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    config = load_config(args.config)
    if args.preview:
        if config["mode"] != "simulation":
            parser.error("--preview requires simulation mode")
        model = Model(config.get("commands_enabled", False))
        model.update(SimulatedTransport().poll())
        print(json.dumps(model.values, indent=2, ensure_ascii=False))
        return
    if args.demo_venus_docker:
        check_demo(config)
    else:
        check_firmware()
    Runtime(config).run()


if __name__ == "__main__":
    main()
