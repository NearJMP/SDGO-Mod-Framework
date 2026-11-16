from __future__ import annotations

import copy
import importlib.util
import json
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = ROOT / "tools" / "generate_sticker_localization.py"
SPEC = importlib.util.spec_from_file_location("generate_sticker_localization", GENERATOR_PATH)
assert SPEC and SPEC.loader
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class StickerLocalizationBankTest(unittest.TestCase):
    def setUp(self) -> None:
        self.names_value = json.loads(GENERATOR.DEFAULT_NAMES.read_text(encoding="utf-8"))
        self.shop_value = json.loads(GENERATOR.DEFAULT_SHOP.read_text(encoding="utf-8"))

    def test_reviewed_bank_is_complete_exact_and_reproducible(self) -> None:
        baseline, names = GENERATOR._validate_names(self.names_value)
        shop = GENERATOR._validate_shop(self.shop_value, baseline, names)
        self.assertEqual(len(names), 298)
        self.assertEqual(len(shop), 347)
        self.assertEqual(baseline["total_count"], 414)
        self.assertEqual(baseline["english_count"], 286)
        self.assertEqual(baseline["non_ascii_count"], 127)
        self.assertEqual(baseline["reviewed_non_ascii_count"], 12)
        self.assertEqual(baseline["excluded_incomplete_ascii_count"], 1)
        self.assertEqual(
            GENERATOR.DEFAULT_OUTPUT.read_text(encoding="utf-8"),
            GENERATOR._render(baseline, names, shop),
        )
        exact = {(entry["id"], entry["source"]): entry["target"] for entry in names}
        self.assertEqual(len(exact), 298)
        self.assertEqual(exact[(240183, "SIEG Sticker")], "SIEG 贴纸")
        self.assertEqual(
            exact[(240205, "BB Senshi Chousa Sticker")],
            "BB 战士 Chousa 贴纸",
        )
        self.assertEqual(
            exact[(240207, "Yuushuu Army Sticker")],
            "Yuushuu Army 贴纸",
        )
        self.assertEqual(exact[(240358, "삼국전 여포군 스티커")], "三国传吕布军贴纸")
        self.assertEqual(exact[(240361, "刀(Sword) Sticker")], "刀(Sword)贴纸")
        self.assertNotIn((240009, "즐 Sticker"), exact)
        self.assertNotIn((240335, " Sticker"), exact)
        self.assertNotIn("大肥鱼", {entry["source"] for entry in names})

    def test_shop_projection_is_exact_and_sticker_scoped(self) -> None:
        baseline, names = GENERATOR._validate_names(self.names_value)
        shop = GENERATOR._validate_shop(self.shop_value, baseline, names)
        translated_by_id = {entry["id"]: entry["source"] for entry in names}
        source_by_id: dict[int, str] = {}
        for entry in shop:
            self.assertEqual(entry["category_id"] // 100000, 17)
            self.assertTrue(240001 <= entry["sticker_id"] <= 240414)
            self.assertEqual(
                source_by_id.setdefault(entry["sticker_id"], entry["sticker_source"]),
                entry["sticker_source"],
            )
            if entry["sticker_id"] in translated_by_id:
                self.assertEqual(
                    translated_by_id[entry["sticker_id"]], entry["sticker_source"]
                )
            self.assertTrue(entry["target"])
        self.assertEqual(
            len({(entry["category_id"], entry["source"]) for entry in shop}), 347
        )
        exact = {(entry["category_id"], entry["source"]): entry for entry in shop}
        self.assertEqual(
            exact[(1720176, "삼국전 여포군 스티커")]["sticker_id"], 240358
        )
        self.assertEqual(exact[(1710005, "즐 스티커")]["target"], "“兹尔”贴纸（音译）")
        self.assertEqual(
            exact[(1720156, "龍 Sticker")],
            {
                "category_id": 1720156,
                "source": "龍 Sticker",
                "sticker_id": 240335,
                "sticker_source": " Sticker",
                "target": "龍贴纸",
            },
        )

    def test_all_shop_targets_reuse_exact_scoped_reviews(self) -> None:
        baseline, names = GENERATOR._validate_names(self.names_value)
        shop = GENERATOR._validate_shop(self.shop_value, baseline, names)
        text = (ROOT / "mods" / "shop_localization" / "zh-CN.ini").read_text(
            encoding="utf-8"
        )
        scoped: dict[tuple[int, str], str] = {}
        for block in re.split(r"(?m)^\[entry:[^\]]+\]\s*$", text):
            source = re.search(r"(?m)^Source=@category:(17\d{5}):(.*)$", block)
            target = re.search(r"(?m)^Text=(.*)$", block)
            if source and target:
                scoped[(int(source.group(1)), source.group(2))] = target.group(1)

        self.assertEqual(len(scoped), 347)
        self.assertEqual(len(shop), 347)
        for entry in shop:
            self.assertEqual(
                scoped[(entry["category_id"], entry["source"])], entry["target"]
            )

        phrases = {(entry["id"], entry["source"]) for entry in names}
        self.assertNotIn((240009, "즐 Sticker"), phrases)
        self.assertNotIn((240335, " Sticker"), phrases)

    def test_schema_and_source_drift_fail_closed(self) -> None:
        bad_names = copy.deepcopy(self.names_value)
        bad_names["entries"][0]["source"] += "\0suffix"
        with self.assertRaisesRegex(ValueError, "reviewed bounded stock name"):
            GENERATOR._validate_names(bad_names)

        bad_names = copy.deepcopy(self.names_value)
        bad_names["entries"][0]["source"] = " Sticker"
        with self.assertRaisesRegex(ValueError, "reviewed bounded stock name"):
            GENERATOR._validate_names(bad_names)

        bad_names = copy.deepcopy(self.names_value)
        bad_names["entries"][1]["id"] = bad_names["entries"][0]["id"]
        with self.assertRaisesRegex(ValueError, "invalid or unsorted ID"):
            GENERATOR._validate_names(bad_names)

        baseline, names = GENERATOR._validate_names(self.names_value)
        bad_shop = copy.deepcopy(self.shop_value)
        bad_shop["entries"][0]["sticker_source"] += "?"
        with self.assertRaisesRegex(ValueError, "exactly link"):
            GENERATOR._validate_shop(bad_shop, baseline, names)

        bad_shop = copy.deepcopy(self.shop_value)
        bad_shop["entries"][0]["target"] += "\0suffix"
        with self.assertRaisesRegex(ValueError, "target is not reviewed Chinese"):
            GENERATOR._validate_shop(bad_shop, baseline, names)


if __name__ == "__main__":
    unittest.main()
