#pragma once
#include "AssetLibrary.h"
#include "GameplayActors.h"

namespace Art
{
    // Per-character visual state, driven by simulation clocks rather than draw count.
    class PlayerAnimator
    {
      public:
        const Pose& Evaluate(const Model& model,
                             const AssetLibrary& library,
                             const PlayerActor& player,
                             const WeaponActor& weapon);

        float Yaw() const
        {
            return yaw_;
        }

        float AttackSampleTime() const
        {
            return sampleTime_;
        }

        static bool SelfTest(const Model& model, const AssetLibrary& library);

      private:
        const Model* model_ = nullptr;
        const PlayerActor* owner_ = nullptr;
        float lastClock_ = -1, transitionClock_ = 0, deathClock_ = 0, yaw_ = 0, sampleTime_ = 0;
        int state_ = -1;
        uint64_t serial_ = 0;
        std::vector<float> upperBody_, attackMask_;
        Pose idle_, run_, ready_, attack_, locomotion_, armed_, target_, transition_, output_;
    };
} // namespace Art
