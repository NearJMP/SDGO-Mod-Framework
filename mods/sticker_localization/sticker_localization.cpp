#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "dictionary.h"
#include "sdgo/sdgo_api.h"
#include "sdgo/sticker_localization.h"

namespace {

volatile LONG g_installed = 0;
volatile LONG g_enabled = 0;
alignas(8) volatile LONG64 g_queries = 0;
alignas(8) volatile LONG64 g_matches = 0;
alignas(8) volatile LONG64 g_writes = 0;
alignas(8) volatile LONG64 g_guarded = 0;
alignas(8) volatile LONG64 g_invalid = 0;

bool CopySourceBytes(const wchar_t* source, std::uint32_t sourceChars,
                     wchar_t* output) noexcept
{
    __try {
        std::memcpy(output, source, sourceChars * sizeof(wchar_t));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CopySource(const wchar_t* source, std::uint32_t sourceChars,
                std::wstring* output) noexcept
{
    if (!source || !sourceChars || !output ||
        sourceChars >= SDGO_STICKER_LOCALIZATION_NAME_CAPACITY)
        return false;
    try {
        std::wstring value(sourceChars, L'\0');
        if (!CopySourceBytes(source, sourceChars, value.data()) ||
            value.find(L'\0') != std::wstring::npos)
            return false;
        output->swap(value);
        return true;
    } catch (...) {
        return false;
    }
}

std::uint32_t Publish(std::wstring_view target, wchar_t* output,
                      std::uint32_t outputChars) noexcept
{
    if (target.empty() || target.size() >=
                              SDGO_STICKER_LOCALIZATION_NAME_CAPACITY) {
        InterlockedIncrement64(&g_invalid);
        return 0;
    }
    const auto required = static_cast<std::uint32_t>(target.size() + 1);
    if (!output || outputChars < required)
        return required;
    __try {
        std::memcpy(output, target.data(), target.size() * sizeof(wchar_t));
        output[target.size()] = L'\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement64(&g_invalid);
        return 0;
    }
    InterlockedIncrement64(&g_writes);
    return required;
}

std::uint32_t LookupSticker(std::uint32_t stickerId, const wchar_t* source,
                            std::uint32_t sourceChars, wchar_t* output,
                            std::uint32_t outputChars) noexcept
{
    InterlockedIncrement64(&g_queries);
    if (!InterlockedCompareExchange(&g_installed, 0, 0) ||
        !InterlockedCompareExchange(&g_enabled, 0, 0))
        return 0;
    std::wstring sourceText;
    if (!CopySource(source, sourceChars, &sourceText)) {
        InterlockedIncrement64(&g_invalid);
        return 0;
    }
    const std::uint32_t ownership =
        sdgo::sticker_localization::QueryCatalogOwnership(
            stickerId, sourceText.data(),
            static_cast<std::uint32_t>(sourceText.size()));
    if (ownership != SDGO_STICKER_CATALOG_NOT_OWNED) {
        InterlockedIncrement64(&g_guarded);
        return 0;
    }
    const auto* phrase = sdgo::sticker_localization::FindStickerPhrase(
        stickerId, sourceText);
    if (!phrase)
        return 0;
    InterlockedIncrement64(&g_matches);
    return Publish(phrase->target, output, outputChars);
}

std::uint32_t LookupShop(std::uint32_t categoryId, const wchar_t* source,
                         std::uint32_t sourceChars, wchar_t* output,
                         std::uint32_t outputChars) noexcept
{
    InterlockedIncrement64(&g_queries);
    if (!InterlockedCompareExchange(&g_installed, 0, 0) ||
        !InterlockedCompareExchange(&g_enabled, 0, 0))
        return 0;
    std::wstring sourceText;
    if (!CopySource(source, sourceChars, &sourceText)) {
        InterlockedIncrement64(&g_invalid);
        return 0;
    }
    const auto* projection =
        sdgo::sticker_localization::FindShopProjection(categoryId, sourceText);
    if (!projection)
        return 0;
    const std::uint32_t ownership =
        sdgo::sticker_localization::QueryCatalogOwnership(
            projection->sticker_id, projection->sticker_source.data(),
            static_cast<std::uint32_t>(projection->sticker_source.size()));
    if (ownership != SDGO_STICKER_CATALOG_NOT_OWNED) {
        InterlockedIncrement64(&g_guarded);
        return 0;
    }
    InterlockedIncrement64(&g_matches);
    return Publish(projection->target, output, outputChars);
}

} // namespace

extern "C" __declspec(dllexport) std::uint32_t __cdecl
SdgoStickerLocalizationLookupV1(std::uint32_t stickerId,
                                const wchar_t* source,
                                std::uint32_t sourceChars, wchar_t* output,
                                std::uint32_t outputChars)
{
    return LookupSticker(stickerId, source, sourceChars, output, outputChars);
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
SdgoStickerShopLocalizationLookupV1(std::uint32_t categoryId,
                                    const wchar_t* source,
                                    std::uint32_t sourceChars,
                                    wchar_t* output,
                                    std::uint32_t outputChars)
{
    return LookupShop(categoryId, source, sourceChars, output, outputChars);
}

extern "C" __declspec(dllexport) int __cdecl
SdgoStickerLocalizationGetSnapshotV1(
    SdgoStickerLocalizationSnapshotV1* output)
{
    if (!output || output->size < sizeof(*output) || output->version != 1)
        return 0;
    SdgoStickerLocalizationSnapshotV1 snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 1;
    snapshot.installed = static_cast<std::uint32_t>(
        InterlockedCompareExchange(&g_installed, 0, 0));
    snapshot.enabled = static_cast<std::uint32_t>(
        InterlockedCompareExchange(&g_enabled, 0, 0));
    snapshot.phrase_count = static_cast<std::uint32_t>(
        sdgo::sticker_localization::generated::kStickerPhrases.size());
    snapshot.shop_projection_count = static_cast<std::uint32_t>(
        sdgo::sticker_localization::generated::kShopProjections.size());
    snapshot.queries = static_cast<std::uint64_t>(
        InterlockedCompareExchange64(&g_queries, 0, 0));
    snapshot.matches = static_cast<std::uint64_t>(
        InterlockedCompareExchange64(&g_matches, 0, 0));
    snapshot.writes = static_cast<std::uint64_t>(
        InterlockedCompareExchange64(&g_writes, 0, 0));
    snapshot.guarded = static_cast<std::uint64_t>(
        InterlockedCompareExchange64(&g_guarded, 0, 0));
    snapshot.invalid = static_cast<std::uint64_t>(
        InterlockedCompareExchange64(&g_invalid, 0, 0));
    __try {
        *output = snapshot;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

SDGO_MOD_ENTRY
{
    InterlockedExchange(&g_installed, 0);
    InterlockedExchange(&g_enabled, 0);
    if (!api || api->version != SDGO_API_VERSION || !api->SettingInt ||
        !api->Log)
        return 0;
    const bool enabled = api->SettingInt(mod, "Enabled", 1) != 0;
    InterlockedExchange(&g_enabled, enabled ? 1 : 0);
    InterlockedExchange(&g_installed, 1);
    api->Log(mod, SDGO_LOG_INFO,
             "stock sticker localization provider ready: %u names, %u shop projections; enabled=%u",
             static_cast<unsigned>(
                 sdgo::sticker_localization::generated::kStickerPhrases.size()),
             static_cast<unsigned>(
                 sdgo::sticker_localization::generated::kShopProjections.size()),
             enabled ? 1u : 0u);
    return 1;
}
