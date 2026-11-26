/**
 * @file UCameraModifier.cpp
 * @brief 카메라 관리자가 확정한 시점을 그리기 직전에 고치는 카메라 모디파이어의 기반 클래스입니다.
 * @ingroup Engine
 */

#include "Engine/UCameraModifier.h"
#include "Engine/APlayerCameraManager.h"
#include "Engine/FMinimalViewInfo.h"
#include <algorithm>
#include <cmath>

namespace Engine {

    Engine::UCameraModifier::UCameraModifier() = default;

    Engine::UCameraModifier::~UCameraModifier() = default;

    bool Engine::UCameraModifier::AddedToCamera(Engine::APlayerCameraManager* CameraManager) {
        mCameraOwner = CameraManager;

        return true;
    }

    bool Engine::UCameraModifier::ModifyCamera(Core::FTime, Engine::FMinimalViewInfo&) {
        return false;
    }

    void Engine::UCameraModifier::UpdateAlpha(Core::FTime DeltaTime) {
        const float Target{mPendingDisable ? 0.0f : 1.0f};
        const Core::FTime BlendTime{mPendingDisable ? mAlphaOutTime : mAlphaInTime};

        if (BlendTime <= Core::FTime{} || !DeltaTime.IsFinite()) {
            mAlpha = Target;
        } else if (mAlpha < Target) {
            mAlpha = std::min(mAlpha + DeltaTime / BlendTime, Target);
        } else if (mAlpha > Target) {
            mAlpha = std::max(mAlpha - DeltaTime / BlendTime, Target);
        }

        if (mPendingDisable && mAlpha <= 0.0f) {
            mPendingDisable = false;
            mDisabled = true;
        }
    }

    Engine::APlayerCameraManager* Engine::UCameraModifier::GetCameraOwner() const {
        return mCameraOwner;
    }

    Int32 Engine::UCameraModifier::GetPriority() const {
        return mPriority;
    }

    void Engine::UCameraModifier::SetPriority(Int32 Priority) {
        mPriority = std::clamp<Int32>(Priority, 0, 255);
    }

    Core::FTime Engine::UCameraModifier::GetAlphaInTime() const {
        return mAlphaInTime;
    }

    void Engine::UCameraModifier::SetAlphaInTime(Core::FTime Time) {
        mAlphaInTime = Time.IsFinite() ? std::max(Time, Core::FTime{}) : Core::FTime{};
    }

    Core::FTime Engine::UCameraModifier::GetAlphaOutTime() const {
        return mAlphaOutTime;
    }

    void Engine::UCameraModifier::SetAlphaOutTime(Core::FTime Time) {
        mAlphaOutTime = Time.IsFinite() ? std::max(Time, Core::FTime{}) : Core::FTime{};
    }

    float Engine::UCameraModifier::GetAlpha() const {
        return mAlpha;
    }

    bool Engine::UCameraModifier::IsDisabled() const {
        return mDisabled;
    }

    void Engine::UCameraModifier::EnableModifier() {
        mDisabled = false;
        mPendingDisable = false;
    }

    void Engine::UCameraModifier::DisableModifier(bool Immediate) {
        if (Immediate) {
            mDisabled = true;
            mPendingDisable = false;
            mAlpha = 0.0f;
        } else if (!mDisabled) {
            mPendingDisable = true;
        }
    }

}
