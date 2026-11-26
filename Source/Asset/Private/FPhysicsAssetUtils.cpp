/**
 * @file FPhysicsAssetUtils.cpp
 * @brief 스켈레탈 메시의 뼈와 정점으로 물리 에셋을 자동으로 만듭니다.
 * @ingroup Asset
 */

#include "Asset/FPhysicsAssetUtils.h"
#include "Core/Literals/UnitLiterals.h"

#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FQuat.h"
#include "Core/Math/FRotator.h"
#include "Core/Math/FTransform.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "PhysicsRuntime/UPhysicsAsset.h"

#include <algorithm>
#include <utility>

using namespace Core::Literals;

namespace {
    /**
     * @brief 충돌 모양 한 변의 최소 절반 길이(m)입니다.
     */
    constexpr float MinPrimSize{(0.005_m).GetBase()};

    /**
     * @brief 그래픽과 겹쳐 보이지 않도록 모양을 키우는 비율입니다.
     */
    constexpr float PrimSizeScale{1.01f};

    /**
     * @brief 정점 위치를 모두 감싸는 상자를 구합니다.
     * @param[in] Positions 감쌀 정점 위치입니다.
     * @param[out] OutBox 구한 상자입니다.
     * @return 정점이 하나 이상 있으면 true입니다.
     */
    bool ComputeBounds(const Core::TArray<FVector>& Positions, FBox& OutBox) {
        if (Positions.empty()) {
            return false;
        }

        OutBox.mMinimum = Positions.front();
        OutBox.mMaximum = Positions.front();

        for (const FVector& Position : Positions) {
            OutBox.mMinimum = OutBox.mMinimum.Min(Position);
            OutBox.mMaximum = OutBox.mMaximum.Max(Position);
        }

        return true;
    }

    /**
     * @brief 상자의 절반 크기를 구합니다.
     * @param[in] Box 크기를 구할 상자입니다.
     * @return 축별 절반 크기입니다.
     */
    FVector GetExtent(const FBox& Box) {
        return (Box.mMaximum - Box.mMinimum) * 0.5f;
    }

    /**
     * @brief 뼈에 배정된 정점이 차지하는 크기를 구합니다.
     * @param[in] Info 뼈에 배정된 정점 목록입니다.
     * @return 감싸는 상자의 절반 크기 벡터 길이이며, 정점이 없으면 0입니다.
     */
    float CalcBoneInfoLength(const Asset::FBoneVertInfo& Info) {
        FBox BoneBox{};

        if (!ComputeBounds(Info.mPositions, BoneBox)) {
            return 0.0f;
        }

        return GetExtent(BoneBox).Length();
    }

    /**
     * @brief 자식 뼈의 정점을 부모 뼈 기준 좌표로 바꿔 부모 목록에 더합니다.
     * @param[in] LocalToParent 자식 뼈에서 부모 뼈로 가는 변환입니다.
     * @param[in] ChildInfo 더할 자식 뼈의 정점 목록입니다.
     * @param[in,out] ParentInfo 정점을 받을 부모 뼈의 목록입니다.
     */
    void AddInfoToParentInfo(const FTransform& LocalToParent, const Asset::FBoneVertInfo& ChildInfo, Asset::FBoneVertInfo& ParentInfo) {
        ParentInfo.mPositions.reserve(ParentInfo.mPositions.size() + ChildInfo.mPositions.size());

        for (const FVector& Position : ChildInfo.mPositions) {
            ParentInfo.mPositions.push_back(LocalToParent.TransformPosition(Position));
        }
    }
}

