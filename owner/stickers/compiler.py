"""Compile neutral sticker projects into native kind-38 rows and sealed resources.

Image conversion is deliberately separate from artwork editing. The native row
contains no dimensions: the first-placement size lives in the catalog sidecar.
"""

from __future__ import annotations
import hashlib
import io
import json
import math
import re
import struct
from PIL import Image, ImageOps
from owner.native_archive import build_archive
from owner.units.assets import sprite_binary
from . import images

SCHEMA = "sdgo.sticker-definitions/1"
HOST_SHA256 = "e2dba73c10302b50cb0e6f2e58dd0a14effa8d224989ae570a7f8e05af79ff92"
MAX_ENTRIES = 2048
MAX_NATIVE_ARCHIVE_BYTES = 128 * 1024 * 1024
CONTENT_FILES = frozenset(
    {
        "catalog.bin",
        "definitions.json",
        "resource-overlay.ini",
        "resources.zdx",
        "resources.zpk",
    }
)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def json_bytes(value):
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False)
        + "\n"
    ).encode("utf-8")


def fnv(raw):
    value = 2166136261
    for byte in raw:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def size(value):
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
        or not 0.2 <= value <= 8
    ):
        raise ValueError("首次放置尺寸须在 0.2 到 8 之间")
    return float(value)


def paths(identity):
    if type(identity) is not int or not 1 <= identity <= 9999999:
        raise ValueError("原生纹章编号无效")
    return f"sprs/sdgo_stickers_{identity}.spr", f"txrs/sdgo_stickers_{identity}.txr"


def allowed_resource(path):
    return bool(
        re.fullmatch(
            r"(?:sprs/sdgo_stickers_[1-9][0-9]{0,6}\.spr|txrs/sdgo_stickers_[1-9][0-9]{0,6}\.txr)",
            path,
        )
    )


def _text(row, offset, capacity, text):
    if (
        not isinstance(text, str)
        or not text
        or any(
            ord(c) < 32
            or ord(c) == 127
            or 0xD800 <= ord(c) <= 0xDFFF
            or ord(c) > 0xFFFF
            for c in text
        )
    ):
        raise ValueError("原生纹章名称或路径包含不支持的字符")
    raw = text.encode("utf-16-le")
    if len(raw) >= capacity * 2:
        raise ValueError(f"原生纹章名称或路径最多 {capacity-1} 个字")
    row[offset : offset + capacity * 2] = raw.ljust(capacity * 2, b"\0")


def record(identity, name):
    sprite, texture = paths(identity)
    row = bytearray(409)
    struct.pack_into("<I", row, 0, identity)
    _text(row, 5, 50, name)
    _text(row, 109, 48, sprite.replace("/", "\\"))
    _text(row, 209, 100, texture.replace("/", "\\"))
    # UserLevel, group/faction limits and AllowUnitID stay unrestricted (zero).
    # StickerType=1 means an ordinary emblem, never the eraser type (2).
    row[108] = 1
    return bytes(row)


def category_record(identity, category_id, name):
    if type(category_id) is not int or not 1_790_000 <= category_id <= 1_799_999:
        raise ValueError("纹章物品类别编号无效")
    sprite, _ = paths(identity)
    row = bytearray(614)
    struct.pack_into("<I", row, 0, category_id)
    _text(row, 5, 50, name)
    struct.pack_into("<I", row, 105, identity)
    _text(row, 114, 50, sprite.replace("/", "\\"))
    return bytes(row)


def definition(project, image, identity, category_id=None):
    if not isinstance(project, dict) or not re.fullmatch(
        r"[0-9a-f]{32}", project.get("id", "")
    ):
        raise ValueError("纹章项目身份无效")
    inspected = images.validate_canonical_png(image)
    descriptor = project.get("image")
    if (
        not isinstance(descriptor, dict)
        or descriptor.get("sha256") != sha(image)
        or descriptor.get("bytes") != len(image)
        or (descriptor.get("width"), descriptor.get("height"))
        != (inspected["width"], inspected["height"])
    ):
        raise ValueError("项目图片与保存的描述不一致")
    settings = project.get("settings", {})
    if set(settings) - {"native_id", "default_width", "default_height"}:
        raise ValueError("存在未支持的原生纹章设置")
    if "native_id" in settings and settings["native_id"] != identity:
        raise ValueError("原生编号与项目设置不一致")
    if category_id is None:
        category_id = 1_790_000 + int(project["id"][8:16], 16) % 10_000
    name = project.get("display_name")
    record(identity, name)
    category_record(identity, category_id, name)
    return {
        "owner": project["id"],
        "id": identity,
        "category_id": category_id,
        "name": name,
        "image_sha256": sha(image),
        "width": size(settings.get("default_width", 2)),
        "height": size(settings.get("default_height", 2)),
    }


