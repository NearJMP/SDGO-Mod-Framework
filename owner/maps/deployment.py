"""Install a map_catalog client package (built by owner.maps.bundle) into a stopped client.

Only these targets are written: ``sdgo/mods/map_catalog/<declared files>`` and loose ``BGM/m<NNN>*.mp3``.
Every file is checked against the package manifest digest before anything is written; existing files are
replaced atomically. No script from the package is executed.
"""
from __future__ import annotations

import configparser
import hashlib
import io
import json
import os
import re
import shutil
import struct
import tempfile
import zipfile
from pathlib import Path

from manager import storage
from manager.operations import unmapped_image
from manager.target_locks import lease
from owner.native_archive import CHUNK_SIZE, MAX_ARCHIVE_BYTES, MAX_PARTS, OverlayReader, part_paths, update_digest
from . import boundary_format

MAX_PACKAGE_BYTES = 256 * 1024 * 1024
MAX_FILE_BYTES = 128 * 1024 * 1024
MAX_STREAM_BYTES = MAX_ARCHIVE_BYTES + MAX_PACKAGE_BYTES
PREFIX = 'sdgo/mods/map_catalog/'
MOD_FILES = {'catalog.bin', 'definitions.json', 'resource-overlay.ini', 'resources.zdx', 'resources.zpk', 'missions.lua', 'boundaries.bin',
             'damage-gates.bin',
             'retreats.bin',
             'map_catalog.dll', 'mod.ini', 'manager.json', 'symbols.fragment.ini'}
PROGRAM_FILES = {'map_catalog.dll', 'mod.ini', 'manager.json', 'symbols.fragment.ini'}
REQUIRED = {'catalog.bin', 'definitions.json', 'resource-overlay.ini', 'resources.zdx', 'resources.zpk'}
BOUNDARY_PROGRAMME_VERSION = (0, 6, 0)


def _programme_version(raw: bytes) -> tuple[int, int, int]:
    try:
        parser = configparser.ConfigParser(interpolation=None, strict=True)
        parser.read_string(raw.decode('utf-8-sig'))
        if parser.get('mod', 'id') != 'map_catalog':
            raise ValueError
        value = parser.get('mod', 'version')
    except (UnicodeError, configparser.Error, KeyError, ValueError):
        raise ValueError('地图导入模块元数据无效') from None
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', value.strip())
    if not match:
        raise ValueError('地图导入模块版本格式无效')
    return tuple(int(part) for part in match.groups())


def _archive_part(leaf: str) -> bool:
    number = re.fullmatch(r'resources\.([1-9][0-9]?)\.zpk', leaf)
    return bool(number and int(number[1]) < MAX_PARTS)


def _target(name: str) -> str:
    if name.startswith('client/' + PREFIX):
        leaf = name[len('client/' + PREFIX):]
        if leaf in MOD_FILES or _archive_part(leaf):
            return PREFIX + leaf
    if re.fullmatch(r'client/BGM/m[0-9]{3}[a-z0-9]*\.mp3', name):
        return name[len('client/'):]
    raise ValueError('地图包包含未登记的文件：' + name)


def _running(client: Path) -> bool:
    try:
        from manager.packages import _running as running
    except Exception:
        return False
    return running(client / 'GOnline.exe')


def install_package(client, raw: bytes | Path) -> dict:
    client = Path(client).resolve()
    executable = storage.safe_child(client, 'GOnline.exe')
    if not executable.is_file():
        raise ValueError('目标目录不是客户端')
    source = io.BytesIO(raw) if isinstance(raw, bytes) else Path(raw).open('rb')
    with source, lease('host:' + str(client)):
        size = source.seek(0, 2)
        source.seek(0)
        if size > (MAX_PACKAGE_BYTES if isinstance(raw, bytes) else MAX_STREAM_BYTES):
            raise ValueError('地图客户端包超出此入口的大小限制')
        if _running(client):
            raise ValueError('请先关闭这个客户端，再导入地图包')
        directory = storage.safe_child(client, 'sdgo/local-state/map-import')
        directory.mkdir(parents=True, exist_ok=True)
        recovery = next(directory.glob('install-*/recovery-required'), None)
        if recovery is not None:
            raise ValueError('先恢复上次未完成的地图导入。原文件保存在 ' + str(recovery.parent / 'previous'))
        staging = Path(tempfile.mkdtemp(prefix='install-', dir=directory))
        try:
            return _install_staged(client, executable, source, staging, struct.calcsize('P') == 8)
        finally:
            # A failed rollback deliberately keeps the original files for recovery.
            if not (staging / 'recovery-required').exists():
                shutil.rmtree(staging)


