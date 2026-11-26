/**
 * @file UCameraComponent.cpp
 * @brief 시야각 설정과 월드 변환으로부터 카메라 뷰 생성에 대한 구현입니다.
 * @ingroup Engine
 */

#include "Engine/Component/UCameraComponent.h"
#include "Core/Literals/AngleFunctions.h"
#include "Engine/FMinimalViewInfo.h"
#include "Engine/FSceneView.h"
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    Engine::UCameraComponent::UCameraComponent()
		: mSensorHeight{Core::FLength::FromBase(Engine::Lens::DefaultSensorHeightMillimeters * 0.001f)},
		  mFieldOfView{60_deg} {
        mFocalLength = Engine::Lens::FocalLengthFromFieldOfView(mFieldOfView, mSensorHeight);
    }

    bool Engine::UCameraComponent::GetUseFieldOfView() const {
        return mUseFieldOfView;
    }

    bool Engine::UCameraComponent::SetUseFieldOfView(bool UseFieldOfView) {
        if (UseFieldOfView == mUseFieldOfView) {
            return true;
        }

        // 전환해도 화면 구도가 바뀌지 않도록 현재 모드의 실제 값을 새 모드의 입력값으로 옮깁니다.
        if (UseFieldOfView) {
            mFieldOfView = GetFieldOfView();
        } else {
            mFocalLength = GetFocalLength();
        }

        mUseFieldOfView = UseFieldOfView;

        return true;
    }

    /**
     * @brief 카메라의 실제 수직 시야각을 반환합니다.
     */
    Core::FAngle Engine::UCameraComponent::GetFieldOfView() const {
        return mUseFieldOfView ? mFieldOfView : Engine::Lens::FieldOfViewFromFocalLength(mFocalLength, mSensorHeight);
    }

    /**
     * @brief 유효한 범위의 시야각을 검사하여 카메라에 설정합니다.
     */
    bool Engine::UCameraComponent::SetFieldOfView(Core::FAngle FieldOfView) {
        if (!FieldOfView.IsFinite() || FieldOfView <= 0_deg || FieldOfView >= 180_deg) {
            return false;
        }

        if (mUseFieldOfView) {
            mFieldOfView = FieldOfView;

            return true;
        }

        return SetFocalLength(Engine::Lens::FocalLengthFromFieldOfView(FieldOfView, mSensorHeight));
    }

    Core::FLength Engine::UCameraComponent::GetFocalLength() const {
        return mUseFieldOfView ? Engine::Lens::FocalLengthFromFieldOfView(mFieldOfView, mSensorHeight) : mFocalLength;
    }

    bool Engine::UCameraComponent::SetFocalLength(Core::FLength FocalLength) {
        if (!FocalLength.IsFinite() || FocalLength <= 0_m) {
            return false;
        }

        if (!mUseFieldOfView) {
            mFocalLength = FocalLength;

            return true;
        }

        const Core::FAngle FieldOfView{Engine::Lens::FieldOfViewFromFocalLength(FocalLength, mSensorHeight)};

        if (!FieldOfView.IsFinite() || FieldOfView <= 0_deg || FieldOfView >= 180_deg) {
            return false;
        }

        mFieldOfView = FieldOfView;

        return true;
    }

    Core::FLength Engine::UCameraComponent::GetSensorHeight() const {
        return mSensorHeight;
    }

    bool Engine::UCameraComponent::SetSensorHeight(Core::FLength SensorHeight) {
        if (!SensorHeight.IsFinite() || SensorHeight <= 0_m) {
            return false;
        }

        mSensorHeight = SensorHeight;

        return true;
    }

    const Engine::FDepthOfFieldSettings& Engine::UCameraComponent::GetDepthOfFieldSettings() const {
        return mDepthOfFieldSettings;
    }

    bool Engine::UCameraComponent::SetDepthOfFieldSettings(const Engine::FDepthOfFieldSettings& Settings) {
        if (!Settings.mFocalDistance.IsFinite() || Settings.mFocalDistance < 0_m || !std::isfinite(Settings.mMaxBlurRadius) || Settings.mMaxBlurRadius < 0.0f || !std::isfinite(Settings.mFStop) || Settings.mFStop <= 0.0f) {
            return false;
        }

        if (Settings.mApertureShape != Engine::EDepthOfFieldApertureShape::Circle &&
            Settings.mApertureShape != Engine::EDepthOfFieldApertureShape::Pentagon &&
            Settings.mApertureShape != Engine::EDepthOfFieldApertureShape::Hexagon) {
            return false;
        }

        mDepthOfFieldSettings = Settings;

        return true;
    }

    bool Engine::UCameraComponent::SetDepthOfFieldFocalDistance(Core::FLength Distance) {
        Engine::FDepthOfFieldSettings Settings{mDepthOfFieldSettings};
        Settings.mFocalDistance = Distance;

        return SetDepthOfFieldSettings(Settings);
    }

    Engine::FMinimalViewInfo Engine::UCameraComponent::GetCameraViewInfo() const {
        Engine::FMinimalViewInfo ViewInfo{};
        ViewInfo.mLocation = GetTransform().mLocation;
        ViewInfo.mRotation = GetTransform().mRotation;
        ViewInfo.mFieldOfView = GetFieldOfView();
        ViewInfo.mSensorHeight = mSensorHeight;
        ViewInfo.mDepthOfFieldSettings = mDepthOfFieldSettings;

        return ViewInfo;
    }

    /**
     * @brief 카메라의 월드 변환과 시야각으로 뷰·투영 행렬을 계산합니다.
     */
    bool Engine::UCameraComponent::GetCameraView(const FVector2& Size, Engine::FSceneView& View) const {
        return GetTransform().IsValid() && GetCameraViewInfo().CalcSceneView(Size, View);
    }

}
