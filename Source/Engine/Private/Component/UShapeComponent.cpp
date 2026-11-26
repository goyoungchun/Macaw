/**
 * @file UShapeComponent.cpp
 * @brief 렌더 프록시 없이 단순 충돌 형상을 제공하고 에디터 보조 선으로 표시하는 프리미티브 컴포넌트입니다.
 * @ingroup Engine
 */

#include "Core/Types.h"
#include "Engine/Component/UShapeComponent.h"
#include "Engine/AActor.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/EPrimitiveType.h"
#include "Core/Math/FBox.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <algorithm>
#include <cmath>

namespace Engine {

    /**
     * @brief 겹침만 감지하는 OverlapAll 프리셋과 겹침 이벤트를 켠 기본 상태로 구성합니다.
     */
    Engine::UShapeComponent::UShapeComponent() {
        // 언리얼의 셰이프 컴포넌트처럼 막지 않고 겹침만 감지하는 것이 기본입니다.
        SetCollisionPreset(PhysicsRuntime::ECollisionPreset::OverlapAll);
        SetGenerateOverlapEvents(true);
    }

    /**
     * @brief 현재 표시·에셋 상태에서 렌더 프록시를 생성할지 판정합니다.
     */
    bool Engine::UShapeComponent::ShouldCreateRenderState() const {
        return false;
    }

    /**
     * @brief 충돌 형상을 지정된 색상의 선으로 수집합니다.
     */
    void Engine::UShapeComponent::DrawDebug(Engine::FPrimitiveDrawInterface& DrawInterface) const {
        const FVector Color{mShapeColor.x, mShapeColor.y, mShapeColor.z};

        DrawShape(DrawInterface, Color, mShapeColor.w);
    }

    /**
     * @brief 컴포넌트의 형상 크기로 만든 충돌 형상을 반환합니다.
     */
    const PhysicsRuntime::UBodySetup* Engine::UShapeComponent::GetBodySetup() const {
        if (mShapeBodySetup == nullptr) {
            std::shared_ptr<PhysicsRuntime::UBodySetup> Setup{CoreUObject::NewObject<PhysicsRuntime::UBodySetup>()};
            Setup->SetName(GetName() + "BodySetup");
            UpdateBodySetup(Setup->GetAggGeom());
            mShapeBodySetup = std::move(Setup);
        }

        return mShapeBodySetup.get();
    }

    /**
     * @brief 형상 크기가 바뀌었을 때 바디 셋업을 버리고 물리 바디를 다시 만듭니다.
     */
    void Engine::UShapeComponent::MarkBodySetupDirty() {
        mShapeBodySetup.reset();
        RecreatePhysicsState();
    }

    /**
     * @brief 선택한 경우에만 보조 충돌 도형을 그릴지 검사합니다.
     */
    bool Engine::UShapeComponent::ShouldDrawOnlyIfSelected() const {
        return mDrawOnlyIfSelected;
    }

    /**
     * @brief 충돌 형상에 적용할 최대 절대 축 스케일을 반환합니다.
     */
    float Engine::UShapeComponent::GetShapeScale() const {
        FVector Scale{GetTransform().mScale};

        return std::min({ std::abs(Scale.x), std::abs(Scale.y), std::abs(Scale.z) });
    }

    /**
     * @brief 컴포넌트 변환을 적용한 월드 충돌 경계를 계산합니다.
     */
    FBox Engine::UShapeComponent::CalcBounds() const {
        return CalcLocalBounds().Transformed(GetWorldMatrix());
    }

    /**
     * @brief 월드 공간의 축 정렬 경계 상자를 반환합니다.
     */
    FBox Engine::UShapeComponent::GetWorldBounds() const {
        return CalcBounds();
    }

    /**
     * @brief 표시할 프리미티브 메시 종류를 반환합니다.
     */
    Engine::EPrimitiveType Engine::UShapeComponent::GetPrimitiveType() const {
        return Engine::EPrimitiveType::Cube;
    }

    /**
     * @brief 충돌 도형을 표시할 RGBA 색상을 반환합니다.
     */
    const FVector4& Engine::UShapeComponent::GetShapeColor() const {
        return mShapeColor;
    }

    /**
     * @brief 충돌 도형을 표시할 RGBA 색상을 설정합니다.
     */
    bool Engine::UShapeComponent::SetShapeColor(const FVector4& Color) {
        if (!std::isfinite(Color.x) || !std::isfinite(Color.y) || !std::isfinite(Color.z) || !std::isfinite(Color.w)) {
            return false;
        }

        mShapeColor = Color;

        return true;
    }

    /**
     * @brief 선택한 경우에만 충돌 도형을 표시할지 여부를 설정합니다.
     */
    void Engine::UShapeComponent::SetDrawOnlyIfSelected(bool DrawOnlyIfSelected) {
        mDrawOnlyIfSelected = DrawOnlyIfSelected;
    }

}
