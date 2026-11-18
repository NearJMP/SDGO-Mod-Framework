# PvP map authoring guide

[中文](map-pvp-authoring.zh-CN.md)

This guide is for authors who build a PvP battle map in Blender or another 3D tool and hand it to a server owner for import. It follows the order of the work: size, coordinates, look, ground, spawns, icon and packaging, and ends with a pre-delivery checklist and common problems. The complete field reference is [Map and mission import](map-import.md).

The import only reads the map package you provide and generates the records and resources the game needs. The server owner registers it in Manager, compiles it, installs the client package and writes the server rows; players receive the map through the server owner's client package or update.

## 1. Decide the size

- **Units**: 1 game unit = 1 cm. Coordinates and heights in `map.json` use game units.
- **Terrain size**: `terrain.half_extent` is 4096, 8192 or 16384, a terrain 81.92, 163.84 or 327.68 m across. Models, spawns and item points must lie inside it.
- **Play area**: `terrain.play_area` `half_x` and `half_z` are the half widths of the area units can move in, and `ceiling` is the height limit. Units cannot leave the play area, so stands and scenery can sit outside it.
- Reference: a 160 m stadium uses `half_extent: 8192`, a play area of `half_x/half_z: 3050` (about 61 m square) and `ceiling: 2000`, which suits two teams of six.

## 2. Coordinates and Blender export

- Game axes: X east, Y up, Z north. Blender axes: X east, Y north, Z up. Exported with the default glTF settings (+Y up), the compiler converts the axes: **Blender +Y is game north**.
- Model in metres. Before export, apply all transforms so scale and rotation are baked into the vertices. In `map.json` give the object `"position": [0, 0, 0]` and `"scale": 100` (metres to centimetres).
- `.glb` works, as does `.gltf` with separate `.bin` and texture files. A single package file may not exceed 64 MiB; a package may hold at most 512 MiB, 2048 files and 1024 objects. Use "glTF Separate" for large models and textures.
- Only static triangle meshes are read: no skins, morph targets or animations. Each material reads one base colour texture (PNG or JPEG, scaled down above 2048 pixels) and its base colour factor; translucent materials use `alphaMode: "BLEND"`, optionally with `doubleSided`.
- Give nodes meaningful names (for example `Floor`, `Platform_A`); the ground selection uses them.
- **Scale**: one model may hold about a million vertices and a million triangles; the compiler splits it into nodes within the client's per-node limits (65535 vertices, 21845 triangles). The client is a 32-bit program with about 2 GB of memory, and a map is loaded only for its battles. Reference: a stadium with nineteen 2048-class textures and 490,000 triangles of grass peaks at about 1.4 GB in battle; test larger maps first.

## 3. The look: bake lighting into the textures

The client draws only each model's base colour texture: no real-time lighting, normal maps or specular. What the Blender viewport shows, especially with a strong world light or an HDRI, does not reach the game. To get shading, shadows and detail, bake them into the textures:

1. Give each object a new non-overlapping UV map (Smart UV Project is fine). Split very large objects into several parts with one texture each, aiming at 1–3 cm per pixel on the ground.
2. Keep only a sun and a sky light in the scene, with the direction and colour of `terrain.lighting` in `map.json`.
3. Bake "Diffuse" in Cycles with Direct, Indirect and Color enabled, using the "Standard" view transform. When shadows come out too dark, add about 10 % of a colour-only bake as ambient light.
4. Use the bake as the new base colour texture and replace the procedural materials before export.

Watch for:

- **Normals must face outwards.** Inverted faces look fine in a Blender render but bake black. Turn on Face Orientation and check that every visible face is blue.
- **Grass and plants** can be real geometry; a few hundred thousand triangles are fine. Colouring each blade like the ground under it blends best.
- **Fading while blocking the camera**: small props fade while they block the camera; walkable ground and models wider than 30 m stay opaque by default. Use `objects[].camera_fade` to choose per object. In a stadium, split walls and pillars into their own objects so only the part in front of the camera fades.

## 4. Ground and terrain

Units do not walk on model triangles but on a 257×257 terrain height field (grid spacing = `half_extent` × 2 / 256, about 64 cm at 8192). There are two approaches:

- **Terrain only**: provide `height.png`, terrain layer textures and masks; the game draws the terrain.
- **Model as ground** (recommended for detailed floors): set `"ground": ["Floor", "Platform_A"]` on the model object so the compiler takes the top surface of these meshes at every grid vertex they cover, and set `"terrain": {"render": false, ...}` to hide the height field so only the model shows.

`height.png`:

