# Native borderless display

[简体中文](native-display.zh-CN.md) · [Feature index](../README.md)

Enable **Native borderless display** in SDGO Mod Manager, choose a resolution and restart the game. During battle, **Desktop** fills the startup monitor at its display resolution. A smaller fixed preset opens a centered borderless window at that pixel size. A larger fixed preset still renders at the selected size and fits the output to the monitor while preserving its aspect ratio. For example, 2160p renders a 3840×2160 frame on a 1440p monitor.

Battle keeps the game's widescreen HUD coordinates and original sizes for HUD elements. The HUD can look smaller at higher render resolutions. Login and lobby present the original 1024×768 canvas in a larger centered 4:3 window. Pointer coordinates map to the active render canvas. The hangar and unit encyclopedia use the game's live models and animations. Large battle render surfaces can cost more memory and frame time.

Disable the mod in Manager and restart to use the game's usual display options.
