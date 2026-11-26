/**
 * @file FFlyCameraController.cpp
 * @brief 뷰어 카메라를 우클릭 + WASD/QE로 자유롭게 날아다니게 하는 입력 처리를 구현합니다.
 * @ingroup Viewer
 */

#include "Viewer/FFlyCameraController.h"

#include "Core/Math/FQuat.h"
#include "Core/Math/FVector.h"

#include <algorithm>
#include <cmath>

using namespace Viewer;

void FFlyCameraController::Update(FViewerCamera& Camera, const FFlyInput& Input) {
    // 눈을 고정한 채 시선을 돌린다.
    Camera.LookAround(Input.mMouseDelta);

    // 비행 중 휠은 확대·축소 대신 이동 속도를 조절한다.
    if (Input.mWheel != 0.0f && std::isfinite(Input.mWheel)) {
        mSpeed = std::clamp(mSpeed * std::pow(1.25f, Input.mWheel), 0.1f, 64.0f);
    }

    const FQuat Rotation{Camera.GetRotation().Quaternion()};
    FVector Move{};

    Move += Input.mForward ? Rotation.ForwardVector() : FVector{};
    Move -= Input.mBackward ? Rotation.ForwardVector() : FVector{};
    Move += Input.mRight ? Rotation.RightVector() : FVector{};
    Move -= Input.mLeft ? Rotation.RightVector() : FVector{};
    Move += Input.mUp ? FVector{0.0f, 0.0f, 1.0f} : FVector{};
    Move -= Input.mDown ? FVector{0.0f, 0.0f, 1.0f} : FVector{};

    if (Move.LengthSquared() > 0.0f && std::isfinite(Input.mDeltaTime)) {
        const float Speed{Camera.GetRadius() * mSpeed * (Input.mFast ? 3.0f : 1.0f)};

        Camera.MoveEye(Move.Normalized() * Speed * std::max(Input.mDeltaTime, 0.0f));
    }
}

float FFlyCameraController::GetSpeed() const {
    return mSpeed;
}
