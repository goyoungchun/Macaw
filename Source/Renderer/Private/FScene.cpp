/**
 * @file FScene.cpp
 * @brief 프리미티브 프록시와 객체 상수 버퍼를 소유하는 Direct3D 11 렌더 장면입니다.
 * @ingroup Renderer
 */

#include "Core/Types.h"
#include "FScene.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FBox.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Engine/EPrimitiveType.h"
#include "Asset/UStaticMesh.h"
#include "Asset/UMaterial.h"
#include "Asset/UTexture2D.h"
#include "Asset/USkeletalMesh.h"
#include "FObjectConstants.h"
#include "FScenePrimitive.h"
#include "Core/Base/ErrorHandler.h"

#include <algorithm>

namespace Renderer {

    /**
     * @brief FScene의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Renderer::FScene::FScene(ID3D11Device* Device, ID3D11DeviceContext* Context)
		: mDevice{Device},
		  mContext{Context} {
    }

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Renderer::FScene::~FScene() = default;

    /**
     * @brief 컴포넌트 GUID를 키로 높이 안개 프록시를 렌더 장면에 추가하거나 교체합니다.
     */
    void Renderer::FScene::AddHeightFog(const Engine::FHeightFogSceneProxy& Proxy) {
        const auto Existing{std::find_if(mHeightFogs.begin(), mHeightFogs.end(), [&Proxy](const Engine::FHeightFogSceneProxy& Fog) {
            return Fog.mId == Proxy.mId;
        })};

        if (Existing != mHeightFogs.end()) {
            *Existing = Proxy;
        } else {
            mHeightFogs.push_back(Proxy);
        }

        ++mRevision;
    }

    /**
     * @brief 지정한 GUID의 높이 안개 프록시를 렌더 장면에서 제거합니다.
     */
    void Renderer::FScene::RemoveHeightFog(const Core::FGuid& Id) {
        if (std::erase_if(mHeightFogs, [&Id](const Engine::FHeightFogSceneProxy& Fog) {
            return Fog.mId == Id;
        }) > 0) {
            ++mRevision;
        }
    }

    /**
     * @brief 현재 렌더 장면에 등록된 높이 안개 프록시 목록을 조회합니다.
     */
    const Core::TArray<Engine::FHeightFogSceneProxy>& Renderer::FScene::GetHeightFogs() const {
        return mHeightFogs;
    }

    void Renderer::FScene::AddSkyAtmosphere(const Engine::FSkyAtmosphereSceneProxy& Proxy) {
        const auto Existing{std::find_if(mSkyAtmospheres.begin(), mSkyAtmospheres.end(), [&Proxy](const Engine::FSkyAtmosphereSceneProxy& Atmosphere) {
            return Atmosphere.mId == Proxy.mId;
        })};

        if (Existing != mSkyAtmospheres.end()) {
            *Existing = Proxy;
        } else {
            mSkyAtmospheres.push_back(Proxy);
        }

        ++mRevision;
    }

    void Renderer::FScene::RemoveSkyAtmosphere(const Core::FGuid& Id) {
        if (std::erase_if(mSkyAtmospheres, [&Id](const Engine::FSkyAtmosphereSceneProxy& Atmosphere) {
            return Atmosphere.mId == Id;
        }) > 0) {
            ++mRevision;
        }
    }

    const Engine::FSkyAtmosphereSceneProxy* Renderer::FScene::GetSkyAtmosphere() const {
        const auto Active{std::find_if(mSkyAtmospheres.begin(), mSkyAtmospheres.end(), [](const Engine::FSkyAtmosphereSceneProxy& Atmosphere) {
            return Atmosphere.mVisible;
        })};

        return Active != mSkyAtmospheres.end() ? &*Active : nullptr;
    }

    const Engine::FLightSceneProxy* Renderer::FScene::GetAtmosphereSunLight() const {
        const auto Sun{std::find_if(mLights.begin(), mLights.end(), [](const Engine::FLightSceneProxy& Light) {
            return Light.mType == Engine::ELightType::Directional && Light.mAtmosphereSunLight && Light.mVisible;
        })};

        return Sun != mLights.end() ? &*Sun : nullptr;
    }

