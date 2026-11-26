/**
 * @file FPhysicsContact.h
 * @brief 한 시뮬레이션 스텝에서 새로 닿기 시작한 두 강체의 접촉 정보입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Math/FVector.h"

namespace physx {
    class PxRigidActor;
}

namespace PhysicsRuntime {

    /**
     * @brief 한 시뮬레이션 스텝에서 새로 닿기 시작한 두 강체의 접촉 정보입니다.
     * @details 물리 장면이 시뮬레이션 결과를 받는 동안 기록하고, 결과 수신이 끝난 뒤 이용자가 조회합니다.
     * @note 강체 주소는 다음 시뮬레이션 전까지만 유효하며, 그 사이에 강체를 해제하면 사용하지 않아야 합니다.
     * @ingroup PhysicsRuntime
     */
    struct FPhysicsContact {
        /**
         * @brief 접촉한 첫 번째 강체입니다.
         */
        physx::PxRigidActor* mActor0{};

        /**
         * @brief 접촉한 두 번째 강체입니다.
         */
        physx::PxRigidActor* mActor1{};

        /**
         * @brief 접촉점들의 평균 월드 위치입니다.
         */
        FVector mPoint{};

        /**
         * @brief 두 번째 강체에서 첫 번째 강체를 향하는 첫 접촉점의 월드 법선입니다.
         */
        FVector mNormal{};

        /**
         * @brief 접촉점들에 가해진 충격량의 합(N·s)입니다.
         */
        FVector mImpulse{};
    };

}
