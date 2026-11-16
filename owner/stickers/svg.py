"""Strict, self-contained SVG validation and bounded rasterization.

The renderer is never given caller XML.  We parse a small shape-only subset,
validate every element and attribute, rebuild a new SVG document, and only then
pass the reconstructed string to resvg with fonts and external resources off.
"""

from __future__ import annotations

import math
import re
import xml.etree.ElementTree as ElementTree

from defusedxml import ElementTree as DefusedElementTree

SVG_NAMESPACE = "http://www.w3.org/2000/svg"
MAX_SVG_BYTES = 16 * 1024 * 1024
MAX_SVG_NODES = 512
MAX_SVG_DEPTH = 32
MAX_SVG_ATTRIBUTES = 4096
MAX_SVG_ATTRIBUTE_BYTES = 256 * 1024
MAX_SVG_PATH_BYTES = 64 * 1024
MAX_SVG_PATH_TOKENS = 32768
MAX_SVG_COORDINATE = 1_000_000.0

_NUMBER_TEXT = r"[+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?"
_NUMBER = re.compile(_NUMBER_TEXT)
_INTEGER_SIZE = re.compile(r"(?:0|[1-9]\d{0,4})(?:px)?", re.IGNORECASE)
_PATH = re.compile(rf"(?:[MmZzLlHhVvCcSsQqTtAa]|{_NUMBER_TEXT}|[\s,])*\Z")
_POINTS = re.compile(rf"(?:{_NUMBER_TEXT}|[\s,])+\Z")
_PAINT = re.compile(
    r"(?:none|transparent|[A-Za-z]{1,32}|#[0-9A-Fa-f]{3,8}|"
    r"rgba?\([0-9.,%+\-\s]+\)|hsla?\([0-9.,%+\-\s]+\))\Z"
)
_PRESERVE_ASPECT = re.compile(
    r"(?:none|x(?:Min|Mid|Max)Y(?:Min|Mid|Max)(?:\s+(?:meet|slice))?)\Z"
)

_COMMON_ATTRIBUTES = frozenset(
    {
        "fill",
        "fill-opacity",
        "fill-rule",
        "opacity",
        "stroke",
        "stroke-linecap",
        "stroke-linejoin",
        "stroke-miterlimit",
        "stroke-opacity",
        "stroke-width",
        "transform",
    }
)
_TAG_ATTRIBUTES = {
    "svg": frozenset({"width", "height", "viewBox", "preserveAspectRatio"})
    | _COMMON_ATTRIBUTES,
    "g": _COMMON_ATTRIBUTES,
    "path": frozenset({"d"}) | _COMMON_ATTRIBUTES,
    "rect": frozenset({"x", "y", "width", "height", "rx", "ry"}) | _COMMON_ATTRIBUTES,
    "circle": frozenset({"cx", "cy", "r"}) | _COMMON_ATTRIBUTES,
    "ellipse": frozenset({"cx", "cy", "rx", "ry"}) | _COMMON_ATTRIBUTES,
    "line": frozenset({"x1", "y1", "x2", "y2"}) | _COMMON_ATTRIBUTES,
    "polyline": frozenset({"points"}) | _COMMON_ATTRIBUTES,
    "polygon": frozenset({"points"}) | _COMMON_ATTRIBUTES,
}
_NUMERIC_ATTRIBUTES = frozenset(
    {
        "x",
        "y",
        "x1",
        "y1",
        "x2",
        "y2",
        "cx",
        "cy",
        "r",
        "rx",
        "ry",
        "width",
        "height",
        "stroke-width",
        "stroke-miterlimit",
        "opacity",
        "fill-opacity",
        "stroke-opacity",
    }
)
_NONNEGATIVE_ATTRIBUTES = frozenset(
    {"r", "rx", "ry", "width", "height", "stroke-width", "stroke-miterlimit"}
)
_UNIT_INTERVAL_ATTRIBUTES = frozenset({"opacity", "fill-opacity", "stroke-opacity"})