namespace Asset::FPhysicsAssetUtils {
    /**
     * @brief 정점을 뼈별로 나누고 뼈 기준 좌표로 바꿉니다.
     */
    void CalcBoneVertInfos(const USkeletalMesh& Mesh, const USkeleton& Skeleton, EPhysAssetFitVertWeight VertWeight, Core::TArray<FBoneVertInfo>& Infos) {
        const Core::TArray<FMatrix>& InverseBindPoses{Skeleton.GetInverseBindPoses()};
        const Core::TArray<FVector>& Positions{Mesh.GetMeshData().GetPositions()};
        const Core::TArray<FMeshImportSkinWeight>& SkinWeights{Mesh.GetSkinWeights()};
        const Index_t BoneCount{InverseBindPoses.size()};

        Infos.assign(BoneCount, {});

        for (Index_t VertexIndex{}; VertexIndex < Positions.size() && VertexIndex < SkinWeights.size(); ++VertexIndex) {
            const auto& BoneIndices{SkinWeights[VertexIndex].GetBoneIndices()};
            const auto& Weights{SkinWeights[VertexIndex].GetWeights()};

            const auto AddToBone{[&](Index_t BoneIndex) {
                if (BoneIndex < BoneCount) {
                    Infos[BoneIndex].mPositions.push_back(InverseBindPoses[BoneIndex].TransformPosition(Positions[VertexIndex]));
                }
            }};

            if (VertWeight == EPhysAssetFitVertWeight::DominantWeight) {
                Index_t DominantInfluence{InvalidIndex};
                float DominantWeight{0.0f};

                for (Index_t Influence{}; Influence < FMeshImportSkinWeight::MaxInfluences; ++Influence) {
                    if (Weights[Influence] > DominantWeight) {
                        DominantWeight = Weights[Influence];
                        DominantInfluence = Influence;
                    }
                }

                if (DominantInfluence != InvalidIndex) {
                    AddToBone(BoneIndices[DominantInfluence]);
                }
            } else {
                for (Index_t Influence{}; Influence < FMeshImportSkinWeight::MaxInfluences; ++Influence) {
                    if (Weights[Influence] > 0.0f) {
                        AddToBone(BoneIndices[Influence]);
                    }
                }
            }
        }
    }

