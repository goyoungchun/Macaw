/**
 * @file USkeletalMesh.cpp
 * @brief 본 데이터 검증과 공유 바인드 형상 및 GPU 스킨 가중치를 관리합니다.
 * @ingroup Asset
 */

#include "Core/Types.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Asset/FPhysicsAssetUtils.h"
#include "Core/Math/FMatrix.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Core/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <d3d11.h>
#include <wrl/client.h>

namespace Asset {

    namespace {

        void IncludeBounds(std::optional<FBox>& Bounds, const FBox& IncludedBounds) {
            if (Bounds.has_value()) {
                Bounds->mMinimum = Bounds->mMinimum.Min(IncludedBounds.mMinimum);
                Bounds->mMaximum = Bounds->mMaximum.Max(IncludedBounds.mMaximum);
            } else {
                Bounds = IncludedBounds;
            }
        }

    }

    /**
     * @brief 형상과 가중치를 검증하고 저장합니다.
     */
    bool Asset::USkeletalMesh::SetMeshData(Asset::FMeshData MeshData, Core::TArray<Asset::FMeshImportSkinWeight> SkinWeights) {
        if (SkinWeights.size() != MeshData.GetPositions().size() || SkinWeights.size() > 16000000) {
            return false;
        }

        const Core::TArray<FVector>& Positions{MeshData.GetPositions()};
        Core::TArray<std::optional<FBox>> InfluenceBounds{};
        std::optional<FBox> UnweightedBounds{};

        for (Index_t VertexIndex{}; VertexIndex < Positions.size(); ++VertexIndex) {
            const Asset::FMeshImportSkinWeight& SkinWeight{SkinWeights[VertexIndex]};
            const FVector& Position{Positions[VertexIndex]};
            const FBox VertexBounds{Position, Position};
            float Total{};

            for (Index_t Index{}; Index < Asset::FMeshImportSkinWeight::MaxInfluences; ++Index) {
                const Index_t BoneIndex{static_cast<Index_t>(SkinWeight.GetBoneIndices()[Index])};
                const float Weight{SkinWeight.GetWeights()[Index]};

                if (BoneIndex < 0 || BoneIndex >= Asset::MaxSkeletonBones || !std::isfinite(Weight) || Weight < 0.0f || Weight > 1.0f) {
                    return false;
                }

                if (BoneIndex >= InfluenceBounds.size()) {
                    InfluenceBounds.resize(BoneIndex + 1);
                }

                if (Weight > 0.0f) {
                    IncludeBounds(InfluenceBounds[BoneIndex], VertexBounds);
                }

                Total += Weight;
            }

            // 가중치 합은 1이어야 합니다. 합이 0인 정점은 본 영향이 없다는 뜻이며 바인드 위치에 그대로 남습니다.
            if (Total != 0.0f && std::abs(Total - 1.0f) > 0.0001f) {
                return false;
            }

            if (Total == 0.0f) {
                IncludeBounds(UnweightedBounds, VertexBounds);
            }
        }

        if (!mGeometry.SetMeshData(std::move(MeshData))) {
            return false;
        }

        mSkinWeightBuffer.Reset();
        mClothVertexBuffer.Reset();
        mSkinWeights = std::move(SkinWeights);
        mSkinMatrixCount = static_cast<Uint32>(InfluenceBounds.size());
        mSkinInfluenceBounds = std::move(InfluenceBounds);
        mUnweightedBounds = UnweightedBounds;

        // 형상이 바뀌었으므로 정점으로 만든 물리 에셋을 버립니다.
        mGeneratedPhysicsAsset.reset();
        mGeneratedPhysicsAssetSkeleton = nullptr;

        // 섹션과 정점이 바뀌었으므로 그 섹션으로 만든 천 에셋과 연결도 버립니다.
        mMeshClothingAssets.clear();
        mSectionClothingData.clear();

        return true;
    }

    /**
     * @brief 바인드 포즈의 CPU 형상을 반환합니다.
     */
    const Asset::FMeshData& Asset::USkeletalMesh::GetMeshData() const {
        return mGeometry.GetMeshData();
    }

    /**
     * @brief 정점별 스킨 가중치를 반환합니다.
     */
    const Core::TArray<Asset::FMeshImportSkinWeight>& Asset::USkeletalMesh::GetSkinWeights() const {
        return mSkinWeights;
    }

    Uint32 Asset::USkeletalMesh::GetSkinMatrixCount() const {
        return mSkinMatrixCount;
    }

