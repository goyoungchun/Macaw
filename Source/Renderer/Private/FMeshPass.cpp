/**
 * @file FMeshPass.cpp
 * @brief 장면 프리미티브를 선별해 정렬된 메시 드로 목록을 만드는 메시 패스의 기반 클래스입니다.
 * @ingroup Renderer
 */

#include "Core/Types.h"
#include "FMeshPass.h"

#include "FScene.h"
#include "FScenePrimitive.h"
#include "Asset/UMaterial.h"
#include "Asset/UStaticMesh.h"
#include "Asset/USkeletalMesh.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector4.h"
#include "Engine/FSceneViewFamily.h"
#include "Engine/Particles/FParticleRenderData.h"

#include <algorithm>
#include <functional>

namespace Renderer {

    /**
     * @brief 파이프라인 조회와 메시 자원 생성에 사용할 렌더러 의존성을 보관합니다.
     */
    Renderer::FMeshPass::FMeshPass(Renderer::FRenderer& Renderer, ID3D11Device* Device, ID3D11DeviceContext* Context, const Core::TStaticArray<std::unique_ptr<Asset::UStaticMesh>, 3>& Meshes)
		: mRenderer{&Renderer},
		  mDevice{Device},
		  mContext{Context},
		  mMeshes{&Meshes} {
    }

    /**
     * @brief 객체 수명을 끝냅니다.
     */
    Renderer::FMeshPass::~FMeshPass() = default;

    /**
     * @brief 경계 상자의 8개 꼭짓점이 모두 같은 클립 평면 밖에 있지 않은지 검사합니다.
     */
    bool Renderer::FMeshPass::IsVisible(const FMatrix& ViewProjection, const FBox& Bounds) {
        Core::TStaticArray<Uint32, 6> Outside{};

        for (Index_t Index{}; Index < 8; ++Index) {
            const FVector Point{(Index & 1) ? Bounds.mMaximum.x : Bounds.mMinimum.x, (Index & 2) ? Bounds.mMaximum.y : Bounds.mMinimum.y, (Index & 4) ? Bounds.mMaximum.z : Bounds.mMinimum.z};
            const FVector4 Clip{FVector4{Point, 1.0f} * ViewProjection};

            Outside[0] += Clip.x < -Clip.w;
            Outside[1] += Clip.x > Clip.w;
            Outside[2] += Clip.y < -Clip.w;
            Outside[3] += Clip.y > Clip.w;
            Outside[4] += Clip.z < 0.0f;
            Outside[5] += Clip.z > Clip.w;
        }

        return std::none_of(Outside.begin(), Outside.end(), [](Uint32 Count) {
            return Count == 8;
        });
    }

    /**
     * @brief SubMesh가 이번 프레임에 천 결과로 그려야 하는 천 섹션인지 검사합니다.
     */
    Index_t Renderer::FMeshPass::GetClothAssetIndex(const Renderer::FScenePrimitive& Primitive, Index_t SubMeshIndex) {
        const Asset::USkeletalMesh* SkeletalMesh{Primitive.mProxy.mSkeletalMesh.Get()};

        if (SkeletalMesh == nullptr || Primitive.mClothResources.empty() || !SkeletalMesh->HasClothVertices()) {
            return InvalidIndex;
        }

        const Asset::FClothingSectionData* SectionData{SkeletalMesh->GetSectionClothingData(SubMeshIndex)};

        if (SectionData == nullptr || !SectionData->IsValid() || !Primitive.mClothResources.contains(SectionData->mCorrespondClothAssetIndex)) {
            return InvalidIndex;
        }

        return SectionData->mCorrespondClothAssetIndex;
    }

