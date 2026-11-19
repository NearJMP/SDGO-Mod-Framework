#include <windows.h>
#include <objbase.h>
#include <array>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>

#include "sdgo/resource_worker_abi.h"
#include "sdgo/sdgo_api.h"

namespace {
constexpr DWORD connectTimeout = 15000;
constexpr DWORD requestTimeout = 6000;
constexpr uint32_t protocol = 0x31525358;
struct Reply { uint32_t serial; uint32_t status; uint32_t file; uint32_t bytes; };
struct Request { uint32_t serial; char name[240]; };
static_assert(sizeof(Request) == 244 && sizeof(Reply) == 16);

const SdgoApi* framework = nullptr;
SdgoMod* self = nullptr;
std::mutex gate;
HANDLE pipe = INVALID_HANDLE_VALUE;
std::wstring cache;
uint32_t serial = 0;
bool failed = false;

void Disconnect() {
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }
}

bool Finish(OVERLAPPED& operation, BOOL complete, DWORD& count, DWORD timeout, DWORD error) {
    if (complete) return true;
    if (error != ERROR_IO_PENDING) return false;
    if (WaitForSingleObject(operation.hEvent, timeout) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &operation);
        GetOverlappedResult(pipe, &operation, &count, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &operation, &count, FALSE) != FALSE;
}

bool Exchange(void* buffer, DWORD size, bool send, DWORD timeout) {
    OVERLAPPED pending{};
    pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!pending.hEvent) return false;
    DWORD count = 0;
    const BOOL complete = send ? WriteFile(pipe, buffer, size, &count, &pending)
                               : ReadFile(pipe, buffer, size, &count, &pending);
    const DWORD error = complete ? ERROR_SUCCESS : GetLastError();
    const bool okay = Finish(pending, complete, count, timeout, error) && count == size;
    CloseHandle(pending.hEvent);
    return okay;
}

std::wstring Image(HANDLE process) {
    wchar_t path[32768];
    DWORD length = static_cast<DWORD>(std::size(path));
    return QueryFullProcessImageNameW(process, 0, path, &length)
        ? std::wstring(path, length) : std::wstring();
}

bool Start() {
    GUID guid;
    wchar_t formatted[40];
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, formatted, std::size(formatted))) return false;
    std::wstring nonce;
    for (const wchar_t ch : std::wstring(formatted)) {
        if (ch >= L'0' && ch <= L'9') nonce += ch;
        else if (ch >= L'A' && ch <= L'F') nonce += ch + (L'a' - L'A');
    }
    if (nonce.size() != 32) return false;
    auto image = Image(GetCurrentProcess());
    const auto slash = image.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    const std::wstring root = image.substr(0, slash);
    const std::wstring manager = root + L"\\SDGO Mod Manager.exe";
    cache = root + L"\\sdgo\\local-state\\resource-worker\\" + nonce;
    const std::wstring endpoint = L"\\\\.\\pipe\\sdgo-resource-v1-" + nonce;
    pipe = CreateNamedPipeW(endpoint.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
        1, 4096, 4096, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;
    // Only the installed Manager executable is used. The worker runs without a window.
    std::wstring command = L"\"" + manager + L"\" --resource-worker " + endpoint + L" " + nonce;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(manager.c_str(), command.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &child)) return false;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    OVERLAPPED pending{};
    pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!pending.hEvent) return false;
    const BOOL connected = ConnectNamedPipe(pipe, &pending);
    const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
    DWORD ignored = 0;
    const bool accepted = error == ERROR_PIPE_CONNECTED ||
                           Finish(pending, connected, ignored, connectTimeout, error);
    CloseHandle(pending.hEvent);
    if (!accepted) return false;
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid)) return false;
    HANDLE peer = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!peer) return false;
    const auto peerImage = Image(peer);
    CloseHandle(peer);
    if (_wcsicmp(peerImage.c_str(), manager.c_str()) != 0) return false;
    Reply hello{};
    if (!Exchange(&hello, sizeof(hello), false, connectTimeout) ||
        hello.serial != protocol || hello.status != 1 || hello.file != 0 || hello.bytes != 64) return false;
    framework->Log(self, SDGO_LOG_INFO, "runtime resource worker connected: x64 process %lu", pid);
    return true;
}

int __cdecl Prepare() {
    std::lock_guard<std::mutex> lock(gate);
    if (failed) return 0;
    if (pipe != INVALID_HANDLE_VALUE) return 1;
    if (Start()) return 1;
    Disconnect();
    failed = true;
    framework->Log(self, SDGO_LOG_ERROR, "runtime resource worker unavailable; required map bundle cannot mount");
    return 0;
}

void* __cdecl Open(const wchar_t* name,
                   void* (__cdecl *nativeOpen)(const wchar_t*, const wchar_t*),
                   uint32_t* bytes) {
    std::lock_guard<std::mutex> lock(gate);
    if (pipe == INVALID_HANDLE_VALUE) return nullptr;
    Request request{};
    request.serial = ++serial;
    const auto length = std::wcslen(name);
    if (!length || length > std::size(request.name)) return nullptr;
    for (size_t i = 0; i < length; ++i) {
        if (name[i] < L' ' || name[i] > L'~') return nullptr;
        request.name[i] = static_cast<char>(name[i] == L'\\' ? L'/' : name[i]);
    }
    Reply reply{};
    if (!Exchange(&request, sizeof(request), true, requestTimeout) ||
        !Exchange(&reply, sizeof(reply), false, requestTimeout) || reply.serial != request.serial) {
        Disconnect();
        failed = true;
        framework->Log(self, SDGO_LOG_ERROR, "runtime resource worker connection lost");
        return nullptr;
    }
    if ((reply.status != 1 && reply.status != 2) || !reply.file ||
        reply.bytes < 8 || reply.bytes > 64u * 1024u * 1024u) return nullptr;
    wchar_t filename[14];
    swprintf_s(filename, L"%08x.zpk", reply.file);
    void* stream = nativeOpen((cache + L"\\" + filename).c_str(), L"rb");
    if (!stream) return nullptr;
    *bytes = reply.bytes;
    if (reply.status == 1)
        framework->Log(self, SDGO_LOG_INFO, "x64 worker decoded %ls (%u bytes)", name, reply.bytes);
    return stream;
}

const SdgoResourceWorkerV1 worker{sizeof(worker), 1, &Prepare, &Open};
}

extern "C" const SdgoResourceWorkerV1* __cdecl SdgoGetLiveResourceWorkerV1() {
    return framework ? &worker : nullptr;
}

SDGO_MOD_ENTRY {
    framework = api;
    self = mod;
    return 1;
}
