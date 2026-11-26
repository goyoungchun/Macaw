/**
 * @file FTranslucencyMeshPass.cpp
 * @brief 반투명 메시·스프라이트·텍스트를 정렬하고 반투명 패스로 그립니다.
 * @ingroup Renderer
 */

#include "FTranslucencyMeshPass.h"

#include "FScene.h"
#include "FScenePrimitive.h"
#include "FSpriteRenderer.h"
#include "FParticleRenderer.h"
#include "Engine/Particles/FParticleRenderData.h"
#include "FTextRenderer.h"
#include "Renderer/Renderer.h"
#include "Asset/UMaterial.h"
#include "Asset/UStaticMesh.h"
#include "Asset/USkeletalMesh.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/FSceneViewFamily.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace Renderer {

    Renderer::EMeshPass Renderer::FTranslucencyMeshPass::GetType() const {
        return Renderer::EMeshPass::TranslucencyMeshPass;
    }

    Renderer::FTranslucencyMeshPass::FRenderResult Renderer::FTranslucencyMeshPass::Render(const Engine::FSceneViewFamily& Family, bool EditorIcons, const Renderer::FTranslucencyMeshPass::FRenderParameters& Parameters) {
        using FParticleBatch = const Engine::FParticleSpriteBatch*;
        using FParticleTrail = const Engine::FParticleTrailBatch*;
        using FPrimitive = std::variant<const Engine::FPrimitiveSceneProxy*, Engine::FDrawText, std::size_t, FParticleBatch, FParticleTrail>;
        using FCommand = std::pair<float, FPrimitive>;
        Core::TArray<FCommand> Commands{};
        Core::TArray<Renderer::FMeshDrawCommand> MeshCommands{};
        Renderer::FTranslucencyMeshPass::FRenderResult Result{};

        const auto Depth{[&Family](const FVector& Position, bool ScreenSpace) {
            const FVector4 Clip{FVector4{Position, 1.0f} * Family.mView.mViewProjection};
            const float Value{ScreenSpace ? Position.z : Clip.w > 0.0f ? Clip.z / Clip.w : -1.0f};

            return std::isfinite(Value) ? Value : -1.0f;
        }};

        Core::TArray<Engine::FPrimitiveSceneProxy> LightIcons{};

        if (EditorIcons && Family.mEditorElements != nullptr && (Family.mShowFlags.mLightIcons || Family.mShowFlags.mFogIcons)) {
            const auto* Scene{static_cast<const Renderer::FScene*>(Family.mScene)};

            LightIcons.reserve(Scene->GetLights().size() + Scene->GetHeightFogs().size());

            for (const Engine::FLightSceneProxy& Light : Scene->GetLights()) {
                if (Family.mShowFlags.mLightIcons && Light.mVisible) {
                    LightIcons.push_back(Light.CreateIconProxy());
                }
            }

            for (const Engine::FHeightFogSceneProxy& Fog : Scene->GetHeightFogs()) {
                if (Family.mShowFlags.mFogIcons && Fog.mVisible) {
                    LightIcons.push_back(Fog.CreateIconProxy());
                }
            }

            for (const Engine::FPrimitiveSceneProxy& Icon : LightIcons) {
                Commands.emplace_back(Depth(Icon.mWorld.TransformPosition({}), false), FPrimitive{&Icon});
            }
        }

        if (!EditorIcons && Family.mShowFlags.mPrimitives) {
            const auto* Scene{static_cast<const Renderer::FScene*>(Family.mScene)};
            Uint32 Primitives{};

            if (BuildCommands(*Scene, Family, Family.mView.mViewProjection, MeshCommands, Primitives)) {
                for (std::size_t Index{}; Index < MeshCommands.size(); ++Index) {
                    const Renderer::FMeshDrawCommand& MeshCommand{MeshCommands[Index]};
                    const FBox& Bounds{MeshCommand.mParticleInstance != nullptr ? MeshCommand.mParticleInstance->mBounds : MeshCommand.mPrimitive->mProxy.mBounds};
                    const FVector Center{(Bounds.mMinimum + Bounds.mMaximum) * 0.5f};
                    Commands.emplace_back(Depth(Center, false), FPrimitive{Index});
                }
            }

            for (const Renderer::FScenePrimitive& Primitive : Scene->GetPrimitives()) {
                const Engine::FPrimitiveSceneProxy& Proxy{Primitive.mProxy};

                if (Proxy.mParticles != nullptr) {
                    // 같은 컴포넌트의 에미터는 같은 깊이 키라 안정 정렬로 배열 순서가 유지됩니다.
                    const FVector Center{(Proxy.mBounds.mMinimum + Proxy.mBounds.mMaximum) * 0.5f};
                    const float Value{Depth(Center, false)};

                    for (const Engine::FParticleSpriteBatch& Batch : Proxy.mParticles->mSprites) {
                        if (!Batch.mInstances.empty()) {
                            Commands.emplace_back(Value, FPrimitive{&Batch});
                        }
                    }

                    for (const Engine::FParticleTrailBatch& Batch : Proxy.mParticles->mTrails) {
                        if (!Batch.mStripEnds.empty()) {
                            Commands.emplace_back(Value, FPrimitive{&Batch});
                        }
                    }
                } else if (Proxy.mSprite.has_value() && Family.mShowFlags.mSprites) {
                    Commands.emplace_back(Depth(Proxy.mWorld.TransformPosition({}), false), FPrimitive{&Proxy});
                } else if (Proxy.mText.has_value() && Proxy.mText->mDepthTest && Family.mShowFlags.mText) {
                    Engine::FDrawText Text{*Proxy.mText};
                    Text.mPosition = Proxy.mWorld.TransformPosition(Text.mPosition);
                    Text.mScale.x *= Proxy.mWorld.TransformVector({0.0f, 1.0f, 0.0f}).Length();
                    Text.mScale.y *= Proxy.mWorld.TransformVector({0.0f, 0.0f, 1.0f}).Length();

                    const float Value{Depth(Text.mPosition, Text.mScreenSpace)};

                    Commands.emplace_back(Value, FPrimitive{std::move(Text)});
                }
            }
        }

        if (EditorIcons && Family.mEditorElements != nullptr) {
            if (Family.mShowFlags.mSprites) {
                for (const Engine::FPrimitiveSceneProxy& Sprite : Family.mEditorElements->GetSprites()) {
                    Commands.emplace_back(Depth(Sprite.mWorld.TransformPosition({}), false), FPrimitive{&Sprite});
                }
            }

            if (Family.mShowFlags.mText) {
                for (const Engine::FDrawText& Text : Family.mEditorElements->GetTexts()) {
                    if (Text.mDepthTest) {
                        Commands.emplace_back(Depth(Text.mPosition, Text.mScreenSpace), FPrimitive{Text});
                    }
                }
            }
        }

        std::stable_sort(Commands.begin(), Commands.end(), [](const FCommand& First, const FCommand& Second) {
            return First.first > Second.first;
        });

        mContext->OMSetDepthStencilState(Parameters.mDepthReadOnly, 0);
        mRenderer->mViewBuffer.Update(mContext, Family.mView.mViewProjection);
        mRenderer->mViewBuffer.Bind(mContext, RenderCore::EShaderStage::Vertex | RenderCore::EShaderStage::Pixel, 1);

        Core::TArray<Renderer::FMeshDrawCommand> MeshRun{};
        const auto SubmitMeshRun{[&]() {
            if (MeshRun.empty()) {
                return;
            }

            ID3D11RasterizerState* Rasterizer{Family.mViewMode == Engine::EViewMode::Wireframe ? Parameters.mWireframeRasterizer : nullptr};
            mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Result.mDrawCalls += mRenderer->SubmitMeshCommands(Rasterizer, GetType(), MeshRun, false);
            Result.mVisiblePrimitives += static_cast<Uint32>(MeshRun.size());
            MeshRun.clear();
        }};

        for (const FCommand& Command : Commands) {
            if (const auto* MeshIndex{std::get_if<std::size_t>(&Command.second)}) {
                MeshRun.push_back(MeshCommands[*MeshIndex]);
                continue;
            }

            SubmitMeshRun();

            bool Drawn{};

            if (const auto* Sprite{std::get_if<const Engine::FPrimitiveSceneProxy*>(&Command.second)}) {
                mContext->OMSetBlendState(Parameters.mBlend, nullptr, 0xFFFFFFFFu);
                mContext->RSSetState(Family.mViewMode == Engine::EViewMode::Wireframe ? Parameters.mWireframeRasterizer : Parameters.mRasterizer);

                Drawn = Parameters.mSpriteRenderer != nullptr && Parameters.mSpriteRenderer->Draw(mContext, **Sprite, Family.mView);
            } else if (const auto* Batch{std::get_if<FParticleBatch>(&Command.second)}) {
                mContext->OMSetBlendState(mRenderer->GetTranslucencyBlendState((*Batch)->mMaterial.Get()), nullptr, 0xFFFFFFFFu);
                mContext->RSSetState(Family.mViewMode == Engine::EViewMode::Wireframe ? Parameters.mWireframeRasterizer : Parameters.mRasterizer);

                Drawn = Parameters.mParticleRenderer != nullptr && Parameters.mParticleRenderer->Draw(mContext, **Batch, Family.mView);
            } else if (const auto* Trail{std::get_if<FParticleTrail>(&Command.second)}) {
                mContext->OMSetBlendState(mRenderer->GetTranslucencyBlendState((*Trail)->mMaterial.Get()), nullptr, 0xFFFFFFFFu);
                mContext->RSSetState(Family.mViewMode == Engine::EViewMode::Wireframe ? Parameters.mWireframeRasterizer : Parameters.mRasterizer);

                Drawn = Parameters.mParticleRenderer != nullptr && Parameters.mParticleRenderer->DrawTrail(mContext, **Trail, Family.mView);
            } else {
                mContext->OMSetBlendState(Parameters.mBlend, nullptr, 0xFFFFFFFFu);
                mContext->RSSetState(Parameters.mRasterizer);

                Drawn = Parameters.mTextRenderer != nullptr && Parameters.mTextRenderer->Draw(mContext, std::get<Engine::FDrawText>(Command.second), Family.mView);
            }

            if (Drawn) {
                ++Result.mDrawCalls;
                ++Result.mVisiblePrimitives;
            }
        }

        SubmitMeshRun();
        mContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);

        return Result;
    }

    Uint32 Renderer::FTranslucencyMeshPass::RenderOverlayTexts(const Engine::FSceneViewFamily& Family, const Renderer::FTranslucencyMeshPass::FRenderParameters& Parameters) {
        mContext->RSSetState(Parameters.mRasterizer);

        if (Parameters.mTextRenderer == nullptr || !Family.mShowFlags.mText) {
            return 0;
        }

        Core::TArray<Engine::FDrawText> Texts{};
        Uint32 DrawCalls{};

        if (Family.mShowFlags.mPrimitives) {
            const auto* Scene{static_cast<const Renderer::FScene*>(Family.mScene)};

            for (const Renderer::FScenePrimitive& Primitive : Scene->GetPrimitives()) {
                const Engine::FPrimitiveSceneProxy& Proxy{Primitive.mProxy};

                if (Proxy.mText.has_value() && !Proxy.mText->mDepthTest) {
                    Engine::FDrawText Text{*Proxy.mText};
                    Text.mPosition = Proxy.mWorld.TransformPosition(Text.mPosition);
                    Text.mScale.x *= Proxy.mWorld.TransformVector({0.0f, 1.0f, 0.0f}).Length();
                    Text.mScale.y *= Proxy.mWorld.TransformVector({0.0f, 0.0f, 1.0f}).Length();

                    Texts.push_back(std::move(Text));
                }
            }
        }

        if (Family.mEditorElements != nullptr) {
            for (const Engine::FDrawText& Text : Family.mEditorElements->GetTexts()) {
                if (!Text.mDepthTest) {
                    Texts.push_back(Text);
                }
            }
        }

        std::stable_sort(Texts.begin(), Texts.end(), [&Family](const Engine::FDrawText& First, const Engine::FDrawText& Second) {
            if (First.mScreenSpace != Second.mScreenSpace) {
                return !First.mScreenSpace;
            }

            if (First.mScreenSpace) {
                return First.mPosition.z > Second.mPosition.z;
            }

            return (First.mPosition - Family.mView.mLocation).Dot(Family.mView.mForward) > (Second.mPosition - Family.mView.mLocation).Dot(Family.mView.mForward);
        });

        mContext->OMSetBlendState(Parameters.mBlend, nullptr, 0xFFFFFFFFu);
        mContext->OMSetDepthStencilState(Parameters.mDepthDisabled, 0);

        for (const Engine::FDrawText& Text : Texts) {
            if (Parameters.mTextRenderer->Draw(mContext, Text, Family.mView)) {
                ++DrawCalls;
            }
        }

        mContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);

        return DrawCalls;
    }

    bool Renderer::FTranslucencyMeshPass::BeginCommands(const Engine::FSceneViewFamily&) {
        return true;
    }

    bool Renderer::FTranslucencyMeshPass::Includes(const Renderer::FScenePrimitive&, const Engine::FSceneViewFamily& Family) const {
        return Family.mShowFlags.mPrimitives;
    }

    void Renderer::FTranslucencyMeshPass::AddCommands(const Renderer::FScenePrimitive& Primitive, Asset::UStaticMesh& Mesh, const Engine::FSceneViewFamily& Family, Core::TArray<Renderer::FMeshDrawCommand>& Commands, const Engine::FParticleMeshBatch* ParticleBatch) {
        const bool Skinned{ParticleBatch == nullptr && Primitive.mProxy.mSkeletalMesh != nullptr};
        const bool ReceiveShadows{Family.mShowFlags.mShadows && (ParticleBatch != nullptr || Primitive.mProxy.mReceiveShadow)};

        const auto& SubMeshes{Mesh.GetMeshData().GetSubMeshes()};

        for (Index_t SubMeshIndex{}; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Asset::FSubMesh& SubMesh{SubMeshes[SubMeshIndex]};
            Asset::UMaterial* Material{ResolveMaterial(Primitive, SubMesh.GetMaterialIndex(), mRenderer->GetDefaultMaterial(), ParticleBatch)};

            if (Material == nullptr || Material->GetBlendMode() == Asset::EBlendMode::Opaque || !Material->CreateResources(mDevice, mContext)) {
                continue;
            }

            // 천 섹션은 정점을 입자 위치로 옮기는 천 변형 셰이더로 그립니다.
            const Index_t ClothAssetIndex{GetClothAssetIndex(Primitive, SubMeshIndex)};
            Renderer::FMaterialPipeline* Pipeline{mRenderer->GetMaterialPipeline(*Material, Family.mViewMode, Skinned, ReceiveShadows, ClothAssetIndex != InvalidIndex, ParticleBatch != nullptr)};

            if (Pipeline != nullptr) {
                Commands.emplace_back(Pipeline, Material, &Mesh, &Primitive, SubMesh.GetFirstIndex(), SubMesh.GetIndexCount(), ClothAssetIndex);
            }
        }
    }

}
