#include "artwork.h"
#include <bcrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <vector>

namespace native_display {
namespace {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
constexpr unsigned kMaxAtlases = 24;
constexpr unsigned kMaxEdge = 4096;
constexpr uint64_t kMaxTextureBytes = 96 * 1024 * 1024;
constexpr uint64_t kMaxPngBytes = 16 * 1024 * 1024;
constexpr GUID kArtworkCache{0x023fe33e, 0x6e92, 0x45dc, {0xaf,0xf3,0x98,0x43,0x28,0x1e,0xa2,0x20}};

struct Artwork {
    std::wstring path;
    unsigned width = 0;
    unsigned height = 0;
    std::array<uint8_t, 32> digest{};
    std::array<uint8_t, 32> fourBitDigest{};
    ComPtr<IDirect3DTexture9> texture;
    unsigned factor = 0;
    bool attempted = false;
};

const SdgoApi* api;
SdgoMod* mod;
std::vector<Artwork> artwork;
ComPtr<IDirect3DDevice9> artworkDevice;
uint64_t residentBytes = 0;

unsigned Nibble(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return 16;
}

unsigned Dimension(const std::wstring& name, size_t start, size_t end) {
    if (end == std::wstring::npos || end <= start || end - start > 4) return 0;
    unsigned value = 0;
    for (size_t i = start; i < end; ++i) {
        if (name[i] < L'0' || name[i] > L'9') return 0;
        value = value * 10 + (name[i] - L'0');
    }
    return value;
}

bool Metadata(const std::wstring& name, Artwork& item) {
    const size_t x = name.find(L'x'), separator = name.find(L'-');
    if (x == std::wstring::npos || separator == std::wstring::npos ||
        name.size() != separator + 134 || name.compare(name.size() - 4, 4, L".png")) return false;
    const size_t prefix = separator + 1;
    item.width = Dimension(name, 0, x);
    item.height = Dimension(name, x + 1, separator);
    if (!item.width || !item.height || item.width > kMaxEdge / 2 || item.height > kMaxEdge / 2)
        return false;
    if (name[prefix + 64] != L'-') return false;
    const auto parse = [&](size_t start, auto& digest) {
        for (size_t i = 0; i < digest.size(); ++i) {
            const unsigned hi = Nibble(name[start + 2 * i]), lo = Nibble(name[start + 2 * i + 1]);
            if (hi > 15 || lo > 15) return false;
            digest[i] = static_cast<uint8_t>(hi * 16 + lo);
        }
        return true;
    };
    return parse(prefix, item.digest) && parse(prefix + 65, item.fourBitDigest);
}

bool DigestPixels(IDirect3DTexture9* texture, const D3DSURFACE_DESC& desc,
                  std::array<uint8_t, 32>& result) {
    std::vector<uint32_t> row(desc.Width);
    const bool format32 = desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    D3DLOCKED_RECT locked{};
    const UINT rowBytes = desc.Width * 4;
    bool ok = false;
    if (SUCCEEDED(texture->LockRect(0, &locked, nullptr, D3DLOCK_READONLY))) {
        const UINT sourceBytes = format32 ? rowBytes : desc.Width * 2;
        if (locked.pBits && locked.Pitch >= static_cast<INT>(sourceBytes)) {
            const auto* data = static_cast<const uint8_t*>(locked.pBits);
            ok = true;
            for (UINT y = 0; y < desc.Height && ok; ++y) {
                const auto* pixels = data + static_cast<size_t>(y) * locked.Pitch;
                if (format32) memcpy(row.data(), pixels, rowBytes);
                else {
                    const auto* rgba4444 = reinterpret_cast<const uint16_t*>(pixels);
                    for (UINT x = 0; x < desc.Width; ++x) {
                        const uint32_t packed = rgba4444[x];
                        row[x] = ((packed & 0xf000) << 12 | (packed & 0xf00) << 8 |
                                  (packed & 0xf0) << 4 | (packed & 0xf)) * 17;
                    }
                }
                for (uint32_t& pixel : row) {
                    if (desc.Format == D3DFMT_X8R8G8B8) pixel |= 0xff000000;
                    else if (!(pixel & 0xff000000)) pixel = 0;
                }
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(row.data()), rowBytes, 0) >= 0;
            }
        }
        texture->UnlockRect(0);
    }
    if (ok) ok = BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool Upload(Artwork& item, IDirect3DDevice9* device, IWICBitmapSource* image) {
    UINT width = 0, height = 0;
    if (FAILED(image->GetSize(&width, &height)) || width > kMaxEdge || height > kMaxEdge) return false;
    const unsigned factor = width / item.width;
    if ((factor != 2 && factor != 3) || width != item.width * factor || height != item.height * factor)
        return false;
    const uint64_t bytes = static_cast<uint64_t>(width) * height * 4;
    if (bytes + residentBytes > kMaxTextureBytes) return false;

    ComPtr<IDirect3DTexture9> texture;
    if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                     texture.GetAddressOf(), nullptr))) return false;
    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) return false;
    const bool copied = locked.pBits && locked.Pitch >= static_cast<INT>(width * 4) &&
        SUCCEEDED(image->CopyPixels(nullptr, static_cast<UINT>(locked.Pitch),
            static_cast<UINT>(locked.Pitch) * height, static_cast<BYTE*>(locked.pBits)));
    const HRESULT unlocked = texture->UnlockRect(0);
    if (!copied || FAILED(unlocked)) return false;
    item.texture = texture;
    item.factor = factor;
    residentBytes += bytes;
    return true;
}

