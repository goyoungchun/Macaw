/**
 * @file PrimitiveMesh.cpp
 * @brief Plane, Cube와 Sphere 기본 메시의 생성 및 캐시 조회를 제공합니다.
 * @ingroup Engine
 */

#include "Core/Types.h"
#include "Engine/PrimitiveMesh.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector2.h"
#include "Asset/FMeshData.h"
#include "Engine/EPrimitiveType.h"
#include "Core/Literals.h"

#include <cmath>

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief 프리미티브 종류의 표시 이름을 반환합니다.
     */
    Core::FString PrimitiveName(Engine::EPrimitiveType Type) {
        switch (Type) {
        case Engine::EPrimitiveType::Plane:
            return "Plane";

        case Engine::EPrimitiveType::Cube:
            return "Cube";

        case Engine::EPrimitiveType::Sphere:
            return "Sphere";

        default:
            return "Unknown";
        }
    }

    /**
     * @brief 기본 프리미티브의 CPU 정점, 삼각형 인덱스와 경계 상자를 생성합니다.
     */
    Asset::FMeshData BuildPrimitive(Engine::EPrimitiveType Type) {
        constexpr float HalfSize{(0.5_m).GetBase()};
        constexpr float PlaneHalfThickness{(0.001_m).GetBase()};

        Asset::FMeshData Mesh{};
        Core::TArray<FVector>& Positions{Mesh.GetPositions()};
        Core::TArray<FVector>& Normals{Mesh.GetNormals()};
        Core::TArray<FVector4>& Tangents{Mesh.GetTangents()};
        Core::TArray<Uint32>& Indices{Mesh.GetIndices()};

        Mesh.GetUVChannels().resize(1);

        Core::TArray<FVector2>& UVs{Mesh.GetUVChannels().front()};

        if (Type == Engine::EPrimitiveType::Plane) {
            Positions = {{-HalfSize, -HalfSize, 0.0f}, {HalfSize, -HalfSize, 0.0f}, {HalfSize, HalfSize, 0.0f}, {-HalfSize, HalfSize, 0.0f}};

            Normals.assign(4, {0.0f, 0.0f, 1.0f});
            Tangents.assign(4, {1.0f, 0.0f, 0.0f, 1.0f});

            UVs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
            Indices = {0, 1, 2, 0, 2, 3};

            Mesh.SetBounds({{-HalfSize, -HalfSize, -PlaneHalfThickness}, {HalfSize, HalfSize, PlaneHalfThickness}});
        } else if (Type == Engine::EPrimitiveType::Cube) {
            const Core::TStaticArray<FVector, 6> FaceNormals{{{1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}};

            for (const FVector& Normal : FaceNormals) {
                const FVector Up{std::abs(Normal.z) > 0.5f ? FVector{0.0f, 1.0f, 0.0f} : FVector{0.0f, 0.0f, 1.0f}};
                const FVector Right{Up.Cross(Normal)};
                const Uint32 Base{static_cast<Uint32>(Positions.size())};

                Positions.insert(Positions.end(), {(Normal - Right - Up) * HalfSize, (Normal + Right - Up) * HalfSize, (Normal + Right + Up) * HalfSize, (Normal - Right + Up) * HalfSize});
                Normals.insert(Normals.end(), 4, Normal);
                Tangents.insert(Tangents.end(), 4, FVector4{Right, 1.0f});
                UVs.insert(UVs.end(), {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}});

                Indices.insert(Indices.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
            }

            Mesh.SetBounds({{-HalfSize, -HalfSize, -HalfSize}, {HalfSize, HalfSize, HalfSize}});
        } else if (Type == Engine::EPrimitiveType::Sphere) {
            constexpr Uint32 Rings{20};
            constexpr Uint32 Segments{32};

            Core::TStaticArray<FVector2, Segments + 1> Circle{};

            for (Index_t Segment{}; Segment < Segments; ++Segment) {
                Circle[Segment] = FVector2{1.0f, 0.0f}.Rotated(1_turn * Segment / Segments);
            }

            Circle.back() = Circle.front();

            Positions.reserve((Rings + 1) * (Segments + 1));
            Normals.reserve((Rings + 1) * (Segments + 1));
            Tangents.reserve((Rings + 1) * (Segments + 1));
            UVs.reserve((Rings + 1) * (Segments + 1));

            for (Index_t Ring{}; Ring <= Rings; ++Ring) {
                const Core::FAngle Phi{0.5_turn * Ring / Rings};
                const float Radius{Core::Math::Sin(Phi)};
                const float Height{Core::Math::Cos(Phi)};

                for (Index_t Segment{}; Segment <= Segments; ++Segment) {
                    const FVector2 Radial{Circle[Segment] * Radius};
                    const FVector Normal{Radial.x, Radial.y, Height};

                    Positions.push_back(Normal * HalfSize);
                    Normals.push_back(Normal);
                    Tangents.push_back({-Circle[Segment].y, Circle[Segment].x, 0.0f, -1.0f});
                    UVs.push_back({static_cast<float>(Segment) / Segments, static_cast<float>(Ring) / Rings});
                }
            }

            for (Index_t Ring{}; Ring < Rings; ++Ring) {
                for (Index_t Segment{}; Segment < Segments; ++Segment) {
                    const Uint32 A{static_cast<Uint32>(Ring * (Segments + 1) + Segment)};
                    const Uint32 B{static_cast<Uint32>(A + Segments + 1)};

                    Indices.insert(Indices.end(), {A, B, A + 1, A + 1, B, B + 1});
                }
            }

            Mesh.SetBounds({{-HalfSize, -HalfSize, -HalfSize}, {HalfSize, HalfSize, HalfSize}});
        }

        Mesh.GetColors().assign(Positions.size(), {1.0f, 1.0f, 1.0f, 1.0f});

        return Mesh;
    }

    /**
     * @brief 종류별로 캐시된 읽기 전용 기본 메시를 반환합니다.
     */
    const Asset::FMeshData& GetPrimitiveMesh(Engine::EPrimitiveType Type) {
        static const Core::TStaticArray<Asset::FMeshData, 3> Meshes{Engine::BuildPrimitive(Engine::EPrimitiveType::Plane), Engine::BuildPrimitive(Engine::EPrimitiveType::Cube), Engine::BuildPrimitive(Engine::EPrimitiveType::Sphere)};

        return Meshes[static_cast<Index_t>(Type)];
    }

}
