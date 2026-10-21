import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest


spec = importlib.util.spec_from_file_location("extract_game", Path(__file__).parents[1] / "tools/extract_game.py")
game = importlib.util.module_from_spec(spec)
spec.loader.exec_module(game)


def fixture(name=b"split.bin", size=700, next_cluster=0xffff):
    data = bytearray(8192 + 8 * 512)
    struct.pack_into("<4sIII", data, 0, b"FATX", 0xffffffff, 1, 1)
    struct.pack_into("<H", data, 4096 + 2, 0xffff)  # root
    struct.pack_into("<H", data, 4096 + 3 * 2, 5)  # fragmented file
    struct.pack_into("<H", data, 4096 + 5 * 2, next_cluster)
    data[8192] = len(name)
    data[8193] = 0x20
    data[8194:8194 + len(name)] = name
    struct.pack_into("<II", data, 8192 + 44, 3, size)
    data[8192 + 2 * 512:8192 + 3 * 512] = b"A" * 512
    data[8192 + 4 * 512:8192 + 5 * 512] = b"B" * 512
    return data


class FatxTests(unittest.TestCase):
    def test_fragmented_file_and_partial_last_cluster(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "assets"
            files = game.Fatx(fixture(), 32 * 512).extract(output)
            self.assertEqual((output / "split.bin").read_bytes(), b"A" * 512 + b"B" * 188)
            self.assertEqual(files[0]["size"], 700)

    def test_directory_path_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                game.Fatx(fixture(name=b"../escape"), 32 * 512).extract(Path(temp) / "assets")
            self.assertFalse((Path(temp) / "escape").exists())

    def test_cycle_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "cyclic"):
                game.Fatx(fixture(size=1300, next_cluster=3), 32 * 512).extract(Path(temp) / "assets")

    def test_early_chain_end_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "before its declared length"):
                game.Fatx(fixture(size=1300), 32 * 512).extract(Path(temp) / "assets")

    def test_wrong_security_dump_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "pic.data"
            path.write_bytes(bytes(80))
            with self.assertRaisesRegex(ValueError, "does not match"):
                game.security_data(path)


if __name__ == "__main__":
    unittest.main()
