import copy
import hashlib
import io
import json
import unittest

from PIL import Image, PngImagePlugin

from owner.stickers import images, protocol


def canonical(color=(20, 90, 160, 120), size=(18, 12)):
    source = io.BytesIO()
    Image.new("RGBA", size, color).save(source, "PNG")
    return images.canonicalize_raster(source.getvalue())


def package():
    alpha = canonical()
    opaque = canonical((230, 200, 80, 255), (12, 12))
    stickers = [
        protocol.make_sticker(
            "z-last",
            "后一个",
            opaque,
            settings={"category": "reaction"},
        ),
        protocol.make_sticker(
            "a-first",
            "第一个",
            alpha,
            presentation={
                "size": {"width": 40, "height": 40},
                "fit": "contain",
                "padding_rgba": [0, 0, 0, 0],
            },
            settings={"tags": ["public-example", "透明"]},
        ),
    ]
    manifest = protocol.make_manifest(
        stickers,
        default_settings={"preview": {"checkerboard": True}},
        settings={"owner_ui": {"editable": True, "zoom": 1.25}},
    )
    payloads = {
        "images/a-first.png": alpha["png"],
        "images/z-last.png": opaque["png"],
    }
    return manifest, payloads


class StickerProtocolTests(unittest.TestCase):
    def test_manifest_round_trip_is_utf8_deterministic_and_sorted(self):
        manifest, payloads = package()
        raw = protocol.encode_manifest(manifest)
        decoded = protocol.decode_manifest(raw)
        self.assertEqual(manifest, decoded)
        self.assertIn("第一个".encode("utf-8"), raw)
        self.assertEqual(["a-first", "z-last"], [x["id"] for x in decoded["stickers"]])
        self.assertEqual(raw, protocol.encode_manifest(decoded))
        receipt = protocol.validate_payloads(decoded, payloads)
        self.assertEqual(2, receipt["sticker_count"])
        self.assertEqual(sum(len(value) for value in payloads.values()), receipt["asset_bytes"])

    def test_default_presentation_keeps_source_size_transparency_and_is_editable(self):
        value = protocol.default_presentation()
        self.assertIsNone(value["size"])
        self.assertEqual("contain", value["fit"])
        self.assertEqual([0, 0, 0, 0], value["padding_rgba"])
        edited = protocol.normalize_presentation(
            {
                "size": {"width": 96, "height": 72},
                "fit": "contain",
                "padding_rgba": [255, 255, 255, 0],
            }
        )
        self.assertEqual({"width": 96, "height": 72}, edited["size"])
        with self.assertRaisesRegex(ValueError, "preserve aspect ratio"):
            protocol.normalize_presentation(
                {"size": None, "fit": "stretch", "padding_rgba": [0, 0, 0, 0]}
            )

    def test_general_settings_round_trip_without_defining_native_fields(self):
        manifest, _payloads = package()
        manifest["settings"] = {
            "ui": {
                "preview_scale": 1.5,
                "columns": 4,
                "labels": ["常用", "战斗"],
                "optional": None,
            }
        }
        normalized = protocol.validate_manifest(manifest)
        self.assertEqual(manifest["settings"], normalized["settings"])
        self.assertNotIn("native_id", normalized["stickers"][0])
        self.assertNotIn("record", normalized["stickers"][0])

    def test_image_descriptor_is_bound_to_canonical_bytes(self):
        result = canonical()
        descriptor = protocol.image_descriptor("sample", result)
        self.assertEqual("images/sample.png", descriptor["path"])
        self.assertEqual(hashlib.sha256(result["png"]).hexdigest(), descriptor["sha256"])
        changed = dict(result)
        changed["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "hash does not match"):
            protocol.image_descriptor("sample", changed)
        changed = dict(result)
        changed["width"] += 1
        with self.assertRaisesRegex(ValueError, "metadata does not match"):
            protocol.image_descriptor("sample", changed)

    def test_structural_fields_paths_hashes_and_payload_sets_are_strict(self):
        manifest, payloads = package()
        extra = copy.deepcopy(manifest)
        extra["unexpected"] = True
        with self.assertRaisesRegex(ValueError, "unknown fields"):
            protocol.validate_manifest(extra)
        traversal = copy.deepcopy(manifest)
        traversal["stickers"][0]["image"]["path"] = "../outside.png"
        with self.assertRaisesRegex(ValueError, "image path"):
            protocol.validate_manifest(traversal)
        url = copy.deepcopy(manifest)
        url["stickers"][0]["image"]["path"] = "https://example.invalid/a.png"
        with self.assertRaisesRegex(ValueError, "image path"):
            protocol.validate_manifest(url)
        excessive = copy.deepcopy(manifest)
        excessive["stickers"][0]["image"]["width"] = images.MAX_DIMENSION
        excessive["stickers"][0]["image"]["height"] = images.MAX_DIMENSION
        with self.assertRaisesRegex(ValueError, "pixel budget"):
            protocol.validate_manifest(excessive)
        with self.assertRaisesRegex(ValueError, "payload set mismatch"):
            protocol.validate_payloads(manifest, {**payloads, "images/extra.png": b"x"})
        corrupted = dict(payloads)
        corrupted["images/a-first.png"] += b"x"
        with self.assertRaisesRegex(ValueError, "byte count"):
            protocol.validate_payloads(manifest, corrupted)

    def test_payload_must_be_the_exact_canonical_rgba_png(self):
        picture = Image.new("RGBA", (8, 8), (1, 2, 3, 4))
        metadata = PngImagePlugin.PngInfo()
        metadata.add_text("note", "removed by canonicalization")
        output = io.BytesIO()
        picture.save(output, "PNG", pnginfo=metadata)
        raw = output.getvalue()
        fake = {
            "schema": protocol.SCHEMA,
            "defaults": {
                "presentation": protocol.default_presentation(),
                "settings": {},
            },
            "stickers": [
                {
                    "id": "sample",
                    "display_name": "Sample",
                    "image": {
                        "path": "images/sample.png",
                        "sha256": hashlib.sha256(raw).hexdigest(),
                        "bytes": len(raw),
                        "width": 8,
                        "height": 8,
                    },
                    "presentation": None,
                    "settings": {},
                }
            ],
            "settings": {},
        }
        with self.assertRaisesRegex(ValueError, "not canonical"):
            protocol.validate_payloads(fake, {"images/sample.png": raw})

    def test_json_reader_rejects_duplicate_keys_invalid_numbers_and_non_utf8(self):
        with self.assertRaisesRegex(ValueError, "duplicate key"):
            protocol.decode_manifest(b'{"schema":"one","schema":"two"}')
        with self.assertRaisesRegex(ValueError, "invalid number"):
            protocol.decode_manifest(b'{"value":NaN}')
        with self.assertRaisesRegex(ValueError, "UTF-8"):
            protocol.decode_manifest(b"\xff")

    def test_settings_are_bounded_json_only(self):
        manifest, _payloads = package()
        invalid = copy.deepcopy(manifest)
        invalid["settings"] = {"raw": b"not-json"}
        with self.assertRaisesRegex(ValueError, "non-JSON"):
            protocol.validate_manifest(invalid)
        too_deep = value = {}
        for index in range(protocol.MAX_SETTING_DEPTH + 2):
            value["next"] = {}
            value = value["next"]
        invalid = copy.deepcopy(manifest)
        invalid["settings"] = too_deep
        with self.assertRaisesRegex(ValueError, "depth"):
            protocol.validate_manifest(invalid)

    def test_manifest_encoding_refuses_nan_even_in_flexible_settings(self):
        manifest, _payloads = package()
        manifest["settings"] = {"bad": float("nan")}
        with self.assertRaisesRegex(ValueError, "finite"):
            protocol.encode_manifest(manifest)


if __name__ == "__main__":
    unittest.main()
