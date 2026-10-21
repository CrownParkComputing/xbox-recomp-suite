import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from audit_apk import audit, LIBRARIES


class ApkPayloadTests(unittest.TestCase):
    def make_apk(self, path, extras=(), machine=183):
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", header, 18, machine)
        with zipfile.ZipFile(path, "w") as apk:
            for name in ["AndroidManifest.xml", "classes.dex", "resources.arsc"]:
                apk.writestr(name, b"fixture")
            for name in LIBRARIES:
                apk.writestr("lib/arm64-v8a/" + name, header)
            for name in extras:
                apk.writestr(name, b"fixture")

    def test_application_only_payload_passes(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "test.apk"
            self.make_apk(path, ["res/drawable/icon.xml", "META-INF/CERT.SF"])
            with contextlib.redirect_stdout(io.StringIO()):
                audit(path)

    def test_rejects_game_files_and_unexpected_asset_payloads(self):
        for entry in ["assets/default.xbe", "assets/readme.txt", "res/raw/game.bin", "res/raw/security.pic", "game.cvm", "../escape"]:
            with self.subTest(entry=entry), tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "test.apk"
                self.make_apk(path, [entry])
                with self.assertRaises(ValueError):
                    audit(path)

    def test_rejects_other_native_architectures(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "test.apk"
            self.make_apk(path, machine=62)
            with self.assertRaises(ValueError):
                audit(path)


if __name__ == "__main__":
    unittest.main()
