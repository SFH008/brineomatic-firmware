# Venus Docker demo

The local environment inspected on 2026-10-11 consists of `venus-docker` (recorded
D-Bus playback, firmware placeholder `v99.99~99`), `venus-gui-v2` (nginx), and
`venus-gui-v2-builder`. The running browser reports GUI version **v1.4.0**; its
source checkout is commit `3a76db4de93963726c3517c780160a9d35ed1b4c`.
This environment does not establish physical Cerbo / Venus OS 3.81 compatibility.

## Open the page

Refresh the existing Venus browser, then select **Settings → Integrations →
UI plugins → brineomatic**. The page marks itself as simulation. Start, Flush
and Stop operate an in-memory simulator; it has no HTTP transport or ESP32
connection. The bridge publishes the same D-Bus service and telemetry paths as
the production scaffold.

Start opens a generator-style modal with Continuous, Duration (1–1440 minutes)
and Litres (1–100000 L) choices. Duration is converted to milliseconds and the
litre target is sent as `volume_l`; the bridge maps these to the existing firmware
commands. Cancel sends nothing. Stop is shown prominently during any stoppable
operation; Flush remains a separate configured-cycle action. Sensors are unchanged.
The simulator does not model automatic target completion, so use Stop to end a
demo run; the physical firmware handles target completion.

The demo manifest is `build/demo/brineomatic.json`, bounded to GUI **v1.4.0**.
The production manifest remains `build/gui-v2/brineomatic.json`, bounded to
**v1.3.22**. Create the demo manifest and simulation configuration with:

```sh
python3 scripts/package_demo.py --gui-version v1.4.0
```

The demo runtime flag `--demo-venus-docker` accepts only placeholder firmware
`v99.99~99` and simulation transport. It rejects HTTP transport and real firmware.
Simulation commands are enabled in the demo configuration so the UI can be tested.

## Current temporary installation

- Bridge/config/log/PID: `/tmp/brineomatic-demo/` inside `venus-docker`.
- Plugin: `/data/apps/available/brineomatic/gui-v2/brineomatic.json`.
- Enabled link: `/data/apps/enabled/brineomatic`.
- Process: `python3 -m brineomatic_cerbo.runtime --config
  /tmp/brineomatic-demo/config.json --demo-venus-docker`.

No boot hook or supervised service was added. The bridge must be started again
after a container restart; the plugin files may remain until explicitly removed.
Do not run the production installer on this development container.

## Verified in this environment

- Actual GUI v1.4.0 reports loading the plugin's MQTT resource chunks successfully.
- Complete plugin page instantiates and displays connected simulated telemetry.
- Page Start and Stop controls drive `IDLE → RUNNING → IDLE` through MQTT/D-Bus.
- Page Flush and Stop controls drive `IDLE → FLUSHING → IDLE`.
- Start dialog Continuous sends no target, Duration sends 60 minutes as
  `duration_ms=3600000`, and Litres sends `volume_l=100`, verified on D-Bus.
- Cancel leaves the last request unchanged. The target dialog fits an 800×480
  browser viewport using the native Venus modal and spin box components.
- Command result displays Acknowledged; Start and Flush disable in active states.
- Production firmware parsing accepts the version file's first line followed by
  vendor/build metadata, while rejecting other firmware versions.

See `build/demo/controls-running.png`, `build/demo/start-duration.png` and
`build/demo/start-litres-small.png` for screenshots of the current controls.
The simulator models state transitions only; target completion, physical safeguards,
GX performance, VRM authorization, and reboot/update behavior remain unverified.

## Remove the temporary demo

From the host, run `python3 scripts/remove_demo.py --container venus-docker`.
The cleanup checks the placeholder firmware, demo manifest, symlink target and
process command line before removing the owned demo paths. It does not change
the recorded playback, MQTT configuration or GUI web server.
