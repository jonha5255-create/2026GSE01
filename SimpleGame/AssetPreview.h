#pragma once
#include "AssetLibrary.h"
#include "PrototypeRenderer.h"

namespace Art
{
    class ModelRenderer
    {
      public:
        void Draw(PrototypeRenderer& renderer,
                  const Model& model,
                  const Pose& pose,
                  const std::vector<size_t>& selected,
                  Point origin,
                  float pixelsPerMeter,
                  float yaw = 0);

      private:
        struct Face
        {
            Point a, b, c;
            Ink color;
            float depth;
        };

        std::vector<Vec3> positions_;
        std::vector<Face> faces_;
    };

    class AssetPreview
    {
      public:
        explicit AssetPreview(const std::filesystem::path& root) : library_(root)
        {
        }

        void Draw(PrototypeRenderer& renderer, float seconds);

        void SetAction(int action)
        {
            action_ = action >= 0 && action < 9 ? action : 1;
        }

        void Reload()
        {
            library_.ReloadCatalogs();
        }

      private:
        AssetLibrary library_;
        ModelRenderer painter_;
        Pose pose_;
        int action_ = 1;
    };
} // namespace Art
