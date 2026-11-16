#include "../../support/native_resource_overlay/native_resource_overlay.h"
#include "sdgo/sdgo_api.h"
#include "sdgo/sticker_catalog_format.h"
#include <atomic>
#include <memory>
#include <vector>
#include <windows.h>

namespace {
using namespace sdgo::stickers;
struct Value {
  uint32_t kind;
  uint8_t *record;
};
struct Pair {
  uint32_t id;
  Value *value;
};
struct Result {
  void *node;
  uint8_t inserted, pad[3];
};
static_assert(sizeof(Value) == 8 && sizeof(Pair) == 8 && sizeof(Result) == 8);
using Find = bool(__thiscall *)(void *, const uint8_t **, uint32_t, int, char);
using Data = void *(__cdecl *)();
using LoadTable = void(__thiscall *)(void *, const wchar_t *, uint32_t);
using Allocate = void *(__cdecl *)(size_t);
using Release = void(__cdecl *)(void *);
using Make = void *(__thiscall *)(void *, const Pair *);
using Insert = void(__thiscall *)(void *, Result *, void *, char);
using Register = void(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using MapFind = void(__thiscall *)(void *, void **, const uint32_t *);
const SdgoApi *g_api;
SdgoMod *g_mod;
Data data;
Find find;
LoadTable load;
Allocate allocate;
Release release;
Make make;
Insert insert;
Register reg;
MapFind mapFind;
std::vector<Entry> entries;
std::atomic<bool> ready{false};
bool attempted = false;
uint32_t published = 0;
bool mutationStarted = false;
bool Any(void *grid, uint32_t id) {
  void *n = nullptr;
  mapFind(grid, &n, &id);
  return n && n != *reinterpret_cast<void **>(static_cast<uint8_t *>(grid) + 4);
}
struct Allocation {
  uint8_t *row = nullptr;
  Value *value = nullptr;
  void *node = nullptr;
  ~Allocation() {
    if (node)
      release(node);
    if (value)
      release(value);
    if (row)
      release(row);
  }
  void Transfer() {
    row = nullptr;
    value = nullptr;
    node = nullptr;
  }
};
bool Install(void *grid) {
  if (!grid || grid != data())
    return false;
  struct NativeRow {
    uint32_t id, kind;
    const std::vector<uint8_t> *bytes;
  };
  std::vector<NativeRow> rows;
  for (const auto &e : entries) {
    rows.push_back({e.id, 38, &e.record});
    rows.push_back({e.categoryId, 34, &e.item});
  }
  for (const auto &e : rows)
    if (Any(grid, e.id)) {
      g_api->Log(g_mod, SDGO_LOG_ERROR,
                 "sticker grid key %lu already owned; no records registered",
                 static_cast<unsigned long>(e.id));
      return false;
    }
  std::vector<std::unique_ptr<Allocation>> pending;
  for (const auto &e : rows) {
    auto a = std::make_unique<Allocation>();
    a->row = static_cast<uint8_t *>(allocate(e.bytes->size()));
    a->value = static_cast<Value *>(allocate(8));
    if (!a->row || !a->value)
      return false;
    std::memcpy(a->row, e.bytes->data(), e.bytes->size());
    *a->value = {e.kind, a->row};
    const Pair pair{e.id, a->value};
    a->node = make(grid, &pair);
    if (!a->node)
      return false;
    pending.push_back(std::move(a));
  }
  for (const auto &e : rows)
    if (Any(grid, e.id))
      return false;
  for (size_t i = 0; i < rows.size(); ++i) {
    auto &a = *pending[i];
    auto *row = a.row;
    auto *node = a.node;
    // Insertion may link/consume allocations before raising an exception.
    a.Transfer();
    mutationStarted = true;
    Result result{};
    insert(grid, &result, node, 0);
    if (!result.inserted)
      return false;
    ++published;
    reg(grid, row, static_cast<uint32_t>(rows[i].bytes->size()), rows[i].kind);
    const uint8_t *actual = nullptr;
    if (!find(grid, &actual, rows[i].id, static_cast<int>(rows[i].kind), 0) ||
        actual != row)
      return false;
  }
  return true;
}
bool Guarded(void *grid) {
  __try {
    return Install(grid);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
const wchar_t *Base(const wchar_t *p) {
  const wchar_t *result = p ? p : L"";
  for (auto q = result; *q; ++q)
    if (*q == L'/' || *q == L'\\')
      result = q + 1;
  return result;
}
void __fastcall Loaded(void *grid, void *, const wchar_t *file, uint32_t kind) {
  load(grid, file, kind);
  if (attempted || kind != 38 || _wcsicmp(Base(file), L"0054.gdt"))
    return;
  attempted = true;
  if (!Guarded(grid)) {
    g_api->Log(g_mod, SDGO_LOG_ERROR,
               "sticker catalog registration failed; published=%lu",
               static_cast<unsigned long>(published));
    if (mutationStarted) {
      TerminateProcess(GetCurrentProcess(), 0xE0C03801u);
      ExitProcess(0xE0C03801u);
    }
    return;
  }
  ready.store(true, std::memory_order_release);
  g_api->Log(g_mod, SDGO_LOG_INFO, "sticker catalog ready: %u native records",
             static_cast<unsigned>(entries.size()));
}
template <class T> bool Bind(T &v, const char *n) {
  v = reinterpret_cast<T>(g_api->Symbol(g_mod, n));
  return v != nullptr;
}
bool Resources(const std::wstring &root) {
  using Getter = const SdgoResourceOverlayBrokerApiV1 *(__cdecl *)();
  for (const auto name : {L"capsule_catalog.dll", L"unit_catalog.dll",
                          L"map_catalog.dll", L"operator_catalog.dll"}) {
    const auto module = GetModuleHandleW(name);
    if (!module)
      continue;
    const auto get = reinterpret_cast<Getter>(
        GetProcAddress(module, "SdgoGetResourceOverlayBrokerV1"));
    if (!get)
      continue;
    const auto broker = get();
    if (!broker)
      continue;
    if (broker->size < sizeof(*broker) || broker->version != 1 ||
        !broker->RegisterBundle)
      return false;
    char error[512]{};
    if (!broker->RegisterBundle(root.c_str(), L"resource-overlay.ini",
                                "sticker_catalog", error, sizeof(error))) {
      g_api->Log(g_mod, SDGO_LOG_ERROR, "sticker overlay rejected: %s", error);
      return false;
    }
    return true;
  }
  std::string error;
  auto &provider = sdgo::resource_overlay::LocalProvider();
  if (!provider.RegisterBundleFiles(root.c_str(), L"resource-overlay.ini",
                                    "sticker_catalog", error) ||
      !provider.Start(g_api, g_mod, nullptr, nullptr, error)) {
    g_api->Log(g_mod, SDGO_LOG_ERROR, "sticker overlay failed: %s",
               error.c_str());
    return false;
  }
  return true;
}
const Entry *Owned(uint32_t id) {
  auto it =
      std::lower_bound(entries.begin(), entries.end(), id,
                       [](const Entry &e, uint32_t n) { return e.id < n; });
  return it != entries.end() && it->id == id ? &*it : nullptr;
}
bool WriteSize(float *w, float *h, float width, float height) {
  __try {
    if (!w || !h)
      return false;
    *w = width;
    *h = height;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
} // namespace
extern "C" __declspec(dllexport)
uint32_t __cdecl SdgoStickerCatalogOwnershipV1(uint32_t id, const wchar_t *,
                                               uint32_t) {
  if (!ready.load(std::memory_order_acquire))
    return 2;
  return Owned(id) ? 1 : 0;
}
extern "C" __declspec(dllexport)
uint32_t __cdecl SdgoStickerCatalogDefaultSizeV1(uint32_t id, float *width,
                                                 float *height) {
  if (!ready.load(std::memory_order_acquire))
    return 0;
  const auto e = Owned(id);
  return e && WriteSize(width, height, e->width, e->height) ? 1 : 0;
}
extern "C" __declspec(dllexport) const char *__cdecl SdgoStickerCatalogAbiV1() {
  return "SDGO_STICKER_CATALOG_ABI_1_KIND_38_RECORD_409_CATEGORY_34_RECORD_614_"
         "DEFAULT_SIZE_V1";
}
SDGO_MOD_ENTRY {
  g_api = api;
  g_mod = mod;
  if (!api || api->version != SDGO_API_VERSION ||
      !api->SettingInt(mod, "Enabled", 1))
    return 0;
  const std::wstring root = api->ModDirectory(mod);
  const auto path = root + L"\\catalog.bin";
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    ready.store(true, std::memory_order_release);
    return 1;
  }
  std::string error;
  if (!sdgo::stickers::Load(path, entries, error)) {
    api->Log(mod, SDGO_LOG_ERROR, "%s", error.c_str());
    return 0;
  }
  if (!Bind(data, "grid.data") || !Bind(find, "grid.find") ||
      !Bind(allocate, "crt.operator_new") ||
      !Bind(release, "crt.operator_delete") || !Bind(make, "grid.make_node") ||
      !Bind(insert, "grid.insert_node") || !Bind(reg, "grid.register_record") ||
      !Bind(mapFind, "grid.map_find"))
    return 0;
  if (!api->HookCode(mod, "sticker_catalog.load_table",
                     reinterpret_cast<void *>(&Loaded),
                     reinterpret_cast<void **>(&load)))
    return 0;
  // No fallible initialization follows publication to a shared broker.
  if (!Resources(root))
    return 0;
  return 1;
}