    /**
     * @brief 메시와 스켈레톤으로 바디와 관절을 새로 채웁니다.
     */
    bool CreateFromSkeletalMesh(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const USkeletalMesh& Mesh, const USkeleton& Skeleton, const FPhysAssetCreateParams& Params) {
        PhysicsAsset.mSkeletalBodySetups.clear();
        PhysicsAsset.mConstraintSetup.clear();
        PhysicsAsset.mCollisionDisabledPairs.clear();
        PhysicsAsset.UpdateBodySetupIndexMap();

        if (!Mesh.IsValid() || !Skeleton.IsValid()) {
            return false;
        }

        const Core::TArray<FMeshImportBone>& Bones{Skeleton.GetBones()};
        const Core::TArray<FTransform>& LocalPose{Skeleton.GetReferencePose()};
        const Index_t NumBones{Bones.size()};

        Core::TArray<FBoneVertInfo> Infos{};
        CalcBoneVertInfos(Mesh, Skeleton, Params.mVertWeight, Infos);

        // 작은 뼈는 끝에서부터 위로 올라가며 부모 뼈에 정점을 합칩니다.
        Core::TArray<FBoneVertInfo> BoneToMergedBones(NumBones);
        Core::TArray<float> MergedSizes(NumBones, 0.0f);

        for (Index_t BoneIndex{NumBones}; BoneIndex-- > 0;) {
            const float MyMergedSize{MergedSizes[BoneIndex] += CalcBoneInfoLength(Infos[BoneIndex])};

            if (MyMergedSize < Params.mMinBoneSize && MyMergedSize >= Params.mMinWeldSize) {
                const Int32 ParentIndex{Bones[BoneIndex].GetParentIndex()};

                if (ParentIndex >= 0) {
                    FBoneVertInfo& ParentMergedBones{BoneToMergedBones[ParentIndex]};
                    const FTransform& LocalTM{LocalPose[BoneIndex]};

                    MergedSizes[ParentIndex] += MyMergedSize;
                    AddInfoToParentInfo(LocalTM, Infos[BoneIndex], ParentMergedBones);

                    // 이 뼈에 이미 합쳐진 정점도 부모에게 넘깁니다.
                    AddInfoToParentInfo(LocalTM, BoneToMergedBones[BoneIndex], ParentMergedBones);
                    BoneToMergedBones[BoneIndex].mPositions.clear();
                }
            }
        }

        // 큰 뼈가 형제로 둘 이상 갈라지면 그 부모를 루트 바디로 강제로 만듭니다.
        Index_t ForcedRootBoneIndex{InvalidIndex};
        Int32 FirstParentBoneIndex{-1};

        for (Index_t BoneIndex{}; BoneIndex < NumBones; ++BoneIndex) {
            if (MergedSizes[BoneIndex] > Params.mMinBoneSize) {
                const Int32 ParentBoneIndex{Bones[BoneIndex].GetParentIndex()};

                if (ParentBoneIndex < 0) {
                    break;
                }

                if (FirstParentBoneIndex < 0) {
                    FirstParentBoneIndex = ParentBoneIndex;
                } else if (ParentBoneIndex == FirstParentBoneIndex) {
                    ForcedRootBoneIndex = static_cast<Index_t>(ParentBoneIndex);
                    break;
                }
            }
        }

        for (Index_t BoneIndex{}; BoneIndex < NumBones; ++BoneIndex) {
            const bool MakeBone{Params.mBodyForAll || MergedSizes[BoneIndex] > Params.mMinBoneSize || BoneIndex == ForcedRootBoneIndex};

            if (!MakeBone) {
                continue;
            }

            const Core::FString& BoneName{Bones[BoneIndex].GetName()};
            const Index_t NewBodyIndex{CreateNewBody(PhysicsAsset, BoneName)};
            PhysicsRuntime::UBodySetup& NewBodySetup{*PhysicsAsset.mSkeletalBodySetups[NewBodyIndex]};

            // 합쳐진 정점은 이미 이 뼈 기준 좌표이므로 그대로 붙입니다.
            FBoneVertInfo Info{Infos[BoneIndex]};
            const Core::TArray<FVector>& MergedPositions{BoneToMergedBones[BoneIndex].mPositions};
            Info.mPositions.insert(Info.mPositions.end(), MergedPositions.begin(), MergedPositions.end());

            if (!CreateCollisionFromBone(NewBodySetup, Skeleton, BoneIndex, Params, Info)) {
                DestroyBody(PhysicsAsset, NewBodyIndex);
                continue;
            }

            if (!Params.mCreateConstraints) {
                continue;
            }

            // 바디가 있는 부모를 찾을 때까지 올라가며 자식 뼈의 부모 기준 변환을 쌓습니다.
            FTransform RelTM{};
            Int32 ParentIndex{static_cast<Int32>(BoneIndex)};
            Index_t ParentBodyIndex{InvalidIndex};
            Core::FString ParentName{};

            do {
                RelTM = RelTM * LocalPose[ParentIndex];
                ParentIndex = Bones[ParentIndex].GetParentIndex();

                if (ParentIndex < 0) {
                    break;
                }

                ParentName = Bones[ParentIndex].GetName();
                ParentBodyIndex = PhysicsAsset.FindBodyIndex(ParentName);
            } while (ParentBodyIndex == InvalidIndex);

            if (ParentBodyIndex == InvalidIndex) {
                continue;
            }

            const Index_t NewConstraintIndex{CreateNewConstraint(PhysicsAsset, BoneName)};
            PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset.mConstraintSetup[NewConstraintIndex]->mDefaultInstance};

            Constraint.mSwing1Motion = Params.mAngularConstraintMode;
            Constraint.mSwing2Motion = Params.mAngularConstraintMode;
            Constraint.mTwistMotion = Params.mAngularConstraintMode;

            // 관절은 자식 뼈의 원점에 둡니다.
            Constraint.mConstraintBone1 = BoneName;
            Constraint.mPos1 = FVector{};
            Constraint.mPriAxis1 = FVector{1.0f, 0.0f, 0.0f};
            Constraint.mSecAxis1 = FVector{0.0f, 1.0f, 0.0f};

            Constraint.mConstraintBone2 = ParentName;
            Constraint.mPos2 = RelTM.mLocation;
            Constraint.mPriAxis2 = RelTM.mRotation.RotateVector(FVector{1.0f, 0.0f, 0.0f});
            Constraint.mSecAxis2 = RelTM.mRotation.RotateVector(FVector{0.0f, 1.0f, 0.0f});
        }

