"""Manager provider for source-neutral sticker projects."""
from __future__ import annotations

import base64
import binascii
from urllib.parse import parse_qs, urlsplit

from manager import storage
from manager.providers import Binary, Provider
from owner.stickers import images

from . import native
from .projects import StickerProjects


class Stickers(Provider):
    id, title = "stickers", "自定义贴纸"
    fields = {}

    def describe(self):
        return {
            "id": self.id,
            "title": self.title,
            "fields": {},
            "config": {},
            "configured": True,
            "audience": "user",
            "embedded_configuration": True,
            "panel_without_configuration": True,
        }

    def configure(self, values):
        if values:
            raise ValueError("贴纸项目在功能页面中选择现有客户端和服务器")
        return self.describe()

    def get_engine(self):
        if self.engine is None:
            self.engine = StickerProjects(self.workspace)
        return self.engine

    def change_review_target(self,path,body):
        if path!='/api/stickers/native/apply-server':return None
        self._body(body,{'project_id','expected_revision'})
        project=self.get_engine().get(body['project_id'])
        if project['revision']!=body['expected_revision']:raise storage.Conflict('贴纸项目已改变，请刷新后重试')
        hosts=self.get_engine().validate_hosts(project['hosts'])
        if not hosts['server']:raise ValueError('请选择世界服务器')
        from owner.stickers.server import world_ini
        return world_ini(self.workspace.host(hosts['server']))

    @staticmethod
    def _body(value, required=(), optional=()):
        if not isinstance(value, dict):
            raise ValueError("贴纸请求必须是对象")
        required = set(required)
        allowed = required | set(optional)
        if set(value) - allowed or required - set(value):
            raise ValueError("贴纸请求字段无效")
        return value

    @staticmethod
    def _image_bytes(encoded):
        if not isinstance(encoded, str) or not encoded:
            raise ValueError("请选择要导入的图片")
        maximum = ((images.MAX_INPUT_BYTES + 2) // 3) * 4
        if len(encoded) > maximum:
            raise ValueError("导入图片超过 16 MiB 上限")
        try:
            raw = base64.b64decode(encoded, validate=True)
        except (ValueError, binascii.Error) as error:
            raise ValueError("导入图片编码无效") from error
        if not raw or len(raw) > images.MAX_INPUT_BYTES:
            raise ValueError("导入图片超过 16 MiB 上限")
        return raw

    def _state(self):
        engine = self.get_engine()
        return {
            "projects": engine.list(),
            "hosts": engine.host_choices(),
            "presets": engine.presets(),
            "codecs": images.codec_capabilities(),
            "native": native.capabilities(),
        }

    def _native(self, operation, body):
        self._body(body, {"project_id", "expected_revision"})
        engine = self.get_engine()
        project, revision, directory = engine._read(body["project_id"], validate_asset=True)
        if body["expected_revision"] != revision:
            raise storage.Conflict("贴纸项目已改变，请刷新后重试")
        image = engine._asset(project, directory)
        hosts = engine.validate_hosts(project["hosts"])
        client = self.workspace.host(hosts["client"]) if hosts["client"] else None
        server = self.workspace.host(hosts["server"]) if hosts["server"] else None
        function = {
            "preflight": native.preflight,
            "compile": native.compile,
            "install": native.install,
            "server-plan": native.server_plan,
            "apply-server": native.apply_server,
        }[operation]
        return function(project, image, client, server)

    def request(self, method, uri, body, headers):
        parsed = urlsplit(uri)
        path = parsed.path
        engine = self.get_engine()
        if method == "GET":
            if path == "/api/stickers":
                return self._state()
            if path == "/api/stickers/presets/big-whale/image":
                return Binary(engine.preset_bytes(), "image/png")
            if path == "/api/stickers/projects/image":
                query = parse_qs(parsed.query, keep_blank_values=True)
                if set(query) - {"project_id", "revision"} or len(query.get("project_id", [])) != 1:
                    raise ValueError("贴纸图片请求无效")
                revision = query.get("revision", [None])
                if len(revision) != 1:
                    raise ValueError("贴纸图片版本无效")
                return Binary(
                    engine.image(query["project_id"][0], revision[0]),
                    "image/png",
                )
            raise ValueError("贴纸操作未登记")
        if method != "POST":
            raise ValueError("贴纸操作未登记")
        if path == "/api/stickers/projects/list":
            self._body(body)
            return self._state()
        if path == "/api/stickers/projects/create":
            self._body(
                body,
                {"display_name", "description", "hosts", "presentation", "preset_id"},
                {"settings"},
            )
            return {
                "project": engine.create(
                    display_name=body["display_name"],
                    description=body["description"],
                    hosts=body["hosts"],
                    presentation=body["presentation"],
                    settings=body.get("settings"),
                    preset_id=body["preset_id"],
                ),
                "projects": engine.list(),
            }
        if path == "/api/stickers/projects/get":
            self._body(body, {"project_id"})
            return {"project": engine.get(body["project_id"])}
        if path == "/api/stickers/projects/save":
            self._body(
                body,
                {
                    "project_id", "expected_revision", "display_name", "description",
                    "hosts", "presentation", "settings",
                },
            )
            return {
                "project": engine.save(
                    body["project_id"],
                    body["expected_revision"],
                    display_name=body["display_name"],
                    description=body["description"],
                    hosts=body["hosts"],
                    presentation=body["presentation"],
                    settings=body["settings"],
                ),
                "projects": engine.list(),
            }
        if path == "/api/stickers/projects/import":
            self._body(
                body,
                {
                    "project_id", "expected_revision", "filename", "image_base64",
                    "allow_animated_first_frame", "presentation",
                },
            )
            if type(body["allow_animated_first_frame"]) is not bool:
                raise ValueError("动画首帧确认无效")
            return {
                "project": engine.import_image(
                    body["project_id"],
                    body["expected_revision"],
                    self._image_bytes(body["image_base64"]),
                    source_name=body["filename"],
                    allow_animated_first_frame=body["allow_animated_first_frame"],
                    presentation=body["presentation"],
                ),
                "projects": engine.list(),
            }
        if path == "/api/stickers/projects/delete":
            self._body(body, {"project_id", "expected_revision"})
            result = engine.delete(body["project_id"], body["expected_revision"])
            return {**result, "projects": engine.list()}
        if path.startswith("/api/stickers/native/"):
            operation = path.rsplit("/", 1)[-1]
            if operation not in {"preflight", "compile", "install", "server-plan", "apply-server"}:
                raise ValueError("贴纸原生操作未登记")
            return self._native(operation, body)
        raise ValueError("贴纸操作未登记")


__all__ = ["Stickers"]
