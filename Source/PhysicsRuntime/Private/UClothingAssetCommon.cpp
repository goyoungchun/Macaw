/**
 * @file UClothingAssetCommon.cpp
 * @brief 스켈레탈 메시의 섹션 하나를 천으로 시뮬레이션할 데이터를 담은 에셋을 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/UClothingAssetCommon.h"
#include "PhysicsRuntime/ClothingMeshUtils.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Core/Archive.h"

#include <algorithm>

namespace PhysicsRuntime {

    namespace {
        /**
         * @brief 한 배열에 허용하는 최대 원소 수이며 깨진 파일이 큰 메모리를 잡지 않게 합니다.
         */
        constexpr Uint32 MaxSerializedCount{50000000u};

        /**
         * @brief 배열 개수를 저장하거나 읽고 원소마다 직렬화 함수를 부릅니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         * @param[in] Name 배열 이름입니다.
         * @param[in,out] Values 저장하거나 채울 배열이며 불러올 때 개수만큼 기본값으로 채웁니다.
         * @param[in] SerializeElement 원소 하나를 직렬화하는 함수입니다.
         */
        template <typename TElement, typename TFunction>
        void SerializeArray(Core::FArchive& Archive, std::string_view Name, Core::TArray<TElement>& Values, TFunction&& SerializeElement) {
            Uint32 Count{Archive.IsLoading() ? 0u : static_cast<Uint32>(Values.size())};

            if (Archive.HasError() || !Archive.BeginArray(Name, Count) || Count > MaxSerializedCount) {
                Archive.SetError("Invalid clothing array count");

                return;
            }

            if (Archive.IsLoading()) {
                Values.clear();
                Values.resize(Count);
            }

            for (TElement& Value : Values) {
                SerializeElement(Value);

                if (Archive.HasError()) {
                    return;
                }
            }

            Archive.EndArray();
        }

        /**
         * @brief 번호 하나를 64비트로 저장하거나 읽습니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         * @param[in] Name 값 이름입니다.
         * @param[in,out] Value 저장하거나 채울 번호입니다.
         */
        void SerializeIndex(Core::FArchive& Archive, std::string_view Name, Index_t& Value) {
            Uint64 Stored{static_cast<Uint64>(Value)};

            Archive.Serialize(Name, Stored);

            if (Archive.IsLoading()) {
                Value = static_cast<Index_t>(Stored);
            }
        }

        /**
         * @brief 가중치 맵 하나를 저장하거나 읽습니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         * @param[in,out] Map 저장하거나 채울 맵입니다.
         */
        void SerializeWeightMap(Core::FArchive& Archive, FPointWeightMap& Map) {
            SerializeArray(Archive, "Values", Map.mValues, [&Archive](float& Value) {
                Archive.Serialize("Value", Value);
            });
            Archive.Serialize("Name", Map.mName);

            Uint32 Target{static_cast<Uint32>(Map.mCurrentTarget)};
            Archive.Serialize("Target", Target);
            Archive.Serialize("Enabled", Map.mEnabled);

            if (Archive.IsLoading()) {
                Map.mCurrentTarget = static_cast<EWeightMapTargetCommon>(Target);
            }
        }

        /**
         * @brief 충돌체 목록을 저장하거나 읽습니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         * @param[in,out] Data 저장하거나 채울 목록입니다.
         */
        void SerializeCollisionData(Core::FArchive& Archive, FClothCollisionData& Data) {
            SerializeArray(Archive, "Spheres", Data.mSpheres, [&Archive](FClothCollisionPrim_Sphere& Sphere) {
                SerializeIndex(Archive, "BoneIndex", Sphere.mBoneIndex);
                Archive.Serialize("Radius", Sphere.mRadius);
                Archive.Serialize("LocalPosition", Sphere.mLocalPosition);
            });
            SerializeArray(Archive, "SphereConnections", Data.mSphereConnections, [&Archive](FClothCollisionPrim_SphereConnection& Connection) {
                SerializeIndex(Archive, "Sphere0", Connection.mSphereIndices[0]);
                SerializeIndex(Archive, "Sphere1", Connection.mSphereIndices[1]);
            });
            SerializeArray(Archive, "Convexes", Data.mConvexes, [&Archive](FClothCollisionPrim_Convex& Convex) {
                SerializeArray(Archive, "Planes", Convex.mPlanes, [&Archive](FVector4& Plane) {
                    Archive.Serialize("Plane", Plane);
                });
                SerializeIndex(Archive, "BoneIndex", Convex.mBoneIndex);
            });
        }

        /**
         * @brief 시뮬레이션 메시 하나를 저장하거나 읽습니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         * @param[in,out] Mesh 저장하거나 채울 시뮬레이션 메시입니다.
         */
        void SerializePhysicalMesh(Core::FArchive& Archive, FClothPhysicalMeshData& Mesh) {
            SerializeArray(Archive, "Vertices", Mesh.mVertices, [&Archive](FVector& Vertex) {
                Archive.Serialize("Vertex", Vertex);
            });
            SerializeArray(Archive, "Normals", Mesh.mNormals, [&Archive](FVector& Normal) {
                Archive.Serialize("Normal", Normal);
            });
            SerializeArray(Archive, "VertexColors", Mesh.mVertexColors, [&Archive](FVector4& Color) {
                Archive.Serialize("Color", Color);
            });
            SerializeArray(Archive, "Indices", Mesh.mIndices, [&Archive](Uint32& Index) {
                Archive.Serialize("Index", Index);
            });

            // 가중치 맵은 대상 번호를 키로 저장합니다.
            Core::TArray<std::pair<Uint32, FPointWeightMap>> WeightMaps{};

            if (!Archive.IsLoading()) {
                WeightMaps.assign(Mesh.mWeightMaps.begin(), Mesh.mWeightMaps.end());
            }

            SerializeArray(Archive, "WeightMaps", WeightMaps, [&Archive](std::pair<Uint32, FPointWeightMap>& Entry) {
                Archive.Serialize("Key", Entry.first);
                SerializeWeightMap(Archive, Entry.second);
            });

            if (Archive.IsLoading()) {
                Mesh.mWeightMaps.clear();
                Mesh.mWeightMaps.insert(WeightMaps.begin(), WeightMaps.end());
            }

            SerializeArray(Archive, "InverseMasses", Mesh.mInverseMasses, [&Archive](float& InverseMass) {
                Archive.Serialize("InverseMass", InverseMass);
            });
            SerializeArray(Archive, "BoneData", Mesh.mBoneData, [&Archive](FClothVertBoneData& BoneData) {
                SerializeIndex(Archive, "NumInfluences", BoneData.mNumInfluences);

                for (Index_t Influence{}; Influence < BoneData.mBoneIndices.size(); ++Influence) {
                    Archive.Serialize("BoneIndex", BoneData.mBoneIndices[Influence]);
                    Archive.Serialize("BoneWeight", BoneData.mBoneWeights[Influence]);
                }
            });
            SerializeIndex(Archive, "MaxBoneWeights", Mesh.mMaxBoneWeights);
            SerializeIndex(Archive, "NumFixedVerts", Mesh.mNumFixedVerts);
            SerializeArray(Archive, "SelfCollisionIndices", Mesh.mSelfCollisionIndices, [&Archive](Uint32& Index) {
                Archive.Serialize("Index", Index);
            });
        }
    }

    /**
     * @brief 정점 하나의 삼각형 대응을 저장하거나 불러옵니다.
     */
    void SerializeMeshToMeshVertData(Core::FArchive& Archive, FMeshToMeshVertData& Data) {
        Archive.Serialize("PositionBaryCoordsAndDist", Data.mPositionBaryCoordsAndDist);
        Archive.Serialize("NormalBaryCoordsAndDist", Data.mNormalBaryCoordsAndDist);
        Archive.Serialize("TangentBaryCoordsAndDist", Data.mTangentBaryCoordsAndDist);

        for (Uint32& SourceIndex : Data.mSourceMeshVertIndices) {
            Archive.Serialize("SourceIndex", SourceIndex);
        }

        Archive.Serialize("Weight", Data.mWeight);
    }

    /**
     * @brief 에셋 내부 상수입니다.
     */
    namespace {
        /**
         * @brief 최대 거리가 이 값보다 작으면 고정 입자로 봅니다.
         */
        constexpr float KinematicThreshold{1.e-8f};
    }

    /**
     * @brief 상세 단계가 없는 빈 에셋을 구성합니다.
     */
    UClothingAssetCommon::UClothingAssetCommon()
        : mClothConfig{CoreUObject::NewObject<UClothConfigNv>()} {
    }

    /**
     * @brief 상세 단계 데이터를 정리합니다.
     */
    UClothingAssetCommon::~UClothingAssetCommon() = default;

    /**
     * @brief 빈 상세 단계를 하나 추가합니다.
     */
    Index_t UClothingAssetCommon::AddNewLod() {
        mLodData.emplace_back();

        return mLodData.size() - 1;
    }

    /**
     * @brief 상세 단계 번호가 유효한지 검사합니다.
     */
    bool UClothingAssetCommon::IsValidLod(Index_t LodIndex) const {
        return LodIndex < mLodData.size();
    }

    /**
     * @brief 상세 단계 수를 반환합니다.
     */
    Index_t UClothingAssetCommon::GetNumLods() const {
        return mLodData.size();
    }

    /**
     * @brief 역질량, 고정 입자 수, 뼈 영향 수를 다시 구합니다.
     */
    void UClothingAssetCommon::InvalidateCachedData() {
        for (FClothLODDataCommon& CurrentLodData : mLodData) {
            FClothPhysicalMeshData& PhysMesh{CurrentLodData.mPhysicalMeshData};
            Core::TArray<float>& InvMasses{PhysMesh.mInverseMasses};
            const Index_t NumVerts{PhysMesh.mVertices.size()};

            // 입자마다 닿은 삼각형 넓이를 더해 질량으로 씁니다.
            InvMasses.assign(NumVerts, 0.0f);

            for (Index_t TriangleBaseIndex{}; TriangleBaseIndex + 2 < PhysMesh.mIndices.size(); TriangleBaseIndex += 3) {
                const Uint32 Index0{PhysMesh.mIndices[TriangleBaseIndex]};
                const Uint32 Index1{PhysMesh.mIndices[TriangleBaseIndex + 1]};
                const Uint32 Index2{PhysMesh.mIndices[TriangleBaseIndex + 2]};
                const FVector AB{PhysMesh.mVertices[Index1] - PhysMesh.mVertices[Index0]};
                const FVector AC{PhysMesh.mVertices[Index2] - PhysMesh.mVertices[Index0]};
                const float TriangleArea{AB.Cross(AC).Length()};

                InvMasses[Index0] += TriangleArea;
                InvMasses[Index1] += TriangleArea;
                InvMasses[Index2] += TriangleArea;
            }

            PhysMesh.mNumFixedVerts = 0;

            const FPointWeightMap* const MaxDistances{PhysMesh.FindWeightMap(EWeightMapTargetCommon::MaxDistance)};
            const bool HasMaxDistances{MaxDistances != nullptr && MaxDistances->Num() > 0};
            float MassSum{};

            for (Index_t VertIndex{}; VertIndex < NumVerts; ++VertIndex) {
                if (HasMaxDistances && (*MaxDistances)[VertIndex] < KinematicThreshold) {
                    InvMasses[VertIndex] = 0.0f;
                    ++PhysMesh.mNumFixedVerts;
                } else {
                    MassSum += InvMasses[VertIndex];
                }
            }

            // 움직이는 입자의 평균 질량이 1이 되도록 맞춘 뒤 역수를 취합니다.
            if (MassSum > 0.0f) {
                const float MassScale{static_cast<float>(NumVerts - PhysMesh.mNumFixedVerts) / MassSum};

                for (float& InvMass : InvMasses) {
                    if (InvMass != 0.0f) {
                        InvMass = 1.0f / (InvMass * MassScale);
                    }
                }
            }

            // 가중치가 큰 순서로 채웠으므로 처음 나오는 0 앞까지가 영향 수입니다.
            for (FClothVertBoneData& BoneData : PhysMesh.mBoneData) {
                BoneData.mNumInfluences = MaxTotalInfluences;

                for (Index_t InfluenceIndex{}; InfluenceIndex < MaxTotalInfluences; ++InfluenceIndex) {
                    if (BoneData.mBoneWeights[InfluenceIndex] == 0.0f) {
                        BoneData.mNumInfluences = InfluenceIndex;

                        break;
                    }
                }
            }
        }
    }

    /**
     * @brief 사용 뼈 이름마다 스켈레톤의 뼈 번호를 다시 찾습니다.
     */
    void UClothingAssetCommon::RefreshBoneMapping(const Core::TArray<Core::FString>& SkeletonBoneNames) {
        mUsedBoneIndices.assign(mUsedBoneNames.size(), InvalidIndex);

        for (Index_t BoneNameIndex{}; BoneNameIndex < mUsedBoneNames.size(); ++BoneNameIndex) {
            const auto Found{std::find(SkeletonBoneNames.begin(), SkeletonBoneNames.end(), mUsedBoneNames[BoneNameIndex])};

            if (Found != SkeletonBoneNames.end()) {
                mUsedBoneIndices[BoneNameIndex] = static_cast<Index_t>(Found - SkeletonBoneNames.begin());
            }
        }
    }

    /**
     * @brief 천이 부딪힐 몸을 담은 물리 에셋을 정하고, 그 몸통 뼈도 사용 뼈에 넣어 뼈 번호를 다시 찾습니다.
     */
    void UClothingAssetCommon::SetPhysicsAsset(UPhysicsAsset* PhysicsAsset, const Core::TArray<Core::FString>& SkeletonBoneNames) {
        mPhysicsAsset = PhysicsAsset != nullptr ? CoreUObject::MakeObjectRef(PhysicsAsset) : CoreUObject::TObjectRef<UPhysicsAsset>{};

        if (PhysicsAsset != nullptr) {
            for (const std::unique_ptr<UBodySetup>& BodySetup : PhysicsAsset->mSkeletalBodySetups) {
                if (BodySetup == nullptr) {
                    continue;
                }

                // 스켈레톤에 있는 뼈만 넣어야 뼈 번호를 찾을 수 있습니다.
                const Core::FString& BoneName{BodySetup->GetBoneName()};
                const bool InSkeleton{std::find(SkeletonBoneNames.begin(), SkeletonBoneNames.end(), BoneName) != SkeletonBoneNames.end()};

                if (InSkeleton && std::find(mUsedBoneNames.begin(), mUsedBoneNames.end(), BoneName) == mUsedBoneNames.end()) {
                    mUsedBoneNames.push_back(BoneName);
                }
            }
        }

        RefreshBoneMapping(SkeletonBoneNames);
    }

    /**
     * @brief 식별자, 상세 단계 데이터, 사용 뼈, 물리 에셋 경로, 설정을 저장하거나 불러옵니다.
     */
    void UClothingAssetCommon::SerializeClothingData(Core::FArchive& Archive) {
        // 값이 바뀌면 이 번호를 올리고 예전 번호를 읽는 분기를 둡니다.
        Uint32 Version{1};

        Archive.Serialize("ClothingAssetVersion", Version);

        if (Archive.HasError() || Version != 1) {
            Archive.SetError("Unsupported clothing asset version");

            return;
        }

        // 섹션 연결 데이터가 이 식별자로 천 에셋을 찾으므로 그대로 되살립니다.
        Core::FGuid Guid{GetGuid()};
        Archive.Serialize("Guid", Guid);

        if (Archive.IsLoading() && !RestoreGuid(Guid)) {
            Archive.SetError("Invalid clothing asset guid");

            return;
        }

        SerializeArray(Archive, "Lods", mLodData, [&Archive](FClothLODDataCommon& Lod) {
            SerializePhysicalMesh(Archive, Lod.mPhysicalMeshData);
            SerializeArray(Archive, "PointWeightMaps", Lod.mPointWeightMaps, [&Archive](FPointWeightMap& Map) {
                SerializeWeightMap(Archive, Map);
            });
            SerializeArray(Archive, "TransitionUp", Lod.mTransitionUpSkinData, [&Archive](FMeshToMeshVertData& Data) {
                SerializeMeshToMeshVertData(Archive, Data);
            });
            SerializeArray(Archive, "TransitionDown", Lod.mTransitionDownSkinData, [&Archive](FMeshToMeshVertData& Data) {
                SerializeMeshToMeshVertData(Archive, Data);
            });
            SerializeCollisionData(Archive, Lod.mCollisionData);
        });
        SerializeArray(Archive, "LodMap", mLodMap, [&Archive](Index_t& LodIndex) {
            SerializeIndex(Archive, "LodIndex", LodIndex);
        });
        SerializeArray(Archive, "UsedBoneNames", mUsedBoneNames, [&Archive](Core::FString& BoneName) {
            Archive.Serialize("BoneName", BoneName);
        });
        SerializeArray(Archive, "UsedBoneIndices", mUsedBoneIndices, [&Archive](Index_t& BoneIndex) {
            SerializeIndex(Archive, "BoneIndex", BoneIndex);
        });
        SerializeIndex(Archive, "ReferenceBoneIndex", mReferenceBoneIndex);
        Archive.Serialize("PhysicsAssetPath", mPhysicsAssetPath);

        if (Archive.IsLoading() && mClothConfig == nullptr) {
            mClothConfig = CoreUObject::NewObject<UClothConfigNv>();
        }

        mClothConfig->SerializeConfig(Archive);
    }

    /**
     * @brief 설정의 천끼리 충돌 거리로 상세 단계마다 천끼리 부딪힐 입자를 다시 고릅니다.
     */
    void UClothingAssetCommon::BuildSelfCollisionData() {
        if (mClothConfig == nullptr) {
            return;
        }

        const Core::FLength SelfCollisionRadius{mClothConfig->NeedsSelfCollisionIndices()};

        for (FClothLODDataCommon& Lod : mLodData) {
            Lod.mPhysicalMeshData.BuildSelfCollisionData(SelfCollisionRadius);
        }
    }

    /**
     * @brief 천 설정을 반환합니다.
     */
    UClothConfigNv* UClothingAssetCommon::GetClothConfig() {
        return mClothConfig.get();
    }

    /**
     * @brief 천 설정을 반환합니다.
     */
    const UClothConfigNv* UClothingAssetCommon::GetClothConfig() const {
        return mClothConfig.get();
    }

    /**
     * @brief 시뮬레이션 좌표의 기준 뼈를 루트로 정합니다.
     */
    void UClothingAssetCommon::CalculateReferenceBoneIndex() {
        mReferenceBoneIndex = 0;
    }

    /**
     * @brief 이웃한 상세 단계 사이에서 입자 위치를 옮길 데이터를 만듭니다.
     */
    void UClothingAssetCommon::BuildLodTransitionData() {
        const Index_t NumLods{GetNumLods()};

        for (Index_t LodIndex{}; LodIndex < NumLods; ++LodIndex) {
            FClothLODDataCommon& CurrentLod{mLodData[LodIndex]};
            const FClothPhysicalMeshData& CurrentPhysMesh{CurrentLod.mPhysicalMeshData};
            const ClothingMeshUtils::ClothMeshDesc CurrentMeshDesc{CurrentPhysMesh.mVertices, CurrentPhysMesh.mNormals, CurrentPhysMesh.mIndices};

            CurrentLod.mTransitionUpSkinData.clear();
            CurrentLod.mTransitionDownSkinData.clear();

            if (LodIndex > 0) {
                const FClothPhysicalMeshData& PrevPhysMesh{mLodData[LodIndex - 1].mPhysicalMeshData};
                const ClothingMeshUtils::ClothMeshDesc PrevMeshDesc{PrevPhysMesh.mVertices, PrevPhysMesh.mNormals, PrevPhysMesh.mIndices};

                ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod.mTransitionUpSkinData, CurrentMeshDesc, nullptr, PrevMeshDesc);
            }

            if (LodIndex + 1 < NumLods) {
                const FClothPhysicalMeshData& NextPhysMesh{mLodData[LodIndex + 1].mPhysicalMeshData};
                const ClothingMeshUtils::ClothMeshDesc NextMeshDesc{NextPhysMesh.mVertices, NextPhysMesh.mNormals, NextPhysMesh.mIndices};

                ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod.mTransitionDownSkinData, CurrentMeshDesc, nullptr, NextMeshDesc);
            }
        }
    }
}