def available() -> bool:
    try:
        import resvg_py  # noqa: F401
    except (ImportError, OSError):
        return False
    return True


def _local_name(value: str, label: str) -> str:
    if value.startswith("{"):
        namespace, separator, local = value[1:].partition("}")
        if not separator or namespace != SVG_NAMESPACE:
            raise ValueError(f"SVG {label} uses an unsupported namespace")
        return local
    if ":" in value:
        raise ValueError(f"SVG {label} uses an unsupported namespace")
    return value


def _finite_numbers(
    value: str, label: str, *, maximum_count: int = MAX_SVG_PATH_TOKENS
) -> list[float]:
    matches = list(_NUMBER.finditer(value))
    if len(matches) > maximum_count:
        raise ValueError(f"SVG {label} exceeds the numeric token limit")
    numbers = []
    for match in matches:
        number = float(match.group(0))
        if not math.isfinite(number) or abs(number) > MAX_SVG_COORDINATE:
            raise ValueError(f"SVG {label} contains an out-of-range number")
        numbers.append(number)
    return numbers


def _single_number(value: str, label: str) -> float:
    if not re.fullmatch(_NUMBER_TEXT, value.strip()):
        raise ValueError(f"SVG {label} must be one finite number")
    return _finite_numbers(value, label, maximum_count=1)[0]


def _root_size(value: str, label: str) -> int:
    compact = value.strip()
    if not _INTEGER_SIZE.fullmatch(compact):
        raise ValueError(f"SVG {label} must be an integer CSS pixel size")
    number = int(compact[:-2] if compact.lower().endswith("px") else compact)
    if number < 1:
        raise ValueError(f"SVG {label} must be positive")
    return number


def _validate_transform(value: str) -> None:
    position = 0
    count = 0
    function = re.compile(
        r"\s*(matrix|translate|scale|rotate|skewX|skewY)\s*\(([^()]*)\)\s*,?"
    )
    limits = {
        "matrix": (6, 6),
        "translate": (1, 2),
        "scale": (1, 2),
        "rotate": (1, 3),
        "skewX": (1, 1),
        "skewY": (1, 1),
    }
    while position < len(value):
        match = function.match(value, position)
        if match is None:
            raise ValueError("SVG transform uses unsupported syntax")
        arguments = match.group(2)
        if not re.fullmatch(rf"(?:{_NUMBER_TEXT}|[\s,])+", arguments):
            raise ValueError("SVG transform uses unsupported syntax")
        numbers = _finite_numbers(arguments, "transform", maximum_count=6)
        minimum, maximum = limits[match.group(1)]
        if not minimum <= len(numbers) <= maximum:
            raise ValueError("SVG transform has an invalid argument count")
        count += len(numbers)
        if count > 128:
            raise ValueError("SVG transform exceeds the numeric token limit")
        position = match.end()
    if count == 0:
        raise ValueError("SVG transform cannot be empty")


def _validate_path(value: str) -> None:
    if not value or len(value.encode("utf-8")) > MAX_SVG_PATH_BYTES:
        raise ValueError("SVG path data exceeds the byte limit")
    if not _PATH.fullmatch(value) or not re.search(r"[Mm]", value):
        raise ValueError("SVG path data uses unsupported syntax")
    commands = re.findall(r"[MmZzLlHhVvCcSsQqTtAa]", value)
    numbers = _finite_numbers(value, "path data")
    if len(commands) + len(numbers) > MAX_SVG_PATH_TOKENS:
        raise ValueError("SVG path data exceeds the token limit")


def _validate_points(value: str) -> None:
    if (
        not value
        or len(value.encode("utf-8")) > MAX_SVG_PATH_BYTES
        or not _POINTS.fullmatch(value)
    ):
        raise ValueError("SVG points use unsupported syntax")
    numbers = _finite_numbers(value, "points")
    if len(numbers) < 4 or len(numbers) % 2:
        raise ValueError("SVG points must contain coordinate pairs")


