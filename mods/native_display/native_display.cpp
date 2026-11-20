#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "sdgo/sdgo_api.h"

namespace {

using RendererInit = bool(__thiscall*)(void*, HWND, int, int, bool, DWORD);
using RendererChange = bool(__thiscall*)(void*, bool, int, int, bool, bool);
using WindowPosition = decltype(&SetWindowPos);

const SdgoApi* hostApi;
SdgoMod* ownerMod;
void* configReturn;
void* mouseReturn;
RendererInit originalInit;
RendererChange originalChange;
WindowPosition originalPosition;
uint8_t* application;
void* renderer;
HWND gameWindow;
bool placing;
bool menuCanvas = true;
constexpr int menuWidth = 1024;
constexpr int menuHeight = 768;

struct Size { int width, height; };
Size preset{};
RECT bounds{};
RECT displayBounds{};
int Width(const RECT& rect) { return rect.right - rect.left; }
int Height(const RECT& rect) { return rect.bottom - rect.top; }
int RenderWidth() { return menuCanvas ? menuWidth : Width(displayBounds); }
int RenderHeight() { return menuCanvas ? menuHeight : Height(displayBounds); }

void __stdcall ObserveApplication(uint8_t* app) { application = app; }

__declspec(naked) void ConfigSite() {
    __asm { pushfd }
    __asm { pushad }
    __asm { push edi }
    __asm { call ObserveApplication }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [configReturn] }
}

bool SelectBounds(HWND window) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return false;
    bounds = info.rcMonitor;
    const int monitorWidth = Width(bounds), monitorHeight = Height(bounds);
    if (preset.width && preset.width <= monitorWidth && preset.height <= monitorHeight) {
        bounds.left += (monitorWidth - preset.width) / 2;
        bounds.top += (monitorHeight - preset.height) / 2;
        bounds.right = bounds.left + preset.width;
        bounds.bottom = bounds.top + preset.height;
    }
    displayBounds = bounds;
    return Width(bounds) >= menuWidth && Height(bounds) >= menuHeight &&
        Width(bounds) <= 3840 && Height(bounds) <= 2160;
}

void SelectCanvas(bool menu) {
    menuCanvas = menu;
    bounds = displayBounds;
    if (!menu) return;
    // The menu is laid out as a clipped 4:3 composition. The whole game
    // window presents that original composition at this fitted size.
    const int units = std::min(Width(bounds) / 4, Height(bounds) / 3);
    bounds.left += (Width(bounds) - 4 * units) / 2;
    bounds.top += (Height(bounds) - 3 * units) / 2;
    bounds.right = bounds.left + 4 * units;
    bounds.bottom = bounds.top + 3 * units;
}

LONG CanvasCoordinate(LONG value, int logical, LONG actual) {
    const int64_t product = static_cast<int64_t>(value) * logical;
    return static_cast<LONG>((product < 0 ? product - actual + 1 : product) / actual);
}

void __stdcall MapMenuPoint(HWND window, POINT* point) {
    if (!gameWindow || window != gameWindow || !menuCanvas || !point) return;
    const DWORD error = GetLastError();
    RECT client{};
    if (GetClientRect(window, &client) && client.right >= menuWidth && client.bottom >= menuHeight) {
        point->x = CanvasCoordinate(point->x, menuWidth, client.right);
        point->y = CanvasCoordinate(point->y, menuHeight, client.bottom);
    }
    SetLastError(error);
}

__declspec(naked) void MouseSite() {
    __asm { pushfd }
    __asm { pushad }
    __asm { push edi }
    __asm { push ebx }
    __asm { call MapMenuPoint }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [mouseReturn] }
}

LRESULT CALLBACK MenuPointer(HWND window, UINT message, WPARAM keys, LPARAM position, UINT_PTR id, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, &MenuPointer, id);
        if (window == gameWindow) { gameWindow = nullptr; renderer = nullptr; }
    }
    if (menuCanvas && window == gameWindow && message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
        POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        const bool screen = message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
        if (!screen || ScreenToClient(window, &point)) {
            MapMenuPoint(window, &point);
            if (!screen || ClientToScreen(window, &point)) position = MAKELPARAM(point.x, point.y);
        }
    }
    return DefSubclassProc(window, message, keys, position);
}

template<class T> T& Field(size_t offset) { return *reinterpret_cast<T*>(application + offset); }

void ApplyNativeLayout() {
    Field<int>(0x134) = RenderWidth();
    Field<int>(0x138) = RenderHeight();
    // The native battle HUD and pointer transforms use these coordinate
    // factors independently. In particular, (512,384) is the reticle centre.
    Field<float>(0x98) = static_cast<float>(RenderWidth()) / menuWidth;
    Field<float>(0x9c) = static_cast<float>(RenderHeight()) / menuHeight;
    Field<uint8_t>(0xa4) = 0;   // ScreenMode is windowed directly on the same monitor.
    Field<uint8_t>(0x140) = 0;  // GameMode also remains windowed.
    Field<uint8_t>(0x458) = 1;  // Follow the native borderless scene transition.
}

