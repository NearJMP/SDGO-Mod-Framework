"""Bounded raster inspection and deterministic sticker PNG canonicalization.

All public entry points consume bytes supplied by the caller.  This module never
resolves paths, URLs, SVG references, or other external resources.
"""

from __future__ import annotations

import hashlib
import io
import math
from pathlib import PurePath
import re
import warnings
from typing import Any, Iterable

MAX_INPUT_BYTES = 16 * 1024 * 1024
MAX_CANONICAL_BYTES = 32 * 1024 * 1024
MAX_DIMENSION = 8192
MAX_PIXELS = 16_777_216
MAX_FRAMES = 64
MAX_DECODED_FRAME_PIXELS = 67_108_864

_CORE_FORMATS = ("PNG", "JPEG", "BMP", "GIF", "WEBP", "TIFF", "TGA", "DDS", "ICO")
_OPTIONAL_FORMATS = ("AVIF",)
_FORMAT_EXTENSIONS = {
    "PNG": frozenset({".png"}),
    "JPEG": frozenset({".jpg", ".jpeg"}),
    "BMP": frozenset({".bmp"}),
    "GIF": frozenset({".gif"}),
    "WEBP": frozenset({".webp"}),
    "TIFF": frozenset({".tif", ".tiff"}),
    "TGA": frozenset({".tga"}),
    "DDS": frozenset({".dds"}),
    "ICO": frozenset({".ico"}),
    "AVIF": frozenset({".avif"}),
    "HEIF": frozenset({".heic", ".heif"}),
    "SVG": frozenset({".svg"}),
}
_HEIF_BRANDS = frozenset(
    {
        b"heic",
        b"heix",
        b"heim",
        b"heis",
        b"hevc",
        b"hevx",
        b"hevm",
        b"hevs",
        b"mif1",
        b"msf1",
    }
)


def _pillow():
    try:
        from PIL import Image, ImageOps, UnidentifiedImageError
    except ImportError as error:  # pragma: no cover - the Manager build declares Pillow
        raise ValueError("Sticker image import requires Pillow>=11.1,<13") from error
    Image.init()
    return Image, ImageOps, UnidentifiedImageError


def codec_capabilities() -> dict[str, Any]:
    """Report the exact decoders available to the current Pillow build."""
    Image, _ImageOps, _UnidentifiedImageError = _pillow()
    opened = set(Image.OPEN)
    saved = set(Image.SAVE)
    formats = {}
    for name in (*_CORE_FORMATS, *_OPTIONAL_FORMATS):
        available = name in opened
        formats[name.lower()] = {
            "available": available,
            "decoder": available,
            "encoder": name in saved,
            "extensions": sorted(_FORMAT_EXTENSIONS[name]),
            "dependency": (
                "Pillow>=11.1,<13"
                if name in _CORE_FORMATS
                else "Pillow built with AVIF decoder support"
            ),
        }
    try:
        heif, heif_info = _heif_module()
        heif_available = bool(heif_info.get("decoders", {}).get("libde265"))
        heif_version = getattr(heif, "__version__", "unknown")
        heif_native = str(heif_info.get("libheif", "unknown"))
    except ValueError:
        heif_available = False
        heif_version = "unavailable"
        heif_native = "unavailable"
    formats["heic"] = {
        "available": heif_available,
        "decoder": heif_available,
        "encoder": False,
        "extensions": [".heic", ".heif"],
        "dependency": "pi-heif-decoder==1.7.0.post4 (decoder-only libheif/libde265)",
        "version": heif_version,
        "libheif": heif_native,
    }
    try:
        from . import svg

        svg_available = svg.available()
    except (ImportError, OSError):
        svg_available = False
    formats["svg"] = {
        "available": svg_available,
        "decoder": svg_available,
        "encoder": False,
        "extensions": [".svg"],
        "dependency": "resvg-py==0.5.0 after bounded shape-only XML reconstruction",
    }
    return {
        "dependency": "Pillow>=11.1,<13",
        "formats": formats,
        "limits": {
            "input_bytes": MAX_INPUT_BYTES,
            "canonical_bytes": MAX_CANONICAL_BYTES,
            "dimension": MAX_DIMENSION,
            "pixels": MAX_PIXELS,
            "frames": MAX_FRAMES,
            "decoded_frame_pixels": MAX_DECODED_FRAME_PIXELS,
        },
    }


