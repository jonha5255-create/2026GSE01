#include "stdafx.h"
#include "WorldArt.h"
#include "FrameProfiler.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Art
{
    namespace
    {
        constexpr float Pi = 3.14159265359f;

        Vec3 Add(Vec3 a, Vec3 b)
        {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }

        Vec3 Sub(Vec3 a, Vec3 b)
        {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }

        Vec3 Times(Vec3 a, float s)
        {
            return {a.x * s, a.y * s, a.z * s};
        }

        float Dot(Vec3 a, Vec3 b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        Vec3 Cross(Vec3 a, Vec3 b)
        {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }

        float Length3(Vec3 a)
        {
            return std::sqrt(Dot(a, a));
        }

        Vec3 Unit3(Vec3 a)
        {
            return Times(a, 1 / std::max(.000001f, Length3(a)));
        }

        Matrix Translate(Vec3 p)
        {
            return Matrix::TRS(p, {0, 0, 0, 1}, {1, 1, 1});
        }

        Matrix RotateX(float a)
        {
            return Matrix::TRS({}, {std::sin(a / 2), 0, 0, std::cos(a / 2)}, {1, 1, 1});
        }

        size_t Bone(const Model& model, const char* name)
        {
            for (size_t i = 0; i < model.nodes.size(); ++i)
            {
                if (model.nodes[i].name == name)
                {
                    return i;
                }
            }
            throw std::runtime_error(std::string("Yokai attachment bone missing: ") + name);
        }

        Vec4 Multiply(Vec4 a, Vec4 b)
        {
            return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
        }

        Vec4 Rotation(const Matrix& matrix)
        {
            auto m = matrix.v;
            for (int col = 0; col < 3; ++col)
            {
                float n = std::sqrt(m[col * 4] * m[col * 4] + m[col * 4 + 1] * m[col * 4 + 1]
                                    + m[col * 4 + 2] * m[col * 4 + 2]);
                for (int row = 0; row < 3; ++row)
                {
                    m[col * 4 + row] /= std::max(.000001f, n);
                }
            }
            Vec4 q;
            float trace = m[0] + m[5] + m[10];
            if (trace > 0)
            {
                float s = std::sqrt(trace + 1) * 2;
                q = {(m[6] - m[9]) / s, (m[8] - m[2]) / s, (m[1] - m[4]) / s, s / 4};
            }
            else
            {
                int i = m[0] > m[5] ? (m[0] > m[10] ? 0 : 2) : (m[5] > m[10] ? 1 : 2);
                int j = (i + 1) % 3, k = (i + 2) % 3;
                float s =
                    std::sqrt(std::max(.000001f, 1 + m[i * 4 + i] - m[j * 4 + j] - m[k * 4 + k]))
                    * 2;
                float v[4]{};
                v[i] = s / 4;
                v[j] = (m[j * 4 + i] + m[i * 4 + j]) / s;
                v[k] = (m[k * 4 + i] + m[i * 4 + k]) / s;
                v[3] = (m[j * 4 + k] - m[k * 4 + j]) / s;
                q = {v[0], v[1], v[2], v[3]};
            }
            return q;
        }

        void RotateBone(const Model& model, Pose& pose, size_t joint, Vec3 from, Vec3 to)
        {
            auto parent = model.nodes[joint].parent;
            Matrix inverse = parent >= 0 ? pose.world[parent].InverseAffine() : Matrix{};
            auto origin = inverse.Point({});
            auto a = Unit3(Sub(inverse.Point(from), origin));
            auto b = Unit3(Sub(inverse.Point(to), origin));
            auto axis = Cross(a, b);
            float w = 1 + Dot(a, b);
            if (w < .00001f)
            {
                axis = Unit3(Cross(a, std::abs(a.x) < .8f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}));
                w = 0;
            }
            float n = std::sqrt(Dot(axis, axis) + w * w);
            Vec4 delta{axis.x / n, axis.y / n, axis.z / n, w / n};
            pose.local[joint].rotation = Multiply(delta, pose.local[joint].rotation);
            pose.RebuildWorld(model);
        }

        void SolveArm(const Model& model, Pose& pose, bool left, Vec3 target)
        {
            size_t shoulder = Bone(model, left ? "upper_arm.L" : "upper_arm.R");
            size_t elbow = Bone(model, left ? "forearm.L" : "forearm.R");
            size_t wrist = Bone(model, left ? "hand.L" : "hand.R");
            Matrix handOrientation = pose.world[wrist];
            auto a = pose.world[shoulder].Point({}), b = pose.world[elbow].Point({}),
                 c = pose.world[wrist].Point({});
            float upper = Length3(Sub(b, a)), lower = Length3(Sub(c, b));
            auto direction = Unit3(Sub(target, a));
            float distance = std::clamp(
                Length3(Sub(target, a)), std::abs(upper - lower) + .001f, upper + lower - .001f);
            target = Add(a, Times(direction, distance));
            auto bend = Sub(Sub(b, a), Times(direction, Dot(Sub(b, a), direction)));
            if (Length3(bend) < .0001f)
            {
                bend = Cross(direction, {0, 1, 0});
            }
            float along = (upper * upper + distance * distance - lower * lower) / (2 * distance);
            auto nextElbow = Add(
                a,
                Add(Times(direction, along),
                    Times(Unit3(bend), std::sqrt(std::max(0.f, upper * upper - along * along)))));
            RotateBone(model, pose, shoulder, Sub(b, a), Sub(nextElbow, a));
            b = pose.world[elbow].Point({});
            c = pose.world[wrist].Point({});
            RotateBone(model, pose, elbow, Sub(c, b), Sub(target, b));
            int parent = model.nodes[wrist].parent;
            pose.local[wrist].rotation =
                Rotation(pose.world[parent].InverseAffine() * handOrientation);
            pose.RebuildWorld(model);
        }
    } // namespace

    Matrix WorldArt::HandAttachment(
        const Model& model, const Pose& bind, const Pose& pose, int family, bool left) const
    {
        size_t hand = Bone(model, left ? "hand.L" : "hand.R");
        float scale = family == 0 ? .8f : family == 1 ? .7f : .72f;
        auto grip = yokai_->Family(family).forms[1].sockets.at("grip");
        auto delta = pose.world[hand] * bind.world[hand].InverseAffine();
        return delta * Translate(bind.world[hand].Point({})) * RotateX(family == 0 ? 0 : Pi / 2)
               * Matrix::TRS({}, {0, 0, 0, 1}, {left ? -scale : scale, scale, scale})
               * Translate(Times(grip, -1));
    }

    void WorldArt::NodeDraw(PrototypeRenderer& r,
                            Source& source,
                            const std::string& file,
                            const Pose& pose,
                            const Matrix& attachment,
                            const YokaiFormAsset* form,
                            int evolution,
                            Point p,
                            float scale,
                            float yaw,
                            float opacity,
                            bool sharedDepth,
                            bool additive)
    {
        std::vector<std::array<float, 16>> palette{Matrix{}.v};
        for (size_t i = 0; i < source.model->nodes.size(); ++i)
        {
            bool visible = true;
            if (form)
            {
                for (int node = int(i); node >= 0; node = source.model->nodes[node].parent)
                {
                    auto stage = form->nodeStages.find(source.model->nodes[node].name);
                    if (stage != form->nodeStages.end())
                    {
                        visible = stage->second <= evolution;
                        break;
                    }
                }
            }
            auto matrix = attachment * pose.world[i];
            if (!visible)
            {
                matrix.v.fill(0);
            }
            palette.push_back(matrix.v);
        }
        std::string key = "gpu-yokai-node-v1:" + file;
        const auto& geometry = Geometry(source, {}, key, true, true);
        r.ModelMesh(
            key, geometry, palette, p, scale, yaw, {1, 1, 1}, {}, opacity, sharedDepth, additive, true);
    }

    void WorldArt::AttachWeapon(PrototypeRenderer& r,
                                const PlayerActor& player,
                                Pose& pose,
                                Source& body,
                                Point p,
                                float zoom,
                                float opacity)
    {
        if (!player.weapon || player.health <= 0)
        {
            muzzleOwner_ = NoActor;
            return;
        }
        int family = int(player.yokai);
        const auto& form = yokai_->Family(family).forms[1];
        auto& source = File(form.file);
        auto attachment = HandAttachment(*body.model, body.bind, pose, family);
        NodeDraw(r,
                 source,
                 form.file,
                 source.bind,
                 attachment,
                 &form,
                 player.Evolution(),
                 p,
                 36 * zoom,
                 playerAnimator_.Yaw(),
                 opacity,
                 true);
        if (family == 2)
        {
            auto left = HandAttachment(*body.model, body.bind, pose, family, true);
            NodeDraw(r,
                     source,
                     form.file,
                     source.bind,
                     left,
                     &form,
                     player.Evolution(),
                     p,
                     36 * zoom,
                     playerAnimator_.Yaw(),
                     opacity,
                     true);
        }
        muzzleOwner_ = family == 1 ? player.Id() : NoActor;
        if (family == 1)
        {
            muzzleBody_ = attachment.Point(form.sockets.at("muzzle"));
            muzzleYaw_ = playerAnimator_.Yaw();
        }
    }

    void WorldArt::FitRifleHands(Source& body, Pose& pose)
    {
        auto chest = pose.world[Bone(*body.model, "chest")].Point({});
        SolveArm(*body.model, pose, false, {chest.x + .03f, chest.y - .13f, chest.z + .10f});
        auto weapon = HandAttachment(*body.model, body.bind, pose, 1);
        auto support = weapon.Point(yokai_->Family(1).forms[1].sockets.at("support_hand"));
        SolveArm(*body.model, pose, true, support);
    }

    void WorldArt::Companion(
        PrototypeRenderer& r, const PlayerActor& player, Point p, float zoom, float time)
    {
        const auto& form = yokai_->Family(int(player.yokai)).forms[0];
        auto& source = File(form.file);
        float bob = std::sin(time * 3) * 3;
        r.Ellipse({p.x + 42 * zoom, p.y - 3 * zoom}, 14 * zoom, 4 * zoom, Color(0, 0, 0, .4f));
        Point at{p.x + 42 * zoom, p.y - (20 + bob) * zoom};
        NodeDraw(r,
                 source,
                 form.file,
                 source.bind,
                 Matrix{},
                 &form,
                 player.Evolution(),
                 at,
                 28 * zoom,
                 playerAnimator_.Yaw());
    }

    void WorldArt::Vfx(PrototypeRenderer& r, const WeaponVfxActor& actor, Point p, float zoom)
    {
        const auto& effect = yokai_->Effect(actor.effectIndex);
        if (actor.elapsed < effect.onset || actor.elapsed >= effect.duration)
        {
            return;
        }
        auto& source = File(effect.file);
        if (!actor.anchorResolved)
        {
            if (effect.anchor == "muzzle_socket" && actor.owner == muzzleOwner_)
            {
                float c = std::cos(muzzleYaw_), s = std::sin(muzzleYaw_);
                actor.anchorOffset = {(muzzleBody_.x * c + muzzleBody_.z * s) * 36,
                                      (-muzzleBody_.x * s + muzzleBody_.z * c) * 36};
                actor.anchorHeight = muzzleBody_.y * 36;
            }
            else if (effect.kind != WeaponEffectKind::Impact && effect.family != 1)
            {
                float forward =
                    effect.kind == WeaponEffectKind::Skill && effect.family == 0 ? 0.f : 25.f;
                actor.anchorOffset = {actor.direction.x * forward, actor.direction.y * forward};
                actor.anchorHeight = effect.kind == WeaponEffectKind::Skill ? 18.f : 28.f;
            }
            actor.anchorResolved = true;
        }
        p.x += (actor.anchorOffset.x - actor.anchorOffset.y) * .85f * zoom;
        p.y += ((actor.anchorOffset.x + actor.anchorOffset.y) * .425f - actor.anchorHeight) * zoom;
        Pose pose;
        pose.Sample(*source.model, effect.id, actor.elapsed, false);
        Matrix offset;
        float yaw = std::atan2(actor.direction.x, actor.direction.y);
        if (effect.anchor == "muzzle_socket")
        {
            if (!source.effectAnchorReady)
            {
                Pose peak;
                peak.Sample(*source.model, effect.id, effect.onset + 1 / effect.fps, false);
                for (const auto& primitive : source.model->primitives)
                {
                    if (source.model->nodes[primitive.node].name.find("Muzzle0")
                        != std::string::npos)
                    {
                        std::vector<Vec3> vertices;
                        peak.SkinVertices(*source.model, primitive, vertices);
                        for (auto v : vertices)
                        {
                            source.effectAnchor = Add(source.effectAnchor, v);
                        }
                        source.effectAnchor =
                            Times(source.effectAnchor, 1.f / std::max(size_t(1), vertices.size()));
                        for (const auto& vertex : primitive.vertices)
                        {
                            source.effectAnchorLocal =
                                Add(source.effectAnchorLocal, vertex.position);
                        }
                        source.effectAnchorLocal =
                            Times(source.effectAnchorLocal,
                                  1.f / std::max(size_t(1), primitive.vertices.size()));
                        source.effectAnchorNode = primitive.node;
                        break;
                    }
                }
                source.effectAnchorReady = true;
            }
            offset = Translate(Times(source.effectAnchor, -1));
            // Keep the animated flash centered on the barrel as its authored scale changes.
            // Tracer and spark nodes retain their own translations.
            if (source.effectAnchorNode < pose.world.size())
            {
                auto shift =
                    Sub(source.effectAnchor,
                        pose.world[source.effectAnchorNode].Point(source.effectAnchorLocal));
                for (size_t i = 0; i < source.model->nodes.size(); ++i)
                {
                    const auto& name = source.model->nodes[i].name;
                    if (name.find("Muzzle0") != std::string::npos
                        || name.find("HotCore0") != std::string::npos)
                    {
                        pose.world[i].v[12] += shift.x;
                        pose.world[i].v[13] += shift.y;
                        pose.world[i].v[14] += shift.z;
                    }
                }
            }
            yaw += Pi; // Authored gun tracers travel toward Blender +Y / GLB -Z.
        }
        float size =
            effect.kind == WeaponEffectKind::Skill ? (effect.family == 0 ? 1.9f : 1.25f) : 1.f;
        size *= 1 + actor.evolution * .08f;
        float opacity = std::clamp((effect.duration - actor.elapsed) / .10f, 0.f, 1.f);
        NodeDraw(r,
                 source,
                 effect.file,
                 pose,
                 offset,
                 nullptr,
                 0,
                 p,
                 36 * size * zoom,
                 yaw,
                 opacity,
                 false,
                 true);
        ++FrameProfiler::Get().Counters().vfxDraws;
    }
} // namespace Art
