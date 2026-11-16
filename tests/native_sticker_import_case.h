// Neutral imported-image acceptance through genuine production page controls.
bool importSelectionRequested = false;
int StickerImportCase(const SdgoGameTestContextV1 *context, void *root) {
  using namespace sdgo::surface_decal;
  using String = sdgo::hangar::ui::NativeString;
  using Find = void *(__thiscall *)(void *, const String *, int);
  const auto find = reinterpret_cast<Find>(
      ownerApi->Symbol(ownerMod, "hangar.ui_find_control"));
  const auto module = GetModuleHandleW(L"unit_appearance_client.dll");
  const auto observe = reinterpret_cast<test::LayerObserve>(
      GetProcAddress(module, test::layerExport));
  test::LayerSnapshot snapshot;
  if (!find || !observe || !observe(0, &snapshot))
    return SDGO_GAME_TEST_FAIL;
  if (!snapshot.editing || snapshot.pending)
    return SDGO_GAME_TEST_WAIT;
  const auto app = reinterpret_cast<void *(__cdecl *)()>(
      ownerApi->Symbol(ownerMod, "game.app"));
  if (!app || !app())
    return SDGO_GAME_TEST_WAIT;
  const auto window = At<HWND>(app(), 0x3c);
  RECT client{};
  if (!GetClientRect(window, &client) || client.right <= 0 ||
      client.bottom <= 0)
    return SDGO_GAME_TEST_FAIL;
  const auto click = [&](const wchar_t *name) {
    String key;
    key.length = unsigned(wcslen(name));
    key.capacity = key.length;
    key.data.pointer = name;
    if (key.length < 8) {
      key.capacity = 7;
      std::memcpy(key.data.small, name, (key.length + 1) * 2);
    }
    auto *c = find(root, &key, 4);
    if (!c || !At<uint8_t>(c, 0x162) || !At<uint8_t>(c, 0x167))
      return false;
    const int x = int((At<float>(root, 0x148) + At<float>(c, 0x148) +
                       At<int>(c, 0x154) * .5) *
                      client.right / 1024);
    const int y = int((At<float>(root, 0x14c) + At<float>(c, 0x14c) +
                       At<int>(c, 0x158) * .5) *
                      client.bottom / 768);
    ownerApi->Log(ownerMod, SDGO_LOG_INFO,
                  "import probe click %ls client=%d,%d root=%.0f,%.0f "
                  "control=%.0f,%.0f,%d,%d",
                  name, x, y, At<float>(root, 0x148), At<float>(root, 0x14c),
                  At<float>(c, 0x148), At<float>(c, 0x14c), At<int>(c, 0x154),
                  At<int>(c, 0x158));
    // Use the actual native button event dispatcher, as the factory's regular
    // UI does. Geometry placement below still uses the production ray picker.
    auto *service = Service("surface_decal.sticker_vtable");
    using Event = void(__stdcall *)(void *, unsigned, std::uintptr_t, void *);
    if (!service)
      return false;
    At<Event>(At<void *>(service), 14 * 4)(
        service, 0x101, reinterpret_cast<std::uintptr_t>(c) + 0x118, c);
    return true;
  };
  wchar_t text[64]{};
  GetEnvironmentVariableW(L"SDGO_STICKER_IMPORT_ID", text, 64);
  const auto id = wcstoul(text, nullptr, 10);
  if (!id)
    return SDGO_GAME_TEST_FAIL;
  static unsigned phase = 0;
  static uint64_t revision = 0;
  static layers::Design proposal;
  layers::Design saved, draft;
  if (!layers::wire::Decode(snapshot.saved.data(), snapshot.savedBytes,
                            saved) ||
      !layers::wire::Decode(snapshot.preview.data(), snapshot.previewBytes,
                            draft))
    return SDGO_GAME_TEST_WAIT;
  if (phase == 0) {
    const auto frame = CaptureScene(L"00-imported-sticker-blue-shield-before");
    if (frame < 1)
      return frame < 0 ? SDGO_GAME_TEST_FAIL : SDGO_GAME_TEST_WAIT;
    if (GetEnvironmentVariableW(L"SDGO_STICKER_IMPORT_INSPECT", nullptr, 0)) {
      context->Assert(snapshot.models > 0 && snapshot.stockTypes > 0,
                      "real native model and owned sticker inventory are "
                      "available for visual inspection");
      return SDGO_GAME_TEST_PASS;
    }
    revision = saved.revision;
    if (!saved.items.empty()) {
      context->Assert(0, "import authoring uses an existing test unit without "
                         "a prior custom design");
      return SDGO_GAME_TEST_FAIL;
    }
    if (!click(L"SurfaceAdd"))
      return SDGO_GAME_TEST_WAIT;
    phase = 1;
  } else if (phase == 1) {
    GetEnvironmentVariableW(L"SDGO_SURFACE_STICKER_ROW", text, 64);
    const auto row = wcstoul(text, nullptr, 10);
    const auto frame = CaptureScene(L"01-imported-sticker-native-library");
    if (frame < 1)
      return frame < 0 ? SDGO_GAME_TEST_FAIL : SDGO_GAME_TEST_WAIT;
    if (!snapshot.stockTypes || row >= ui::LayerLayout::visibleRows ||
        !click((L"SurfaceRow" + std::to_wstring(row)).c_str()))
      return SDGO_GAME_TEST_WAIT;
    importSelectionRequested = true;
    phase = 2;
  } else if (phase == 2) {
    const auto *item = layers::Find(draft, snapshot.selected);
    if (!item) {
      const auto frame = CaptureScene(L"01-after-library-selection");
      if (frame < 1)
        return frame < 0 ? SDGO_GAME_TEST_FAIL : SDGO_GAME_TEST_WAIT;
      context->Assert(
          0, "native library click selects an editable imported sticker layer");
      return SDGO_GAME_TEST_FAIL;
    }
    context->Assert(item->sticker == id,
                    "native library selected the actual imported sticker ID");
    if (item->sticker != id)
      return SDGO_GAME_TEST_FAIL;
    double x = .55, y = .47;
    if (GetEnvironmentVariableW(L"SDGO_STICKER_IMPORT_X", text, 64))
      x = wcstod(text, nullptr);
    if (GetEnvironmentVariableW(L"SDGO_STICKER_IMPORT_Y", text, 64))
      y = wcstod(text, nullptr);
    if (x < .1 || x > .9 || y < .1 || y > .9)
      return SDGO_GAME_TEST_FAIL;
    // The isolated hidden graphics window can live on a different thread.
    // Reuse the existing native-test entry to the production ray picker;
    // it reads the real draw matrices/geometry and changes only the draft.
    using Place = int(__cdecl *)(double, double);
    const auto place = reinterpret_cast<Place>(
        GetProcAddress(module, "SdgoSurfaceDecalTestPlaceV1"));
    if (!place || !place(client.right * x, client.bottom * y)) {
      context->Assert(
          0,
          "configured shield point is accepted by the production ray picker");
      return SDGO_GAME_TEST_FAIL;
    }
    phase = 3;
  } else if (phase == 3) {
    const auto *item = layers::Find(draft, snapshot.selected);
    if (!item || !item->placed) {
      context->Assert(0, "configured shield point hits native geometry through "
                         "the production ray picker");
      return SDGO_GAME_TEST_FAIL;
    }
    using Size = uint32_t(__cdecl *)(uint32_t, float *, float *);
    const auto catalog = GetModuleHandleW(L"sticker_catalog.dll");
    const auto defaults = catalog
                              ? reinterpret_cast<Size>(GetProcAddress(
                                    catalog, "SdgoStickerCatalogDefaultSizeV1"))
                              : nullptr;
    float width = 0, height = 0;
    const bool sized = defaults && defaults(id, &width, &height) &&
                       std::abs(item->placement.width - width) < .001 &&
                       std::abs(item->placement.height - height) < .001;
    context->Assert(sized,
                    "first placement consumes the imported default size");
    if (!sized)
      return SDGO_GAME_TEST_FAIL;
    const auto frame = CaptureScene(L"02-imported-sticker-shield-preview");
    if (frame < 1)
      return frame < 0 ? SDGO_GAME_TEST_FAIL : SDGO_GAME_TEST_WAIT;
    if (GetEnvironmentVariableW(L"SDGO_STICKER_IMPORT_PREVIEW_ONLY", nullptr,
                                0))
      return SDGO_GAME_TEST_PASS;
    proposal = draft;
    if (!click(L"SurfaceSave"))
      return SDGO_GAME_TEST_WAIT;
    phase = 4;
  } else if (phase == 4) {
    if (!snapshot.armed)
      return SDGO_GAME_TEST_WAIT;
    context->Assert(
        snapshot.quotedItems == 1,
        "real World server quotes one imported sticker for a new layer");
    if (snapshot.quotedItems != 1)
      return SDGO_GAME_TEST_FAIL;
    if (!click(L"SurfaceSave"))
      return SDGO_GAME_TEST_WAIT;
    phase = 5;
  } else if (phase == 5) {
    if (snapshot.armed || saved.revision == revision)
      return SDGO_GAME_TEST_WAIT;
    const bool same =
        layers::Same(saved, proposal) && saved.revision == revision + 1;
    context->Assert(same,
                    "server acknowledges the exact imported sticker design");
    if (!same)
      return SDGO_GAME_TEST_FAIL;
    if (!snapshot.triangles || snapshot.draws < 20)
      return SDGO_GAME_TEST_WAIT;
    const auto frame = CaptureScene(L"03-imported-sticker-shield-saved");
    if (frame < 1)
      return frame < 0 ? SDGO_GAME_TEST_FAIL : SDGO_GAME_TEST_WAIT;
    if (!click(L"SurfaceCancel"))
      return SDGO_GAME_TEST_WAIT;
    return SDGO_GAME_TEST_PASS;
  } else
    return SDGO_GAME_TEST_PASS;
  return SDGO_GAME_TEST_WAIT;
}
