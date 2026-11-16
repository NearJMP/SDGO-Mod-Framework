#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

#include "sdgo/sdgo_api.h"
#include "sdgo/sticker_localization.h"

extern "C" int __cdecl SdgoModLoad(const SdgoApi*, SdgoMod*);
extern "C" std::uint32_t __cdecl SdgoStickerLocalizationLookupV1(
    std::uint32_t, const wchar_t*, std::uint32_t, wchar_t*, std::uint32_t);
extern "C" std::uint32_t __cdecl SdgoStickerShopLocalizationLookupV1(
    std::uint32_t, const wchar_t*, std::uint32_t, wchar_t*, std::uint32_t);
extern "C" int __cdecl SdgoStickerLocalizationGetSnapshotV1(
    SdgoStickerLocalizationSnapshotV1*);

namespace {

unsigned g_checks = 0;
bool g_enabled = true;

void Check(bool condition, const char* message)
{
    ++g_checks;
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", message);
    if (!condition)
        throw std::runtime_error(message);
}

int SettingInt(SdgoMod*, const char*, int)
{
    return g_enabled ? 1 : 0;
}

void Log(SdgoMod*, int, const char*, ...)
{
}

using SetMode = void(__cdecl*)(std::uint32_t);

std::uint32_t StickerLookup(std::uint32_t id, std::wstring_view source,
                            wchar_t* output, std::uint32_t outputChars)
{
    return SdgoStickerLocalizationLookupV1(
        id, source.data(), static_cast<std::uint32_t>(source.size()), output,
        outputChars);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    try {
        Check(argc == 2, "the ownership fixture path is supplied");
        Check(!GetModuleHandleW(L"sticker_catalog.dll"),
              "the first provider lookup runs with no catalog loaded");

        SdgoApi api{};
        api.size = sizeof(api);
        api.version = SDGO_API_VERSION;
        api.Log = &Log;
        api.SettingInt = &SettingInt;
        Check(SdgoModLoad(&api, reinterpret_cast<SdgoMod*>(1)) == 1,
              "the provider installs without owning a hook");

        constexpr std::wstring_view source = L"Triangle Sticker";
        constexpr std::wstring_view target = L"三角形贴纸";
        const auto required = StickerLookup(240005u, source, nullptr, 0);
        Check(required == target.size() + 1,
              "a size query returns required characters including NUL");
        std::array<wchar_t, SDGO_STICKER_LOCALIZATION_NAME_CAPACITY> output{};
        output.fill(L'X');
        Check(StickerLookup(240005u, source, output.data(), required - 1) ==
                  required &&
                  output.front() == L'X',
              "an undersized output is never written");
        Check(StickerLookup(240005u, source, output.data(), output.size()) ==
                  required &&
                  std::wstring_view(output.data()) == target,
              "the absent catalog permits the exact stock translation");
        output.fill(L'X');
        Check(StickerLookup(240005u, L"Triangle Sticker?", output.data(),
                            output.size()) == 0 &&
                  output.front() == L'X',
              "source drift is refused without touching output");
        Check(StickerLookup(249999u, L"大肥鱼", output.data(), output.size()) ==
                  0 &&
                  output.front() == L'X',
              "the exact imported custom name remains unchanged");
        constexpr std::wstring_view nonAsciiSource = L"삼국전 여포군 스티커";
        constexpr std::wstring_view nonAsciiTarget = L"三国传吕布军贴纸";
        output.fill(L'X');
        const auto nonAsciiRequired = StickerLookup(
            240358u, nonAsciiSource, output.data(), output.size());
        Check(nonAsciiRequired == nonAsciiTarget.size() + 1 &&
                  std::wstring_view(output.data()) == nonAsciiTarget,
              "a same-source reviewed non-ASCII sticker is translated");
        output.fill(L'X');
        Check(StickerLookup(240009u, L"즐 Sticker", output.data(),
                            output.size()) == 0 &&
                  output.front() == L'X',
              "an ambiguous transliterated sticker remains verbatim");
        output.fill(L'X');
        Check(StickerLookup(240335u, L" Sticker", output.data(),
                            output.size()) == 0 &&
                  output.front() == L'X',
              "the incomplete ASCII stock source remains verbatim");

        constexpr std::wstring_view shopSource = L"삼각형 스티커";
        constexpr std::wstring_view incompleteAliasSource = L"龍 Sticker";
        constexpr std::wstring_view incompleteAliasTarget = L"龍贴纸";
        const HMODULE fixture = LoadLibraryW(argv[1]);
        Check(fixture != nullptr, "the catalog ownership fixture loads");
        const auto setMode = reinterpret_cast<SetMode>(
            GetProcAddress(fixture, "SetStickerCatalogOwnershipFixtureMode"));
        Check(setMode != nullptr, "the ownership fixture exposes its mode setter");
        for (const std::uint32_t mode : {1u, 2u, 3u, 4u}) {
            setMode(mode);
            output.fill(L'X');
            Check(StickerLookup(240005u, source, output.data(), output.size()) ==
                      0 &&
                      output.front() == L'X',
                  "owned, unknown, exceptional, and malformed sticker ownership fail closed");
            output.fill(L'X');
            Check(SdgoStickerShopLocalizationLookupV1(
                      1710001u, shopSource.data(),
                      static_cast<std::uint32_t>(shopSource.size()),
                      output.data(), output.size()) == 0 &&
                      output.front() == L'X',
                  "owned, unknown, exceptional, and malformed shop ownership fail closed");
        }
        setMode(1);
        output.fill(L'X');
        Check(SdgoStickerShopLocalizationLookupV1(
                  1720156u, incompleteAliasSource.data(),
                  static_cast<std::uint32_t>(incompleteAliasSource.size()),
                  output.data(), output.size()) == 0 &&
                  output.front() == L'X',
              "an owned incomplete-source alias remains unchanged");
        setMode(0);
        Check(StickerLookup(240005u, source, output.data(), output.size()) ==
                  required,
              "an explicit not-owned result permits stock translation");

        output.fill(L'X');
        const auto shopRequired = SdgoStickerShopLocalizationLookupV1(
            1710001u, shopSource.data(),
            static_cast<std::uint32_t>(shopSource.size()), output.data(),
            output.size());
        Check(shopRequired == required &&
                  std::wstring_view(output.data()) == target,
              "the exact sticker category projection uses the same reviewed target");
        output.fill(L'X');
        Check(SdgoStickerShopLocalizationLookupV1(
                  1710001u, L"삼각형 스티커?", 8u, output.data(),
                  output.size()) == 0 &&
                  output.front() == L'X',
              "shop category source drift is refused");
        output.fill(L'X');
        Check(SdgoStickerShopLocalizationLookupV1(
                  1799999u, shopSource.data(),
                  static_cast<std::uint32_t>(shopSource.size()),
                  output.data(), output.size()) == 0 &&
                  output.front() == L'X',
              "an unknown shop category remains unchanged");
        output.fill(L'X');
        Check(SdgoStickerShopLocalizationLookupV1(
                  1720176u, nonAsciiSource.data(),
                  static_cast<std::uint32_t>(nonAsciiSource.size()),
                  output.data(), output.size()) == nonAsciiRequired &&
                  std::wstring_view(output.data()) == nonAsciiTarget,
              "the same-source non-ASCII shop projection is translated");
        constexpr std::wstring_view ambiguousShopSource = L"즐 스티커";
        constexpr std::wstring_view ambiguousShopTarget = L"“兹尔”贴纸（音译）";
        output.fill(L'X');
        const auto ambiguousRequired = SdgoStickerShopLocalizationLookupV1(
            1710005u, ambiguousShopSource.data(),
            static_cast<std::uint32_t>(ambiguousShopSource.size()),
            output.data(), output.size());
        Check(ambiguousRequired == ambiguousShopTarget.size() + 1 &&
                  std::wstring_view(output.data()) == ambiguousShopTarget,
              "an exact category retains its previously reviewed transliteration");
        output.fill(L'X');
        const auto incompleteAliasRequired = SdgoStickerShopLocalizationLookupV1(
            1720156u, incompleteAliasSource.data(),
            static_cast<std::uint32_t>(incompleteAliasSource.size()),
            output.data(), output.size());
        Check(incompleteAliasRequired == incompleteAliasTarget.size() + 1 &&
                  std::wstring_view(output.data()) == incompleteAliasTarget,
              "an exact category alias retains its reviewed label while the incomplete native sticker source remains untranslated");
        output.fill(L'X');
        Check(SdgoStickerShopLocalizationLookupV1(
                  1720156u, L"龍 Sticker?", 10u, output.data(),
                  output.size()) == 0 && output.front() == L'X',
              "an incomplete-source category alias still requires its exact category source");

        SdgoStickerLocalizationSnapshotV1 snapshot{};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 1;
        Check(SdgoStickerLocalizationGetSnapshotV1(&snapshot) == 1 &&
                  snapshot.installed == 1 && snapshot.enabled == 1 &&
                  snapshot.phrase_count == 298 &&
                  snapshot.shop_projection_count == 347 &&
                  snapshot.guarded >= 8,
              "the provider snapshot reports reviewed counts and guarded lookups");

        g_enabled = false;
        Check(SdgoModLoad(&api, reinterpret_cast<SdgoMod*>(1)) == 1,
              "the provider can install in its disabled state");
        output.fill(L'X');
        Check(StickerLookup(240005u, source, output.data(), output.size()) == 0 &&
                  output.front() == L'X',
              "the separate Enabled setting preserves stock source text");

        FreeLibrary(fixture);
        std::printf("%u checks passed\n", g_checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
