# Brineomatic × Victron Cerbo GX scaffold

Source-aligned with **Venus OS 3.81 / GUI-v2 1.3.22 / dbus-flashmq 26**.
This is an experimental third-party scaffold, not a certified or hardware-tested integration.
No stock firmware, Victron QML, switching hardware, or ESP32 firmware is modified.

## Included

- Python bridge with a dependency-free model and a replaceable HTTP transport.
- Simulation mode and a terminal preview that needs no Cerbo, Qt, D-Bus, or network.
- Native QML settings page under **Settings → Integrations → UI plugins → brineomatic**.
- Six `/GenericInput` value indicators for the stock Switch pane: product flow,
  product salinity, membrane/filter pressure, produced volume, motor temperature.
- One `/SwitchableOutput/stop` **momentary** control. No raw pump/valve toggles.
- Automatic Start/Flush/Stop requests on the native page, disabled by default.
- Exact firmware guard, pinned plugin compiler, supervised startup, bounded logs,
  install and disable scripts, and local tests.

The native page deliberately uses stock scrollable list controls for the first
compatibility proof. The earlier four-tile visual concept is a later UI milestone.
The simulator demonstrates states, not realistic fluid dynamics, target completion,
or safeguard behavior.

## Local preview and checks

Run from the repository root:

```sh
PYTHONPATH=integrations/victron/bridge python3 -m brineomatic_cerbo.runtime \
  --config integrations/victron/config.example.json --preview
PYTHONPATH=integrations/victron/bridge python3 -m unittest discover \
  -s integrations/victron/tests -v
```

Preview prints the D-Bus path snapshot; it neither connects to hardware nor registers
a D-Bus service. Setting `commands_enabled` in simulation exercises UI commands on
the simulator only. A running simulation service publishes `/Simulation=1` and the
native page clearly labels it. The stock Switch pane cannot display that banner:
do not confuse its simulated measurements with hardware telemetry.

## Build the native plugin

Use Qt **6** host tools compatible with the 3.81 GUI; `lupdate`, `lrelease`, and `rcc`
must be on PATH. Download/check out the official compiler at the pinned GUI commit:

https://github.com/victronenergy/gui-v2/blob/d680c66e78d320901475ffde071a907e8a256b32/tools/gui-v2-plugin-compiler.py

```sh
python3 integrations/victron/scripts/build_plugin.py \
  --compiler /path/to/gui-v2-plugin-compiler.py
```

The wrapper verifies the compiler's Git blob hash, uses the required `brineomatic`
directory basename, and checks the output resource and version limits. Output is
`integrations/victron/build/gui-v2/brineomatic.json`, ignored by Git.
Both inclusive GUI bounds are `v1.3.22`; newer GUI builds must be validated before
broadening those bounds. **The artifact is not precompiled in this scaffold.**
Do not replace it with a manifest containing an empty or fake resource.

## Install on a Cerbo for simulation testing

Copy the integration directory, including the compiled `build/gui-v2` artifact,
to the Cerbo. Root access is required. The installer checks Venus OS 3.81 and imports
`dbus`, GLib, and the installed `vedbus` before mutations. Standard Venus OS should
suffice; Node-RED and Venus OS Large are not requirements.

```sh
sh /path/to/victron/scripts/install.sh
```

The service registration directory `/opt/victronenergy/service` must be writable;
use the documented Venus root-access procedure if the root filesystem is read-only.
The installer does not remount it automatically. It refuses an existing installation
and symlinked `rc.local`, copies only this application, and inserts one identifiable
boot hook into the existing `/data/rc.local`. It does not replace user boot logic.
Installation is not transactional; if a disk/write error occurs, inspect the copied
application and registrations before retrying. Do not rerun over a partial install.

Layout:

```text
/data/apps/available/brineomatic/
  bridge/brineomatic_cerbo/
  config/config.json
  gui-v2/brineomatic.json
  service/run
  service/log/run
  scripts/enable-service.sh
```

UI activation uses `/data/apps/enabled/brineomatic`; bridge supervision uses
`/opt/victronenergy/service/brineomatic`. The boot hook restores the latter after
updates only on 3.81. The application does not modify the MQTT ACL or open ports.
Use the existing Remote Console security configuration.

