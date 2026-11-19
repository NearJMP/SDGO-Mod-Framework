"""Apply the reversible PE flag on an explicitly supported client image."""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import tempfile

from . import storage


LAA = 0x0020
SUPPORTED_ORIGINALS = {'e2dba73c10302b50cb0e6f2e58dd0a14effa8d224989ae570a7f8e05af79ff92'}


def image_variant(raw: bytes, enabled: bool) -> bytes:
    if type(enabled) is not bool:
        raise ValueError('LAA 设置必须为布尔值')
    if len(raw) < 64 or raw[:2] != b'MZ':
        raise ValueError('客户端程序不是有效的 PE 文件')
    pe = struct.unpack_from('<I', raw, 60)[0]
    if pe < 64 or pe + 122 > len(raw) or raw[pe:pe + 4] != b'PE\0\0':
        raise ValueError('客户端程序的 PE 文件头无效')
    machine, optional_size, flags, magic = struct.unpack_from('<H14xHHH', raw, pe + 4)
    if machine != 0x14c or magic != 0x10b or optional_size < 120 or not flags & 2 or flags & 0x2000:
        raise ValueError('LAA 设置只支持已登记的 32 位游戏程序')
    original_flags = flags & ~LAA
    original = raw[:pe + 22] + struct.pack('<H', original_flags) + raw[pe + 24:]
    if hashlib.sha256(original).hexdigest() not in SUPPORTED_ORIGINALS:
        raise ValueError('当前客户端程序与已验证的原版不匹配，未更改文件')
    target_flags = original_flags | (LAA if enabled else 0)
    return raw[:pe + 22] + struct.pack('<H', target_flags) + raw[pe + 24:]


def inspect(root):
    root = Path(root).resolve()
    executable = root / 'GOnline.exe'
    if (executable.resolve() != executable or not executable.is_file()
            or executable.stat().st_size > 32 * 1024**2):
        raise ValueError('目标目录内没有兼容的客户端程序')
    raw = executable.read_bytes()
    original = image_variant(raw, False)
    return {'enabled': raw != original, 'sha256': storage.revision(raw)}


def configure(root, enabled, expected):
    from .client_runtime import ClientRuntime
    from .operations import unmapped_image
    from .target_locks import lease

    root = Path(root).resolve()
    executable = root / 'GOnline.exe'
    with lease('host:' + str(root)):
        current = inspect(root)
        if current['sha256'] != expected:
            raise storage.Conflict('客户端版本已更改，请重新读取 LAA 设置')
        if type(enabled) is not bool:
            raise ValueError('LAA 设置必须为布尔值')
        if current['enabled'] == enabled:
            return current

        target = image_variant(executable.read_bytes(), enabled)
        recovery = storage.safe_child(root, 'sdgo/local-state/client-memory')
        recovery.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='switch-', dir=recovery) as directory:
            staging = Path(directory)
            candidate = staging / executable.name
            storage.atomic(candidate, target)
            with unmapped_image(executable, replace=True), unmapped_image(candidate, replace=True):
                if inspect(root) != current:
                    raise storage.Conflict('客户端版本已更改，请重新读取 LAA 设置')
                ClientRuntime.move_image(candidate, executable, staging)
        return inspect(root)
