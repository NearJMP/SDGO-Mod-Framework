# Native borderless display

[简体中文](native-display.zh-CN.md) · [Feature index](../README.md)

Enable **Native borderless display** in SDGO Mod Manager, choose a resolution and restart the game. During battle, **Desktop** fills the startup monitor at its display resolution. A smaller fixed preset opens a centered borderless window at that pixel size. A preset larger than the monitor uses the monitor's resolution when it fits the supported size range.

Battle scenes render at the selected pixel size and use the game's widescreen HUD coordinates. Login and lobby present the original 1024×768 canvas in a larger centered 4:3 window. Pointer coordinates map back to the canvas. Models, textures, interface art and game text use the client's assets. Large battle render surfaces can cost more memory and frame time.

Disable the mod in Manager and restart to use the game's usual display options.
