/**
 * @file FPrimitiveDrawInterface.cpp
 * @brief 에디터 보조 선분을 프레임 단위로 수집합니다.
 * @ingroup Engine
 */

#include "Core/Types.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Core/Math/FVector.h"
#include "Engine/FDrawLine.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FQuat.h"
#include "Core/Literals.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief 보조 도형을 선분으로 나누는 고정 설정입니다.
     */
    namespace {
        /**
         * @brief 원호 한 바퀴를 나눌 선분 개수입니다.
         */
        constexpr Int32 ArcSegmentsPerTurn{32};
    }

    /**
     * @brief 주어진 좌표와 색상으로 보조 선분을 프레임 목록에 추가합니다.
     */
    void Engine::FPrimitiveDrawInterface::DrawLine(const FVector& Start, const FVector& End, const FVector& Color, bool DepthTest, float Thickness, float Opacity) {
        if (!Start.IsFinite() || !End.IsFinite() || !Color.IsFinite() || !std::isfinite(Thickness) || Thickness <= 0.0f || !std::isfinite(Opacity) || Opacity <= 0.0f || (End - Start).LengthSquared() <= 0.0f) {
            return;
        }

        mLines.push_back(Engine::FDrawLine{Start, End, Color, DepthTest, Thickness, std::min(Opacity, 1.0f)});
    }

    /**
     * @brief AABB의 열두 모서리를 지정한 픽셀 두께의 선분으로 수집합니다.
     */
    void Engine::FPrimitiveDrawInterface::DrawBox(const FBox& Bounds, const FVector& Color, bool DepthTest, float Thickness) {
        for (Index_t Corner{}; Corner < 8; ++Corner) {
            const FVector Start{(Corner & 1) != 0 ? Bounds.mMaximum.x : Bounds.mMinimum.x, (Corner & 2) != 0 ? Bounds.mMaximum.y : Bounds.mMinimum.y, (Corner & 4) != 0 ? Bounds.mMaximum.z : Bounds.mMinimum.z};

            for (Index_t Axis{}; Axis < 3; ++Axis) {
                const Index_t Mask{Index_t{1} << Axis};

                if ((Corner & Mask) != 0) {
                    continue;
                }

                const Index_t Other{Corner | Mask};
                const FVector End{(Other & 1) != 0 ? Bounds.mMaximum.x : Bounds.mMinimum.x, (Other & 2) != 0 ? Bounds.mMaximum.y : Bounds.mMinimum.y, (Other & 4) != 0 ? Bounds.mMaximum.z : Bounds.mMinimum.z};

                DrawLine(Start, End, Color, DepthTest, Thickness);
            }
        }
    }

    /**
     * @brief 회전한 상자의 열두 모서리를 선분으로 수집합니다.
     */
    void Engine::FPrimitiveDrawInterface::DrawOrientedBox(const FVector& Center, const FQuat& Rotation, const FVector& HalfExtent, const FVector& Color, bool DepthTest, float Thickness, float Opacity) {
        const auto Corner{[&Center, &Rotation, &HalfExtent](Index_t Index) {
            const FVector Local{(Index & 1) != 0 ? HalfExtent.x : -HalfExtent.x, (Index & 2) != 0 ? HalfExtent.y : -HalfExtent.y, (Index & 4) != 0 ? HalfExtent.z : -HalfExtent.z};

            return Center + Rotation.RotateVector(Local);
        }};

        for (Index_t Index{}; Index < 8; ++Index) {
            for (Index_t Mask{1}; Mask < 8; Mask <<= 1) {
                if ((Index & Mask) == 0) {
                    DrawLine(Corner(Index), Corner(Index | Mask), Color, DepthTest, Thickness, Opacity);
                }
            }
        }
    }

    /**
     * @brief 두 기저 방향이 이루는 평면에서 원호를 선분으로 수집합니다.
     */
    void Engine::FPrimitiveDrawInterface::DrawArc(const FVector& Center, const FVector& AxisU, const FVector& AxisV, Core::FLength Radius, Core::FAngle StartAngle, Core::FAngle EndAngle, const FVector& Color, bool DepthTest, float Thickness, float Opacity) {
        const Int32 Segments{std::max(1, static_cast<Int32>(ArcSegmentsPerTurn * ((EndAngle - StartAngle) / 1_turn)))};
        const Core::FAngle Step{(EndAngle - StartAngle) / Segments};
        FVector Previous{Center + AxisU * (Core::Math::Cos(StartAngle) * Radius.GetBase()) + AxisV * (Core::Math::Sin(StartAngle) * Radius.GetBase())};

        for (Int32 Index{1}; Index <= Segments; ++Index) {
            const Core::FAngle Angle{StartAngle + Step * Index};
            const FVector Point{Center + AxisU * (Core::Math::Cos(Angle) * Radius.GetBase()) + AxisV * (Core::Math::Sin(Angle) * Radius.GetBase())};

            DrawLine(Previous, Point, Color, DepthTest, Thickness, Opacity);

            Previous = Point;
        }
    }

    /**
     * @brief 현재 프레임의 보조 선분 목록을 비웁니다.
     */
    void Engine::FPrimitiveDrawInterface::Clear() {
        mLines.clear();
        mMeshes.clear();
        mSprites.clear();
        mTexts.clear();
    }

    /**
     * @brief 현재 프레임에 수집한 보조 선분을 반환합니다.
     */
    const Core::TArray<Engine::FDrawLine>& Engine::FPrimitiveDrawInterface::GetLines() const {
        return mLines;
    }

    /**
     * @brief 깊이 검사와 우선순위를 가진 에디터 메시 그리기 요청을 추가합니다.
     */
    void Engine::FPrimitiveDrawInterface::DrawMesh(const Engine::FDrawMesh& Mesh) {
        mMeshes.push_back(Mesh);
    }

    /**
     * @brief 이번 프레임의 에디터 메시 그리기 요청들을 반환합니다.
     */
    const Core::TArray<Engine::FDrawMesh>& Engine::FPrimitiveDrawInterface::GetMeshes() const {
        return mMeshes;
    }

    void Engine::FPrimitiveDrawInterface::DrawSprite(const Engine::FPrimitiveSceneProxy& Sprite) {
        mSprites.push_back(Sprite);
    }

    const Core::TArray<Engine::FPrimitiveSceneProxy>& Engine::FPrimitiveDrawInterface::GetSprites() const {
        return mSprites;
    }

    /**
     * @brief 이번 프레임에 표시할 텍스트 요청을 수집합니다.
     */
    void Engine::FPrimitiveDrawInterface::DrawString(const Engine::FDrawText& Text) {
        mTexts.push_back(Text);
    }

    /**
     * @brief 현재 프레임의 텍스트 요청 목록을 반환합니다.
     */
    const Core::TArray<Engine::FDrawText>& Engine::FPrimitiveDrawInterface::GetTexts() const {
        return mTexts;
    }

}
