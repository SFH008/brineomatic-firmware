#!/bin/sh
# Called at boot to restore the service registration after Venus updates.
set -eu
task_app=/data/apps/available/brineomatic
task_version=$(sed -n '1p' /opt/victronenergy/version)
case "$task_version" in
    3.81|v3.81) ;;
    *) echo "Brineomatic: unsupported Venus version; service not enabled" >&2; exit 1 ;;
esac
task_service=/opt/victronenergy/service/brineomatic
if [ -L "$task_service" ]; then
    [ "$(readlink "$task_service")" = "$task_app/service" ] || exit 1
elif [ -e "$task_service" ]; then
    echo "Brineomatic: service path already occupied" >&2
    exit 1
else
    ln -s "$task_app/service" "$task_service"
fi
