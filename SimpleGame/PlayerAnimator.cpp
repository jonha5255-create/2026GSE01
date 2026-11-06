#include "stdafx.h"
#include "PlayerAnimator.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Art
{
    namespace
    {
        constexpr float Pi = 3.14159265359f;

        float Smooth(float t)
        {
            t = std::clamp(t, 0.f, 1.f);
            return t * t * (3 - 2 * t);
        }

        float Angle(float value)
        {
            return std::remainder(value, 2 * Pi);
        }

        const char* AttackClip(YokaiForm form)
        {
            return form == YokaiForm::Sword  ? "NPC_Attack_Sword"
                   : form == YokaiForm::Claw ? "NPC_Attack_Claw"
                                             : "NPC_Attack_DualPistol";
        }
    } // namespace

    Pose& PlayerAnimator::Evaluate(const Model& model,
                                   const AssetLibrary& library,
                                   const PlayerActor& player,
                                   const WeaponActor& weapon)
    {
        const float clock = player.animationClock;
        bool reset = model_ != &model || owner_ != &player || clock < lastClock_;
        if (reset)
        {
            model_ = &model;
            owner_ = &player;
            state_ = -1;
            output_ = {};
            upperBody_.assign(model.nodes.size(), 0);
            // Bone indices in exported GLBs are not necessarily parent-first.
            for (size_t i = 0; i < model.nodes.size(); ++i)
            {
                for (int node = int(i); node >= 0; node = model.nodes[node].parent)
                {
                    if (model.nodes[node].name == "spine")
                    {
                        upperBody_[i] = 1;
                        break;
                    }
                }
            }
        }
        float dt = reset ? 0 : std::max(0.f, clock - lastClock_);
        bool skill = player.weapon && player.health > 0 && weapon.skillTime >= 0
                     && weapon.skillForm == player.yokai;
        bool active = skill
                      || (player.weapon && player.health > 0 && weapon.animationTime >= 0
                          && weapon.animationForm == player.yokai);
        int state = int(player.yokai) + (player.weapon ? 10 : 0) + (active ? 100 : 0)
                    + (player.health <= 0 ? 1000 : 0) + (skill ? 200 : 0);
        auto serial = skill ? weapon.skillSerial : weapon.animationSerial;
        float attackElapsed = skill ? weapon.skillTime : weapon.animationTime;
        bool change = state != state_ || (active && serial_ != serial);
        if (change)
        {
            transition_ = output_;
            // An event may have occurred between the previous render and this one.
            transitionClock_ = clock - (active ? std::min(.08f, attackElapsed) : 0);
            if (player.health <= 0 && state_ < 1000)
            {
                deathClock_ = clock;
            }
            state_ = state;
            serial_ = serial;
        }
        FarmPoint direction = skill    ? weapon.skillAim
                              : active ? weapon.animationAim
                                       : player.moveFacing;
        float desired = std::atan2(direction.x, direction.y);
        if (reset)
        {
            yaw_ = desired;
        }
        else if (active || (Length(player.moveInput) > .01f && player.firingHold <= 0))
        {
            yaw_ += Angle(desired - yaw_) * (1 - std::exp(-20.f * dt));
            yaw_ = Angle(yaw_);
        }

        if (player.health <= 0)
        {
            target_.Sample(model, "NPC_Death", clock - deathClock_, false, true, false);
        }
        else
        {
            float moving = std::clamp(player.movementBlend, 0.f, 1.f);
            if (moving <= .00001f)
            {
                locomotion_.Sample(model, "NPC_Idle", clock, true, true, false);
            }
            else if (moving >= .99999f)
            {
                locomotion_.Sample(model, "NPC_Run", player.locomotionTime, true, true, false);
            }
            else
            {
                idle_.Sample(model, "NPC_Idle", clock, true, true, false);
                run_.Sample(model, "NPC_Run", player.locomotionTime, true, true, false);
                locomotion_.Mix(model, idle_, run_, moving, {}, false);
            }
            const char* clip = AttackClip(player.yokai);
            armed_ = locomotion_;
            if (player.weapon)
            {
                ready_.Sample(model, clip, 0, false, true, false);
                armed_.Mix(model, locomotion_, ready_, 1, upperBody_, false);
            }
            target_ = armed_;
            sampleTime_ = 0;
            if (active)
            {
                if (skill && player.yokai == YokaiForm::Sword)
                {
                    clip = "NPC_Attack_SwordHeavy";
                }
                const auto& info = library.Animation(clip);
                float elapsed = attackElapsed;
                float duration = skill ? (player.yokai == YokaiForm::Ranged ? .50f
                                          : player.yokai == YokaiForm::Claw ? .75f
                                                                            : .85f)
                                       : std::max(.01f, weapon.animationDuration);
                float progress = std::clamp(elapsed / duration, 0.f, 1.f);
                if (skill && player.yokai == YokaiForm::Sword)
                {
                    float impact = info.events.empty() ? info.duration * .46f : info.events.front().time;
                    sampleTime_ = elapsed <= .25f ? elapsed / .25f * impact
                                                  : impact
                                                        + std::min(1.f, (elapsed - .25f) / .60f)
                                                              * (info.duration - impact);
                }
                else if (skill && player.yokai == YokaiForm::Claw && info.events.size() >= 2)
                {
                    float first = info.events[0].time, second = info.events[1].time,
                          middle = (first + second) * .5f;
                    if (elapsed <= .25f)
                    {
                        sampleTime_ = elapsed / .25f * first;
                    }
                    else if (elapsed <= .30f)
                    {
                        sampleTime_ = first + (elapsed - .25f) / .05f * (middle - first);
                    }
                    else if (elapsed <= .39f)
                    {
                        sampleTime_ = middle + (elapsed - .30f) / .09f * (second - middle);
                    }
                    else if (elapsed <= .44f)
                    {
                        sampleTime_ = second + (elapsed - .39f) / .05f * (info.duration - second);
                    }
                    else if (elapsed <= .53f)
                    {
                        sampleTime_ = (elapsed - .44f) / .09f * first;
                    }
                    else
                    {
                        sampleTime_ =
                            first + std::min(1.f, (elapsed - .53f) / .07f) * (middle - first);
                    }
                }
                else if (player.yokai == YokaiForm::Ranged)
                {
                    // One gameplay shot uses one authored recoil cycle, not all four shots.
                    float first = info.events.empty() ? .05f : info.events[0].time;
                    float end = info.events.size() > 1 ? info.events[1].time - .05f : info.duration;
                    sampleTime_ = skill && elapsed < .25f
                                      ? 0
                                      : std::min(end, first + (skill ? elapsed - .25f : elapsed));
                }
                else if (player.yokai == YokaiForm::Claw && info.events.size() >= 2)
                {
                    float middle = (info.events[0].time + info.events[1].time) * .5f;
                    sampleTime_ = weapon.animationStroke < 0
                                      ? progress * middle
                                      : middle + progress * (info.duration - middle);
                }
                else
                {
                    sampleTime_ = progress * info.duration;
                }
                attack_.Sample(model, clip, sampleTime_, false, true, false);
                attackMask_ = upperBody_;
                for (auto& value : attackMask_)
                {
                    // Preserve footwork while moving; stationary attacks may use the full body.
                    value = value > 0 ? 1.f : 1 - moving;
                }
                float envelope = Smooth(elapsed / .035f) * Smooth((duration - elapsed) / .075f);
                if (skill && player.yokai == YokaiForm::Ranged)
                {
                    envelope =
                        Smooth((elapsed - .25f) / .02f) * Smooth((duration - elapsed) / .075f);
                }
                target_.Mix(model, armed_, attack_, envelope, attackMask_, false);
            }
        }
        if (!transition_.local.empty() && clock - transitionClock_ < .08f)
        {
            output_.Mix(model, transition_, target_, Smooth((clock - transitionClock_) / .08f));
        }
        else
        {
            output_ = target_;
            output_.RebuildWorld(model);
        }
        lastClock_ = clock;
        return output_;
    }

    bool PlayerAnimator::SelfTest(const Model& model, const AssetLibrary& library)
    {
        bool ok = true;
        auto check = [&](bool pass, const char* label)
        {
            std::cout << (pass ? "PASS " : "FAIL ") << label << '\n';
            ok &= pass;
        };
        auto difference = [](const Pose& a, const Pose& b)
        {
            float maximum = 0;
            for (size_t i = 0; i < a.world.size(); ++i)
            {
                for (size_t j = 0; j < 16; ++j)
                {
                    maximum = std::max(maximum, std::abs(a.world[i].v[j] - b.world[i].v[j]));
                }
            }
            return maximum;
        };
        Model synthetic;
        synthetic.nodes.resize(2);
        synthetic.nodes[0].rotation = {0, .70710678f, 0, .70710678f};
        synthetic.nodes[1].parent = 0;
        synthetic.nodes[1].translation = {1, 0, 0};
        Pose left, right, mixed;
        left.Sample(synthetic, "", 0, false);
        synthetic.nodes[0].rotation = {0, -.70710678f, 0, .70710678f};
        right.Sample(synthetic, "", 0, false);
        mixed = right;
        mixed.Mix(synthetic, left, mixed, .5f);
        auto endpoint = mixed.world[1].Point({});
        check(std::abs(std::sqrt(endpoint.x * endpoint.x + endpoint.z * endpoint.z) - 1) < .0001f,
              "local TRS crossfade preserves bone length instead of shrinking skin matrices");

        PlayerActor hero;
        WeaponActor weapon;
        hero.weapon = true;
        hero.yokai = YokaiForm::Ranged;
        hero.moveInput = {1, 0};
        hero.moveFacing = Unit({1, -1});
        hero.movementBlend = 1;
        hero.animationClock = 1;
        hero.locomotionTime = .31f;
        weapon.aim = {0, 1};
        PlayerAnimator animator;
        auto walking = animator.Evaluate(model, library, hero, weapon);
        check(std::abs(Angle(animator.Yaw() - std::atan2(hero.moveFacing.x, hero.moveFacing.y)))
                  < .0001f,
              "armed locomotion faces movement rather than a stale weapon aim");
        Pose run, ready;
        run.Sample(model, "NPC_Run", hero.locomotionTime, true);
        ready.Sample(model, "NPC_Attack_DualPistol", 0, false);
        bool arms = true, legs = true;
        for (size_t i = 0; i < model.nodes.size(); ++i)
        {
            auto rotationEqual = [&](const Pose& pose)
            {
                auto a = walking.local[i].rotation, b = pose.local[i].rotation;
                return std::abs(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) - 1)
                       < .0001f;
            };
            if (model.nodes[i].name == "hand.R" || model.nodes[i].name == "forearm.L")
            {
                arms &= rotationEqual(ready);
            }
            if (model.nodes[i].name == "foot.L" || model.nodes[i].name == "shin.R")
            {
                legs &= rotationEqual(run);
            }
        }
        check(arms && legs,
              "armed movement keeps a ready upper body and authored running footwork");
        check(difference(walking, animator.Evaluate(model, library, hero, weapon)) < .000001f,
              "rendering a character twice does not advance animation clocks");

        weapon.BeginAttackAnimation(YokaiForm::Ranged, .20f);
        weapon.aim = {-1, 0};
        bool oneCycle = true;
        for (int i = 1; i <= 11; ++i)
        {
            hero.animationClock += 1.f / 60;
            weapon.animationTime = i / 60.f;
            animator.Evaluate(model, library, hero, weapon);
            oneCycle &= animator.AttackSampleTime() >= .05f && animator.AttackSampleTime() < .25f;
        }
        check(oneCycle && std::abs(Angle(animator.Yaw())) < .2f,
              "one shot uses one authored recoil and a captured firing direction");
        auto before = animator.Evaluate(model, library, hero, weapon);
        weapon.BeginAttackAnimation(YokaiForm::Ranged, .20f);
        auto interrupted = animator.Evaluate(model, library, hero, weapon);
        check(difference(before, interrupted) < .00001f,
              "an interrupted attack crossfades from the displayed pose without snapping");
        hero.weapon = false;
        hero.animationClock += .1f;
        animator.Evaluate(model, library, hero, weapon);
        check(animator.AttackSampleTime() == 0,
              "leaving weapon mode cancels the visual attack layer");

        auto facingAfterSecond = [&](int fps)
        {
            PlayerActor actor;
            WeaponActor item;
            PlayerAnimator controller;
            actor.moveInput = {1, 0};
            actor.moveFacing = {0, 1};
            controller.Evaluate(model, library, actor, item);
            actor.moveFacing = {std::sin(2.f), std::cos(2.f)};
            for (int i = 1; i <= fps; ++i)
            {
                actor.animationClock = float(i) / fps;
                controller.Evaluate(model, library, actor, item);
            }
            return controller.Yaw();
        };
        check(std::abs(Angle(facingAfterSecond(30) - facingAfterSecond(120))) < .0001f,
              "character turning is independent of 30 or 120 render frames per second");
        return ok;
    }
} // namespace Art
