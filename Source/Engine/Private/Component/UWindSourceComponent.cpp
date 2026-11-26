/**
 * @file UWindSourceComponent.cpp
 * @brief 월드에 바람을 내는 원천 컴포넌트를 구현합니다.
 * @ingroup Engine
 */

#include "Engine/Component/UWindSourceComponent.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/FWindField.h"
#include "Engine/UWorld.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Engine {

    namespace {

        /**
         * @brief 원점에서 방향으로 화살표를 그립니다. 몸통과 화살촉 두 줄입니다.
         * @param[in,out] Draw 선을 추가할 표시 목록입니다.
         * @param[in] Start 화살표 시작 위치입니다.
         * @param[in] Direction 정규화된 방향입니다.
         * @param[in] Length 화살표 길이(m)입니다.
         * @param[in] Color 선 색입니다.
         */
        void DrawArrow(Engine::FPrimitiveDrawInterface& Draw, const FVector& Start, const FVector& Direction, float Length, const FVector& Color) {
            const FVector End{Start + Direction * Length};
            // 방향과 수직인 임의의 축을 구해 화살촉의 두 날개를 만든다.
            const FVector Up{std::abs(Direction.z) < 0.95f ? FVector{0.0f, 0.0f, 1.0f} : FVector{1.0f, 0.0f, 0.0f}};
            const FVector Side{Direction.Cross(Up).Normalized()};
            const float Head{Length * 0.2f};

            Draw.DrawLine(Start, End, Color, false, 2.0f);
            Draw.DrawLine(End, End - Direction * Head + Side * (Head * 0.5f), Color, false, 2.0f);
            Draw.DrawLine(End, End - Direction * Head - Side * (Head * 0.5f), Color, false, 2.0f);
        }

    }

    /**
     * @brief 기본값으로 구성합니다.
     */
    Engine::UWindSourceComponent::UWindSourceComponent() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Engine::UWindSourceComponent::~UWindSourceComponent() = default;

    /**
     * @brief 컴포넌트를 월드에 연결하고 월드의 바람장에 원천으로 등록합니다.
     */
    void Engine::UWindSourceComponent::RegisterComponent(Engine::UWorld* World) {
        Engine::USceneComponent::RegisterComponent(World);

        if (Engine::UWorld* Current{GetWorld()}) {
            Current->GetWindField().RegisterSource(this);
        }
    }

    /**
     * @brief 월드의 바람장에서 원천을 빼고 월드와의 연결을 해제합니다.
     */
    void Engine::UWindSourceComponent::UnregisterComponent() {
        if (Engine::UWorld* Current{GetWorld()}) {
            Current->GetWindField().UnregisterSource(this);
        }

        Engine::USceneComponent::UnregisterComponent();
    }

    /**
     * @brief 이 원천이 월드 위치에 만드는 바람 속도를 계산합니다.
     */
    FVector Engine::UWindSourceComponent::SampleWind(const FVector& Position) const {
        const float Strength{std::isfinite(mStrength) ? std::max(mStrength, 0.0f) : 0.0f};

        if (!mWindEnabled || Strength <= 0.0f) {
            return FVector{};
        }

        const FTransform& Transform{GetTransform()};
        const FVector Forward{Transform.mRotation.ForwardVector()};

        if (mWindType == Engine::EWindSourceType::Directional) {
            return Forward * Strength;
        }

        const float Radius{std::isfinite(mRadius) ? std::max(mRadius, 0.001f) : 0.001f};
        const FVector Offset{Position - Transform.mLocation};
        const float Distance{Offset.Length()};

        if (Distance >= Radius) {
            return FVector{};
        }

        // 가까울수록 세다: (1 - d/R)^지수. 지수가 0이면 반경 안에서 일정하다.
        const float Falloff{std::pow(1.0f - Distance / Radius, std::max(mFalloff, 0.0f))};

        if (mWindType == Engine::EWindSourceType::Point) {
            // 원천 바로 위는 방향이 정해지지 않으므로 바람이 없다고 본다.
            return Distance > 1.0e-4f ? Offset * (Strength * Falloff / Distance) : FVector{};
        }

        // 원뿔: 앞쪽 축에서 반각 안에서만 불고, 축에서 멀어질수록 약해진다.
        if (Distance <= 1.0e-4f) {
            return Forward * Strength;
        }

        const float CosHalf{std::cos(std::clamp(mConeHalfAngle, 1.0f, 89.0f) * std::numbers::pi_v<float> / 180.0f)};
        const float CosAngle{Offset.Dot(Forward) / Distance};

        if (CosAngle <= CosHalf) {
            return FVector{};
        }

        const float Edge{(CosAngle - CosHalf) / (1.0f - CosHalf)};

        return Forward * (Strength * Falloff * Edge);
    }

    /**
     * @brief 원천의 방향과 범위를 에디터 뷰포트에 선으로 그립니다.
     */
    void Engine::UWindSourceComponent::DrawVisualization(Engine::FPrimitiveDrawInterface& Draw, bool Highlight) const {
        const FTransform& Transform{GetTransform()};
        const FVector Origin{Transform.mLocation};
        const FVector Forward{Transform.mRotation.ForwardVector()};
        const FVector Right{Transform.mRotation.RightVector()};
        const FVector Up{Transform.mRotation.UpVector()};
        const FVector Color{mWindEnabled ? (Highlight ? FVector{1.0f, 0.65f, 0.15f} : FVector{0.4f, 0.85f, 1.0f}) : FVector{0.5f, 0.5f, 0.5f}};
        const float Length{std::clamp(mStrength * 0.2f, 0.5f, 3.0f)};

        if (mWindType == Engine::EWindSourceType::Directional) {
            // 방향 바람은 위치에 의미가 없으므로 컴포넌트 주변에 화살표 몇 개로 방향만 보여 준다.
            for (const FVector& Offset : {FVector{}, Right * 0.5f, Right * -0.5f, Up * 0.5f, Up * -0.5f}) {
                DrawArrow(Draw, Origin + Offset, Forward, Length, Color);
            }

            return;
        }

        const float Radius{std::isfinite(mRadius) ? std::max(mRadius, 0.001f) : 0.001f};

        if (mWindType == Engine::EWindSourceType::Point) {
            for (const FVector& Axis : {Forward, -Forward, Right, -Right, Up, -Up}) {
                DrawArrow(Draw, Origin + Axis * (Radius * 0.15f), Axis, Radius * 0.35f, Color);
            }

            return;
        }

        // 원뿔: 축 방향 화살표와 사거리 끝의 원뿔 가장자리 네 줄을 그린다.
        const float HalfAngle{std::clamp(mConeHalfAngle, 1.0f, 89.0f) * std::numbers::pi_v<float> / 180.0f};
        const float EndRadius{std::tan(HalfAngle) * Radius};
        const FVector EndCenter{Origin + Forward * Radius};

        DrawArrow(Draw, Origin, Forward, Radius * 0.6f, Color);

        for (const FVector& Edge : {Right, -Right, Up, -Up}) {
            Draw.DrawLine(Origin, EndCenter + Edge * EndRadius, Color, false, 1.5f);
        }
    }

}
