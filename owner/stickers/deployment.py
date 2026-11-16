"""Owner-safe, data-only sticker content installation into a stopped client."""

from __future__ import annotations
import configparser
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import threading
from manager import storage
from owner.native_archive import read_archive_bytes
from . import compiler

LOCK = threading.RLock()
JOURNAL = ".sticker-install-transaction"
ABI = b"SDGO_STICKER_CATALOG_ABI_1_KIND_38_RECORD_409_CATEGORY_34_RECORD_614_DEFAULT_SIZE_V1"
TEMP_SUFFIX = ".sticker-import-pending"


def _write(path, raw):
    path = Path(path)
    safe(path.parent, path.name + TEMP_SUFFIX)
    storage.atomic(path, raw, temporary_name=path.name + TEMP_SUFFIX)


def programme(root):
    dll = safe(root, "sticker_catalog.dll")
    ini = safe(root, "mod.ini")
    if not dll.is_file() or not ini.is_file() or dll.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("请先通过 Manager 安装配套的贴纸导入程序")
    raw = dll.read_bytes()
    if len(raw) < 256 or raw[:2] != b"MZ":
        raise ValueError("贴纸导入程序不是受支持的 Win32 DLL")
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    if (
        pe + 24 > len(raw)
        or raw[pe : pe + 4] != b"PE\0\0"
        or struct.unpack_from("<H", raw, pe + 4)[0] != 0x14C
        or ABI not in raw
    ):
        raise ValueError("贴纸导入程序缺少配套的 kind-38/409B 协议能力，请先更新程序")
    config = storage.ini_values(ini.read_bytes())
    if (
        config.get("mod", "id", fallback="") != "sticker_catalog"
        or config.get("mod", "version", fallback="") != "0.1.0"
    ):
        raise ValueError("贴纸导入程序版本与编译器不匹配")
    return {"version": "0.1.0", "sha256": compiler.sha(raw), "abi": 1}


def _journal_cleanup(root):
    directory = safe(root, JOURNAL)
    # Publication or rollback was fully verified. Retire the journal first so
    # interrupted backup cleanup is safe to retry without missing-backup errors.
    safe(directory, "journal.json").unlink(missing_ok=True)
    for name in (*sorted(compiler.CONTENT_FILES), "manifest.json", "journal.json"):
        safe(directory, name).unlink(missing_ok=True)
        safe(directory, name + TEMP_SUFFIX).unlink(missing_ok=True)
    directory.rmdir()