bool Decode(Artwork& item, IDirect3DDevice9* device) {
    std::error_code error;
    const auto size = fs::file_size(item.path, error);
    if (error || size < 33 || size > kMaxPngBytes) return false;
    struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> rgba;
    GUID format{};
    UINT frames = 0;
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.GetAddressOf()))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(item.path.c_str(), &GUID_VendorMicrosoft,
            GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) &&
        SUCCEEDED(decoder->GetContainerFormat(&format)) && format == GUID_ContainerFormatPng &&
        SUCCEEDED(decoder->GetFrameCount(&frames)) && frames == 1 &&
        SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())) &&
        SUCCEEDED(factory->CreateFormatConverter(rgba.GetAddressOf())) &&
        SUCCEEDED(rgba->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
        Upload(item, device, rgba.Get());
}
} // namespace

bool ScanArtwork(const SdgoApi* host, SdgoMod* owner) {
    api = host;
    mod = owner;
    auto directory = fs::path(api->ModDirectory(mod)).parent_path().parent_path() /
        L"local-state" / L"native-display" / L"artwork";
    const auto pathText = directory.wstring();
    if (pathText.compare(0, 4, L"\\\\?\\") != 0) {
        directory = pathText.compare(0, 2, L"\\\\") == 0
            ? L"\\\\?\\UNC\\" + pathText.substr(2) : L"\\\\?\\" + pathText;
    }
    std::error_code error;
    for (const auto& parent : {directory.parent_path().parent_path(), directory.parent_path(), directory}) {
        const DWORD attributes = GetFileAttributesW(parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    for (fs::directory_iterator iter(directory, error), end; !error && iter != end; iter.increment(error)) {
        if (artwork.size() >= kMaxAtlases) break;
        const auto path = iter->path();
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) continue;
        Artwork item;
        if (!Metadata(path.filename().wstring(), item)) continue;
        item.path = path.wstring();
        artwork.push_back(std::move(item));
    }
    if (!artwork.empty()) api->Log(mod, SDGO_LOG_INFO, "%u local sprite atlas inputs available",
                                  static_cast<unsigned>(artwork.size()));
    return !artwork.empty();
}

void ReleaseArtwork() {
    for (auto& item : artwork) {
        item.texture.Reset();
        item.attempted = false;
    }
    artworkDevice.Reset();
    residentBytes = 0;
}

Replacement FindArtwork(IDirect3DTexture9* texture, const D3DSURFACE_DESC& desc) {
    if (artwork.empty() || (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8 &&
                           desc.Format != D3DFMT_A4R4G4B4) ||
        desc.Usage || desc.Pool != D3DPOOL_MANAGED) return {};
    UINT match = 0;
    DWORD size = sizeof(match);
    if (FAILED(texture->GetPrivateData(kArtworkCache, &match, &size))) {
        std::array<uint8_t, 32> digest{};
        bool hashed = false;
        for (unsigned i = 0; i < artwork.size(); ++i) {
            auto& item = artwork[i];
            if (item.width != desc.Width || item.height != desc.Height) continue;
            if (!hashed && !DigestPixels(texture, desc, digest)) return {};
            hashed = true;
            const auto& expected = desc.Format == D3DFMT_A4R4G4B4 ? item.fourBitDigest : item.digest;
            if (expected != digest) continue;
            // Two sources can collapse to the same pixels in the legacy
            // 16-bit format. An ambiguous match keeps the game's texture.
            if (match) {
                match = 0;
                break;
            }
            match = i + 1;
        }
        texture->SetPrivateData(kArtworkCache, &match, sizeof(match), 0);
    }
    if (!match || match > artwork.size()) return {};
    ComPtr<IDirect3DDevice9> device;
    if (FAILED(texture->GetDevice(device.GetAddressOf()))) return {};
    if (device.Get() != artworkDevice.Get()) {
        ReleaseArtwork();
        artworkDevice = device;
    }
    auto& item = artwork[match - 1];
    if (!item.attempted) {
        item.attempted = true;
        const bool loaded = Decode(item, device.Get());
        api->Log(mod, loaded ? SDGO_LOG_INFO : SDGO_LOG_WARN,
            "local atlas %ux%u %s", desc.Width, desc.Height,
            loaded ? "is active" : "could not be loaded; original remains active");
    }
    return {item.texture.Get(), item.factor};
}
} // namespace native_display