- 257×257 greyscale PNG (8 or 16 bit); black is `min` and white is `max` (game units).
- **Row 0 is the north edge (Blender +Y) and column 0 the west edge**, and layer masks use the same orientation. The most common mistake is a vertically flipped image: units walk under the floor and rocks poke through it. Check by displacing a plane with the height image in Blender and comparing it with the model.

`ground`:

- Select only surfaces people should stand on: floors, platforms, steps. Selected walls, roofs and tall props would become walkable too.
- Platforms higher than the main floor (lawns, stand walkways) must be selected as well, or units sink into them.
- Detail finer than the grid spacing (small steps, thin railings) does not affect walking height.

Collision:

- `collision: "auto"` makes one bounding box per mesh, good for separate crates or pillars; `"none"` has no collision; or list 1–64 boxes (`center`, `half_extents`, `rotation_y`).
- Never use `auto` for a whole stadium: one huge box would block the entire field. Use `"none"` for the stadium and give walls and obstacles their own boxes or objects.

## 5. Spawns, item points and launch pads

- `spawns.team_a` and `spawns.team_b` hold exactly 6 points each; `spawns.shared` holds 1–12 points without a team; the `grid` mode also needs `grid_respawn_a` and `grid_respawn_b`. Each point has a `position` (`[x, y, z]` in game units) and a `facing` (`[x, z]` direction; `[0, 1]` faces north).
- Place spawns about 200 (2 m) above the visible ground. The compiler requires at least 20 above the ground and outside colliding objects. After changing the ground (for example adding `ground`), re-seat the spawn heights on the new ground.
- Keep the two teams balanced, and leave room to walls and the play area edge.
- `item_patterns`: `red` and `blue` need the same number of sets, each with exactly 8 item points, at most 8 sets; with no items, give both empty lists. Item points may not be more than 5 below the ground or inside an object.
- `launch_pads`: `position` is the trigger centre (about 100 above the ground) and `target` the landing point, with optional `size` (trigger cube edge 50–1000, default 300) and `speed` (16–100, default 40). The target must be inside the play area, at least 20 above the ground there and below `ceiling`; the flight may not pass through terrain or colliding objects and must be 157–27081 long. The pad itself is invisible; for a visible "orb", place a model with `collision: "none"` at the same position.

## 6. Sky, fog and music

- `terrain.sky` (`zenith`, `horizon`, `ground`) sets the sky colours and `terrain.fog` (`color`, `start`, `end`, with `end` greater than `start`) the distance fog.
- `terrain.lighting`: `sun_direction` must point down (y < 0). Bake textures with the same sun direction so the picture is consistent.
- `presentation.bgm`: an MP3 in the package or a shipped `{"native": "BGM\\NNNN.mp3"}`.

## 7. Icon, names and minimap

- **Names**: `name.zh` and `name.en` (up to 48 characters each). `name.en` is written in upper case above the room icon; keep it to about 16 characters of letters, digits and common punctuation.
- **Icon** (`presentation.preview`): provide only a **scene picture**, any aspect ratio, preferably landscape and at least 480 pixels wide, without text. The compiler crops it to 120×64 in the shipped style and adds the glowing English name tag above it.
- **Minimap** (`presentation.minimap`): a square PNG covering the **whole terrain** (2 × `half_extent` across), north up and east right. The compiler crops the battle radar to the play area and frames it in orange-yellow. An orthographic top-down render of the baked models matches the game best.
- `presentation.boundary_lines` (default `true`) paints the play area edge on the terrain; with the terrain hidden (`render: false`) the line is hidden too.

## 8. `map.json` skeleton

A PvP map that uses a model as ground. `files` must list the SHA-256 of every package file except `map.json`.

