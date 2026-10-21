import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_game


class ArchivePreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.xbe = b"XBEH" + bytes(2306048 - 4)
        cls.digest = hashlib.sha256(cls.xbe).hexdigest()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "game.zip"
        self.output = self.root / "assets"

    def archive_with(self, extras=()):
        with zipfile.ZipFile(self.archive, "w", compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("outrun2.xbe", self.xbe)
            for name, contents in extras:
                z.writestr(name, contents)

    def test_extracts_exact_bytes_and_manifest(self):
        self.archive_with([("Media/example.txt", b"local fixture")])
        with patch.object(prepare_game, "XBE_SHA256", self.digest):
            manifest = prepare_game.extract(self.archive, self.output)
        self.assertEqual((self.output / "outrun2.xbe").read_bytes(), self.xbe)
        self.assertEqual((self.output / "Media/example.txt").read_bytes(), b"local fixture")
        self.assertEqual(manifest[1], {"path": "Media/example.txt", "size": 13,
                                     "sha256": hashlib.sha256(b"local fixture").hexdigest()})

    def test_existing_output_is_preserved(self):
        self.archive_with()
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("keep")
        with self.assertRaises(ValueError):
            prepare_game.extract(self.archive, self.output)
        self.assertEqual(marker.read_text(), "keep")

    def test_rejects_paths_outside_the_asset_tree(self):
        for name in ["../escape", "/absolute", "Media/../../escape", "Media\\escape", "C:/escape"]:
            with self.subTest(name=name):
                self.archive_with([(name, b"fixture")])
                with self.assertRaises(ValueError):
                    prepare_game.extract(self.archive, self.output)
                self.assertFalse(self.output.exists())

    def test_rejects_symlinks_and_case_collisions(self):
        link = zipfile.ZipInfo("Media/link")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        for extras in [[(link, b"../../escape")], [("Media/A", b"a"), ("media/a", b"b")]]:
            with self.subTest(extras=str(extras)):
                self.archive_with(extras)
                with self.assertRaises(ValueError):
                    prepare_game.extract(self.archive, self.output)
                self.assertFalse(self.output.exists())

    def test_rejects_other_executables_before_writing(self):
        self.archive_with()
        with self.assertRaises(ValueError):
            prepare_game.extract(self.archive, self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
