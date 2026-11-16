from __future__ import annotations
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image
from owner.stickers import compiler, deployment, images
from owner.units.assets import render_portrait
from owner.native_archive import build_archive, read_archive_bytes


class StickerCompilerTests(unittest.TestCase):
    def setUp(self):
        output = io.BytesIO()
        Image.new("RGBA", (40, 80), (20, 80, 200, 255)).save(output, "PNG")
        self.image = images.canonicalize_raster(
            output.getvalue(), source_name="fixture.png"
        )["png"]
        self.project = {
            "id": "a" * 32,
            "display_name": "示例纹章",
            "image": {
                "sha256": compiler.sha(self.image),
                "bytes": len(self.image),
                "width": 40,
                "height": 80,
            },
            "settings": {"default_width": 1.2, "default_height": 1.8},
        }
        self.entry = compiler.definition(self.project, self.image, 8000001)
        self.resources = compiler.textures(self.image, 8000001)
        self.files = compiler.compile_content([self.entry], self.resources)

    def test_native_row_has_proven_fields_and_no_dimensions_in_reserved_dword(self):
        raw = compiler.record(8000001, "示例纹章")
        self.assertEqual(409, len(raw))
        self.assertEqual(8000001, struct.unpack_from("<I", raw)[0])
        self.assertEqual("示例纹章", raw[5:105].decode("utf-16-le").split("\0")[0])
        self.assertEqual(bytes([0, 0, 0, 1]), raw[105:109])
        self.assertEqual(bytes(4), raw[205:209])
        self.assertEqual(
            "sprs\\sdgo_stickers_8000001.spr",
            raw[109:205].decode("utf-16-le").split("\0")[0],
        )
        self.assertEqual(
            "txrs\\sdgo_stickers_8000001.txr",
            raw[209:409].decode("utf-16-le").split("\0")[0],
        )
        catalog = self.files["catalog.bin"]
        self.assertEqual(1075, len(catalog))
        self.assertEqual(
            (1.2, 1.8),
            tuple(round(f, 1) for f in struct.unpack_from("<ff", catalog, 44)),
        )
        item = compiler.category_record(8000001, self.entry["category_id"], "示例纹章")
        self.assertEqual(614, len(item))
        self.assertEqual(8000001, struct.unpack_from("<I", item, 105)[0])
        self.assertEqual(
            "sprs\\sdgo_stickers_8000001.spr",
            item[114:214].decode("utf-16-le").split("\0")[0],
        )

    def test_native_material_and_thumbnail_preserve_portrait_and_transparency(self):
        sprite, texture = compiler.paths(8000001)
        material = Image.open(io.BytesIO(self.resources[texture]))
        self.assertEqual((512, 512), material.size)
        self.assertEqual((128, 0, 384, 512), material.getbbox())
        self.assertEqual((20, 80, 200, 255), material.getpixel((256, 256)))
        thumbnail = Image.open(
            io.BytesIO(render_portrait(sprite, self.resources.__getitem__))
        )
        self.assertGreater(thumbnail.height, 0)
        self.assertGreater(thumbnail.width, 0)

    def test_invalid_id_name_size_and_image_fail_before_publication(self):
        for identity in (0, True, 10_000_000, -1):
            with self.assertRaises(ValueError):
                compiler.record(identity, "sample")
        for name in ("x" * 50, "bad\0name", "😀"):
            with self.assertRaises(ValueError):
                compiler.record(8000001, name)
        for value in (True, float("nan"), 0, 9):
            with self.assertRaises(ValueError):
                compiler.definition(
                    {**self.project, "settings": {"default_width": value}},
                    self.image,
                    8000001,
                )
        with self.assertRaises(ValueError):
            compiler.definition(self.project, self.image + b"bad", 8000001)
        with self.assertRaises(ValueError):
            compiler.compile_content(
                [self.entry], {**self.resources, "txrs/other.txr": b"x"}
            )

    def test_server_plan_is_ascii_definition_only_and_bound_to_client_record(self):
        plan = compiler.server_plan(self.entry)
        self.assertFalse(plan["player_grants"])
        self.assertFalse(plan["credentials_included"])
        self.assertEqual("sdgo-sticker-8000001", plan["rows"][0]["ItemName"])
        self.assertEqual(
            compiler.sha(compiler.record(8000001, "示例纹章")),
            plan["client_record_sha256"],
        )
        self.assertNotIn("sql", plan)

    def client(self, root):
        (root / "GOnline.exe").write_bytes(b"fixture")
        folder = root / "sdgo/mods/sticker_catalog"
        folder.mkdir(parents=True)
        binary = bytearray(512)
        binary[:2] = b"MZ"
        struct.pack_into("<I", binary, 0x3C, 128)
        binary[128:132] = b"PE\0\0"
        struct.pack_into("<H", binary, 132, 0x14C)
        binary[256 : 256 + len(deployment.ABI)] = deployment.ABI
        (folder / "sticker_catalog.dll").write_bytes(binary)
        (folder / "mod.ini").write_bytes(
            b"[mod]\nid=sticker_catalog\nversion=0.1.0\n[settings]\nEnabled=1\n"
        )
        return folder

    @patch("manager.updates.running", return_value=False)
    def test_install_merge_and_foreign_content_preservation(self, _):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self.client(root)
            (folder / "unrelated.txt").write_text("authored")
            state = deployment.install(root, self.files, None)
            entries, resources, revision = deployment.installed(root)
            self.assertEqual([self.entry], entries)
            self.assertEqual(self.resources, resources)
            self.assertEqual(state["revision"], revision)
            entry2 = {
                **self.entry,
                "id": 8000002,
                "category_id": 1790002,
                "owner": "b" * 32,
            }
            more = compiler.compile_content(
                entries + [entry2],
                {**resources, **compiler.textures(self.image, 8000002)},
            )
            deployment.install(root, more, revision)
            self.assertEqual(2, len(deployment.installed(root)[0]))
            self.assertEqual("authored", (folder / "unrelated.txt").read_text())
            with self.assertRaisesRegex(ValueError, "已改变"):
                deployment.install(root, self.files, revision)
            (folder / "definitions.json").write_bytes(b"{}")
            with self.assertRaisesRegex(ValueError, "未登记修改"):
                deployment.installed(root)

    @patch("manager.updates.running", return_value=False)
    def test_failed_write_restores_all_previous_content(self, _):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self.client(root)
            state = deployment.install(root, self.files, None)
            original = {p.name: p.read_bytes() for p in folder.iterdir()}
            real = deployment.storage.atomic
            calls = 0

            def fail_once(path, data, **kwargs):
                nonlocal calls
                if Path(path).parent == folder:
                    calls += 1
                    if calls == 3:
                        raise OSError("injected write failure")
                real(path, data, **kwargs)

            with patch.object(deployment.storage, "atomic", side_effect=fail_once):
                with self.assertRaisesRegex(OSError, "injected"):
                    deployment.install(root, self.files, state["revision"])
            self.assertEqual(
                original, {p.name: p.read_bytes() for p in folder.iterdir()}
            )

    def test_running_client_cannot_be_modified(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self.client(root)
            with patch("manager.updates.running", return_value=True):
                with self.assertRaisesRegex(ValueError, "关闭"):
                    deployment.install(root, self.files, None)
            self.assertFalse((folder / "catalog.bin").exists())

    @patch("manager.updates.running", return_value=False)
    def test_candidate_mismatch_is_refused_without_any_target_write(self, _):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self.client(root)
            before = {p.name: p.read_bytes() for p in folder.iterdir()}
            invalid = {**self.files, "catalog.bin": self.files["catalog.bin"] + b"bad"}
            with self.assertRaisesRegex(ValueError, "不匹配"):
                deployment.install(root, invalid, None)
            self.assertEqual(before, {p.name: p.read_bytes() for p in folder.iterdir()})

    def test_native_archive_decoder_enforces_declared_size_during_inflate(self):
        path = "txrs/sdgo_stickers_8000001.txr"
        index, archive, _ = build_archive(
            {path: b"x" * (1024 * 1024)}, compiler.allowed_resource
        )
        corrupt = bytearray(archive)
        struct.pack_into("<I", corrupt, 16, 0)
        with self.assertRaisesRegex(ValueError, "length|framing"):
            read_archive_bytes(
                index,
                bytes(corrupt),
                compiler.allowed_resource,
                max_archive_bytes=2 * 1024 * 1024,
                max_resource_bytes=2 * 1024 * 1024,
                max_total_bytes=2 * 1024 * 1024,
            )


if __name__ == "__main__":
    unittest.main()