def _source_extension(source_name: str | None) -> str | None:
    if source_name is None:
        return None
    if (
        not isinstance(source_name, str)
        or not source_name
        or len(source_name) > 240
        or any(ord(character) < 32 for character in source_name)
        or "/" in source_name
        or "\\" in source_name
        or ":" in source_name
    ):
        raise ValueError("source_name must be one bounded filename, not a path or URL")
    suffix = PurePath(source_name).suffix.lower()
    if not suffix:
        raise ValueError("source_name must include a supported image extension")
    known = {
        extension for values in _FORMAT_EXTENSIONS.values() for extension in values
    }
    if suffix not in known:
        raise ValueError(f"unsupported sticker image extension: {suffix}")
    return suffix


def _raw_bytes(
    raw: bytes | bytearray | memoryview,
    *,
    maximum: int = MAX_INPUT_BYTES,
) -> bytes:
    if not isinstance(raw, (bytes, bytearray, memoryview)):
        raise ValueError("sticker image must be supplied as bytes")
    value = bytes(raw)
    if not value or len(value) > maximum:
        raise ValueError(f"sticker image must be 1..{maximum} bytes")
    return value


def _size(width: Any, height: Any, label: str) -> tuple[int, int]:
    if (
        isinstance(width, bool)
        or isinstance(height, bool)
        or not isinstance(width, int)
        or not isinstance(height, int)
        or not 1 <= width <= MAX_DIMENSION
        or not 1 <= height <= MAX_DIMENSION
        or width * height > MAX_PIXELS
    ):
        raise ValueError(
            f"{label} must be within {MAX_DIMENSION}px and {MAX_PIXELS} pixels"
        )
    return width, height


def _format_for_extension(extension: str | None) -> str | None:
    if extension is None:
        return None
    for name, extensions in _FORMAT_EXTENSIONS.items():
        if extension in extensions:
            return name
    return None


def _heif_module():
    try:
        import pi_heif
        from pi_heif import options
    except (ImportError, OSError) as error:
        raise ValueError(
            "HEIF decoder is unavailable: pi-heif-decoder==1.7.0.post4 is required"
        ) from error
    if options.DISABLE_SECURITY_LIMITS is not False:
        raise ValueError("HEIF decoder security limits must remain enabled")
    pi_heif.register_heif_opener(
        thumbnails=False,
        depth_images=False,
        aux_images=False,
        decode_threads=1,
    )
    if (
        options.DISABLE_SECURITY_LIMITS is not False
        or options.THUMBNAILS is not False
        or options.DEPTH_IMAGES is not False
        or options.AUX_IMAGES is not False
        or options.DECODE_THREADS != 1
    ):
        raise ValueError("HEIF decoder safety options could not be enforced")
    try:
        info = pi_heif.libheif_info()
    except (ImportError, OSError, RuntimeError) as error:
        raise ValueError("HEIF decoder native libraries are unavailable") from error
    encoders = set(info.get("encoders", {})) - {"mask"}
    if encoders:
        raise ValueError("HEIF dependency must be a decoder-only build")
    if not info.get("decoders", {}).get("libde265"):
        raise ValueError("HEIF dependency does not provide the libde265 decoder")
    return pi_heif, info


def _is_heif(data: bytes) -> bool:
    return len(data) >= 12 and data[4:8] == b"ftyp" and data[8:12] in _HEIF_BRANDS


def _looks_like_svg(data: bytes) -> bool:
    sample = data[:256]
    if sample.startswith(b"\xef\xbb\xbf"):
        sample = sample[3:]
    sample = sample.lstrip(b" \t\r\n")
    return sample.startswith(b"<svg") and (
        len(sample) == 4 or sample[4:5] in b" \t\r\n>"
    )


