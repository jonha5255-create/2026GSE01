#include "stdafx.h"
#include "AssetModel.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <numeric>
#pragma warning(push, 0)
#pragma warning(disable : 4996)
#define CGLTF_IMPLEMENTATION
#include "Dependencies/AssetImport/cgltf.h"
#pragma warning(pop)

namespace Art
{
    namespace
    {
        void Require(bool pass, const char* message)
        {
            if (!pass)
            {
                throw std::runtime_error(message);
            }
        }

        Vec4 Normalize(Vec4 q)
        {
            float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            Require(std::isfinite(length) && length > 1e-8f, "Invalid quaternion");
            return {q.x / length, q.y / length, q.z / length, q.w / length};
        }

        Vec4 Blend(Vec4 a, Vec4 b, float t, bool rotation)
        {
            float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (rotation && dot < 0)
            {
                b = {-b.x, -b.y, -b.z, -b.w};
                dot = -dot;
            }
            float x = 1 - t, y = t;
            if (rotation && dot < .9995f)
            {
                float angle = std::acos(std::clamp(dot, -1.f, 1.f));
                x = std::sin((1 - t) * angle) / std::sin(angle);
                y = std::sin(t * angle) / std::sin(angle);
            }
            Vec4 q{a.x * x + b.x * y, a.y * x + b.y * y, a.z * x + b.z * y, a.w * x + b.w * y};
            return rotation ? Normalize(q) : q;
        }

        std::vector<float> Floats(const cgltf_accessor* accessor, cgltf_type type)
        {
            Require(accessor && accessor->type == type && accessor->count <= 4000000,
                    "Missing, oversized or mistyped accessor");
            std::vector<float> result(accessor->count * cgltf_num_components(type));
            Require(cgltf_accessor_unpack_floats(accessor, result.data(), result.size())
                        == result.size(),
                    "Invalid accessor storage");
            for (float value : result)
            {
                Require(std::isfinite(value), "Non-finite accessor value");
            }
            return result;
        }

        Vec4 SampleChannel(const Channel& c, float time)
        {
            size_t i =
                size_t(std::upper_bound(c.times.begin(), c.times.end(), time) - c.times.begin());
            auto value = [&](size_t k)
            {
                return c.values[k * (c.interpolation == Interpolation::Cubic ? 3 : 1)
                                + (c.interpolation == Interpolation::Cubic ? 1 : 0)];
            };
            if (i == 0)
            {
                return value(0);
            }
            if (i == c.times.size())
            {
                return value(i - 1);
            }
            float span = c.times[i] - c.times[i - 1], t = (time - c.times[i - 1]) / span;
            if (c.interpolation == Interpolation::Step)
            {
                return value(i - 1);
            }
            if (c.interpolation != Interpolation::Cubic)
            {
                return Blend(value(i - 1), value(i), t, c.path == ChannelPath::Rotation);
            }
            Vec4 a = value(i - 1), b = value(i), out = c.values[(i - 1) * 3 + 2],
                 in = c.values[i * 3];
            float aa = 2 * t * t * t - 3 * t * t + 1, bb = -2 * t * t * t + 3 * t * t;
            float oo = (t * t * t - 2 * t * t + t) * span, ii = (t * t * t - t * t) * span;
            Vec4 v{aa * a.x + bb * b.x + oo * out.x + ii * in.x,
                   aa * a.y + bb * b.y + oo * out.y + ii * in.y,
                   aa * a.z + bb * b.z + oo * out.z + ii * in.z,
                   aa * a.w + bb * b.w + oo * out.w + ii * in.w};
            return c.path == ChannelPath::Rotation ? Normalize(v) : v;
        }
    } // namespace

