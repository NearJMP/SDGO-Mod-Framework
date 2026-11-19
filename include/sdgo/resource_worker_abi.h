#pragma once
#include <cstdint>

struct SdgoResourceWorkerV1 {
    std::uint32_t size;
    std::uint32_t version;
    int (__cdecl *Prepare)();
    void* (__cdecl *Open)(const wchar_t* name,
                          void* (__cdecl *native_open)(const wchar_t*, const wchar_t*),
                          std::uint32_t* bytes);
};

using SdgoGetResourceWorkerV1 = const SdgoResourceWorkerV1* (__cdecl*)();
