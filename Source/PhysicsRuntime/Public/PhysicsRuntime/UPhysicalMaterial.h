/**
 * @file UPhysicalMaterial.h
 * @brief 충돌 형상에 적용할 마찰과 반발 계수를 정의합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "CoreUObject/UObject.h"
#include "UPhysicalMaterial.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 충돌 형상에 적용할 마찰과 반발 계수를 보관합니다.
     * @ingroup PhysicsRuntime
     */
    UCLASS()
    class UPhysicalMaterial final : public CoreUObject::UObject {
        GENERATED_BODY()

    public:

        /**
         * @brief 정지 마찰 계수를 반환합니다.
         */
        float GetStaticFriction() const;

        /**
         * @brief 유한한 0 이상의 정지 마찰 계수를 적용합니다.
         * @return 값을 적용했으면 true입니다.
         */
        bool SetStaticFriction(float Friction);

        /**
         * @brief 운동 마찰 계수를 반환합니다.
         */
        float GetDynamicFriction() const;

        /**
         * @brief 유한한 0 이상의 운동 마찰 계수를 적용합니다.
         * @return 값을 적용했으면 true입니다.
         */
        bool SetDynamicFriction(float Friction);

        /**
         * @brief 반발 계수를 반환합니다.
         */
        float GetRestitution() const;

        /**
         * @brief 유한한 0 이상 1 이하의 반발 계수를 적용합니다.
         * @return 값을 적용했으면 true입니다.
         */
        bool SetRestitution(float Restitution);

    private:

        /**
         * @brief 접촉한 물체가 미끄러지기 전의 마찰 계수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Getter = GetStaticFriction, Setter = SetStaticFriction)
        float mStaticFriction{0.5f};

        /**
         * @brief 접촉한 물체가 미끄러지는 동안의 마찰 계수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Getter = GetDynamicFriction, Setter = SetDynamicFriction)
        float mDynamicFriction{0.5f};

        /**
         * @brief 충돌 후 튀어 오르는 정도를 정하는 반발 계수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Getter = GetRestitution, Setter = SetRestitution)
        float mRestitution{0.6f};
    };

}
