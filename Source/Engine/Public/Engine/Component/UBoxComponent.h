/**
 * @file UBoxComponent.h
 * @brief 로컬 반길이로 정의한 상자 충돌 컴포넌트입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UShapeComponent.h"
#include "Core/Literals/UnitLiterals.h"
#include "UBoxComponent.generated.h"

namespace Engine {

    /**
     * @brief 로컬 반길이로 정의한 상자 충돌 컴포넌트입니다.
     */
    UCLASS()
    class UBoxComponent : public Engine::UShapeComponent {
        GENERATED_BODY()

    public:
        UBoxComponent();

    public:
        /**
         * @brief 충돌 형상의 로컬 축 정렬 경계를 계산합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        FBox CalcLocalBounds() const override;

        /**
         * @brief 상자의 로컬 축별 반길이를 설정합니다.
         * @param[in] InExtent 상자 각 축의 반길이입니다.
         */
        void SetBoxExtent(const FVector& InExtent);

        /**
         * @brief 월드 스케일을 반영한 상자 축별 반길이를 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        FVector GetScaledBoxExtent() const;

        /**
         * @brief 상자의 로컬 축별 반길이를 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const FVector& GetBoxExtent() const;

    protected:
        /**
         * @brief 월드 트랜스폼을 적용한 형상의 윤곽선을 수집합니다.
         * @param[in] DrawInterface 보조 선분을 추가할 프레임 수집기입니다.
         * @param[in] Color 선 색상입니다.
         */
        void DrawShape(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector& Color, float Opacity) const override;

        /**
         * @brief 상자 반길이로 상자 형상 하나를 채웁니다.
         * @param[out] Geometry 비어 있는 상태로 전달되는 바디 셋업의 형상 목록입니다.
         */
        void UpdateBodySetup(PhysicsRuntime::FKAggregateGeom& Geometry) const override;

    private:
        /**
         * @brief 상자의 로컬 축별 반길이입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Shape", Getter = GetBoxExtent, Setter = SetBoxExtent)
        FVector mBoxExtent{};
    };

}
