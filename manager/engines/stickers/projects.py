"""Revision-safe Manager storage for source-neutral sticker projects."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import stat

from manager import storage
from owner.stickers import images, protocol


PROJECT_SCHEMA = "sdgo.sticker-project/1"
MAX_PROJECTS = 512
MAX_PROJECT_BYTES = 1024 * 1024
MAX_NAME = 100
MAX_DESCRIPTION = 4000
_PROJECT_ID = re.compile(r"[0-9a-f]{32}")
_SHA256 = re.compile(r"[0-9a-f]{64}")
PRESET_ID = "big-whale"
PRESET_NAME = "大肥鱼"
PRESET_SHA256 = "c437a66448168db48cece7e4b5c05059d303c8529fb72c4f33df6f7b9ed2947b"


def _unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("贴纸项目包含重复字段")
        value[key] = item
    return value


def _text(value, label, maximum, *, empty=False, multiline=False):
    if not isinstance(value, str) or len(value) > maximum or (not empty and not value.strip()):
        raise ValueError(label + "无效")
    value = value.strip() if not multiline else value.strip(" \t\r\n")
    for character in value:
        code = ord(character)
        if code < 32 and not (multiline and character in "\r\n\t"):
            raise ValueError(label + "包含无效字符")
    return value


def _project_id(value):
    if not isinstance(value, str) or not _PROJECT_ID.fullmatch(value):
        raise ValueError("贴纸项目标识无效")
    return value


def _host_ref(value, label):
    if value is None:
        return None
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", value):
        raise ValueError(label + "无效")
    return value


def _hosts(value):
    if not isinstance(value, dict) or set(value) != {"client", "server"}:
        raise ValueError("贴纸项目宿主选择无效")
    return {
        "client": _host_ref(value["client"], "客户端选择"),
        "server": _host_ref(value["server"], "服务器选择"),
    }


def _placement(value, label):
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not 0.2 <= float(value) <= 8.0
    ):
        raise ValueError(label + "必须是 0.2–8.0 的有限数字")
    return float(value)


def _sticker_settings(value):
    normalized = protocol.normalize_settings(value, "project settings")
    normalized["default_width"] = _placement(
        normalized.get("default_width", 2.0), "首次放置宽度"
    )
    normalized["default_height"] = _placement(
        normalized.get("default_height", 2.0), "首次放置高度"
    )
    return normalized


def _normalize_project(value):
    fields = {
        "schema", "id", "display_name", "description", "hosts",
        "presentation", "settings", "image", "source", "sizing",
    }
    if not isinstance(value, dict) or set(value) != fields:
        raise ValueError("贴纸项目结构无效")
    if value["schema"] != PROJECT_SCHEMA:
        raise ValueError("贴纸项目版本无效")
    identity = _project_id(value["id"])
    name = _text(value["display_name"], "贴纸名称", MAX_NAME)
    description = _text(
        value["description"], "贴纸说明", MAX_DESCRIPTION, empty=True, multiline=True
    )
    hosts = _hosts(value["hosts"])
    presentation = protocol.normalize_presentation(value["presentation"])
    settings = _sticker_settings(value["settings"])
    image = value["image"]
    source = value["source"]
    sizing = value["sizing"]
    if image is None:
        if source is not None or sizing is not None:
            raise ValueError("未导入图片的贴纸项目不能包含图片元数据")
    else:
        manifest = protocol.validate_manifest({
            "schema": protocol.SCHEMA,
            "defaults": {
                "presentation": protocol.default_presentation(),
                "settings": {},
            },
            "stickers": [{
                "id": identity,
                "display_name": name,
                "image": image,
                "presentation": presentation,
                "settings": settings,
            }],
            "settings": {},
        })
        image = manifest["stickers"][0]["image"]
        source = protocol.normalize_settings(source, "source metadata")
        sizing = protocol.normalize_settings(sizing, "sizing metadata")
    return {
        "schema": PROJECT_SCHEMA,
        "id": identity,
        "display_name": name,
        "description": description,
        "hosts": hosts,
        "presentation": presentation,
        "settings": settings,
        "image": image,
        "source": source,
        "sizing": sizing,
    }


def _read_json(raw):
    if not raw or len(raw) > MAX_PROJECT_BYTES:
        raise ValueError("贴纸项目文件大小无效")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_object,
                           parse_constant=lambda item: (_ for _ in ()).throw(
                               ValueError("贴纸项目数字无效")
                           ))
    except UnicodeDecodeError as error:
        raise ValueError("贴纸项目必须使用 UTF-8") from error
    except json.JSONDecodeError as error:
        raise ValueError("贴纸项目 JSON 无效") from error
    return _normalize_project(value)


def _project_bytes(value):
    normalized = _normalize_project(value)
    raw = storage.json_bytes(normalized)
    if len(raw) > MAX_PROJECT_BYTES:
        raise ValueError("贴纸项目文件过大")
    return normalized, raw


def _canonical_metadata(result):
    return {
        "path": "images/placeholder.png",
        "sha256": result["sha256"],
        "bytes": result["byte_count"],
        "width": result["width"],
        "height": result["height"],
    }


def _lexical_absolute(path):
    return Path(os.path.abspath(os.fspath(path)))


def _is_reparse(path):
    try:
        value = Path(path).lstat()
    except FileNotFoundError:
        return False
    except OSError as error:
        raise ValueError("无法检查贴纸项目路径") from error
    return bool(
        stat.S_ISLNK(value.st_mode)
        or getattr(value, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    )


class StickerProjects:
    def __init__(self, workspace):
        self.workspace = workspace
        self.workspace_root = _lexical_absolute(workspace.root)
        self.root = _lexical_absolute(self.workspace_root / "data" / "stickers")
        if not self.root.is_relative_to(self.workspace_root):
            raise ValueError("贴纸工作区路径越界")
        self._guard_path(self.root)

    def _guard_path(self, path):
        path = _lexical_absolute(path)
        if not path.is_relative_to(self.workspace_root):
            raise ValueError("贴纸项目路径越界")
        cursor = self.workspace_root
        if _is_reparse(cursor):
            raise ValueError("贴纸项目路径不能经过链接或重解析点")
        for part in path.relative_to(self.workspace_root).parts:
            cursor = cursor / part
            try:
                cursor.lstat()
            except FileNotFoundError:
                break
            except OSError as error:
                raise ValueError("无法检查贴纸项目路径") from error
            if _is_reparse(cursor):
                raise ValueError("贴纸项目路径不能经过链接或重解析点")
        return path

    @staticmethod
    def preset_path():
        return Path(__file__).resolve().parents[3] / "owner" / "stickers" / "examples" / PRESET_ID / "image.png"

    def preset_bytes(self):
        path = self.preset_path()
        if path.is_symlink() or not path.is_file():
            raise ValueError("内置大肥鱼预设不可用")
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != PRESET_SHA256:
            raise ValueError("内置大肥鱼预设校验失败")
        images.inspect_raster(raw, source_name=path.name)
        return raw

    def presets(self):
        raw = self.preset_bytes()
        inspected = images.inspect_raster(raw, source_name="big-whale.png")
        return [{
            "id": PRESET_ID,
            "display_name": PRESET_NAME,
            "description": "公开内置示例，可直接建立项目后继续编辑。",
            "sha256": PRESET_SHA256,
            "bytes": len(raw),
            "width": inspected["width"],
            "height": inspected["height"],
            "has_transparency": inspected["has_transparency"],
            "asset_url": "/api/stickers/presets/big-whale/image",
        }]

    def host_choices(self):
        clients = []
        servers = []
        labels = {"world": "世界服务器", "battle": "对战服务器", "agent": "中继服务器"}
        for host in self.workspace.settings.get("hosts", []):
            identity = host.get("id")
            role = host.get("role")
            if not isinstance(identity, str):
                continue
            item = {
                "id": identity,
                "name": host.get("name") or identity,
                "role": role,
            }
            if role == "client":
                clients.append(item)
            elif role == "world":
                item["role_name"] = labels[role]
                servers.append(item)
        return {"clients": clients, "servers": servers}

    def validate_hosts(self, value):
        normalized = _hosts(value)
        choices = self.host_choices()
        client_ids = {item["id"] for item in choices["clients"]}
        server_ids = {item["id"] for item in choices["servers"]}
        if normalized["client"] is not None and normalized["client"] not in client_ids:
            raise ValueError("请选择当前 Manager 已添加的客户端")
        if normalized["server"] is not None and normalized["server"] not in server_ids:
            raise ValueError("请选择当前 Manager 已添加的服务器")
        return normalized

    def _directory(self, identity, *, existing=False):
        identity = _project_id(identity)
        root = self._guard_path(self.root)
        directory = self._guard_path(root / identity)
        if existing and not directory.is_dir():
            raise ValueError("贴纸项目不存在")
        return directory

    def _read(self, identity, *, validate_asset=False):
        directory = self._directory(identity, existing=True)
        path = self._guard_path(directory / "project.json")
        if not path.is_file():
            raise ValueError("贴纸项目文件不存在")
        with path.open("rb") as stream:
            size = os.fstat(stream.fileno()).st_size
            if not 1 <= size <= MAX_PROJECT_BYTES:
                raise ValueError("贴纸项目文件大小无效")
            raw = stream.read(MAX_PROJECT_BYTES + 1)
        if len(raw) != size:
            raise ValueError("贴纸项目文件读取不完整")
        project = _read_json(raw)
        if project["id"] != identity:
            raise ValueError("贴纸项目身份不匹配")
        if validate_asset and project["image"] is not None:
            self._asset(project, directory)
        return project, storage.revision(raw), directory

    def _asset_path(self, project, directory):
        descriptor = project["image"]
        if descriptor is None:
            raise ValueError("贴纸项目尚未导入图片")
        sha256 = descriptor["sha256"]
        if not _SHA256.fullmatch(sha256):
            raise ValueError("贴纸图片摘要无效")
        path = self._guard_path(directory / "assets" / (sha256 + ".png"))
        if not path.is_relative_to(directory):
            raise ValueError("贴纸图片路径无效")
        return path

    def _asset(self, project, directory):
        path = self._asset_path(project, directory)
        if not path.is_file() or path.stat().st_size != project["image"]["bytes"]:
            raise ValueError("贴纸图片文件缺失或大小不匹配")
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != project["image"]["sha256"]:
            raise ValueError("贴纸图片校验失败")
        inspected = images.validate_canonical_png(raw)
        if (inspected["width"], inspected["height"]) != (
            project["image"]["width"], project["image"]["height"]
        ):
            raise ValueError("贴纸图片尺寸与项目记录不匹配")
        return raw

    def _public(self, project, revision):
        value = dict(project)
        value["revision"] = revision
        value["ready"] = project["image"] is not None
        value["asset_url"] = (
            "/api/stickers/projects/image?project_id=" + project["id"] + "&revision=" + revision
            if project["image"] is not None else None
        )
        if project["image"] is not None:
            value["manifest"] = protocol.make_manifest([
                {
                    "id": project["id"],
                    "display_name": project["display_name"],
                    "image": project["image"],
                    "presentation": project["presentation"],
                    "settings": project["settings"],
                }
            ])
        else:
            value["manifest"] = None
        return value

    def list(self):
        root = self._guard_path(self.root)
        if not root.exists():
            return []
        if not root.is_dir():
            raise ValueError("贴纸工作区目录无效")
        entries = list(root.iterdir())
        if any(_is_reparse(path) for path in entries):
            raise ValueError("贴纸工作区包含链接或重解析点")
        directories = [path for path in entries if path.is_dir()]
        if len(directories) > MAX_PROJECTS:
            raise ValueError("贴纸项目数量超过上限")
        result = []
        for directory in sorted(directories, key=lambda item: item.name):
            if not _PROJECT_ID.fullmatch(directory.name):
                raise ValueError("贴纸工作区包含未登记目录")
            project, revision, _ = self._read(directory.name)
            result.append({
                "id": project["id"],
                "display_name": project["display_name"],
                "description": project["description"],
                "hosts": project["hosts"],
                "ready": project["image"] is not None,
                "image": project["image"],
                "revision": revision,
            })
        return result

    def get(self, identity):
        project, revision, _ = self._read(identity, validate_asset=True)
        return self._public(project, revision)

    def create(self, *, display_name, description="", hosts=None, presentation=None,
               settings=None, preset_id=None):
        root = self._guard_path(self.root)
        if root.exists() and not root.is_dir():
            raise ValueError("贴纸工作区目录无效")
        if len(self.list()) >= MAX_PROJECTS:
            raise ValueError("贴纸项目数量达到上限")
        identity = secrets.token_hex(16)
        directory = self._directory(identity)
        while directory.exists():
            identity = secrets.token_hex(16)
            directory = self._directory(identity)
        project = {
            "schema": PROJECT_SCHEMA,
            "id": identity,
            "display_name": display_name,
            "description": description,
            "hosts": self.validate_hosts(hosts or {"client": None, "server": None}),
            "presentation": presentation or protocol.default_presentation(),
            "settings": {} if settings is None else settings,
            "image": None,
            "source": None,
            "sizing": None,
        }
        project, raw = _project_bytes(project)
        directory.mkdir(parents=True, exist_ok=False)
        self._guard_path(directory)
        try:
            if preset_id is not None:
                if preset_id != PRESET_ID:
                    raise ValueError("内置贴纸预设不存在")
                canonical = images.canonicalize_raster(
                    self.preset_bytes(), source_name="big-whale.png"
                )
                project = self._with_image(project, canonical)
                project, raw = _project_bytes(project)
                self._write_asset(directory, canonical["png"])
            project_path = self._guard_path(directory / "project.json")
            storage.atomic(project_path, raw)
        except Exception:
            self._remove_empty_or_owned(directory)
            raise
        return self._public(project, storage.revision(raw))

    @staticmethod
    def _with_image(project, canonical):
        descriptor = _canonical_metadata(canonical)
        descriptor["path"] = "images/" + project["id"] + ".png"
        return {
            **project,
            "image": descriptor,
            "source": canonical["source"],
            "sizing": canonical["sizing"],
        }

    def _write_asset(self, directory, raw):
        directory = self._guard_path(directory)
        sha256 = hashlib.sha256(raw).hexdigest()
        assets = self._guard_path(directory / "assets")
        assets.mkdir(exist_ok=True)
        self._guard_path(assets)
        path = self._guard_path(assets / (sha256 + ".png"))
        created = False
        if path.exists():
            if not path.is_file() or path.read_bytes() != raw:
                raise ValueError("贴纸图片摘要路径冲突")
        else:
            storage.atomic(path, raw)
            created = True
        return path, created

    def save(self, identity, expected_revision, *, display_name, description, hosts,
             presentation, settings=None):
        project, revision, directory = self._read(identity, validate_asset=True)
        if expected_revision != revision:
            raise storage.Conflict("贴纸项目已在其他位置改变，请刷新后重试")
        candidate = {
            **project,
            "display_name": display_name,
            "description": description,
            "hosts": self.validate_hosts(hosts),
            "presentation": presentation,
            "settings": project["settings"] if settings is None else settings,
        }
        candidate, raw = _project_bytes(candidate)
        storage.atomic(self._guard_path(directory / "project.json"), raw)
        return self._public(candidate, storage.revision(raw))

    def import_image(self, identity, expected_revision, raw, *, source_name,
                     allow_animated_first_frame=False, presentation=None):
        project, revision, directory = self._read(identity, validate_asset=True)
        if expected_revision != revision:
            raise storage.Conflict("贴纸项目已在其他位置改变，请刷新后重试")
        canonical = images.canonicalize_raster(
            raw,
            source_name=source_name,
            allow_animated_first_frame=allow_animated_first_frame,
        )
        candidate = self._with_image(project, canonical)
        if presentation is not None:
            candidate["presentation"] = presentation
        candidate, project_raw = _project_bytes(candidate)
        new_asset, created = self._write_asset(directory, canonical["png"])
        old_asset = self._asset_path(project, directory) if project["image"] is not None else None
        try:
            storage.atomic(self._guard_path(directory / "project.json"), project_raw)
        except Exception:
            if created and old_asset != new_asset and new_asset.is_file():
                new_asset.unlink()
            raise
        if old_asset is not None and old_asset != new_asset and old_asset.is_file():
            self._guard_path(old_asset).unlink()
        return self._public(candidate, storage.revision(project_raw))

    def image(self, identity, expected_revision=None):
        project, revision, directory = self._read(identity)
        if expected_revision is not None and expected_revision != revision:
            raise storage.Conflict("贴纸项目已改变，请刷新图片")
        return self._asset(project, directory)

    def delete(self, identity, expected_revision):
        project, revision, directory = self._read(identity, validate_asset=True)
        if expected_revision != revision:
            raise storage.Conflict("贴纸项目已在其他位置改变，请刷新后重试")
        self._remove_empty_or_owned(directory)
        return {"deleted": project["id"]}

    def _remove_empty_or_owned(self, directory):
        directory = self._guard_path(directory)
        if not directory.is_relative_to(self.root) or directory == self.root:
            raise ValueError("拒绝删除贴纸工作区之外的目录")
        if not directory.exists():
            return
        allowed = {directory / "project.json"}
        assets = self._guard_path(directory / "assets")
        if assets.exists():
            if not assets.is_dir():
                raise ValueError("贴纸项目资产目录无效")
            for path in assets.iterdir():
                if _is_reparse(path) or not path.is_file() or not re.fullmatch(r"[0-9a-f]{64}\.png", path.name):
                    raise ValueError("贴纸项目包含未登记资产，拒绝删除")
                allowed.add(path)
        for path in directory.iterdir():
            if path == assets:
                continue
            if path not in allowed or _is_reparse(path) or not path.is_file():
                raise ValueError("贴纸项目包含未登记文件，拒绝删除")
        for path in allowed:
            if path.exists():
                self._guard_path(path).unlink()
        if assets.exists():
            assets.rmdir()
        directory.rmdir()