def validate_definitions(document):
    if (
        not isinstance(document, dict)
        or set(document) != {"schema", "entries"}
        or document["schema"] != SCHEMA
        or not isinstance(document["entries"], list)
        or len(document["entries"]) > MAX_ENTRIES
    ):
        raise ValueError("已安装纹章目录无效")
    result = []
    ids = set()
    owners = set()
    categories = set()
    for e in document["entries"]:
        if (
            not isinstance(e, dict)
            or set(e)
            != {"owner", "id", "category_id", "name", "image_sha256", "width", "height"}
            or not isinstance(e["owner"], str)
            or not re.fullmatch(r"[0-9a-f]{32}", e["owner"])
            or not isinstance(e["image_sha256"], str)
            or not re.fullmatch(r"[0-9a-f]{64}", e["image_sha256"])
        ):
            raise ValueError("已安装纹章定义无效")
        record(e["id"], e["name"])
        size(e["width"])
        size(e["height"])
        category_record(e["id"], e["category_id"], e["name"])
        if e["id"] in ids or e["category_id"] in categories or e["owner"] in owners:
            raise ValueError("纹章目录编号或项目重复")
        ids.add(e["id"])
        categories.add(e["category_id"])
        owners.add(e["owner"])
        result.append(dict(e))
    if ids & categories:
        raise ValueError("纹章和物品类别编号发生全局冲突")
    return {"schema": SCHEMA, "entries": sorted(result, key=lambda e: e["id"])}


def catalog(entries):
    entries = validate_definitions({"schema": SCHEMA, "entries": entries})["entries"]
    payload = b""
    for e in entries:
        row = record(e["id"], e["name"])
        item = category_record(e["id"], e["category_id"], e["name"])
        payload += (
            struct.pack(
                "<IIIff", e["id"], e["category_id"], fnv(row), e["width"], e["height"]
            )
            + row
            + item
        )
    return (
        struct.pack(
            "<8s6I", b"SDGOST1\0", 1, 32, len(entries), len(payload), fnv(payload), 0
        )
        + payload
    )


def textures(image, identity):
    images.validate_canonical_png(image)
    sprite, texture = paths(identity)
    with Image.open(io.BytesIO(image)) as source:
        # Square native material/thumbnail; contain preserves portrait proportions.
        contained = ImageOps.contain(
            source.convert("RGBA"), (512, 512), Image.Resampling.LANCZOS
        )
        canvas = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
        canvas.alpha_composite(
            contained, ((512 - contained.width) // 2, (512 - contained.height) // 2)
        )
        output = io.BytesIO()
        canvas.save(output, "PNG", optimize=False)
    canonical = images.canonicalize_raster(
        output.getvalue(), source_name="material.png"
    )["png"]
    return {sprite: sprite_binary(texture, 64, 64), texture: canonical}


def compile_content(entries, resources):
    doc = validate_definitions({"schema": SCHEMA, "entries": entries})
    expected = {path for e in doc["entries"] for path in paths(e["id"])}
    if set(resources) != expected:
        raise ValueError("纹章定义与图片资源未成对，拒绝编译")
    for e in doc["entries"]:
        sprite, texture = paths(e["id"])
        if resources[sprite] != sprite_binary(texture, 64, 64):
            raise ValueError("纹章缩略图与材质路径绑定无效")
        checked = images.validate_canonical_png(resources[texture])
        if (checked["width"], checked["height"]) != (512, 512):
            raise ValueError("原生纹章材质须为 512 × 512 透明 PNG")
    zdx, zpk, _ = build_archive(resources, allowed_resource)
    if len(zpk) > MAX_NATIVE_ARCHIVE_BYTES:
        raise ValueError("纹章原生资源超过 128 MiB，请减少项目数量或图片复杂度")
    overlay = (
        f"[runtime]\nschema=1\nindex=resources.zdx\narchive=resources.zpk\nindex_sha256={sha(zdx)}\narchive_sha256={sha(zpk)}\n"
    ).encode("ascii")
    return {
        "catalog.bin": catalog(doc["entries"]),
        "definitions.json": json_bytes(doc),
        "resource-overlay.ini": overlay,
        "resources.zdx": zdx,
        "resources.zpk": zpk,
    }


def server_plan(entry):
    validate_definitions({"schema": SCHEMA, "entries": [entry]})
    # Native UTF-16 display names stay in the client. The server's legacy
    # varchar field uses an ASCII token to survive non-Unicode SQL collations.
    row = {
        "StickerID": entry["id"],
        "ItemName": f'sdgo-sticker-{entry["id"]}',
        "WebFilename1": "",
        "WebFilename2": "",
        "UserLevel": 0,
        "AllowGroupInfluence": 0,
        "AllowInfluence": 0,
        "AllowUnitID": 0,
        "StickerType": 1,
    }
    payload = {
        "schema": "sdgo.sticker-server-plan/1",
        "owner": entry["owner"],
        "rows": [row],
        "categories": [
            {
                "CatalogueID": entry["category_id"],
                "Name": f'sdgo-sticker-{entry["id"]}',
                "RefIndex": entry["id"],
                "TimeLimit": 0,
                "Option1": 0,
                "Option2": 0,
            }
        ],
        "client_record_sha256": sha(record(entry["id"], entry["name"])),
        "client_category_sha256": sha(
            category_record(entry["id"], entry["category_id"], entry["name"])
        ),
        "client_presentation": {
            "image_sha256": entry["image_sha256"],
            "width": entry["width"],
            "height": entry["height"],
        },
        "credentials_included": False,
        "player_grants": False,
    }
    payload["revision"] = sha(json_bytes(payload))
    return payload
