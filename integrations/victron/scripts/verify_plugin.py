#!/usr/bin/env python3
"""Host-only resource/render smoke check; does not load Victron's native module."""
import argparse
import base64
import json
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/gui-v2/brineomatic.json")
    args = parser.parse_args()
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PySide6.QtCore import QFile, QResource, QUrl, qVersion
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQuick import QQuickView

    if qVersion() != "6.8.3":
        parser.error("Use PySide6 / Qt 6.8.3 for this pinned host check")
    app = QGuiApplication([])
    manifest = json.loads(args.manifest.read_text())
    with tempfile.TemporaryDirectory(prefix="brineomatic-resource-") as directory:
        resource = Path(directory) / "brineomatic.rcc"
        resource.write_bytes(base64.b64decode(manifest["resource"], validate=True))
        if not QResource.registerResource(str(resource)):
            raise ValueError("Qt rejected the compiled resource")
        try:
            for source in (ROOT / "qml/brineomatic").glob("*.qml"):
                embedded = QFile(":/brineomatic/" + source.name)
                if not embedded.open(QFile.ReadOnly) or bytes(embedded.readAll()) != source.read_bytes():
                    raise ValueError("Resource does not match source: " + source.name)
                embedded.close()
            view = QQuickView()
            view.setResizeMode(QQuickView.SizeRootObjectToView)
            view.setSource(QUrl("qrc:/brineomatic/MetricsPanel.qml"))
            if view.errors():
                raise ValueError(str(view.errors()))
            view.resize(440, 180)
            view.rootObject().setProperty("readings", ["120.0 L/h", "180 ppm", "55.0 bar", "42.0 L"])
            view.show()
            for _ in range(10):
                app.processEvents()
            output = args.manifest.parent / "metrics-preview.png"
            if not view.grabWindow().save(str(output)):
                raise ValueError("Could not save preview")
            view.close()
            print("Embedded sources verified; telemetry cards rendered: " + str(output))
        finally:
            QResource.unregisterResource(str(resource))


if __name__ == "__main__":
    main()