```json
{
  "schema": "sdgo.map-package", "schema_version": 3,
  "map_key": "author.example-stadium",
  "name": {"zh": "示例竞技场", "en": "EXAMPLE STADIUM"},
  "description": {"zh": "两队在圆形竞技场中对战。"},
  "modes": ["normal", "deathmatch"],
  "servers": ["training", "beginner", "expert"],
  "terrain": {
    "half_extent": 8192, "render": false,
    "height": {"image": "terrain/height.png", "min": 0, "max": 0},
    "layers": [{"texture": "terrain/ground.jpg", "tiling": 32}],
    "lighting": {"sun_direction": [-0.4, -1, 0.3], "sun_color": [1, 0.96, 0.88], "bake_shadows": false},
    "fog": {"color": [0.42, 0.52, 0.56], "start": 9000, "end": 16000},
    "sky": {"zenith": [0.12, 0.3, 0.48], "horizon": [0.62, 0.78, 0.82], "ground": [0.35, 0.4, 0.38]},
    "play_area": {"half_x": 3000, "half_z": 3000, "ceiling": 2000}
  },
  "objects": [
    {"model": "models/stadium.gltf", "position": [0, 0, 0], "rotation_y": 0, "scale": 100,
     "collision": "none", "only": "pvp", "ground": ["Floor", "Platform_A"]},
    {"model": "models/walls.glb", "position": [0, 0, 0], "rotation_y": 0, "scale": 100}
  ],
  "spawns": {
    "team_a": [
      {"position": [-2000, 200, -500], "facing": [1, 0]}, {"position": [-2000, 200, -300], "facing": [1, 0]},
      {"position": [-2000, 200, -100], "facing": [1, 0]}, {"position": [-2000, 200, 100], "facing": [1, 0]},
      {"position": [-2000, 200, 300], "facing": [1, 0]}, {"position": [-2000, 200, 500], "facing": [1, 0]}
    ],
    "team_b": [
      {"position": [2000, 200, -500], "facing": [-1, 0]}, {"position": [2000, 200, -300], "facing": [-1, 0]},
      {"position": [2000, 200, -100], "facing": [-1, 0]}, {"position": [2000, 200, 100], "facing": [-1, 0]},
      {"position": [2000, 200, 300], "facing": [-1, 0]}, {"position": [2000, 200, 500], "facing": [-1, 0]}
    ],
    "shared": [{"position": [0, 200, -1500], "facing": [0, 1]}, {"position": [0, 200, 1500], "facing": [0, -1]}]
  },
  "item_patterns": {"red": [], "blue": []},
  "launch_pads": [{"position": [-1650, 300, 0], "target": [-300, 700, 0]}],
  "presentation": {"preview": "images/preview.png", "minimap": "images/minimap.png",
                   "bgm": {"native": "BGM\\0022.mp3"}},
  "files": {"models/stadium.gltf": "<sha256>"}
}
```

`files` shows a single entry here; list every package file (models, `.bin`, textures, height image, images).

## 9. Packaging and delivery

- Layout: `map.json`, `models/` (glTF and textures), `terrain/` (height image, layers and masks), `images/` (scene picture, minimap).
- Update the SHA-256 values in `files` whenever a file changes.
- `map_key` identifies the map (3–64 lower-case letters, digits, `.`, `_`, `-`). Registering the same `map_key` again replaces the files and keeps the names and settings the server owner changed in Manager.
- Hand the whole folder (or an archive) to the server owner, who registers it in Manager's "Map and mission import", compiles, installs the client package, checks and applies the server rows and restarts the World/Battle services.

## 10. Pre-delivery checklist

- [ ] Blender +Y is north; all transforms are applied; `scale` is 100 in `map.json`.
- [ ] Row 0 of `height.png` and the masks is the north edge, checked against the model.
- [ ] Face orientation is correct (visible faces are blue); no inverted normals.
- [ ] Lighting and shadows are baked into the textures; nothing relies on a strong viewport light.
- [ ] Every surface people stand on is in `ground`; spawns sit about 200 above the ground.
- [ ] `team_a` and `team_b` have 6 spawns each; `item_patterns` has equal red and blue sets of 8 points (or none).
- [ ] No file exceeds 64 MiB and the package stays under 512 MiB; textures are at most 2048 pixels.
- [ ] No skins or animations; translucent materials use `BLEND`.
- [ ] The whole stadium does not use `collision: "auto"`.
- [ ] The icon is a plain scene picture; the minimap is north up and covers the whole terrain.
- [ ] The `files` digests match the files.

## 11. Common problems

| Symptom | Cause and fix |
| --- | --- |
| Units walk under the floor and rocks poke through it | The height image is flipped vertically, or a standing surface is missing from `ground`. |
| Some faces are black in the game | Inverted normals: a bake lights only the side a normal points to. Fix the face orientation and bake again. |
| The game looks darker and flatter than Blender | The viewport used a strong world light or HDRI; the game shows only what is baked into the textures. |
| The whole map turns see-through near the edges | The stadium is one object; compile with Manager 1.16.52 or newer, or set `"camera_fade": false` on that object. |
| A high-polygon model crashes the client while the map loads | Compile with Manager 1.16.51 or newer (models are split within the client's node limits). |
| The room icon has no English name | Compile with Manager 1.16.52 or newer; the name comes from `name.en`. |
| Registration reports a file larger than 64 MiB | Export glTF Separate, or split the model into several objects. |
| A spawn is "below the ground" | The ground changed but the spawn heights did not; place them 200 above the new ground. |