    void Renderer::FScene::AddSkyDome(const Engine::FSkyDomeSceneProxy& Proxy) {
        const auto Existing{std::find_if(mSkyDomes.begin(), mSkyDomes.end(), [&Proxy](const Engine::FSkyDomeSceneProxy& SkyDome) {
            return SkyDome.mId == Proxy.mId;
        })};

        if (Existing != mSkyDomes.end()) {
            *Existing = Proxy;
        } else {
            mSkyDomes.push_back(Proxy);
        }

        ++mRevision;
    }

    void Renderer::FScene::RemoveSkyDome(const Core::FGuid& Id) {
        if (std::erase_if(mSkyDomes, [&Id](const Engine::FSkyDomeSceneProxy& SkyDome) {
            return SkyDome.mId == Id;
        }) > 0) {
            ++mRevision;
        }
    }

    const Engine::FSkyDomeSceneProxy* Renderer::FScene::GetSkyDome() const {
        const auto Active{std::find_if(mSkyDomes.begin(), mSkyDomes.end(), [](const Engine::FSkyDomeSceneProxy& SkyDome) {
            return SkyDome.mVisible && SkyDome.mPanorama != nullptr;
        })};

        return Active != mSkyDomes.end() ? &*Active : nullptr;
    }

    void Renderer::FScene::AddDecal(const Engine::FDecalSceneProxy& Proxy) {
        const auto Existing{std::find_if(mDecals.begin(), mDecals.end(), [&Proxy](const Engine::FDecalSceneProxy& Decal) {
            return Decal.mId == Proxy.mId;
        })};

        if (Existing != mDecals.end()) {
            if (Existing->mSortOrder == Proxy.mSortOrder) {
                *Existing = Proxy;

                return;
            }

            mDecals.erase(Existing);
        }

        const auto Position{std::upper_bound(mDecals.begin(), mDecals.end(), Proxy.mSortOrder, [](Int32 SortOrder, const Engine::FDecalSceneProxy& Decal) {
            return SortOrder < Decal.mSortOrder;
        })};

        mDecals.insert(Position, Proxy);
    }

    void Renderer::FScene::RemoveDecal(const Core::FGuid& Id) {
        std::erase_if(mDecals, [&Id](const Engine::FDecalSceneProxy& Decal) {
            return Decal.mId == Id;
        });
    }

    const Core::TArray<Engine::FDecalSceneProxy>& Renderer::FScene::GetDecals() const {
        return mDecals;
    }

    /**
     * @brief 프록시의 GUID로 렌더 장면에 프리미티브를 추가하거나 교체합니다.
     */
    void Renderer::FScene::AddPrimitive(const Engine::FPrimitiveSceneProxy& Proxy) {
        // 공유 형상 버퍼 외에 스킨 정점 버퍼도 같은 장치에서 준비합니다.
        if (Proxy.mSkeletalMesh != nullptr && (Proxy.mMesh != Proxy.mSkeletalMesh->GetGeometry() || Proxy.mSkinMatrices.size() < Proxy.mSkeletalMesh->GetSkinMatrixCount() || !Proxy.mSkeletalMesh->CreateResources(mDevice))) {
            Core::ErrorHandler::Report("SkeletalMesh", "Cannot prepare skeletal mesh resources or pose", Core::ErrorHandler::EErrorLevel::Error);

            return;
        }

        if (Proxy.mMesh != nullptr && !Proxy.mMesh->CreateResources(mDevice)) {
            Core::ErrorHandler::Report("Mesh", "Cannot create project mesh resources", Core::ErrorHandler::EErrorLevel::Error);

            return;
        }

        const auto Position{mIndices.find(Proxy.mId)};

        bool CasterChanged{IsShadowCaster(Proxy)};

        if (Position != mIndices.end()) {
            Renderer::FScenePrimitive& Primitive{mPrimitives[Position->second]};

            CasterChanged = CasterChanged || IsShadowCaster(Primitive.mProxy);
            Primitive.mProxy = Proxy;

            Upload(Primitive);
        } else {
            Renderer::FScenePrimitive Primitive{Proxy};

            Upload(Primitive);

            mIndices.emplace(Proxy.mId, mPrimitives.size());
            mPrimitives.push_back(std::move(Primitive));
        }

        if (CasterChanged) {
            mCasterRevision = Core::FRevision::Next();
        }

        ++mRevision;
    }

