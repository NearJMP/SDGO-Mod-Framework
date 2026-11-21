#pragma once
#include "sdgo/sdgo_api.h"

namespace native_display {
bool InstallSprites(const SdgoApi* api, SdgoMod* mod, const char* hudSetting);
void SetSpriteCanvas(int width, int height, bool battle);
void ReleaseArtwork();
}