    /**
     * @brief 공통 메시 드로 경로가 사용할 바인드 형상을 반환합니다.
     */
    Asset::UStaticMesh* Asset::USkeletalMesh::GetGeometry() {
        return &mGeometry;
    }

    /**
     * @brief 장치에 바인드 형상과 불변 가중치 버퍼를 준비합니다.
     */
    bool Asset::USkeletalMesh::CreateResources(ID3D11Device* Device) {
        if (Device == nullptr || !IsValid() || !mGeometry.CreateResources(Device)) {
            return false;
        }

        // 천 정점 버퍼는 천 섹션 연결이 바뀔 때마다 비워지므로 가중치와 따로 준비합니다.
        if (!CreateClothVertexBuffer(Device)) {
            return false;
        }

        // 가중치는 에셋을 교체하기 전까지 변하지 않으므로, 같은 장치에 이미 만든 버퍼가 있으면 재사용합니다.
        if (mSkinWeightBuffer.IsCreatedOn(Device)) {
            return true;
        }

        // 임포트 클래스의 메모리 배치와 무관하게 uint4, float4 순으로 입력을 만듭니다.
        static_assert(sizeof(FSkinWeightVertex) == sizeof(Uint32) * 4 + sizeof(float) * 4);

        Core::TArray<FSkinWeightVertex> Vertices{};
        Vertices.resize(mSkinWeights.size());

        for (Index_t Index{}; Index < mSkinWeights.size(); ++Index) {
            std::memcpy(Vertices[Index].mBoneIndices.data(), mSkinWeights[Index].GetBoneIndices().data(), sizeof(Uint32) * 4);
            std::memcpy(Vertices[Index].mWeights.data(), mSkinWeights[Index].GetWeights().data(), sizeof(float) * 4);
        }

        // 객체마다 달라지는 포즈는 본 팔레트(FScene::UploadSkinning)에 있고, 이 버퍼는 같은 에셋을 쓰는 객체가 공유하므로 불변으로 만듭니다.
        FSkinWeightBuffer Buffer{};
        Core::FString Error{};

        if (!Buffer.Create(Device, Vertices, Error)) {
            return false;
        }

        mSkinWeightBuffer = std::move(Buffer);

        return true;
    }

    /**
     * @brief 형상과 가중치의 GPU 자원을 해제합니다.
     */
    void Asset::USkeletalMesh::ReleaseResources() {
        mGeometry.ReleaseResources();
        mSkinWeightBuffer.Reset();
        mClothVertexBuffer.Reset();
    }

    /**
     * @brief 천 섹션의 연결 데이터로 메시 정점 순서의 천 정점 정보를 만들어 올립니다.
     */
    bool Asset::USkeletalMesh::CreateClothVertexBuffer(ID3D11Device* Device) {
        if (mClothVertexBuffer.IsCreatedOn(Device)) {
            return true;
        }

        const bool HasClothSection{std::any_of(mSectionClothingData.begin(), mSectionClothingData.end(), [](const Asset::FClothingSectionData& SectionData) {
            return SectionData.IsValid();
        })};

        if (!HasClothSection) {
            return true;
        }

        // 셰이더는 정점 번호로 바로 읽으므로 메시 정점 수만큼 만들고, 천이 아닌 정점은 스키닝만 쓰도록 표시해 둡니다.
        static_assert(sizeof(FClothVertexGpu) == 80);

        const Index_t NumVertices{GetMeshData().GetPositions().size()};
        Core::TArray<FClothVertexGpu> Vertices(NumVertices);

        for (const Asset::FClothingSectionData& SectionData : mSectionClothingData) {
            if (!SectionData.IsValid() || SectionData.mClothMappingData.size() != SectionData.mSectionVertexIndices.size()) {
                continue;
            }

            for (Index_t MappingIndex{}; MappingIndex < SectionData.mClothMappingData.size(); ++MappingIndex) {
                const Uint32 VertexIndex{SectionData.mSectionVertexIndices[MappingIndex]};

                if (VertexIndex >= NumVertices) {
                    continue;
                }

                const PhysicsRuntime::FMeshToMeshVertData& Mapping{SectionData.mClothMappingData[MappingIndex]};
                FClothVertexGpu& Vertex{Vertices[VertexIndex]};

                Vertex.mPositionBaryCoordsAndDist = {Mapping.mPositionBaryCoordsAndDist.x, Mapping.mPositionBaryCoordsAndDist.y, Mapping.mPositionBaryCoordsAndDist.z, Mapping.mPositionBaryCoordsAndDist.w};
                Vertex.mNormalBaryCoordsAndDist = {Mapping.mNormalBaryCoordsAndDist.x, Mapping.mNormalBaryCoordsAndDist.y, Mapping.mNormalBaryCoordsAndDist.z, Mapping.mNormalBaryCoordsAndDist.w};
                Vertex.mTangentBaryCoordsAndDist = {Mapping.mTangentBaryCoordsAndDist.x, Mapping.mTangentBaryCoordsAndDist.y, Mapping.mTangentBaryCoordsAndDist.z, Mapping.mTangentBaryCoordsAndDist.w};
                Vertex.mSourceMeshVertIndices = Mapping.mSourceMeshVertIndices;
                Vertex.mWeight = Mapping.mWeight;
            }
        }

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context{};
        Device->GetImmediateContext(Context.GetAddressOf());

        FClothVertexBuffer Buffer{};
        Core::FString Error{};

        if (Context == nullptr || !Buffer.Upload(Context.Get(), Vertices, Error)) {
            return false;
        }

        mClothVertexBuffer = std::move(Buffer);

        return true;
    }