    /**
     * @brief 지정한 프리미티브의 월드 행렬과 경계 상자를 갱신합니다.
     */
    void Renderer::FScene::UpdatePrimitiveTransform(const Core::FGuid& Id, const FMatrix& World, const FBox& Bounds) {
        const auto Position{mIndices.find(Id)};

        if (Position == mIndices.end()) {
            return;
        }

        Renderer::FScenePrimitive& Primitive{mPrimitives[Position->second]};

        if (Primitive.mProxy.mWorld == World) {
            return;
        }

        Primitive.mProxy.mWorld = World;
        Primitive.mProxy.mBounds = Bounds;

        Upload(Primitive);

        if (IsShadowCaster(Primitive.mProxy)) {
            mCasterRevision = Core::FRevision::Next();
        }

        ++mRevision;
    }

    /**
     * @brief 식별자에 해당하는 프리미티브와 관련 GPU 리소스를 제거합니다.
     */
    void Renderer::FScene::RemovePrimitive(const Core::FGuid& Id) {
        const auto Position{mIndices.find(Id)};

        if (Position == mIndices.end()) {
            return;
        }

        const Index_t Index{static_cast<Index_t>(Position->second)};

        if (IsShadowCaster(mPrimitives[Index].mProxy)) {
            mCasterRevision = Core::FRevision::Next();
        }

        if (Index + 1 != mPrimitives.size()) {
            mPrimitives[Index] = std::move(mPrimitives.back());
            mIndices[mPrimitives[Index].mProxy.mId] = Index;
        }

        mPrimitives.pop_back();
        mIndices.erase(Position);

        ++mRevision;
    }

    /**
     * @brief 현재 장면에 등록된 프리미티브 개수를 반환합니다.
     */
    std::size_t Renderer::FScene::GetPrimitiveCount() const {
        return mPrimitives.size();
    }

    /**
     * @brief 프리미티브 추가, 제거 또는 변환 갱신 시 증가하는 장면 개정 번호를 반환합니다.
     */
    Uint64 Renderer::FScene::GetRevision() const {
        return mRevision;
    }

    /**
     * @brief 장면이 소유한 렌더 프리미티브를 반환합니다.
     */
    const Core::TArray<Renderer::FScenePrimitive>& Renderer::FScene::GetPrimitives() const {
        return mPrimitives;
    }

    /**
     * @brief 누적 변환 업로드 횟수를 반환하고 카운터를 초기화합니다.
     */
    Core::FRevision Renderer::FScene::GetIdentity() const {
        return mIdentity;
    }

    /**
     * @brief 그림자 깊이 결과에 영향을 주는 캐스터 상태의 리비전을 반환합니다.
     */
    Core::FRevision Renderer::FScene::GetCasterRevision() const {
        Core::FRevision Revision{mCasterRevision};

        const auto IncludeMaterial{[&Revision](const Asset::UMaterial* Material) {
            if (Material != nullptr && Material->GetBlendModeRevision().GetValue() > Revision.GetValue()) {
                Revision = Material->GetBlendModeRevision();
            }
        }};

        for (const Renderer::FScenePrimitive& Primitive : mPrimitives) {
            if (!IsShadowCaster(Primitive.mProxy)) {
                continue;
            }

            if (Primitive.mProxy.mMaterial != nullptr) {
                IncludeMaterial(Primitive.mProxy.mMaterial.Get());
            } else {
                for (const auto& Material : Primitive.mProxy.mMaterials) {
                    IncludeMaterial(Material.Get());
                }
            }
        }

        return Revision;
    }

