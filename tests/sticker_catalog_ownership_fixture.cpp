#include <windows.h>

#include <cstdint>

namespace {
volatile LONG g_mode = 0;
}

extern "C" __declspec(dllexport) void __cdecl
SetStickerCatalogOwnershipFixtureMode(std::uint32_t mode)
{
    InterlockedExchange(&g_mode, static_cast<LONG>(mode));
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
SdgoStickerCatalogOwnershipV1(std::uint32_t, const wchar_t*, std::uint32_t)
{
    const auto mode = static_cast<std::uint32_t>(
        InterlockedCompareExchange(&g_mode, 0, 0));
    if (mode == 3u)
        RaiseException(0xE0424242u, 0, 0, nullptr);
    if (mode == 4u)
        return 99u;
    return mode;
}
