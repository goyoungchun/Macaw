/**
 * @file FClothingSimulationContextCommon.h
 * @brief 천 시뮬레이션 한 프레임에 넘기는 입력을 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"

#include "Core/Types.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FVector.h"

namespace PhysicsRuntime {

    /**
     * @brief 이번 프레임에 천을 순간 이동시킬지 나타냅니다.
     * @ingroup PhysicsRuntime
     */
    enum class EClothingTeleportMode : Uint8 {
        /**
         * @brief 순간 이동하지 않고 기준 뼈의 움직임을 관성으로 전합니다.
         */
        None,

        /**
         * @brief 천을 새 위치로 옮기되 지금 속도는 유지합니다.
         */
        Teleport,

        /**
         * @brief 천을 새 위치로 옮기고 입자를 스키닝 위치에 다시 놓아 속도를 없앱니다.
         */
        TeleportAndReset
    };

    /**
     * @brief 천 시뮬레이션 한 프레임의 입력입니다.
     * @details 천을 소유한 컴포넌트가 애니메이션 포즈를 정한 뒤 채워서 넘깁니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothingSimulationContextCommon {
        /**
         * @brief 스켈레톤 뼈 순서의 컴포넌트 좌표 뼈 행렬입니다.
         */
        Core::TArray<FMatrix> mBoneTransforms{};

        /**
         * @brief 스켈레톤 뼈 순서의 스키닝 행렬(역바인드 * 컴포넌트 좌표 뼈 행렬)입니다.
         */
        Core::TArray<FMatrix> mRefToLocals{};

        /**
         * @brief 컴포넌트의 월드 행렬입니다.
         */
        FMatrix mComponentToWorld{};

        /**
         * @brief 월드의 중력 가속도(m/s²)입니다.
         */
        FVector mWorldGravity{0.0f, 0.0f, -9.81f};

        /**
         * @brief 이번 프레임에 진행할 시간(초)이며 최대 물리 시간으로 잘라 둔 값입니다.
         */
        Core::FTime mDeltaTime{};

        /**
         * @brief 메시의 상세 단계 번호입니다.
         */
        Index_t mPredictedLod{};

        /**
         * @brief 입자마다의 최대 거리에 곱할 배율이며 1이면 천 에셋 값을 그대로 씁니다.
         */
        float mMaxDistanceScale{1.0f};

        /**
         * @brief 월드 좌표의 바람 속도(m/s)입니다.
         */
        FVector mWindVelocity{};

        /**
         * @brief 단순한 바람 방식에서 바람 쪽으로 속도를 맞추는 정도이며 클수록 빨리 바람을 따라갑니다.
         */
        float mWindAdaption{2.0f};

        /**
         * @brief 이번 프레임에 천을 순간 이동시킬지 나타냅니다.
         */
        EClothingTeleportMode mTeleportMode{EClothingTeleportMode::None};
    };
}
