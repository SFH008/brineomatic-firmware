#!/usr/bin/env python3
"""Create an isolated demo manifest without broadening production version bounds."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gui-version", required=True, help="Exact development GUI version, e.g. v1.4.0")
    args = parser.parse_args()
    if not re.fullmatch(r"v\d+\.\d+\.\d+", args.gui_version):
        parser.error("Expected an exact vMAJOR.MINOR.PATCH version")
    manifest = json.loads((ROOT / "build/gui-v2/brineomatic.json").read_text())
    if manifest["minRequiredVersion"] != "v1.3.22" or manifest["maxRequiredVersion"] != "v1.3.22":
        raise ValueError("Source must be the pinned production manifest")
    manifest.update(version=manifest["version"] + "-demo", minRequiredVersion=args.gui_version,
                    maxRequiredVersion=args.gui_version)
    output = ROOT / "build/demo/brineomatic.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n")
    config = {"mode": "simulation", "commands_enabled": True, "device_instance": 0,
              "stale_after_seconds": 5}
    (output.parent / "config.json").write_text(json.dumps(config, indent=2) + "\n")
    print(output)


if __name__ == "__main__":
    main()