    /**
     * @brief 본 인덱스와 가중치를 담은 정점 버퍼를 입력 슬롯에 연결합니다.
     */
    void Asset::USkeletalMesh::BindSkinWeights(ID3D11DeviceContext* Context, Index_t Slot) const {
        mSkinWeightBuffer.Bind(Context, Slot);
    }

    /**
     * @brief 입력 슬롯의 스킨 가중치 버퍼 연결을 해제합니다.
     */
    void Asset::USkeletalMesh::UnbindSkinWeights(ID3D11DeviceContext* Context, Index_t Slot) {
        FSkinWeightBuffer::Unbind(Context, Slot);
    }

    /**
     * @brief 메시 정점마다 붙어 있는 천 입자 삼각형 정보를 담은 버퍼를 정점 셰이더 슬롯에 연결합니다.
     */
    void Asset::USkeletalMesh::BindClothVertices(ID3D11DeviceContext* Context, Index_t Slot) const {
        mClothVertexBuffer.Bind(Context, RenderCore::EShaderStage::Vertex, Slot);
    }

    /**
     * @brief 정점 셰이더 슬롯의 천 정점 버퍼 연결을 해제합니다.
     */
    void Asset::USkeletalMesh::UnbindClothVertices(ID3D11DeviceContext* Context, Index_t Slot) {
        FClothVertexBuffer::Unbind(Context, RenderCore::EShaderStage::Vertex, Slot);
    }

    /**
     * @brief 천 정점 버퍼를 준비했는지 검사합니다.
     */
    bool Asset::USkeletalMesh::HasClothVertices() const {
        return mClothVertexBuffer.IsCreated();
    }

    /**
     * @brief 바인드 형상의 로컬 경계를 반환합니다.
     */
    const FBox& Asset::USkeletalMesh::GetBounds() const {
        return mGeometry.GetBounds();
    }

    FBox USkeletalMesh::CalcSkinnedWorldBounds(std::span<const FMatrix> SkinMatrices, const FMatrix& ComponentToWorld) const {
        if (SkinMatrices.size() < mSkinInfluenceBounds.size()) {
            return GetBounds().Transformed(ComponentToWorld);
        }

        std::optional<FBox> Bounds{};

        if (mUnweightedBounds.has_value()) {
            Bounds = mUnweightedBounds->Transformed(ComponentToWorld);
        }

        for (Index_t BoneIndex{}; BoneIndex < mSkinInfluenceBounds.size(); ++BoneIndex) {
            const std::optional<FBox>& InfluenceBounds{mSkinInfluenceBounds[BoneIndex]};

            if (InfluenceBounds.has_value()) {
                IncludeBounds(Bounds, InfluenceBounds->Transformed(SkinMatrices[BoneIndex] * ComponentToWorld));
            }
        }

        if (!Bounds.has_value()) {
            return GetBounds().Transformed(ComponentToWorld);
        }

        const FVector Magnitude{Bounds->mMinimum.Abs().Max(Bounds->mMaximum.Abs()).Max(FVector{1.0f, 1.0f, 1.0f})};
        const FVector Padding{Magnitude * (32.0f * std::numeric_limits<float>::epsilon())};

        return FBox{Bounds->mMinimum - Padding, Bounds->mMaximum + Padding};
    }

    /**
     * @brief 메시 전체를 하나의 바디로 다룰 때 사용할 충돌 형상을 반환합니다.
     */
    const PhysicsRuntime::UBodySetup* Asset::USkeletalMesh::GetBodySetup() const {
        // 바인드 형상을 보관하는 정적 메시가 경계 상자 바디 셋업을 함께 만듭니다.
        return mGeometry.GetBodySetup();
    }

