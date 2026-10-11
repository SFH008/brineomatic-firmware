import io
import json
from pathlib import Path
import tempfile
import time
import unittest
from concurrent.futures import Future
from unittest.mock import patch

from brineomatic_cerbo.core import Model, STOP_PATH
from brineomatic_cerbo.runtime import Runtime, check_firmware, check_demo, load_config
from brineomatic_cerbo.transport import HttpTransport, SimulatedTransport, NoRedirect


def snapshot(state="IDLE", **fields):
    return dict(brineomatic=True, status=state, **fields)


def request_json(**fields):
    return json.dumps(dict(expires_at=time.time() + 10, **fields))


class ModelTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.model = Model(True, clock=lambda: self.now)
        self.model.update(snapshot(product_flowrate=120, volume=42))

    def request(self, **fields):
        return self.model.request(request_json(id="test-id", operation="start", **fields))

    def test_default_is_read_only(self):
        model = Model()
        model.update(snapshot())
        with self.assertRaisesRegex(ValueError, "disabled"):
            model.request('{"id":"x","operation":"start"}')
        self.assertEqual(model.values["/SwitchableOutput/stop/Settings/ShowUIControl"], 0)

    def test_targets_preserve_firmware_units(self):
        self.assertEqual(self.request(volume_l=100), {"cmd": "start_watermaker", "volume": 100})
        self.model.finish("accepted")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.request(volume_l=100)
        command = self.model.request(request_json(id="duration", operation="start", duration_ms=60000))
        self.assertEqual(command["duration"], 60000)

    def test_state_gating(self):
        for state in ("RUNNING", "STARTUP", "PICKLED", "MANUAL", "STOPPING"):
            self.model.update(snapshot(state))
            with self.assertRaises(ValueError):
                self.request()

    def test_stop_all_active_modes(self):
        for state in ("RUNNING", "FLUSHING", "PICKLING", "DEPICKLING"):
            self.model.update(snapshot(state))
            command = self.model.request(request_json(id=state, operation="stop"))
            self.assertEqual(command, {"cmd": "stop_watermaker"})
            self.model.finish("accepted")

    def test_reject_bad_targets_and_fields(self):
        for fields in ({"volume_l": True}, {"volume_l": -1}, {"volume_l": float("nan")},
                       {"volume_l": 10 ** 400},
                       {"duration_ms": 1.5}, {"duration_ms": float("inf")},
                       {"duration_ms": 1, "volume_l": 1}, {"cmd": "restart"}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                self.request(**fields)
        for raw in ('[]', 'null', '{', '{}'):
            with self.assertRaises(ValueError):
                self.model.request(raw)

    def test_no_parallel_commands(self):
        self.request()
        with self.assertRaisesRegex(ValueError, "pending"):
            self.model.request(request_json(id="other", operation="flush"))

    def test_expired_and_unbounded_requests_rejected(self):
        for expiry in (None, time.time() - 1, time.time() + 1000):
            with self.assertRaisesRegex(ValueError, "expir"):
                self.model.request(json.dumps({"id":"expired", "operation":"start", "expires_at":expiry}))

    def test_stale_readings_are_invalidated_before_command(self):
        self.now = 6
        with self.assertRaisesRegex(ValueError, "disconnected"):
            self.request()
        self.assertIsNone(self.model.values["/GenericInput/volume/Value"])
        self.assertEqual(self.model.values["/SwitchableOutput/stop/Status"], 0x20)

    def test_missing_and_nonfinite_measurements(self):
        self.model.update(snapshot(volume=float("nan"), product_flowrate=True))
        self.assertIsNone(self.model.values["/GenericInput/volume/Value"])
        self.assertIsNone(self.model.values["/GenericInput/product_flowrate/Value"])
        self.assertEqual(self.model.values["/GenericInput/product_flowrate/Status"], 1)

    def test_switch_pane_contract(self):
        self.assertEqual(self.model.values["/GenericInput/volume/Settings/Unit"], "L")
        self.assertEqual(self.model.values["/GenericInput/volume/Settings/ShowUIInput"], 6)
        self.assertEqual(self.model.values["/SwitchableOutput/stop/Settings/Type"], 0)
        self.assertEqual(self.model.values["/SwitchableOutput/stop/Settings/ValidTypes"], 1)

    def test_countdown_conversion(self):
        self.model.update(snapshot(next_flush_countdown=18000000))
        self.assertEqual(self.model.values["/NextFlushSeconds"], 18000)
        self.model.update(snapshot(next_flush_countdown=0))
        self.assertIsNone(self.model.values["/NextFlushSeconds"])


class FakePool:
    def __init__(self):
        self.calls = []

    def submit(self, fn, *args):
        self.calls.append((fn, args))
        return Future()


class SchedulerTests(unittest.TestCase):
    def runtime(self):
        r = Runtime.__new__(Runtime)
        r.model = Model(True)
        r.model.update(snapshot("RUNNING"))
        r.service = dict(r.model.values)
        r.pool = FakePool()
        r.transport = SimulatedTransport()
        r.future = None
        r.job = None
        r.next_poll = float("inf")
        return r

    def test_momentary_release_does_not_stop_twice(self):
        r = self.runtime()
        self.assertTrue(r.on_write(STOP_PATH, 1))
        self.assertTrue(r.on_write(STOP_PATH, 0))
        r._tick()
        self.assertEqual(len(r.pool.calls), 1)
        self.assertEqual(r.pool.calls[0][1], ({"cmd": "stop_watermaker"},))
        r.future.set_result(None)
        r._tick()
        r._tick()
        self.assertEqual(len(r.pool.calls), 1)
        self.assertEqual(r.model.values[STOP_PATH], 0)

    def test_uncertain_command_is_not_replayed(self):
        r = self.runtime()
        r.on_write(STOP_PATH, 1)
        r._tick()
        r.future.set_exception(TimeoutError())
        r._tick()
        self.assertIsNone(r.model.pending)
        self.assertEqual(json.loads(r.model.values["/Commands/Result"])["status"], "unconfirmed")
        r.model.update(snapshot("RUNNING"))
        r._tick()
        self.assertEqual(len(r.pool.calls), 1)

    def test_queued_command_cancelled_on_poll_failure(self):
        r = self.runtime()
        r.job = "poll"
        r.future = Future()
        r.on_write(STOP_PATH, 1)
        r.future.set_exception(TimeoutError())
        r._tick()
        self.assertIsNone(r.model.pending)
        self.assertEqual(r.pool.calls, [])

    def test_expiry_rechecked_before_dispatch(self):
        r = self.runtime()
        r.on_write(STOP_PATH, 1)
        request_id, command, expiry = r.model.pending
        r.model.pending = (request_id, command, time.time() - 1)
        r._tick()
        self.assertIsNone(r.model.pending)
        self.assertEqual(r.pool.calls, [])


class TransportTests(unittest.TestCase):
    def test_http_credentials_in_body_and_ack_correlation(self):
        transport = HttpTransport("http://brineomatic.local", "guest", "private")
        with patch.object(transport.opener, "open", return_value=io.BytesIO(b'{"msgid":1,"status":"ok"}')) as call:
            transport.send({"cmd": "stop_watermaker"})
        request = call.call_args.args[0]
        self.assertNotIn("private", request.full_url)
        self.assertEqual(json.loads(request.data)["pass"], "private")
        with patch.object(transport.opener, "open", return_value=io.BytesIO(b'{"msgid":99,"status":"ok"}')):
            with self.assertRaises(ValueError):
                transport.send({"cmd": "stop_watermaker"})

    def test_reject_bad_origin_and_redirects(self):
        for endpoint in ("file:///tmp/x", "http://user:pass@localhost", "http://localhost/path", "http://localhost?x=1"):
            with self.assertRaises(ValueError):
                HttpTransport(endpoint, "a", "b")
        self.assertIsNone(NoRedirect().redirect_request(None, None, 302, "", {}, "http://other"))

    def test_firmware_rejection(self):
        transport = HttpTransport("http://localhost", "a", "b")
        with patch.object(transport.opener, "open", return_value=io.BytesIO(b'{"msgid":1,"status":"error","error":"private"}')):
            with self.assertRaisesRegex(ValueError, "Firmware rejected"):
                transport.send({"cmd": "start_watermaker"})

    def test_simulation_transition(self):
        transport = SimulatedTransport()
        transport.send({"cmd": "start_watermaker"})
        self.assertEqual(transport.poll()["status"], "RUNNING")
        transport.send({"cmd": "stop_watermaker"})
        self.assertEqual(transport.poll()["status"], "IDLE")


class CompatibilityTests(unittest.TestCase):
    def test_exact_firmware_guard(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "version"
            path.write_text("v3.81\nVictron Energy\n20261007000000\n")
            check_firmware(path)
            for version in ("v3.80", "v3.81~1", "v3.90"):
                path.write_text(version)
                with self.assertRaises(ValueError):
                    check_firmware(path)

    def test_demo_requires_placeholder_and_simulation(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "version"
            path.write_text("v99.99~99\nVictron Energy\n20210416100626\n")
            check_demo({"mode": "simulation"}, path)
            with self.assertRaises(ValueError):
                check_demo({"mode": "http"}, path)
            path.write_text("v3.81\n")
            with self.assertRaises(ValueError):
                check_demo({"mode": "simulation"}, path)

    def test_configuration_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "config.json"
            for config in ([1], {"mode":"other"}, {"mode":"simulation","commands_enabled":"false"},
                           {"mode":"simulation","stale_after_seconds":0},
                           {"mode":"simulation","device_instance":1}):
                path.write_text(json.dumps(config))
                with self.assertRaises(ValueError):
                    load_config(path)


if __name__ == "__main__":
    unittest.main()
