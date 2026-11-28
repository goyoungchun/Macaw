#include "Engine/Landscape/FLandscapeComponentSceneProxy.h"
#include "Engine/Landscape/ULandscapeComponent.h"
#include "Asset/UStaticMesh.h"
#include "Asset/UTexture2D.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <mutex>
#include <utility>

using namespace Engine;

namespace {
    Asset::FMeshData BuildGrid(Uint32 Quads) {
        constexpr float QuadSize{Engine::LandscapeQuadSize.GetBase()};
        // 격자 양 끝을 포함하므로 한 변의 정점 수는 Quad 수보다 하나 많습니다.
        const Uint32 Side{Quads + 1};
        const Uint32 Vertices{Side * Side};
        const float UVScale{1.0f / static_cast<float>(Quads)};
        Asset::FMeshData Mesh{};
        auto& Positions{Mesh.GetPositions()};
        auto& Indices{Mesh.GetIndices()};
        auto& UVs{Mesh.GetUVChannels().emplace_back()};

        Positions.reserve(Vertices);
        UVs.reserve(Vertices);
        Indices.reserve(6 * Quads * Quads);
        Mesh.GetNormals().assign(Vertices, {0.0f, 0.0f, 1.0f});
        Mesh.GetTangents().assign(Vertices, {1.0f, 0.0f, 0.0f, 1.0f});

        for (Uint32 Y{}; Y < Side; ++Y) {
            for (Uint32 X{}; X < Side; ++X) {
                Positions.emplace_back(QuadSize * X, QuadSize * Y, 0.0f);
                UVs.emplace_back(UVScale * X, UVScale * Y);
            }
        }

        // 기본 Plane과 같은 정점 순서로 각 Quad를 위쪽을 향하는 삼각형 두 개로 나눕니다.
        for (Uint32 Y{}; Y < Quads; ++Y) {
            for (Uint32 X{}; X < Quads; ++X) {
                const Uint32 Start{Y * Side + X};
                Indices.insert(Indices.end(), {Start, Start + 1, Start + Side + 1, Start, Start + Side + 1, Start + Side});
            }
        }

        return Mesh;
    }
}

FLandscapeRenderResources::FLandscapeRenderResources(std::unique_ptr<Asset::UStaticMesh> Mesh, std::unique_ptr<Asset::UTexture2D> Heightmap)
    : mMesh{std::move(Mesh)},
      mHeightmap{std::move(Heightmap)} {
}

FLandscapeRenderResources::~FLandscapeRenderResources() = default;

std::shared_ptr<const FLandscapeRenderResources> FLandscapeRenderResources::Acquire(Uint32 Quads) {
    constexpr Uint32 MaxQuads{1024};
    if (Quads == 0 || Quads > MaxQuads) {
        return {};
    }

    // 타일 위치를 별도로 적용하므로 Quad 수가 같으면 격자를 공유할 수 있습니다.
    // 캐시는 약한 참조만 보관하고 컴포넌트와 씬 프록시가 리소스의 수명을 유지합니다.
    static Core::TMap<Uint32, std::weak_ptr<const FLandscapeRenderResources>> Grids{};
    static std::mutex Mutex{};
    const std::lock_guard Lock{Mutex};
    auto& Grid{Grids[Quads]};

    if (auto Existing{Grid.lock()}; Existing != nullptr) {
        return Existing;
    }

    auto Mesh{CoreUObject::NewObject<Asset::UStaticMesh>()};
    auto Heightmap{CoreUObject::NewObject<Asset::UTexture2D>()};
    // 높이맵 슬롯을 바인딩할 수 있도록 값이 0인 1×1 텍스처를 준비합니다.
    if (!Mesh->SetMeshData(BuildGrid(Quads)) || !Heightmap->SetPixels(1, 1, Asset::ETextureFormat::R32Float, Core::TArray<Uint8>(sizeof(float), 0))) {
        return {};
    }

    auto Resources{std::make_shared<FLandscapeRenderResources>(std::move(Mesh), std::move(Heightmap))};
    Grid = Resources;

    return Resources;
}

Asset::UStaticMesh* FLandscapeRenderResources::GetMesh() const {
    return mMesh.get();
}

Asset::UTexture2D* FLandscapeRenderResources::GetHeightmap() const {
    return mHeightmap.get();
}

FLandscapeComponentSceneProxy::FLandscapeComponentSceneProxy(std::shared_ptr<const FLandscapeRenderResources> Resources)
    : mResources{std::move(Resources)} {
}

EVertexFactoryType FLandscapeComponentSceneProxy::GetVertexFactoryType() const {
    return EVertexFactoryType::Landscape;
}

Asset::UTexture2D* FLandscapeComponentSceneProxy::GetHeightmap() const {
    return mResources->GetHeightmap();
}
