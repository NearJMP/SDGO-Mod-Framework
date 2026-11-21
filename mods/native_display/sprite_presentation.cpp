#include "sprite_presentation.h"
#include "artwork.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace native_display {
namespace {
using SpriteDraw = HRESULT (WINAPI*)(void*, IDirect3DTexture9*, const RECT*, const D3DVECTOR*, const D3DVECTOR*, D3DCOLOR);
using GetTransform = HRESULT (WINAPI*)(void*, D3DMATRIX*);
using SetTransform = HRESULT (WINAPI*)(void*, const D3DMATRIX*);
using HudRender = void (__thiscall*)(void*);

SpriteDraw nativeDraw;
GetTransform getTransform;
SetTransform setTransform;
HudRender nativeSpriteRender;
void* spriteBindReturn;
std::array<HudRender, 8> nativeHud{};
thread_local unsigned hudDepth = 0;
bool hasArtwork = false;
float configuredScale = 0;
float hudScale = 1;
int width = 0, height = 0;

struct SpriteBinding {
    IDirect3DDevice9* device = nullptr;
    IDirect3DTexture9* original = nullptr;
};
thread_local SpriteBinding* currentBinding;

Replacement LookupArtwork(IDirect3DTexture9* texture, const D3DSURFACE_DESC& description) {
    try {
        return FindArtwork(texture, description);
    } catch (const std::bad_alloc&) {
        return {};
    }
}

void __stdcall ReplaceNativeBinding(void** args) {
    if (!currentBinding || !args[2]) return;
    auto* texture = static_cast<IDirect3DTexture9*>(args[2]);
    D3DSURFACE_DESC desc{};
    if (FAILED(texture->GetLevelDesc(0, &desc))) return;
    const auto replacement = LookupArtwork(texture, desc);
    if (!replacement.texture) return;
    currentBinding->device = static_cast<IDirect3DDevice9*>(args[0]);
    currentBinding->original = texture;
    args[2] = replacement.texture;
}

__declspec(naked) void SpriteBind() {
    __asm { pushfd }
    __asm { pushad }
    __asm { mov ebx, esp }
    __asm { sub esp, 0x210 }
    __asm { and esp, 0xfffffff0 }
    __asm { fxsave [esp] }
    __asm { lea eax, [ebx+36] }
    __asm { push eax }
    __asm { call ReplaceNativeBinding }
    __asm { fxrstor [esp] }
    __asm { mov esp, ebx }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [spriteBindReturn] }
}

void __fastcall RenderNativeSprite(void* self, void*) {
    SpriteBinding binding;
    SpriteBinding* previous = currentBinding;
    currentBinding = &binding;
    nativeSpriteRender(self);
    currentBinding = previous;
    if (binding.device) binding.device->SetTexture(0, binding.original);
}

template<size_t Mode> void __fastcall RenderHud(void* self, void*) {
    struct Scope {
        Scope() { ++hudDepth; }
        ~Scope() { --hudDepth; }
    } scope;
    nativeHud[Mode](self);
}

struct HudHook {
    const char* symbol;
    void* detour;
};
const HudHook hudHooks[] = {
    {"native_display.hud_team", reinterpret_cast<void*>(&RenderHud<0>)},
    {"native_display.hud_deathmatch", reinterpret_cast<void*>(&RenderHud<1>)},
    {"native_display.hud_mission", reinterpret_cast<void*>(&RenderHud<2>)},
    {"native_display.hud_grid", reinterpret_cast<void*>(&RenderHud<3>)},
    {"native_display.hud_leader", reinterpret_cast<void*>(&RenderHud<4>)},
    {"native_display.hud_base", reinterpret_cast<void*>(&RenderHud<5>)},
    {"native_display.hud_observer", reinterpret_cast<void*>(&RenderHud<6>)},
    {"native_display.hud_deathmatch_observer", reinterpret_cast<void*>(&RenderHud<7>)},
};
static_assert(std::size(hudHooks) == nativeHud.size());

bool ApplyHud(D3DMATRIX& transform, const RECT& source, const D3DVECTOR& center, const D3DVECTOR& position) {
    // The original sprite render roots contain both HUD and transient overlays.
    // Only the fixed HUD screen bands use the larger scale.
    if (transform._14 != 0 || transform._24 != 0 || transform._34 != 0 || transform._44 != 1)
        return false;
    const float w = static_cast<float>(source.right) - source.left;
    const float h = static_cast<float>(source.bottom) - source.top;
    if (w <= 0 || h <= 0) return false;
    const float x = position.x - center.x + w * .5f;
    const float y = position.y - center.y + h * .5f;
    const float z = position.z - center.z;
    const float cx = x * transform._11 + y * transform._21 + z * transform._31 + transform._41;
    const float cy = x * transform._12 + y * transform._22 + z * transform._32 + transform._42;
    const float spanX = std::abs(w * transform._11) + std::abs(h * transform._21);
    const float spanY = std::abs(w * transform._12) + std::abs(h * transform._22);
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(spanX) || !std::isfinite(spanY) ||
        spanX > width * .48f || spanY > height * .46f) return false;
    const float u = cx / width, v = cy / height;
    float ax = width * .5f, ay = height * .5f;
    if (v >= -.02f && v < .64f && u >= -.02f && u < .28f) {
        // A full player list extends below the top status band.
        ax = 0;
        ay = 0;
    } else if (v >= -.02f && v < .34f && u >= .28f && u <= 1.02f) {
        ax = u > .72f ? static_cast<float>(width) : ax;
        ay = 0;
    } else if (v > .72f && v <= 1.02f && u > .24f && u < .76f) {
        ay = static_cast<float>(height);
    } else if (std::abs(u - .5f) > .04f || std::abs(v - .5f) > .07f) {
        return false;
    }
    for (auto& row : transform.m) {
        row[0] *= hudScale;
        row[1] *= hudScale;
    }
    transform._41 += ax * (1 - hudScale);
    transform._42 += ay * (1 - hudScale);
    return true;
}