def _validate_attribute(tag: str, name: str, value: str, *, root: bool) -> None:
    if name not in _TAG_ATTRIBUTES[tag]:
        raise ValueError(f"SVG attribute {name!r} is unsupported on {tag}")
    if not value or any(
        ord(character) < 32 and character not in "\t\r\n" for character in value
    ):
        raise ValueError(f"SVG attribute {name!r} is invalid")
    lowered = value.casefold()
    if (
        "url(" in lowered
        or "@import" in lowered
        or "javascript:" in lowered
        or "data:" in lowered
    ):
        raise ValueError("SVG external and embedded resource references are forbidden")
    if name in {"fill", "stroke"}:
        if not _PAINT.fullmatch(value.strip()):
            raise ValueError(f"SVG {name} uses unsupported paint syntax")
        return
    if name in {"fill-rule"} and value not in {"nonzero", "evenodd"}:
        raise ValueError("SVG fill-rule is unsupported")
    if name == "stroke-linecap" and value not in {"butt", "round", "square"}:
        raise ValueError("SVG stroke-linecap is unsupported")
    if name == "stroke-linejoin" and value not in {"miter", "round", "bevel"}:
        raise ValueError("SVG stroke-linejoin is unsupported")
    if name == "preserveAspectRatio":
        if not _PRESERVE_ASPECT.fullmatch(value.strip()):
            raise ValueError("SVG preserveAspectRatio is unsupported")
        return
    if name == "transform":
        _validate_transform(value)
        return
    if name == "d":
        _validate_path(value)
        return
    if name == "points":
        _validate_points(value)
        return
    if name == "viewBox":
        if not re.fullmatch(rf"(?:{_NUMBER_TEXT}|[\s,])+", value):
            raise ValueError("SVG viewBox uses unsupported syntax")
        numbers = _finite_numbers(value, "viewBox", maximum_count=4)
        if len(numbers) != 4 or numbers[2] <= 0 or numbers[3] <= 0:
            raise ValueError("SVG viewBox must contain x y width height")
        return
    if name in _NUMERIC_ATTRIBUTES:
        if root and name in {"width", "height"}:
            _root_size(value, name)
            return
        number = _single_number(value, name)
        if name in _NONNEGATIVE_ATTRIBUTES and number < 0:
            raise ValueError(f"SVG {name} cannot be negative")
        if name in _UNIT_INTERVAL_ATTRIBUTES and not 0 <= number <= 1:
            raise ValueError(f"SVG {name} must be within 0..1")


def _dimensions(
    root_attributes: dict[str, str], *, max_dimension: int, max_pixels: int
) -> tuple[int, int]:
    width_text = root_attributes.get("width")
    height_text = root_attributes.get("height")
    if (width_text is None) != (height_text is None):
        raise ValueError("SVG width and height must be supplied together")
    if width_text is not None:
        width = _root_size(width_text, "width")
        height = _root_size(height_text or "", "height")
    else:
        view_box = root_attributes.get("viewBox")
        if view_box is None:
            raise ValueError("SVG requires bounded width and height or a viewBox")
        values = _finite_numbers(view_box, "viewBox", maximum_count=4)
        width_float, height_float = values[2], values[3]
        if (
            width_float < 1
            or height_float < 1
            or not width_float.is_integer()
            or not height_float.is_integer()
        ):
            raise ValueError(
                "SVG viewBox-only dimensions must be positive integer pixels"
            )
        width, height = int(width_float), int(height_float)
    if width > max_dimension or height > max_dimension or width * height > max_pixels:
        raise ValueError(
            f"SVG dimensions must be within {max_dimension}px and {max_pixels} pixels"
        )
    return width, height


