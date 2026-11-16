"""Strict, source-neutral sticker package manifest protocol."""
from __future__ import annotations

import copy
import hashlib
import json
import math
import re
from collections.abc import Mapping
from typing import Any

from . import images


SCHEMA = "sdgo.owner-stickers/1"
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_STICKERS = 2048
MAX_TOTAL_ASSET_BYTES = 128 * 1024 * 1024
MAX_SETTING_DEPTH = 8
MAX_SETTING_NODES = 4096
MAX_SETTING_STRING = 8192
_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}")
_SHA256 = re.compile(r"[0-9a-f]{64}")
_PATH = re.compile(r"images/[a-z0-9][a-z0-9._-]{0,63}\.png")


def _exact_dict(
    value: Any,
    allowed: set[str],
    required: set[str],
    label: str,
) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    unknown = set(value) - allowed
    missing = required - set(value)
    if unknown:
        raise ValueError(f"{label} has unknown fields: {sorted(unknown)}")
    if missing:
        raise ValueError(f"{label} is missing fields: {sorted(missing)}")
    return value


def _bounded_text(value: Any, label: str, maximum: int) -> str:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > maximum
        or any(ord(character) < 32 for character in value)
    ):
        raise ValueError(f"{label} must be nonempty bounded text")
    return value


def _integer(value: Any, label: str, minimum: int, maximum: int) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not minimum <= value <= maximum
    ):
        raise ValueError(f"{label} must be in {minimum}..{maximum}")
    return value


def _sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or not _SHA256.fullmatch(value):
        raise ValueError(f"{label} must be a lowercase SHA-256")
    return value


def _rgba(value: Any, label: str) -> list[int]:
    if not isinstance(value, list) or len(value) != 4:
        raise ValueError(f"{label} must contain four channels")
    return [
        _integer(channel, f"{label} channel", 0, 255) for channel in value
    ]


def _size(value: Any, label: str) -> dict[str, int] | None:
    if value is None:
        return None
    raw = _exact_dict(value, {"width", "height"}, {"width", "height"}, label)
    width = _integer(raw["width"], f"{label} width", 1, images.MAX_DIMENSION)
    height = _integer(raw["height"], f"{label} height", 1, images.MAX_DIMENSION)
    if width * height > images.MAX_PIXELS:
        raise ValueError(f"{label} exceeds the pixel budget")
    return {"width": width, "height": height}


def normalize_presentation(value: Any, label: str = "presentation") -> dict[str, Any]:
    raw = _exact_dict(
        value,
        {"size", "fit", "padding_rgba"},
        {"size", "fit", "padding_rgba"},
        label,
    )
    if raw["fit"] != "contain":
        raise ValueError(f"{label} fit must preserve aspect ratio with contain")
    return {
        "size": _size(raw["size"], f"{label} size"),
        "fit": "contain",
        "padding_rgba": _rgba(raw["padding_rgba"], f"{label} padding_rgba"),
    }


def default_presentation() -> dict[str, Any]:
    """Preserve source dimensions and transparency until an owner edits the size."""
    return {
        "size": None,
        "fit": "contain",
        "padding_rgba": [0, 0, 0, 0],
    }


def _setting_value(value: Any, label: str, depth: int, budget: list[int]) -> Any:
    budget[0] += 1
    if budget[0] > MAX_SETTING_NODES:
        raise ValueError(f"{label} exceeds the settings node budget")
    if depth > MAX_SETTING_DEPTH:
        raise ValueError(f"{label} exceeds the settings depth limit")
    if value is None or isinstance(value, bool):
        return value
    if isinstance(value, int) and not isinstance(value, bool):
        if abs(value) > 10**15:
            raise ValueError(f"{label} integer is out of range")
        return value
    if isinstance(value, float):
        if not math.isfinite(value) or abs(value) > 10**15:
            raise ValueError(f"{label} number must be finite and bounded")
        return value
    if isinstance(value, str):
        if len(value) > MAX_SETTING_STRING or any(ord(character) < 32 for character in value):
            raise ValueError(f"{label} string is invalid")
        return value
    if isinstance(value, list):
        return [
            _setting_value(item, f"{label}[{index}]", depth + 1, budget)
            for index, item in enumerate(value)
        ]
    if isinstance(value, Mapping):
        normalized = {}
        for key, item in value.items():
            if (
                not isinstance(key, str)
                or not key
                or len(key) > 128
                or any(ord(character) < 32 for character in key)
            ):
                raise ValueError(f"{label} has an invalid key")
            normalized[key] = _setting_value(
                item, f"{label}.{key}", depth + 1, budget
            )
        return normalized
    raise ValueError(f"{label} contains a non-JSON value")


