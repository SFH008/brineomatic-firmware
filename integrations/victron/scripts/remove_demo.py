#!/usr/bin/env python3
"""Remove only the owned, temporary venus-docker demonstration."""
import argparse
import subprocess

CLEANUP = r'''
import json, os, pathlib, shutil, signal
if pathlib.Path('/opt/victronenergy/version').read_text().splitlines()[0] != 'v99.99~99':
    raise SystemExit('Refusing cleanup outside the demo firmware')
root = pathlib.Path('/tmp/brineomatic-demo')
app = pathlib.Path('/data/apps/available/brineomatic')
link = pathlib.Path('/data/apps/enabled/brineomatic')
manifest = json.loads((app / 'gui-v2/brineomatic.json').read_text())
if manifest.get('version') not in {'0.1.0-demo', '0.2.0-demo'} or manifest.get('name') != 'brineomatic':
    raise SystemExit('Refusing to remove a non-demo plugin')
if not link.is_symlink() or os.readlink(link) != str(app):
    raise SystemExit('Enabled plugin is not the owned demo link')
if root.is_symlink() or app.is_symlink():
    raise SystemExit('Refusing unexpected directory symlinks')
pid = int((root / 'bridge.pid').read_text())
cmdline = pathlib.Path('/proc') / str(pid) / 'cmdline'
if cmdline.exists():
    arguments = cmdline.read_bytes().split(b'\0')
    if b'brineomatic_cerbo.runtime' not in arguments or b'--demo-venus-docker' not in arguments:
        raise SystemExit('PID no longer belongs to the demo bridge')
    os.kill(pid, signal.SIGTERM)
link.unlink()
shutil.rmtree(app)
shutil.rmtree(root)
print('Temporary Brineomatic demo removed')
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--container", default="venus-docker")
    args = parser.parse_args()
    subprocess.run(["docker", "exec", args.container, "python3", "-c", CLEANUP], check=True)


if __name__ == "__main__":
    main()
