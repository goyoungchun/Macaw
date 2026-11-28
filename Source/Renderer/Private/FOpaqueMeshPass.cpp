/**
 * @file FOpaqueMeshPass.cpp
 * @brief 보이는 메시의 표면과 깊이를 G-buffer에 기록하는 메시 패스입니다.
 * @ingroup Renderer
 */

#include "FOpaqueMeshPass.h"
#include "Asset/USkeletalMesh.h"

#include "FScenePrimitive.h"
#include "Renderer/Renderer.h"
#include "Asset/UMaterial.h"
#include "Asset/UStaticMesh.h"
#include "Engine/FSceneViewFamily.h"

namespace Renderer {

    /**
     * @brief 표면과 깊이를 기록하는 기본 패스 종류를 반환합니다.
     */
    Renderer::EMeshPass Renderer::FOpaqueMeshPass::GetType() const {
        return Renderer::EMeshPass::OpaqueMeshPass;
    }

    /**
     * @brief 머티리얼이 없거나 준비에 실패한 SubMesh가 쓸 기본 머티리얼과 파이프라인을 준비합니다.
     */
    bool Renderer::FOpaqueMeshPass::BeginCommands(const Engine::FSceneViewFamily& Family) {
        mDefaultMaterial = mRenderer->GetDefaultMaterial();

        Renderer::FMaterialPipeline* Pipeline{mDefaultMaterial != nullptr ? mRenderer->GetMaterialPipeline(*mDefaultMaterial, Family.mViewMode, false, Family.mShowFlags.mShadows) : nullptr};

        return Pipeline != nullptr && mDefaultMaterial->CreateResources(mDevice, mContext);
    }

    /**
     * @brief 프리미티브 표시 플래그가 켜져 있으면 모든 메시 프리미티브를 대상으로 합니다.
     */
    bool Renderer::FOpaqueMeshPass::Includes(const Renderer::FScenePrimitive&, const Engine::FSceneViewFamily& Family) const {
        return Family.mShowFlags.mPrimitives;
    }

    /**
     * @brief SubMesh마다 덮어쓰기·슬롯·기본 머티리얼 순으로 머티리얼을 정해 해당 파이프라인 드로를 추가합니다.
     */
    void Renderer::FOpaqueMeshPass::AddCommands(const Renderer::FScenePrimitive& Primitive, Asset::UStaticMesh& Mesh, const Engine::FSceneViewFamily& Family, Core::TArray<Renderer::FMeshDrawCommand>& Commands, const Engine::FParticleMeshBatch* ParticleBatch) {
        const bool Skinned{ParticleBatch == nullptr && Primitive.mProxy.mSkeletalMesh != nullptr};
        const bool ReceiveShadows{Family.mShowFlags.mShadows && (ParticleBatch != nullptr || Primitive.mProxy.mReceiveShadow)};

		if (ParticleBatch == nullptr && Primitive.mProxy.mLandscape.has_value() && Primitive.mProxy.mLandscape->mShared != nullptr) {
			const Engine::FLandscapeComponentRenderData& Comp{*Primitive.mProxy.mLandscape};
			const Engine::FLandscapeSharedRenderData& Shared{*Comp.mShared};
			Asset::UMaterial* Material{ResolveMaterial(Primitive, 0u, mDefaultMaterial.Get(), nullptr)};

			if (Material == nullptr || Material->GetBlendMode() != Asset::EBlendMode::Opaque) {
				return;
			}

			if (!Material->CreateResources(mDevice, mContext)) {
				Material = mDefaultMaterial.Get();
			}

			Renderer::FMaterialPipeline* Pipeline{mRenderer->GetMaterialPipeline(*Material, Family.mViewMode, false, ReceiveShadows, false, false, true)};

			if (Pipeline == nullptr) {
				Material = mDefaultMaterial.Get();
				Pipeline = mRenderer->GetMaterialPipeline(*Material, Family.mViewMode, false, ReceiveShadows, false, false, true);
			}

			if (Pipeline == nullptr || Shared.mLODRanges.empty()) {
				return;
			}

			for (Uint32 SubY{}; SubY < Shared.mNumSubsections; ++SubY) {
				for (Uint32 SubX{}; SubX < Shared.mNumSubsections; ++SubX) {
					const Index_t SubLocalIndex{static_cast<Index_t>(SubY) * Shared.mNumSubsections + SubX};

					if (SubLocalIndex < Comp.mSubsectionBounds.size() && !IsVisible(mCurrentViewProjection, Comp.mSubsectionBounds[SubLocalIndex])) {
						continue;
					}

					const Uint32 GlobalSubX{Comp.mComponentIndexX * Shared.mNumSubsections + SubX};
					const Uint32 GlobalSubY{Comp.mComponentIndexY * Shared.mNumSubsections + SubY};
					const Renderer::FLandscapeSectionLODState SectionState{mLandscapeLODSolver.GetSectionState(Shared.mLandscapeId, GlobalSubX, GlobalSubY)};
					const Uint32 ClampedLOD{SectionState.mLOD < Shared.mLODRanges.size() ? SectionState.mLOD : static_cast<Uint32>(Shared.mLODRanges.size() - 1u)};
					const Asset::FLandscapeLODSectionRange& Range{Shared.mLODRanges[ClampedLOD]};

					Renderer::FMeshDrawCommand DrawCommand{Pipeline, Material, &Mesh, &Primitive, Range.mFirstIndex, Range.mIndexCount};
					DrawCommand.SetLandscapeDrawInfo({true, SubX, SubY, ClampedLOD, SectionState.mMorphFactor, SectionState.mStitchMask});
					Commands.push_back(DrawCommand);
				}
			}

			return;
		}

        const auto& SubMeshes{Mesh.GetMeshData().GetSubMeshes()};

        for (Index_t SubMeshIndex{}; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Asset::FSubMesh& SubMesh{SubMeshes[SubMeshIndex]};
            Asset::UMaterial* Material{ResolveMaterial(Primitive, SubMesh.GetMaterialIndex(), mDefaultMaterial.Get(), ParticleBatch)};

            if (Material->GetBlendMode() != Asset::EBlendMode::Opaque) {
                continue;
            }

            if (!Material->CreateResources(mDevice, mContext)) {
                Material = mDefaultMaterial;
            }

            // 천 섹션은 정점을 입자 위치로 옮기는 천 변형 셰이더로 그립니다.
            const Index_t ClothAssetIndex{GetClothAssetIndex(Primitive, SubMeshIndex)};
            const bool Cloth{ClothAssetIndex != InvalidIndex};
            Renderer::FMaterialPipeline* Pipeline{mRenderer->GetMaterialPipeline(*Material, Family.mViewMode, Skinned, ReceiveShadows, Cloth, ParticleBatch != nullptr)};

            if (Pipeline == nullptr) {
                // 실패한 스킨 셰이더를 정적 파이프라인으로 대신 그리면 바인드 포즈가 노출됩니다.
                Material = mDefaultMaterial;
                Pipeline = mRenderer->GetMaterialPipeline(*Material, Family.mViewMode, Skinned, ReceiveShadows, Cloth, ParticleBatch != nullptr);
            }

            if (Pipeline != nullptr) {
                Commands.emplace_back(Pipeline, Material, &Mesh, &Primitive, SubMesh.GetFirstIndex(), SubMesh.GetIndexCount(), ClothAssetIndex);
            }
        }
    }

}
