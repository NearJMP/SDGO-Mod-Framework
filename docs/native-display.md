# Native borderless display

[简体中文](native-display.zh-CN.md) · [Feature index](../README.md)

Enable **Native borderless display** in SDGO Mod Manager, choose a resolution and restart the game. During battle, **Desktop** fills the startup monitor at its display resolution. A smaller fixed preset opens a centered borderless window at that pixel size. A larger fixed preset still renders at the selected size and fits the output to the monitor while preserving its aspect ratio. For example, 2160p renders a 3840×2160 frame on a 1440p monitor.

Battle keeps the game's widescreen HUD coordinates and original sizes for HUD elements. The HUD can look smaller at higher render resolutions. Login and lobby present the original 1024×768 canvas in a larger centered 4:3 window. Pointer coordinates map to the active render canvas. The hangar and unit encyclopedia use the game's live models and animations. Large battle render surfaces can cost more memory and frame time.

Disable the mod in Manager and restart to use the game's usual display options.

## Local image previews

Open **Game Features → Native borderless display → Settings → Local image preview**. Select your own PNG and click the preview button. The 1440p and 2160p preview presets multiply each image's dimensions by 2 and 3, respectively. Save the preview as a PNG for an asset importer that accepts those dimensions.

Higher render resolutions benefit live models, scenes, animation and effects. Review static icons, cards, backgrounds and lettering baked into artwork image by image. Check fine strokes, borders and transparent edges at their intended size. Sprite sheets also depend on the importer preserving crop rectangles and logical dimensions.

You can use a local tool such as [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) for backgrounds, illustrations and textures. Choose its output as the optional AI PNG to compare it with a conventional resize. Manager validates the dimensions and restores the original transparency. When using your own local AI, give it this instruction and replace `N` with the selected multiplier:

> Resize to N times the original width and height and export an sRGB PNG. Preserve the unit's colours, composition, silhouette, icons, text and borders. Enhance existing detail gently. Keep the full colour image and export to a separate file.

Manager processes previews locally in its window. The programme download includes the tool. Supply your own images and AI model.