To disable the scaffold and stop its service:

```sh
sh /path/to/victron/scripts/uninstall.sh
```

This removes only matching registration symlinks and the exact boot hook. Application
files, credentials, and logs are retained. Updating an existing installation is a
manual operation for this scaffold; there is no automatic updater.

## Connect to Brineomatic

First validate the simulated page and Switch pane locally and through Remote Console.
Enable the Brineomatic **HTTP JSON API** in its existing settings. The API is disabled
by default in YarrboardFramework; no firmware change should be needed.

Create `/data/apps/available/brineomatic/config/credentials.json` using
`credentials.example.json`, with the configured guest account, and `chmod 600` it.
Change `config/config.json` to `"mode": "http"`, set the ESP32 origin in `endpoint`,
and leave `commands_enabled` false for the first telemetry check. Restart the service
after editing configuration. Polling uses `POST /api/endpoint` with `get_update`,
credentials in the JSON body, and correlated `msgid`; URLs never contain credentials.
HTTP is plaintext on the LAN; for TLS use `https://...` and a trusted `ca_file`.
Certificate checks are not disabled and redirects/proxies are not followed.

Only after validating telemetry and normal firmware operation should a test setup
use `commands_enabled: true`. This enables the three native operations and exposes
the momentary Stop in the stock Switch pane. Target requests support either
`volume_l` (litres, max 100000) or `duration_ms` (whole milliseconds, max 86400000).
These are scaffold bounds, not watermaker operating recommendations.

## Data and command contract

- Service: `com.victronenergy.brineomatic`; DeviceInstance **0**, one device only.
- ProductId **0** is an unassigned placeholder, not a borrowed Victron identifier.
  The settings plugin does not depend on ProductId-based device-page integration.
- Raw JSON updates use L/h, litres, bar, °C, and ppm. Do not feed the MQTT telemetry
  into this adapter: that telemetry is converted to the user's configured units.
- `/GenericInput/<name>/Settings/Type=1`, `ValidTypes=2`, `ShowUIInput=6`
  (local and remote), with explicit unit strings. In particular litres are `L`,
  **not** the reserved `/Volume` keyword, which requires m³.
- Momentary Stop has `Type=0`, `ValidTypes=1`, `ShowUIControl=1` when enabled;
  otherwise it is hidden. `/Status=0x20` disables it when Stop is unavailable.
  Release writes (`State=0`) never send a machine command.
- Indicator settings are fixed/read-only in this scaffold. Output settings advertise
  `Adjustable=0`. Editable names, groups, type changes, and persistent UI preferences
  are a later extension; writes to those paths are not accepted.
- Missing/non-finite measurements are invalid (`None` → D-Bus invalid → MQTT null).
  Failure/staleness invalidates readings and disables commands.
- Only configured firmware readings are displayed; automatic sensor capability
  discovery is not implemented. Verify installed sensors before interpreting zeros.
- `/Commands/Request` is a JSON string with `id`, `operation`, `expires_at`, and an
  optional target. For example `operation=start`, `volume_l=100`. `expires_at` is
  Unix seconds, at most 30 seconds in the future; QML uses a 10-second lifetime.
  Browser and Cerbo clocks must be synchronized before enabling commands.
- `/Commands/Result` contains correlated `pending`, `accepted`, `unconfirmed`, or
  `cancelled`. Invalid D-Bus writes are rejected without replacing the previous result.
  Accepted means firmware acknowledgement, not proof that pumps have started.
- A single worker keeps blocking HTTP off the D-Bus/GLib thread. Commands are never
  retried, persisted, or queued through disconnects. Recent request IDs are deduplicated
  in memory; a timeout leaves the result unconfirmed until actual state is observed.
- The ESP32 remains authoritative for state transitions, safeguards, and sequencing.
  Stop is the existing normal stop operation, not an emergency-stop circuit.

## 3.81 source compatibility and remaining validation

Switch-pane indicators and switching changes arrived in **3.80** and are included in
3.81; 3.81 itself is a maintenance release. This scaffold uses the modern APIs without
impersonating switching hardware or adopting opportunity-load automation.

