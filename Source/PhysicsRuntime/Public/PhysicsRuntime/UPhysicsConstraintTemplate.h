/**
 * @file UPhysicsConstraintTemplate.h
 * @brief Physics Asset에 저장하는 관절 하나의 기본 설정을 정의합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "CoreUObject/UObject.h"
#include "PhysicsRuntime/FConstraintInstance.h"
#include "UPhysicsConstraintTemplate.generated.h"

namespace PhysicsRuntime {
    /**
     * @brief Physics Asset에 저장하는 관절 하나의 기본 설정입니다.
     * @ingroup PhysicsRuntime
     * @note 실행 중에는 이 기본값을 복사해 관절마다 사용합니다.
     */
    UCLASS()
    class UPhysicsConstraintTemplate : public CoreUObject::UObject {
        GENERATED_BODY()

    public:
        /**
         * @brief 기본 관절 설정으로 구성합니다.
         */
        UPhysicsConstraintTemplate();

        /**
         * @brief 관절 설정을 정리합니다.
         */
        ~UPhysicsConstraintTemplate() override;

    public:
        /**
         * @brief 관절의 기본 설정입니다.
         */
        FConstraintInstance mDefaultInstance{};
    };
}