    Matrix Matrix::operator*(const Matrix& b) const
    {
        Matrix r;
        r.v.fill(0);
        for (int c = 0; c < 4; ++c)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int k = 0; k < 4; ++k)
                {
                    r.v[c * 4 + row] += v[k * 4 + row] * b.v[c * 4 + k];
                }
            }
        }
        return r;
    }

    Vec3 Matrix::Point(Vec3 p) const
    {
        return {v[0] * p.x + v[4] * p.y + v[8] * p.z + v[12],
                v[1] * p.x + v[5] * p.y + v[9] * p.z + v[13],
                v[2] * p.x + v[6] * p.y + v[10] * p.z + v[14]};
    }

    Matrix Matrix::TRS(Vec3 t, Vec4 rotation, Vec3 s)
    {
        auto q = Normalize(rotation);
        Matrix r;
        r.v = {(1 - 2 * (q.y * q.y + q.z * q.z)) * s.x,
               2 * (q.x * q.y + q.z * q.w) * s.x,
               2 * (q.x * q.z - q.y * q.w) * s.x,
               0,
               2 * (q.x * q.y - q.z * q.w) * s.y,
               (1 - 2 * (q.x * q.x + q.z * q.z)) * s.y,
               2 * (q.y * q.z + q.x * q.w) * s.y,
               0,
               2 * (q.x * q.z + q.y * q.w) * s.z,
               2 * (q.y * q.z - q.x * q.w) * s.z,
               (1 - 2 * (q.x * q.x + q.y * q.y)) * s.z,
               0,
               t.x,
               t.y,
               t.z,
               1};
        return r;
    }

    const Clip* Model::FindClip(const std::string& name) const
    {
        for (const auto& clip : clips)
        {
            if (clip.name == name)
            {
                return &clip;
            }
        }
        return nullptr;
    }

    std::shared_ptr<const Model> Model::Load(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        Require(bool(stream), "Cannot open GLB");
        auto length = stream.tellg();
        Require(length >= 20 && length <= 128 * 1024 * 1024, "Invalid GLB file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        stream.seekg(0);
        Require(bool(stream.read(reinterpret_cast<char*>(bytes.data()), length)),
                "Truncated GLB file");
        return Parse(bytes);
    }

    std::shared_ptr<const Model> Model::Parse(const std::vector<uint8_t>& bytes)
    {
        Require(bytes.size() >= 20 && bytes.size() <= 128 * 1024 * 1024, "Invalid GLB size");
        cgltf_options options{};
        cgltf_data* raw = nullptr;
        auto result = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
        std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
        Require(result == cgltf_result_success && raw, "GLB parse failed");
        Require(raw->file_type == cgltf_file_type_glb, "Expected embedded binary glTF 2.0");
        for (size_t i = 0; i < raw->buffers_count; ++i)
        {
            Require(!raw->buffers[i].uri, "External buffers unsupported; export GLB");
        }
        Require(cgltf_load_buffers(&options, raw, nullptr) == cgltf_result_success,
                "GLB buffer load failed");
        Require(cgltf_validate(raw) == cgltf_result_success, "glTF validation failed");
        Require(raw->nodes_count <= 4096 && raw->skins_count <= 256
                    && raw->animations_count <= 1024,
                "GLB object count exceeds limits");
        for (size_t i = 0; i < raw->extensions_required_count; ++i)
        {
            Require(false, "Required glTF extensions unsupported by LowPolyKit renderer");
        }
        auto model = std::make_shared<Model>();
        model->nodes.resize(raw->nodes_count);
        for (size_t i = 0; i < raw->nodes_count; ++i)
        {
            const auto& n = raw->nodes[i];
            auto& node = model->nodes[i];
            node.name = n.name ? n.name : "";
            node.parent = n.parent ? int(n.parent - raw->nodes) : -1;
            node.matrixTransform = n.has_matrix != 0;
            std::copy(n.matrix, n.matrix + 16, node.matrix.v.begin());
            node.translation = {n.translation[0], n.translation[1], n.translation[2]};
            node.rotation = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
            node.scale = {n.scale[0], n.scale[1], n.scale[2]};
            for (float f : node.matrix.v)
            {
                Require(std::isfinite(f), "Non-finite node matrix");
            }
            for (float f : {node.translation.x,
                            node.translation.y,
                            node.translation.z,
                            node.scale.x,
                            node.scale.y,
                            node.scale.z})
            {
                Require(std::isfinite(f), "Non-finite node transform");
            }
            Normalize(node.rotation);
        }
        std::function<void(cgltf_node*, size_t)> activate = [&](cgltf_node* n, size_t depth)
        {
            Require(depth <= model->nodes.size(), "Cyclic scene hierarchy");
            model->nodes[size_t(n - raw->nodes)].active = true;
            for (size_t j = 0; j < n->children_count; ++j)
            {
                activate(n->children[j], depth + 1);
            }
        };
        if (raw->scene)
        {
            for (size_t i = 0; i < raw->scene->nodes_count; ++i)
            {
                activate(raw->scene->nodes[i], 0);
            }
        }
        else
        {
            for (size_t i = 0; i < raw->nodes_count; ++i)
            {
                if (!raw->nodes[i].parent)
                {
                    activate(&raw->nodes[i], 0);
                }
            }
        }
        for (size_t i = 0; i < raw->materials_count; ++i)
        {
            const auto& m = raw->materials[i];
            Material out;
            out.name = m.name ? m.name : "";
            Require(!m.has_pbr_specular_glossiness, "Unsupported specular/glossiness material");
            Require(!m.pbr_metallic_roughness.base_color_texture.texture
                        && !m.normal_texture.texture && !m.emissive_texture.texture
                        && !m.occlusion_texture.texture
                        && !m.pbr_metallic_roughness.metallic_roughness_texture.texture,
                    "Textured materials unsupported by palette renderer");
            auto* c = m.pbr_metallic_roughness.base_color_factor;
            out.color = {c[0], c[1], c[2], c[3]};
            out.emission = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
            out.doubleSided = m.double_sided != 0;
            model->materials.push_back(out);
        }
        for (size_t i = 0; i < raw->skins_count; ++i)
        {
            const auto& s = raw->skins[i];
            Skin skin;
            Require(s.joints_count > 0 && s.joints_count <= 512, "Invalid skin joint count");
            skin.inverseBind.resize(s.joints_count);
            if (s.inverse_bind_matrices)
            {
                auto matrices = Floats(s.inverse_bind_matrices, cgltf_type_mat4);
                Require(s.inverse_bind_matrices->count == s.joints_count,
                        "Inverse bind count mismatch");
                for (size_t j = 0; j < s.joints_count; ++j)
                {
                    std::copy_n(matrices.data() + j * 16, 16, skin.inverseBind[j].v.begin());
                }
            }
            for (size_t j = 0; j < s.joints_count; ++j)
            {
                skin.joints.push_back(size_t(s.joints[j] - raw->nodes));
            }
            model->skins.push_back(std::move(skin));
        }
        size_t loadedVertices = 0, loadedIndices = 0;
        for (size_t nodeIndex = 0; nodeIndex < raw->nodes_count; ++nodeIndex)
        {
            const auto& node = raw->nodes[nodeIndex];
            if (!node.mesh || !model->nodes[nodeIndex].active)
            {
                continue;
            }
            for (size_t i = 0; i < node.mesh->primitives_count; ++i)
            {
                const auto& p = node.mesh->primitives[i];
                Require(p.type == cgltf_primitive_type_triangles && !p.has_draco_mesh_compression
                            && p.targets_count == 0,
                        "Only uncompressed triangle meshes without morph targets are supported");
                const cgltf_accessor *position = nullptr, *normal = nullptr, *joints = nullptr,
                                     *weights = nullptr;
                for (size_t a = 0; a < p.attributes_count; ++a)
                {
                    const auto& attr = p.attributes[a];
                    if (attr.type == cgltf_attribute_type_position)
                    {
                        position = attr.data;
                    }
                    if (attr.type == cgltf_attribute_type_normal)
                    {
                        normal = attr.data;
                    }
                    if (attr.type == cgltf_attribute_type_joints && attr.index == 0)
                    {
                        joints = attr.data;
                    }
                    if (attr.type == cgltf_attribute_type_weights && attr.index == 0)
                    {
                        weights = attr.data;
                    }
                    Require((attr.type != cgltf_attribute_type_joints
                             && attr.type != cgltf_attribute_type_weights)
                                || attr.index == 0,
                            "More than four skin influences are unsupported");
                }
                auto positions = Floats(position, cgltf_type_vec3);
                loadedVertices += position->count;
                Require(loadedVertices <= 2000000 && model->primitives.size() < 8192,
                        "Expanded mesh data exceeds runtime limits");
                auto normals = normal ? Floats(normal, cgltf_type_vec3)
                                      : std::vector<float>(positions.size(), 0);
                Require(normals.size() == positions.size(), "Normal count mismatch");
                Primitive primitive;
                primitive.node = nodeIndex;
                primitive.skin = node.skin ? int(node.skin - raw->skins) : -1;
                primitive.material = p.material ? int(p.material - raw->materials) : -1;
                primitive.vertices.resize(position->count);
                auto jointData = joints ? Floats(joints, cgltf_type_vec4) : std::vector<float>();
                auto weightData = weights ? Floats(weights, cgltf_type_vec4) : std::vector<float>();
                Require(primitive.skin < 0
                            || (jointData.size() == position->count * 4
                                && weightData.size() == jointData.size()),
                        "Skinned mesh requires four joint/weight values per vertex");
                for (size_t v = 0; v < position->count; ++v)
                {
                    auto& out = primitive.vertices[v];
                    out.position = {positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]};
                    out.normal = {normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]};
                    if (primitive.skin >= 0)
                    {
                        float total = 0;
                        for (size_t j = 0; j < 4; ++j)
                        {
                            float joint = jointData[v * 4 + j], weight = weightData[v * 4 + j];
                            Require(joint >= 0 && std::floor(joint) == joint
                                        && joint < model->skins[primitive.skin].joints.size()
                                        && weight >= 0,
                                    "Invalid skin influence");
                            out.joints[j] = uint32_t(joint);
                            total += out.weights[j] = weight;
                        }
                        Require(total > 1e-6f, "Vertex has no skin weight");
                        for (float& weight : out.weights)
                        {
                            weight /= total;
                        }
                    }
                }
                if (p.indices)
                {
                    Require(p.indices->count <= 12000000, "Too many triangle indices");
                    primitive.indices.resize(p.indices->count);
                    Require(cgltf_accessor_unpack_indices(
                                p.indices, primitive.indices.data(), 4, primitive.indices.size())
                                == primitive.indices.size(),
                            "Invalid index accessor");
                }
                else
                {
                    primitive.indices.resize(position->count);
                    std::iota(primitive.indices.begin(), primitive.indices.end(), 0u);
                }
                Require(primitive.indices.size() % 3 == 0, "Incomplete triangle");
                loadedIndices += primitive.indices.size();
                Require(loadedIndices <= 12000000, "Expanded triangle data exceeds runtime limits");
                for (auto index : primitive.indices)
                {
                    Require(index < primitive.vertices.size(), "Index outside vertex buffer");
                }
                model->primitives.push_back(std::move(primitive));
            }
        }
        for (size_t i = 0; i < raw->animations_count; ++i)
        {
            const auto& a = raw->animations[i];
            Clip clip;
            clip.name = a.name ? a.name : "";
            for (size_t j = 0; j < a.channels_count; ++j)
            {
                const auto& c = a.channels[j];
                Require(c.target_node && c.target_path != cgltf_animation_path_type_weights,
                        "Unsupported animation target");
                Channel channel;
                channel.node = size_t(c.target_node - raw->nodes);
                Require(!model->nodes[channel.node].matrixTransform, "Animated node must use TRS");
                channel.path =
                    c.target_path == cgltf_animation_path_type_rotation ? ChannelPath::Rotation
                    : c.target_path == cgltf_animation_path_type_scale  ? ChannelPath::Scale
                                                                        : ChannelPath::Translation;
                channel.interpolation =
                    c.sampler->interpolation == cgltf_interpolation_type_step ? Interpolation::Step
                    : c.sampler->interpolation == cgltf_interpolation_type_cubic_spline
                        ? Interpolation::Cubic
                        : Interpolation::Linear;
                channel.times = Floats(c.sampler->input, cgltf_type_scalar);
                Require(!channel.times.empty() && channel.times[0] >= 0,
                        "Invalid animation time range");
                for (size_t k = 1; k < channel.times.size(); ++k)
                {
                    Require(channel.times[k] > channel.times[k - 1],
                            "Animation timestamps must increase strictly");
                }
                int width = channel.path == ChannelPath::Rotation ? 4 : 3;
                auto values =
                    Floats(c.sampler->output, width == 4 ? cgltf_type_vec4 : cgltf_type_vec3);
                Require(values.size() / width
                            == channel.times.size()
                                   * (channel.interpolation == Interpolation::Cubic ? 3 : 1),
                        "Animation key count mismatch");
                for (size_t k = 0; k < values.size(); k += width)
                {
                    channel.values.push_back(
                        {values[k], values[k + 1], values[k + 2], width == 4 ? values[k + 3] : 0});
                }
                clip.duration = std::max(clip.duration, channel.times.back());
                clip.channels.push_back(std::move(channel));
            }
            model->clips.push_back(std::move(clip));
        }
        Require(!model->primitives.empty(), "GLB has no drawable meshes");
        // Validate the complete hierarchy and bind pose before publishing a shared model.
        Pose pose;
        pose.Sample(*model, "", 0, false);
        return model;
    }

    void Pose::Sample(const Model& model,
                      const std::string& name,
                      float seconds,
                      bool loop,
                      bool removeRootMotion)
    {
        Require(std::isfinite(seconds), "Non-finite animation time");
        auto transforms = model.nodes;
        const auto* clip = name.empty() ? nullptr : model.FindClip(name);
        Require(name.empty() || clip, "Unknown animation clip");
        float t = std::max(0.f, seconds);
        if (clip && clip->duration > 0)
        {
            t = loop ? std::fmod(t, clip->duration) : std::min(t, clip->duration);
        }
        if (clip)
        {
            for (const auto& channel : clip->channels)
            {
                auto value = SampleChannel(channel, t);
                auto& node = transforms[channel.node];
                if (channel.path == ChannelPath::Rotation)
                {
                    node.rotation = Normalize(value);
                }
                else if (channel.path == ChannelPath::Translation)
                {
                    node.translation = {value.x, value.y, value.z};
                }
                else
                {
                    node.scale = {value.x, value.y, value.z};
                }
            }
        }
        rootDisplacement = {};
        world.resize(model.nodes.size());
        std::vector<uint8_t> visited(model.nodes.size(), 0);
        std::function<void(size_t)> evaluate = [&](size_t i)
        {
            Require(visited[i] != 1, "Cycle in node hierarchy");
            if (visited[i] == 2)
            {
                return;
            }
            visited[i] = 1;
            auto& node = transforms[i];
            if (node.name == "root")
            {
                auto base = model.nodes[i].translation;
                rootDisplacement = {node.translation.x - base.x,
                                    node.translation.y - base.y,
                                    node.translation.z - base.z};
                if (removeRootMotion)
                {
                    node.translation = base;
                }
            }
            auto local = node.matrixTransform
                             ? node.matrix
                             : Matrix::TRS(node.translation, node.rotation, node.scale);
            if (node.parent >= 0)
            {
                Require(size_t(node.parent) < model.nodes.size(), "Invalid parent node");
                evaluate(size_t(node.parent));
                world[i] = world[node.parent] * local;
            }
            else
            {
                world[i] = local;
            }
            for (float f : world[i].v)
            {
                Require(std::isfinite(f), "Non-finite evaluated pose");
            }
            visited[i] = 2;
        };
        for (size_t i = 0; i < world.size(); ++i)
        {
            evaluate(i);
        }
    }

    void Pose::SkinVertices(const Model& model,
                            const Primitive& primitive,
                            std::vector<Vec3>& out) const
    {
        Require(world.size() == model.nodes.size(), "Pose/model mismatch");
        out.resize(primitive.vertices.size());
        std::vector<Matrix> palette;
        if (primitive.skin >= 0)
        {
            const auto& skin = model.skins[primitive.skin];
            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                palette.push_back(world[skin.joints[j]] * skin.inverseBind[j]);
            }
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            const auto& vertex = primitive.vertices[i];
            if (palette.empty())
            {
                out[i] = world[primitive.node].Point(vertex.position);
            }
            else
            {
                Vec3 p{};
                for (size_t j = 0; j < 4; ++j)
                {
                    auto v = palette[vertex.joints[j]].Point(vertex.position);
                    p.x += v.x * vertex.weights[j];
                    p.y += v.y * vertex.weights[j];
                    p.z += v.z * vertex.weights[j];
                }
                out[i] = p;
            }
        }
    }

    void AnimationPlayer::Play(const ClipInfo& clip, bool restart)
    {
        if (restart || clip_.name != clip.name)
        {
            clip_ = clip;
            time_ = 0;
        }
    }

    std::vector<AnimationEvent> AnimationPlayer::Advance(float dt)
    {
        Require(std::isfinite(dt) && dt >= 0 && dt <= 60, "Invalid animation delta time");
        std::vector<AnimationEvent> fired;
        if (clip_.duration <= 0 || dt == 0)
        {
            return fired;
        }
        float end = time_ + dt;
        size_t loops = clip_.loop ? size_t(std::floor(end / clip_.duration)) : 0;
        Require(loops <= 10000, "Animation event traversal exceeds limit");
        for (size_t loop = 0; loop <= loops; ++loop)
        {
            float start = loop ? 0 : time_;
            float stop = std::min(clip_.duration, end - loop * clip_.duration);
            for (const auto& event : clip_.events)
            {
                if ((event.time > start || (loop > 0 && event.time == 0)) && event.time <= stop)
                {
                    fired.push_back(event);
                }
            }
        }
        time_ = clip_.loop ? std::fmod(end, clip_.duration) : std::min(end, clip_.duration);
        return fired;
    }
} // namespace Art
