#pragma once
#include "AssetPreview.h"
#include "LivingWorld.h"
#include "GameplayActors.h"
#include <unordered_map>

namespace Art
{
    class WorldArt
    {
      public:
        static std::shared_ptr<WorldArt> Shared();
        WorldArt();
        void Npc(PrototypeRenderer& r, const NpcActor& npc, Point p, float zoom, float time);
        void Player(PrototypeRenderer& r,
                    const PlayerActor& player,
                    const WeaponActor& weapon,
                    Point p,
                    float zoom,
                    float time);
        void Enemy(PrototypeRenderer& r, const EnemyActor& enemy, Point p, float zoom, float time);
        void Static(PrototypeRenderer& r,
                    const std::string& slot,
                    int index,
                    Point p,
                    float width,
                    float depth,
                    float zoom,
                    float opacity = 1,
                    bool vertical = false);
        int BuildingIndex(int region, uint32_t variant) const;

      private:
        struct Source
        {
            struct SkinFrame
            {
                Pose pose;
                std::vector<std::array<float, 16>> bones;
                uint64_t used = 0;
            };

            std::shared_ptr<const Model> model;
            Pose bind;
            std::map<std::string, SkinFrame> frames;
            std::map<std::string, std::vector<PrototypeRenderer::ModelVertex>> geometry;
            std::vector<std::array<float, 16>> bindBones;
            Vec3 low{1e9f, 1e9f, 1e9f}, high{-1e9f, -1e9f, -1e9f};
        };

        Source& File(const std::string& path);
        void Animated(PrototypeRenderer& r,
                      Point origin,
                      float scale,
                      const std::string& file,
                      const std::string& clip,
                      float time,
                      float yaw,
                      const std::map<std::string, int>& parts,
                      float opacity = 1);
        std::vector<std::array<float, 16>> Palette(const Model& model, const Pose& pose) const;
        const std::vector<PrototypeRenderer::ModelVertex>& Geometry(
            Source& source, const std::map<std::string, int>& parts, std::string& key);
        void CreatureDraw(PrototypeRenderer& r,
                          int index,
                          const std::string& action,
                          float seconds,
                          Point p,
                          float zoom,
                          float facingX,
                          float facingY);
        AssetLibrary library_;
        std::map<std::string, Source> sources_;
        std::map<std::string, std::map<int, std::string>> files_;
        std::vector<std::vector<int>> regions_;
        uint64_t access_ = 0;
    };
} // namespace Art
