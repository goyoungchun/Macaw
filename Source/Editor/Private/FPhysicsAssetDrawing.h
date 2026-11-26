/**
 * @file FPhysicsAssetDrawing.h
 * @brief 물리 에셋 편집기가 바디 설명서의 충돌 형상을 선으로 그리는 함수를 정의합니다.
 * @ingroup Editor
 */

#pragma once

#include "Core/Math/FTransform.h"
#include "Core/Math/FVector.h"

namespace Engine {
    class FPrimitiveDrawInterface;
}

namespace PhysicsRuntime {
    class UBodySetup;
}

namespace Editor::PhysicsAssetDrawing {

    /**
     * @brief 바디 설명서의 모든 충돌 형상을 바디의 월드 변환 기준 선으로 그립니다.
     * @param[in,out] Draw 선을 기록할 그리기 인터페이스입니다.
     * @param[in] Setup 그릴 충돌 형상을 가진 바디 설명서입니다.
     * @param[in] BodyWorld 바디의 월드 위치·회전과 형상 크기에 반영할 스케일입니다. 바디를 만들 때와 같은 규칙으로 형상 크기를 정합니다.
     * @param[in] Color 선 색상입니다.
     * @param[in] Thickness 픽셀 단위 선 두께입니다.
     * @note 장면 깊이와 무관하게 항상 보이도록 깊이 검사를 하지 않습니다.
     */
    void DrawBodySetup(Engine::FPrimitiveDrawInterface& Draw, const PhysicsRuntime::UBodySetup& Setup, const FTransform& BodyWorld, const FVector& Color, float Thickness);

}
