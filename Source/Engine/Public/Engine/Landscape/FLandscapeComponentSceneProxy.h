#pragma once

#include "Engine/FPrimitiveSceneProxy.h"

namespace Asset {
    class UTexture2D;
}

namespace Engine {
    class FLandscapeRenderResources {
    public:
        FLandscapeRenderResources(std::unique_ptr<Asset::UStaticMesh> Mesh, std::unique_ptr<Asset::UTexture2D> Heightmap);
        ~FLandscapeRenderResources();

    public:
        static std::shared_ptr<const Engine::FLandscapeRenderResources> Acquire(Uint32 Quads);

        Asset::UStaticMesh* GetMesh() const;
        Asset::UTexture2D* GetHeightmap() const;

    private:
        std::unique_ptr<Asset::UStaticMesh> mMesh{};
        std::unique_ptr<Asset::UTexture2D> mHeightmap{};
    };

    class FLandscapeComponentSceneProxy final : public Engine::FPrimitiveSceneProxy {
    public:
        explicit FLandscapeComponentSceneProxy(std::shared_ptr<const Engine::FLandscapeRenderResources> Resources);

    public:
        Engine::EVertexFactoryType GetVertexFactoryType() const override;
        Asset::UTexture2D* GetHeightmap() const;

    private:
        std::shared_ptr<const Engine::FLandscapeRenderResources> mResources{};
    };
}
