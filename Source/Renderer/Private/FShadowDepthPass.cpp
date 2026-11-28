/**
 * @file FShadowDepthPass.cpp
 * @brief 그림자를 드리우는 메시를 깊이 전용 파이프라인으로 아틀라스 타일에 기록하는 메시 패스입니다.
 * @ingroup Renderer
 */

#include "Core/Types.h"
#include "Asset/USkeletalMesh.h"
#include "FShadowDepthPass.h"

#include "FScenePrimitive.h"
#include "Renderer/Renderer.h"
#include "Asset/UStaticMesh.h"
#include "Asset/UMaterial.h"

#include <utility>

namespace Renderer {

    bool Renderer::FShadowDepthPass::Initialize() {
        decltype(mRasterizers) Rasterizers{};
        D3D11_RASTERIZER_DESC Description{};
        Description.FillMode = D3D11_FILL_SOLID;
        Description.CullMode = D3D11_CULL_NONE;
        Description.DepthClipEnable = true;
        Description.ScissorEnable = true;
        Description.DepthBiasClamp = 0.005f;

        for (Index_t DepthLevel{}; DepthLevel < Rasterizers.size(); ++DepthLevel) {
            Description.DepthBias = static_cast<INT>(DepthLevel) * Engine::ShadowDepthBiasStep;
            auto& SlopeRasterizers{Rasterizers[DepthLevel]};

            for (Index_t SlopeLevel{}; SlopeLevel < SlopeRasterizers.size(); ++SlopeLevel) {
                Description.SlopeScaledDepthBias = static_cast<float>(SlopeLevel) * Engine::ShadowSlopeBiasStep;

                if (FAILED(mDevice->CreateRasterizerState(&Description, SlopeRasterizers[SlopeLevel].GetAddressOf()))) {
                    return false;
                }
            }
        }

        mRasterizers = std::move(Rasterizers);

        return true;
    }

    ID3D11RasterizerState* Renderer::FShadowDepthPass::GetRasterizer(Engine::EShadowBiasPreset DepthPreset, Engine::EShadowBiasPreset SlopePreset) const {
        return mRasterizers[static_cast<Uint32>(DepthPreset)][static_cast<Uint32>(SlopePreset)].Get();
    }

    /**
     * @brief 깊이 전용 입력 레이아웃을 쓰는 그림자 깊이 패스 종류를 반환합니다.
     */
    Renderer::EMeshPass Renderer::FShadowDepthPass::GetType() const {
        return Renderer::EMeshPass::ShadowDepth;
    }

    /**
     * @brief 모든 드로가 공유할 깊이 전용 파이프라인을 조회합니다.
     */
    bool Renderer::FShadowDepthPass::BeginCommands(const Engine::FSceneViewFamily&) {
        mPipeline = mRenderer->GetDepthPipeline();

        return mPipeline != nullptr;
    }

    /**
     * @brief 그림자를 드리우는 프리미티브만 대상으로 합니다.
     */
    bool Renderer::FShadowDepthPass::Includes(const Renderer::FScenePrimitive& Primitive, const Engine::FSceneViewFamily&) const {
        return Primitive.mProxy.mCastShadow;
    }

    /**
     * @brief SubMesh마다 머티리얼 없이 깊이 전용 파이프라인 드로를 추가합니다.
     */
    void Renderer::FShadowDepthPass::AddCommands(const Renderer::FScenePrimitive& Primitive, Asset::UStaticMesh& Mesh, const Engine::FSceneViewFamily&, Core::TArray<Renderer::FMeshDrawCommand>& Commands, const Engine::FParticleMeshBatch*) {
		if (Primitive.mProxy.mLandscape.has_value() && Primitive.mProxy.mLandscape->mShared != nullptr) {
			const Engine::FLandscapeComponentRenderData& Comp{*Primitive.mProxy.mLandscape};
			const Engine::FLandscapeSharedRenderData& Shared{*Comp.mShared};
			Renderer::FMaterialPipeline* LandscapePipeline{mRenderer->GetDepthPipeline(false, false, true)};

			if (LandscapePipeline == nullptr || Shared.mLODRanges.empty()) {
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

					Renderer::FMeshDrawCommand DrawCommand{LandscapePipeline, nullptr, &Mesh, &Primitive, Range.mFirstIndex, Range.mIndexCount};
					DrawCommand.SetLandscapeDrawInfo({true, SubX, SubY, ClampedLOD, SectionState.mMorphFactor, SectionState.mStitchMask});
					Commands.push_back(DrawCommand);
				}
			}

			return;
		}

        const bool Skinned{Primitive.mProxy.mSkeletalMesh != nullptr};
        Renderer::FMaterialPipeline* SurfacePipeline{Skinned ? mRenderer->GetDepthPipeline(true) : mPipeline};

        if (SurfacePipeline == nullptr) {
            return;
        }

        const auto& SubMeshes{Mesh.GetMeshData().GetSubMeshes()};

        for (Index_t SubMeshIndex{}; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Asset::FSubMesh& SubMesh{SubMeshes[SubMeshIndex]};
            Asset::UMaterial* Material{ResolveMaterial(Primitive, SubMesh.GetMaterialIndex(), nullptr, nullptr)};

            if (Material != nullptr && Material->GetBlendMode() != Asset::EBlendMode::Opaque) {
                continue;
            }

            // 천 섹션은 그림자도 입자 위치로 옮긴 모양으로 그립니다.
            const Index_t ClothAssetIndex{GetClothAssetIndex(Primitive, SubMeshIndex)};
            Renderer::FMaterialPipeline* Pipeline{ClothAssetIndex != InvalidIndex ? mRenderer->GetDepthPipeline(true, true) : SurfacePipeline};

            if (Pipeline == nullptr) {
                continue;
            }

            Commands.emplace_back(Pipeline, nullptr, &Mesh, &Primitive, SubMesh.GetFirstIndex(), SubMesh.GetIndexCount(), ClothAssetIndex);
        }
    }

}
