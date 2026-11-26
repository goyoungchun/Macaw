/**
 * @file ETickingGroup.h
 * @brief Tick 실행 그룹의 순서와 월드 내부 경계를 정의합니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "ETickingGroup.generated.h"

namespace Engine {

    /** @brief 일반 프레임에서 Tick 실행 그룹이 열리는 순서입니다. */
    UENUM()
    enum class ETickingGroup : Uint8 {
        /** @brief 물리 시작 경계보다 앞서 실행하는 그룹입니다. */
        PrePhysics,

        /** @brief 물리 시뮬레이션 시작을 위해 예약한 내부 경계입니다. */
        StartPhysics UMETA(Hidden),

        /** @brief 물리 시작 경계와 완료 경계 사이에 실행하는 그룹입니다. */
        DuringPhysics,

        /** @brief 물리 시뮬레이션 완료를 위해 예약한 내부 경계입니다. */
        EndPhysics UMETA(Hidden),

        /** @brief 물리 완료 경계 이후에 실행하는 그룹입니다. */
        PostPhysics,

        /** @brief 게임 월드의 겹침 처리와 카메라 갱신 이후에 실행하는 그룹입니다. */
        PostUpdateWork,

        /** @brief PostUpdateWork를 지난 뒤 새로 배치된 Tick을 실행하는 내부 그룹입니다. */
        LastDemotable UMETA(Hidden),

        /** @brief LastDemotable 이후 프레임 끝에서 새로 들어온 Tick을 처리하는 내부 그룹입니다. */
        NewlySpawned UMETA(Hidden),

        /** @brief 그룹 큐의 개수이자 소속된 실행 그룹이 없음을 나타내는 값입니다. */
        Count UMETA(Hidden)
    };

}