def recover(client):
    """Retry completes interrupted rollback; foreign edits always stop it."""
    from manager.target_locks import lease
    from manager.updates import running

    client = Path(client)
    with lease("host:" + str(client.resolve()).casefold()):
        root = safe(client, "sdgo/mods/sticker_catalog")
        directory = safe(root, JOURNAL)
        if not directory.exists():
            return False
        if running(client / "GOnline.exe"):
            raise ValueError("请先关闭所选客户端，再恢复未完成的安装")
        names = compiler.CONTENT_FILES | {"manifest.json"}
        expected = (
            names
            | {"journal.json"}
            | {n + TEMP_SUFFIX for n in names | {"journal.json"}}
        )
        if not directory.is_dir() or any(
            p.name not in expected or not p.is_file() or safe(directory, p.name) != p
            for p in directory.iterdir()
        ):
            raise ValueError("安装恢复目录包含未知内容，已保留供检查")
        path = safe(directory, "journal.json")
        if not path.exists():
            _journal_cleanup(root)
            return True
        if path.stat().st_size > 64 * 1024:
            raise ValueError("安装恢复日志超过预算")
        journal = json.loads(path.read_bytes())
        if (
            not isinstance(journal, dict)
            or set(journal) != {"schema", "before", "after"}
            or journal["schema"] != "sdgo.sticker-transaction/1"
            or set(journal["before"]) != names
            or set(journal["after"]) != names
        ):
            raise ValueError("安装恢复日志无效")
        previous = {}
        for name in sorted(names):
            target = safe(root, name)
            backup = safe(directory, name)
            old = journal["before"][name]
            new = journal["after"][name]
            if old is None:
                if backup.exists():
                    raise ValueError("安装恢复备份不符合日志")
                previous[name] = None
            else:
                if not backup.is_file() or backup.stat().st_size > 128 * 1024 * 1024:
                    raise ValueError("安装恢复备份缺失或过大")
                previous[name] = backup.read_bytes()
                if compiler.sha(previous[name]) != old:
                    raise ValueError("安装恢复备份校验失败")
            if target.is_file() and target.stat().st_size > 128 * 1024 * 1024:
                raise ValueError("安装恢复目标超过预算")
            current = compiler.sha(target.read_bytes()) if target.is_file() else None
            if current not in (old, new):
                raise ValueError("安装后出现未登记的作者修改，已保留恢复材料，拒绝覆盖")
        for name, raw in previous.items():
            safe(root, name + TEMP_SUFFIX).unlink(missing_ok=True)
            if raw is None:
                safe(root, name).unlink(missing_ok=True)
            else:
                _write(safe(root, name), raw)
        for name, raw in previous.items():
            path = safe(root, name)
            if (raw is None and path.exists()) or (
                raw is not None and path.read_bytes() != raw
            ):
                raise ValueError("纹章安装恢复读回失败，已保留恢复材料")
        _journal_cleanup(root)
        return True


def safe(root, relative):
    root = Path(os.path.abspath(root))
    path = root / relative
    if not path.is_relative_to(root):
        raise ValueError("纹章路径越界")
    for part in (
        root,
        *[
            root.joinpath(*path.relative_to(root).parts[:i])
            for i in range(1, len(path.relative_to(root).parts) + 1)
        ],
    ):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(
            info, "st_file_attributes", 0
        ) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
            raise ValueError("纹章安装路径不能经过链接")
    return path


def installed(client):
    root = safe(client, "sdgo/mods/sticker_catalog")
    if safe(root, JOURNAL).exists():
        raise ValueError("存在未完成的纹章安装，请重试安装以恢复")
    receipt = safe(root, "manifest.json")
    present = [name for name in compiler.CONTENT_FILES if safe(root, name).exists()]
    if not present:
        if receipt.exists():
            raise ValueError("纹章安装回执存在，但内容缺失")
        return [], {}, None
    if set(present) != compiler.CONTENT_FILES or not receipt.is_file():
        raise ValueError("已安装纹章内容不完整或缺少归属回执")
    if receipt.stat().st_size > 1024 * 1024:
        raise ValueError("纹章安装回执过大")
    state = json.loads(receipt.read_bytes())
    if (
        set(state) != {"schema", "files", "revision"}
        or state["schema"] != "sdgo.sticker-content/1"
        or set(state["files"]) != compiler.CONTENT_FILES
    ):
        raise ValueError("纹章安装回执无效")
    if state["revision"] != compiler.sha(compiler.json_bytes(state["files"])):
        raise ValueError("纹章安装回执版本校验失败")
    raw = {}
    for name in compiler.CONTENT_FILES:
        p = safe(root, name)
        if p.stat().st_size > 128 * 1024 * 1024:
            raise ValueError("纹章内容超过预算")
        raw[name] = p.read_bytes()
        if compiler.sha(raw[name]) != state["files"][name]:
            raise ValueError("已安装纹章有未登记修改，请先保存作者内容")
    doc = compiler.validate_definitions(json.loads(raw["definitions.json"]))
    if raw["catalog.bin"] != compiler.catalog(doc["entries"]):
        raise ValueError("纹章原生目录与定义不一致")
    config = configparser.ConfigParser(interpolation=None, strict=True)
    config.read_string(raw["resource-overlay.ini"].decode("ascii"))
    if set(config.sections()) != {"runtime"} or dict(config["runtime"]) != {
        "schema": "1",
        "index": "resources.zdx",
        "archive": "resources.zpk",
        "index_sha256": compiler.sha(raw["resources.zdx"]),
        "archive_sha256": compiler.sha(raw["resources.zpk"]),
    }:
        raise ValueError("纹章资源绑定无效")
    resources = read_archive_bytes(
        raw["resources.zdx"],
        raw["resources.zpk"],
        compiler.allowed_resource,
        max_archive_bytes=compiler.MAX_NATIVE_ARCHIVE_BYTES,
        max_resource_bytes=2 * 1024 * 1024,
        max_total_bytes=128 * 1024 * 1024,
    )
    if set(resources) != {p for e in doc["entries"] for p in compiler.paths(e["id"])}:
        raise ValueError("纹章资源范围不一致")
    return doc["entries"], resources, state["revision"]


