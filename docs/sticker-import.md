# Custom sticker import

Use **Mod Manager → Owner tools → Custom stickers** to turn an image into a native emblem. A project holds its name, transparent image and first-placement size. The generated client resources and server definitions form a pair; original emblems are preserved.

Baseline Chinese sticker names are a [separate Mod](sticker-localization.md). Authored names remain under the author's control.

## Create and apply

1. Create a project or choose the bundled public **大肥鱼** example.
2. Set its name and select an existing client and WorldServer in Manager. Import the image and inspect its proportions and transparency. Animated inputs require explicit first-frame acknowledgement.
3. Save the first-placement dimensions. They control the initial free-layer placement on the model; subsequent in-game editing remains available. Authoring-preview pixel dimensions are separate.
4. Run native preflight to check the supported client, identifiers, resources and server ownership. The client checks the native registry again at startup.
5. Close the selected client and install the client content. Review and apply the matching server definition while the service group is stopped, then restart normally and verify readiness.
6. Provide the emblem through the owner's existing inventory, shop or reward tools. Place, preview and save it in the factory's sticker library. Consumption follows the [unit appearance](unit-appearance.md) settings.

Import does not grant inventory, create offers or set prices. Players receive the matching content through **the server owner's client package or update**. Player installation never executes database scripts.

## Images and proportions

The import page reports the codecs actually available and the file/pixel limits. It checks image bytes rather than trusting extensions. Common raster formats, including PNG, JPEG, BMP, GIF, WebP, TIFF, TGA, DDS, ICO, AVIF and HEIC/HEIF, are normalized to transparent-capable PNG. HEIC/HEIF uses a decoder-only build with native security limits enabled.

Self-contained SVG is accepted through a bounded shape-only subset. The importer reconstructs approved paths and basic shapes before rasterization; it rejects declarations, entities, scripts, styles, text and fonts, embedded images, external references, resource URLs and excessive dimensions or complexity.

Existing transparency is preserved. White backgrounds are not automatically removed by color, which could erase white clothing or artwork. Prepare an alpha image when a cutout is needed. Native materials use a 512 × 512 canvas with aspect-preserving contain and transparent padding. The thumbnail uses the same material.

A smooth shield surface with reasonable margins usually gives a cleaner composition. The neutral shield silhouette is an authoring preview; the real model, lighting, placement and style determine the in-game result.

## Projects and protocol

Projects support create, open, edit and delete. Deleting a project does not unregister installed server definitions or erase player inventory. Reapplying an installed project preserves its native ID and other projects. Baseline IDs, foreign ownership and unrecorded author edits are refused.

The neutral image-package schema is `sdgo.owner-stickers/1`. A generic authoring example, using your own transparent image:

```python
from pathlib import Path
from owner.stickers import images, protocol

canonical = images.canonicalize_raster(
    Path("roundel.png").read_bytes(), source_name="roundel.png"
)
entry = protocol.make_sticker(
    "training-roundel", "Training roundel", canonical,
    settings={"default_width": 2.0, "default_height": 2.0},
)
manifest = protocol.make_manifest([entry])
protocol.validate_payloads(
    manifest, {entry["image"]["path"]: canonical["png"]}
)
Path("manifest.json").write_bytes(protocol.encode_manifest(manifest))
```

Each asset uses `images/<id>.png` and declares its byte count, dimensions and SHA-256. Duplicate IDs/paths and unknown top-level fields are rejected. Payload bytes must match all descriptors. An image package is authoring input, not a native overlay to copy directly into the game.

Manager's `sdgo.sticker-project/1` adds a 32-character hexadecimal project identity and selected hosts. `owner.stickers.compiler.definition(project, image, native_id)` validates a native definition; `textures`, `compile_content` and `server_plan` produce paired outputs. Native names allow at most 49 BMP characters. Target preflight allocates IDs, preserving an already installed project's ID.

## Native contract

| Output | Purpose |
| --- | --- |
| `catalog.bin` | `SDGOST1` v1 catalog: kind-38 409-byte emblems, linked kind-34 614-byte categories, and separate first-placement dimensions |
| `definitions.json` | Project ownership, emblem/category IDs, names, source-image hashes and default sizes |
| `resources.zdx` / `resources.zpk` | Validated thumbnail and material resources |
| `resource-overlay.ini` | SHA-256 binding for the sealed resource archive |
| `manifest.json` | Portable content hashes used by owner-profile distribution |
| `server-plan.json` | Definition-only plan, without credentials or player grants |

The linked family-17 category is required by the original inventory and grant procedures. Its `RefIndex` points to the emblem and its thumbnail points to the same SPR. Registering this category does not create a shop offer. Server plans contain both `rows` and `categories`; grant tools use the category ID.

The current ordinary-emblem profile uses unrestricted level/faction/unit fields and `StickerType=1`. The native name starts at `+5`, SPR path at `+109`, and material path at `+209`. The DWORD at `+205` stays zero: it is not an image-size field. First-placement dimensions belong to the catalog extension.

Only `sprs/sdgo_stickers_<id>.spr` and `txrs/sdgo_stickers_<id>.txr` are served by the shared resource broker's `sticker_catalog` domain. The `.txr` contains a PNG accepted by the native loader. Original archives are not rewritten.

Server application checks ownership, changes fixed definitions and records hashes in one transaction. Its legacy varchar name is an ASCII internal token; the displayed name comes from the client's UTF-16 row.

Client installation uses a cross-process target lease, hash receipts and a durable recovery journal. Retry can recover interrupted installation. Foreign author edits cause refusal with recovery materials preserved. Distribute the paired files through the existing owner-content publication flow.
