/**
 * @file ClothEditing.cpp
 * @brief 에디터 명령과 메시 뷰어가 함께 쓰는 천 에셋 만들기, 설정 항목 표, 가중치 칠하기 도구를 구현합니다.
 * @ingroup Editor
 */

#include "ClothEditing.h"
#include "Asset/FClothingAssetFactory.h"
#include "Asset/FClothingSectionData.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "PhysicsRuntime/FClothPhysicalMeshData.h"
#include "PhysicsRuntime/UClothConfigNv.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace Editor::ClothEditing {

    /**
     * @brief 스켈레톤의 뼈 이름을 뼈 번호 순서로 모읍니다.
     */
    Core::TArray<Core::FString> GetSkeletonBoneNames(const Asset::USkeleton& Skeleton) {
        Core::TArray<Core::FString> BoneNames{};
        BoneNames.reserve(Skeleton.GetBones().size());

        for (const Asset::FMeshImportBone& Bone : Skeleton.GetBones()) {
            BoneNames.push_back(Bone.GetName());
        }

        return BoneNames;
    }

    /**
     * @brief 섹션 하나로 천 에셋을 만들어 메시에 넣고 섹션에 연결합니다.
     */
    PhysicsRuntime::UClothingAssetCommon* CreateClothFromSection(Asset::USkeletalMesh& Mesh, const Asset::USkeleton& Skeleton, Index_t SubMeshIndex, PhysicsRuntime::UPhysicsAsset* PhysicsAsset, const Core::FString& PhysicsAssetPath, Core::FString& Error) {
        std::unique_ptr<PhysicsRuntime::UClothingAssetCommon> NewAsset{Asset::FClothingAssetFactory::CreateFromSkeletalMesh(Mesh, Skeleton, SubMeshIndex, PhysicsAsset)};

        if (NewAsset == nullptr) {
            Error = "Cannot create a clothing asset from section " + std::to_string(SubMeshIndex) + ". See the Console for details.";

            return nullptr;
        }

        NewAsset->mPhysicsAssetPath = PhysicsAssetPath;

        // 최대 거리를 칠하기 전에도 움직이도록 위쪽 일부 입자만 고정하고 나머지는 움직일 수 있게 둡니다.
        constexpr float FixedBandRatio{0.2f};
        constexpr float MovableMaxDistance{0.5f};
        PhysicsRuntime::FClothLODDataCommon& LodData{NewAsset->mLodData[0]};
        PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{LodData.mPhysicalMeshData};
        float MinZ{std::numeric_limits<float>::max()};
        float MaxZ{std::numeric_limits<float>::lowest()};

        for (const FVector& Vertex : PhysMesh.mVertices) {
            MinZ = std::min(MinZ, Vertex.z);
            MaxZ = std::max(MaxZ, Vertex.z);
        }

        const float FixedAboveZ{MaxZ - (MaxZ - MinZ) * FixedBandRatio};
        PhysicsRuntime::FPointWeightMap& MaxDistances{PhysMesh.AddWeightMap(PhysicsRuntime::EWeightMapTargetCommon::MaxDistance)};

        for (Index_t VertIndex{}; VertIndex < PhysMesh.mVertices.size(); ++VertIndex) {
            MaxDistances[VertIndex] = PhysMesh.mVertices[VertIndex].z >= FixedAboveZ ? 0.0f : MovableMaxDistance;
        }

        for (PhysicsRuntime::FPointWeightMap& PointWeightMap : LodData.mPointWeightMaps) {
            if (PointWeightMap.mCurrentTarget == PhysicsRuntime::EWeightMapTargetCommon::MaxDistance) {
                PointWeightMap.mValues = MaxDistances.mValues;
            }
        }

        // 고정 입자가 바뀌었으므로 역질량과 천끼리 부딪힐 입자를 다시 구합니다.
        NewAsset->InvalidateCachedData();
        NewAsset->BuildSelfCollisionData();

        PhysicsRuntime::UClothingAssetCommon& ClothingAsset{*NewAsset};
        Mesh.AddClothingAsset(std::move(NewAsset));

        if (!Asset::FClothingAssetFactory::BindToSkeletalMesh(Mesh, ClothingAsset, SubMeshIndex, 0)) {
            Mesh.RemoveClothingAsset(ClothingAsset.GetGuid());
            Error = "Cannot bind the clothing asset to section " + std::to_string(SubMeshIndex) + ". See the Console for details.";

            return nullptr;
        }

        return &ClothingAsset;
    }

    /**
     * @brief 천이 연결된 섹션의 렌더 정점과 천 입자 사이 연결 데이터를 다시 만듭니다.
     */
    bool RebuildSectionMapping(Asset::USkeletalMesh& Mesh, Index_t SubMeshIndex, Core::FString& Error) {
        const Asset::FClothingSectionData* const SectionData{Mesh.GetSectionClothingData(SubMeshIndex)};
        const auto& ClothingAssets{Mesh.GetMeshClothingAssets()};

        if (SectionData == nullptr || !SectionData->IsValid() || SectionData->mCorrespondClothAssetIndex >= ClothingAssets.size()) {
            Error = "Section " + std::to_string(SubMeshIndex) + " has no cloth.";

            return false;
        }

        PhysicsRuntime::UClothingAssetCommon& ClothingAsset{*ClothingAssets[SectionData->mCorrespondClothAssetIndex]};
        const Index_t AssetLodIndex{SectionData->mAssetLodIndex};
        const Core::TArray<Index_t> PreviousLodMap{ClothingAsset.mLodMap};

        // 연결 함수는 이미 연결된 상세 단계를 거부하므로, 연결 표를 비운 뒤 다시 연결하고 실패하면 되돌립니다.
        ClothingAsset.mLodMap.clear();

        if (!Asset::FClothingAssetFactory::BindToSkeletalMesh(Mesh, ClothingAsset, SubMeshIndex, AssetLodIndex)) {
            ClothingAsset.mLodMap = PreviousLodMap;
            Error = "Cannot rebuild the cloth mapping of section " + std::to_string(SubMeshIndex) + ". See the Console for details.";

            return false;
        }

        return true;
    }

    namespace {
        /**
         * @brief 강성 항목 하나를 만듭니다.
         * @param[in] Key 콘솔 이름입니다.
         * @param[in] Label 패널 이름입니다.
         * @param[in] Format 표시 형식입니다.
         * @param[in] Min 최솟값입니다.
         * @param[in] Max 최댓값입니다.
         * @param[in] Tooltip 설명입니다.
         * @param[in] GetFloat 값 위치를 구하는 함수입니다.
         * @return 강성 묶음의 설정 항목입니다.
         */
        FClothConfigField MakeStiffnessField(const char* Key, const char* Label, const char* Format, float Min, float Max, const char* Tooltip, float* (*GetFloat)(PhysicsRuntime::UClothConfigNv&)) {
            return FClothConfigField{.mKey = Key, .mLabel = Label, .mGroup = StiffnessGroup, .mFormat = Format, .mDisplayScale = 1.0f, .mMin = Min, .mMax = Max, .mSpeed = 0.005f, .mDefault = FVector{1.0f, 0.0f, 0.0f}, .mTooltip = Tooltip, .mGetFloat = GetFloat};
        }
    }

    /**
     * @brief 모든 천 설정 항목을 패널 표시 순서로 반환합니다.
     */
    const Core::TArray<FClothConfigField>& GetConfigFields() {
        using PhysicsRuntime::UClothConfigNv;

        static const Core::TArray<FClothConfigField> Fields{
            // 강성 표는 세로, 가로, 굽힘, 전단 순서로 한 행에 세기, 배율, 늘어남 한계, 줄어듦 한계 네 칸입니다.
            MakeStiffnessField("vertical.stiffness", "Vertical Stiffness", "%.2f", 0.0f, 1.0f, "되돌리는 세기", [](UClothConfigNv& C) { return &C.mVerticalConstraint.mStiffness; }),
            MakeStiffnessField("vertical.stiffnessmultiplier", "Vertical Multiplier", "%.2f", 0.0f, 1.0f, "느슨한 구간 배율", [](UClothConfigNv& C) { return &C.mVerticalConstraint.mStiffnessMultiplier; }),
            MakeStiffnessField("vertical.stretchlimit", "Vertical Stretch Limit", "%.2fx", 1.0f, 10.0f, "늘어남 한계", [](UClothConfigNv& C) { return &C.mVerticalConstraint.mStretchLimit; }),
            MakeStiffnessField("vertical.compressionlimit", "Vertical Compress Limit", "%.2fx", 0.01f, 1.0f, "줄어듦 한계", [](UClothConfigNv& C) { return &C.mVerticalConstraint.mCompressionLimit; }),
            MakeStiffnessField("horizontal.stiffness", "Horizontal Stiffness", "%.2f", 0.0f, 1.0f, "되돌리는 세기", [](UClothConfigNv& C) { return &C.mHorizontalConstraint.mStiffness; }),
            MakeStiffnessField("horizontal.stiffnessmultiplier", "Horizontal Multiplier", "%.2f", 0.0f, 1.0f, "느슨한 구간 배율", [](UClothConfigNv& C) { return &C.mHorizontalConstraint.mStiffnessMultiplier; }),
            MakeStiffnessField("horizontal.stretchlimit", "Horizontal Stretch Limit", "%.2fx", 1.0f, 10.0f, "늘어남 한계", [](UClothConfigNv& C) { return &C.mHorizontalConstraint.mStretchLimit; }),
            MakeStiffnessField("horizontal.compressionlimit", "Horizontal Compress Limit", "%.2fx", 0.01f, 1.0f, "줄어듦 한계", [](UClothConfigNv& C) { return &C.mHorizontalConstraint.mCompressionLimit; }),
            MakeStiffnessField("bend.stiffness", "Bend Stiffness", "%.2f", 0.0f, 1.0f, "되돌리는 세기", [](UClothConfigNv& C) { return &C.mBendConstraint.mStiffness; }),
            MakeStiffnessField("bend.stiffnessmultiplier", "Bend Multiplier", "%.2f", 0.0f, 1.0f, "느슨한 구간 배율", [](UClothConfigNv& C) { return &C.mBendConstraint.mStiffnessMultiplier; }),
            MakeStiffnessField("bend.stretchlimit", "Bend Stretch Limit", "%.2fx", 1.0f, 10.0f, "늘어남 한계", [](UClothConfigNv& C) { return &C.mBendConstraint.mStretchLimit; }),
            MakeStiffnessField("bend.compressionlimit", "Bend Compress Limit", "%.2fx", 0.01f, 1.0f, "줄어듦 한계", [](UClothConfigNv& C) { return &C.mBendConstraint.mCompressionLimit; }),
            MakeStiffnessField("shear.stiffness", "Shear Stiffness", "%.2f", 0.0f, 1.0f, "되돌리는 세기", [](UClothConfigNv& C) { return &C.mShearConstraint.mStiffness; }),
            MakeStiffnessField("shear.stiffnessmultiplier", "Shear Multiplier", "%.2f", 0.0f, 1.0f, "느슨한 구간 배율", [](UClothConfigNv& C) { return &C.mShearConstraint.mStiffnessMultiplier; }),
            MakeStiffnessField("shear.stretchlimit", "Shear Stretch Limit", "%.2fx", 1.0f, 10.0f, "늘어남 한계", [](UClothConfigNv& C) { return &C.mShearConstraint.mStretchLimit; }),
            MakeStiffnessField("shear.compressionlimit", "Shear Compress Limit", "%.2fx", 0.01f, 1.0f, "줄어듦 한계", [](UClothConfigNv& C) { return &C.mShearConstraint.mCompressionLimit; }),

            FClothConfigField{.mKey = "solverfrequency", .mLabel = "Solver Frequency", .mGroup = "Simulation", .mFormat = "%.0f Hz", .mMin = 30.0f, .mMax = 1000.0f, .mSpeed = 1.0f, .mDefault = FVector{120.0f, 0.0f, 0.0f},
                .mTooltip = "1초에 푸는 횟수.\n높이면 덜 늘어나고 안정되지만 느려짐",
                .mFrequency = &UClothConfigNv::mSolverFrequency},
            FClothConfigField{.mKey = "stiffnessfrequency", .mLabel = "Stiffness Frequency", .mGroup = "Simulation", .mFormat = "%.0f Hz", .mMin = 1.0f, .mMax = 1000.0f, .mSpeed = 1.0f, .mDefault = FVector{100.0f, 0.0f, 0.0f},
                .mTooltip = "세기·감쇠 값의 기준 횟수.\n낮추면 같은 값이라도 약해짐",
                .mFrequency = &UClothConfigNv::mStiffnessFrequency},
            FClothConfigField{.mKey = "gravityscale", .mLabel = "Gravity Scale", .mGroup = "Simulation", .mFormat = "%.2fx", .mMin = 0.0f, .mMax = 10.0f, .mSpeed = 0.01f, .mDefault = FVector{1.0f, 0.0f, 0.0f},
                .mTooltip = "중력 배율. 0 = 무중력",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mGravityScale; }},
            FClothConfigField{.mKey = "gravityoverride", .mLabel = "Gravity", .mGroup = "Simulation", .mFormat = "%.2f m/s2", .mMin = -50.0f, .mMax = 50.0f, .mSpeed = 0.05f, .mDefault = FVector{0.0f, 0.0f, 0.0f},
                .mTooltip = "월드 중력 대신 쓸 중력(m/s2).\nOverride Gravity가 켜졌을 때만 씀",
                .mGetVector = [](UClothConfigNv& C) { return &C.mGravityOverride; }},

            FClothConfigField{.mKey = "damping", .mLabel = "Damping", .mGroup = "Damping & Drag", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 0.99f, .mSpeed = 0.005f, .mDefault = FVector{0.4f, 0.4f, 0.4f},
                .mTooltip = "움직임을 줄이는 비율.\n높이면 흔들림이 빨리 멈춤",
                .mGetVector = [](UClothConfigNv& C) { return &C.mDamping; }},
            FClothConfigField{.mKey = "lineardrag", .mLabel = "Linear Drag", .mGroup = "Damping & Drag", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 0.99f, .mSpeed = 0.005f, .mDefault = FVector{0.2f, 0.2f, 0.2f},
                .mTooltip = "몸이 움직일 때 천이 따라오는 정도",
                .mGetVector = [](UClothConfigNv& C) { return &C.mLinearDrag; }},
            FClothConfigField{.mKey = "angulardrag", .mLabel = "Angular Drag", .mGroup = "Damping & Drag", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 0.99f, .mSpeed = 0.005f, .mDefault = FVector{0.2f, 0.2f, 0.2f},
                .mTooltip = "몸이 돌 때 천이 따라 도는 정도",
                .mGetVector = [](UClothConfigNv& C) { return &C.mAngularDrag; }},

            FClothConfigField{.mKey = "linearinertia", .mLabel = "Linear Inertia", .mGroup = "Inertia", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.005f, .mDefault = FVector{1.0f, 1.0f, 1.0f},
                .mTooltip = "몸의 가속을 천에 전하는 비율.\n0 = 갑자기 움직여도 안 휘날림",
                .mGetVector = [](UClothConfigNv& C) { return &C.mLinearInertiaScale; }},
            FClothConfigField{.mKey = "angularinertia", .mLabel = "Angular Inertia", .mGroup = "Inertia", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.005f, .mDefault = FVector{1.0f, 1.0f, 1.0f},
                .mTooltip = "몸의 회전 가속을 천에 전하는 비율",
                .mGetVector = [](UClothConfigNv& C) { return &C.mAngularInertiaScale; }},
            FClothConfigField{.mKey = "centrifugalinertia", .mLabel = "Centrifugal Inertia", .mGroup = "Inertia", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.005f, .mDefault = FVector{1.0f, 1.0f, 1.0f},
                .mTooltip = "원심력을 천에 전하는 비율.\n0 = 돌아도 치마가 안 퍼짐",
                .mGetVector = [](UClothConfigNv& C) { return &C.mCentrifugalInertiaScale; }},

            FClothConfigField{.mKey = "tetherstiffness", .mLabel = "Tether Stiffness", .mGroup = "Tether", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.005f, .mDefault = FVector{1.0f, 0.0f, 0.0f},
                .mTooltip = "고정 입자에서 멀어지지 않게 잡는 끈의 세기.\n낮추면 고무줄처럼 늘어남",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mTetherStiffness; }},
            FClothConfigField{.mKey = "tetherlimit", .mLabel = "Tether Limit", .mGroup = "Tether", .mFormat = "%.2fx", .mMin = 0.0f, .mMax = 4.0f, .mSpeed = 0.005f, .mDefault = FVector{1.0f, 0.0f, 0.0f},
                .mTooltip = "끈 길이 배율. 0 = 끈 없음",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mTetherLimit; }},

            FClothConfigField{.mKey = "collisionthickness", .mLabel = "Thickness", .mGroup = "Collision", .mFormat = "%.3f m", .mMin = 0.0f, .mMax = 0.1f, .mSpeed = 0.0005f, .mDefault = FVector{0.01f, 0.0f, 0.0f},
                .mTooltip = "충돌체에서 천을 띄우는 거리.\n높이면 덜 뚫지만 떠 보임",
                .mLength = &UClothConfigNv::mCollisionThickness},
            FClothConfigField{.mKey = "friction", .mLabel = "Friction", .mGroup = "Collision", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.005f, .mDefault = FVector{0.1f, 0.0f, 0.0f},
                .mTooltip = "충돌체에서 덜 미끄러지는 정도",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mFriction; }},

            FClothConfigField{.mKey = "selfcollisionradius", .mLabel = "Radius", .mGroup = "Self Collision", .mFormat = "%.3f m", .mMin = 0.0f, .mMax = 0.1f, .mSpeed = 0.0005f, .mDefault = FVector{0.0f, 0.0f, 0.0f},
                .mTooltip = "천끼리 충돌용 입자 반지름. 0 = 끔\n켜면 계산이 크게 늘어남",
                .mLength = &UClothConfigNv::mSelfCollisionRadius},
            FClothConfigField{.mKey = "selfcollisionstiffness", .mLabel = "Stiffness", .mGroup = "Self Collision", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.005f, .mDefault = FVector{0.0f, 0.0f, 0.0f},
                .mTooltip = "천끼리 겹치면 밀어내는 세기. 0 = 끔",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mSelfCollisionStiffness; }},
            FClothConfigField{.mKey = "selfcollisioncullscale", .mLabel = "Cull Scale", .mGroup = "Self Collision", .mFormat = "%.2fx", .mMin = 0.0f, .mMax = 4.0f, .mSpeed = 0.005f, .mDefault = FVector{1.0f, 0.0f, 0.0f},
                .mTooltip = "가까운 입자를 계산에서 빼는 범위.\n높이면 빠르지만 덜 정확",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mSelfCollisionCullScale; }},

            FClothConfigField{.mKey = "winddrag", .mLabel = "Drag", .mGroup = "Wind", .mFormat = "%.4f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.0001f, .mDefault = FVector{0.0002f, 0.0f, 0.0f},
                .mTooltip = "바람에 밀리는 정도. Accurate에서만 씀",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mWindDragCoefficient; }},
            FClothConfigField{.mKey = "windlift", .mLabel = "Lift", .mGroup = "Wind", .mFormat = "%.4f", .mMin = 0.0f, .mMax = 1.0f, .mSpeed = 0.0001f, .mDefault = FVector{0.0002f, 0.0f, 0.0f},
                .mTooltip = "바람에 떠오르는 정도. Accurate에서만 씀",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mWindLiftCoefficient; }},

            FClothConfigField{.mKey = "defaultwind", .mLabel = "Default Wind", .mGroup = "Wind", .mFormat = "%.1f m/s", .mMin = -50.0f, .mMax = 50.0f, .mSpeed = 0.1f, .mDefault = FVector{0.0f, 0.0f, 0.0f},
                .mTooltip = "이 천에 기본으로 부는 바람(m/s, x y z).\n컴포넌트의 Cloth Wind Velocity가 0이면 이 값을 씀",
                .mGetVector = [](UClothConfigNv& C) { return &C.mDefaultWindVelocity; }},

            FClothConfigField{.mKey = "animdrivespring", .mLabel = "Spring", .mGroup = "Anim Drive", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 10.0f, .mSpeed = 0.01f, .mDefault = FVector{1.0f, 0.0f, 0.0f},
                .mTooltip = "칠한 입자를 애니메이션 위치로 당기는 세기",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mAnimDriveSpringStiffness; }},
            FClothConfigField{.mKey = "animdrivedamper", .mLabel = "Damper", .mGroup = "Anim Drive", .mFormat = "%.2f", .mMin = 0.0f, .mMax = 10.0f, .mSpeed = 0.01f, .mDefault = FVector{1.0f, 0.0f, 0.0f},
                .mTooltip = "당길 때 출렁임을 줄이는 세기",
                .mGetFloat = [](UClothConfigNv& C) { return &C.mAnimDriveDamperStiffness; }},
        };

        return Fields;
    }

    /**
     * @brief 강성 표의 행 이름과 설명을 반환합니다.
     */
    const Core::TArray<FClothLabel>& GetStiffnessRows() {
        static const Core::TArray<FClothLabel> Rows{
            {"Vertical", "중력 방향 연결. 낮추면 아래로 잘 늘어남"},
            {"Horizontal", "가로 방향 연결. 낮추면 옆으로 잘 늘어남"},
            {"Bend", "접힘을 막는 연결. 낮추면 잘 구겨짐"},
            {"Shear", "대각선 연결. 낮추면 잘 비틀림"},
        };

        return Rows;
    }

    /**
     * @brief 강성 표의 열 이름과 설명을 반환합니다.
     */
    const Core::TArray<FClothLabel>& GetStiffnessColumns() {
        static const Core::TArray<FClothLabel> Columns{
            {"Stiffness", "늘어난 연결을 되돌리는 세기.\n1 = 바로 되돌림, 0 = 안 되돌림"},
            {"Multiplier", "느슨한 구간(두 한계 사이)에서 세기에 곱하는 값.\n낮추면 그 구간이 고무처럼 느슨"},
            {"Stretch", "원래 길이의 몇 배까지 느슨하게 둘지. 1 = 없음"},
            {"Compress", "원래 길이의 몇 배까지 줄어도 느슨하게 둘지. 1 = 없음"},
        };

        return Columns;
    }

    /**
     * @brief 강성 표가 아닌 설정 묶음의 이름과 설명을 반환합니다.
     */
    const Core::TArray<FClothLabel>& GetConfigGroups() {
        static const Core::TArray<FClothLabel> Groups{
            {"Simulation", "푸는 횟수와 중력"},
            {"Damping & Drag", "흔들림이 잦아드는 정도, 몸을 따라오는 정도"},
            {"Inertia", "몸의 가속·회전이 천에 전해지는 정도"},
            {"Tether", "고정 입자에서 너무 멀어지지 않게 잡는 끈"},
            {"Collision", "몸 충돌체(구·캡슐)에 닿을 때"},
            {"Self Collision", "천끼리 부딪힘. 반지름·세기가 모두 0보다 커야 켜짐"},
            {"Wind", "바람 받는 방식과 이 천의 기본 바람"},
            {"Anim Drive", "칠한 입자를 애니메이션 위치로 당기는 힘"},
        };

        return Groups;
    }

    /**
     * @brief 바람 방식 선택 칸의 설명을 반환합니다.
     */
    const char* GetWindMethodTooltip() {
        return "Legacy: 입자를 바람 쪽으로 직접 끎. 바람이 없어도 공기 저항처럼 작용\nAccurate: 천 면 방향으로 Drag·Lift를 계산";
    }

    bool FClothConfigField::HasScalarValue() const {
        return mGetFloat != nullptr || mLength != nullptr || mFrequency != nullptr;
    }

    float FClothConfigField::GetScalarValue(PhysicsRuntime::UClothConfigNv& Config) const {
        if (mLength != nullptr) {
            return (Config.*mLength).GetBase();
        }

        if (mFrequency != nullptr) {
            return (Config.*mFrequency).GetBase();
        }

        return *mGetFloat(Config);
    }

    void FClothConfigField::SetScalarValue(PhysicsRuntime::UClothConfigNv& Config, float BaseValue) const {
        if (mLength != nullptr) {
            Config.*mLength = Core::FLength::FromBase(BaseValue);
        } else if (mFrequency != nullptr) {
            Config.*mFrequency = Core::FFrequency::FromBase(BaseValue);
        } else {
            *mGetFloat(Config) = BaseValue;
        }
    }

    /**
     * @brief 설정의 벡터 항목 이름과 값 위치를 모읍니다.
     */
    Core::TArray<std::pair<Core::FString, FVector*>> GetConfigVectors(PhysicsRuntime::UClothConfigNv& Config) {
        Core::TArray<std::pair<Core::FString, FVector*>> Vectors{};

        for (const FClothConfigField& Field : GetConfigFields()) {
            if (Field.mGetVector != nullptr) {
                Vectors.emplace_back(Field.mKey, Field.mGetVector(Config));
            }
        }

        return Vectors;
    }

    /**
     * @brief 칠하기 대상의 이름, 단위, 범위, 설명을 반환합니다.
     */
    const FPaintTargetInfo& GetPaintTargetInfo(int TargetIndex) {
        static const FPaintTargetInfo Targets[NumPaintTargets]{
            {"Max Distance", "%.3f m", 1.0f, 0.002f, "애니메이션 위치에서 벗어날 수 있는 거리(m).\n0 = 몸에 고정"},
            {"Backstop Distance", "%.3f m", 1.0f, 0.002f, "몸 안쪽을 막는 구를 안쪽으로 미는 거리(m).\nBackstop Radius와 함께 칠함"},
            {"Backstop Radius", "%.3f m", 1.0f, 0.002f, "몸 안쪽을 막는 구의 반지름(m). 0 = 안 막음"},
            {"Anim Drive Multiplier", "%.2f", 1.0f, 0.005f, "애니메이션 위치로 당기는 세기 배율.\n0 = 안 당김, 1 = Spring 그대로"},
        };

        return Targets[std::clamp(TargetIndex, 0, NumPaintTargets - 1)];
    }

    /**
     * @brief 칠하기 대상 번호의 표시 이름을 반환합니다.
     */
    const char* GetPaintTargetName(int TargetIndex) {
        return GetPaintTargetInfo(TargetIndex).mName;
    }

    /**
     * @brief 칠하기 대상 번호를 가중치 맵 대상으로 바꿉니다.
     */
    PhysicsRuntime::EWeightMapTargetCommon GetPaintTarget(int TargetIndex) {
        switch (TargetIndex) {
            case 1:
                return PhysicsRuntime::EWeightMapTargetCommon::BackstopDistance;
            case 2:
                return PhysicsRuntime::EWeightMapTargetCommon::BackstopRadius;
            case 3:
                return PhysicsRuntime::EWeightMapTargetCommon::AnimDriveMultiplier;
            default:
                return PhysicsRuntime::EWeightMapTargetCommon::MaxDistance;
        }
    }

    /**
     * @brief 칠하기 도구의 이름과 설명을 반환합니다.
     */
    const Core::TArray<FClothLabel>& GetPaintTools() {
        static const Core::TArray<FClothLabel> Tools{
            {"Brush", "끄는 동안 붓 안 입자에 Value를 칠함. Shift = 0"},
            {"Smooth", "끄는 동안 붓 안 값을 이웃 평균으로 다듬음"},
            {"Fill", "누른 곳과 값이 비슷한 이어진 입자를 한 번에 채움"},
        };

        return Tools;
    }

    /**
     * @brief 시뮬레이션 메시의 삼각형으로 입자마다 변으로 이어진 이웃 입자를 모읍니다.
     */
    Core::TArray<Core::TArray<Uint32>> BuildParticleNeighbors(const PhysicsRuntime::FClothPhysicalMeshData& PhysMesh) {
        const Index_t NumParticles{PhysMesh.mVertices.size()};
        Core::TArray<Core::TArray<Uint32>> Neighbors(NumParticles);

        const auto AddNeighbor{[&Neighbors, NumParticles](Uint32 From, Uint32 To) {
            if (From < NumParticles && To < NumParticles && std::find(Neighbors[From].begin(), Neighbors[From].end(), To) == Neighbors[From].end()) {
                Neighbors[From].push_back(To);
            }
        }};

        for (Index_t BaseIndex{}; BaseIndex + 2 < PhysMesh.mIndices.size(); BaseIndex += 3) {
            for (Index_t Corner{}; Corner < 3; ++Corner) {
                const Uint32 A{PhysMesh.mIndices[BaseIndex + Corner]};
                const Uint32 B{PhysMesh.mIndices[BaseIndex + (Corner + 1) % 3]};

                AddNeighbor(A, B);
                AddNeighbor(B, A);
            }
        }

        return Neighbors;
    }

    /**
     * @brief 고른 입자의 값을 이웃 입자 값의 평균 쪽으로 다듬습니다.
     */
    void SmoothWeights(Core::TArray<float>& Values, const Core::TArray<Core::TArray<Uint32>>& Neighbors, const Core::TArray<Index_t>& Particles, float Strength) {
        // 다듬는 도중 바뀐 값을 다시 읽지 않도록 바꾸기 전 값으로 평균을 구합니다.
        const Core::TArray<float> Original{Values};
        const float Ratio{std::clamp(Strength, 0.0f, 1.0f)};

        for (const Index_t Particle : Particles) {
            if (Particle >= Values.size() || Particle >= Neighbors.size() || Neighbors[Particle].empty()) {
                continue;
            }

            float Sum{};

            for (const Uint32 Neighbor : Neighbors[Particle]) {
                Sum += Original[Neighbor];
            }

            const float Average{Sum / static_cast<float>(Neighbors[Particle].size())};

            Values[Particle] = Original[Particle] + (Average - Original[Particle]) * Ratio;
        }
    }

    /**
     * @brief 시작 입자에서 이웃을 따라가며 시작 값과 차이가 기준 이하인 입자를 한 값으로 채웁니다.
     */
    Index_t FillWeights(Core::TArray<float>& Values, const Core::TArray<Core::TArray<Uint32>>& Neighbors, Index_t StartParticle, float Value, float Threshold) {
        if (StartParticle >= Values.size() || StartParticle >= Neighbors.size()) {
            return 0;
        }

        // 바꾸기 전 시작 값과 비교해야 이미 채운 입자를 기준으로 번지지 않습니다.
        const float StartValue{Values[StartParticle]};
        Core::TArray<bool> Visited(Values.size(), false);
        Core::TArray<Index_t> Pending{StartParticle};
        Index_t NumFilled{};

        Visited[StartParticle] = true;

        while (!Pending.empty()) {
            const Index_t Particle{Pending.back()};
            Pending.pop_back();

            Values[Particle] = Value;
            ++NumFilled;

            for (const Uint32 Neighbor : Neighbors[Particle]) {
                if (!Visited[Neighbor] && std::abs(Values[Neighbor] - StartValue) <= Threshold) {
                    Visited[Neighbor] = true;
                    Pending.push_back(Neighbor);
                }
            }
        }

        return NumFilled;
    }

    /**
     * @brief 칠한 가중치 맵을 천 에셋에 반영합니다.
     */
    void ApplyPaintedWeights(Asset::USkeletalMesh& Mesh, PhysicsRuntime::UClothingAssetCommon& ClothingAsset) {
        // 시뮬레이션 메시의 맵을 편집용 가중치 맵 목록에도 그대로 남깁니다.
        for (PhysicsRuntime::FClothLODDataCommon& Lod : ClothingAsset.mLodData) {
            for (const auto& [TargetValue, Map] : Lod.mPhysicalMeshData.mWeightMaps) {
                const auto Target{static_cast<PhysicsRuntime::EWeightMapTargetCommon>(TargetValue)};
                auto Found{std::find_if(Lod.mPointWeightMaps.begin(), Lod.mPointWeightMaps.end(), [Target](const PhysicsRuntime::FPointWeightMap& PointWeightMap) {
                    return PointWeightMap.mCurrentTarget == Target;
                })};

                if (Found == Lod.mPointWeightMaps.end()) {
                    PhysicsRuntime::FPointWeightMap& NewMap{Lod.mPointWeightMaps.emplace_back()};
                    NewMap.mName = GetPaintTargetName(Target == PhysicsRuntime::EWeightMapTargetCommon::BackstopDistance ? 1 : Target == PhysicsRuntime::EWeightMapTargetCommon::BackstopRadius ? 2 : Target == PhysicsRuntime::EWeightMapTargetCommon::AnimDriveMultiplier ? 3 : 0);
                    NewMap.mCurrentTarget = Target;
                    NewMap.mEnabled = true;
                    NewMap.mValues = Map.mValues;
                } else {
                    Found->mValues = Map.mValues;
                }
            }
        }

        // 최대 거리가 바뀌면 고정 입자와 역질량, 천끼리 부딪힐 입자가 달라집니다.
        ClothingAsset.InvalidateCachedData();
        ClothingAsset.BuildSelfCollisionData();

        // 세 입자가 모두 고정인 삼각형에 붙은 정점은 스키닝만 쓰도록 섹션 정점 표시를 다시 정합니다.
        const Index_t NumSubMeshes{Mesh.GetMeshData().GetSubMeshes().size()};

        for (Index_t SubMeshIndex{}; SubMeshIndex < NumSubMeshes; ++SubMeshIndex) {
            const Asset::FClothingSectionData* SectionData{Mesh.GetSectionClothingData(SubMeshIndex)};

            if (SectionData == nullptr || !SectionData->IsValid() || !(SectionData->mAssetGuid == ClothingAsset.GetGuid()) || !ClothingAsset.IsValidLod(SectionData->mAssetLodIndex)) {
                continue;
            }

            const PhysicsRuntime::FPointWeightMap* const MaxDistances{ClothingAsset.mLodData[SectionData->mAssetLodIndex].mPhysicalMeshData.FindWeightMap(PhysicsRuntime::EWeightMapTargetCommon::MaxDistance)};
            Asset::FClothingSectionData Updated{*SectionData};

            for (PhysicsRuntime::FMeshToMeshVertData& VertData : Updated.mClothMappingData) {
                const bool AllFixed{MaxDistances != nullptr && MaxDistances->Num() > 0 && MaxDistances->AreAllBelowThreshold(VertData.mSourceMeshVertIndices[0], VertData.mSourceMeshVertIndices[1], VertData.mSourceMeshVertIndices[2])};

                VertData.mSourceMeshVertIndices[3] = AllFixed ? PhysicsRuntime::FMeshToMeshVertData::NoSimulationFlag : 0u;
            }

            // 연결 데이터를 다시 넣으면 렌더러가 쓰는 천 정점 버퍼도 다음에 새로 만듭니다.
            Mesh.SetSectionClothingData(SubMeshIndex, std::move(Updated));
        }
    }
}
