/**
 * @file CollisionTypes.h
 * @brief 바디가 쿼리·시뮬레이션에 참여하는 방식과 다른 바디에 대한 응답, 그리고 이를 묶은 프리셋을 정의합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "CollisionTypes.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 바디가 장면 쿼리와 물리 시뮬레이션 중 어디에 참여하는지 정합니다.
     * @ingroup PhysicsRuntime
     */
    UENUM()
    enum class ECollisionEnabled : Uint8 {
        /**
         * @brief 쿼리와 시뮬레이션 모두에서 빠집니다.
         */
        NoCollision,
        /**
         * @brief 레이캐스트·스윕·오버랩 같은 장면 쿼리에만 참여합니다.
         */
        QueryOnly,
        /**
         * @brief 물리 시뮬레이션 접촉에만 참여합니다.
         */
        PhysicsOnly,
        /**
         * @brief 장면 쿼리와 물리 시뮬레이션 모두에 참여합니다.
         */
        QueryAndPhysics
    };

    /**
     * @brief 다른 바디와 만났을 때의 응답입니다.
     * @details 두 바디의 응답 중 약한 쪽을 씁니다. 값이 클수록 강한 응답입니다.
     * @ingroup PhysicsRuntime
     */
    UENUM()
    enum class ECollisionResponse : Uint8 {
        /**
         * @brief 서로 무시합니다.
         */
        Ignore,
        /**
         * @brief 막지 않고 겹침으로만 감지합니다.
         */
        Overlap,
        /**
         * @brief 이동을 막고 물리 접촉을 만듭니다.
         */
        Block
    };

    /**
     * @brief 충돌 참여 방식과 응답을 함께 정하는 프리셋입니다.
     * @ingroup PhysicsRuntime
     */
    UENUM()
    enum class ECollisionPreset : Uint8 {
        /**
         * @brief 참여 방식과 응답을 직접 정합니다.
         */
        Custom,
        /**
         * @brief 충돌하지 않습니다. NoCollision과 Ignore입니다.
         */
        NoCollision,
        /**
         * @brief 모든 바디를 막습니다. QueryAndPhysics와 Block입니다.
         */
        BlockAll,
        /**
         * @brief 모든 바디와 겹침만 감지합니다. QueryOnly와 Overlap입니다.
         */
        OverlapAll
    };

    /**
     * @brief 두 바디의 응답 중 실제로 적용할 응답을 반환합니다.
     * @param[in] First 한쪽 바디의 응답입니다.
     * @param[in] Second 다른 쪽 바디의 응답입니다.
     * @return 둘 중 약한 응답입니다.
     */
    constexpr PhysicsRuntime::ECollisionResponse CombineCollisionResponse(PhysicsRuntime::ECollisionResponse First, PhysicsRuntime::ECollisionResponse Second) {
        return First < Second ? First : Second;
    }

}