BOOL FillBounds(HWND window, HWND after, UINT flags) {
    placing = true;
    const LONG old = GetWindowLongW(window, GWL_STYLE);
    const LONG style = (old & ~static_cast<LONG>(WS_OVERLAPPEDWINDOW)) | static_cast<LONG>(WS_POPUP);
    const DWORD error = GetLastError();
    bool styled = true;
    if (style != old) {
        SetLastError(ERROR_SUCCESS);
        styled = SetWindowLongW(window, GWL_STYLE, style) || GetLastError() == ERROR_SUCCESS;
        if (styled) flags |= SWP_FRAMECHANGED;
    }
    SetLastError(error);
    const BOOL positioned = styled && originalPosition(window, after, bounds.left, bounds.top, Width(bounds), Height(bounds),
        flags & ~(SWP_NOMOVE | SWP_NOSIZE));
    if (!positioned && styled && style != old) {
        const DWORD failed = GetLastError();
        SetWindowLongW(window, GWL_STYLE, old);
        SetLastError(failed);
    }
    placing = false;
    return positioned;
}

BOOL WINAPI Position(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags) {
    constexpr UINT noGeometry = SWP_NOMOVE | SWP_NOSIZE;
    if (gameWindow && window == gameWindow && !placing && !IsIconic(window) && (flags & noGeometry) != noGeometry) {
        if (FillBounds(window, after, flags)) return TRUE;
    }
    return originalPosition(window, after, x, y, cx, cy, flags);
}

bool __fastcall Init(void* self, void*, HWND window, int w, int h, bool vsync, DWORD flags) {
    if (!application || Field<HWND>(0x3c) != window || !IsWindow(window) || !SelectBounds(window) ||
        !SetWindowSubclass(window, &MenuPointer, 1, 0)) {
        hostApi->Log(ownerMod, SDGO_LOG_WARN, "native display could not bind the game window and a compatible monitor rectangle");
        return originalInit(self, window, w, h, vsync, flags);
    }
    SelectCanvas(true);
    if (!FillBounds(window, nullptr, SWP_NOACTIVATE | SWP_NOZORDER)) {
        RemoveWindowSubclass(window, &MenuPointer, 1);
        return originalInit(self, window, w, h, vsync, flags);
    }
    gameWindow = window;
    ApplyNativeLayout();
    const bool initialized = originalInit(self, window, RenderWidth(), RenderHeight(), vsync, flags & ~1u);
    if (initialized) {
        renderer = self;
        hostApi->Log(ownerMod, SDGO_LOG_INFO, "native display menu %dx%d in borderless %dx%d; battle preset %dx%d",
            RenderWidth(), RenderHeight(), Width(bounds), Height(bounds), Width(displayBounds), Height(displayBounds));
    }
    return initialized;
}

bool __fastcall Change(void* self, void*, bool windowed, int w, int h, bool vsync, bool force) {
    if (!renderer || self != renderer || !IsWindow(gameWindow) || placing)
        return originalChange(self, windowed, w, h, vsync, force);
    const bool previous = menuCanvas;
    SelectCanvas(w == menuWidth && h == menuHeight);
    RECT current{};
    if (!IsIconic(gameWindow) &&
        (!GetWindowRect(gameWindow, &current) || !EqualRect(&current, &bounds) ||
         (GetWindowLongW(gameWindow, GWL_STYLE) & WS_OVERLAPPEDWINDOW)) &&
        !FillBounds(gameWindow, nullptr, SWP_NOACTIVATE | SWP_NOZORDER)) {
        SelectCanvas(previous);
        return originalChange(self, windowed, w, h, vsync, force);
    }
    ApplyNativeLayout();
    const bool result = originalChange(self, true, RenderWidth(), RenderHeight(), vsync, force);
    hostApi->Log(ownerMod, SDGO_LOG_INFO, "native display %s %dx%d in borderless %dx%d, force=%u result=%u",
        menuCanvas ? "menu" : "battle", RenderWidth(), RenderHeight(), Width(bounds), Height(bounds),
        static_cast<unsigned>(force), static_cast<unsigned>(result));
    return result;
}

} // namespace

SDGO_MOD_ENTRY {
    hostApi = api;
    ownerMod = mod;
    char value[16]{};
    api->SettingString(mod, "Resolution", "desktop", value, sizeof(value));
    if (!strcmp(value, "1080p")) preset = {1920, 1080};
    else if (!strcmp(value, "1440p")) preset = {2560, 1440};
    else if (!strcmp(value, "2160p")) preset = {3840, 2160};
    else if (strcmp(value, "desktop")) return 0;
    if (api->size < offsetof(SdgoApi, HookSite) + sizeof(api->HookSite) || !api->HookSite) return 0;
    return api->HookSite(mod, "native_display.video_config", reinterpret_cast<void*>(&ConfigSite), &configReturn) &&
        api->HookSite(mod, "native_display.mouse_client_point", reinterpret_cast<void*>(&MouseSite), &mouseReturn) &&
        api->HookCode(mod, "native_display.renderer_init", reinterpret_cast<void*>(&Init), reinterpret_cast<void**>(&originalInit)) &&
        api->HookCode(mod, "native_display.renderer_change", reinterpret_cast<void*>(&Change), reinterpret_cast<void**>(&originalChange)) &&
        api->HookImport(mod, "user32.dll", "SetWindowPos", reinterpret_cast<void*>(&Position), reinterpret_cast<void**>(&originalPosition));
}
