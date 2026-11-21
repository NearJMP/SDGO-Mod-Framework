# Native borderless display

[简体中文](native-display.zh-CN.md) · [Feature index](../README.md)

Enable **Native borderless display** in SDGO Mod Manager, choose a resolution and restart the game. During battle, **Desktop** fills the startup monitor at its display resolution. A smaller fixed preset opens a centered borderless window at that pixel size. A larger fixed preset still renders at the selected size and fits the output to the monitor while preserving its aspect ratio. For example, 2160p renders a 3840×2160 frame on a 1440p monitor.

**Battle HUD size** is independent of render resolution. Auto uses 150% at 2160p for fixed HUD elements such as the status, minimap, weapon bar, reticle and their text. At or below 1440p, Auto uses the original size. You can also select 125%, 150% or Off. Login and lobby use the centered 4:3 canvas and pointer input maps to the active canvas. Large battle render surfaces can cost more memory and frame time.

Disable the mod in Manager and restart to use the game's usual display options.

## Local image previews

Open **Game Features → Native borderless display → Settings → Local image preview**. Select the original PNG and multiply its dimensions by 2 or 3. Choose **Use in game**, then restart the client with the mod enabled. Applying a different scale for the same source updates its local entry. Choose **Restore original** and restart to remove that entry.

The game matches a compatible static UI sprite sheet by its original pixels. Use the full PNG for your client version and preserve its transparent margins and tile positions. One sheet can contain several icons and buttons. The renderer maps the original crop rectangles to the enlarged image and uses the native layout and hit regions. A missing match or an unreadable replacement falls back to the original. Live models, scenes, animation and effects benefit from higher render resolution.

You can use a local tool such as [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) for backgrounds, illustrations and textures. Choose its output as the optional AI PNG to compare it with a conventional resize. Manager validates the dimensions and restores the original transparency. When using your own local AI, give it this instruction and replace `N` with the selected multiplier:

> Resize to N times the original width and height and export an sRGB PNG. Preserve the unit's colours, composition, silhouette, icons, text and borders. Enhance existing detail gently. Keep the full colour image and export to a separate file.

Manager processes and applies images locally. Supply your own source images and AI model.
