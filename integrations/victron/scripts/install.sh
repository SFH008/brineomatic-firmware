#!/bin/sh
# Run on the Cerbo only. No firmware, stock QML, MQTT ACL, or relay changes.
set -eu
[ "$(id -u)" = 0 ] || { echo "Run as root on the Cerbo" >&2; exit 1; }
task_source=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_app=/data/apps/available/brineomatic
task_version=$(sed -n '1p' /opt/victronenergy/version)
case "$task_version" in 3.81|v3.81) ;; *) echo "Venus OS 3.81 required" >&2; exit 1 ;; esac
[ ! -e "$task_app" ] || { echo "Existing installation found; refusing to overwrite" >&2; exit 1; }
[ ! -e /data/apps/enabled/brineomatic ] && [ ! -L /data/apps/enabled/brineomatic ]
[ ! -e /opt/victronenergy/service/brineomatic ] && [ ! -L /opt/victronenergy/service/brineomatic ]
[ -w /opt/victronenergy/service ] || { echo "Service directory must be writable for registration" >&2; exit 1; }
[ ! -L /data/rc.local ] || { echo "rc.local symlink requires manual boot-hook setup" >&2; exit 1; }
[ -f "$task_source/build/gui-v2/brineomatic.json" ] || { echo "Build plugin first" >&2; exit 1; }
# Dependency checks happen before any installation mutations.
PYTHONPATH=/opt/victronenergy/dbus-systemcalc-py/ext/velib_python python3 -c 'import dbus, vedbus; from gi.repository import GLib'
python3 - "$task_source/build/gui-v2/brineomatic.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
assert m['name'] == 'brineomatic' and m['resource']
assert m['minRequiredVersion'] == m['maxRequiredVersion'] == 'v1.3.22'
PY
mkdir -p "$task_app/config" "$task_app/gui-v2" /data/apps/enabled
cp -R "$task_source/bridge" "$task_source/service" "$task_source/scripts" "$task_app/"
cp "$task_source/build/gui-v2/brineomatic.json" "$task_app/gui-v2/"
cp "$task_source/config.example.json" "$task_app/config/config.json"
cp "$task_source/README.md" "$task_app/"
chmod 700 "$task_app/config"
chmod 600 "$task_app/config/config.json"
chmod 755 "$task_app/service/run" "$task_app/service/log/run" "$task_app/scripts/enable-service.sh"
# Preserve any pre-existing rc.local; append only our exact boot hook.
python3 - <<'PY'
from pathlib import Path
p = Path('/data/rc.local')
text = p.read_text() if p.exists() else '#!/bin/sh\n'
hook = '/data/apps/available/brineomatic/scripts/enable-service.sh # brineomatic-scaffold'
lines = text.splitlines()
# Insert directly after the shebang, before any exits or conditional user code.
index = 1 if lines and lines[0].startswith('#!') else 0
lines.insert(index, hook)
p.write_text('\n'.join(lines) + '\n')
p.chmod(p.stat().st_mode | 0o100)
PY
ln -s "$task_app" /data/apps/enabled/brineomatic
"$task_app/scripts/enable-service.sh"
echo "Installed in simulation mode with commands disabled. See README for hardware validation."
