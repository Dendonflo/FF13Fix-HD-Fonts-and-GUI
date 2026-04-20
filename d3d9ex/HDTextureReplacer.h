#pragma once

#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <d3d9.h>

#include "spdlog/spdlog.h"

// Runtime HD texture replacement for FF XIII via content hashing.
// Covers fonts, GUI elements, and map tiles.
//
// This system identifies textures by hashing their pixel data at runtime
// and looking up the hash in a pre-computed database.
//
// Flow:
//   1. At startup, load hash_database.txt (hash -> texture name)
//   2. Scan hd_textures/ folder for HD DDS replacement files
//      - Non-map namespaces: pixel data loaded into RAM immediately
//      - Map namespaces (*scene[0-9]+): only file paths indexed; loaded on demand
//   3. On first SetTexture for each texture, lock it read-only, hash pixels,
//      look up name in database
//   4. If HD replacement exists for that name, create HD texture and swap
//   5. On map scene change: flush old scene's textures and pixel data from memory
//
// Format-agnostic: works with DXT1, DXT5, etc.
class HDTextureReplacer
{
public:
    void Init(const std::wstring& modDir);
    // Called from SetTexture hook — identifies texture by hash, swaps if HD available.
    // Needs device pointer to create HD textures on first match.
    IDirect3DBaseTexture9* OnSetTexture(IDirect3DDevice9* pDevice,
                                        IDirect3DBaseTexture9* pTexture);


    void ReleaseTextures();

    // Called from CreateTexture hook — evicts stale cache entries for reused pointers.
    void InvalidateTexture(IDirect3DBaseTexture9* pTexture);

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

    // game pointer -> texture key ("namespace/name")
    // Kept for all matched textures so FlushMapScene can identify map-scene pointers
    std::unordered_map<IDirect3DBaseTexture9*, std::string> pointerKey;

    // Active map scene namespace (e.g. "scene00019"); empty until first map tile seen
    std::string currentMapNamespace;

    // Map tile paths indexed at startup: key -> full DDS path on disk (lazy-load source)
    std::unordered_map<std::string, std::wstring> mapTilePaths;

    // Map tile name -> live D3D9 texture (owner for map tiles; textureMap is non-owning for maps)
    std::unordered_map<std::string, IDirect3DTexture9*> nameToHDTex;

    // LRU order for map tiles: front = most recently used, back = oldest
    std::list<std::string> lruOrder;
    std::unordered_map<std::string, std::list<std::string>::iterator> lruIndex;

    // Maximum number of HD map tile textures kept alive simultaneously.
    // 58 is the largest scene tile count observed; 128 gives comfortable headroom
    // for multiple namespace prefixes (map_scene + gui_scene) within the same scene.
    static constexpr size_t MAP_TILE_LRU_CAP = 128;

    // Returns true if ns matches the map-tile namespace pattern (*scene[0-9]+)
    static bool IsMapNamespace(const std::string& ns);

    // Returns true if the key's namespace is a map namespace
    static bool IsMapTile(const std::string& key);

    // Returns the trailing digits of a namespace (the scene number)
    // e.g. "map_scene00023" -> "00023", "gui_scene00023" -> "00023"
    static std::string ExtractSceneNumber(const std::string& ns);

    // Upload HD pixel data and return a new D3D9 texture (caller owns it)
    IDirect3DTexture9* CreateHDTexture(IDirect3DDevice9* pDevice, const std::string& texName);

    // Release all D3D9 textures, pixel data, and tracking entries for the current map scene
    void FlushMapScene();

