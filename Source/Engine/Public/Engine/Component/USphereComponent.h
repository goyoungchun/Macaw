/**
 * @file USphereComponent.h
 * @brief 로컬 반지름으로 정의한 구 충돌 컴포넌트입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UShapeComponent.h"
#include "Core/Literals/UnitLiterals.h"
#include "USphereComponent.generated.h"

namespace Engine {

    /**
     * @brief 로컬 반지름으로 정의한 구 충돌 컴포넌트입니다.
     */
    UCLASS()
    class USphereComponent : public Engine::UShapeComponent {
        GENERATED_BODY()

    public:
        USphereComponent();

    public:
        /**
         * @brief 충돌 형상의 로컬 축 정렬 경계를 계산합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        FBox CalcLocalBounds() const override;

        /**
         * @brief 구의 로컬 반지름을 설정합니다.
         * @param[in] InRadius 형상의 반지름입니다.
         */
        void SetSphereRadius(Core::FLength InRadius);

        /**
         * @brief 월드 스케일을 반영한 구 반지름을 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FLength GetScaledSphereRadius() const;

        /**
         * @brief 구의 로컬 반지름을 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FLength GetSphereRadius() const;

    protected:
        /**
         * @brief 월드 트랜스폼을 적용한 형상의 윤곽선을 수집합니다.
         * @param[in] DrawInterface 보조 선분을 추가할 프레임 수집기입니다.
         * @param[in] Color 선 색상입니다.
         */
        void DrawShape(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector& Color, float Opacity) const override;

        /**
         * @brief 구 반지름으로 구 형상 하나를 채웁니다.
         * @param[out] Geometry 비어 있는 상태로 전달되는 바디 셋업의 형상 목록입니다.
         */
        void UpdateBodySetup(PhysicsRuntime::FKAggregateGeom& Geometry) const override;

    private:
        /**
         * @brief 구의 로컬 반지름입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Shape", Getter = GetSphereRadius, Setter = SetSphereRadius)
        Core::FLength mSphereRadius{};
    };

}
