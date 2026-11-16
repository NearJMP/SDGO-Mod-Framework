"""Manager adapter to the paired native sticker compiler and installer."""

from __future__ import annotations
import base64
import functools
import hashlib
import io
from pathlib import Path
import zipfile
from owner.stickers import compiler, deployment, server
from owner.units.assets import Baseline


class NativeUnavailable(ValueError):
    pass


def capabilities():
    return {
        "available": True,
        "blocker": "安装需要配套贴纸导入程序；客户端和世界服务器需分别应用同一项目。",
        "operations": {
            key: True
            for key in (
                "preflight",
                "compile",
                "install",
                "server_plan",
                "apply_server",
            )
        },
        "known_limits": {
            "texture_edge": 512,
            "texture_path_utf16": 99,
            "sprite_path_utf16": 47,
            "entries": 2048,
        },
    }


@functools.lru_cache(maxsize=4)
def _baseline(path, index_time, index_size, archive_time):
    return Baseline(Path(path))


def _prepare(project, image, client_host, server_host, *, check_server=True):
    if not isinstance(client_host, dict) or client_host.get("role") != "client":
        raise NativeUnavailable("请选择已添加的客户端")
    root = Path(client_host["path"])
    exe = root / "GOnline.exe"
    if (
        not exe.is_file()
        or hashlib.sha256(exe.read_bytes()).hexdigest() != compiler.HOST_SHA256
    ):
        raise NativeUnavailable("所选客户端不是当前贴纸导入器支持的原生版本")
    index = root / "GPackData.zdx"
    archive = root / "GPackData.zpk"
    if not index.is_file() or not archive.is_file():
        raise NativeUnavailable("所选客户端缺少原生资源归档")
    baseline = _baseline(
        str(root),
        index.stat().st_mtime_ns,
        index.stat().st_size,
        archive.stat().st_mtime_ns,
    )
    entries, resources, installed_revision = deployment.installed(root)
    owner = project["id"]
    existing = next((e for e in entries if e["owner"] == owner), None)
    explicit = project.get("settings", {}).get("native_id")
    identity = (
        existing["id"]
        if existing
        else (
            explicit
            if explicit is not None
            else 8_000_000 + int(owner[:8], 16) % 1_000_000
        )
    )
    if existing and explicit is not None and explicit != identity:
        raise ValueError("已安装项目不能直接更换编号，请保留原有归属")
    foreign = {e["id"] for e in entries if e["owner"] != owner}
    if existing is None and explicit is None:
        for _ in range(1_000_000):
            if identity not in foreign and not baseline.any_id(identity):
                break
            identity = 8_000_000 + (identity - 8_000_000 + 1) % 1_000_000
        else:
            raise ValueError("没有可分配的纹章编号")
    if identity in foreign or baseline.any_id(identity):
        raise ValueError("原生编号已被原版或其他项目占用")
    used = {e["category_id"] for e in entries if e["owner"] != owner}
    category_id = (
        existing["category_id"]
        if existing
        else 1_790_000 + int(owner[8:16], 16) % 10_000
    )
    if not existing:
        for _ in range(10_000):
            if category_id not in used and not baseline.any_id(category_id):
                break
            category_id = 1_790_000 + (category_id - 1_790_000 + 1) % 10_000
        else:
            raise ValueError("没有可分配的纹章物品编号")
    if category_id in used or baseline.any_id(category_id):
        raise ValueError("纹章物品编号已被原版或其他项目占用")
    entry = compiler.definition(project, image, identity, category_id)
    for path in compiler.paths(identity):
        try:
            baseline.read(path)
        except (FileNotFoundError, KeyError):
            pass
        else:
            raise ValueError("纹章资源路径与目标客户端原版资源冲突")
    plan = compiler.server_plan(entry)
    server_state = (
        server.preflight(server_host, plan) if server_host and check_server else None
    )
    entries = [e for e in entries if e["owner"] != owner] + [entry]
    resources.update(compiler.textures(image, identity))
    files = compiler.compile_content(entries, resources)
    result = {
        "native_id": identity,
        "category_id": category_id,
        "revision": plan["revision"],
        "restart_required": True,
        "server_checked": server_state is not None,
        "server_state": server_state,
        "server_plan": plan,
        "files": [
            {"path": name, "sha256": compiler.sha(raw), "bytes": len(raw)}
            for name, raw in sorted(files.items())
        ],
        "warnings": [
            "客户端启动时将再次核对原生编号冲突。",
            "应用定义不会发放纹章；库存、商城和奖励由服主已有工具管理。",
        ],
    }
    return root, entry, files, installed_revision, result


def preflight(project, image, client_host=None, server_host=None):
    *_, result = _prepare(project, image, client_host, server_host)
    return {**result, "message": "贴纸资源与编号预检通过；请核对配套服务器计划。"}


def compile(project, image, client_host=None, server_host=None):
    _, _, files, _, result = _prepare(project, image, client_host, server_host)
    package = io.BytesIO()
    with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as z:
        for name, raw in sorted(files.items()):
            z.writestr("client/sdgo/mods/sticker_catalog/" + name, raw)
        z.writestr("server-plan.json", compiler.json_bytes(result["server_plan"]))
        z.writestr(
            "manifest.json",
            compiler.json_bytes(
                {
                    "schema": "sdgo.sticker-publication/1",
                    "revision": result["revision"],
                    "files": result["files"],
                }
            ),
        )
    return {
        **result,
        "message": "已生成配套资源与服务器定义计划。",
        "filename": "stickers-" + result["revision"][:12] + ".zip",
        "package_base64": base64.b64encode(package.getvalue()).decode("ascii"),
    }


def install(project, image, client_host=None, server_host=None):
    from manager.owner_channel import mode

    if client_host and mode(client_host) in ("player", "blocked"):
        raise ValueError(
            "这个客户端跟随服主配置，请先通过 Manager 切换为维护者再编辑共享内容"
        )
    from manager.target_locks import lease

    if not client_host:
        raise ValueError("请选择客户端")
    with lease("host:" + str(Path(client_host["path"]).resolve()).casefold()):
        deployment.recover(client_host["path"])
        root, _, files, revision, result = _prepare(
            project, image, client_host, server_host
        )
        receipt = deployment.install(root, files, revision)
    return {
        **result,
        "receipt": receipt,
        "message": "已安装客户端内容；还须应用同一服务器定义，并在正常重启后激活。",
    }


def server_plan(project, image, client_host=None, server_host=None):
    *_, result = _prepare(project, image, client_host, server_host)
    return {**result, "message": "已生成服务器定义计划；没有修改库存、商城或奖励。"}


def apply_server(project, image, client_host=None, server_host=None):
    root, entry, _, _, result = _prepare(project, image, client_host, server_host)
    entries, _, _ = deployment.installed(root)
    if entry not in entries:
        raise ValueError("请先安装同一项目的客户端内容，再应用服务器定义")
    applied = server.apply(server_host, entry)
    return {
        **result,
        **applied,
        "message": "服务器纹章定义已应用；需正常重启世界服务器激活。",
    }
