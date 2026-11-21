#pragma once
#include <d3d9.h>
#include "sdgo/sdgo_api.h"

namespace native_display {
struct Replacement {
    IDirect3DTexture9* texture;
    unsigned factor;
};
bool ScanArtwork(const SdgoApi* api, SdgoMod* mod);
Replacement FindArtwork(IDirect3DTexture9* texture, const D3DSURFACE_DESC& desc);
void ReleaseArtwork();
}
