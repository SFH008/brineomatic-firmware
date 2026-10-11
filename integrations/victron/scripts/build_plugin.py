#!/usr/bin/env python3
"""Package with Victron's matching compiler; never emit a fake resource manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path,
                        help="gui-v2-plugin-compiler.py from GUI-v2 1.3.22")
    parser.add_argument("--output", type=Path, default=ROOT / "build/gui-v2/brineomatic.json")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    compiler_bytes = compiler.read_bytes()
    blob = b"blob " + str(len(compiler_bytes)).encode() + b"\0" + compiler_bytes
    if hashlib.sha1(blob).hexdigest() != "e3d85e431f82694bf8781b9b1a57e099ddaafec4":
        parser.error("Compiler does not match the pinned GUI-v2 1.3.22 file")
    for program in ("lupdate", "lrelease", "rcc"):
        if not shutil.which(program):
            parser.error(program + " is missing; install matching Qt 6 host tools")
    with tempfile.TemporaryDirectory(prefix="brineomatic-plugin-") as temp:
        source = Path(temp) / "brineomatic"  # Compiler requires directory basename == plugin name.
        shutil.copytree(ROOT / "qml/brineomatic", source)
        subprocess.run(["python3", str(compiler), "--name", "brineomatic",
                        "--version", "0.2.0", "--min-required-version", "v1.3.22",
                        "--max-required-version", "v1.3.22",
                        "--settings", "PageBrineomatic.qml"], cwd=source, check=True)
        manifest_path = source / "brineomatic.json"
        manifest = json.loads(manifest_path.read_text())
        if not manifest.get("resource") or manifest.get("integrations") != [
                {"type": 1, "url": "qrc:/brineomatic/PageBrineomatic.qml"}]:
            raise ValueError("Compiler did not produce a valid settings plugin")
        if (manifest.get("minRequiredVersion"), manifest.get("maxRequiredVersion")) != ("v1.3.22", "v1.3.22"):
            raise ValueError("Compiler did not preserve version bounds")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(manifest_path, args.output)
        print(args.output)


if __name__ == "__main__":
    main()