    // Evict the least-recently-used map tile texture to reclaim VRAM/RAM
    void EvictOldestMapTile();

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


inline uint64_t HDTextureReplacer::FNV1a64(const uint8_t* data, size_t len, uint64_t h)
{
    for (size_t i = 0; i < len; i++)
    {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}


inline bool HDTextureReplacer::IsBlockCompressed(D3DFORMAT format)
{
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 ||
           format == D3DFMT_DXT3 || format == D3DFMT_DXT4 ||
           format == D3DFMT_DXT5;
}

inline UINT HDTextureReplacer::GetBytesPerBlock(D3DFORMAT format)
{
    switch (format) {
        case D3DFMT_DXT1: return 8;
        case D3DFMT_DXT2: case D3DFMT_DXT3: return 16;
        case D3DFMT_DXT4: case D3DFMT_DXT5: return 16;
        default: return 0;
    }
}

inline UINT HDTextureReplacer::GetBytesPerPixel(D3DFORMAT format)
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

inline UINT HDTextureReplacer::ComputeRowPitch(D3DFORMAT format, UINT width)
{
    if (IsBlockCompressed(format))
        return ((width + 3) / 4) * GetBytesPerBlock(format);
    UINT bpp = GetBytesPerPixel(format);
    return bpp ? width * bpp : 0;
}

inline UINT HDTextureReplacer::ComputeRowCount(D3DFORMAT format, UINT height)
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
inline void HDTextureReplacer::ScanHDSubdir(const std::wstring& subDirPath,
                                      const std::string& prefix)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((subDirPath + L"\\*.dds").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    const bool isMap = IsMapNamespace(prefix);
    int count = 0;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring filePath = subDirPath + L"\\" + fd.cFileName;
        std::wstring fnameW   = fd.cFileName;
        std::string  fname(fnameW.begin(), fnameW.end());
        std::string  key = prefix + "/" + StripDDSExtension(fname);

        if (isMap)
        {
            // Map tile: record the path on disk only — pixel data loaded on demand
            mapTilePaths[key] = filePath;
        }
        else
        {
            // Non-map (fonts, UI, etc.): load pixel data into RAM now
            UINT hdW, hdH;
            D3DFORMAT format;
            std::vector<uint8_t> pixels;
            if (!ReadDDS(filePath, hdW, hdH, format, pixels))
                continue;

            HDTextureData hd;
            hd.hdW = hdW;
            hd.hdH = hdH;
            hd.format = format;
            hd.pixelData = std::move(pixels);

            spdlog::info("HDTextures: HD texture '{}' loaded ({}x{}, {} bytes)",
                         key, hdW, hdH, hd.pixelData.size());
            hdData[key] = std::move(hd);
        }
        ++count;

    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (isMap)
        spdlog::info("HDTextures: map scene '{}': {} tile(s) indexed for lazy load", prefix, count);
}

inline void HDTextureReplacer::Init(const std::wstring& modDir)
{
    std::wstring hashDBPath = modDir + L"\\hash_database.txt";
    {
        std::ifstream f(hashDBPath);
        if (!f.is_open())
        {
            spdlog::info("HDTextures: no hash_database.txt found, texture replacement disabled");
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
        spdlog::info("HDTextures: loaded {} entries from hash database", hashDB.size());
    }

    // --- Scan hd_textures/<subdir>/*.dds ---
    // Each subdirectory is a namespace (e.g. gui_resident, gui_other).
    // Key in hdData = "subdir/texturename" matching hash_database.txt entries.
    std::wstring hdRoot = modDir + L"\\hd_textures";
    DWORD attr = GetFileAttributesW(hdRoot.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        spdlog::info("HDTextures: no hd_textures directory found");
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
        spdlog::info("HDTextures:{} HD texture(s) available for replacement", hdData.size());
}

// OnSetTexture — identify texture by hash, swap if HD replacement available
inline IDirect3DBaseTexture9* HDTextureReplacer::OnSetTexture(IDirect3DDevice9* pDevice,
                                                        IDirect3DBaseTexture9* pTexture)
{
    if (!pTexture || hashDB.empty()) return pTexture;

    // Fast path: already mapped to an HD texture
    auto mapIt = textureMap.find(pTexture);
    if (mapIt != textureMap.end())
        return mapIt->second;

    // Already checked with no match — skip
    if (checkedTextures.count(pTexture))
        return pTexture;

    // --- First time seeing this texture: identify by hashing ---
    checkedTextures.insert(pTexture);

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

    D3DLOCKED_RECT locked;
    if (FAILED(tex->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)))
        return pTexture;

    uint64_t h = 14695981039346656037ULL;
    const uint8_t* bits = static_cast<const uint8_t*>(locked.pBits);
    for (UINT row = 0; row < rowCount; row++)
        h = FNV1a64(bits + row * locked.Pitch, rowPitch, h);

    tex->UnlockRect(0);

    auto dbIt = hashDB.find(h);
    if (dbIt == hashDB.end())
        return pTexture;

    const std::string& texName = dbIt->second;

    // Map tile: switch scene if namespace changed, then lazy-load pixel data
    if (IsMapTile(texName))
    {
        const std::string ns = texName.substr(0, texName.find('/'));

        if (ns != currentMapNamespace)
        {
            // Only flush when the scene NUMBER changes — different prefixes for the same
            // scene number (e.g. "map_scene00023" vs "gui_scene00023") coexist in memory.
            if (ExtractSceneNumber(ns) != ExtractSceneNumber(currentMapNamespace))
                FlushMapScene();
            currentMapNamespace = ns;
            spdlog::info("HDTextures: map namespace '{}'", ns);
        }

        // If the HD texture is already resident (e.g. game reused a different pointer
        // for a tile we've already loaded), reuse it — no disk read, no upload.
        auto nameIt = nameToHDTex.find(texName);
        if (nameIt != nameToHDTex.end())
        {
            // Touch LRU
            lruOrder.erase(lruIndex[texName]);
            lruOrder.push_front(texName);
            lruIndex[texName] = lruOrder.begin();

            textureMap[pTexture] = nameIt->second;
            pointerKey[pTexture] = texName;
            return nameIt->second;
        }

        // Not yet resident — lazy load pixel data from disk
        if (hdData.find(texName) == hdData.end())
        {
            auto pathIt = mapTilePaths.find(texName);
            if (pathIt != mapTilePaths.end())
            {
                UINT hdW, hdH;
                D3DFORMAT format;
                std::vector<uint8_t> pixels;
                if (ReadDDS(pathIt->second, hdW, hdH, format, pixels))
                {
                    HDTextureData hd;
                    hd.hdW = hdW; hd.hdH = hdH;
                    hd.format = format;
                    hd.pixelData = std::move(pixels);
                    spdlog::info("HDTextures: lazy-loaded map tile '{}' ({}x{})", texName, hdW, hdH);
                    hdData[texName] = std::move(hd);
                }
            }
        }
    }

    IDirect3DTexture9* hdTex = CreateHDTexture(pDevice, texName);
    if (!hdTex)
        return pTexture;

    textureMap[pTexture] = hdTex;
    pointerKey[pTexture] = texName;

    // Map tiles: register in nameToHDTex (takes ownership) and LRU
    if (IsMapTile(texName))
    {
        nameToHDTex[texName] = hdTex;
        lruOrder.push_front(texName);
        lruIndex[texName] = lruOrder.begin();

        while (lruOrder.size() > MAP_TILE_LRU_CAP)
            EvictOldestMapTile();
    }

    spdlog::info("HDTextures: '{}' matched by hash {:016x}, swapped to HD ({}x{} -> {}x{})",
                 texName, h, desc.Width, desc.Height,
                 hdData.at(texName).hdW, hdData.at(texName).hdH);

    return hdTex;
}


inline void HDTextureReplacer::ReleaseTextures()
{
    // Non-map textures: owned by textureMap — release here
    for (auto& [ptr, tex] : textureMap)
    {
        auto keyIt = pointerKey.find(ptr);
        bool isMap = keyIt != pointerKey.end() && IsMapTile(keyIt->second);
        if (!isMap && tex) tex->Release();
    }
    textureMap.clear();

    // Map textures: owned by nameToHDTex — release here
    for (auto& [name, tex] : nameToHDTex)
        if (tex) tex->Release();
    nameToHDTex.clear();
    lruOrder.clear();
    lruIndex.clear();

    checkedTextures.clear();
    pointerKey.clear();
    currentMapNamespace.clear();
    // Free lazily-loaded map tile pixel data (non-map hdData stays — loaded at Init)
    for (auto it = hdData.begin(); it != hdData.end(); )
        it = IsMapTile(it->first) ? hdData.erase(it) : std::next(it);
}

inline void HDTextureReplacer::InvalidateTexture(IDirect3DBaseTexture9* pTexture)
{
    auto it = textureMap.find(pTexture);
    if (it != textureMap.end())
    {
        // Map tiles: owned by nameToHDTex, do not release from textureMap
        auto keyIt = pointerKey.find(pTexture);
        bool isMap = keyIt != pointerKey.end() && IsMapTile(keyIt->second);
        if (!isMap && it->second) it->second->Release();
        textureMap.erase(it);
    }
    checkedTextures.erase(pTexture);
    pointerKey.erase(pTexture);
}

inline bool HDTextureReplacer::IsMapNamespace(const std::string& ns)
{
    // Strip trailing digits
    size_t i = ns.size();
    while (i > 0 && std::isdigit((unsigned char)ns[i - 1])) --i;
    // Must end with "scene" before the digits (e.g. "scene00019", "map_scene00019")
    if (i == ns.size() || i < 5) return false;
    return ns.substr(i - 5, 5) == "scene";
}

inline std::string HDTextureReplacer::ExtractSceneNumber(const std::string& ns)
{
    size_t i = ns.size();
    while (i > 0 && std::isdigit((unsigned char)ns[i - 1])) --i;
    return ns.substr(i);
}

inline bool HDTextureReplacer::IsMapTile(const std::string& key)
{
    auto slash = key.find('/');
    if (slash == std::string::npos) return false;
    return IsMapNamespace(key.substr(0, slash));
}

inline IDirect3DTexture9* HDTextureReplacer::CreateHDTexture(IDirect3DDevice9* pDevice,
                                                       const std::string& texName)
{
    auto hdIt = hdData.find(texName);
    if (hdIt == hdData.end()) return nullptr;
    const HDTextureData& hd = hdIt->second;

    IDirect3DTexture9* hdTex = nullptr;
    HRESULT hr = pDevice->CreateTexture(hd.hdW, hd.hdH, 1, 0,
                                        hd.format, D3DPOOL_MANAGED,
                                        &hdTex, nullptr);
    if (FAILED(hr))
    {
        spdlog::error("HDTextures: failed to create HD texture for '{}' (hr=0x{:08X})",
                      texName, (unsigned)hr);
        return nullptr;
    }

    D3DLOCKED_RECT hdLocked;
    hr = hdTex->LockRect(0, &hdLocked, nullptr, 0);
    if (FAILED(hr))
    {
        spdlog::error("HDTextures: failed to lock HD texture for '{}' (hr=0x{:08X})",
                      texName, (unsigned)hr);
        hdTex->Release();
        return nullptr;
    }

    UINT hdRowPitch = ComputeRowPitch(hd.format, hd.hdW);
    UINT hdRowCount = ComputeRowCount(hd.format, hd.hdH);
    const uint8_t* src = hd.pixelData.data();
    for (UINT row = 0; row < hdRowCount; row++)
        memcpy(static_cast<uint8_t*>(hdLocked.pBits) + row * hdLocked.Pitch,
               src + row * hdRowPitch, hdRowPitch);

    hdTex->UnlockRect(0);
    return hdTex;
}

inline void HDTextureReplacer::EvictOldestMapTile()
{
    if (lruOrder.empty()) return;

    const std::string name = lruOrder.back();
    lruOrder.pop_back();
    lruIndex.erase(name);

    // Release the HD texture (nameToHDTex is the owner for map tiles)
    auto hdTexIt = nameToHDTex.find(name);
    if (hdTexIt != nameToHDTex.end())
    {
        if (hdTexIt->second) hdTexIt->second->Release();
        nameToHDTex.erase(hdTexIt);
    }

    // Remove all textureMap and tracking entries pointing to this tile.
    // textureMap is non-owning for map tiles — no Release.
    std::vector<IDirect3DBaseTexture9*> toRemove;
    for (auto& [ptr, key] : pointerKey)
        if (key == name)
            toRemove.push_back(ptr);

    for (auto ptr : toRemove)
    {
        textureMap.erase(ptr);
        checkedTextures.erase(ptr);
        pointerKey.erase(ptr);
    }

    // hdData kept — allows fast recreation from RAM if tile is accessed again
    spdlog::info("HDTextures: LRU evicted '{}' ({} pointer(s) removed)", name, toRemove.size());
}

inline void HDTextureReplacer::FlushMapScene()
{
    if (currentMapNamespace.empty()) return;

    // Release all map HD textures via nameToHDTex (owner).
    // Multiple namespace prefixes (map_scene + gui_scene) for the same scene number
    // may be present — nameToHDTex covers all of them.
    size_t released = nameToHDTex.size();
    for (auto& [name, tex] : nameToHDTex)
        if (tex) tex->Release();
    nameToHDTex.clear();
    lruOrder.clear();
    lruIndex.clear();

    // Clean up textureMap and tracking entries for map tiles (non-owning, no Release)
    std::vector<IDirect3DBaseTexture9*> toRemove;
    toRemove.reserve(pointerKey.size());
    for (auto& [ptr, key] : pointerKey)
        if (IsMapTile(key))
            toRemove.push_back(ptr);

    for (auto ptr : toRemove)
    {
        textureMap.erase(ptr);
        checkedTextures.erase(ptr);
        pointerKey.erase(ptr);
    }

    // Free all lazily-loaded map tile pixel data (RAM reclaim)
    for (auto it = hdData.begin(); it != hdData.end(); )
        it = IsMapTile(it->first) ? hdData.erase(it) : std::next(it);

    spdlog::info("HDTextures: flushed map scene '{}' ({} HD texture(s), {} pointer(s) released)",
                 currentMapNamespace, released, toRemove.size());
}

// ReadDDS — parse DDS header for dimensions and format, read pixel data
inline bool HDTextureReplacer::ReadDDS(const std::wstring& path, UINT& width, UINT& height,
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
            spdlog::warn("HDTextures: unsupported RGB bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x20000)) // DDPF_LUMINANCE
    {
        if (rgbBitCount == 8)
            format = D3DFMT_L8;
        else
        {
            spdlog::warn("HDTextures: unsupported luminance bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else if (fourCC == 0 && (pfFlags & 0x2)) // DDPF_ALPHA
    {
        if (rgbBitCount == 8)
            format = D3DFMT_A8;
        else
        {
            spdlog::warn("HDTextures: unsupported alpha bit count {} in DDS", rgbBitCount);
            return false;
        }
    }
    else
    {
        spdlog::warn("HDTextures: unsupported DDS format (fourCC=0x{:08X}, flags=0x{:08X})",
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
