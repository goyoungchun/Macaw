/**
 * @file FMinimalViewInfo.cpp
 * @brief 카메라 위치·회전·시야각과 전환 블렌딩을 계산합니다.
 * @ingroup Engine
 */

#include "Engine/FMinimalViewInfo.h"
#include "Core/Literals/AngleFunctions.h"
#include "Engine/FSceneView.h"
#include "Core/Math/FMatrix.h"
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    constinit const Core::FAngle Engine::FMinimalViewInfo::mDefaultFieldOfView{60_deg};

    Engine::FMinimalViewInfo Engine::FMinimalViewInfo::Blend(const Engine::FMinimalViewInfo& Other, float Alpha) const {
        Engine::FMinimalViewInfo Result{};
        Result.mLocation = mLocation.Lerp(Other.mLocation, Alpha);
        Result.mRotation = mRotation.Slerp(Other.mRotation, Alpha);
        Result.mFieldOfView = mFieldOfView + (Other.mFieldOfView - mFieldOfView) * Alpha;
        Result.mSensorHeight = Core::Math::Lerp(mSensorHeight, Other.mSensorHeight, Alpha);

        const Engine::FDepthOfFieldSettings& From{mDepthOfFieldSettings};
        const Engine::FDepthOfFieldSettings& To{Other.mDepthOfFieldSettings};
        Engine::FDepthOfFieldSettings& DepthOfField{Result.mDepthOfFieldSettings};
        DepthOfField = Alpha < 0.5f ? From : To;

        if (Alpha > 0.0f && Alpha < 1.0f) {
            if (From.mEnabled && To.mEnabled) {
                DepthOfField.mFocalDistance = Core::Math::Lerp(From.mFocalDistance, To.mFocalDistance, Alpha);
                DepthOfField.mMaxBlurRadius = std::lerp(From.mMaxBlurRadius, To.mMaxBlurRadius, Alpha);
                DepthOfField.mFStop = std::lerp(From.mFStop, To.mFStop, Alpha);
            } else if (From.mEnabled != To.mEnabled) {
                // 한쪽만 DOF를 쓰면 해당 카메라의 초점을 유지한 채 블러 반경을 보간합니다.
                DepthOfField = From.mEnabled ? From : To;
                DepthOfField.mMaxBlurRadius *= From.mEnabled ? 1.0f - Alpha : Alpha;
            }
        }

        return Result;
    }

    bool Engine::FMinimalViewInfo::CalcSceneView(const FVector2& Size, Engine::FSceneView& View) const {
        if (!std::isfinite(Size.x) || !std::isfinite(Size.y) || Size.x <= 0.0f || Size.y <= 0.0f || !mLocation.IsFinite() || !mRotation.IsFinite() || !mFieldOfView.IsFinite() || mFieldOfView <= 0_deg || mFieldOfView >= 180_deg) {
            return false;
        }

        Engine::FSceneView CameraView{};
        CameraView.mLocation = mLocation;
        CameraView.mForward = mRotation.ForwardVector();
        CameraView.mSize = Size;
        CameraView.mFieldOfView = mFieldOfView;
        CameraView.mSensorHeight = mSensorHeight;
        CameraView.mDepthOfFieldSettings = mDepthOfFieldSettings;
        CameraView.mView = FMatrix::LookTo(CameraView.mLocation, CameraView.mForward, mRotation.UpVector());
        CameraView.mProjection = FMatrix::Perspective(mFieldOfView, Size.x / Size.y, 0.05_m, 10000_m);
        CameraView.mViewProjection = CameraView.mView * CameraView.mProjection;
        View = CameraView;

        return true;
    }

}
