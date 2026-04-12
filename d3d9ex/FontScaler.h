#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <d3d9.h>

#include "spdlog/spdlog.h"

// Runtime HD texture replacement for FF XIII via content hashing.
//
// this system identifies textures by hashing their pixel data at runtime
// and looking up the hash in a pre-computed database.
//
// Flow:
//   1. At startup, load hash_database.txt (hash -> texture name)
//   2. Scan hd_textures/ folder for HD DDS replacement files
//   3. On first SetTexture for each texture, lock it read-only, hash pixels,
//      look up name in database
//   4. If HD replacement exists for that name, create HD texture and swap
//
// Format-agnostic: works with DXT1, DXT5, etc.
class FontScaler
{
public:
    void Init(const std::wstring& modDir);
    // Called from SetTexture hook — identifies texture by hash, swaps if HD available.
    // Needs device pointer to create HD textures on first match.
    IDirect3DBaseTexture9* OnSetTexture(IDirect3DDevice9* pDevice,
                                        IDirect3DBaseTexture9* pTexture);


    void ReleaseTextures();

private:
    struct HDTextureData {
        UINT hdW, hdH;
        D3DFORMAT format;
        std::vector<uint8_t> pixelData;
    };

    // Pre-computed hash -> texture name
    std::unordered_map<uint64_t, std::string> hashDB;

    // name -> HD replacement data 
    std::unordered_map<std::string, HDTextureData> hdData;

    // original texture -> HD texture
    std::unordered_map<IDirect3DBaseTexture9*, IDirect3DTexture9*> textureMap;

    // Textures already checked (no match or already mapped)
    std::unordered_set<IDirect3DBaseTexture9*> checkedTextures;

    void ScanHDSubdir(const std::wstring& subDirPath, const std::string& prefix);

    static bool ReadDDS(const std::wstring& path, UINT& width, UINT& height,
                        D3DFORMAT& format, std::vector<uint8_t>& pixelData);

    
    static uint64_t FNV1a64(const uint8_t* data, size_t len,
                            uint64_t h = 14695981039346656037ULL);

    static bool IsBlockCompressed(D3DFORMAT format);
    static UINT GetBytesPerBlock(D3DFORMAT format);
    static UINT GetBytesPerPixel(D3DFORMAT format);
    static UINT ComputeRowPitch(D3DFORMAT format, UINT width);
    static UINT ComputeRowCount(D3DFORMAT format, UINT height);
};


