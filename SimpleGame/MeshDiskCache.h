#pragma once
#include <vector>
#include <string>
#include <cstdint>
enum class MeshLoad
{
    Missing,
    Loaded,
    Rejected
};

class MeshDiskCache
{
  public:
    bool readEnabled = true;
    void Initialize();
    MeshLoad Load(const std::string& key, std::vector<float>& vertices);
    bool Save(const std::string& key, const std::vector<float>& vertices);
    std::wstring Path(const std::string& key) const;
    static uint64_t Hash(const void* bytes, size_t count);
    static bool Validate(const std::vector<float>& vertices);
    bool SelfTest();

  private:
    void Prune();
    std::wstring directory_;
    unsigned writes_ = 0;
};
