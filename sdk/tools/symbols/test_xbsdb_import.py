#!/usr/bin/env python3
"""xbsdb_import.py: XbSymbolDatabase output into a config's [symbols] and [data]. Run: python3 test_xbsdb_import.py"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))

SIGNATURES = """[symbols]
D3DDevice_SetTexture = {sec = "D3D"}
D3DDevice_Swap = {sec = "D3D"}
D3D_MakeRequestedSpace = {sec = "D3D"}

[anchors]
Anchor = {}

[data]
D3D_pDevice = {in = "x"}
D3D_Streams = {in = "x"}
D3D_Device = {in = "x"}
"""

RUNTIME = """HLE_STDCALL(D3DDevice_SetBackBufferScale, 2) {
    return 0;
}
"""

FOUND = """XAPILIB__XapiInitProcess = 0x000d0000
D3D8__D3DDevice_SetBackBufferScale = 0x00167830
D3D8__D3DDevice_SetTexture = 0x00168460
D3D8__D3D_MakeRequestedSpace_8 = 0x0016ce50
D3D8__D3D_g_pDevice = 0x00175db8
D3D8__D3D_g_Stream = 0x00178e90
D3D8__D3DDevice_SetTexture = 0x00999999
"""

CONFIG = """[xbe]
path = "assets/default.xbe"

[symbols]
D3DDevice_SetTexture = 0
D3DDevice_Swap = 0

[data]
D3D_Device = 0x176260  # Direct3D_CreateDevice 0x16AE99
D3D_pDevice = 0

# a comment block that is not part of the table
[abi]
"""


class Import(unittest.TestCase):
    def run_import(self):
        with tempfile.TemporaryDirectory() as d:
            sdk = os.path.join(d, "sdk")
            paths = {"found.txt": os.path.join(d, "found.txt"), "game.toml": os.path.join(d, "game.toml"),
                     "signatures.toml": os.path.join(sdk, "xdk", "5849", "signatures.toml"),
                     "d3d8.cpp": os.path.join(sdk, "runtime", "xdk", "d3d8.cpp")}
            for n, text in (("found.txt", FOUND), ("signatures.toml", SIGNATURES), ("game.toml", CONFIG),
                            ("d3d8.cpp", RUNTIME)):
                os.makedirs(os.path.dirname(paths[n]), exist_ok=True)
                with open(paths[n], "w") as f:
                    f.write(text)
            r = subprocess.run([sys.executable, os.path.join(HERE, "xbsdb_import.py"), paths["found.txt"], sdk,
                                paths["game.toml"]], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(paths["game.toml"]) as f:
                return r.stdout, f.read()

    def test_found_functions_get_their_address(self):
        _, config = self.run_import()
        self.assertRegex(config, r"D3DDevice_SetTexture +=  ?0x168460")

    def test_a_function_only_the_runtime_replaces_is_found_too(self):
        _, config = self.run_import()
        self.assertIn("D3DDevice_SetBackBufferScale = 0x167830", config)

    def test_a_name_printed_twice_keeps_the_first(self):
        _, config = self.run_import()
        self.assertNotIn("0x999999", config)

    def test_names_spelled_differently_are_renamed(self):
        _, config = self.run_import()
        self.assertRegex(config, r"D3D_MakeRequestedSpace +=  ?0x16CE50")
        self.assertIn("D3D_pDevice = 0x175DB8", config)
        self.assertIn("D3D_Streams = 0x178E90", config)

    def test_names_not_found_stay_zero(self):
        _, config = self.run_import()
        self.assertRegex(config, r"D3DDevice_Swap +=  ?0\n")

    def test_a_hand_set_value_and_its_evidence_survive(self):
        _, config = self.run_import()
        self.assertIn("D3D_Device = 0x176260  # Direct3D_CreateDevice 0x16AE99", config)

    def test_names_the_sdk_does_not_implement_are_left_out(self):
        _, config = self.run_import()
        self.assertNotIn("XapiInitProcess", config)
        self.assertNotIn("Anchor", config)

    def test_other_tables_and_comments_are_untouched(self):
        _, config = self.run_import()
        self.assertIn('[xbe]\npath = "assets/default.xbe"', config)
        self.assertIn("# a comment block that is not part of the table\n[abi]", config)

    def test_the_count_says_how_many_names_were_found(self):
        out, _ = self.run_import()
        self.assertIn("6 of 7 names found", out)


if __name__ == "__main__":
    unittest.main()
