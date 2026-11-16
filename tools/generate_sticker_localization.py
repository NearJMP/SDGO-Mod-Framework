#!/usr/bin/env python3
"""Validate the public reviewed stock sticker bank and emit its C++ lookup tables.

The inputs contain neutral stock IDs and display text only. They never contain
native row payloads, images, or source-specific extraction logic.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NAMES = ROOT / "mods" / "sticker_localization" / "names.json"
DEFAULT_SHOP = ROOT / "mods" / "sticker_localization" / "shop-categories.json"
DEFAULT_OUTPUT = ROOT / "mods" / "sticker_localization" / "dictionary.generated.h"
HEX64 = re.compile(r"[0-9a-f]{64}")
ASCII_ENGLISH = re.compile(r"[A-Za-z]")
CJK = re.compile(r"[\u3400-\u9fff\uf900-\ufaff]")
INCOMPLETE_ASCII_SOURCE = " Sticker"
ENGLISH_RULE = (
    'ASCII string containing A-Z or a-z, excluding exact incomplete source " Sticker"'
)


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def _utf16_units(value: str) -> int:
    return len(value.encode("utf-16-le")) // 2


def _cpp_wide(value: str) -> str:
    out: list[str] = ['L"']
    for char in value:
        code = ord(char)
        if char == "\\":
            out.append("\\\\")
        elif char == '"':
            out.append('\\"')
        elif char == "\n":
            out.append("\\n")
        elif char == "\r":
            out.append("\\r")
        elif char == "\t":
            out.append("\\t")
        elif 0x20 <= code <= 0x7E:
            out.append(char)
        elif code <= 0xFFFF:
            out.append(f"\\u{code:04X}")
        else:
            out.append(f"\\U{code:08X}")
    out.append('"')
    return "".join(out)


def _validate_names(value: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    if set(value) != {"schema_version", "baseline", "entries"} or value["schema_version"] != 1:
        raise ValueError("names.json has an unsupported schema")
    baseline = value["baseline"]
    if not isinstance(baseline, dict) or set(baseline) != {
        "index_sha256", "gdt_sha256", "record_size", "total_count",
        "english_rule", "english_count", "non_ascii_count",
        "reviewed_non_ascii_count", "excluded_incomplete_ascii_count",
    }:
        raise ValueError("names.json baseline metadata is incomplete")
    if not HEX64.fullmatch(str(baseline["index_sha256"])) or not HEX64.fullmatch(str(baseline["gdt_sha256"])):
        raise ValueError("baseline hashes must be lowercase SHA-256")
    if (baseline["record_size"] != 409 or baseline["total_count"] != 414 or
            baseline["english_rule"] != ENGLISH_RULE or
            baseline["english_count"] != 286 or baseline["non_ascii_count"] != 127 or
            baseline["reviewed_non_ascii_count"] != 12 or
            baseline["excluded_incomplete_ascii_count"] != 1):
        raise ValueError("baseline sticker layout/count differs from the reviewed bank")
    entries = value["entries"]
    expected_count = baseline["english_count"] + baseline["reviewed_non_ascii_count"]
    if not isinstance(entries, list) or len(entries) != expected_count:
        raise ValueError("the reviewed sticker bank must contain exactly 298 rows")
    seen: set[int] = set()
    previous = 0
    english_count = 0
    non_ascii_count = 0
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict) or set(entry) != {"id", "source", "target"}:
            raise ValueError(f"sticker entry {index} has unsupported fields")
        identity, source, target = entry["id"], entry["source"], entry["target"]
        if not isinstance(identity, int) or not 240001 <= identity <= 240414 or identity in seen or identity <= previous:
            raise ValueError(f"sticker entry {index} has an invalid or unsorted ID")
        if (not isinstance(source, str) or not source or "\0" in source or
                source == INCOMPLETE_ASCII_SOURCE):
            raise ValueError(f"sticker {identity} source is not a reviewed bounded stock name")
        if source.isascii():
            if not ASCII_ENGLISH.search(source):
                raise ValueError(f"sticker {identity} source is not a reviewed bounded stock name")
            english_count += 1
        else:
            non_ascii_count += 1
        if not isinstance(target, str) or not target or "\0" in target or not CJK.search(target):
            raise ValueError(f"sticker {identity} target is not reviewed Chinese text")
        if _utf16_units(source) >= 50 or _utf16_units(target) >= 50:
            raise ValueError(f"sticker {identity} text exceeds the native 50-wchar field")
        seen.add(identity)
        previous = identity
    if (english_count != baseline["english_count"] or
            non_ascii_count != baseline["reviewed_non_ascii_count"]):
        raise ValueError("reviewed sticker source classes differ from baseline metadata")
    return baseline, entries


def _validate_shop(value: dict[str, Any], baseline: dict[str, Any], names: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if (set(value) != {"schema_version", "baseline_index_sha256",
                       "baseline_gdt_sha256", "entries"} or
            value["schema_version"] != 2):
        raise ValueError("shop-categories.json has an unsupported schema")
    if (value["baseline_index_sha256"] != baseline["index_sha256"] or
            value["baseline_gdt_sha256"] != baseline["gdt_sha256"]):
        raise ValueError("shop/category projection was not built from the reviewed baseline")
    translated_by_id = {entry["id"]: entry for entry in names}
    entries = value["entries"]
    if not isinstance(entries, list) or len(entries) != 347:
        raise ValueError("the exact reviewed shop/category bank must contain 347 rows")
    seen: set[tuple[int, str]] = set()
    sticker_sources: dict[int, str] = {}
    previous: tuple[int, str] | None = None
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict) or set(entry) != {
                "category_id", "source", "sticker_id", "sticker_source", "target"}:
            raise ValueError(f"shop projection {index} has unsupported fields")
        category = entry["category_id"]
        source = entry["source"]
        sticker = entry["sticker_id"]
        sticker_source = entry["sticker_source"]
        target = entry["target"]
        if not isinstance(category, int) or category // 100000 != 17:
            raise ValueError(f"shop projection {index} is not a sticker category")
        if not isinstance(source, str) or not source or "\0" in source or _utf16_units(source) >= 50:
            raise ValueError(f"shop category {category} source exceeds the native field")
        if (not isinstance(sticker, int) or not 240001 <= sticker <= 240414 or
                not isinstance(sticker_source, str) or not sticker_source or
                "\0" in sticker_source or _utf16_units(sticker_source) >= 50):
            raise ValueError(f"shop category {category} has an invalid typed sticker reference")
        if (not isinstance(target, str) or not target or "\0" in target or
                not CJK.search(target) or _utf16_units(target) >= 50):
            raise ValueError(f"shop category {category} target is not reviewed Chinese text")
        prior_source = sticker_sources.setdefault(sticker, sticker_source)
        if prior_source != sticker_source:
            raise ValueError(f"shop category {category} conflicts with another source for sticker {sticker}")
        translated = translated_by_id.get(sticker)
        if translated is not None and translated["source"] != sticker_source:
            raise ValueError(f"shop category {category} does not exactly link to the reviewed sticker source")
        key = (category, source)
        if key in seen or previous is not None and key <= previous:
            raise ValueError(f"shop projection {index} is duplicated or unsorted")
        seen.add(key)
        previous = key
    return entries


def _render(baseline: dict[str, Any], names: list[dict[str, Any]], shop: list[dict[str, Any]]) -> str:
    lines = [
        "// Generated by tools/generate_sticker_localization.py. Do not edit by hand.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace sdgo::sticker_localization::generated {",
        "",
        "struct StickerPhrase { std::uint32_t id; std::wstring_view source; std::wstring_view target; };",
        "struct ShopProjection { std::uint32_t category_id; std::wstring_view source; std::uint32_t sticker_id; std::wstring_view sticker_source; std::wstring_view target; };",
        "",
        f'inline constexpr char kBaselineIndexSha256[] = "{baseline["index_sha256"]}";',
        f'inline constexpr char kBaselineGdtSha256[] = "{baseline["gdt_sha256"]}";',
        f"inline constexpr std::uint32_t kBaselineRecordBytes = {baseline['record_size']}u;",
        f"inline constexpr std::uint32_t kBaselineStickerCount = {baseline['total_count']}u;",
        f"inline constexpr std::uint32_t kBaselineEnglishCount = {baseline['english_count']}u;",
        f"inline constexpr std::uint32_t kBaselineNonAsciiCount = {baseline['non_ascii_count']}u;",
        f"inline constexpr std::uint32_t kReviewedNonAsciiCount = {baseline['reviewed_non_ascii_count']}u;",
        f"inline constexpr std::uint32_t kExcludedIncompleteAsciiCount = {baseline['excluded_incomplete_ascii_count']}u;",
        "",
        f"inline constexpr std::array<StickerPhrase, {len(names)}> kStickerPhrases = {{{{",
    ]
    for entry in names:
        lines.append(f"    {{{entry['id']}u, {_cpp_wide(entry['source'])}, {_cpp_wide(entry['target'])}}},")
    lines.extend(["}};", "", f"inline constexpr std::array<ShopProjection, {len(shop)}> kShopProjections = {{{{"])
    for entry in shop:
        lines.append(
            f"    {{{entry['category_id']}u, {_cpp_wide(entry['source'])}, "
            f"{entry['sticker_id']}u, {_cpp_wide(entry['sticker_source'])}, "
            f"{_cpp_wide(entry['target'])}}},"
        )
    lines.extend(["}};", "", "} // namespace sdgo::sticker_localization::generated", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--names", type=Path, default=DEFAULT_NAMES)
    parser.add_argument("--shop", type=Path, default=DEFAULT_SHOP)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    baseline, names = _validate_names(_load(args.names))
    shop = _validate_shop(_load(args.shop), baseline, names)
    rendered = _render(baseline, names, shop)
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            raise SystemExit("generated sticker localization header is stale")
    else:
        args.output.write_text(rendered, encoding="utf-8")
    print(
        f"validated {len(names)} sticker names "
        f"({baseline['english_count']} ASCII-English and "
        f"{baseline['reviewed_non_ascii_count']} reviewed non-ASCII) and "
        f"{len(shop)} exact shop projections"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
