#include "sdgo/sticker_catalog_format.h"
#include <cstdlib>
#include <limits>

using namespace sdgo::stickers;
namespace {
void Check(bool ok, const char *message) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(2);
  }
}
void Put(std::vector<uint8_t> &data, size_t at, uint32_t value) {
  std::memcpy(data.data() + at, &value, 4);
}
void TextAt(std::vector<uint8_t> &data, size_t at, const std::wstring &value) {
  std::memcpy(data.data() + at, value.c_str(), (value.size() + 1) * 2);
}
Entry Example(uint32_t id, uint32_t category) {
  Entry e;
  e.id = id;
  e.categoryId = category;
  e.record.resize(409);
  e.item.resize(614);
  Put(e.record, 0, id);
  TextAt(e.record, 5, L"Example");
  e.record[108] = 1;
  TextAt(e.record, 109,
         L"sprs\\sdgo_stickers_" + std::to_wstring(id) + L".spr");
  TextAt(e.record, 209,
         L"txrs\\sdgo_stickers_" + std::to_wstring(id) + L".txr");
  Put(e.item, 0, category);
  TextAt(e.item, 5, L"Example");
  Put(e.item, 105, id);
  TextAt(e.item, 114, L"sprs\\sdgo_stickers_" + std::to_wstring(id) + L".spr");
  return e;
}
std::vector<uint8_t> Encode(const std::vector<Entry> &entries) {
  std::vector<uint8_t> out(32 + 1043 * entries.size());
  std::memcpy(out.data(), "SDGOST1\0", 8);
  Put(out, 8, 1);
  Put(out, 12, 32);
  Put(out, 16, static_cast<uint32_t>(entries.size()));
  Put(out, 20, static_cast<uint32_t>(out.size() - 32));
  size_t at = 32;
  for (const auto &e : entries) {
    Put(out, at, e.id);
    Put(out, at + 4, e.categoryId);
    Put(out, at + 8, Hash(e.record.data(), e.record.size()));
    std::memcpy(out.data() + at + 12, &e.width, 4);
    std::memcpy(out.data() + at + 16, &e.height, 4);
    std::memcpy(out.data() + at + 20, e.record.data(), 409);
    std::memcpy(out.data() + at + 429, e.item.data(), 614);
    at += 1043;
  }
  Put(out, 24, Hash(out.data() + 32, out.size() - 32));
  return out;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  std::vector<Entry> decoded;
  std::string error;
  if (argc == 2) {
    Check(Load(argv[1], decoded, error), error.c_str());
    std::printf("native stickers=%u rows=%u\n",
                static_cast<unsigned>(decoded.size()),
                static_cast<unsigned>(decoded.size() * 2));
    return 0;
  }
  auto e = Example(8000001, 1790001);
  Check(Valid(e), "ordinary linked records accepted");
  auto raw = Encode({e});
  Check(Decode(raw, decoded, error) && decoded.size() == 1,
        "native format decodes exact pair");
  raw.back() ^= 1;
  Check(!Decode(raw, decoded, error), "payload corruption rejected");
  auto other = Example(8000002, 1790001);
  Check(!Decode(Encode({e, other}), decoded, error),
        "duplicate global category rejected");
  other = Example(8000002, 1790002);
  Put(other.item, 105, 8000001);
  Check(!Decode(Encode({other}), decoded, error),
        "cross-project RefIndex rejected");
  other = Example(8000002, 1790002);
  TextAt(other.item, 114, L"sprs\\foreign.spr");
  Check(!Decode(Encode({other}), decoded, error),
        "foreign thumbnail path rejected");
  other = Example(8000002, 1790002);
  other.width = std::numeric_limits<float>::quiet_NaN();
  Check(!Decode(Encode({other}), decoded, error),
        "nonfinite dimensions rejected");
  raw = Encode({e});
  raw.push_back(0);
  Check(!Decode(raw, decoded, error), "trailing bytes rejected");
  std::printf("sticker native format checks passed\n");
  return 0;
}
