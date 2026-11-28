#pragma once

#include "Core/Types.h"
#include "Engine/EVertexFactoryType.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace Engine {
    struct FPrimitiveSceneProxy;
}

namespace Renderer {
    struct FScenePrimitive;

    class FVertexFactory {
    public:
        FVertexFactory(const char* SurfaceShader, const char* DepthShader, bool Instanced, bool Skinned);
        virtual ~FVertexFactory();

    public:
        static const Renderer::FVertexFactory& Get(Engine::EVertexFactoryType Type);

        const char* GetSurfaceShader() const;
        const char* GetDepthShader() const;
        bool IsInstanced() const;
        bool IsSkinned() const;

        virtual bool CreateResources(ID3D11Device* Device, const Engine::FPrimitiveSceneProxy& Proxy) const;
        virtual bool BindResources(ID3D11DeviceContext* Context, const Renderer::FScenePrimitive& Primitive, Index_t ClothAssetIndex) const;
        virtual void UnbindResources(ID3D11DeviceContext* Context) const;

    private:
        const char* mSurfaceShader{};
        const char* mDepthShader{};
        bool mInstanced{};
        bool mSkinned{};
    };
}
