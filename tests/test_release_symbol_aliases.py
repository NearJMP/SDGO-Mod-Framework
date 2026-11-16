from pathlib import Path
import configparser
import tempfile
import unittest

from tools.build_release import validate_symbol_requirements


class ReleaseSymbolAliasesTests(unittest.TestCase):
    def test_global_and_fragment_address_drift_is_refused_before_packaging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            symbols = root / "client/sdgo/symbols"
            symbols.mkdir(parents=True)
            module = root / "client/sdgo/mods/example"
            module.mkdir(parents=True)
            host = "Example-00000001-00002000"
            header = "[host]\nimage=Example.exe\ntimestamp=1\nsize_of_image=8192\n"
            (symbols / (host + ".ini")).write_text(
                header + "[example.join]\nkind=site\nrva=0x100\n", "utf-8"
            )
            (module / "symbols.fragment.ini").write_text(
                header + "[example.join]\nkind=site\nrva=0x103\n", "utf-8"
            )
            (module / "mod.ini").write_text("[mod]\nrequires=example.join\n", "utf-8")
            catalog = {
                "mods": [
                    {
                        "id": "example",
                        "target": "client",
                        "hosts": [host],
                        "files": ["symbols.fragment.ini"],
                        "runtime_files": [],
                    }
                ]
            }
            with self.assertRaisesRegex(ValueError, "Conflicting global/Mod symbol"):
                validate_symbol_requirements(root, catalog)
            (symbols / (host + ".ini")).write_text(
                header + "[example.join]\nkind=site\nrva=259\n", "utf-8"
            )
            validate_symbol_requirements(root, catalog)

    def test_cinematic_join_contract_is_coherent(self):
        root = Path(__file__).resolve().parents[1]
        parsers = []
        for path in [
            root / "symbols/GOnline-54BE1568-00C97000.ini",
            root / "mods/unit_appearance_client/symbols.fragment.ini",
        ]:
            p = configparser.ConfigParser(interpolation=None)
            p.read(path, encoding="utf-8-sig")
            parsers.append(p)
        section = "rgb_paint.style_return_25"
        self.assertEqual(0x103B16, int(parsers[0][section]["rva"], 0))
        self.assertEqual(dict(parsers[0][section]), dict(parsers[1][section]))


if __name__ == "__main__":
    unittest.main()