def install(client, files, expected_revision):
    from manager.updates import running
    from manager.target_locks import lease

    client = Path(client)
    if not (client / "GOnline.exe").is_file():
        raise ValueError("请选择客户端")
    if set(files) != compiler.CONTENT_FILES:
        raise ValueError("纹章安装文件不完整")
    if any(
        not isinstance(raw, bytes) or len(raw) > compiler.MAX_NATIVE_ARCHIVE_BYTES
        for raw in files.values()
    ):
        raise ValueError("纹章安装文件超过预算或不是固定字节")
    resources = read_archive_bytes(
        files["resources.zdx"],
        files["resources.zpk"],
        compiler.allowed_resource,
        max_archive_bytes=compiler.MAX_NATIVE_ARCHIVE_BYTES,
        max_resource_bytes=2 * 1024 * 1024,
        max_total_bytes=128 * 1024 * 1024,
    )
    document = compiler.validate_definitions(json.loads(files["definitions.json"]))
    if compiler.compile_content(document["entries"], resources) != files:
        raise ValueError("待安装的纹章目录、定义和资源不匹配，未写入任何文件")
    with LOCK, lease("host:" + str(client.resolve()).casefold()):
        if running(client / "GOnline.exe"):
            raise ValueError("请关闭所选客户端后再安装纹章")
        recover(client)
        _, _, revision = installed(client)
        if revision != expected_revision:
            raise storage.Conflict("所选客户端的纹章目录已改变，请重新预检")
        root = safe(client, "sdgo/mods/sticker_catalog")
        programme(root)
        old = {
            name: safe(root, name).read_bytes() if safe(root, name).is_file() else None
            for name in (*sorted(files), "manifest.json")
        }
        hashes = {name: compiler.sha(raw) for name, raw in sorted(files.items())}
        state = {
            "schema": "sdgo.sticker-content/1",
            "files": hashes,
            "revision": compiler.sha(compiler.json_bytes(hashes)),
        }
        pending = {**files, "manifest.json": compiler.json_bytes(state)}
        directory = safe(root, JOURNAL)
        if directory.exists():
            raise ValueError("存在未完成的纹章安装，请先恢复")
        directory.mkdir()
        for name, raw in old.items():
            if raw is not None:
                _write(safe(directory, name), raw)
        _write(
            safe(directory, "journal.json"),
            compiler.json_bytes(
                {
                    "schema": "sdgo.sticker-transaction/1",
                    "before": {
                        name: compiler.sha(raw) if raw is not None else None
                        for name, raw in old.items()
                    },
                    "after": {name: compiler.sha(raw) for name, raw in pending.items()},
                }
            ),
        )
        try:
            for name, raw in pending.items():
                _write(safe(root, name), raw)
            for name, raw in pending.items():
                if safe(root, name).read_bytes() != raw:
                    raise ValueError("纹章安装读回不一致")
        except Exception:
            recover(client)
            raise
        _journal_cleanup(root)
        installed(client)
        return state