        return !PhysicsAsset.mSkeletalBodySetups.empty();
    }

    /**
     * @brief 뼈의 정점을 감싸는 충돌 모양 하나를 설계도에 넣습니다.
     */
    bool CreateCollisionFromBone(PhysicsRuntime::UBodySetup& BodySetup, const USkeleton& Skeleton, Index_t BoneIndex, const FPhysAssetCreateParams& Params, const FBoneVertInfo& Info) {
        if (BoneIndex >= Skeleton.GetGlobalBindPoses().size()) {
            return false;
        }

        BodySetup.GetAggGeom() = {};

        FBox BoneBox{};
        FBox TransformedBox{};
        FVector BoxCenter{};
        FVector BoxExtent{};

        if (ComputeBounds(Info.mPositions, BoneBox)) {
            // 최소 크기는 뼈의 스케일까지 반영한 크기로 판단합니다.
            TransformedBox = BoneBox.Transformed(Skeleton.GetGlobalBindPoses()[BoneIndex]);
            BoxCenter = (BoneBox.mMinimum + BoneBox.mMaximum) * 0.5f;
            BoxExtent = GetExtent(BoneBox);
        }

        const FVector TransformedExtent{GetExtent(TransformedBox)};
        const float MinRad{std::min({TransformedExtent.x, TransformedExtent.y, TransformedExtent.z})};

        if (MinRad < MinPrimSize) {
            BoxExtent = FVector{MinPrimSize, MinPrimSize, MinPrimSize};
        }

        switch (Params.mGeomType) {
            case EPhysAssetFitGeomType::Box: {
                PhysicsRuntime::FKBoxElem BoxElem{};
                BoxElem.mCenter = BoxCenter;
                BoxElem.mHalfExtent = BoxExtent * PrimSizeScale;
                BodySetup.GetAggGeom().mBoxElems.push_back(BoxElem);
                break;
            }
            case EPhysAssetFitGeomType::Sphere: {
                PhysicsRuntime::FKSphereElem SphereElem{};
                SphereElem.mCenter = BoxCenter;
                SphereElem.mRadius = Core::FLength::FromBase(std::max({BoxExtent.x, BoxExtent.y, BoxExtent.z})) * PrimSizeScale;
                BodySetup.GetAggGeom().mSphereElems.push_back(SphereElem);
                break;
            }
            case EPhysAssetFitGeomType::Sphyl: {
                // 캡슐은 Z축 방향이므로 가장 긴 축을 Z축으로 돌립니다.
                constexpr Core::FAngle QuarterTurn{90_deg};
                PhysicsRuntime::FKSphylElem SphylElem{};
                SphylElem.mCenter = BoxCenter;

                if (BoxExtent.x > BoxExtent.z && BoxExtent.x > BoxExtent.y) {
                    SphylElem.mRotation = FRotator{FQuat{FVector{0.0f, 1.0f, 0.0f}, -QuarterTurn}};
                    SphylElem.mRadius = Core::FLength::FromBase(std::max(BoxExtent.y, BoxExtent.z)) * PrimSizeScale;
                    SphylElem.mLength = Core::FLength::FromBase(BoxExtent.x) * PrimSizeScale;
                } else if (BoxExtent.y > BoxExtent.z && BoxExtent.y > BoxExtent.x) {
                    SphylElem.mRotation = FRotator{FQuat{FVector{1.0f, 0.0f, 0.0f}, QuarterTurn}};
                    SphylElem.mRadius = Core::FLength::FromBase(std::max(BoxExtent.x, BoxExtent.z)) * PrimSizeScale;
                    SphylElem.mLength = Core::FLength::FromBase(BoxExtent.y) * PrimSizeScale;
                } else {
                    SphylElem.mRadius = Core::FLength::FromBase(std::max(BoxExtent.x, BoxExtent.y)) * PrimSizeScale;
                    SphylElem.mLength = Core::FLength::FromBase(BoxExtent.z) * PrimSizeScale;
                }

                BodySetup.GetAggGeom().mSphylElems.push_back(SphylElem);
                break;
            }
        }

        return true;
    }

    /**
     * @brief 뼈 이름으로 빈 바디 설계도를 추가합니다.
     */
    Index_t CreateNewBody(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Core::FString& BodyName) {
        const Index_t BodyIndex{PhysicsAsset.FindBodyIndex(BodyName)};

        if (BodyIndex != InvalidIndex) {
            return BodyIndex;
        }

        std::unique_ptr<PhysicsRuntime::UBodySetup> NewBodySetup{CoreUObject::NewObject<PhysicsRuntime::UBodySetup>()};
        NewBodySetup->SetBoneName(BodyName);
        PhysicsAsset.mSkeletalBodySetups.push_back(std::move(NewBodySetup));
        PhysicsAsset.UpdateBodySetupIndexMap();

        return PhysicsAsset.mSkeletalBodySetups.size() - 1;
    }

    /**
     * @brief 바디 설계도 하나와 그 뼈를 쓰는 관절을 지웁니다.
     */
    void DestroyBody(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, Index_t BodyIndex) {
        if (BodyIndex >= PhysicsAsset.mSkeletalBodySetups.size()) {
            return;
        }

        const Core::FString BoneName{PhysicsAsset.mSkeletalBodySetups[BodyIndex]->GetBoneName()};

        std::erase_if(PhysicsAsset.mConstraintSetup, [&BoneName](const std::unique_ptr<PhysicsRuntime::UPhysicsConstraintTemplate>& Setup) {
            return Setup == nullptr || Setup->mDefaultInstance.mConstraintBone1 == BoneName || Setup->mDefaultInstance.mConstraintBone2 == BoneName;
        });

        // 지운 바디의 뼈를 가리키는 충돌 비활성 쌍도 함께 지운다.
        std::erase_if(PhysicsAsset.mCollisionDisabledPairs, [&BoneName](const std::pair<Core::FString, Core::FString>& Pair) {
            return Pair.first == BoneName || Pair.second == BoneName;
        });

        PhysicsAsset.mSkeletalBodySetups.erase(PhysicsAsset.mSkeletalBodySetups.begin() + static_cast<std::ptrdiff_t>(BodyIndex));
        PhysicsAsset.UpdateBodySetupIndexMap();
    }

    /**
     * @brief 관절 이름으로 기본 관절 설정을 추가합니다.
     */
    Index_t CreateNewConstraint(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Core::FString& ConstraintName) {
        const Index_t ConstraintIndex{PhysicsAsset.FindConstraintIndex(ConstraintName)};

        if (ConstraintIndex != InvalidIndex) {
            return ConstraintIndex;
        }

        std::unique_ptr<PhysicsRuntime::UPhysicsConstraintTemplate> NewConstraintSetup{CoreUObject::NewObject<PhysicsRuntime::UPhysicsConstraintTemplate>()};
        NewConstraintSetup->mDefaultInstance.mJointName = ConstraintName;
        PhysicsAsset.mConstraintSetup.push_back(std::move(NewConstraintSetup));

        return PhysicsAsset.mConstraintSetup.size() - 1;
    }
}
