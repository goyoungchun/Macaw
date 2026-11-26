/**
 * @file UCameraShakeModifier.cpp
 * @brief 사인파 진동으로 카메라를 흔드는 기본 카메라 모디파이어입니다.
 * @ingroup Engine
 */

#include "Core/Types.h"
#include "Core/Literals/AngleFunctions.h"
#include "Engine/UCameraShakeModifier.h"
#include "Engine/FMinimalViewInfo.h"
#include "Core/Math/FQuat.h"
#include "Core/Math/FRotator.h"
#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    constinit const Core::FTime Engine::UCameraShakeModifier::mNotStopping{-1_s};

    Engine::UCameraShakeModifier::UCameraShakeModifier() = default;

    Engine::UCameraShakeModifier::~UCameraShakeModifier() = default;

    bool Engine::UCameraShakeModifier::ModifyCamera(Core::FTime DeltaTime, Engine::FMinimalViewInfo& ViewInfo) {
        FVector Location{};
        FVector Rotation{};
        Core::FAngle FieldOfView{};

        for (FActiveShake& Shake : mShakes) {
            Shake.mElapsed += DeltaTime;

            const float Weight{EvaluateWeight(Shake) * GetAlpha()};

            if (Weight <= 0.0f) {
                continue;
            }

            float Wave[7]{};

            for (Index_t Axis{}; Axis < 7; ++Axis) {
                Wave[Axis] = Core::Math::Sin(1_turn * Shake.mFrequency * Shake.mRate[Axis] * Shake.mElapsed + Shake.mPhase[Axis] * 1_rad) * Weight;
            }

            Location.x += Shake.mLocationAmplitude.x * Wave[0];
            Location.y += Shake.mLocationAmplitude.y * Wave[1];
            Location.z += Shake.mLocationAmplitude.z * Wave[2];
            Rotation.x += Shake.mRotationAmplitude.x * Wave[3];
            Rotation.y += Shake.mRotationAmplitude.y * Wave[4];
            Rotation.z += Shake.mRotationAmplitude.z * Wave[5];
            FieldOfView += Shake.mFieldOfViewAmplitude * Wave[6];
        }

        std::erase_if(mShakes, [](const FActiveShake& Shake) {
            return EvaluateWeight(Shake) < 0.0f;
        });

        // 위치와 회전 모두 카메라 기준으로 더해 어느 방향을 보든 같은 느낌으로 흔들립니다.
        ViewInfo.mLocation = ViewInfo.mLocation + ViewInfo.mRotation.RotateVector(Location);
        ViewInfo.mRotation = (ViewInfo.mRotation * FRotator{Rotation.x * 1_deg, Rotation.y * 1_deg, Rotation.z * 1_deg}.Quaternion()).Normalized();
        ViewInfo.mFieldOfView = std::clamp(ViewInfo.mFieldOfView + FieldOfView, 1_deg, 170_deg);

        return false;
    }

    Int32 Engine::UCameraShakeModifier::StartShake(Core::FTime Duration, Core::FFrequency Frequency, const FVector& LocationAmplitude, const FVector& RotationAmplitude, Core::FAngle FieldOfViewAmplitude, Core::FTime BlendInTime, Core::FTime BlendOutTime) {
        if (!Duration.IsFinite() || !Frequency.IsFinite() || Frequency <= 0_Hz || !FieldOfViewAmplitude.IsFinite() || !BlendInTime.IsFinite() || !BlendOutTime.IsFinite()) {
            return 0;
        }

        std::uniform_real_distribution<float> Phase{(0_rad).GetBase(), (1_turn).GetBase()};
        std::uniform_real_distribution<float> Rate{0.85f, 1.15f};
        FActiveShake Shake{};

        Shake.mId = mNextId++;
        Shake.mDuration = Duration;
        Shake.mFrequency = Frequency;
        Shake.mLocationAmplitude = LocationAmplitude;
        Shake.mRotationAmplitude = RotationAmplitude;
        Shake.mFieldOfViewAmplitude = FieldOfViewAmplitude;
        Shake.mBlendInTime = std::max(BlendInTime, 0_s);
        Shake.mBlendOutTime = std::max(BlendOutTime, 0_s);

        for (Index_t Axis{}; Axis < 7; ++Axis) {
            Shake.mPhase[Axis] = Phase(mRandom);
            Shake.mRate[Axis] = Rate(mRandom);
        }

        mShakes.push_back(Shake);

        return Shake.mId;
    }

    bool Engine::UCameraShakeModifier::StopShake(Int32 Id, bool Immediate) {
        const auto Found{std::find_if(mShakes.begin(), mShakes.end(), [Id](const FActiveShake& Shake) {
            return Shake.mId == Id;
        })};

        if (Found == mShakes.end()) {
            return false;
        }

        if (Immediate || Found->mBlendOutTime <= 0_s) {
            mShakes.erase(Found);
        } else if (Found->mStopTime < 0_s) {
            Found->mStopTime = Found->mElapsed;
        }

        return true;
    }

    void Engine::UCameraShakeModifier::StopAllShakes(bool Immediate) {
        if (Immediate) {
            mShakes.clear();

            return;
        }

        for (FActiveShake& Shake : mShakes) {
            if (Shake.mStopTime < 0_s) {
                Shake.mStopTime = Shake.mElapsed;
            }
        }

        std::erase_if(mShakes, [](const FActiveShake& Shake) {
            return Shake.mBlendOutTime <= 0_s;
        });
    }

    Int32 Engine::UCameraShakeModifier::GetActiveShakeCount() const {
        return static_cast<Int32>(mShakes.size());
    }

    float Engine::UCameraShakeModifier::EvaluateWeight(const FActiveShake& Shake) {
        float Weight{Shake.mBlendInTime > 0_s ? std::min(static_cast<float>(Shake.mElapsed / Shake.mBlendInTime), 1.0f) : 1.0f};

        if (Shake.mStopTime >= 0_s) {
            const Core::FTime Since{Shake.mElapsed - Shake.mStopTime};

            if (Since >= Shake.mBlendOutTime) {
                return -1.0f;
            }

            Weight = std::min(Weight, 1.0f - Since / Shake.mBlendOutTime);
        } else if (Shake.mDuration > 0_s) {
            const Core::FTime Remaining{Shake.mDuration - Shake.mElapsed};

            if (Remaining <= 0_s) {
                return -1.0f;
            }

            if (Shake.mBlendOutTime > 0_s) {
                Weight = std::min(Weight, static_cast<float>(Remaining / Shake.mBlendOutTime));
            }
        }

        return Weight;
    }

}