inline uint64_t FontScaler::FNV1a64(const uint8_t* data, size_t len, uint64_t h)
{
    for (size_t i = 0; i < len; i++)
    {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}


inline bool FontScaler::IsBlockCompressed(D3DFORMAT format)
{
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 ||
           format == D3DFMT_DXT3 || format == D3DFMT_DXT4 ||
           format == D3DFMT_DXT5;
}

inline UINT FontScaler::GetBytesPerBlock(D3DFORMAT format)
{
    switch (format) {
        case D3DFMT_DXT1: return 8;
        case D3DFMT_DXT2: case D3DFMT_DXT3: return 16;
        case D3DFMT_DXT4: case D3DFMT_DXT5: return 16;
        default: return 0;
    }
}

inline UINT FontScaler::GetBytesPerPixel(D3DFORMAT format)
{
    switch (format) {
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
        case D3DFMT_R5G6B5: case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: return 2;
        case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4: return 2;
        case D3DFMT_A8: case D3DFMT_L8: return 1;
        case D3DFMT_A8L8: return 2;
        default: return 0;
    }
}

inline UINT FontScaler::ComputeRowPitch(D3DFORMAT format, UINT width)
{
    if (IsBlockCompressed(format))
        return ((width + 3) / 4) * GetBytesPerBlock(format);
    UINT bpp = GetBytesPerPixel(format);
    return bpp ? width * bpp : 0;
}

inline UINT FontScaler::ComputeRowCount(D3DFORMAT format, UINT height)
{
    if (IsBlockCompressed(format))
        return (height + 3) / 4;
    return height;
}

// Init — load hash database + recursively scan hd_textures/ subdirectories
// Strip DDS extensions: "wfnt16.txbh.dds" or "wfnt16.dds" -> "wfnt16"
static std::string StripDDSExtension(const std::string& fname)
{
    size_t pos = fname.find(".txbh.dds");
    if (pos != std::string::npos) return fname.substr(0, pos);
    pos = fname.rfind(".dds");
    if (pos != std::string::npos) return fname.substr(0, pos);
    return fname;
}

// Scan one subdirectory of hd_textures/ and load all DDS files into hdData.
// key = "subdir/texturename" (e.g. "gui_resident/wfnt16")
inline void FontScaler::ScanHDSubdir(const std::wstring& subDirPath,
                                      const std::string& prefix)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((subDirPath + L"\\*.dds").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring filePath = subDirPath + L"\\" + fd.cFileName;

        UINT hdW, hdH;
        D3DFORMAT format;
        std::vector<uint8_t> pixels;
        if (!ReadDDS(filePath, hdW, hdH, format, pixels))
            continue;

        std::wstring fnameW = fd.cFileName;
        std::string fname(fnameW.begin(), fnameW.end());
        std::string key = prefix + "/" + StripDDSExtension(fname);

        HDTextureData hd;
        hd.hdW = hdW;
        hd.hdH = hdH;
        hd.format = format;
        hd.pixelData = std::move(pixels);

        spdlog::info("FontScaler: HD texture '{}' loaded ({}x{}, {} bytes)",
                     key, hdW, hdH, hd.pixelData.size());
        hdData[key] = std::move(hd);

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

inline void FontScaler::Init(const std::wstring& modDir)
{
    std::wstring hashDBPath = modDir + L"\\hash_database.txt";
    {
        std::ifstream f(hashDBPath);
        if (!f.is_open())
        {
            spdlog::info("FontScaler: no hash_database.txt found, texture replacement disabled");
            return;
        }

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#') continue;

            std::istringstream iss(line);
            std::string hashStr, name;
            iss >> hashStr >> name;
            if (hashStr.empty() || name.empty()) continue;

            uint64_t hash = std::strtoull(hashStr.c_str(), nullptr, 16);
            hashDB[hash] = name;
        }
        spdlog::info("FontScaler: loaded {} entries from hash database", hashDB.size());
    }

    // --- Scan hd_textures/<subdir>/*.dds ---
    // Each subdirectory is a namespace (e.g. gui_resident, gui_other).
    // Key in hdData = "subdir/texturename" matching hash_database.txt entries.
    std::wstring hdRoot = modDir + L"\\hd_textures";
    DWORD attr = GetFileAttributesW(hdRoot.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        spdlog::info("FontScaler: no hd_textures directory found");
        return;
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((hdRoot + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring subDirPath = hdRoot + L"\\" + fd.cFileName;
        std::wstring subNameW = fd.cFileName;
        std::string subName(subNameW.begin(), subNameW.end());

        ScanHDSubdir(subDirPath, subName);

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (!hdData.empty())
        spdlog::info("FontScaler: {} HD texture(s) available for replacement", hdData.size());
}

// OnSetTexture — identify texture by hash, swap if HD replacement available
inline IDirect3DBaseTexture9* FontScaler::OnSetTexture(IDirect3DDevice9* pDevice,
                                                        IDirect3DBaseTexture9* pTexture)
{
    if (!pTexture || hashDB.empty()) return pTexture;

    // Fast path: already mapped to HD
    auto mapIt = textureMap.find(pTexture);
    if (mapIt != textureMap.end())
        return mapIt->second;

    // Already checked, no match
    if (checkedTextures.count(pTexture))
        return pTexture;

    // --- First time seeing this texture: identify by hashing ---
    checkedTextures.insert(pTexture);

    // Only handle 2D textures
    if (pTexture->GetType() != D3DRTYPE_TEXTURE)
        return pTexture;

    IDirect3DTexture9* tex = static_cast<IDirect3DTexture9*>(pTexture);

    D3DSURFACE_DESC desc;
    if (FAILED(tex->GetLevelDesc(0, &desc)))
        return pTexture;

    UINT rowPitch = ComputeRowPitch(desc.Format, desc.Width);
    UINT rowCount = ComputeRowCount(desc.Format, desc.Height);
    if (rowPitch == 0 || rowCount == 0)
        return pTexture;

    // Lock texture read-only to hash its pixel data
    D3DLOCKED_RECT locked;
    if (FAILED(tex->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)))
        return pTexture;

    // Hash row-by-row to handle potential pitch padding.
    // Streaming FNV-1a: hash(row0 ++ row1 ++ ...) matches the pre-computed
    // hash from the contiguous DDS pixel data.
    uint64_t h = 14695981039346656037ULL;
    const uint8_t* bits = static_cast<const uint8_t*>(locked.pBits);
    for (UINT row = 0; row < rowCount; row++)
        h = FNV1a64(bits + row * locked.Pitch, rowPitch, h);

    tex->UnlockRect(0);

    // Look up hash in database
    auto dbIt = hashDB.find(h);
    if (dbIt == hashDB.end())
        return pTexture;

    const std::string& texName = dbIt->second;

    // Check if we have an HD replacement for this name
    auto hdIt = hdData.find(texName);
    if (hdIt == hdData.end())
        return pTexture;

    const HDTextureData& hd = hdIt->second;

    // Create HD texture
    IDirect3DTexture9* hdTex = nullptr;
    HRESULT hr = pDevice->CreateTexture(hd.hdW, hd.hdH, 1, 0,
                                        hd.format, D3DPOOL_MANAGED,
                                        &hdTex, nullptr);
    if (FAILED(hr))
    {
        spdlog::error("FontScaler: failed to create {}x{} HD texture for '{}' (hr=0x{:08X})",
                      hd.hdW, hd.hdH, texName, (unsigned)hr);
        return pTexture;
    }

    // Lock and fill with HD pixel data
    D3DLOCKED_RECT hdLocked;
    hr = hdTex->LockRect(0, &hdLocked, nullptr, 0);
    if (FAILED(hr))
    {
        spdlog::error("FontScaler: failed to lock HD texture for '{}' (hr=0x{:08X})",
                      texName, (unsigned)hr);
        hdTex->Release();
        return pTexture;
    }

    UINT hdRowPitch = ComputeRowPitch(hd.format, hd.hdW);
    UINT hdRowCount = ComputeRowCount(hd.format, hd.hdH);
    const uint8_t* src = hd.pixelData.data();

    for (UINT row = 0; row < hdRowCount; row++)
    {
        memcpy(static_cast<uint8_t*>(hdLocked.pBits) + row * hdLocked.Pitch,
               src + row * hdRowPitch,
               hdRowPitch);
    }

    hdTex->UnlockRect(0);

    textureMap[pTexture] = hdTex;

    spdlog::info("FontScaler: '{}' matched by hash {:016x}, swapped to HD ({}x{} -> {}x{})",
                 texName, h, desc.Width, desc.Height, hd.hdW, hd.hdH);

    return hdTex;
}


inline void FontScaler::ReleaseTextures()
{
    for (auto& pair : textureMap)
    {
        if (pair.second)
            pair.second->Release();
    }
    textureMap.clear();
    checkedTextures.clear();
}

// ReadDDS — parse DDS header for dimensions and format, read pixel data
inline bool FontScaler::ReadDDS(const std::wstring& path, UINT& width, UINT& height,
                                 D3DFORMAT& format, std::vector<uint8_t>& pixelData)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    uint8_t header[128];
    f.read(reinterpret_cast<char*>(header), 128);
    if (f.gcount() != 128) return false;
    if (memcmp(header, "DDS ", 4) != 0) return false;

    height = *reinterpret_cast<uint32_t*>(header + 12);
    width  = *reinterpret_cast<uint32_t*>(header + 16);

    // Pixel format: fourCC at offset 84, flags at 80, bitcount at 88
    uint32_t fourCC      = *reinterpret_cast<uint32_t*>(header + 84);
    uint32_t pfFlags     = *reinterpret_cast<uint32_t*>(header + 80);
    uint32_t rgbBitCount = *reinterpret_cast<uint32_t*>(header + 88);

    if (fourCC == 0x31545844)      // "DXT1"
        format = D3DFMT_DXT1;
    else if (fourCC == 0x33545844) // "DXT3"
        format = D3DFMT_DXT3;
    else if (fourCC == 0x35545844) // "DXT5"
        format = D3DFMT_DXT5;
    else if (fourCC == 0 && (pfFlags & 0x40)) // DDPF_RGB
    {
        if (rgbBitCount == 32)
            format = D3DFMT_A8R8G8B8;
        else if (rgbBitCount == 16)
            format = D3DFMT_A4R4G4B4;
        else
        {
            spdlog::warn("FontScaler: unsupported RGB bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x20000)) // DDPF_LUMINANCE
    {
        if (rgbBitCount == 8)
            format = D3DFMT_L8;
        else
        {
            spdlog::warn("FontScaler: unsupported luminance bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x2)) // DDPF_ALPHA
    {
        if (rgbBitCount == 8)
            format = D3DFMT_A8;
        else
        {
            spdlog::warn("FontScaler: unsupported alpha bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else
    {
        spdlog::warn("FontScaler: unsupported DDS format (fourCC=0x{:08X}, flags=0x{:08X})",
                     fourCC, pfFlags);
        return false;
    }

    // Read pixel data
    f.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(f.tellg());
    size_t dataSize = fileSize - 128;

    pixelData.resize(dataSize);
    f.seekg(128);
    f.read(reinterpret_cast<char*>(pixelData.data()), dataSize);

    return f.gcount() == static_cast<std::streamsize>(dataSize);
}