    /**
     * @brief 프록시가 그림자 깊이 패스에 그려지는 캐스터인지 검사합니다.
     */
    bool Renderer::FScene::IsShadowCaster(const Engine::FPrimitiveSceneProxy& Proxy) {
        return Proxy.mCastShadow && !Proxy.mSprite.has_value() && !Proxy.mText.has_value();
    }

    /**
     * @brief 누적 변환 업로드 횟수를 반환하고 카운터를 초기화합니다.
     */
    Uint32 Renderer::FScene::TakeTransformUpdates() const {
        const Uint32 Count{mTransformUpdates};

        mTransformUpdates = 0;

        return Count;
    }

    /**
     * @brief 프리미티브의 객체 상수 버퍼를 현재 프록시 값으로 갱신합니다.
     */
    void Renderer::FScene::Upload(Renderer::FScenePrimitive& Primitive) {
        if (Primitive.mProxy.mParticles != nullptr) {
            Primitive.mObjectBuffer.Reset();

            return;
        }

        if (Primitive.mProxy.mText.has_value() || Primitive.mProxy.mSprite.has_value()) {
            Primitive.mObjectBuffer.Reset();

            return;
        }

        if (!UploadSkinning(Primitive)) {
            Primitive.mObjectBuffer.Reset();
            Core::ErrorHandler::Report("SkeletalMesh", "Cannot upload skeletal pose", Core::ErrorHandler::EErrorLevel::Error);

            return;
        }

        UploadCloth(Primitive);

        Renderer::FObjectConstants& Constants{Primitive.mObjectConstants};
        Constants.mWorld = Primitive.mProxy.mWorld;
        Constants.mUVTransform = {Primitive.mProxy.mUVScale.x, Primitive.mProxy.mUVScale.y, Primitive.mProxy.mUVOffset.x, Primitive.mProxy.mUVOffset.y};

        if (!Constants.mWorld.TryInverse(Constants.mWorldInverse)) {
            Core::ErrorHandler::Fatal("Renderer", "Singular render transform");
        }

        Core::FString Error{};

        if (!Primitive.mObjectBuffer.Create(mDevice, Error)) {
            Core::ErrorHandler::Fatal("Renderer", "Object buffer allocation failed");
        }

        Primitive.mObjectBuffer.Update(mContext, Constants);

        ++mTransformUpdates;
    }

    bool Renderer::FScene::UploadSkinning(Renderer::FScenePrimitive& Primitive) {
        if (Primitive.mProxy.mSkeletalMesh == nullptr) {
            Primitive.mSkinBuffer.Reset();

            return true;
        }

        // 프록시가 다시 만들어져도 포즈가 그대로이면 다시 올리지 않습니다.
        if (Primitive.mSkinBuffer.IsCreated() && Primitive.mProxy.mPoseRevision.IsValid() && Primitive.mUploadedPoseRevision == Primitive.mProxy.mPoseRevision) {
            return true;
        }

        // 각 본의 위치 변환과 법선 역전치를 함께 올려 비균일 스케일도 처리합니다.
        // 한 원소의 배치(행렬 2개)는 Skinning.hlsl의 FSkinBone과 일치해야 합니다.
        Core::TArray<Core::TStaticArray<FMatrix, 2>> Bones{};
        Bones.reserve(Primitive.mProxy.mSkinMatrices.size());

        for (const FMatrix& Skin : Primitive.mProxy.mSkinMatrices) {
            FMatrix Inverse{};

            if (!Skin.IsFinite() || !Skin.TryInverse(Inverse) || !Inverse.IsFinite()) {
                return false;
            }

            Bones.push_back({Skin, Inverse.Transposed()});
        }

        if (Bones.empty() || Bones.size() > 100000) {
            return false;
        }

        Core::FString Error{};

        if (!Primitive.mSkinBuffer.Upload(mContext, Bones, Error)) {
            return false;
        }

        Primitive.mUploadedPoseRevision = Primitive.mProxy.mPoseRevision;

        return true;
    }

