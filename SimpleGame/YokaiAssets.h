#pragma once
#include "AssetLibrary.h"
#include <array>

namespace Art
{
    enum class WeaponEffectKind
    {
        Basic,
        Skill,
        Impact
    };

    struct YokaiFormAsset
    {
        std::string file;
        std::map<std::string, int> nodeStages;
        std::map<std::string, Vec3> sockets;
    };

    struct YokaiFamilyAsset
    {
        int index = -1;
        std::string name;
        std::array<YokaiFormAsset, 2> forms;
    };

    struct WeaponEffectAsset
    {
        int index = -1, family = -1;
        WeaponEffectKind kind = WeaponEffectKind::Basic;
        std::string id, file, anchor;
        float duration = 0, onset = 0, fps = 30;
    };

    // Data only: usable by headless simulation without creating GL resources.
    class YokaiAssets
    {
      public:
        explicit YokaiAssets(const std::filesystem::path& root);
        static const YokaiAssets& Shared();
        const YokaiFamilyAsset& Family(int index) const;
        const WeaponEffectAsset& Effect(int family, WeaponEffectKind kind) const;
        const WeaponEffectAsset& Effect(int index) const;

        const std::vector<WeaponEffectAsset>& Effects() const
        {
            return effects_;
        }

        static Vec3 BlenderToGlb(Vec3 p)
        {
            return {p.x, p.z, -p.y};
        }

        static bool SelfTest(const std::filesystem::path& root);

      private:
        std::vector<YokaiFamilyAsset> families_;
        std::vector<WeaponEffectAsset> effects_;
    };
} // namespace Art
