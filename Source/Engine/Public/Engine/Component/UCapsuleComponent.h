/**
 * @file UCapsuleComponent.h
 * @brief Z축 중심 선분과 반구를 가진 캡슐 충돌 컴포넌트입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UShapeComponent.h"
#include "Core/Literals/UnitLiterals.h"
#include "UCapsuleComponent.generated.h"

namespace Engine {

    /**
     * @brief Z축 중심 선분과 반구를 가진 캡슐 충돌 컴포넌트입니다.
     */
    UCLASS()
    class UCapsuleComponent : public Engine::UShapeComponent {
        GENERATED_BODY()

    public:
        UCapsuleComponent();

    public:
        /**
         * @brief 충돌 형상의 로컬 축 정렬 경계를 계산합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        FBox CalcLocalBounds() const override;

        /**
         * @brief 아카이브 모드에 따라 값을 읽거나 저장합니다.
         * @param[in,out] Archive 모드에 따라 값을 읽거나 쓸 아카이브입니다.
         */
        void Serialize(Core::FArchive& Archive) override;

        /**
         * @brief 캡슐의 로컬 반지름과 반높이를 함께 설정합니다.
         * @param[in] InRadius 형상의 반지름입니다.
         * @param[in] InHalfHeight 반구를 포함한 캡슐 반높이입니다.
         */
        void SetCapsuleSize(Core::FLength InRadius, Core::FLength InHalfHeight);

        /**
         * @brief 월드 스케일을 반영한 캡슐 반지름을 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FLength GetScaledCapsuleRadius() const;

        /**
         * @brief 월드 스케일을 반영한 캡슐 반높이를 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FLength GetScaledCapsuleHalfHeight() const;

        /**
         * @brief 반구를 제외한 캡슐 중심 선분 반길이를 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FLength GetScaledCapsuleHalfHeightWithoutHemisphere() const;

        /**
         * @brief 반구를 포함한 캡슐 반높이를 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FLength GetCapsuleHalfHeight() const;

        /**
         * @brief 반구를 포함한 캡슐 반높이를 설정합니다.
         * @param[in] InHalfHeight 반구를 포함한 캡슐 반높이입니다.
         */
        void SetCapsuleHalfHeight(Core::FLength InHalfHeight);

        /**
         * @brief 캡슐의 로컬 반지름을 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FLength GetCapsuleRadius() const;

        /**
         * @brief 캡슐의 로컬 반지름을 설정합니다.
         * @param[in] InRadius 형상의 반지름입니다.
         */
        void SetCapsuleRadius(Core::FLength InRadius);

    protected:
        /**
         * @brief 월드 트랜스폼을 적용한 형상의 윤곽선을 수집합니다.
         * @param[in] DrawInterface 보조 선분을 추가할 프레임 수집기입니다.
         * @param[in] Color 선 색상입니다.
         */
        void DrawShape(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector& Color, float Opacity) const override;

        /**
         * @brief 캡슐 반지름과 반높이로 캡슐 형상 하나를 채웁니다.
         * @param[out] Geometry 비어 있는 상태로 전달되는 바디 셋업의 형상 목록입니다.
         */
        void UpdateBodySetup(PhysicsRuntime::FKAggregateGeom& Geometry) const override;

    private:
        // 반구를 포함한 높이
        /**
         * @brief 반구를 포함한 캡슐 반높이입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Shape", Getter = GetCapsuleHalfHeight, Setter = SetCapsuleHalfHeight)
        Core::FLength mCapsuleHalfHeight{};

        /**
         * @brief 캡슐의 로컬 반지름입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Shape", Getter = GetCapsuleRadius, Setter = SetCapsuleRadius)
        Core::FLength mCapsuleRadius{};
    };

}
