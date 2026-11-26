/**
 * @file FViewerCamera.cpp
 * @brief 에셋 경계를 기준으로 Fit, 회전, 이동과 확대를 수행하는 궤도 카메라입니다.
 * @ingroup Viewer
 */

#include "Core/Types.h"
#include "Core/Literals/AngleFunctions.h"
#include "Viewer/FViewerCamera.h"

#include "Core/Math.h"
#include "Core/Math/FQuat.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

using namespace Viewer;

FViewerCamera::FViewerCamera()
	: mRotation{-20_deg, 45_deg, 0_deg},
	  mRadius{(1_m).GetBase()},
	  mDistance{(3_m).GetBase()},
	  mFieldOfView{60_deg} {
}

bool FViewerCamera::Fit(const FBox& Bounds, const FVector2& Size, float Padding) {
    if (!std::isfinite(Size.x) || !std::isfinite(Size.y) || Size.x <= 0.0f || Size.y <= 0.0f || !std::isfinite(Padding) || Padding < 1.0f) {
        return false;
    }

    const FVector Center{(Bounds.mMinimum + Bounds.mMaximum) * 0.5f};
    const FVector Extent{(Bounds.mMaximum - Bounds.mMinimum) * 0.5f};

    if (!std::isfinite(Center.x) || !std::isfinite(Center.y) || !std::isfinite(Center.z) || !std::isfinite(Extent.x) || !std::isfinite(Extent.y) || !std::isfinite(Extent.z) || Extent.x < 0.0f || Extent.y < 0.0f || Extent.z < 0.0f) {
        return false;
    }

    const FQuat Rotation{mRotation.Quaternion()};
    const FVector Forward{Rotation.ForwardVector()};
    const FVector Right{Rotation.RightVector()};
    const FVector Up{Rotation.UpVector()};
    const float TanVertical{Core::Math::Tan(mFieldOfView * 0.5f)};
    const float TanHorizontal{TanVertical * Size.x / Size.y};
    const float Radius{std::max(Extent.Length(), (0.01_m).GetBase())};
    const float MinimumDepth{std::max(Radius * 0.01f, (0.0001_m).GetBase())};
    float Distance{MinimumDepth};

    for (Index_t Corner{}; Corner < 8; ++Corner) {
        const FVector Offset{(Corner & 1) != 0 ? Extent.x : -Extent.x, (Corner & 2) != 0 ? Extent.y : -Extent.y, (Corner & 4) != 0 ? Extent.z : -Extent.z};
        const float Depth{Offset.Dot(Forward)};

        Distance = std::max({Distance, Padding * std::abs(Offset.Dot(Right)) / TanHorizontal - Depth, Padding * std::abs(Offset.Dot(Up)) / TanVertical - Depth, MinimumDepth - Depth});
    }

    if (!std::isfinite(Distance)) {
        return false;
    }

    mCenter = Center;
    mRadius = Radius;
    mDistance = std::max(Distance, Radius * 0.02f);

    return true;
}

Engine::FSceneView FViewerCamera::CalcSceneView(const FVector2& Size) const {
    const FQuat Rotation{mRotation.Quaternion()};
    Engine::FSceneView View{};
    View.mSize = Size;
    View.mLocation = mCenter - Rotation.ForwardVector() * mDistance;
    View.mForward = Rotation.ForwardVector();
    View.mFieldOfView = mFieldOfView;
    View.mView = FMatrix::LookTo(View.mLocation, View.mForward, Rotation.UpVector());
    View.mProjection = FMatrix::Perspective(mFieldOfView, std::max(Size.x, 1.0f) / std::max(Size.y, 1.0f), std::max(Core::FLength::FromBase(mRadius * 0.001f), 0.00001_m), Core::FLength::FromBase(std::max(mDistance + mRadius * 3.0f, mRadius * 10.0f)));
    View.mViewProjection = View.mView * View.mProjection;

    return View;
}

void FViewerCamera::Orbit(const FVector2& Delta) {
    if (std::isfinite(Delta.x) && std::isfinite(Delta.y)) {
        mRotation.yaw += Delta.x * Core::Math::ToDegrees(0.3_deg);
        mRotation.pitch = std::clamp(mRotation.pitch - Delta.y * Core::Math::ToDegrees(0.3_deg), Core::Math::ToDegrees(-89_deg), Core::Math::ToDegrees(89_deg));
    }
}

void FViewerCamera::Pan(const FVector2& Delta, float Height) {
    if (std::isfinite(Delta.x) && std::isfinite(Delta.y) && std::isfinite(Height) && Height > 0.0f) {
        const FQuat Rotation{mRotation.Quaternion()};

        mCenter += (Rotation.RightVector() * -Delta.x + Rotation.UpVector() * Delta.y) * (mDistance / Height);
    }
}

void FViewerCamera::Zoom(float Delta) {
    if (std::isfinite(Delta)) {
        mDistance = std::clamp(mDistance * std::exp(-Delta * 0.15f), mRadius * 0.02f, mRadius * 1000.0f);
    }
}

FVector FViewerCamera::GetLocation() const {
    return mCenter - mRotation.Quaternion().ForwardVector() * mDistance;
}

void FViewerCamera::MoveEye(const FVector& Delta) {
    if (Delta.IsFinite()) {
        mCenter += Delta;
    }
}

void FViewerCamera::LookAround(const FVector2& Delta) {
    if (!std::isfinite(Delta.x) || !std::isfinite(Delta.y)) {
        return;
    }

    // 눈 위치를 먼저 구해 두고 회전한 뒤, 같은 거리만큼 새 시선 방향 앞에 중심을 다시 놓는다.
    const FVector Eye{GetLocation()};

    mRotation.yaw += Delta.x * 0.3f;
    mRotation.pitch = std::clamp(mRotation.pitch - Delta.y * 0.3f, -89.0f, 89.0f);
    mCenter = Eye + mRotation.Quaternion().ForwardVector() * mDistance;
}

void FViewerCamera::SetRotation(const FRotator& Rotation) {
    if (std::isfinite(Rotation.pitch) && std::isfinite(Rotation.yaw) && std::isfinite(Rotation.roll)) {
        mRotation = Rotation;
    }
}

void FViewerCamera::SetFieldOfView(Core::FAngle FieldOfView) {
    if (FieldOfView.IsFinite()) {
        mFieldOfView = std::clamp(FieldOfView, 1_deg, 170_deg);
    }
}

const FRotator& FViewerCamera::GetRotation() const {
    return mRotation;
}

const FVector& FViewerCamera::GetCenter() const {
    return mCenter;
}

float FViewerCamera::GetRadius() const {
    return mRadius;
}

float FViewerCamera::GetDistance() const {
    return mDistance;
}