    /**
     * @brief 패스 정책으로 프리미티브를 선별하고 파이프라인·머티리얼·메시 순으로 정렬된 드로 목록을 만듭니다.
     */
    bool Renderer::FMeshPass::BuildCommands(const Renderer::FScene& Scene, const Engine::FSceneViewFamily& Family, const FMatrix& ViewProjection, Core::TArray<Renderer::FMeshDrawCommand>& Commands, Uint32& Primitives) {
        Commands.clear();

        Primitives = 0;

        if (!BeginCommands(Family)) {
            return false;
        }

        Commands.reserve(Scene.GetPrimitives().size());

        const auto AddPrimitive{[&](const Renderer::FScenePrimitive& Primitive) {
            if (Primitive.mProxy.mText.has_value() || Primitive.mProxy.mSprite.has_value() || !Includes(Primitive, Family) || !IsVisible(ViewProjection, Primitive.mProxy.mBounds)) {
                return;
            }

            Asset::UStaticMesh* Mesh{Primitive.mProxy.mMesh != nullptr ? Primitive.mProxy.mMesh.Get() : (*mMeshes)[static_cast<Index_t>(Primitive.mProxy.mPrimitive)].get()};

            if (Primitive.mProxy.mSkeletalMesh != nullptr && (!Primitive.mSkinBuffer.IsCreated() || !Primitive.mProxy.mSkeletalMesh->CreateResources(mDevice))) {
                return;
            }

            if (Mesh == nullptr || (!Mesh->HasResources() && !Mesh->CreateResources(mDevice)) || !Primitive.mObjectBuffer.IsCreated()) {
                return;
            }

            const auto PreviousCount{Commands.size()};
            AddCommands(Primitive, *Mesh, Family, Commands, nullptr);

            if (Commands.size() != PreviousCount) {
                ++Primitives;
            }
        }};

        for (const Renderer::FScenePrimitive& Primitive : Scene.GetPrimitives()) {
            if (Primitive.mProxy.mParticles != nullptr) {
                if (GetType() != Renderer::EMeshPass::ShadowDepth && Includes(Primitive, Family)) {
                    AddParticleCommands(Primitive, Family, ViewProjection, Commands, Primitives);
                }

                continue;
            }

            AddPrimitive(Primitive);
        }

        if (GetType() == Renderer::EMeshPass::TranslucencyMeshPass) {
            return true;
        }

        std::stable_sort(Commands.begin(), Commands.end(), [](const Renderer::FMeshDrawCommand& First, const Renderer::FMeshDrawCommand& Second) {
            const std::less<const void*> Less{};

            if (First.mPipeline != Second.mPipeline) {
                return Less(First.mPipeline, Second.mPipeline);
            }

            if (First.mMaterial != Second.mMaterial) {
                return Less(First.mMaterial, Second.mMaterial);
            }

            if (First.mMesh != Second.mMesh) {
                return Less(First.mMesh, Second.mMesh);
            }

            if (First.mFirstIndex != Second.mFirstIndex) {
                return First.mFirstIndex < Second.mFirstIndex;
            }

            return First.mIndexCount < Second.mIndexCount;
        });

        return true;
    }

    Asset::UMaterial* Renderer::FMeshPass::ResolveMaterial(const Renderer::FScenePrimitive& Primitive, Index_t MaterialIndex, Asset::UMaterial* Default, const Engine::FParticleMeshBatch* ParticleBatch) {
        if (ParticleBatch == nullptr && Primitive.mProxy.mMaterial != nullptr) {
            return Primitive.mProxy.mMaterial.Get();
        }

        const auto& Slots{ParticleBatch != nullptr ? ParticleBatch->mMaterials : Primitive.mProxy.mMaterials};
        Asset::UMaterial* SlotMaterial{MaterialIndex < Slots.size() ? Slots[MaterialIndex].Get() : nullptr};

        return SlotMaterial != nullptr ? SlotMaterial : Default;
    }

    void Renderer::FMeshPass::AddParticleCommands(const Renderer::FScenePrimitive& Primitive, const Engine::FSceneViewFamily& Family, const FMatrix& ViewProjection, Core::TArray<Renderer::FMeshDrawCommand>& Commands, Uint32& Primitives) {
        Core::TArray<Renderer::FMeshDrawCommand> Sections{};
        const auto Visible{[&ViewProjection](const Engine::FParticleMeshInstance& Instance) {
            return IsVisible(ViewProjection, Instance.mBounds);
        }};

        for (const Engine::FParticleMeshBatch& Batch : Primitive.mProxy.mParticles->mMeshes) {
            Asset::UStaticMesh* Mesh{Batch.mMesh.Get()};
            const auto First{std::ranges::find_if(Batch.mInstances, Visible)};

            if (First == Batch.mInstances.end() || Mesh == nullptr || (!Mesh->HasResources() && !Mesh->CreateResources(mDevice))) {
                continue;
            }

            Sections.clear();
            AddCommands(Primitive, *Mesh, Family, Sections, &Batch);

            if (Sections.empty()) {
                continue;
            }

            for (auto Instance{First}; Instance != Batch.mInstances.end(); ++Instance) {
                if (!Visible(*Instance)) {
                    continue;
                }

                for (const Renderer::FMeshDrawCommand& Section : Sections) {
                    Commands.push_back(Section);
                    Commands.back().mParticleInstance = &*Instance;
                }

                ++Primitives;
            }
        }
    }

}
