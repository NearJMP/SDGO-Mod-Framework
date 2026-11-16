#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "dictionary.generated.h"

namespace sdgo::sticker_localization {

inline const generated::StickerPhrase* FindStickerPhrase(
    std::uint32_t id, std::wstring_view source) noexcept
{
    const auto& values = generated::kStickerPhrases;
    const auto found = std::lower_bound(
        values.begin(), values.end(), id,
        [](const generated::StickerPhrase& value, std::uint32_t key) {
            return value.id < key;
        });
    return found != values.end() && found->id == id && found->source == source
               ? &*found
               : nullptr;
}

inline const generated::ShopProjection* FindShopProjection(
    std::uint32_t categoryId, std::wstring_view source) noexcept
{
    const auto& values = generated::kShopProjections;
    const auto found = std::lower_bound(
        values.begin(), values.end(), categoryId,
        [](const generated::ShopProjection& value, std::uint32_t key) {
            return value.category_id < key;
        });
    if (found == values.end() || found->category_id != categoryId)
        return nullptr;
    for (auto cursor = found;
         cursor != values.end() && cursor->category_id == categoryId;
         ++cursor) {
        if (cursor->source == source)
            return &*cursor;
    }
    return nullptr;
}

} // namespace sdgo::sticker_localization
