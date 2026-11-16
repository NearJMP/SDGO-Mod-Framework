#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace sdgo::stickers {
constexpr uint32_t kKind = 38, kRecordBytes = 409, kMaxEntries = 2048;
inline uint32_t Hash(const void *data, size_t size) {
  auto p = static_cast<const uint8_t *>(data);
  uint32_t h = 2166136261u;
  while (size--)
    h = (h ^ *p++) * 16777619u;
  return h;
}
inline uint32_t U32(const void *p) {
  uint32_t n;
  std::memcpy(&n, p, 4);
  return n;
}
inline std::wstring Text(const uint8_t *p, size_t offset, size_t capacity) {
  std::wstring s;
  for (size_t i = 0; i < capacity; ++i) {
    uint16_t c;
    std::memcpy(&c, p + offset + 2 * i, 2);
    if (!c)
      return s;
    if (c < 32 || c == 127 || (c >= 0xd800 && c <= 0xdfff))
      return {};
    s.push_back(static_cast<wchar_t>(c));
  }
  return {};
}
inline bool Resource(const std::wstring &path, const wchar_t *root,
                     const wchar_t *extension, uint32_t id) {
  return path == std::wstring(root) + L"\\sdgo_stickers_" +
                     std::to_wstring(id) + extension;
}
struct Entry {
  uint32_t id = 0, categoryId = 0;
  float width = 2, height = 2;
  std::vector<uint8_t> record, item;
};
inline bool Valid(const Entry &e) {
  return e.id > 0 && e.id <= 9999999 && e.record.size() == kRecordBytes &&
         U32(e.record.data()) == e.id && e.record[4] == 0 &&
         !Text(e.record.data(), 5, 50).empty() && e.record[105] == 0 &&
         e.record[106] == 0 && e.record[107] == 0 && e.record[108] == 1 &&
         U32(e.record.data() + 205) == 0 &&
         Resource(Text(e.record.data(), 109, 48), L"sprs", L".spr", e.id) &&
         Resource(Text(e.record.data(), 209, 100), L"txrs", L".txr", e.id) &&
         std::isfinite(e.width) && std::isfinite(e.height) && e.width >= .2f &&
         e.width <= 8 && e.height >= .2f && e.height <= 8 &&
         e.categoryId >= 1790000 && e.categoryId <= 1799999 &&
         e.item.size() == 614 && U32(e.item.data()) == e.categoryId &&
         e.item[4] == 0 &&
         Text(e.item.data(), 5, 50) == Text(e.record.data(), 5, 50) &&
         U32(e.item.data() + 105) == e.id && U32(e.item.data() + 109) == 0 &&
         e.item[113] == 0 &&
         Resource(Text(e.item.data(), 114, 50), L"sprs", L".spr", e.id);
}
// Header: magic[8], version, headerBytes, count, payloadBytes, FNV1a, reserved.
// Entry: id, category id, record FNV1a, default width/height(float32),
// packed native kind-38 row[409] and kind-34 row[614].
inline bool Decode(const std::vector<uint8_t> &raw, std::vector<Entry> &entries,
                   std::string &error) {
  entries.clear();
  if (raw.size() < 32 || std::memcmp(raw.data(), "SDGOST1\0", 8) ||
      U32(raw.data() + 8) != 1 || U32(raw.data() + 12) != 32 ||
      U32(raw.data() + 28)) {
    error = "invalid sticker catalog header";
    return false;
  }
  const uint32_t count = U32(raw.data() + 16), size = U32(raw.data() + 20);
  if (count > kMaxEntries || size != count * 1043u ||
      raw.size() != 32u + size ||
      Hash(raw.data() + 32, size) != U32(raw.data() + 24)) {
    error = "invalid sticker catalog size or hash";
    return false;
  }
  uint32_t last = 0;
  std::set<uint32_t> identities;
  for (uint32_t i = 0; i < count; ++i) {
    const auto p = raw.data() + 32 + 1043 * i;
    Entry e;
    e.id = U32(p);
    e.categoryId = U32(p + 4);
    std::memcpy(&e.width, p + 12, 4);
    std::memcpy(&e.height, p + 16, 4);
    e.record.assign(p + 20, p + 429);
    e.item.assign(p + 429, p + 1043);
    if (e.id <= last || !Valid(e) || Hash(e.record.data(), 409) != U32(p + 8) ||
        !identities.insert(e.id).second ||
        !identities.insert(e.categoryId).second) {
      error = "invalid sticker record or ordering";
      return false;
    }
    last = e.id;
    entries.push_back(std::move(e));
  }
  return true;
}
inline bool Load(const std::wstring &path, std::vector<Entry> &entries,
                 std::string &error) {
  FILE *f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") || !f) {
    error = "cannot open sticker catalog";
    return false;
  }
  std::vector<uint8_t> raw(32 + kMaxEntries * 1043u + 1);
  const auto n = fread(raw.data(), 1, raw.size(), f);
  const bool bad = ferror(f) != 0;
  fclose(f);
  raw.resize(n);
  if (bad) {
    error = "cannot read sticker catalog";
    return false;
  }
  return Decode(raw, entries, error);
}
} // namespace sdgo::stickers
