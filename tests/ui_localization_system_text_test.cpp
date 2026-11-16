#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "../mods/ui_localization/system_text.h"

namespace {

using namespace sdgo::ui_localization::system_text;

unsigned g_checks = 0;
const char* g_hookName = nullptr;
void* g_detour = nullptr;

void Check(bool condition, const char* message)
{
    ++g_checks;
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", message);
    if (!condition)
        throw std::runtime_error(message);
}

void WriteId(Record* record, std::uint32_t id)
{
    std::memcpy(record->data(), &id, sizeof(id));
}

void WriteText(Record* record, const wchar_t* text)
{
    std::memset(record->data() + kTextOffset, 0, kTextCharacters * 2);
    for (std::size_t index = 0; text[index]; ++index) {
        const std::uint16_t value = static_cast<std::uint16_t>(text[index]);
        std::memcpy(record->data() + kTextOffset + index * 2, &value,
                    sizeof(value));
    }
}

std::wstring ReadText(const Record& record)
{
    wchar_t text[kTextCharacters] = {};
    for (std::size_t index = 0; index < kTextCharacters; ++index) {
        std::uint16_t value = 0;
        std::memcpy(&value, record.data() + kTextOffset + index * 2,
                    sizeof(value));
        text[index] = static_cast<wchar_t>(value);
        if (!value)
            return text;
    }
    throw std::runtime_error("record text has no terminator");
}


void WriteStickerId(StickerRecord* record, std::uint32_t id)
{
    std::memcpy(record->data(), &id, sizeof(id));
}

void WriteStickerText(StickerRecord* record, const wchar_t* text)
{
    std::memset(record->data() + kStickerTextOffset, 0,
                kStickerTextCharacters * sizeof(wchar_t));
    for (std::size_t index = 0; text[index]; ++index) {
        const std::uint16_t value = static_cast<std::uint16_t>(text[index]);
        std::memcpy(record->data() + kStickerTextOffset + index * 2, &value,
                    sizeof(value));
    }
}

std::wstring ReadStickerText(const StickerRecord& record)
{
    wchar_t text[kStickerTextCharacters] = {};
    for (std::size_t index = 0; index < kStickerTextCharacters; ++index) {
        std::uint16_t value = 0;
        std::memcpy(&value,
                    record.data() + kStickerTextOffset + index * 2,
                    sizeof(value));
        text[index] = static_cast<wchar_t>(value);
        if (!value)
            return text;
    }
    throw std::runtime_error("sticker text has no terminator");
}

std::uint32_t __cdecl FakeStickerLookup(std::uint32_t stickerId,
                                    const wchar_t* source,
                                    std::uint32_t sourceChars,
                                    wchar_t* output,
                                    std::uint32_t outputChars)
{
    constexpr wchar_t kSource[] = L"Triangle Sticker";
    constexpr wchar_t kTarget[] = L"三角形贴纸";
    if (stickerId != 240005 ||
        sourceChars != std::size(kSource) - 1 ||
        std::wstring_view(source, sourceChars) !=
            std::wstring_view(kSource, std::size(kSource) - 1))
        return 0;
    constexpr std::uint32_t required =
        static_cast<std::uint32_t>(std::size(kTarget));
    if (output && outputChars >= required)
        std::memcpy(output, kTarget, sizeof(kTarget));
    return required;
}

std::uint32_t __cdecl MalformedStickerLookup(
    std::uint32_t, const wchar_t*, std::uint32_t, wchar_t*, std::uint32_t)
{
    return 3;
}

int HookCode(SdgoMod*, const char* name, void* detour, void** original)
{
    g_hookName = name;
    g_detour = detour;
    *original = reinterpret_cast<void*>(1);
    return 1;
}

} // namespace

