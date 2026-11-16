#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "sdgo/sticker_localization.h"

namespace sdgo::shop_localization::sticker_names {

enum class Resolution {
    not_sticker_category,
    translated,
    preserve_source
};

using Lookup = bool (*)(std::uint32_t categoryId, std::wstring_view source,
                        std::wstring* output) noexcept;

inline bool IsStockCategory(std::uint32_t categoryId) noexcept
{
    return categoryId / 100000u == 17u;
}

inline bool ProviderLookup(std::uint32_t categoryId, std::wstring_view source,
                           std::wstring* output) noexcept
{
    return sdgo::sticker_localization::TryLookupShopCategory(
        categoryId, source.data(), static_cast<std::uint32_t>(source.size()),
        output);
}

inline Resolution ResolveWith(std::uint32_t categoryId,
                              std::wstring_view source, Lookup lookup,
                              std::wstring* output) noexcept
{
    if (!IsStockCategory(categoryId))
        return Resolution::not_sticker_category;
    if (!lookup || !output)
        return Resolution::preserve_source;
    try {
        std::wstring localized;
        if (!lookup(categoryId, source, &localized) || localized.empty())
            return Resolution::preserve_source;
        output->swap(localized);
        return Resolution::translated;
    } catch (...) {
        return Resolution::preserve_source;
    }
}

inline Resolution Resolve(std::uint32_t categoryId, std::wstring_view source,
                          std::wstring* output) noexcept
{
    return ResolveWith(categoryId, source, &ProviderLookup, output);
}

} // namespace sdgo::shop_localization::sticker_names
