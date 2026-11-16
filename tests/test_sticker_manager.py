from __future__ import annotations

import base64
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from unittest import mock
import urllib.error
import urllib.request

from PIL import Image

from manager.engines.stickers import native
from manager.engines.stickers.projects import PRESET_SHA256
from manager.providers import attach
from manager.server import make_server
from manager import storage
from manager.workspace import Workspace
from owner.stickers import images


ROOT = Path(__file__).resolve().parents[1]
PREFIX = "/api/providers/stickers"


def png(size=(24, 16), color=(10, 80, 180, 120)):
    output = io.BytesIO()
    Image.new("RGBA", size, color).save(output, "PNG")
    return output.getvalue()


class StickerManagerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.client = self.root / "client"
        self.client.mkdir()
        (self.client / "GOnline.exe").write_bytes(b"fixture-client")
        self.world = self.root / "world"
        self.world.mkdir()
        (self.world / "WorldServer.exe").write_bytes(b"fixture-server")
        self.workspace = Workspace(self.root / "manager", ROOT / "mods")
        self.client_host = self.workspace.add_host(self.client)["hosts"][0]
        public = self.workspace.add_host(self.world)
        self.world_host = next(host for host in public["hosts"] if host["role"] == "world")
        attach(self.workspace)
        self.provider = self.workspace.providers["stickers"]

    def tearDown(self):
        self.workspace.close()
        self.temporary.cleanup()

    def create(self, preset=None):
        return self.provider.request(
            "POST",
            "/api/stickers/projects/create",
            {
                "display_name": "测试贴纸",
                "description": "透明图片测试",
                "hosts": {
                    "client": self.client_host["id"],
                    "server": self.world_host["id"],
                },
                "presentation": {
                    "size": None,
                    "fit": "contain",
                    "padding_rgba": [0, 0, 0, 0],
                },
                "preset_id": preset,
            },
            {},
        )["project"]

    def test_public_preset_bytes_are_exact_and_project_copy_is_canonical(self):
        raw = (ROOT / "owner/stickers/examples/big-whale/image.png").read_bytes()
        self.assertEqual(PRESET_SHA256, hashlib.sha256(raw).hexdigest())
        state = self.provider.request("GET", "/api/stickers", {}, {})
        self.assertEqual("大肥鱼", state["presets"][0]["display_name"])
        self.assertEqual(PRESET_SHA256, state["presets"][0]["sha256"])
        returned = self.provider.request(
            "GET", "/api/stickers/presets/big-whale/image", {}, {}
        )
        self.assertEqual(raw, returned.data)
        project = self.create("big-whale")
        saved = self.provider.request(
            "GET", project["asset_url"], {}, {}
        ).data
        inspected = images.validate_canonical_png(saved)
        self.assertEqual((1052, 1495), (inspected["width"], inspected["height"]))
        self.assertNotEqual(PRESET_SHA256, hashlib.sha256(saved).hexdigest())

    def test_revision_safe_import_save_reopen_and_delete(self):
        project = self.create()
        imported = self.provider.request(
            "POST",
            "/api/stickers/projects/import",
            {
                "project_id": project["id"],
                "expected_revision": project["revision"],
                "filename": "sample.png",
                "image_base64": base64.b64encode(png()).decode("ascii"),
                "allow_animated_first_frame": False,
                "presentation": {
                    "size": {"width": 192, "height": 128},
                    "fit": "contain",
                    "padding_rgba": [0, 0, 0, 0],
                },
            },
            {},
        )["project"]
        self.assertTrue(imported["ready"])
        self.assertEqual({"width": 192, "height": 128}, imported["presentation"]["size"])
        self.assertEqual("PNG", imported["source"]["format"])
        asset = self.provider.request("GET", imported["asset_url"], {}, {}).data
        images.validate_canonical_png(asset)

        saved = self.provider.request(
            "POST",
            "/api/stickers/projects/save",
            {
                "project_id": imported["id"],
                "expected_revision": imported["revision"],
                "display_name": "重新命名",
                "description": "保留\n换行说明",
                "hosts": imported["hosts"],
                "presentation": imported["presentation"],
                "settings": {"future": {"enabled": True}},
            },
            {},
        )["project"]
        self.assertEqual("重新命名", saved["display_name"])
        self.assertEqual(
            {
                "future": {"enabled": True},
                "default_width": 2.0,
                "default_height": 2.0,
            },
            saved["settings"],
        )
        with self.assertRaises(storage.Conflict):
            self.provider.request(
                "POST",
                "/api/stickers/projects/save",
                {
                    "project_id": saved["id"],
                    "expected_revision": imported["revision"],
                    "display_name": saved["display_name"],
                    "description": saved["description"],
                    "hosts": saved["hosts"],
                    "presentation": saved["presentation"],
                    "settings": saved["settings"],
                },
                {},
            )
        reopened = self.provider.request(
            "POST", "/api/stickers/projects/get", {"project_id": saved["id"]}, {}
        )["project"]
        self.assertEqual(saved["revision"], reopened["revision"])
        deleted = self.provider.request(
            "POST",
            "/api/stickers/projects/delete",
            {"project_id": saved["id"], "expected_revision": saved["revision"]},
            {},
        )
        self.assertEqual(saved["id"], deleted["deleted"])
        self.assertEqual([], deleted["projects"])

    def test_only_existing_manager_host_ids_are_accepted(self):
        state = self.provider.request("GET", "/api/stickers", {}, {})
        self.assertEqual([self.client_host["id"]], [item["id"] for item in state["hosts"]["clients"]])
        self.assertEqual([self.world_host["id"]], [item["id"] for item in state["hosts"]["servers"]])
        with self.assertRaisesRegex(ValueError, "无效"):
            self.provider.request(
                "POST",
                "/api/stickers/projects/create",
                {
                    "display_name": "拒绝路径",
                    "description": "",
                    "hosts": {"client": "C:/arbitrary", "server": None},
                    "presentation": {"size": None, "fit": "contain", "padding_rgba": [0, 0, 0, 0]},
                    "preset_id": None,
                },
                {},
            )

    def test_reparse_project_and_asset_paths_are_refused_without_touching_targets(self):
        def make_directory_link(link, target):
            try:
                link.symlink_to(target, target_is_directory=True)
                return
            except OSError as symlink_error:
                if os.name != "nt":
                    raise unittest.SkipTest(
                        "directory symlinks are unavailable: " + str(symlink_error)
                    )
            result = subprocess.run(
                ["cmd", "/c", "mklink", "/J", str(link), str(target)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode:
                raise unittest.SkipTest("directory reparse points are unavailable")

        def remove_directory_link(link):
            if link.is_symlink():
                link.unlink()
            else:
                os.rmdir(link)

        project = self.create()
        project_directory = Path(self.workspace.root) / "data" / "stickers" / project["id"]
        outside_project = self.root / "outside-project"
        project_directory.rename(outside_project)
        try:
            make_directory_link(project_directory, outside_project)
            with self.assertRaisesRegex(ValueError, "\u94fe\u63a5|\u91cd\u89e3\u6790"):
                self.provider.request(
                    "POST", "/api/stickers/projects/get", {"project_id": project["id"]}, {}
                )
            self.assertTrue((outside_project / "project.json").is_file())
        finally:
            if project_directory.exists() or project_directory.is_symlink():
                remove_directory_link(project_directory)
            if outside_project.exists():
                outside_project.rename(project_directory)

        project = self.create("big-whale")
        assets = Path(self.workspace.root) / "data" / "stickers" / project["id"] / "assets"
        outside_assets = self.root / "outside-assets"
        assets.rename(outside_assets)
        expected = (outside_assets / (project["image"]["sha256"] + ".png")).read_bytes()
        try:
            make_directory_link(assets, outside_assets)
            with self.assertRaisesRegex(ValueError, "\u94fe\u63a5|\u91cd\u89e3\u6790"):
                self.provider.request(
                    "POST", "/api/stickers/projects/get", {"project_id": project["id"]}, {}
                )
            self.assertEqual(
                expected,
                (outside_assets / (project["image"]["sha256"] + ".png")).read_bytes(),
            )
        finally:
            if assets.exists() or assets.is_symlink():
                remove_directory_link(assets)
            if outside_assets.exists():
                outside_assets.rename(assets)

    def test_project_size_is_rejected_before_json_decode(self):
        project = self.create()
        path = Path(self.workspace.root) / "data" / "stickers" / project["id"] / "project.json"
        path.write_bytes(b"{" + b" " * (1024 * 1024))
        with self.assertRaisesRegex(ValueError, "\u5927\u5c0f"):
            self.provider.request(
                "POST", "/api/stickers/projects/get", {"project_id": project["id"]}, {}
            )

    def test_failed_project_update_preserves_a_preexisting_identical_orphan_asset(self):
        project = self.create()
        canonical = images.canonicalize_raster(png(), source_name="sample.png")
        directory = Path(self.workspace.root) / "data" / "stickers" / project["id"]
        orphan = directory / "assets" / (canonical["sha256"] + ".png")
        orphan.parent.mkdir()
        orphan.write_bytes(canonical["png"])
        original_atomic = storage.atomic

        def fail_project_write(path, raw):
            if Path(path).name == "project.json":
                raise OSError("simulated project write failure")
            return original_atomic(path, raw)

        with mock.patch.object(storage, "atomic", side_effect=fail_project_write):
            with self.assertRaisesRegex(OSError, "simulated"):
                self.provider.request(
                    "POST",
                    "/api/stickers/projects/import",
                    {
                        "project_id": project["id"],
                        "expected_revision": project["revision"],
                        "filename": "sample.png",
                        "image_base64": base64.b64encode(png()).decode("ascii"),
                        "allow_animated_first_frame": False,
                        "presentation": project["presentation"],
                    },
                    {},
                )
        self.assertEqual(canonical["png"], orphan.read_bytes())

    def test_first_placement_dimensions_are_bounded_and_world_hosts_only(self):
        battle = self.root / "battle"
        battle.mkdir()
        (battle / "BattleServer.exe").write_bytes(b"fixture-battle")
        self.workspace.add_host(battle)
        state = self.provider.request("GET", "/api/stickers", {}, {})
        self.assertEqual(["world"], [item["role"] for item in state["hosts"]["servers"]])

        project = self.create()
        self.assertEqual(2.0, project["settings"]["default_width"])
        self.assertEqual(2.0, project["settings"]["default_height"])
        saved = self.provider.request(
            "POST",
            "/api/stickers/projects/save",
            {
                "project_id": project["id"],
                "expected_revision": project["revision"],
                "display_name": project["display_name"],
                "description": project["description"],
                "hosts": project["hosts"],
                "presentation": project["presentation"],
                "settings": {"default_width": 0.2, "default_height": 8},
            },
            {},
        )["project"]
        self.assertEqual(0.2, saved["settings"]["default_width"])
        self.assertEqual(8.0, saved["settings"]["default_height"])
        with self.assertRaisesRegex(ValueError, "0.2"):
            self.provider.request(
                "POST",
                "/api/stickers/projects/save",
                {
                    "project_id": saved["id"],
                    "expected_revision": saved["revision"],
                    "display_name": saved["display_name"],
                    "description": saved["description"],
                    "hosts": saved["hosts"],
                    "presentation": saved["presentation"],
                    "settings": {"default_width": 0.19, "default_height": 2},
                },
                {},
            )

    def test_native_operations_fail_closed_for_unsupported_fixture_client(self):
        project = self.create("big-whale")
        before_client = sorted(path.relative_to(self.client) for path in self.client.rglob("*"))
        before_world = sorted(path.relative_to(self.world) for path in self.world.rglob("*"))
        for operation in ("preflight", "compile", "install", "server-plan", "apply-server"):
            with self.assertRaisesRegex(native.NativeUnavailable, "\u4e0d\u662f\u5f53\u524d|\u4e0d\u652f\u6301"):
                self.provider.request(
                    "POST",
                    "/api/stickers/native/" + operation,
                    {"project_id": project["id"], "expected_revision": project["revision"]},
                    {},
                )
        self.assertEqual(before_client, sorted(path.relative_to(self.client) for path in self.client.rglob("*")))
        self.assertEqual(before_world, sorted(path.relative_to(self.world) for path in self.world.rglob("*")))

    def test_apply_server_handler_passes_authenticated_project_and_selected_hosts(self):
        project = self.create("big-whale")
        receipt = {
            "message": "applied",
            "native_id": 8123456,
            "revision": "a" * 64,
            "restart_required": True,
            "files": [],
            "server_plan": {"schema": "test", "entries": [], "sql": []},
        }
        with mock.patch.object(native, "apply_server", return_value=receipt) as apply:
            result = self.provider.request(
                "POST",
                "/api/stickers/native/apply-server",
                {"project_id": project["id"], "expected_revision": project["revision"]},
                {},
            )
        self.assertEqual(receipt, result)
        native_project, image, client, server = apply.call_args.args
        self.assertEqual(project["id"], native_project["id"])
        self.assertEqual(project["image"]["sha256"], hashlib.sha256(image).hexdigest())
        self.assertEqual("client", client["role"])
        self.assertEqual("world", server["role"])

    def test_http_session_registration_and_panel_contract(self):
        server = make_server(self.workspace, 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = "http://127.0.0.1:" + str(server.server_port)
        try:
            with urllib.request.urlopen(base + "/api/state", timeout=10) as response:
                state = json.loads(response.read())
            self.assertIn("stickers", {item["id"] for item in state["panels"]})
            token = state["session_token"]
            request = urllib.request.Request(
                base + PREFIX + "/api/stickers/projects/list",
                json.dumps({}).encode(),
                headers={
                    "Origin": base,
                    "X-ModManager-Token": token,
                    "Content-Type": "application/json",
                },
            )
            with urllib.request.urlopen(request, timeout=10) as response:
                self.assertEqual([], json.loads(response.read())["projects"])
            denied = urllib.request.Request(
                base + PREFIX + "/api/stickers/projects/list",
                b"{}",
                headers={"Origin": base, "Content-Type": "application/json"},
            )
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(denied, timeout=10)
            self.assertEqual(403, error.exception.code)
            error.exception.close()
            with urllib.request.urlopen(base + "/panels/stickers.js", timeout=10) as response:
                panel = response.read().decode("utf-8")
            self.assertIn("中性盾牌轮廓", panel)
            self.assertIn("这不是原生游戏验收", panel)
            self.assertIn("allow_animated_first_frame", panel)
            self.assertIn("previewFrame.append(preview,previewEmpty)", panel)
            self.assertIn("updatePreviewBounds()", panel)
            self.assertIn("previewFrame.style.aspectRatio", panel)
            self.assertIn("clearNativeResult()", panel)
            self.assertIn("首次放置尺寸", panel)
            self.assertIn("renderNativeResult", panel)
            self.assertIn("请先保存当前项目，再运行原生操作", panel)
            self.assertIn("apply-server", panel)
            self.assertIn("下载编译包", panel)
            self.assertIn("delete visible.package_base64", panel)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_import_transport_accepts_16_mib_base64_only_on_exact_route(self):
        project = self.create()
        server = make_server(self.workspace, 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = "http://127.0.0.1:" + str(server.server_port)
        try:
            with urllib.request.urlopen(base + "/api/state", timeout=10) as response:
                token = json.loads(response.read())["session_token"]

            def rejected(path, body):
                raw = json.dumps(body, separators=(",", ":")).encode()
                request = urllib.request.Request(
                    base + path,
                    raw,
                    headers={
                        "Origin": base,
                        "X-ModManager-Token": token,
                        "Content-Type": "application/json",
                    },
                )
                with self.assertRaises(urllib.error.HTTPError) as caught:
                    urllib.request.urlopen(request, timeout=30)
                payload = json.loads(caught.exception.read())
                caught.exception.close()
                return payload["error"]

            maximum = base64.b64encode(b"\0" * images.MAX_INPUT_BYTES).decode("ascii")
            common = {
                "project_id": project["id"],
                "expected_revision": project["revision"],
                "filename": "maximum.png",
                "allow_animated_first_frame": False,
                "presentation": project["presentation"],
            }
            error = rejected(
                PREFIX + "/api/stickers/projects/import",
                {**common, "image_base64": maximum},
            )
            self.assertIn("corrupt", error)

            over = base64.b64encode(b"\0" * (images.MAX_INPUT_BYTES + 1)).decode("ascii")
            error = rejected(
                PREFIX + "/api/stickers/projects/import",
                {**common, "image_base64": over},
            )
            self.assertIn("16 MiB", error)

            connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=10)
            connection.putrequest("POST", "/api/providers/maps/api/maps/not-an-import")
            connection.putheader("Origin", base)
            connection.putheader("X-ModManager-Token", token)
            connection.putheader("Content-Type", "application/json")
            connection.putheader("Content-Length", str(16 * 1024 * 1024 + 1))
            connection.endheaders()
            response = connection.getresponse()
            self.assertEqual(400, response.status)
            error = json.loads(response.read())["error"]
            connection.close()
            self.assertIn("请求大小", error)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_packaging_includes_only_the_approved_public_preset_tree(self):
        spec = (ROOT / "manager/SDGOModManager.spec").read_text(encoding="utf-8")
        self.assertIn("owner/stickers/examples", spec)
        self.assertNotIn("C:\\jtmp", spec)


if __name__ == "__main__":
    unittest.main()