def _validate_split_overlay(pending, leaves, backend64, runtime_schema):
    parts = {leaf for leaf in leaves if _archive_part(leaf)}
    if not parts:
        overlay = storage.ini_values(pending[PREFIX + 'resource-overlay.ini'].read_bytes())
        if overlay.has_option('runtime', 'parts') or overlay.has_option('runtime', 'part_chunks'):
            raise ValueError('地图包缺少声明的归档分段')
        return 1
    if not backend64:
        raise ValueError('分段地图资源需要 64 位 Mod Manager')
    try:
        overlay = storage.ini_values(pending[PREFIX + 'resource-overlay.ini'].read_bytes())['runtime']
        count = int(overlay.get('parts', '0'))
        chunks = int(overlay.get('part_chunks', '0'))
    except (configparser.Error, KeyError, ValueError) as error:
        raise ValueError('地图归档分段声明无效') from error
    if not 2 <= count <= MAX_PARTS or not 0 < chunks * (CHUNK_SIZE + 8) <= MAX_FILE_BYTES:
        raise ValueError('地图归档分段布局无效')
    zpk = pending[PREFIX + 'resources.zpk']
    paths = part_paths(zpk, count)
    if parts != {p.name for p in paths[1:]}:
        raise ValueError('地图包缺少声明的归档分段')
    if (overlay.get('schema'), overlay.get('index'), overlay.get('archive')) != (runtime_schema, 'resources.zdx', 'resources.zpk'):
        raise ValueError('地图归档声明无效')
    index = pending[PREFIX + 'resources.zdx'].read_bytes()
    digest = hashlib.sha256()
    for path in paths:
        with path.open('rb') as source:
            update_digest(digest, source)
    if (hashlib.sha256(index).hexdigest() != overlay.get('index_sha256') or
            digest.hexdigest() != overlay.get('archive_sha256')):
        raise ValueError('地图归档与分段摘要不符')
    from .bundle import allowed_overlay
    from owner.native_archive import MAX_RESOURCE_BYTES
    with OverlayReader(index, zpk, count, chunks) as reader:
        for name, (size, start, used) in reader.entries.items():
            if (not allowed_overlay(name) or not 0 < size <= MAX_RESOURCE_BYTES or
                    used != (size + CHUNK_SIZE - 1) // CHUNK_SIZE or
                    start // chunks != (start + used - 1) // chunks):
                raise ValueError('地图资源索引无效：' + name)
    return count


def _install_staged(client, executable, source, staging, backend64):
    with zipfile.ZipFile(source) as archive:
        names = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
        if ('manifest.json' not in names or len(archive.infolist()) > 96 or len(set(names)) != len(names)
                or archive.getinfo('manifest.json').file_size > 8 * 1024**2):
            raise ValueError('地图包缺少清单、清单过大或文件有重复')
        manifest = json.loads(archive.read('manifest.json'))
        schema = manifest.get('schema_version')
        if manifest.get('module') != 'map_catalog' or type(schema) is not int or schema not in (1, 2, 3):
            raise ValueError('不是地图导入客户端包')
        if schema == 2 and manifest.get('minimum_programme') != '0.7.0':
            raise ValueError('新版地图包的配套程序声明无效')
        declared = manifest.get('files')
        if not isinstance(declared, dict) or set(declared) != set(names) - {'manifest.json'}:
            raise ValueError('地图包文件与清单不一致')
        pending: dict[str, Path] = {}
        archive_bytes = other_bytes = 0
        for name in sorted(declared):
            relative = _target(name)
            info = archive.getinfo(name)
            if info.file_size > MAX_FILE_BYTES:
                raise ValueError('地图包文件过大：' + name)
            if relative.endswith('.zpk'):
                archive_bytes += info.file_size
            else:
                other_bytes += info.file_size
            if archive_bytes > MAX_ARCHIVE_BYTES or other_bytes > MAX_PACKAGE_BYTES:
                raise ValueError('地图包的解压总量超过限制')
            path = staging / 'incoming' / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256()
            with archive.open(info) as entry, path.open('xb') as stream:
                for block in iter(lambda: entry.read(1024**2), b''):
                    stream.write(block)
                    digest.update(block)
            if digest.hexdigest() != declared[name] or path.stat().st_size != info.file_size:
                raise ValueError('地图包文件摘要不符：' + name)
            pending[relative] = path
    leaves = {path.rsplit('/', 1)[1] for path in pending if path.startswith('sdgo/mods/map_catalog/')}
    if not REQUIRED <= leaves:
        raise ValueError('地图包缺少必需的内容文件')
    overlay = pending[PREFIX + 'resource-overlay.ini'].read_bytes()
    runtime_schema = storage.ini_values(overlay).get('runtime', 'schema', fallback='1')
    live = schema == 3
    if runtime_schema != ('2' if live else '1'):
        raise ValueError('地图包资源后台声明与归档协议不一致')
    archive_parts = _validate_split_overlay(pending, leaves, backend64, runtime_schema)
    mod_dir = client / 'sdgo/mods/map_catalog'
    if 'mod.ini' not in leaves and not (mod_dir / 'mod.ini').is_file():
        raise ValueError('客户端尚未安装地图导入模块（map_catalog）')
    if live:
        from .bundle import LIVE_WORKER_REQUIREMENT
        from manager.dependencies import enforce_install
        from manager.resource_worker import is_x64_manager
        if manifest.get('requires_runtime') != LIVE_WORKER_REQUIREMENT or manifest.get('minimum_programme') != '0.8.0':
            raise ValueError('大地图包的运行时依赖声明无效')
        programme_manifest = pending.get(PREFIX + 'mod.ini', mod_dir / 'mod.ini')
        if _programme_version(programme_manifest.read_bytes()) < (0, 8, 0):
            raise ValueError('大地图包需要 map_catalog 0.8.0 或更高版本')
        worker_dll = client / 'sdgo/mods/resource_worker/resource_worker.dll'
        if not worker_dll.is_file() or not is_x64_manager(client / 'SDGO Mod Manager.exe'):
            raise ValueError('请安装同目录的 64 位 Mod Manager 与资源后台 Mod')
        changes = {name: path.read_bytes() for name, path in pending.items()
                   if name in (PREFIX + 'manager.json', PREFIX + 'mod.ini', PREFIX + 'resource-overlay.ini')}
        enforce_install(client, changes, 'client')
    elif schema == 2:
        programme_manifest = pending.get(PREFIX + 'mod.ini')
        if programme_manifest is None:
            programme_manifest = mod_dir / 'mod.ini'
        if _programme_version(programme_manifest.read_bytes()) < (0, 7, 0):
            raise ValueError('队伍过场与目标保护需要 map_catalog 0.7.0 或更高版本，未写入任何文件')
    gates_path = 'sdgo/mods/map_catalog/damage-gates.bin'
    if gates_path in pending:
        from . import damage_gate_format
        damage_gate_format.decode(pending[gates_path].read_bytes())
        if schema not in (2, 3):
            raise ValueError('目标保护需要新版地图包协议，未写入任何文件')
    retreats_path = 'sdgo/mods/map_catalog/retreats.bin'
    if retreats_path in pending:
        from . import retreat_format
        retreat_format.decode(pending[retreats_path].read_bytes())
        if schema not in (2, 3):
            raise ValueError('友军撤离需要新版地图包协议，未写入任何文件')
    boundary_path = 'sdgo/mods/map_catalog/boundaries.bin'
    if boundary_path in pending:
        boundary_format.decode(pending[boundary_path].read_bytes())
        programme_manifest = pending.get(PREFIX + 'mod.ini', mod_dir / 'mod.ini')
        if _programme_version(programme_manifest.read_bytes()) < BOUNDARY_PROGRAMME_VERSION:
            raise ValueError('任务活动范围需要 map_catalog 0.6.0 或更高版本，未写入任何文件')

    # Content files a previous package installed (per its receipt) and this one no longer ships are removed,
    # so e.g. an old missions.lua cannot outlive its catalog. Program files stay (the Manager owns them).
    stale = []
    receipt_path = mod_dir / 'install-receipt.json'
    try:
        previous = json.loads(receipt_path.read_text(encoding='utf-8')).get('files', {})
    except (OSError, ValueError, AttributeError):
        previous = {}
    for relative in previous if isinstance(previous, dict) else ():
        if relative in pending or not isinstance(relative, str):
            continue
        leaf = relative.removeprefix(PREFIX)
        owned = ((relative.startswith(PREFIX) and (leaf in MOD_FILES - PROGRAM_FILES or _archive_part(leaf)))
                 or re.fullmatch(r'BGM/m[0-9]{3}[a-z0-9]*\.mp3', relative))
        if owned and storage.safe_child(client, relative).is_file():
            stale.append(relative)

    receipt = {'module': 'map_catalog', 'revision': manifest.get('revision'), 'maps': manifest.get('maps', []),
               'files': {_target(name): declared[name] for name in sorted(declared)},
               'resources': {'backend_bits': 64 if backend64 else 32, 'archive_bytes': archive_bytes,
                             'archive_parts': archive_parts, **({'requires_runtime': manifest['requires_runtime']} if live else {})}}
    staged_receipt = staging / 'receipt.json'
    staged_receipt.write_bytes(storage.json_bytes(receipt))
    with unmapped_image(executable):
        _commit(client, staging, {**pending, PREFIX + 'install-receipt.json': staged_receipt}, stale)
    return {'module': 'map_catalog', 'activation': 'restart_required', 'receipt': receipt,
            'written': len(pending), 'removed': stale}


def _commit(client, staging, incoming, stale):
    changes = []
    marker = staging / 'recovery-required'
    marker.touch()
    try:
        for relative in [*stale, *incoming]:
            target = storage.safe_child(client, relative)
            target.parent.mkdir(parents=True, exist_ok=True)
            saved = staging / 'previous' / relative
            if target.exists():
                if not target.is_file():
                    raise ValueError('地图包目标不是文件：' + relative)
                saved.parent.mkdir(parents=True, exist_ok=True)
                os.replace(target, saved)
            changes.append((target, saved))
            if relative in incoming:
                os.replace(incoming[relative], target)
    except BaseException:
        for target, saved in reversed(changes):
            if saved.exists():
                os.replace(saved, target)
            else:
                target.unlink(missing_ok=True)
        marker.unlink()
        raise
    marker.unlink()