    /**
     * @brief 천 에셋마다 입자 위치와 법선, 천 상수를 GPU에 반영합니다.
     */
    void Renderer::FScene::UploadCloth(Renderer::FScenePrimitive& Primitive) {
        if (Primitive.mProxy.mSkeletalMesh == nullptr || Primitive.mProxy.mClothSimData.empty()) {
            Primitive.mClothResources.clear();
            Primitive.mUploadedClothRevision = {};

            return;
        }

        // 프록시가 다시 만들어져도 천 결과가 그대로이면 다시 올리지 않습니다.
        if (Primitive.mProxy.mClothRevision.IsValid() && Primitive.mUploadedClothRevision == Primitive.mProxy.mClothRevision) {
            return;
        }

        // 결과에 없는 천 에셋의 자원은 버립니다.
        std::erase_if(Primitive.mClothResources, [&Primitive](const auto& Entry) {
            return !Primitive.mProxy.mClothSimData.contains(Entry.first);
        });

        Core::TArray<Renderer::FClothParticleGpu> Particles{};

        for (const auto& [AssetIndex, SimData] : Primitive.mProxy.mClothSimData) {
            if (SimData.mPositions.empty() || SimData.mPositions.size() != SimData.mNormals.size()) {
                Primitive.mClothResources.erase(AssetIndex);

                continue;
            }

            Particles.resize(SimData.mPositions.size());

            for (Index_t ParticleIndex{}; ParticleIndex < Particles.size(); ++ParticleIndex) {
                const FVector& Position{SimData.mPositions[ParticleIndex]};
                const FVector& Normal{SimData.mNormals[ParticleIndex]};

                Particles[ParticleIndex].mPosition = {Position.x, Position.y, Position.z};
                Particles[ParticleIndex].mNormal = {Normal.x, Normal.y, Normal.z};
            }

            Renderer::FScenePrimitive::FClothResources& Resources{Primitive.mClothResources[AssetIndex]};
            Core::FString Error{};

            if (!Resources.mParticles.Upload(mContext, Particles, Error) || (!Resources.mConstants.IsCreated() && !Resources.mConstants.Create(mDevice, Error))) {
                Core::ErrorHandler::Report("Cloth", "Cannot upload cloth particles: " + Error, Core::ErrorHandler::EErrorLevel::Warning);
                Primitive.mClothResources.erase(AssetIndex);

                continue;
            }

            Renderer::FClothConstants Constants{};
            Constants.mClothLocalToComponent = SimData.mComponentRelativeTransform.ToMatrix();
            Constants.mClothBlendWeight = Primitive.mProxy.mClothBlendWeight;

            Resources.mConstants.Update(mContext, Constants);
        }

        Primitive.mUploadedClothRevision = Primitive.mProxy.mClothRevision;
    }

    /**
     * @brief 광원 프록시의 GUID로 렌더 장면에 광원을 추가하거나 교체합니다.
     */
    void Renderer::FScene::AddLight(const Engine::FLightSceneProxy& Proxy) {
        const auto Position{mLightIndices.find(Proxy.mId)};

        if (Position == mLightIndices.end()) {
            mLightIndices.emplace(Proxy.mId, mLights.size());
            mLights.push_back(Proxy);
        } else {
            mLights[Position->second] = Proxy;
        }

        ++mRevision;
    }

    /**
     * @brief 지정한 GUID의 광원을 렌더 장면에서 제거합니다.
     */
    void Renderer::FScene::RemoveLight(const Core::FGuid& Id) {
        const auto Position{mLightIndices.find(Id)};

        if (Position == mLightIndices.end()) {
            return;
        }

        const Index_t Index{static_cast<Index_t>(Position->second)};

        if (Index + 1 != mLights.size()) {
            mLights[Index] = mLights.back();
            mLightIndices[mLights[Index].mId] = Index;
        }

        mLights.pop_back();
        mLightIndices.erase(Position);

        ++mRevision;
    }

    /**
     * @brief 장면에 등록된 광원 목록을 반환합니다.
     */
    const Core::TArray<Engine::FLightSceneProxy>& Renderer::FScene::GetLights() const {
        return mLights;
    }

}