def normalize_settings(value: Any, label: str = "settings") -> dict[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    result = _setting_value(value, label, 0, [0])
    assert isinstance(result, dict)
    return result


def _image(value: Any, sticker_id: str) -> dict[str, Any]:
    raw = _exact_dict(
        value,
        {"path", "sha256", "bytes", "width", "height"},
        {"path", "sha256", "bytes", "width", "height"},
        f"sticker {sticker_id} image",
    )
    path = raw["path"]
    expected_path = f"images/{sticker_id}.png"
    if (
        not isinstance(path, str)
        or not _PATH.fullmatch(path)
        or path != expected_path
    ):
        raise ValueError(
            f"sticker {sticker_id} image path must be {expected_path}"
        )
    width = _integer(
        raw["width"],
        f"sticker {sticker_id} image width",
        1,
        images.MAX_DIMENSION,
    )
    height = _integer(
        raw["height"],
        f"sticker {sticker_id} image height",
        1,
        images.MAX_DIMENSION,
    )
    if width * height > images.MAX_PIXELS:
        raise ValueError(f"sticker {sticker_id} image exceeds the pixel budget")
    return {
        "path": path,
        "sha256": _sha256(raw["sha256"], f"sticker {sticker_id} image"),
        "bytes": _integer(
            raw["bytes"],
            f"sticker {sticker_id} image bytes",
            1,
            images.MAX_CANONICAL_BYTES,
        ),
        "width": width,
        "height": height,
    }


def validate_manifest(value: Any) -> dict[str, Any]:
    raw = _exact_dict(
        value,
        {"schema", "defaults", "stickers", "settings"},
        {"schema", "defaults", "stickers", "settings"},
        "sticker manifest",
    )
    if raw["schema"] != SCHEMA:
        raise ValueError("unsupported sticker manifest schema")
    defaults = _exact_dict(
        raw["defaults"],
        {"presentation", "settings"},
        {"presentation", "settings"},
        "sticker defaults",
    )
    stickers = raw["stickers"]
    if not isinstance(stickers, list) or len(stickers) > MAX_STICKERS:
        raise ValueError(f"sticker manifest exceeds {MAX_STICKERS} entries")
    normalized = []
    ids = set()
    paths = set()
    total_bytes = 0
    for index, value in enumerate(stickers):
        entry = _exact_dict(
            value,
            {"id", "display_name", "image", "presentation", "settings"},
            {"id", "display_name", "image", "presentation", "settings"},
            f"sticker {index}",
        )
        sticker_id = entry["id"]
        if not isinstance(sticker_id, str) or not _ID.fullmatch(sticker_id):
            raise ValueError(f"sticker {index} id is invalid")
        if sticker_id in ids:
            raise ValueError(f"duplicate sticker id: {sticker_id}")
        ids.add(sticker_id)
        image = _image(entry["image"], sticker_id)
        if image["path"] in paths:
            raise ValueError(f"duplicate sticker image path: {image['path']}")
        paths.add(image["path"])
        total_bytes += image["bytes"]
        if total_bytes > MAX_TOTAL_ASSET_BYTES:
            raise ValueError("sticker assets exceed the total byte budget")
        normalized.append(
            {
                "id": sticker_id,
                "display_name": _bounded_text(
                    entry["display_name"], f"sticker {sticker_id} display_name", 128
                ),
                "image": image,
                "presentation": (
                    None
                    if entry["presentation"] is None
                    else normalize_presentation(
                        entry["presentation"], f"sticker {sticker_id} presentation"
                    )
                ),
                "settings": normalize_settings(
                    entry["settings"], f"sticker {sticker_id} settings"
                ),
            }
        )
    normalized.sort(key=lambda entry: entry["id"])
    return {
        "schema": SCHEMA,
        "defaults": {
            "presentation": normalize_presentation(
                defaults["presentation"], "default presentation"
            ),
            "settings": normalize_settings(defaults["settings"], "default settings"),
        },
        "stickers": normalized,
        "settings": normalize_settings(raw["settings"], "manifest settings"),
    }


def image_descriptor(sticker_id: str, canonical: Mapping[str, Any]) -> dict[str, Any]:
    """Create one protocol descriptor from ``images.canonicalize_raster`` output."""
    if not isinstance(sticker_id, str) or not _ID.fullmatch(sticker_id):
        raise ValueError("sticker id is invalid")
    if not isinstance(canonical, Mapping):
        raise ValueError("canonical sticker result must be an object")
    required = {"png", "sha256", "byte_count", "width", "height"}
    if not required <= set(canonical):
        raise ValueError("canonical sticker result is incomplete")
    png = canonical["png"]
    if not isinstance(png, bytes):
        raise ValueError("canonical sticker PNG must be bytes")
    if hashlib.sha256(png).hexdigest() != canonical["sha256"]:
        raise ValueError("canonical sticker result hash does not match its bytes")
    inspected = images.validate_canonical_png(png)
    if (
        canonical["byte_count"] != len(png)
        or canonical["width"] != inspected["width"]
        or canonical["height"] != inspected["height"]
    ):
        raise ValueError("canonical sticker result metadata does not match its bytes")
    descriptor = {
        "path": f"images/{sticker_id}.png",
        "sha256": canonical["sha256"],
        "bytes": canonical["byte_count"],
        "width": canonical["width"],
        "height": canonical["height"],
    }
    return _image(descriptor, sticker_id)


def make_manifest(
    stickers: list[Mapping[str, Any]],
    *,
    presentation: Mapping[str, Any] | None = None,
    default_settings: Mapping[str, Any] | None = None,
    settings: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    return validate_manifest(
        {
            "schema": SCHEMA,
            "defaults": {
                "presentation": (
                    default_presentation() if presentation is None else presentation
                ),
                "settings": {} if default_settings is None else default_settings,
            },
            "stickers": stickers,
            "settings": {} if settings is None else settings,
        }
    )


def make_sticker(
    sticker_id: str,
    display_name: str,
    canonical: Mapping[str, Any],
    *,
    presentation: Mapping[str, Any] | None = None,
    settings: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    return {
        "id": sticker_id,
        "display_name": display_name,
        "image": image_descriptor(sticker_id, canonical),
        "presentation": presentation,
        "settings": {} if settings is None else copy.deepcopy(dict(settings)),
    }


def encode_manifest(value: Any) -> bytes:
    normalized = validate_manifest(value)
    raw = (
        json.dumps(
            normalized,
            ensure_ascii=False,
            sort_keys=True,
            indent=2,
            allow_nan=False,
        )
        + "\n"
    ).encode("utf-8")
    if len(raw) > MAX_MANIFEST_BYTES:
        raise ValueError("sticker manifest exceeds the byte budget")
    return raw


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"sticker manifest contains duplicate key: {key}")
        result[key] = value
    return result


def _invalid_constant(value):
    raise ValueError(f"sticker manifest contains invalid number: {value}")


def decode_manifest(raw: bytes | bytearray | memoryview) -> dict[str, Any]:
    if not isinstance(raw, (bytes, bytearray, memoryview)):
        raise ValueError("sticker manifest must be supplied as bytes")
    data = bytes(raw)
    if not data or len(data) > MAX_MANIFEST_BYTES:
        raise ValueError("sticker manifest byte length is invalid")
    try:
        text = data.decode("utf-8")
        value = json.loads(
            text,
            object_pairs_hook=_unique_object,
            parse_constant=_invalid_constant,
        )
    except UnicodeDecodeError as error:
        raise ValueError("sticker manifest must be UTF-8") from error
    except json.JSONDecodeError as error:
        raise ValueError("sticker manifest is invalid JSON") from error
    return validate_manifest(value)


def validate_payloads(
    manifest: Any,
    payloads: Mapping[str, bytes | bytearray | memoryview],
) -> dict[str, Any]:
    """Authenticate an in-memory package without resolving filesystem paths."""
    normalized = validate_manifest(manifest)
    if not isinstance(payloads, Mapping):
        raise ValueError("sticker payloads must be an in-memory path mapping")
    expected = {entry["image"]["path"]: entry for entry in normalized["stickers"]}
    if set(payloads) != set(expected):
        missing = sorted(set(expected) - set(payloads))
        extra = sorted(set(payloads) - set(expected))
        raise ValueError(f"sticker payload set mismatch; missing={missing}, extra={extra}")
    inspected = []
    total = 0
    for path in sorted(expected):
        raw = payloads[path]
        if not isinstance(raw, (bytes, bytearray, memoryview)):
            raise ValueError(f"sticker payload {path} must be bytes")
        data = bytes(raw)
        descriptor = expected[path]["image"]
        total += len(data)
        if total > MAX_TOTAL_ASSET_BYTES:
            raise ValueError("sticker payloads exceed the total byte budget")
        if len(data) != descriptor["bytes"]:
            raise ValueError(f"sticker payload {path} byte count does not match")
        if hashlib.sha256(data).hexdigest() != descriptor["sha256"]:
            raise ValueError(f"sticker payload {path} SHA-256 does not match")
        result = images.validate_canonical_png(data)
        if (result["width"], result["height"]) != (
            descriptor["width"], descriptor["height"]
        ):
            raise ValueError(f"sticker payload {path} dimensions do not match")
        inspected.append(
            {
                "id": expected[path]["id"],
                "path": path,
                "sha256": descriptor["sha256"],
                "bytes": len(data),
                "width": result["width"],
                "height": result["height"],
            }
        )
    return {
        "schema": SCHEMA,
        "sticker_count": len(inspected),
        "asset_bytes": total,
        "assets": inspected,
    }