    /**
     * @brief 정점 수를 반환합니다.
     */
    Uint32 Asset::USkeletalMesh::GetVertexCount() const {
        return mGeometry.GetVertexCount();
    }

    /**
     * @brief 인덱스 수를 반환합니다.
     */
    Uint32 Asset::USkeletalMesh::GetIndexCount() const {
        return mGeometry.GetIndexCount();
    }

    /**
     * @brief 스키닝에 필요한 형상과 본 데이터가 있는지 검사합니다.
     */
    bool Asset::USkeletalMesh::IsValid() const {
        return mGeometry.IsValid() && mSkinWeights.size() == GetVertexCount();
    }

    /**
     * @brief 이 메시가 참조하는 물리 에셋 경로를 반환합니다.
     */
    const Core::FString& Asset::USkeletalMesh::GetPhysicsAssetPath() const {
        return mPhysicsAssetPath;
    }

    /**
     * @brief 이 메시가 참조할 물리 에셋 경로를 설정합니다.
     */
    void Asset::USkeletalMesh::SetPhysicsAssetPath(Core::FString Path) {
        mPhysicsAssetPath = std::move(Path);
    }

    /**
     * @brief 물리 에셋이 지정되지 않았을 때 쓸 물리 에셋을 뼈와 정점으로 만들어 반환합니다.
     */
    PhysicsRuntime::UPhysicsAsset* Asset::USkeletalMesh::GetGeneratedPhysicsAsset(const Asset::USkeleton& Skeleton) {
        if (mGeneratedPhysicsAsset != nullptr && mGeneratedPhysicsAssetSkeleton == &Skeleton) {
            return mGeneratedPhysicsAsset.get();
        }

        auto PhysicsAsset{CoreUObject::NewObject<PhysicsRuntime::UPhysicsAsset>()};

        if (!Asset::FPhysicsAssetUtils::CreateFromSkeletalMesh(*PhysicsAsset, *this, Skeleton, Asset::FPhysAssetCreateParams{})) {
            return nullptr;
        }

        mGeneratedPhysicsAsset = std::move(PhysicsAsset);
        mGeneratedPhysicsAssetSkeleton = &Skeleton;

        return mGeneratedPhysicsAsset.get();
    }

    /**
     * @brief 천 에셋을 메시에 추가합니다.
     */
    Index_t Asset::USkeletalMesh::AddClothingAsset(std::unique_ptr<PhysicsRuntime::UClothingAssetCommon> ClothingAsset) {
        if (ClothingAsset == nullptr) {
            return InvalidIndex;
        }

        mMeshClothingAssets.push_back(std::move(ClothingAsset));

        return mMeshClothingAssets.size() - 1;
    }

    /**
     * @brief 메시가 소유한 천 에셋 목록을 반환합니다.
     */
    const Core::TArray<std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>>& Asset::USkeletalMesh::GetMeshClothingAssets() const {
        return mMeshClothingAssets;
    }

    /**
     * @brief 식별자로 천 에셋을 찾습니다.
     */
    PhysicsRuntime::UClothingAssetCommon* Asset::USkeletalMesh::GetClothingAsset(const Core::FGuid& AssetGuid) const {
        for (const std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>& ClothingAsset : mMeshClothingAssets) {
            if (ClothingAsset->GetGuid() == AssetGuid) {
                return ClothingAsset.get();
            }
        }

        return nullptr;
    }

    /**
     * @brief 섹션의 천 연결 데이터를 반환합니다.
     */
    const Asset::FClothingSectionData* Asset::USkeletalMesh::GetSectionClothingData(Index_t SubMeshIndex) const {
        if (SubMeshIndex >= GetMeshData().GetSubMeshes().size()) {
            return nullptr;
        }

        // 아직 연결한 적 없는 섹션은 빈 연결 데이터로 답합니다.
        static const Asset::FClothingSectionData EmptyClothingData{};

        return SubMeshIndex < mSectionClothingData.size() ? &mSectionClothingData[SubMeshIndex] : &EmptyClothingData;
    }

    /**
     * @brief 섹션의 천 연결 데이터를 저장합니다.
     */
    bool Asset::USkeletalMesh::SetSectionClothingData(Index_t SubMeshIndex, Asset::FClothingSectionData ClothingData) {
        const Index_t NumSubMeshes{GetMeshData().GetSubMeshes().size()};

        if (SubMeshIndex >= NumSubMeshes) {
            return false;
        }

        if (mSectionClothingData.size() < NumSubMeshes) {
            mSectionClothingData.resize(NumSubMeshes);
        }

        mSectionClothingData[SubMeshIndex] = std::move(ClothingData);

        // 정점이 붙는 입자 삼각형이 바뀌었으므로 다음 리소스 준비 때 천 정점 버퍼를 다시 만듭니다.
        mClothVertexBuffer.Reset();

        return true;
    }

