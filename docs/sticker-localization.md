# Chinese stock sticker names

[简体中文](sticker-localization.zh-CN.md) · [Back to the feature index](../README.md)

`sticker_localization` displays reviewed names from the current stock sticker
baseline in Simplified Chinese. It changes display text only. Sticker images,
materials, dimensions, inventory, prices and player-authored content continue
through their existing paths.

## Use

Players receive the Mod through the server owner's client package or update.
In SDGO Mod Manager, enable **汉化原版贴纸名称** and restart the
client. The switch is independent and enabled by default. Turning it off and
restarting preserves the source sticker and category names.

The Mod requires `ui_localization` because that Mod already owns the verified
native GridData lookup hook. `sticker_localization` is a read-only text provider
and installs no second hook at the same site. `sticker_catalog` is optional. If
a loaded catalog owns a sticker, or its ownership answer is unavailable or
invalid, stock localization preserves the exact authored source name.

[Shop localization](shop-localization.md) delegates every current stock sticker
category to this provider. Each category keeps its own reviewed Chinese target;
a category target is never inferred as the linked native sticker name. A custom
product title remains higher priority. When this provider is disabled, absent or
declines an exact match, the shop keeps the source category name instead of
using the general shop dictionary.

## Coverage and matching

The current baseline contains 414 stock sticker rows: 286 reviewed ASCII names
containing an English letter, 127 non-ASCII names, and one incomplete ASCII
source, `" Sticker"`. The native phrase bank contains 298 exact ID-and-source
rows: all 286 reviewed ASCII-English rows plus 12 non-ASCII rows whose full
sticker source has one unambiguous reviewed Chinese name. Other native sticker
names, including ambiguous proper-name transliterations and the incomplete
source, remain unchanged.

The separate shop bank contains all 347 current family-17 category rows. Every
row is keyed by its category ID and complete category source, and records the
typed sticker reference, exact baseline sticker source, and that category's own
previously reviewed Chinese target. Shop lookup checks the exact category key
and catalog ownership of the exact linked sticker before returning this stored
category target. It never performs an ID-only, source-only, or global dictionary
fallback.

Human-readable native-name examples:

| Sticker ID | Exact source | Display result |
| ---: | --- | --- |
| `240005` | `Triangle Sticker` | `三角形贴纸` |
| `240358` | `삼국전 여포군 스티커` | `三国传吕布军贴纸` |
| `240009` | `즐 Sticker` | source text retained |
| `240335` | ` Sticker` | source text retained |
| `240005` | `Triangle Sticker?` | source text retained |

Exact shop-category examples:

```text
shop(category_id=1710005, source="즐 스티커",
     sticker_id=240009, sticker_source="즐 Sticker") -> "“兹尔”贴纸（音译）"
sticker(sticker_id=240009, source="즐 Sticker") -> no match

shop(category_id=1720156, source="龍 Sticker",
     sticker_id=240335, sticker_source=" Sticker") -> "龍贴纸"
sticker(sticker_id=240335, source=" Sticker") -> no match

shop(category_id=1720156, source="龍 Sticker?") -> no match
shop(category_id=1720156, source="龍 Sticker", ownership=owned) -> no match
shop(category_id=1720156, source="龍 Sticker", ownership=unknown) -> no match
```

A category's reviewed label does not authorize translating its linked native
sticker source. The two namespaces retain separate exact keys and targets. An
exact match with catalog ownership `owned` or `unknown` is refused in both
namespaces, protecting custom names that reuse stock IDs or wording.

## Implementation and verification boundary

The provider exports bounded process-local lookups. The existing
`ui_localization` dispatcher copies the complete 409-byte native sticker row,
replaces only the 50-wide-character name slot at bytes `5` through `104`, and
returns a stable cached copy. The original row and every other byte remain
unchanged. Shop display publishes the exact target stored in the matched category
projection after the typed sticker ownership guard passes.

The generated banks, schema refusal cases, exact dictionary lookups, provider
buffer semantics, ownership guards, category aliases, row-copy boundaries and
the existing system text path have focused offline tests. These checks establish
the implementation contract; in-game visual and gameplay acceptance are
recorded separately after a supported client demonstration.
