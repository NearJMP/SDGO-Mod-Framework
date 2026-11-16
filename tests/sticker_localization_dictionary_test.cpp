#include <cstdio>
#include <stdexcept>
#include <string_view>

#include "sdgo/sticker_localization.h"

#include "../mods/sticker_localization/dictionary.h"
#include "../mods/shop_localization/sticker_names.h"

namespace {

unsigned g_checks = 0;

void Check(bool condition, const char* message)
{
    ++g_checks;
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", message);
    if (!condition)
        throw std::runtime_error(message);
}


bool ExactShopLookup(std::uint32_t categoryId, std::wstring_view source,
                     std::wstring* output) noexcept
{
    if (categoryId != 1710001u || source != L"삼각형 스티커" || !output)
        return false;
    try {
        *output = L"三角形贴纸";
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

int main()
{
    using namespace sdgo::sticker_localization;
    using namespace sdgo::sticker_localization::generated;
    try {
        Check(kBaselineStickerCount == 414u &&
                  kBaselineEnglishCount == 286u &&
                  kBaselineNonAsciiCount == 127u &&
                  kReviewedNonAsciiCount == 12u &&
                  kExcludedIncompleteAsciiCount == 1u,
              "the generated metadata reports the reviewed baseline composition");
        Check(kStickerPhrases.size() == 298,
              "the reviewed bank contains 286 English and 12 non-ASCII rows");
        Check(kShopProjections.size() == 347,
              "the reviewed bank contains all 347 exact shop projections");
        for (const auto& phrase : kStickerPhrases) {
            Check(phrase.source.size() < SDGO_STICKER_LOCALIZATION_NAME_CAPACITY,
                  "every source fits the native sticker name slot");
            Check(phrase.target.size() < SDGO_STICKER_LOCALIZATION_NAME_CAPACITY,
                  "every target fits the native sticker name slot");
            Check(FindStickerPhrase(phrase.id, phrase.source) == &phrase,
                  "every reviewed phrase resolves by exact ID and source");
        }
        Check(!FindStickerPhrase(240005u, L"Triangle Sticker?"),
              "source drift is never accepted by ID alone");
        Check(!FindStickerPhrase(249999u, L"大肥鱼"),
              "an imported custom name is outside the stock phrase bank");
        const auto* triangle = FindStickerPhrase(240005u, L"Triangle Sticker");
        Check(triangle && triangle->target == L"三角形贴纸",
              "the reviewed triangle translation is exact");
        const auto* nonAscii =
            FindStickerPhrase(240358u, L"삼국전 여포군 스티커");
        Check(nonAscii && nonAscii->target == L"三国传吕布军贴纸",
              "a same-source reviewed non-ASCII translation is exact");
        Check(!FindStickerPhrase(240009u, L"즐 Sticker"),
              "an ambiguous transliterated token remains verbatim");
        Check(!FindStickerPhrase(240335u, L" Sticker"),
              "the incomplete ASCII source remains outside the phrase bank");
        for (const auto& projection : kShopProjections) {
            Check(projection.category_id / 100000u == 17u,
                  "every shop projection belongs to the sticker category family");
            Check(FindShopProjection(projection.category_id,
                                     projection.source) == &projection,
                  "every shop projection resolves by exact category and source");
            Check(projection.sticker_source.size() <
                      SDGO_STICKER_LOCALIZATION_NAME_CAPACITY,
                  "every typed sticker source fits the native name slot");
            Check(projection.target.size() <
                      SDGO_STICKER_LOCALIZATION_NAME_CAPACITY,
                  "every reviewed category target fits the native name slot");
        }
        Check(!FindShopProjection(1710001u, L"삼각형 스티커?"),
              "shop source drift is never accepted by category alone");
        const auto* nonAsciiProjection =
            FindShopProjection(1720176u, L"삼국전 여포군 스티커");
        Check(nonAsciiProjection && nonAsciiProjection->sticker_id == 240358u &&
                  nonAsciiProjection->sticker_source == L"삼국전 여포군 스티커" &&
                  nonAsciiProjection->target == L"三国传吕布军贴纸",
              "the same-source non-ASCII category projection is exact");
        const auto* transliterated =
            FindShopProjection(1710005u, L"즐 스티커");
        Check(transliterated && transliterated->sticker_id == 240009u &&
                  transliterated->sticker_source == L"즐 Sticker" &&
                  transliterated->target == L"“兹尔”贴纸（音译）",
              "an exact category retains its previously reviewed transliteration");
        const auto* incompleteAlias =
            FindShopProjection(1720156u, L"龍 Sticker");
        Check(incompleteAlias && incompleteAlias->sticker_id == 240335u &&
                  incompleteAlias->sticker_source == L" Sticker" &&
                  incompleteAlias->target == L"龍贴纸",
              "an exact category alias retains its reviewed label without translating the incomplete sticker source");

        using sdgo::shop_localization::sticker_names::Resolution;
        using sdgo::shop_localization::sticker_names::ResolveWith;
        std::wstring shopOutput = L"sentinel";
        Check(ResolveWith(1710001u, L"삼각형 스티커", &ExactShopLookup,
                          &shopOutput) == Resolution::translated &&
                  shopOutput == L"三角形贴纸",
              "the sticker category policy returns an exact provider target");
        shopOutput = L"sentinel";
        Check(ResolveWith(1710001u, L"삼각형 스티커?", &ExactShopLookup,
                          &shopOutput) == Resolution::preserve_source &&
                  shopOutput == L"sentinel",
              "a declined sticker category cannot fall through to legacy text");
        Check(ResolveWith(1600001u, L"Other", &ExactShopLookup,
                          &shopOutput) == Resolution::not_sticker_category,
              "non-sticker categories retain the existing shop localization path");
        std::printf("%u checks passed\n", g_checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
