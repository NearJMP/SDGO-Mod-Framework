// Optional process-local stock sticker localization service. Consumers never
// load either provider and preserve exact source text when a provider declines.
#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>

#define SDGO_STICKER_LOCALIZATION_LOOKUP_V1_EXPORT \
    "SdgoStickerLocalizationLookupV1"
#define SDGO_STICKER_SHOP_LOCALIZATION_LOOKUP_V1_EXPORT \
    "SdgoStickerShopLocalizationLookupV1"
#define SDGO_STICKER_CATALOG_OWNERSHIP_V1_EXPORT \
    "SdgoStickerCatalogOwnershipV1"
#define SDGO_STICKER_LOCALIZATION_SNAPSHOT_V1_EXPORT \
    "SdgoStickerLocalizationGetSnapshotV1"

#define SDGO_STICKER_CATALOG_NOT_OWNED 0u
#define SDGO_STICKER_CATALOG_OWNED 1u
#define SDGO_STICKER_CATALOG_UNKNOWN 2u
#define SDGO_STICKER_LOCALIZATION_NAME_CAPACITY 50u

extern "C" {
using SdgoStickerLocalizationLookupV1Fn = std::uint32_t(__cdecl*)(
    std::uint32_t stableId,
    const wchar_t* source,
    std::uint32_t sourceChars,
    wchar_t* output,
    std::uint32_t outputChars);

using SdgoStickerCatalogOwnershipV1Fn = std::uint32_t(__cdecl*)(
    std::uint32_t stickerId,
    const wchar_t* source,
    std::uint32_t sourceChars);

struct SdgoStickerLocalizationSnapshotV1 {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t installed;
    std::uint32_t enabled;
    std::uint32_t phrase_count;
    std::uint32_t shop_projection_count;
    std::uint64_t queries;
    std::uint64_t matches;
    std::uint64_t writes;
    std::uint64_t guarded;
    std::uint64_t invalid;
};

using SdgoStickerLocalizationGetSnapshotV1Fn = int(__cdecl*)(
    SdgoStickerLocalizationSnapshotV1* output);
}

namespace sdgo::sticker_localization {

inline HMODULE ProviderModule() noexcept
{
    return GetModuleHandleW(L"sticker_localization.dll");
}

inline HMODULE CatalogModule() noexcept
{
    return GetModuleHandleW(L"sticker_catalog.dll");
}

inline std::uint32_t CallLookup(SdgoStickerLocalizationLookupV1Fn lookup,
                                std::uint32_t stableId,
                                const wchar_t* source,
                                std::uint32_t sourceChars,
                                wchar_t* output,
                                std::uint32_t outputChars) noexcept
{
    __try {
        return lookup(stableId, source, sourceChars, output, outputChars);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

inline std::uint32_t QueryCatalogOwnership(std::uint32_t stickerId,
                                           const wchar_t* source,
                                           std::uint32_t sourceChars) noexcept
{
    if (!source || !sourceChars ||
        sourceChars >= SDGO_STICKER_LOCALIZATION_NAME_CAPACITY)
        return SDGO_STICKER_CATALOG_UNKNOWN;
    const HMODULE module = CatalogModule();
    if (!module)
        return SDGO_STICKER_CATALOG_NOT_OWNED;
    const auto query = reinterpret_cast<SdgoStickerCatalogOwnershipV1Fn>(
        GetProcAddress(module, SDGO_STICKER_CATALOG_OWNERSHIP_V1_EXPORT));
    if (!query)
        return SDGO_STICKER_CATALOG_UNKNOWN;
    std::uint32_t result = SDGO_STICKER_CATALOG_UNKNOWN;
    __try {
        result = query(stickerId, source, sourceChars);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = SDGO_STICKER_CATALOG_UNKNOWN;
    }
    return result <= SDGO_STICKER_CATALOG_UNKNOWN
               ? result
               : SDGO_STICKER_CATALOG_UNKNOWN;
}

inline bool TryLookupExport(const char* exportName, std::uint32_t stableId,
                            const wchar_t* source, std::uint32_t sourceChars,
                            std::wstring* output) noexcept
{
    if (!exportName || !source || !output || !sourceChars ||
        sourceChars >= SDGO_STICKER_LOCALIZATION_NAME_CAPACITY)
        return false;
    const HMODULE module = ProviderModule();
    if (!module)
        return false;
    const auto lookup = reinterpret_cast<SdgoStickerLocalizationLookupV1Fn>(
        GetProcAddress(module, exportName));
    if (!lookup)
        return false;
    const std::uint32_t required =
        CallLookup(lookup, stableId, source, sourceChars, nullptr, 0);
    if (!required || required > SDGO_STICKER_LOCALIZATION_NAME_CAPACITY)
        return false;
    std::array<wchar_t, SDGO_STICKER_LOCALIZATION_NAME_CAPACITY> buffer{};
    if (CallLookup(lookup, stableId, source, sourceChars, buffer.data(),
                   required) != required ||
        buffer[required - 1] != L'\0')
        return false;
    for (std::uint32_t index = 0; index + 1 < required; ++index) {
        if (!buffer[index])
            return false;
    }
    try {
        std::wstring value(buffer.data(), required - 1);
        output->swap(value);
        return true;
    } catch (...) {
        return false;
    }
}

inline bool TryLookupSticker(std::uint32_t stickerId, const wchar_t* source,
                             std::uint32_t sourceChars,
                             std::wstring* output) noexcept
{
    return TryLookupExport(SDGO_STICKER_LOCALIZATION_LOOKUP_V1_EXPORT,
                           stickerId, source, sourceChars, output);
}

inline bool TryLookupShopCategory(std::uint32_t categoryId,
                                  const wchar_t* source,
                                  std::uint32_t sourceChars,
                                  std::wstring* output) noexcept
{
    return TryLookupExport(
        SDGO_STICKER_SHOP_LOCALIZATION_LOOKUP_V1_EXPORT, categoryId,
        source, sourceChars, output);
}

} // namespace sdgo::sticker_localization
