"""Install locally supplied PNG atlas variants for the native sprite renderer."""
from __future__ import annotations

import base64
import hashlib
import io
from pathlib import Path
import re
import struct

from . import storage


PNG = b"\x89PNG\r\n\x1a\n"
LIMIT = 16 * 1024 * 1024
MAX_ATLASES = 24
SLOT = re.compile(r"([1-9][0-9]{0,3})x([1-9][0-9]{0,3})-([0-9a-f]{64})-([0-9a-f]{64})\.png")


def decode_png(value):
    from PIL import Image, UnidentifiedImageError

    if not isinstance(value, str) or len(value) > ((LIMIT + 2) // 3) * 4:
        raise ValueError("请选择不超过 16 MiB 的 PNG。")
    try:
        raw = base64.b64decode(value, validate=True)
        if len(raw) < 33 or len(raw) > LIMIT or not raw.startswith(PNG):
            raise ValueError("请选择有效的 PNG。")
        width, height = struct.unpack_from(">II", raw, 16)
        if not 1 <= width <= 4096 or not 1 <= height <= 4096:
            raise ValueError("PNG 边长须在 1 到 4096 像素之间。")
        with Image.open(io.BytesIO(raw), formats=("PNG",)) as image:
            if getattr(image, "n_frames", 1) != 1 or image.size != (width, height):
                raise ValueError("请选择单帧 PNG。")
            image.verify()
        with Image.open(io.BytesIO(raw), formats=("PNG",)) as image:
            return raw, image.convert("RGBA")
    except (UnidentifiedImageError, OSError, SyntaxError) as error:
        raise ValueError("无法读取完整的 PNG。") from error


def source_digest(image, *, four_bit=False):
    # Ignore RGB under fully transparent pixels. D3D stores bytes in BGRA order.
    from PIL import Image

    if four_bit:
        nibble = [(value >> 4) * 17 for value in range(256)]
        image = image.point(nibble * 4)
    mask = image.getchannel("A").point([0] + [255] * 255)
    clear = Image.new("RGBA", image.size)
    clear.paste(image, mask=mask)
    return hashlib.sha256(clear.tobytes("raw", "BGRA")).hexdigest()


def folder(host):
    if host["role"] != "client":
        raise ValueError("请选择需要启用此图片的客户端。")
    directory = Path(host["path"]).resolve() / "sdgo/local-state/native-display/artwork"
    if directory.resolve() != directory:
        raise ValueError("本地游戏图片目录不能使用链接。")
    return directory


def slot_path(directory, name):
    path = directory / name
    if path.resolve() != path:
        raise ValueError("本地游戏图片不能使用链接。")
    return path


def entries(host):
    directory = folder(host)
    if not directory.is_dir():
        return []
    installed = []
    for path in sorted(directory.glob("*.png")):
        match = SLOT.fullmatch(path.name)
        if match and path.is_file() and not path.is_symlink():
            w, h, digest, _ = match.groups()
            installed.append(dict(key=path.name, width=int(w), height=int(h),
                                  source=digest[:12]))
    return installed


def change(host, body):
    directory = folder(host)
    action = body.get("action")
    if action == "remove":
        slot = body.get("key")
        if not isinstance(slot, str) or not SLOT.fullmatch(slot):
            raise ValueError("图片标识无效。")
        with storage.LOCK:
            storage.remove(slot_path(directory, slot))
        return entries(host)
    if action != "apply":
        raise ValueError("图片操作无效。")
    _, source = decode_png(body.get("source"))
    raw, replacement = decode_png(body.get("replacement"))
    width, height = source.size
    factor, remainder = divmod(replacement.width, width)
    if remainder or factor not in (2, 3) or replacement.height != height * factor:
        raise ValueError("成图必须为原图宽高的 2 倍或 3 倍。")
    digest = source_digest(source)
    low_depth = source_digest(source, four_bit=True)
    name = f"{width}x{height}-{digest}-{low_depth}.png"
    with storage.LOCK:
        path = slot_path(directory, name)
        if not path.is_file() and len(entries(host)) >= MAX_ATLASES:
            raise ValueError("最多挂载 24 张图片，请先移除一项。")
        storage.atomic(path, raw)
    return entries(host)
