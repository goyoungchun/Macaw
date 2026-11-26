/**
 * @file PhysicsStats.h
 * @brief 물리 시뮬레이션 처리 시간과 바디·접촉 개수를 측정하는 통계 항목을 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Stats.h"

namespace PhysicsRuntime {

    /**
     * @brief 물리 시뮬레이션 처리 시간과 바디·접촉 개수를 측정하는 통계 항목의 모음입니다.
     * @details 강체는 stat physics, 충돌 쿼리는 stat physicsquery, 래그돌은 stat ragdoll, 천은 stat cloth로 표시합니다.
     */
    namespace PhysicsStats {
        /**
         * @brief 장면 한 스텝의 simulate와 fetchResults 처리 시간을 측정하는 통계 항목입니다.
         */
        extern Core::FStat Simulate;

        extern Core::FStat FetchResults;
        extern Core::FStat VehicleUpdate;

        /**
         * @brief 시뮬레이션 결과를 컴포넌트에 반영하고 충돌 이벤트를 전달하는 처리 시간을 측정하는 통계 항목입니다.
         */
        extern Core::FStat Dispatch;

        /**
         * @brief 진행한 고정 스텝 수를 세는 통계 항목입니다.
         */
        extern Core::FStat Steps;

        /**
         * @brief 새로 시작된 접촉 수를 세는 통계 항목입니다.
         */
        extern Core::FStat Contacts;

        extern Core::FStat ActiveDynamicBodies;
        extern Core::FStat ActiveKinematicBodies;
        extern Core::FStat ActiveConstraints;
        extern Core::FStat DiscreteContactPairs;

        /**
         * @brief 마지막으로 진행한 장면의 동적 바디 수입니다.
         */
        extern Core::FStat DynamicBodies;

        /**
         * @brief 마지막으로 진행한 장면의 정적 바디 수입니다.
         */
        extern Core::FStat StaticBodies;

        extern Core::FStat SweepQueries;
        extern Core::FStat SweepQueryTime;
        extern Core::FStat OverlapQueries;
        extern Core::FStat OverlapQueryTime;
        extern Core::FStat OverlapUpdate;
        extern Core::FStat OverlapPairs;

        extern Core::FStat ArticulatedSetup;
        extern Core::FStat KinematicSync;
        extern Core::FStat KinematicBoneUpdates;
        extern Core::FStat RagdollComponents;
        extern Core::FStat SimulatedBoneBodies;
        extern Core::FStat PoseBlend;
        extern Core::FStat BlendedBones;
        extern Core::FStat AnimRigidBody;

        /**
         * @brief 천 솔버 한 프레임의 처리 시간을 측정하는 통계 항목이며 cloth 그룹에 속합니다.
         */
        extern Core::FStat ClothSimulate;

        extern Core::FStat ClothSolve;

        /**
         * @brief 천 시뮬레이션 메시를 현재 포즈로 스키닝하는 처리 시간을 측정하는 통계 항목이며 cloth 그룹에 속합니다.
         */
        extern Core::FStat ClothSkinPhysMesh;

        /**
         * @brief 천 충돌체를 이번 프레임 뼈 위치로 옮겨 넣는 처리 시간을 측정하는 통계 항목이며 cloth 그룹에 속합니다.
         */
        extern Core::FStat ClothUpdateCollisions;

        extern Core::FStat ClothWriteback;

        /**
         * @brief 이번 프레임에 시뮬레이션이 처리한 천 입자 수이며 cloth 그룹에 속합니다.
         */
        extern Core::FStat ClothParticles;
    }

}