def _inspection(
    data: bytes,
    source_name: str | None,
    detected: str,
    width: int,
    height: int,
    frame_count: int,
    rgba,
):
    alpha_minimum, alpha_maximum = rgba.getchannel("A").getextrema()
    common = math.gcd(width, height)
    return {
        "format": detected,
        "source_name": source_name,
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "source_bytes": len(data),
        "width": width,
        "height": height,
        "aspect_ratio": {"width": width // common, "height": height // common},
        "frame_count": frame_count,
        "selected_frame": 0,
        "frame_policy": "single-frame",
        "discarded_frames": 0,
        "has_transparency": alpha_minimum < 255,
        "alpha_range": [alpha_minimum, alpha_maximum],
    }


def _decode_heif(data: bytes, source_name: str | None):
    Image, ImageOps, _UnidentifiedImageError = _pillow()
    pi_heif, _info = _heif_module()
    try:
        source = pi_heif.open_heif(
            data,
            convert_hdr_to_8bit=True,
            bgr_mode=False,
            remove_stride=True,
            hdr_to_16bit=True,
        )
    except (EOFError, OSError, RuntimeError, SyntaxError, ValueError) as error:
        raise ValueError("HEIF image is corrupt or unsupported") from error
    frame_count = len(source)
    if isinstance(frame_count, bool) or not 1 <= frame_count <= MAX_FRAMES:
        raise ValueError("sticker image frame budget is exceeded")
    aggregate_pixels = 0
    for index in range(frame_count):
        frame_width, frame_height = _size(*source[index].size, "HEIF frame size")
        aggregate_pixels += frame_width * frame_height
    if aggregate_pixels > MAX_DECODED_FRAME_PIXELS:
        raise ValueError("sticker image frame budget is exceeded")
    if frame_count != 1:
        raise ValueError("multi-frame HEIF images are not supported")
    if source.primary_index != 0:
        raise ValueError("single-frame HEIF has an invalid primary image")
    try:
        source[0].load()
        decoded = source.to_pillow()
        decoded.load()
        decoded_width, decoded_height = _size(*decoded.size, "decoded HEIF image size")
        oriented = ImageOps.exif_transpose(decoded)
        if oriented is decoded:
            oriented = decoded.copy()
        decoded.close()
        width, height = _size(*oriented.size, "oriented HEIF image size")
        if decoded_width * decoded_height > MAX_PIXELS:
            raise ValueError("decoded HEIF image exceeds the pixel budget")
        rgba = oriented.convert("RGBA")
        oriented.close()
    except (EOFError, OSError, RuntimeError, SyntaxError, ValueError) as error:
        raise ValueError("HEIF image is corrupt or unsupported") from error
    return rgba, _inspection(
        data, source_name, "HEIF", width, height, frame_count, rgba
    )


def _decode_svg(data: bytes, source_name: str | None):
    Image, _ImageOps, UnidentifiedImageError = _pillow()
    from . import svg

    png, declared_width, declared_height = svg.render_svg(
        data, max_dimension=MAX_DIMENSION, max_pixels=MAX_PIXELS
    )
    try:
        with Image.open(io.BytesIO(png)) as source:
            if str(source.format or "").upper() != "PNG":
                raise ValueError("SVG renderer did not return PNG")
            source.load()
            width, height = _size(*source.size, "rendered SVG image size")
            if (width, height) != (declared_width, declared_height):
                raise ValueError(
                    "SVG renderer dimensions do not match validated dimensions"
                )
            rgba = source.convert("RGBA")
    except ValueError:
        raise
    except (UnidentifiedImageError, OSError, SyntaxError) as error:
        raise ValueError("SVG renderer returned an invalid image") from error
    return rgba, _inspection(data, source_name, "SVG", width, height, 1, rgba)


def _decode(
    raw: bytes | bytearray | memoryview,
    *,
    source_name: str | None,
    allow_gif_first_frame: bool,
    allow_animated_first_frame: bool,
    maximum_input_bytes: int = MAX_INPUT_BYTES,
):
    if (
        type(allow_gif_first_frame) is not bool
        or type(allow_animated_first_frame) is not bool
    ):
        raise ValueError("first-frame options must be booleans")
    Image, ImageOps, UnidentifiedImageError = _pillow()
    data = _raw_bytes(raw, maximum=maximum_input_bytes)
    extension = _source_extension(source_name)
    expected_format = _format_for_extension(extension)
    special_format = (
        "HEIF" if _is_heif(data) else "SVG" if _looks_like_svg(data) else None
    )
    if special_format is not None and expected_format not in {None, special_format}:
        raise ValueError(
            f"source extension {extension} does not match detected {special_format} image"
        )
    selected_special = (
        expected_format if expected_format in {"HEIF", "SVG"} else special_format
    )
    if selected_special == "HEIF":
        return _decode_heif(data, source_name)
    if selected_special == "SVG":
        return _decode_svg(data, source_name)
    if expected_format == "AVIF" and "AVIF" not in Image.OPEN:
        raise ValueError(
            "AVIF decoder is unavailable: Pillow must be built with AVIF support"
        )
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", Image.DecompressionBombWarning)
            with Image.open(io.BytesIO(data)) as probe:
                detected = str(probe.format or "").upper()
                supported = set(_CORE_FORMATS)
                if "AVIF" in Image.OPEN:
                    supported.add("AVIF")
                if detected not in supported:
                    raise ValueError(
                        f"unsupported sticker raster format: {detected or 'unknown'}"
                    )
                if expected_format is not None and expected_format != detected:
                    raise ValueError(
                        f"source extension {extension} does not match detected {detected} image"
                    )
                width, height = _size(*probe.size, "source image size")
                frame_count = getattr(probe, "n_frames", 1)
                if (
                    isinstance(frame_count, bool)
                    or not isinstance(frame_count, int)
                    or not 1 <= frame_count <= MAX_FRAMES
                    or width * height * frame_count > MAX_DECODED_FRAME_PIXELS
                ):
                    raise ValueError("sticker image frame budget is exceeded")
                first_frame_allowed = (detected == "GIF" and allow_gif_first_frame) or (
                    detected in {"GIF", "WEBP"} and allow_animated_first_frame
                )
                if frame_count > 1 and not first_frame_allowed:
                    if detected == "GIF":
                        raise ValueError(
                            "animated GIF import requires allow_gif_first_frame=True "
                            "or allow_animated_first_frame=True"
                        )
                    if detected == "WEBP":
                        raise ValueError(
                            "animated WEBP import requires allow_animated_first_frame=True"
                        )
                    raise ValueError(f"multi-frame {detected} images are not supported")
                probe.verify()
            with Image.open(io.BytesIO(data)) as source:
                source.seek(0)
                source.load()
                oriented = ImageOps.exif_transpose(source)
                if oriented is source:
                    oriented = source.copy()
                width, height = _size(*oriented.size, "oriented image size")
                rgba = oriented.convert("RGBA")
                oriented.close()
    except ValueError:
        raise
    except (Image.DecompressionBombWarning, Image.DecompressionBombError) as error:
        raise ValueError("sticker image exceeds the pixel decode limit") from error
    except (UnidentifiedImageError, OSError, SyntaxError) as error:
        raise ValueError("sticker image is corrupt or unsupported") from error

    inspection = _inspection(
        data, source_name, detected, width, height, frame_count, rgba
    )
    if frame_count > 1:
        inspection["frame_policy"] = "first-frame"
        inspection["discarded_frames"] = frame_count - 1
    return rgba, inspection


def inspect_raster(
    raw: bytes | bytearray | memoryview,
    *,
    source_name: str | None = None,
    allow_gif_first_frame: bool = False,
    allow_animated_first_frame: bool = False,
) -> dict[str, Any]:
    """Fully decode one bounded raster and return source-neutral metadata."""
    image, inspection = _decode(
        raw,
        source_name=source_name,
        allow_gif_first_frame=allow_gif_first_frame,
        allow_animated_first_frame=allow_animated_first_frame,
    )
    image.close()
    return inspection


def _rgba(value: Iterable[int]) -> tuple[int, int, int, int]:
    if not isinstance(value, (list, tuple)) or len(value) != 4:
        raise ValueError("padding_rgba must contain four integer channels")
    channels = []
    for channel in value:
        if (
            isinstance(channel, bool)
            or not isinstance(channel, int)
            or not 0 <= channel <= 255
        ):
            raise ValueError("padding_rgba channels must be integers in 0..255")
        channels.append(channel)
    return tuple(channels)  # type: ignore[return-value]


def _target_size(value: tuple[int, int] | list[int] | None) -> tuple[int, int] | None:
    if value is None:
        return None
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise ValueError("target_size must be [width, height] or None")
    return _size(value[0], value[1], "target image size")


def _contained_size(
    source: tuple[int, int], target: tuple[int, int]
) -> tuple[int, int]:
    source_width, source_height = source
    target_width, target_height = target
    if target_width * source_height <= target_height * source_width:
        width = target_width
        height = max(
            1, (source_height * target_width + source_width // 2) // source_width
        )
        return width, min(target_height, height)
    height = target_height
    width = max(1, (source_width * target_height + source_height // 2) // source_height)
    return min(target_width, width), height


def _render_canonical(
    image,
    inspection: dict[str, Any],
    *,
    target_size: tuple[int, int] | list[int] | None,
    padding_rgba: tuple[int, int, int, int] | list[int],
) -> dict[str, Any]:
    Image, _ImageOps, _UnidentifiedImageError = _pillow()
    target = _target_size(target_size) or image.size
    background = _rgba(padding_rgba)
    rendered_size = _contained_size(image.size, target)
    if rendered_size != image.size:
        # Premultiplied alpha prevents transparent RGB fringes during resampling.
        rendered = (
            image.convert("RGBa")
            .resize(rendered_size, Image.Resampling.LANCZOS)
            .convert("RGBA")
        )
    else:
        rendered = image.copy()
    image.close()
    left = (target[0] - rendered_size[0]) // 2
    top = (target[1] - rendered_size[1]) // 2
    right = target[0] - rendered_size[0] - left
    bottom = target[1] - rendered_size[1] - top
    canvas = Image.new("RGBA", target, background)
    canvas.alpha_composite(rendered, (left, top))
    rendered.close()
    stream = io.BytesIO()
    canvas.save(stream, "PNG", optimize=False, compress_level=9)
    png = stream.getvalue()
    alpha_minimum, alpha_maximum = canvas.getchannel("A").getextrema()
    canvas.close()
    if len(png) > MAX_CANONICAL_BYTES:
        raise ValueError(f"canonical sticker PNG exceeds {MAX_CANONICAL_BYTES} bytes")
    return {
        "png": png,
        "sha256": hashlib.sha256(png).hexdigest(),
        "byte_count": len(png),
        "width": target[0],
        "height": target[1],
        "has_transparency": alpha_minimum < 255,
        "alpha_range": [alpha_minimum, alpha_maximum],
        "source": inspection,
        "sizing": {
            "fit": "contain",
            "source_size": {
                "width": inspection["width"],
                "height": inspection["height"],
            },
            "output_size": {"width": target[0], "height": target[1]},
            "rendered_size": {
                "width": rendered_size[0],
                "height": rendered_size[1],
            },
            "offset": {"x": left, "y": top},
            "padding": {
                "left": left,
                "top": top,
                "right": right,
                "bottom": bottom,
            },
            "padding_rgba": list(background),
        },
    }


def canonicalize_raster(
    raw: bytes | bytearray | memoryview,
    *,
    source_name: str | None = None,
    allow_gif_first_frame: bool = False,
    allow_animated_first_frame: bool = False,
    target_size: tuple[int, int] | list[int] | None = None,
    padding_rgba: tuple[int, int, int, int] | list[int] = (0, 0, 0, 0),
) -> dict[str, Any]:
    """Return canonical RGBA PNG bytes plus deterministic contain/padding metadata.

    ``target_size=None`` preserves the decoded dimensions.  When a target is
    supplied, the image is resized without changing its aspect ratio and is
    centered on the requested RGBA padding color.
    """
    image, inspection = _decode(
        raw,
        source_name=source_name,
        allow_gif_first_frame=allow_gif_first_frame,
        allow_animated_first_frame=allow_animated_first_frame,
    )
    return _render_canonical(
        image,
        inspection,
        target_size=target_size,
        padding_rgba=padding_rgba,
    )


def validate_canonical_png(raw: bytes | bytearray | memoryview) -> dict[str, Any]:
    """Verify the exact metadata-free RGBA PNG form up to the canonical cap."""
    image, inspection = _decode(
        raw,
        source_name=None,
        allow_gif_first_frame=False,
        allow_animated_first_frame=False,
        maximum_input_bytes=MAX_CANONICAL_BYTES,
    )
    result = _render_canonical(
        image,
        inspection,
        target_size=None,
        padding_rgba=(0, 0, 0, 0),
    )
    if result["source"]["format"] != "PNG" or result["png"] != bytes(raw):
        raise ValueError("sticker asset is not canonical RGBA PNG")
    return result
