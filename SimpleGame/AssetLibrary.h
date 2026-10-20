#pragma once
#include "AssetModel.h"

namespace Art
{
    struct AssetEntry
    {
        std::string name, slot, file;
        int index = 0;
    };

    struct Appearance
    {
        std::string id, name, file;
        int paletteIndex = 0;
        std::map<std::string, int> parts;
    };

    struct Creature
    {
        std::string id, name, file;
        int index = 0;
        std::vector<std::string> clips;
    };

    // A library is independent of OpenGL and of runtime NPC/monster IDs.
    class AssetLibrary
    {
      public:
        explicit AssetLibrary(std::filesystem::path root);
        static std::filesystem::path Discover();
        void ReloadCatalogs();
        std::shared_ptr<const Model> Load(const std::string& relativeFile);
        std::filesystem::path Resolve(const std::string& relativeFile) const;
        std::vector<size_t> SelectParts(const Model& model,
                                        const std::map<std::string, int>& parts) const;
        const ClipInfo& Animation(const std::string& name) const;

        const std::vector<AssetEntry>& Entries() const
        {
            return entries_;
        }

        const std::vector<Appearance>& Appearances() const
        {
            return appearances_;
        }

        const std::vector<Creature>& Creatures() const
        {
            return creatures_;
        }

        const std::string& ModularFile() const
        {
            return modularFile_;
        }

        size_t FileLoads() const
        {
            return fileLoads_;
        }

        static bool SelfTest(const std::filesystem::path& root);

      private:
        std::filesystem::path root_;
        std::string modularFile_;
        std::vector<AssetEntry> entries_;
        std::vector<Appearance> appearances_;
        std::vector<Creature> creatures_;
        std::map<std::string, ClipInfo> clips_;
        std::map<std::string, std::map<int, std::string>> partNodes_;

        struct Cached
        {
            std::filesystem::file_time_type modified;
            uintmax_t size = 0;
            std::shared_ptr<const Model> model;
        };

        std::map<std::string, Cached> cache_;
        size_t fileLoads_ = 0;
    };
} // namespace Art
