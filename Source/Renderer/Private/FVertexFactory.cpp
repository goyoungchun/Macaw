#include "FVertexFactory.h"
#include "FScenePrimitive.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/UTexture2D.h"
#include "Engine/FSkeletalMeshSceneProxy.h"
#include "Engine/Landscape/FLandscapeComponentSceneProxy.h"

#include <d3d11.h>

using namespace Renderer;

namespace {
    class FStaticMeshVertexFactory final : public Renderer::FVertexFactory {
    public:
        FStaticMeshVertexFactory();
    };

    class FSkeletalMeshVertexFactory final : public Renderer::FVertexFactory {
    public:
        FSkeletalMeshVertexFactory();

    public:
        bool BindResources(ID3D11DeviceContext* Context, const Renderer::FScenePrimitive& Primitive, Index_t ClothAssetIndex) const override;
        void UnbindResources(ID3D11DeviceContext* Context) const override;
    };

    class FLandscapeVertexFactory final : public Renderer::FVertexFactory {
    public:
        FLandscapeVertexFactory();

    public:
        bool CreateResources(ID3D11Device* Device, const Engine::FPrimitiveSceneProxy& Proxy) const override;
        bool BindResources(ID3D11DeviceContext* Context, const Renderer::FScenePrimitive& Primitive, Index_t ClothAssetIndex) const override;
        void UnbindResources(ID3D11DeviceContext* Context) const override;

    private:
        static constexpr UINT mHeightmapSlot{13};
    };

    FStaticMeshVertexFactory::FStaticMeshVertexFactory()
        : FVertexFactory{"StaticMesh.vs.hlsl", "Depth.hlsl", true, false} {
    }

    FSkeletalMeshVertexFactory::FSkeletalMeshVertexFactory()
        : FVertexFactory{"SkinnedMesh.vs.hlsl", "Depth.hlsl", false, true} {
    }

    bool FSkeletalMeshVertexFactory::BindResources(ID3D11DeviceContext* Context, const FScenePrimitive& Primitive, Index_t ClothAssetIndex) const {
        const auto* Skinned{Engine::FSkeletalMeshSceneProxy::FindSkinnedProxy(*Primitive.mProxy)};
        const Asset::USkeletalMesh* SkeletalMesh{Skinned != nullptr ? Skinned->GetSkeletalMesh() : nullptr};
        if (SkeletalMesh == nullptr) {
            return false;
        }

        // 같은 형상을 공유해도 객체마다 본 팔레트를 다시 바인딩합니다.
        // 가중치 정점 버퍼는 5번 입력 슬롯, 본 팔레트는 정점 셰이더의 t10(Skinning.hlsl의 SkinBones)에 연결합니다.
        SkeletalMesh->BindSkinWeights(Context, 5);
        Primitive.mSkinBuffer.Bind(Context, RenderCore::EShaderStage::Vertex, 10);

        // 천 섹션은 정점별 입자 삼각형 정보(t11), 입자 위치와 법선(t12), 천 상수(b5)를 Cloth.hlsl에 연결합니다.
        if (ClothAssetIndex != InvalidIndex) {
            const auto Resources{Primitive.mClothResources.find(ClothAssetIndex)};
            if (Resources != Primitive.mClothResources.end()) {
                SkeletalMesh->BindClothVertices(Context, 11);
                Resources->second.mParticles.Bind(Context, RenderCore::EShaderStage::Vertex, 12);
                Resources->second.mConstants.Bind(Context, RenderCore::EShaderStage::Vertex, 5);
            }
        }

        return true;
    }

    void FSkeletalMeshVertexFactory::UnbindResources(ID3D11DeviceContext* Context) const {
        Asset::USkeletalMesh::UnbindSkinWeights(Context, 5);
        FScenePrimitive::FSkinBuffer::Unbind(Context, RenderCore::EShaderStage::Vertex, 10);
        Asset::USkeletalMesh::UnbindClothVertices(Context, 11);
        FScenePrimitive::FClothParticleBuffer::Unbind(Context, RenderCore::EShaderStage::Vertex, 12);
        RenderCore::TConstantBuffer<Renderer::FClothConstants>::Unbind(Context, RenderCore::EShaderStage::Vertex, 5);
    }

    // 높이맵을 타일별로 바인딩할 수 있도록 Landscape는 개별 드로를 사용합니다.
    FLandscapeVertexFactory::FLandscapeVertexFactory()
        : FVertexFactory{"Landscape.vs.hlsl", "LandscapeDepth.vs.hlsl", false, false} {
    }

    bool FLandscapeVertexFactory::CreateResources(ID3D11Device* Device, const Engine::FPrimitiveSceneProxy& Proxy) const {
        return static_cast<const Engine::FLandscapeComponentSceneProxy&>(Proxy).GetHeightmap()->CreateResources(Device);
    }

    bool FLandscapeVertexFactory::BindResources(ID3D11DeviceContext* Context, const FScenePrimitive& Primitive, Index_t) const {
        // 표면과 그림자 셰이더에 같은 슬롯을 바인딩합니다. 현재 두 셰이더 모두 입력 위치를 그대로 사용합니다.
        ID3D11ShaderResourceView* View{static_cast<const Engine::FLandscapeComponentSceneProxy&>(*Primitive.mProxy).GetHeightmap()->GetShaderResourceView()};
        Context->VSSetShaderResources(mHeightmapSlot, 1, &View);

        return true;
    }

    void FLandscapeVertexFactory::UnbindResources(ID3D11DeviceContext* Context) const {
        ID3D11ShaderResourceView* Empty{};
        Context->VSSetShaderResources(mHeightmapSlot, 1, &Empty);
    }
}

FVertexFactory::FVertexFactory(const char* SurfaceShader, const char* DepthShader, bool Instanced, bool Skinned)
    : mSurfaceShader{SurfaceShader},
      mDepthShader{DepthShader},
      mInstanced{Instanced},
      mSkinned{Skinned} {
}

FVertexFactory::~FVertexFactory() = default;

const FVertexFactory& FVertexFactory::Get(Engine::EVertexFactoryType Type) {
    static const FStaticMeshVertexFactory Static{};
    static const FSkeletalMeshVertexFactory Skinned{};
    static const FVertexFactory Particle{"ParticleMesh.vs.hlsl", "Depth.hlsl", true, false};
    static const FLandscapeVertexFactory Landscape{};

    switch (Type) {
    case Engine::EVertexFactoryType::Skinned:
        return Skinned;
    case Engine::EVertexFactoryType::Particle:
        return Particle;
    case Engine::EVertexFactoryType::Landscape:
        return Landscape;
    default:
        return Static;
    }
}

const char* FVertexFactory::GetSurfaceShader() const {
    return mSurfaceShader;
}

const char* FVertexFactory::GetDepthShader() const {
    return mDepthShader;
}

bool FVertexFactory::IsInstanced() const {
    return mInstanced;
}

bool FVertexFactory::IsSkinned() const {
    return mSkinned;
}

bool FVertexFactory::CreateResources(ID3D11Device*, const Engine::FPrimitiveSceneProxy&) const {
    return true;
}

bool FVertexFactory::BindResources(ID3D11DeviceContext*, const FScenePrimitive&, Index_t) const {
    return true;
}

void FVertexFactory::UnbindResources(ID3D11DeviceContext*) const {
}
