"""Source-neutral sticker import and package protocol."""

from .images import (
    MAX_CANONICAL_BYTES,
    MAX_DIMENSION,
    MAX_FRAMES,
    MAX_INPUT_BYTES,
    MAX_PIXELS,
    canonicalize_raster,
    codec_capabilities,
    inspect_raster,
    validate_canonical_png,
)
from .protocol import (
    SCHEMA,
    decode_manifest,
    default_presentation,
    encode_manifest,
    image_descriptor,
    make_manifest,
    make_sticker,
    normalize_presentation,
    normalize_settings,
    validate_manifest,
    validate_payloads,
)

__all__ = [
    "MAX_CANONICAL_BYTES",
    "MAX_DIMENSION",
    "MAX_FRAMES",
    "MAX_INPUT_BYTES",
    "MAX_PIXELS",
    "SCHEMA",
    "canonicalize_raster",
    "codec_capabilities",
    "decode_manifest",
    "default_presentation",
    "encode_manifest",
    "image_descriptor",
    "inspect_raster",
    "make_manifest",
    "make_sticker",
    "normalize_presentation",
    "normalize_settings",
    "validate_canonical_png",
    "validate_manifest",
    "validate_payloads",
]
