#!/bin/sh
# Disable only this scaffold; keep its files/config/logs for recovery.
set -eu
task_app=/data/apps/available/brineomatic
task_service=/opt/victronenergy/service/brineomatic
if [ -L "$task_service" ] && [ "$(readlink "$task_service")" = "$task_app/service" ]; then
    svc -dx /service/brineomatic
    rm "$task_service"
fi
if [ -L /data/apps/enabled/brineomatic ] && [ "$(readlink /data/apps/enabled/brineomatic)" = "$task_app" ]; then
    rm /data/apps/enabled/brineomatic
fi
python3 - <<'PY'
from pathlib import Path
p = Path('/data/rc.local')
hook = '/data/apps/available/brineomatic/scripts/enable-service.sh # brineomatic-scaffold'
if p.exists():
    lines = p.read_text().splitlines()
    p.write_text('\n'.join(line for line in lines if line != hook) + '\n')
PY
echo "Scaffold disabled; application files, credentials and logs retained."
