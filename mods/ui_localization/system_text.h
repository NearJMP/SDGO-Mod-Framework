#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sdgo/sdgo_api.h"

namespace sdgo::ui_localization::system_text {

inline constexpr int kTableType = 0x3B;
inline constexpr std::size_t kRecordBytes = 209;
inline constexpr std::size_t kTextOffset = 5;
inline constexpr std::size_t kTextCharacters = 100;
inline constexpr std::size_t kStyleOffset = 205;

inline constexpr int kStickerTableType = 38;
inline constexpr std::size_t kStickerRecordBytes = 409;
inline constexpr std::size_t kStickerTextOffset = 5;
inline constexpr std::size_t kStickerTextCharacters = 50;

using Record = std::array<std::uint8_t, kRecordBytes>;
using StickerRecord = std::array<std::uint8_t, kStickerRecordBytes>;
using StickerLookup = std::uint32_t(__cdecl*)(
    std::uint32_t stickerId, const wchar_t* source,
    std::uint32_t sourceChars, wchar_t* output,
    std::uint32_t outputChars);

enum class PatchResult {
    unchanged,
    replaced,
    invalid
};

// Pure record helpers. They never mutate source or output on refusal.
PatchResult PatchRecord(const std::uint8_t* source, std::size_t sourceBytes,
                        std::uint32_t requestedId, int type,
                        std::wstring_view target, Record* output) noexcept;
PatchResult TranslateRecord(const std::uint8_t* source, std::size_t sourceBytes,
                            std::uint32_t requestedId, int type,
                            Record* output) noexcept;
PatchResult PatchStickerRecord(const std::uint8_t* source,
                               std::size_t sourceBytes,
                               std::uint32_t requestedId, int type,
                               std::wstring_view target,
                               StickerRecord* output) noexcept;
PatchResult TranslateStickerRecordWith(
    const std::uint8_t* source, std::size_t sourceBytes,
    std::uint32_t requestedId, int type, StickerLookup lookup,
    StickerRecord* output) noexcept;
PatchResult TranslateStickerRecord(const std::uint8_t* source,
                                   std::size_t sourceBytes,
                                   std::uint32_t requestedId, int type,
                                   StickerRecord* output) noexcept;

struct Stats {
    std::uint64_t lookups;
    std::uint64_t replaced;
    std::uint64_t invalid;
};

bool Install(const SdgoApi* api, SdgoMod* mod);
Stats Snapshot();

} // namespace sdgo::ui_localization::system_text
