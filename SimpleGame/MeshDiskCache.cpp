#include "stdafx.h"
#include "MeshDiskCache.h"
#include "RenderDiagnostics.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace
{
    // Serialized explicitly rather than relying on C++ struct packing.
    constexpr uint32_t Magic = 0x314d5347, Version = 1, MaxFloats = 262144 * 12;

    template <class T> bool Read(std::ifstream& input, T& value)
    {
        return bool(input.read(reinterpret_cast<char*>(&value), sizeof(value)));
    }

    template <class T> void Write(std::ofstream& out, const T& value)
    {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
} // namespace

uint64_t MeshDiskCache::Hash(const void* bytes, size_t count)
{
    uint64_t hash = 14695981039346656037ULL;
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (size_t i = 0; i < count; ++i)
    {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void MeshDiskCache::Initialize()
{
    directory_ = RenderDiagnostics::ExecutableDirectory() + L"MeshCache";
    if (!CreateDirectoryW(directory_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        RenderDiagnostics::Get().Event("cache_directory_error",
                                       "disk cache unavailable; memory fallback");
    }
    Prune();
}

std::wstring MeshDiskCache::Path(const std::string& key) const
{
    std::wostringstream path;
    path << directory_ << L"\\gse-" << std::hex << std::setw(16) << std::setfill(L'0')
         << Hash(key.data(), key.size()) << L".gmesh";
    return path.str();
}

bool MeshDiskCache::Validate(const std::vector<float>& vertices)
{
    if (vertices.empty() || vertices.size() > MaxFloats || vertices.size() % 36)
    {
        return false;
    }
    for (float value : vertices)
    {
        if (!std::isfinite(value) || std::abs(value) > 1e7f)
        {
            return false;
        }
    }
    return true;
}

MeshLoad MeshDiskCache::Load(const std::string& key, std::vector<float>& vertices)
{
    vertices.clear();
    if (!readEnabled)
    {
        return MeshLoad::Missing;
    }
    std::ifstream in(Path(key), std::ios::binary);
    if (!in)
    {
        return MeshLoad::Missing;
    }
    uint32_t magic = 0, version = 0, keySize = 0, count = 0;
    uint64_t checksum = 0;
    if (!Read(in, magic) || !Read(in, version) || !Read(in, keySize) || !Read(in, count)
        || !Read(in, checksum) || magic != Magic || version != Version || keySize != key.size()
        || keySize > 2048 || !count || count > MaxFloats || count % 36)
    {
        return MeshLoad::Rejected;
    }
    std::string stored(keySize, ' ');
    if (!in.read(&stored[0], keySize) || stored != key)
    {
        return MeshLoad::Rejected;
    }
    std::vector<float> data(count);
    if (!in.read(reinterpret_cast<char*>(data.data()), count * sizeof(float))
        || in.peek() != std::char_traits<char>::eof() || !Validate(data)
        || Hash(data.data(), data.size() * sizeof(float)) != checksum)
    {
        return MeshLoad::Rejected;
    }
    vertices.swap(data);
    return MeshLoad::Loaded;
}

bool MeshDiskCache::Save(const std::string& key, const std::vector<float>& vertices)
{
    if (!Validate(vertices) || key.size() > 2048)
    {
        return false;
    }
    auto destination = Path(key);
    auto temporary = destination + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }
    Write(out, Magic);
    Write(out, Version);
    Write(out, uint32_t(key.size()));
    Write(out, uint32_t(vertices.size()));
    Write(out, Hash(vertices.data(), vertices.size() * sizeof(float)));
    out.write(key.data(), key.size());
    out.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(float));
    out.flush();
    bool good = bool(out);
    out.close();
    good = good
           && MoveFileExW(temporary.c_str(),
                          destination.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!good)
    {
        DeleteFileW(temporary.c_str());
    }
    if (good && ++writes_ % 64 == 0)
    {
        Prune();
    }
    return good;
}

bool MeshDiskCache::SelfTest()
{
    bool savedReads = readEnabled;
    readEnabled = true;
    bool ok = true;
    auto check = [&](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        ok = ok && pass;
    };
    std::string key = "disk-self-test-" + std::to_string(GetCurrentProcessId());
    std::vector<float> geometry(36, 0), loaded;
    geometry[0] = 1;
    check(Save(key, geometry) && Load(key, loaded) == MeshLoad::Loaded && loaded == geometry,
          "mesh file round trip preserves vertex data");
    {
        std::ofstream corrupt(Path(key), std::ios::binary | std::ios::trunc);
        corrupt << "not a mesh";
    }
    check(Load(key, loaded) == MeshLoad::Rejected, "truncated or corrupt mesh file rejected");
    Save(key, geometry);
    {
        std::fstream corrupt(Path(key), std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(-1, std::ios::end);
        char value = 1;
        corrupt.write(&value, 1);
    }
    check(Load(key, loaded) == MeshLoad::Rejected, "mesh checksum mismatch rejected");
    Save(key, geometry);
    {
        std::fstream corrupt(Path(key), std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(4);
        uint32_t version = 999;
        corrupt.write(reinterpret_cast<const char*>(&version), sizeof(version));
    }
    check(Load(key, loaded) == MeshLoad::Rejected, "obsolete cache schema rejected");
    Save(key, geometry);
    {
        std::fstream corrupt(Path(key), std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(12);
        uint32_t count = UINT32_MAX;
        corrupt.write(reinterpret_cast<const char*>(&count), sizeof(count));
    }
    check(Load(key, loaded) == MeshLoad::Rejected, "oversized payload rejected before allocation");
    check(Save(key, geometry) && Load(key, loaded) == MeshLoad::Loaded,
          "invalid cache can be replaced atomically");
    readEnabled = savedReads;
    return ok;
}

void MeshDiskCache::Prune()
{
    struct File
    {
        std::wstring path;
        uint64_t bytes, time;
    };

    std::vector<File> files;
    uint64_t total = 0;
    WIN32_FIND_DATAW item{};
    HANDLE search = FindFirstFileW((directory_ + L"\\gse-*.gmesh").c_str(), &item);
    if (search == INVALID_HANDLE_VALUE)
    {
        return;
    }
    do
    {
        if (item.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        {
            continue;
        }
        uint64_t bytes = (uint64_t(item.nFileSizeHigh) << 32) | item.nFileSizeLow;
        uint64_t time = (uint64_t(item.ftLastWriteTime.dwHighDateTime) << 32)
                        | item.ftLastWriteTime.dwLowDateTime;
        files.push_back({directory_ + L"\\" + item.cFileName, bytes, time});
        total += bytes;
    } while (FindNextFileW(search, &item));
    FindClose(search);
    std::sort(files.begin(),
              files.end(),
              [](const File& a, const File& b)
              {
                  return a.time < b.time;
              });
    size_t remaining = files.size();
    for (const auto& file : files)
    {
        if (total <= 64 * 1024 * 1024 && remaining <= 1024)
        {
            break;
        }
        if (DeleteFileW(file.path.c_str()))
        {
            total -= file.bytes;
            --remaining;
        }
    }
}