def sanitize_svg(
    raw: bytes, *, max_dimension: int, max_pixels: int
) -> tuple[str, int, int]:
    if not raw or len(raw) > MAX_SVG_BYTES:
        raise ValueError(f"SVG source must be 1..{MAX_SVG_BYTES} bytes")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("SVG source must be UTF-8") from error
    if "\x00" in text or "<!" in text or "<?" in text:
        raise ValueError(
            "SVG declarations, entities, comments, and processing instructions are forbidden"
        )
    try:
        root = DefusedElementTree.fromstring(
            text,
            forbid_dtd=True,
            forbid_entities=True,
            forbid_external=True,
        )
    except Exception as error:
        raise ValueError(
            "SVG source is malformed or uses forbidden XML features"
        ) from error

    counters = {"nodes": 0, "attributes": 0, "attribute_bytes": 0}

    def rebuild(element, depth: int):
        if depth > MAX_SVG_DEPTH:
            raise ValueError("SVG nesting depth is exceeded")
        tag = _local_name(element.tag, "element")
        if tag not in _TAG_ATTRIBUTES or (tag == "svg" and depth != 1):
            raise ValueError(f"SVG element {tag!r} is unsupported")
        if (element.text and element.text.strip()) or (
            element.tail and element.tail.strip()
        ):
            raise ValueError("SVG text content is unsupported")
        counters["nodes"] += 1
        if counters["nodes"] > MAX_SVG_NODES:
            raise ValueError("SVG node count is exceeded")
        rebuilt_attributes: dict[str, str] = {}
        for raw_name, raw_value in element.attrib.items():
            name = _local_name(raw_name, "attribute")
            counters["attributes"] += 1
            counters["attribute_bytes"] += len(name.encode("utf-8")) + len(
                raw_value.encode("utf-8")
            )
            if (
                counters["attributes"] > MAX_SVG_ATTRIBUTES
                or counters["attribute_bytes"] > MAX_SVG_ATTRIBUTE_BYTES
            ):
                raise ValueError("SVG attribute budget is exceeded")
            _validate_attribute(tag, name, raw_value, root=depth == 1)
            rebuilt_attributes[name] = raw_value.strip()
        rebuilt = ElementTree.Element(f"{{{SVG_NAMESPACE}}}{tag}")
        for name in sorted(rebuilt_attributes):
            rebuilt.set(name, rebuilt_attributes[name])
        for child in list(element):
            rebuilt.append(rebuild(child, depth + 1))
        return rebuilt

    rebuilt_root = rebuild(root, 1)
    if _local_name(root.tag, "root") != "svg":
        raise ValueError("SVG root element must be svg")
    width, height = _dimensions(
        dict(rebuilt_root.attrib), max_dimension=max_dimension, max_pixels=max_pixels
    )
    ElementTree.register_namespace("", SVG_NAMESPACE)
    sanitized = ElementTree.tostring(
        rebuilt_root, encoding="unicode", short_empty_elements=True
    )
    return sanitized, width, height


def render_svg(
    raw: bytes, *, max_dimension: int, max_pixels: int
) -> tuple[bytes, int, int]:
    sanitized, width, height = sanitize_svg(
        raw, max_dimension=max_dimension, max_pixels=max_pixels
    )
    try:
        import resvg_py
    except (ImportError, OSError) as error:
        raise ValueError(
            "SVG decoder is unavailable: resvg-py==0.5.0 is required"
        ) from error
    try:
        png = resvg_py.svg_to_bytes(
            svg_string=sanitized,
            background=None,
            skip_system_fonts=True,
            width=width,
            height=height,
            dpi=96.0,
            style_sheet=None,
            resources_dir=None,
            font_files=[],
            font_dirs=[],
        )
    except Exception as error:
        raise ValueError("SVG shape rendering failed") from error
    if not isinstance(png, bytes) or not png:
        raise ValueError("SVG shape rendering produced no image")
    return png, width, height
