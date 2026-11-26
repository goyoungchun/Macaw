/**
 * @file UShapeComponent.h
 * @brief 렌더 프록시 없이 단순 충돌 형상을 제공하고 에디터 보조 선으로 표시하는 프리미티브 컴포넌트입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UPrimitiveComponent.h"
#include "Core/Math/FVector4.h"
#include "Core/Math/FBox.h"

#include <memory>

#include "UShapeComponent.generated.h"

namespace PhysicsRuntime {
    struct FKAggregateGeom;
}

namespace Engine {

    class FPrimitiveDrawInterface;

    /**
     * @brief 렌더 프록시 없이 단순 충돌 형상을 제공하고 에디터 보조 선으로 표시하는 프리미티브 컴포넌트입니다.
     */
    UCLASS(Abstract)
    class UShapeComponent : public Engine::UPrimitiveComponent {
        GENERATED_BODY()

    public:
        /**
         * @brief 겹침만 감지하는 OverlapAll 프리셋과 겹침 이벤트를 켠 기본 상태로 구성합니다.
         */
        UShapeComponent();

        /**
         * @brief 충돌 형상의 로컬 축 정렬 경계를 계산합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        virtual FBox CalcLocalBounds() const = 0;

        /**
         * @brief 컴포넌트 변환을 적용한 월드 충돌 경계를 계산합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        FBox CalcBounds() const;

        /**
         * @brief 월드 공간의 축 정렬 경계 상자를 반환합니다.
         * @return 월드 공간의 축 정렬 경계 상자입니다.
         */
        FBox GetWorldBounds() const override;

        /**
         * @brief 표시할 프리미티브 메시 종류를 반환합니다.
         * @return 표시할 프리미티브 메시 종류입니다.
         */
        Engine::EPrimitiveType GetPrimitiveType() const override;

        /**
         * @brief 컴포넌트의 형상 크기로 만든 충돌 형상을 반환합니다.
         * @return 이 컴포넌트가 소유한 바디 셋업이며, 형상 크기가 바뀌면 새로 만듭니다.
         * @note 형상은 스케일을 적용하지 않은 크기이며, 컴포넌트 스케일은 바디를 만들 때 반영합니다.
         */
        const PhysicsRuntime::UBodySetup* GetBodySetup() const override;

        /**
         * @brief 충돌 형상을 지정된 색상의 선으로 수집합니다.
         * @param[in] DrawInterface 보조 선분을 추가할 프레임 수집기입니다.
         */
        void DrawDebug(Engine::FPrimitiveDrawInterface& DrawInterface) const;

        /**
         * @brief 충돌 도형을 표시할 RGBA 색상을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const FVector4& GetShapeColor() const;

        /**
         * @brief 충돌 도형을 표시할 RGBA 색상을 설정합니다.
         * @param[in] Color 적용할 색상입니다.
         * @return 값이 유효해 적용했으면 true, 거부했으면 false입니다.
         */
        bool SetShapeColor(const FVector4& Color);

        /**
         * @brief 선택한 경우에만 보조 충돌 도형을 그릴지 검사합니다.
         * @return 조건을 만족하면 true입니다.
         */
        bool ShouldDrawOnlyIfSelected() const;

        /**
         * @brief 선택한 경우에만 충돌 도형을 표시할지 여부를 설정합니다.
         * @param[in] DrawOnlyIfSelected 선택할 때만 보조 도형을 표시할지 지정합니다.
         */
        void SetDrawOnlyIfSelected(bool DrawOnlyIfSelected);

        /**
         * @brief 충돌 형상에 적용할 최대 절대 축 스케일을 반환합니다.
         * @return 계산하거나 조회한 값입니다.
         */
        float GetShapeScale() const;

    protected:
        /**
         * @brief 월드 트랜스폼을 적용한 형상의 윤곽선을 수집합니다.
         * @param[in] DrawInterface 보조 선분을 추가할 프레임 수집기입니다.
         * @param[in] Color 선 색상입니다.
         */
        virtual void DrawShape(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector& Color, float Opacity) const = 0;

        /**
         * @brief 스케일을 적용하지 않은 형상 크기로 충돌 형상 요소를 채웁니다.
         * @param[out] Geometry 비어 있는 상태로 전달되는 바디 셋업의 형상 목록입니다.
         */
        virtual void UpdateBodySetup(PhysicsRuntime::FKAggregateGeom& Geometry) const = 0;

        /**
         * @brief 형상 크기가 바뀌었을 때 바디 셋업을 버리고 물리 바디를 다시 만듭니다.
         */
        void MarkBodySetupDirty();

    private:
        /**
         * @brief 현재 표시·에셋 상태에서 렌더 프록시를 생성할지 판정합니다.
         * @return 조건을 만족하면 true입니다.
         */
        bool ShouldCreateRenderState() const override;

    private:
        /**
         * @brief 충돌 도형을 표시할 RGBA 색상입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Shape", Getter = GetShapeColor, Setter = SetShapeColor)
        FVector4 mShapeColor{223.0f / 255.0f, 149.0f / 255.0f, 157.0f / 255.0f, 1.0f};

        /**
         * @brief 선택한 경우에만 충돌 도형을 표시할지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Shape", Getter = ShouldDrawOnlyIfSelected, Setter = SetDrawOnlyIfSelected)
        bool mDrawOnlyIfSelected{};

        /**
         * @brief 형상 크기로 만든 바디 셋업이며, 처음 요청할 때 만듭니다.
         */
        mutable std::shared_ptr<PhysicsRuntime::UBodySetup> mShapeBodySetup{};
    };

}