The exact 3.81 recipes specify GUI-v2 **1.3.22**, including browser plugin loading,
and dbus-flashmq **26**, including resource delivery over MQTT:

- [3.81 native GUI recipe](https://github.com/victronenergy/meta-victronenergy/blob/v3.81/meta-ve-software/recipes-ve/gui-v2_1.3.22.bb)
- [3.81 browser GUI recipe](https://github.com/victronenergy/meta-victronenergy/blob/v3.81/meta-ve-software/recipes-ve/gui-v2-webassembly_1.3.22.bb)
- [3.81 MQTT bridge recipe](https://github.com/victronenergy/meta-victronenergy/blob/v3.81/meta-venus/recipes-connectivity/flashmq/dbus-flashmq_26.bb)
- [SwitchableOutput and GenericInput API](https://github.com/victronenergy/venus/wiki/dbus#switch)
- [Plugin guide (experimental; browser-support wording conflicts with release source)](https://github.com/victronenergy/gui-v2/wiki/How-to-create-GUIv2-UI-Plugins)
- [Custom service lifecycle](https://github.com/victronenergy/venus/wiki/howto-add-a-driver-to-Venus)
- [Yarrboard HTTP protocol](https://github.com/hoeken/YarrboardFramework/blob/v3.0.3/src/controllers/HTTPController.cpp)

Before declaring hardware compatibility, check:

1. Plugin compiles with matching Qt tools and loads on GX Touch 50/70 in both themes.
2. The custom service is discovered; its stock Switch-pane indicators and Stop render.
3. Plugin resources and custom-service reads/writes work through LAN and VRM Remote Console.
4. Read-only users cannot issue commands through the native page or MQTT.
5. Real telemetry units match configured sensors, and acknowledgements match firmware.
6. Test command rejection, dropped connections, reboot, multiple clients, and no replay.
7. Confirm CPU/memory use and bounded logging under a sustained run.
8. Test disable, reinstall, and firmware-update behavior on a test Cerbo.

The plugin has been compiled using the pinned official compiler and Qt 6.8.3.
The compiled Qt resource was loaded on the host and both embedded QML files matched
their source bytes. The four telemetry cards rendered in Qt's offscreen runtime;
their component passed `qmllint`, and the page passed Qt's QML syntax parser.
The 21 dependency-free bridge tests also passed.

To repeat the resource/render check, install PySide6-Essentials 6.8.3 in an isolated
host environment, then run `python3 scripts/verify_plugin.py` from this directory.
It writes `build/gui-v2/metrics-preview.png`. This checks the standalone telemetry
component; it does not substitute for loading the complete page with Victron's
native module. The page uses stock ListButton controls, disables controls while
a command is pending, and shows acknowledgement status without raw protocol JSON.
English strings and fixed card colors are provisional; localization, both-theme
appearance and small-screen layouts still require refinement.

GX rendering, actual D-Bus publication, and VRM authorization remain unverified.
Keep hardware commands disabled until those checks are complete. WebSocket
transport, capability discovery, maintenance operations, and
historical graphs are later implementation milestones.

The production-control row now follows the generator interface: Start opens a
native modal for continuous, duration or litre-target production; Stop is visible
above telemetry while operating. Flush is separate. The dialog rechecks live state,
connection, pending command and user access before sending. Targets use existing
firmware command parameters; sensor publication and rendering are unchanged.

## Local development demo

The running local Venus Docker demo has also been validated separately with GUI
v1.4.0. See [DEMO.md](DEMO.md) for the temporary installation, screenshots,
simulation-only controls and cleanup. Its separate manifest does not broaden the
production v1.3.22 version bounds.

## Licensing

Original scaffold sources follow the repository's GPLv3 license. No Victron source
or binary is vendored here; the build uses the official compiler and installed runtime.
If distributing copied Victron code, retain its copyright/permission notices and
the required “This code is based on Victron Energy code” notice, and observe the
Victron Energy OS license's Victron-core-system condition. Do not present this as
an official Victron product. See [GUI license](https://github.com/victronenergy/gui-v2/blob/v1.3.22/LICENSE.txt).
If bundling `velib_python`, retain its [MIT notice](https://github.com/victronenergy/velib_python/blob/master/LICENSE).
