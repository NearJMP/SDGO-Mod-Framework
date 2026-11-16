from __future__ import annotations

import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

from owner.stickers import compiler, deployment, images


class SimulatedTermination(BaseException):
    pass


class StickerDeploymentRecoveryTests(unittest.TestCase):
    def setUp(self):
        stream = io.BytesIO()
        Image.new("RGBA", (32, 48), (24, 96, 208, 255)).save(stream, "PNG")
        self.image = images.canonicalize_raster(
            stream.getvalue(), source_name="recovery-fixture.png"
        )["png"]
        self.first_entry = self._entry("a" * 32, 8_000_101, "恢复测试 A")
        self.first_resources = compiler.textures(self.image, self.first_entry["id"])
        self.first_files = compiler.compile_content(
            [self.first_entry], self.first_resources
        )
        self.second_entry = self._entry("b" * 32, 8_000_102, "恢复测试 B")
        self.second_resources = {
            **self.first_resources,
            **compiler.textures(self.image, self.second_entry["id"]),
        }
        self.second_files = compiler.compile_content(
            [self.first_entry, self.second_entry], self.second_resources
        )

    def _entry(self, owner: str, identity: int, name: str):
        project = {
            "id": owner,
            "display_name": name,
            "image": {
                "sha256": compiler.sha(self.image),
                "bytes": len(self.image),
                "width": 32,
                "height": 48,
            },
            "settings": {"default_width": 1.25, "default_height": 1.75},
        }
        return compiler.definition(project, self.image, identity)

    @staticmethod
    def _programme_binary(include_marker: bool = True) -> bytes:
        binary = bytearray(512)
        binary[:2] = b"MZ"
        struct.pack_into("<I", binary, 0x3C, 0x80)
        binary[0x80:0x84] = b"PE\0\0"
        struct.pack_into("<H", binary, 0x84, 0x14C)
        struct.pack_into("<H", binary, 0x94, 0xE0)
        struct.pack_into("<H", binary, 0x96, 0x2102)
        struct.pack_into("<H", binary, 0x98, 0x10B)
        if include_marker:
            binary[0x100:0x100 + len(deployment.ABI)] = deployment.ABI
        return bytes(binary)

    def _client(self, root: Path, *, include_marker: bool = True) -> Path:
        (root / "GOnline.exe").write_bytes(b"fixture")
        folder = root / "sdgo/mods/sticker_catalog"
        folder.mkdir(parents=True)
        (folder / "sticker_catalog.dll").write_bytes(
            self._programme_binary(include_marker)
        )
        (folder / "mod.ini").write_bytes(
            b"[mod]\nid=sticker_catalog\nversion=0.1.0\n"
            b"[settings]\nEnabled=1\n"
        )
        return folder

    @staticmethod
    def _generation(folder: Path) -> dict[str, bytes]:
        names = sorted(compiler.CONTENT_FILES) + ["manifest.json"]
        return {name: (folder / name).read_bytes() for name in names}

    @staticmethod
    def _install(root: Path, files: dict[str, bytes], revision):
        with patch("manager.updates.running", return_value=False):
            return deployment.install(root, files, revision)

    def _interrupt_publication(
        self, root: Path, folder: Path, revision: str, *, writes: int = 2
    ) -> None:
        real_atomic = deployment.storage.atomic
        published = 0

        def terminate(path, raw, **kwargs):
            nonlocal published
            path = Path(path)
            real_atomic(path, raw, **kwargs)
            if path.parent == folder:
                published += 1
                if published == writes:
                    raise SimulatedTermination("simulated process termination")

        with patch("manager.updates.running", return_value=False), patch.object(
            deployment.storage, "atomic", side_effect=terminate
        ):
            with self.assertRaises(SimulatedTermination):
                deployment.install(root, self.second_files, revision)

    def test_retry_recovers_exact_prior_generation_before_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root)
            first = self._install(root, self.first_files, None)
            before = self._generation(folder)
            self._interrupt_publication(root, folder, first["revision"])
            self.assertTrue((folder / deployment.JOURNAL).is_dir())
            self.assertNotEqual(before, self._generation(folder))

            real_recover = deployment.recover
            recovered = []

            def observe_recovery(client):
                result = real_recover(client)
                recovered.append(self._generation(folder))
                return result

            with patch("manager.updates.running", return_value=False), patch.object(
                deployment, "recover", side_effect=observe_recovery
            ):
                second = deployment.install(
                    root, self.second_files, first["revision"]
                )

            self.assertEqual([before], recovered)
            entries, resources, revision = deployment.installed(root)
            self.assertEqual([self.first_entry, self.second_entry], entries)
            self.assertEqual(self.second_resources, resources)
            self.assertEqual(second["revision"], revision)
            self.assertFalse((folder / deployment.JOURNAL).exists())

    def test_recovery_refuses_foreign_edit_and_preserves_materials(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root)
            first = self._install(root, self.first_files, None)
            self._interrupt_publication(root, folder, first["revision"], writes=1)
            target = folder / "catalog.bin"
            target.write_bytes(b"foreign authored edit")

            with patch("manager.updates.running", return_value=False):
                with self.assertRaisesRegex(ValueError, "未登记的作者修改"):
                    deployment.recover(root)

            self.assertEqual(b"foreign authored edit", target.read_bytes())
            self.assertTrue((folder / deployment.JOURNAL / "journal.json").is_file())

    def test_corrupted_receipt_revision_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root)
            state = self._install(root, self.first_files, None)
            receipt = folder / "manifest.json"
            document = json.loads(receipt.read_bytes())
            document["revision"] = (
                "0" * 64 if state["revision"] != "0" * 64 else "1" * 64
            )
            receipt.write_bytes(compiler.json_bytes(document))

            with self.assertRaisesRegex(ValueError, "版本校验失败"):
                deployment.installed(root)

    def test_programme_without_abi_marker_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root, include_marker=False)

            with self.assertRaisesRegex(ValueError, "kind-38/409B"):
                deployment.programme(folder)
            with patch("manager.updates.running", return_value=False):
                with self.assertRaisesRegex(ValueError, "kind-38/409B"):
                    deployment.install(root, self.first_files, None)
            self.assertFalse((folder / "catalog.bin").exists())
            self.assertFalse((folder / deployment.JOURNAL).exists())

    def test_cross_process_target_lease_blocks_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root)
            lock_root = root / "local-app-data"
            identity = "host:" + str(root.resolve()).casefold()
            script = (
                "import sys\n"
                "from manager.target_locks import lease\n"
                "with lease(sys.argv[1]):\n"
                " print('ready', flush=True)\n"
                " sys.stdin.read(1)\n"
            )
            environment = dict(os.environ)
            environment["LOCALAPPDATA"] = str(lock_root)
            process = subprocess.Popen(
                [sys.executable, "-B", "-c", script, identity],
                cwd=Path(__file__).resolve().parents[1],
                env=environment,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            try:
                self.assertEqual("ready", process.stdout.readline().strip())
                with patch.dict(os.environ, {"LOCALAPPDATA": str(lock_root)}), patch(
                    "manager.updates.running", return_value=False
                ):
                    with self.assertRaisesRegex(ValueError, "另一项变更"):
                        deployment.install(root, self.first_files, None)
                self.assertFalse((folder / "catalog.bin").exists())
            finally:
                if process.poll() is None:
                    process.stdin.write("x")
                    process.stdin.flush()
                try:
                    stdout, stderr = process.communicate(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    stdout, stderr = process.communicate(timeout=10)
                self.assertEqual(0, process.returncode, stdout + stderr)

    def test_backup_temp_interruption_restores_exact_generation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root)
            first = self._install(root, self.first_files, None)
            before = self._generation(folder)
            authored = folder / "authored.txt"
            similar_temp = folder / "catalog.bin.sticker-import-pending.keep"
            unrelated_temp = folder / ".unrelated.tmp"
            authored.write_bytes(b"private authored material")
            similar_temp.write_bytes(b"similar but unrelated")
            unrelated_temp.write_bytes(b"unrelated temp")
            journal = folder / deployment.JOURNAL
            target_name = sorted(compiler.CONTENT_FILES)[0]
            owned_temp = journal / (target_name + deployment.TEMP_SUFFIX)
            seen_temporary_names = []
            real_atomic = deployment.storage.atomic

            def terminate_backup(path, raw, **kwargs):
                path = Path(path)
                if path.parent == journal and path.name == target_name:
                    temporary_name = kwargs.get("temporary_name")
                    seen_temporary_names.append(temporary_name)
                    temporary_path = path.with_name(temporary_name)
                    with temporary_path.open("xb") as stream:
                        stream.write(raw[: max(1, len(raw) // 2)])
                        stream.flush()
                        os.fsync(stream.fileno())
                    raise SimulatedTermination("backup temp interrupted")
                return real_atomic(path, raw, **kwargs)

            with patch("manager.updates.running", return_value=False), patch.object(
                deployment.storage, "atomic", side_effect=terminate_backup
            ):
                with self.assertRaises(SimulatedTermination):
                    deployment.install(root, self.second_files, first["revision"])

            self.assertEqual([target_name + deployment.TEMP_SUFFIX], seen_temporary_names)
            self.assertTrue(owned_temp.is_file())
            self.assertFalse((journal / "journal.json").exists())
            self.assertEqual(before, self._generation(folder))

            with patch("manager.updates.running", return_value=False):
                self.assertTrue(deployment.recover(root))

            self.assertEqual(before, self._generation(folder))
            self.assertFalse(owned_temp.exists())
            self.assertFalse(journal.exists())
            self.assertEqual(b"private authored material", authored.read_bytes())
            self.assertEqual(b"similar but unrelated", similar_temp.read_bytes())
            self.assertEqual(b"unrelated temp", unrelated_temp.read_bytes())

    def test_root_publication_temp_interruption_restores_exact_generation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root)
            first = self._install(root, self.first_files, None)
            before = self._generation(folder)
            authored = folder / "authored.txt"
            similar_temp = folder / "definitions.json.sticker-import-pending.keep"
            unrelated_temp = folder / ".unrelated.tmp"
            authored.write_bytes(b"private authored material")
            similar_temp.write_bytes(b"similar but unrelated")
            unrelated_temp.write_bytes(b"unrelated temp")
            journal = folder / deployment.JOURNAL
            real_atomic = deployment.storage.atomic
            published = 0
            interrupted = {}

            def terminate_publication_temp(path, raw, **kwargs):
                nonlocal published
                path = Path(path)
                if path.parent != folder:
                    return real_atomic(path, raw, **kwargs)
                published += 1
                if published == 1:
                    return real_atomic(path, raw, **kwargs)
                temporary_name = kwargs.get("temporary_name")
                interrupted["target"] = path
                interrupted["temporary_name"] = temporary_name
                temporary_path = path.with_name(temporary_name)
                interrupted["temporary"] = temporary_path
                interrupted["partial"] = raw[: max(1, len(raw) // 2)]
                with temporary_path.open("xb") as stream:
                    stream.write(interrupted["partial"])
                    stream.flush()
                    os.fsync(stream.fileno())
                raise SimulatedTermination("root publication temp interrupted")

            with patch("manager.updates.running", return_value=False), patch.object(
                deployment.storage, "atomic", side_effect=terminate_publication_temp
            ):
                with self.assertRaises(SimulatedTermination):
                    deployment.install(root, self.second_files, first["revision"])

            target = interrupted["target"]
            owned_temp = interrupted["temporary"]
            self.assertEqual(
                target.name + deployment.TEMP_SUFFIX,
                interrupted["temporary_name"],
            )
            self.assertEqual(interrupted["partial"], owned_temp.read_bytes())
            self.assertTrue((journal / "journal.json").is_file())
            self.assertNotEqual(before, self._generation(folder))

            with patch("manager.updates.running", return_value=False):
                self.assertTrue(deployment.recover(root))

            self.assertEqual(before, self._generation(folder))
            self.assertFalse(owned_temp.exists())
            self.assertFalse(journal.exists())
            self.assertEqual(b"private authored material", authored.read_bytes())
            self.assertEqual(b"similar but unrelated", similar_temp.read_bytes())
            self.assertEqual(b"unrelated temp", unrelated_temp.read_bytes())

    def test_cleanup_interruption_remains_recoverable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = self._client(root)
            first = self._install(root, self.first_files, None)
            before = self._generation(folder)
            self._interrupt_publication(root, folder, first["revision"], writes=1)
            journal = folder / deployment.JOURNAL
            real_unlink = Path.unlink
            removed = 0

            def terminate_cleanup(path, *args, **kwargs):
                nonlocal removed
                result = real_unlink(path, *args, **kwargs)
                if Path(path).parent == journal:
                    removed += 1
                    if removed == 1:
                        raise SimulatedTermination("cleanup interrupted")
                return result

            with patch("manager.updates.running", return_value=False), patch.object(
                Path, "unlink", new=terminate_cleanup
            ):
                with self.assertRaises(SimulatedTermination):
                    deployment.recover(root)

            with patch("manager.updates.running", return_value=False):
                self.assertTrue(deployment.recover(root))
            self.assertEqual(before, self._generation(folder))
            self.assertFalse(journal.exists())


if __name__ == "__main__":
    unittest.main()