int main()
{
    try {
        Record source{};
        source.fill(0xA5);
        WriteId(&source, 126956);
        source[4] = 0x5A;
        WriteText(&source, L"Does not match the version. To end the game");
        const std::array<std::uint8_t, 4> style = {0xDE, 0xAD, 0xBE, 0xEF};
        std::memcpy(source.data() + kStyleOffset, style.data(), style.size());
        const Record original = source;

        Record output{};
        output.fill(0xCC);
        Check(TranslateRecord(source.data(), source.size(), 126956, kTableType,
                              &output) == PatchResult::replaced,
              "an exact system text ID and source phrase is translated");
        Check(source == original, "translation never mutates the native row");
        Check(ReadText(output) == L"版本不匹配，游戏即将结束。",
              "the translated row contains the reviewed Chinese phrase");
        for (std::size_t index = 0; index < source.size(); ++index) {
            if (index >= kTextOffset && index < kStyleOffset)
                continue;
            Check(output[index] == source[index],
                  "all bytes outside the fixed text slot are preserved");
        }

        Record sentinel{};
        sentinel.fill(0x7B);
        output = sentinel;
        WriteText(&source, L"Does not match the version. To end the game?");
        Check(TranslateRecord(source.data(), source.size(), 126956, kTableType,
                              &output) == PatchResult::unchanged &&
                  output == sentinel,
              "an unknown source phrase leaves the output untouched");
        Check(TranslateRecord(original.data(), original.size(), 126956, 2,
                              &output) == PatchResult::unchanged &&
                  output == sentinel,
              "non-UI tables are never patched");

        const std::wstring overlong(kTextCharacters, L'长');
        Check(PatchRecord(original.data(), original.size(), 126956, kTableType,
                          overlong, &output) == PatchResult::invalid &&
                  output == sentinel,
              "text that cannot fit the native slot is rejected");
        Check(TranslateRecord(original.data(), original.size(), 126957,
                              kTableType, &output) == PatchResult::invalid &&
                  output == sentinel,
              "a requested ID that disagrees with the row is rejected");

        Record unterminated = original;
        for (std::size_t index = 0; index < kTextCharacters; ++index) {
            const std::uint16_t value = L'X';
            std::memcpy(unterminated.data() + kTextOffset + index * 2, &value,
                        sizeof(value));
        }
        Check(TranslateRecord(unterminated.data(), unterminated.size(), 126956,
                              kTableType, &output) == PatchResult::invalid &&
                  output == sentinel,
              "a row without a bounded text terminator is rejected");

        StickerRecord sticker{};
        sticker.fill(0xA5);
        WriteStickerId(&sticker, 240005);
        sticker[4] = 0;
        WriteStickerText(&sticker, L"Triangle Sticker");
        const StickerRecord originalSticker = sticker;
        StickerRecord stickerOutput{};
        stickerOutput.fill(0xCC);
        Check(TranslateStickerRecordWith(
                  sticker.data(), sticker.size(), 240005, kStickerTableType,
                  &FakeStickerLookup, &stickerOutput) == PatchResult::replaced,
              "an exact stock sticker ID and source are translated");
        Check(sticker == originalSticker,
              "sticker translation never mutates the native row");
        Check(ReadStickerText(stickerOutput) == L"三角形贴纸",
              "the sticker row contains the reviewed Chinese phrase");
        for (std::size_t index = 0; index < sticker.size(); ++index) {
            if (index >= kStickerTextOffset &&
                index < kStickerTextOffset +
                            kStickerTextCharacters * sizeof(wchar_t))
                continue;
            Check(stickerOutput[index] == sticker[index],
                  "all bytes outside the sticker name slot are preserved");
        }

        StickerRecord stickerSentinel{};
        stickerSentinel.fill(0x7B);
        stickerOutput = stickerSentinel;
        WriteStickerText(&sticker, L"Triangle Sticker?");
        Check(TranslateStickerRecordWith(
                  sticker.data(), sticker.size(), 240005, kStickerTableType,
                  &FakeStickerLookup, &stickerOutput) == PatchResult::unchanged &&
                  stickerOutput == stickerSentinel,
              "source drift never receives an ID-only sticker translation");
        sticker = originalSticker;
        WriteStickerId(&sticker, 249999);
        WriteStickerText(&sticker, L"大肥鱼");
        Check(TranslateStickerRecordWith(
                  sticker.data(), sticker.size(), 249999, kStickerTableType,
                  &FakeStickerLookup, &stickerOutput) == PatchResult::unchanged &&
                  stickerOutput == stickerSentinel,
              "an exact custom Chinese sticker name remains unchanged");
        Check(TranslateStickerRecordWith(
                  originalSticker.data(), originalSticker.size(), 240005,
                  kTableType, &FakeStickerLookup, &stickerOutput) ==
                      PatchResult::unchanged &&
                  stickerOutput == stickerSentinel,
              "the system-text table is never treated as a sticker row");
        sticker = originalSticker;
        sticker[4] = 1;
        Check(TranslateStickerRecordWith(
                  sticker.data(), sticker.size(), 240005, kStickerTableType,
                  &FakeStickerLookup, &stickerOutput) == PatchResult::invalid &&
                  stickerOutput == stickerSentinel,
              "a sticker row with an unexpected security byte is refused");
        Check(TranslateStickerRecordWith(
                  originalSticker.data(), originalSticker.size(), 240006,
                  kStickerTableType, &FakeStickerLookup, &stickerOutput) ==
                      PatchResult::invalid &&
                  stickerOutput == stickerSentinel,
              "a sticker row whose ID differs from the request is refused");
        Check(TranslateStickerRecordWith(
                  originalSticker.data(), originalSticker.size(), 240005,
                  kStickerTableType, &MalformedStickerLookup,
                  &stickerOutput) == PatchResult::invalid &&
                  stickerOutput == stickerSentinel,
              "a malformed provider result cannot alter the native row");
        const std::wstring stickerOverlong(kStickerTextCharacters, L'长');
        Check(PatchStickerRecord(
                  originalSticker.data(), originalSticker.size(), 240005,
                  kStickerTableType, stickerOverlong, &stickerOutput) ==
                      PatchResult::invalid &&
                  stickerOutput == stickerSentinel,
              "a sticker name that cannot fit the native slot is rejected");

        SdgoApi api{};
        api.size = sizeof(api);
        api.HookCode = &HookCode;
        Check(Install(&api, reinterpret_cast<SdgoMod*>(1)) &&
                  g_hookName && !std::strcmp(g_hookName,
                                             "ui_localization.grid_find") &&
                  g_detour,
              "installation owns only the declared grid-find hook");
        const Stats stats = Snapshot();
        Check(stats.lookups == 0 && stats.replaced == 0 && stats.invalid == 0,
              "pure record tests do not alter runtime hook statistics");

        std::printf("%u checks passed\n", g_checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