    /**
     * @brief 천 에셋을 메시에서 빼고, 그 에셋을 따르던 섹션의 연결을 지웁니다.
     */
    bool Asset::USkeletalMesh::RemoveClothingAsset(const Core::FGuid& AssetGuid) {
        const auto Found{std::find_if(mMeshClothingAssets.begin(), mMeshClothingAssets.end(), [&AssetGuid](const std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>& ClothingAsset) {
            return ClothingAsset != nullptr && ClothingAsset->GetGuid() == AssetGuid;
        })};

        if (Found == mMeshClothingAssets.end()) {
            return false;
        }

        const Index_t RemovedIndex{static_cast<Index_t>(Found - mMeshClothingAssets.begin())};
        mMeshClothingAssets.erase(Found);

        for (Asset::FClothingSectionData& SectionData : mSectionClothingData) {
            if (SectionData.mAssetGuid == AssetGuid) {
                SectionData = Asset::FClothingSectionData{};
            } else if (SectionData.IsValid() && SectionData.mCorrespondClothAssetIndex != InvalidIndex && SectionData.mCorrespondClothAssetIndex > RemovedIndex) {
                --SectionData.mCorrespondClothAssetIndex;
            }
        }

        // 정점이 따르던 입자 삼각형이 바뀌었으므로 다음 리소스 준비 때 천 정점 버퍼를 다시 만듭니다.
        mClothVertexBuffer.Reset();

        return true;
    }

    /**
     * @brief 천 에셋과 섹션 연결 데이터를 저장하거나 불러옵니다.
     */
    void Asset::USkeletalMesh::SerializeClothingData(Core::FArchive& Archive) {
        constexpr Uint32 MaxClothingAssets{4096u};
        Uint32 NumAssets{Archive.IsLoading() ? 0u : static_cast<Uint32>(mMeshClothingAssets.size())};

        if (Archive.HasError() || !Archive.BeginArray("ClothingAssets", NumAssets) || NumAssets > MaxClothingAssets) {
            Archive.SetError("Invalid clothing asset count");

            return;
        }

        if (Archive.IsLoading()) {
            mMeshClothingAssets.clear();

            for (Uint32 AssetIndex{}; AssetIndex < NumAssets; ++AssetIndex) {
                mMeshClothingAssets.push_back(CoreUObject::NewObject<PhysicsRuntime::UClothingAssetCommon>());
            }
        }

        for (const std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>& ClothingAsset : mMeshClothingAssets) {
            ClothingAsset->SerializeClothingData(Archive);

            if (Archive.HasError()) {
                return;
            }
        }

        Archive.EndArray();

        Uint32 NumSections{Archive.IsLoading() ? 0u : static_cast<Uint32>(mSectionClothingData.size())};

        if (Archive.HasError() || !Archive.BeginArray("SectionClothingData", NumSections) || NumSections > static_cast<Uint32>(GetMeshData().GetSubMeshes().size())) {
            Archive.SetError("Invalid clothing section count");

            return;
        }

        if (Archive.IsLoading()) {
            mSectionClothingData.assign(NumSections, Asset::FClothingSectionData{});
        }

        for (Asset::FClothingSectionData& SectionData : mSectionClothingData) {
            SectionData.Serialize(Archive);

            if (Archive.HasError()) {
                return;
            }
        }

        Archive.EndArray();

        if (Archive.IsLoading()) {
            mClothVertexBuffer.Reset();
        }
    }

    /**
     * @brief 섹션에 연결된 천 에셋을 모읍니다.
     */
    void Asset::USkeletalMesh::GetClothingAssetsInUse(Core::TArray<const PhysicsRuntime::UClothingAssetCommon*>& OutClothingAssets) const {
        OutClothingAssets.clear();

        for (const Asset::FClothingSectionData& SectionData : mSectionClothingData) {
            if (!SectionData.IsValid()) {
                continue;
            }

            const PhysicsRuntime::UClothingAssetCommon* const ClothingAsset{GetClothingAsset(SectionData.mAssetGuid)};

            if (ClothingAsset != nullptr && std::find(OutClothingAssets.begin(), OutClothingAssets.end(), ClothingAsset) == OutClothingAssets.end()) {
                OutClothingAssets.push_back(ClothingAsset);
            }
        }
    }

}