HRESULT WINAPI Draw(void* sprite, IDirect3DTexture9* texture, const RECT* source,
                    const D3DVECTOR* center, const D3DVECTOR* position, D3DCOLOR color) {
    const bool scaleHud = hudDepth && hudScale > 1;
    if ((!scaleHud && !hasArtwork) || !texture)
        return nativeDraw(sprite, texture, source, center, position, color);
    D3DSURFACE_DESC desc{};
    if (FAILED(texture->GetLevelDesc(0, &desc)))
        return nativeDraw(sprite, texture, source, center, position, color);
    const RECT full{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    const RECT rect = source ? *source : full;
    const D3DVECTOR pivot = center ? *center : D3DVECTOR{};
    const D3DVECTOR location = position ? *position : D3DVECTOR{};
    Replacement artwork{};
    if (hasArtwork && rect.left >= 0 && rect.top >= 0 && rect.right <= full.right &&
        rect.bottom <= full.bottom && rect.right > rect.left && rect.bottom > rect.top) {
        artwork = LookupArtwork(texture, desc);
    }
    if (!scaleHud && !artwork.texture)
        return nativeDraw(sprite, texture, source, center, position, color);
    D3DMATRIX current{};
    if (FAILED(getTransform(sprite, &current)))
        return nativeDraw(sprite, texture, source, center, position, color);
    D3DMATRIX hud = current;
    const bool enlarged = scaleHud && ApplyHud(hud, rect, pivot, location);
    if (!enlarged && !artwork.texture)
        return nativeDraw(sprite, texture, source, center, position, color);

    HRESULT result = E_FAIL;
    if (artwork.texture) {
        D3DMATRIX replaced = hud;
        const LONG factor = static_cast<LONG>(artwork.factor);
        for (size_t j = 0; j < 4; ++j) {
            replaced.m[0][j] /= factor;
            replaced.m[1][j] /= factor;
        }
        const RECT crop{rect.left * factor, rect.top * factor, rect.right * factor, rect.bottom * factor};
        const D3DVECTOR highPivot{pivot.x * factor, pivot.y * factor, pivot.z};
        const D3DVECTOR highPosition{location.x * factor, location.y * factor, location.z};
        if (SUCCEEDED(setTransform(sprite, &replaced))) {
            result = nativeDraw(sprite, artwork.texture, &crop, &highPivot, &highPosition, color);
        }
    }
    if (FAILED(result)) {
        setTransform(sprite, &hud);
        result = nativeDraw(sprite, texture, source, center, position, color);
    }
    setTransform(sprite, &current);
    return result;
}
} // namespace

void SetSpriteCanvas(int w, int h, bool battle) {
    width = w;
    height = h;
    hudScale = battle ? (configuredScale > 0 ? configuredScale : std::max(1.0f, h / 1440.0f)) : 1;
}

bool InstallSprites(const SdgoApi* api, SdgoMod* mod, const char* setting) {
    if (!strcmp(setting, "off")) configuredScale = 1;
    else if (!strcmp(setting, "125%")) configuredScale = 1.25f;
    else if (!strcmp(setting, "150%")) configuredScale = 1.5f;
    else if (strcmp(setting, "auto")) return false;
    hasArtwork = ScanArtwork(api, mod);
    if (configuredScale == 1 && !hasArtwork) return true;

    getTransform = reinterpret_cast<GetTransform>(api->Symbol(mod, "native_display.sprite_get_transform"));
    setTransform = reinterpret_cast<SetTransform>(api->Symbol(mod, "native_display.sprite_set_transform"));
    if (!getTransform || !setTransform ||
        !api->HookCode(mod, "native_display.sprite_draw", reinterpret_cast<void*>(&Draw),
                       reinterpret_cast<void**>(&nativeDraw))) return false;
    // CSprite uses normalized UV coordinates in its vertex buffer. Swap only
    // the stage-0 texture in this draw and restore the original after it.
    if (hasArtwork &&
        (!api->HookCode(mod, "native_display.atlas_render", reinterpret_cast<void*>(&RenderNativeSprite),
                        reinterpret_cast<void**>(&nativeSpriteRender)) ||
         !api->HookSite(mod, "native_display.atlas_bind", reinterpret_cast<void*>(&SpriteBind), &spriteBindReturn)))
        return false;
    if (configuredScale != 1)
        for (size_t i = 0; i < nativeHud.size(); ++i)
            if (!api->HookCode(mod, hudHooks[i].symbol, hudHooks[i].detour,
                               reinterpret_cast<void**>(&nativeHud[i]))) return false;
    return true;
}
} // namespace native_display
