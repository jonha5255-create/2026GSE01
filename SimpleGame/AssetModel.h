#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Art
{
    struct Vec3
    {
        float x = 0, y = 0, z = 0;
    };

    struct Vec4
    {
        float x = 0, y = 0, z = 0, w = 0;
    };

    struct Matrix
    {
        std::array<float, 16> v{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        static Matrix TRS(Vec3 translation, Vec4 rotation, Vec3 scale);
        Matrix operator*(const Matrix& other) const;
        Vec3 Point(Vec3 p) const;
    };

    struct Vertex
    {
        Vec3 position, normal;
        std::array<uint32_t, 4> joints{};
        std::array<float, 4> weights{};
    };

    struct Material
    {
        std::string name;
        Vec4 color{1, 1, 1, 1};
        Vec3 emission;
        bool doubleSided = false;
    };

    struct Primitive
    {
        size_t node = 0;
        int skin = -1, material = -1;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct Node
    {
        std::string name;
        int parent = -1;
        bool matrixTransform = false, active = false;
        Matrix matrix;
        Vec3 translation, scale{1, 1, 1};
        Vec4 rotation{0, 0, 0, 1};
    };

    struct Skin
    {
        std::vector<size_t> joints;
        std::vector<Matrix> inverseBind;
    };
    enum class ChannelPath
    {
        Translation,
        Rotation,
        Scale
    };
    enum class Interpolation
    {
        Linear,
        Step,
        Cubic
    };

    struct Channel
    {
        size_t node = 0;
        ChannelPath path;
        Interpolation interpolation;
        std::vector<float> times;
        std::vector<Vec4> values;
    };

    struct Clip
    {
        std::string name;
        float duration = 0;
        std::vector<Channel> channels;
    };

    // Immutable, shared source data. Poses belong to an animation instance, not the model.
    struct Model
    {
        std::vector<Node> nodes;
        std::vector<Material> materials;
        std::vector<Primitive> primitives;
        std::vector<Skin> skins;
        std::vector<Clip> clips;
        const Clip* FindClip(const std::string& name) const;
        static std::shared_ptr<const Model> Load(const std::filesystem::path& path);
        static std::shared_ptr<const Model> Parse(const std::vector<uint8_t>& glb);
    };

    struct Pose
    {
        std::vector<Matrix> world;
        Vec3 rootDisplacement;
        void Sample(const Model& model,
                    const std::string& clip,
                    float seconds,
                    bool loop,
                    bool removeRootMotion = true);
        void SkinVertices(const Model& model,
                          const Primitive& primitive,
                          std::vector<Vec3>& positions) const;
    };

    struct AnimationEvent
    {
        float time = 0;
        std::string name;
        float degrees = 0;
    };

    struct ClipInfo
    {
        std::string name;
        bool loop = false, rootMotion = false;
        float duration = 0, speed = 0;
        std::vector<AnimationEvent> events;
    };

    class AnimationPlayer
    {
      public:
        void Play(const ClipInfo& clip, bool restart = false);
        std::vector<AnimationEvent> Advance(float dt);

        float Time() const
        {
            return time_;
        }

        const ClipInfo& Info() const
        {
            return clip_;
        }

      private:
        ClipInfo clip_;
        float time_ = 0;
    };
} // namespace Art
