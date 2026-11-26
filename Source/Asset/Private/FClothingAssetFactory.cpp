/**
 * @file FClothingAssetFactory.cpp
 * @brief 스켈레탈 메시의 섹션으로 천 에셋을 만들고 섹션에 연결하는 함수를 구현합니다.
 * @ingroup Asset
 */

#include "Asset/FClothingAssetFactory.h"
#include "Asset/FClothingSectionData.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Core/Log/Log.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "PhysicsRuntime/ClothingMeshUtils.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Asset {

    /**
     * @brief 천 에셋 생성 로그 범주와 내부 함수입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogClothingAssetFactory, Log, All);

        /**
         * @brief 같은 위치로 보는 두 정점 사이 거리의 제곱(m²) 기준입니다.
         */
        constexpr float WeldThresholdSquared{1.e-16f};

        /**
         * @brief 넓이가 없는 삼각형으로 판단하는 외적 제곱 길이(m⁴)의 기준입니다.
         * @details 센티미터 좌표에서 쓰던 1e-8을 미터 좌표로 바꾼 값입니다.
         */
        constexpr float DegenerateCrossSquaredThreshold{1.e-16f};

        /**
         * @brief 섹션이 쓰는 메시 정점을 처음 나온 순서로 모으고 삼각형을 섹션 번호로 바꿉니다.
         * @param[in] MeshData 섹션을 가진 메시 형상입니다.
         * @param[in] SubMeshIndex 섹션 번호입니다.
         * @param[out] OutSectionVertexIndices 섹션 정점 순서대로의 메시 정점 번호입니다.
         * @param[out] OutSectionIndices 섹션 정점 번호로 바꾼 삼각형 인덱스입니다.
         * @return 섹션이 있고 삼각형 인덱스가 올바르면 true입니다.
         */
        bool GatherSectionVertices(const FMeshData& MeshData, Index_t SubMeshIndex, Core::TArray<Uint32>& OutSectionVertexIndices, Core::TArray<Uint32>& OutSectionIndices) {
            OutSectionVertexIndices.clear();
            OutSectionIndices.clear();

            const Core::TArray<FSubMesh>& SubMeshes{MeshData.GetSubMeshes()};
            const Core::TArray<Uint32>& Indices{MeshData.GetIndices()};
            const Index_t NumMeshVerts{MeshData.GetPositions().size()};

            if (SubMeshIndex >= SubMeshes.size()) {
                return false;
            }

            const FSubMesh& Section{SubMeshes[SubMeshIndex]};
            const Index_t FirstIndex{Section.GetFirstIndex()};
            const Index_t IndexCount{Section.GetIndexCount()};

            if (IndexCount == 0 || IndexCount % 3 != 0 || FirstIndex + IndexCount > Indices.size()) {
                return false;
            }

            Core::TArray<Uint32> MeshToSection(NumMeshVerts, std::numeric_limits<Uint32>::max());
            OutSectionIndices.reserve(IndexCount);

            for (Index_t Index{FirstIndex}; Index < FirstIndex + IndexCount; ++Index) {
                const Uint32 MeshVertIndex{Indices[Index]};

                if (MeshVertIndex >= NumMeshVerts) {
                    return false;
                }

                if (MeshToSection[MeshVertIndex] == std::numeric_limits<Uint32>::max()) {
                    MeshToSection[MeshVertIndex] = static_cast<Uint32>(OutSectionVertexIndices.size());
                    OutSectionVertexIndices.push_back(MeshVertIndex);
                }

                OutSectionIndices.push_back(MeshToSection[MeshVertIndex]);
            }

            return true;
        }
    }

    /**
     * @brief 섹션 하나를 천 메시로 바꾼 새 천 에셋을 만듭니다.
     */
    std::unique_ptr<PhysicsRuntime::UClothingAssetCommon> FClothingAssetFactory::CreateFromSkeletalMesh(const USkeletalMesh& TargetMesh, const USkeleton& Skeleton, Index_t SubMeshIndex, PhysicsRuntime::UPhysicsAsset* PhysicsAsset) {
        if (SubMeshIndex >= TargetMesh.GetMeshData().GetSubMeshes().size()) {
            return nullptr;
        }

        // 이미 천을 따르는 섹션은 다시 천으로 바꾸지 않습니다.
        const FClothingSectionData* const SectionClothingData{TargetMesh.GetSectionClothingData(SubMeshIndex)};

        if (SectionClothingData != nullptr && SectionClothingData->IsValid()) {
            return nullptr;
        }

        auto NewAsset{CoreUObject::NewObject<PhysicsRuntime::UClothingAssetCommon>()};
        const Index_t LodIndex{NewAsset->AddNewLod()};

        if (!ImportToLodInternal(TargetMesh, Skeleton, SubMeshIndex, *NewAsset, NewAsset->mLodData[LodIndex])) {
            return nullptr;
        }

        Core::TArray<Core::FString> SkeletonBoneNames{};
        SkeletonBoneNames.reserve(Skeleton.GetBones().size());

        for (const FMeshImportBone& Bone : Skeleton.GetBones()) {
            SkeletonBoneNames.push_back(Bone.GetName());
        }

        // 물리 에셋을 정하면서 몸통 뼈까지 사용 뼈에 넣고 뼈 번호를 다시 찾습니다.
        NewAsset->SetPhysicsAsset(PhysicsAsset, SkeletonBoneNames);
        NewAsset->InvalidateCachedData();
        NewAsset->BuildSelfCollisionData();

        return NewAsset;
    }

    /**
     * @brief 섹션의 정점을 위치로 합쳐 천 상세 단계 하나를 채웁니다.
     */
    bool FClothingAssetFactory::ImportToLodInternal(const USkeletalMesh& SourceMesh, const USkeleton& Skeleton, Index_t SubMeshIndex, PhysicsRuntime::UClothingAssetCommon& DestAsset, PhysicsRuntime::FClothLODDataCommon& DestLod) {
        const FMeshData& MeshData{SourceMesh.GetMeshData()};
        Core::TArray<Uint32> SectionVertexIndices{};
        Core::TArray<Uint32> SectionIndices{};

        if (!GatherSectionVertices(MeshData, SubMeshIndex, SectionVertexIndices, SectionIndices)) {
            MACAW_LOG(LogClothingAssetFactory, Warning, "Section {} has no valid triangles.", SubMeshIndex);

            return false;
        }

        const Core::TArray<FVector>& Positions{MeshData.GetPositions()};
        const Core::TArray<FVector>& Normals{MeshData.GetNormals()};
        const Core::TArray<FVector4>& Colors{MeshData.GetColors()};
        const Core::TArray<FMeshImportSkinWeight>& SkinWeights{SourceMesh.GetSkinWeights()};
        const Core::TArray<FMeshImportBone>& Bones{Skeleton.GetBones()};
        const Index_t NumVerts{SectionVertexIndices.size()};
        const Index_t NumIndices{SectionIndices.size()};

        // 법선이나 UV가 달라 갈라진 정점을 위치로 합쳐야 천이 그 경계에서 찢어지지 않습니다.
        Core::TArray<FVector> UniqueVerts{};
        Core::TArray<Index_t> OriginalIndexes{};
        Core::TArray<Uint32> IndexRemap(NumVerts);

        for (Index_t VertIndex{}; VertIndex < NumVerts; ++VertIndex) {
            const FVector& SourcePosition{Positions[SectionVertexIndices[VertIndex]]};
            Index_t RemapIndex{InvalidIndex};

            for (Index_t UniqueVertIndex{}; UniqueVertIndex < UniqueVerts.size(); ++UniqueVertIndex) {
                if ((UniqueVerts[UniqueVertIndex] - SourcePosition).LengthSquared() <= WeldThresholdSquared) {
                    RemapIndex = UniqueVertIndex;

                    break;
                }
            }

            if (RemapIndex == InvalidIndex) {
                UniqueVerts.push_back(SourcePosition);
                OriginalIndexes.push_back(VertIndex);
                RemapIndex = UniqueVerts.size() - 1;
            }

            IndexRemap[VertIndex] = static_cast<Uint32>(RemapIndex);
        }

        const Index_t NumUniqueVerts{UniqueVerts.size()};
        PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{DestLod.mPhysicalMeshData};
        PhysMesh.Reset(NumUniqueVerts, NumIndices);

        for (Index_t VertexIndex{}; VertexIndex < NumUniqueVerts; ++VertexIndex) {
            const Uint32 MeshVertIndex{SectionVertexIndices[OriginalIndexes[VertexIndex]]};

            PhysMesh.mVertices[VertexIndex] = Positions[MeshVertIndex];

            if (MeshVertIndex < Normals.size()) {
                PhysMesh.mNormals[VertexIndex] = Normals[MeshVertIndex];
            }

            if (MeshVertIndex < Colors.size()) {
                PhysMesh.mVertexColors[VertexIndex] = Colors[MeshVertIndex];
            }

            // 가중치가 0인 영향은 건너뛰고, 뼈는 이름으로 사용 뼈 목록에 넣어 그 번호를 저장합니다.
            const FMeshImportSkinWeight& SkinWeight{SkinWeights[MeshVertIndex]};
            PhysicsRuntime::FClothVertBoneData& BoneData{PhysMesh.mBoneData[VertexIndex]};
            Index_t NumInfluences{};

            for (Index_t InfluenceIndex{}; InfluenceIndex < FMeshImportSkinWeight::MaxInfluences && NumInfluences < PhysicsRuntime::MaxTotalInfluences; ++InfluenceIndex) {
                const float Weight{SkinWeight.GetWeights()[InfluenceIndex]};
                const Uint32 BoneIndex{SkinWeight.GetBoneIndices()[InfluenceIndex]};

                if (Weight <= 0.0f) {
                    continue;
                }

                if (BoneIndex >= Bones.size()) {
                    MACAW_LOG(LogClothingAssetFactory, Warning, "Vertex {} uses bone {} that the skeleton does not have.", MeshVertIndex, BoneIndex);

                    return false;
                }

                const Core::FString& BoneName{Bones[BoneIndex].GetName()};
                auto Found{std::find(DestAsset.mUsedBoneNames.begin(), DestAsset.mUsedBoneNames.end(), BoneName)};

                if (Found == DestAsset.mUsedBoneNames.end()) {
                    DestAsset.mUsedBoneNames.push_back(BoneName);
                    Found = DestAsset.mUsedBoneNames.end() - 1;
                }

                BoneData.mBoneIndices[NumInfluences] = static_cast<Uint32>(Found - DestAsset.mUsedBoneNames.begin());
                BoneData.mBoneWeights[NumInfluences] = Weight;
                ++NumInfluences;
            }

            PhysMesh.mMaxBoneWeights = std::max(PhysMesh.mMaxBoneWeights, NumInfluences);
        }

        // 최대 거리를 모두 0으로 두어 칠하기 전까지는 모든 입자가 고정됩니다.
        PhysicsRuntime::FPointWeightMap& PhysMeshMaxDistances{PhysMesh.AddWeightMap(PhysicsRuntime::EWeightMapTargetCommon::MaxDistance)};
        PhysMeshMaxDistances.Initialize(NumUniqueVerts);

        DestLod.mPointWeightMaps.emplace_back().Initialize(PhysMeshMaxDistances, PhysicsRuntime::EWeightMapTargetCommon::MaxDistance);

        for (Index_t IndexIndex{}; IndexIndex < NumIndices; ++IndexIndex) {
            PhysMesh.mIndices[IndexIndex] = IndexRemap[SectionIndices[IndexIndex]];
        }

        // 넓이가 없는 삼각형이 있으면 천 시뮬레이션이 실패하므로 만들지 않습니다.
        for (Index_t TriangleBaseIndex{}; TriangleBaseIndex < NumIndices; TriangleBaseIndex += 3) {
            const FVector& A{PhysMesh.mVertices[PhysMesh.mIndices[TriangleBaseIndex]]};
            const FVector& B{PhysMesh.mVertices[PhysMesh.mIndices[TriangleBaseIndex + 1]]};
            const FVector& C{PhysMesh.mVertices[PhysMesh.mIndices[TriangleBaseIndex + 2]]};

            if ((B - A).Cross(C - A).LengthSquared() <= DegenerateCrossSquaredThreshold) {
                MACAW_LOG(LogClothingAssetFactory, Warning, "Failed to generate the cloth sim mesh. Triangle {} of section {} is degenerate.", TriangleBaseIndex / 3, SubMeshIndex);

                return false;
            }
        }

        return true;
    }

    /**
     * @brief 섹션이 천 에셋의 상세 단계를 따르도록 정점 대응을 만들어 연결합니다.
     */
    bool FClothingAssetFactory::BindToSkeletalMesh(USkeletalMesh& SkelMesh, PhysicsRuntime::UClothingAssetCommon& ClothingAsset, Index_t SubMeshIndex, Index_t AssetLodIndex) {
        const auto& ClothingAssets{SkelMesh.GetMeshClothingAssets()};
        const auto FoundAsset{std::find_if(ClothingAssets.begin(), ClothingAssets.end(), [&ClothingAsset](const auto& OwnedAsset) {
            return OwnedAsset.get() == &ClothingAsset;
        })};

        if (FoundAsset == ClothingAssets.end()) {
            MACAW_LOG(LogClothingAssetFactory, Warning, "Failed to bind a clothing asset. The mesh does not own it.");

            return false;
        }

        if (!ClothingAsset.IsValidLod(AssetLodIndex)) {
            MACAW_LOG(LogClothingAssetFactory, Warning, "Failed to bind clothing LOD{}. The LOD does not exist.", AssetLodIndex);

            return false;
        }

        // 메시는 상세 단계가 하나뿐이므로 천 상세 단계는 메시 상세 단계 0에만 연결합니다.
        constexpr Index_t MeshLodIndex{0};

        if (std::find(ClothingAsset.mLodMap.begin(), ClothingAsset.mLodMap.end(), AssetLodIndex) != ClothingAsset.mLodMap.end()) {
            MACAW_LOG(LogClothingAssetFactory, Warning, "Failed to bind clothing LOD{}. It is already mapped to a mesh LOD.", AssetLodIndex);

            return false;
        }

        if (MeshLodIndex < ClothingAsset.mLodMap.size() && ClothingAsset.mLodMap[MeshLodIndex] != InvalidIndex) {
            return false;
        }

        const FMeshData& MeshData{SkelMesh.GetMeshData()};
        Core::TArray<Uint32> SectionVertexIndices{};
        Core::TArray<Uint32> SectionIndices{};

        if (!GatherSectionVertices(MeshData, SubMeshIndex, SectionVertexIndices, SectionIndices)) {
            MACAW_LOG(LogClothingAssetFactory, Warning, "Failed to bind a clothing asset. Section {} has no valid triangles.", SubMeshIndex);

            return false;
        }

        ClothingAsset.CalculateReferenceBoneIndex();

        // 섹션 정점마다 천 메시의 가장 가까운 삼각형 위 위치를 구합니다.
        const Core::TArray<FVector>& Positions{MeshData.GetPositions()};
        const Core::TArray<FVector>& Normals{MeshData.GetNormals()};
        const Core::TArray<FVector4>& Tangents{MeshData.GetTangents()};
        const bool HasTangents{Tangents.size() == Positions.size()};
        Core::TArray<FVector> RenderPositions{};
        Core::TArray<FVector> RenderNormals{};
        Core::TArray<FVector> RenderTangents{};

        RenderPositions.reserve(SectionVertexIndices.size());
        RenderNormals.reserve(SectionVertexIndices.size());
        RenderTangents.reserve(SectionVertexIndices.size());

        for (const Uint32 MeshVertIndex : SectionVertexIndices) {
            RenderPositions.push_back(Positions[MeshVertIndex]);
            RenderNormals.push_back(MeshVertIndex < Normals.size() ? Normals[MeshVertIndex] : FVector{0.0f, 0.0f, 1.0f});

            if (HasTangents) {
                RenderTangents.push_back(Tangents[MeshVertIndex].ToVector3());
            }
        }

        const PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{ClothingAsset.mLodData[AssetLodIndex].mPhysicalMeshData};
        const PhysicsRuntime::ClothingMeshUtils::ClothMeshDesc TargetMesh{RenderPositions, RenderNormals, SectionIndices};
        const PhysicsRuntime::ClothingMeshUtils::ClothMeshDesc SourceMesh{PhysMesh.mVertices, PhysMesh.mNormals, PhysMesh.mIndices};
        Core::TArray<PhysicsRuntime::FMeshToMeshVertData> MeshToMeshData{};

        PhysicsRuntime::ClothingMeshUtils::GenerateMeshToMeshSkinningData(MeshToMeshData, TargetMesh, HasTangents ? &RenderTangents : nullptr, SourceMesh);

        if (MeshToMeshData.empty()) {
            return false;
        }

        // 세 입자가 모두 고정된 삼각형에 붙은 정점은 시뮬레이션 결과 대신 스키닝만 씁니다.
        const PhysicsRuntime::FPointWeightMap* const MaxDistances{PhysMesh.FindWeightMap(PhysicsRuntime::EWeightMapTargetCommon::MaxDistance)};

        if (MaxDistances != nullptr && MaxDistances->Num() > 0) {
            for (PhysicsRuntime::FMeshToMeshVertData& VertData : MeshToMeshData) {
                if (MaxDistances->AreAllBelowThreshold(VertData.mSourceMeshVertIndices[0], VertData.mSourceMeshVertIndices[1], VertData.mSourceMeshVertIndices[2])) {
                    VertData.mSourceMeshVertIndices[3] = PhysicsRuntime::FMeshToMeshVertData::NoSimulationFlag;
                }
            }
        }

        FClothingSectionData SectionClothingData{};
        SectionClothingData.mAssetGuid = ClothingAsset.GetGuid();
        SectionClothingData.mAssetLodIndex = AssetLodIndex;
        SectionClothingData.mCorrespondClothAssetIndex = static_cast<Index_t>(FoundAsset - ClothingAssets.begin());
        SectionClothingData.mClothMappingData = std::move(MeshToMeshData);
        SectionClothingData.mSectionVertexIndices = std::move(SectionVertexIndices);

        if (!SkelMesh.SetSectionClothingData(SubMeshIndex, std::move(SectionClothingData))) {
            return false;
        }

        if (ClothingAsset.mLodMap.size() <= MeshLodIndex) {
            ClothingAsset.mLodMap.resize(MeshLodIndex + 1, InvalidIndex);
        }

        ClothingAsset.mLodMap[MeshLodIndex] = AssetLodIndex;

        return true;
    }
}
