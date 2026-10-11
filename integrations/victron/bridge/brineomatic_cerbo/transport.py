"""Initial HTTP adapter; bounded requests with no retries of commands."""

import json
import ssl
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, HTTPHandler, HTTPSHandler, ProxyHandler, HTTPRedirectHandler


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None  # Never forward credentials or replay a command to another URL.


class HttpTransport:
    def __init__(self, endpoint, user, password, timeout=2, ca_file=None):
        url = urlsplit(endpoint)
        if url.scheme not in {"http", "https"} or not url.hostname or url.username or url.password or url.query or url.fragment or url.path not in {"", "/"}:
            raise ValueError("endpoint must be an http(s) origin without embedded credentials")
        self.endpoint = endpoint.rstrip("/")
        self.credentials = {"user": user, "pass": password}
        self.timeout = timeout
        self.msgid = 0
        self.opener = build_opener(ProxyHandler({}), NoRedirect(), HTTPHandler(),
                                   HTTPSHandler(context=ssl.create_default_context(cafile=ca_file)))

    def call(self, command):
        self.msgid += 1
        body = dict(command, **self.credentials, msgid=self.msgid)
        request = Request(self.endpoint + "/api/endpoint", data=json.dumps(body).encode(),
                          headers={"Content-Type": "application/json"}, method="POST")
        with self.opener.open(request, timeout=self.timeout) as response:
            raw = response.read(262145)
        if len(raw) > 262144:
            raise ValueError("Response is too large")
        payload = json.loads(raw)
        if not isinstance(payload, dict) or payload.get("msgid") != self.msgid:
            raise ValueError("Invalid command acknowledgement")
        if payload.get("status") == "error":
            # Do not publish raw responses: they might contain sensitive configuration.
            raise ValueError("Firmware rejected request")
        return payload

    def poll(self):
        return self.call({"cmd": "get_update"})

    def send(self, command):
        response = self.call(command)
        if response.get("status") != "ok":
            raise ValueError("Missing success acknowledgement")


class SimulatedTransport:
    def __init__(self):
        self.state = "IDLE"
        self.volume = 0.0

    def poll(self):
        if self.state == "RUNNING":
            self.volume += 1 / 30
        return {"brineomatic": True, "status": self.state, "run_result": "SUCCESS",
                "product_flowrate": 120 if self.state == "RUNNING" else 0,
                "product_salinity": 180, "membrane_pressure": 55 if self.state == "RUNNING" else 0,
                "filter_pressure": 2.1, "volume": round(self.volume, 2),
                "motor_temperature": 34, "next_flush_countdown": 18000000}

    def send(self, command):
        self.state = {"start_watermaker": "RUNNING", "flush_watermaker": "FLUSHING",
                      "stop_watermaker": "IDLE"}[command["cmd"]]
