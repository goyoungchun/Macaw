/**
 * @file USkeletalMeshComponent.cpp
 * @brief 스켈레탈 메시 참조와 객체별 포즈·재질·직렬화를 구현합니다.
 * @ingroup Engine
 */

#include "Core/Types.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Asset/FAssetManager.h"
#include "Core/Archive.h"
#include "Core/FilePath.h"
#include "Core/Literals/AngleFunctions.h"
#include "Core/Base/ErrorHandler.h"
#include "Engine/EPrimitiveType.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Engine/UWorld.h"
#include "Engine/Animation/UAnimSingleNodeInstance.h"
#include "Engine/Animation/UAnimGraphInstance.h"
#include "Engine/Animation/FAnimNode_AssetPlayerBase.h"
#include "Engine/Animation/FAnimationRuntime.h"
#include "Engine/AActor.h"
#include "Asset/Animation/UAnimSequence.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "CoreUObject/Reflection/FProperty.h"
#include "CoreUObject/Reflection/FReflectionRegistry.h"
#include "CoreUObject/Reflection/UClass.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "PhysicsRuntime/FClothingSystemRuntimeModuleNv.h"
#include "PhysicsRuntime/FConstraintInstance.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/FPhysicsScene.h"
#include "PhysicsRuntime/PhysicsStats.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"
#include "PhysicsRuntime/UPhysicsAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <utility>

namespace Engine {

    /**
     * @brief 공유 바인드 형상을 객체별 포즈로 변형하여 표시하는 컴포넌트의 파일 내부 보조 구현입니다.
     */
    namespace {

        /**
         * @brief 저장 가능한 상대 에셋 경로 형식인지 검사합니다.
         * @param Path 처리할 에셋 또는 파일 경로입니다.
         * @return 허용된 상대 경로이면 true입니다.
         */
        bool IsAssetPath(const Core::FString& Path) {
            const std::filesystem::path AssetPath{Core::FilePath::FromUtf8(Path)};

            if (Path.find('\0') != Core::FString::npos || (!Path.empty() && (Core::FilePath::GetContentMount(AssetPath) == Core::FilePath::EContentMount::Invalid || AssetPath.extension() != L".masset"))) {
                return false;
            }

            for (const auto& Part : AssetPath) {
                if (Part == L"..") {
                    return false;
                }
            }

            return true;
        }
    }

    /**
     * @brief 에디터에서도 에셋 변경을 감지하는 컴포넌트를 생성합니다.
     */
    USkeletalMeshComponent::USkeletalMeshComponent() {
        SetEditorTickable(true);
        mPostPhysicsTick.AddPrerequisite(GetPrimaryComponentTick());
    }

    /**
     * @brief 컴포넌트를 파괴합니다.
     */
    USkeletalMeshComponent::~USkeletalMeshComponent() = default;

    USkeletalMeshComponent::FPostPhysicsTick::FPostPhysicsTick(Engine::USkeletalMeshComponent& Target)
        : mTarget{Target} {
        SetTickGroup(Engine::ETickingGroup::PostPhysics);
        SetTickEnabled(false);
    }

    const CoreUObject::UObject* USkeletalMeshComponent::FPostPhysicsTick::GetTarget() const {
        return &mTarget;
    }

    void USkeletalMeshComponent::FPostPhysicsTick::ExecuteTick(float DeltaTime) {
        if (mTarget.HasBegunPlay() && mTarget.GetPrimaryComponentTick().IsTickEnabled() && mTarget.GetPrimaryComponentTick().CanEverTick()) {
            mTarget.BlendInPhysics();
            mTarget.TickClothing(DeltaTime);
        }
    }

    /**
     * @brief 기본 프리미티브 구분값을 반환합니다.
     */
    Engine::EPrimitiveType USkeletalMeshComponent::GetPrimitiveType() const {
        return Engine::EPrimitiveType::Cube;
    }

    /**
     * @brief 공유 바인드 형상을 반환합니다.
     */
    Asset::UStaticMesh* USkeletalMeshComponent::GetStaticMesh() const {
        // 공통 드로 경로에는 바인드 형상 버퍼를 넘기며, 변형은 정점 셰이더가 본 팔레트로 처리합니다.
        return mSkeletalMesh != nullptr ? mSkeletalMesh->GetGeometry() : nullptr;
    }

    /**
     * @brief 연결된 스켈레탈 메시를 반환합니다.
     */
    Asset::USkeletalMesh* USkeletalMeshComponent::GetSkeletalMesh() const {
        return mSkeletalMesh;
    }

    /**
     * @brief 현재 변형 위치와 경계를 가진 CPU 형상을 반환합니다.
     */
    const Asset::FMeshData& USkeletalMeshComponent::GetMeshData() const {
        // 피킹이 실제 보이는 표면과 일치하도록 에셋의 바인드 형상이 아니라 현재 포즈로 변형한 형상을 반환합니다.
        return mSkeletalMesh != nullptr && !mPose.GetRevision().IsValid() ? mSkeletalMesh->GetMeshData() : mPose.GetMeshData();
    }

    FBox USkeletalMeshComponent::GetWorldBounds() const {
        if (mSkeletalMesh == nullptr) {
            return Engine::UMeshComponent::GetWorldBounds();
        }

        const FMatrix ComponentToWorld{GetWorldMatrix()};
        const Core::TArray<FMatrix>& GlobalTransforms{mPose.GetGlobalTransforms()};

        if (!GlobalTransforms.empty()) {
            if (const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{LoadPhysicsAsset(GetAssignedPhysicsAssetPath())}; PhysicsAsset != nullptr) {
                FBox Bounds{mSkeletalMesh->CalcSkinnedWorldBounds(mPose.GetSkinMatrices(), ComponentToWorld)};

                for (const auto& Body : PhysicsAsset->mSkeletalBodySetups) {
                    if (Body == nullptr) {
                        continue;
                    }

                    const Index_t BoneIndex{GetBoneIndex(Body->GetBoneName())};

                    if (BoneIndex == InvalidIndex || BoneIndex >= GlobalTransforms.size()) {
                        continue;
                    }

                    const std::optional<FBox> BodyBounds{Body->GetAggGeom().CalcAABB(GlobalTransforms[BoneIndex] * ComponentToWorld)};

                    if (BodyBounds.has_value()) {
                        Bounds.mMinimum = Bounds.mMinimum.Min(BodyBounds->mMinimum);
                        Bounds.mMaximum = Bounds.mMaximum.Max(BodyBounds->mMaximum);
                    }
                }

                return Bounds;
            }
        }

        FBox MeshBounds{mSkeletalMesh->GetBounds()};

        if (mSkeleton != nullptr && !GlobalTransforms.empty() && !mSkeleton->GetGlobalBindPoses().empty()) {
            const FVector RootOffset{GlobalTransforms.front().Origin() - mSkeleton->GetGlobalBindPoses().front().Origin()};
            MeshBounds.mMinimum += RootOffset;
            MeshBounds.mMaximum += RootOffset;
        }

        return MeshBounds.Transformed(ComponentToWorld);
    }

    /**
     * @brief 프로젝트 상대 에셋 경로를 반환합니다.
     */
    const Core::FString& USkeletalMeshComponent::GetMeshPath() const {
        return mMeshPath;
    }

    /**
     * @brief 에셋 경로를 설정하고 포즈를 초기화합니다.
     */
    bool USkeletalMeshComponent::SetMeshPath(const Core::FString& Path) {
        if (!IsAssetPath(Path)) {
            return false;
        }

        if (!mPreviewAssets && mMeshPath == Path && (Path.empty() || mSkeletalMesh != nullptr)) {
            return true;
        }

        Asset::USkeletalMesh* Mesh{LoadMesh(Path)};

        if (!Path.empty() && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr && Mesh == nullptr) {
            return false;
        }

        // 다른 에셋으로 바꾸면 이전 에셋의 본 번호와 슬롯 오버라이드는 폐기합니다.
        if (mMeshPath != Path) {
            ClearMaterialOverrides();

            mPose = Engine::FSkeletalMeshPose{};
        }

        mPreviewAssets = false;
        mMeshPath = Path;
        mSkeletalMesh = nullptr;

        ResolveMesh();
        MarkRenderStateDirty();

        return true;
    }

    Asset::USkeleton* USkeletalMeshComponent::GetSkeleton() const {
        return mSkeleton;
    }

    const Core::FString& USkeletalMeshComponent::GetSkeletonPath() const {
        return mSkeletonPath;
    }

    bool USkeletalMeshComponent::SetSkeletonPath(const Core::FString& Path) {
        if (!IsAssetPath(Path)) {
            return false;
        }

        if (!mPreviewAssets && mSkeletonPath == Path && (Path.empty() || mSkeleton != nullptr)) {
            return true;
        }

        if (!Path.empty() && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr && LoadSkeleton(Path) == nullptr) {
            return false;
        }

        mPreviewAssets = false;
        mSkeletonPath = Path;
        mSkeleton = nullptr;
        mPose = Engine::FSkeletalMeshPose{};

        ResolveMesh();
        MarkRenderStateDirty();

        return true;
    }

    bool USkeletalMeshComponent::SetPreviewAssets(Asset::USkeletalMesh* Mesh, Asset::USkeleton* Skeleton) {
        Engine::FSkeletalMeshPose Pose{};

        if (GetWorld() == nullptr || GetWorld()->GetWorldType() != Engine::EWorldType::EditorPreview || Mesh == nullptr || Skeleton == nullptr || !Pose.Reset(*Mesh, *Skeleton)) {
            return false;
        }

        ClearMaterialOverrides();
        mMeshPath.clear();
        mSkeletonPath.clear();
        mSkeletalMesh = Mesh;
        mSkeleton = Skeleton;
        mPose = std::move(Pose);
        mUnblendedLocalPose.clear();
        mPreviewAssets = true;

        if (mAnimInstance != nullptr) {
            mAnimInstance->InitializeAnimation(this);
        }

        MarkRenderStateDirty();
        RecreatePhysicsState();

        // 메시가 바뀌었으므로 새 메시의 천 에셋으로 천을 다시 만듭니다.
        RecreateClothingActors();

        return true;
    }

    /**
     * @brief 저장된 에셋 경로를 해석합니다.
     */
    bool USkeletalMeshComponent::ResolveMesh() {
        if (mPreviewAssets) {
            return mSkeletalMesh != nullptr && mSkeleton != nullptr;
        }

        Asset::USkeletalMesh* Mesh{LoadMesh(mMeshPath)};
        Asset::USkeleton* Skeleton{LoadSkeleton(mSkeletonPath)};

        // 캐시에 올라간 에셋은 내용이 바뀌지 않으므로 객체가 같으면 다시 해석할 필요가 없습니다.
        if (Mesh != mSkeletalMesh || Skeleton != mSkeleton) {
            mSkeletalMesh = Mesh;
            mSkeleton = Skeleton;
            mUnblendedLocalPose.clear();
            mPose.Invalidate();

            if (Mesh != nullptr && Skeleton != nullptr) {
                bool PoseReady{false};

                if (!mPose.GetLocalTransforms().empty()) {
                    PoseReady = mPose.Recalculate(*Mesh, *Skeleton);

                    if (!PoseReady) {
                        Core::ErrorHandler::Report("SkeletalMesh", "Stored pose does not match the skeleton; using bind pose", Core::ErrorHandler::EErrorLevel::Warning);
                    }
                }

                if (!PoseReady && !mPose.Reset(*Mesh, *Skeleton)) {
                    mPose = Engine::FSkeletalMeshPose{};
                }
            }

            // 애니메이션 인스턴스의 트랙 → 본 매핑은 스켈레톤 본 순서에 묶여 있으므로, 메시·스켈레톤이 바뀌면 다시 초기화해 매핑을 비웁니다.
            if (mAnimInstance != nullptr) {
                mAnimInstance->InitializeAnimation(this);
            }

            MarkRenderStateDirty();

            // 뼈 번호와 위치가 바뀌었으므로 뼈별 바디를 다시 만듭니다.
            RecreatePhysicsState();

            // 천 에셋도 메시에 붙어 있으므로 새 메시의 천 에셋으로 천을 다시 만듭니다.
            RecreateClothingActors();
        }

        return (mMeshPath.empty() || mSkeletalMesh != nullptr) && (mSkeletonPath.empty() || mSkeleton != nullptr);
    }

    /**
     * @brief 뼈별 바디와 관절을 만들 물리 에셋을 반환합니다.
     */
    PhysicsRuntime::UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const {
        const Core::FString& Path{GetAssignedPhysicsAssetPath()};

        if (!Path.empty()) {
            return LoadPhysicsAsset(Path);
        }

        if (mSkeletalMesh == nullptr) {
            return nullptr;
        }

        // 지정된 물리 에셋이 없으면 메시가 뼈와 정점으로 만든 것을 임시로 씁니다.
        return mSkeleton != nullptr ? mSkeletalMesh->GetGeneratedPhysicsAsset(*mSkeleton) : nullptr;
    }

    /**
     * @brief 메시의 물리 에셋 대신 쓸 물리 에셋 경로를 반환합니다.
     */
    const Core::FString& USkeletalMeshComponent::GetPhysicsAssetOverride() const {
        return mPhysicsAssetOverride;
    }

    /**
     * @brief 메시의 물리 에셋 대신 쓸 물리 에셋 경로를 설정하고 뼈별 바디를 다시 만듭니다.
     */
    bool USkeletalMeshComponent::SetPhysicsAssetOverride(const Core::FString& Path) {
        if (!IsAssetPath(Path)) {
            return false;
        }

        if (mPhysicsAssetOverride == Path) {
            return true;
        }

        if (!Path.empty() && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr && LoadPhysicsAsset(Path) == nullptr) {
            return false;
        }

        mPhysicsAssetOverride = Path;
        RecreatePhysicsState();
        MarkRenderTransformDirty();

        return true;
    }

    /**
     * @brief 뼈 이름으로 뼈 번호를 찾습니다.
     */
    Index_t USkeletalMeshComponent::GetBoneIndex(const Core::FString& BoneName) const {
        if (mSkeleton == nullptr) {
            return InvalidIndex;
        }

        const Core::TArray<Asset::FMeshImportBone>& Bones{mSkeleton->GetBones()};

        for (Index_t Index{}; Index < Bones.size(); ++Index) {
            if (Bones[Index].GetName() == BoneName) {
                return Index;
            }
        }

        return InvalidIndex;
    }

    /**
     * @brief 뼈의 현재 월드 변환을 구합니다.
     */
    bool USkeletalMeshComponent::GetBoneWorldTransform(Index_t BoneIndex, FTransform& OutTransform) const {
        const Core::TArray<FMatrix>& GlobalTransforms{mPose.GetGlobalTransforms()};

        if (BoneIndex >= GlobalTransforms.size()) {
            return false;
        }

        // 뼈의 컴포넌트 기준 변환 뒤에 컴포넌트의 월드 변환을 적용합니다.
        return (GlobalTransforms[BoneIndex] * GetTransform().ToMatrix()).TryGetTransform(OutTransform);
    }

    /**
     * @brief 물리 에셋이나 메시 경계 바디 셋업이 있으면 물리 상태가 필요하다고 판단합니다.
     */
    bool USkeletalMeshComponent::ShouldCreatePhysicsState() const {
        return mCollisionEnabled && (GetPhysicsAsset() != nullptr || Engine::UMeshComponent::ShouldCreatePhysicsState());
    }

    bool USkeletalMeshComponent::IsCollisionEnabled() const {
        return mCollisionEnabled;
    }

    void USkeletalMeshComponent::SetCollisionEnabled(bool Enabled) {
        if (mCollisionEnabled != Enabled) {
            mCollisionEnabled = Enabled;
            RecreatePhysicsState();
        }
    }

    /**
     * @brief 물리 에셋이 있으면 뼈마다 바디를 만들고 관절로 잇고, 없으면 메시 전체 바디 하나를 만듭니다.
     */
    void USkeletalMeshComponent::OnCreatePhysicsState() {
        PhysicsRuntime::FPhysicsScene* Scene{GetWorld() != nullptr ? GetWorld()->GetPhysicsScene() : nullptr};

        if (!mCollisionEnabled) {
            return;
        }

        if (Scene != nullptr && GetPhysicsAsset() != nullptr) {
            InitArticulated(*Scene);
        } else {
            Engine::UMeshComponent::OnCreatePhysicsState();
        }
    }

    /**
     * @brief 메시와 머티리얼을 해석하고 천 에셋마다 천을 만듭니다.
     */
    void USkeletalMeshComponent::OnRegister() {
        Engine::UMeshComponent::OnRegister();

        // 메시 해석에서 메시가 바뀌었으면 이미 천을 만들었으므로, 아직 없을 때만 만듭니다.
        if (mClothingSimulation == nullptr) {
            RecreateClothingActors();
        }
    }

    /**
     * @brief 관절과 뼈별 바디를 모두 해제합니다.
     */
    void USkeletalMeshComponent::OnDestroyPhysicsState() {
        TermArticulated();
        Engine::UMeshComponent::OnDestroyPhysicsState();
    }

    /**
     * @brief 물리 에셋의 설계도마다 뼈 위치에 바디를 만들고 관절로 이어 물리 장면에 넣습니다.
     */
    void USkeletalMeshComponent::InitArticulated(PhysicsRuntime::FPhysicsScene& PhysScene) {
        PhysicsRuntime::UPhysicsAsset* const PhysicsAsset{GetPhysicsAsset()};

        if (PhysicsAsset == nullptr || mSkeletalMesh == nullptr || mSkeleton == nullptr || !mBodies.empty()) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::ArticulatedSetup};
        const Index_t NumBodies{PhysicsAsset->mSkeletalBodySetups.size()};

        PhysicsRuntime::FInitBodySpawnParams SpawnParams{};
        SpawnParams.mStaticPhysics = false;

        // 관절 위치에는 축별 스케일 중 가장 작은 값을 곱합니다.
        const FVector& Scale3D{GetTransform().mScale};
        const float ActualScale{std::min({std::abs(Scale3D.x), std::abs(Scale3D.y), std::abs(Scale3D.z)})};
        const float Scale{ActualScale == 0.0f ? 0.0001f : ActualScale};

        Core::TMap<Core::FString, PhysicsRuntime::FBodyInstance*> NameToBodyMap{};
        const Uint32 CollisionGroup{PhysScene.AllocateCollisionGroup()};

        mBodies.resize(NumBodies);

        for (Index_t BodyIndex{}; BodyIndex < NumBodies; ++BodyIndex) {
            PhysicsRuntime::UBodySetup* BodySetup{PhysicsAsset->mSkeletalBodySetups[BodyIndex].get()};

            if (BodySetup == nullptr) {
                continue;
            }

            const Index_t BoneIndex{GetBoneIndex(BodySetup->GetBoneName())};
            FTransform BoneTransform{};

            if (BoneIndex == InvalidIndex || !GetBoneWorldTransform(BoneIndex, BoneTransform)) {
                continue;
            }

            auto Body{std::make_unique<PhysicsRuntime::FBodyInstance>()};

            // 물리 에셋의 바디는 시뮬레이션을 끈 키네마틱으로 시작하고, 충돌 설정은 컴포넌트의 바디 인스턴스를 따릅니다.
            Body->SetSimulatePhysics(false);
            Body->CopyCollisionSettings(GetBodyInstance());
            // 감쇠, 질량, 관성은 에셋의 바디 셋업이 정한다.
            Body->SetLinearDamping(BodySetup->GetLinearDamping());
            Body->SetAngularDamping(BodySetup->GetAngularDamping());
            Body->SetMassInKg(BodySetup->GetMassInKg());
            Body->SetInertiaScale(BodySetup->GetInertiaScale());
            Body->mInstanceBodyIndex = BodyIndex;
            Body->mInstanceBoneIndex = BoneIndex;

            if (Body->InitBody(*BodySetup, BoneTransform, this, PhysScene, SpawnParams)) {
                // 충돌 설정: 이 에셋의 바디끼리는 비활성 쌍 목록을 따르고, 충돌을 끈 바디는 아무것도 부딪히지 않는다.
                Body->SetCollisionFilter(CollisionGroup, static_cast<Uint32>(BodyIndex), BodySetup->IsCollisionEnabled());
                NameToBodyMap[BodySetup->GetBoneName()] = Body.get();
                mBodies[BodyIndex].mInstance = std::move(Body);
                mBodies[BodyIndex].mCanSimulate = BodySetup->ShouldSimulatePhysics();
            }
        }

        PhysScene.SetCollisionDisabledPairs(CollisionGroup, PhysicsAsset->BuildCollisionDisabledBodyPairs());

        const Index_t NumConstraints{PhysicsAsset->mConstraintSetup.size()};

        mConstraints.resize(NumConstraints);

        for (Index_t ConstraintIndex{}; ConstraintIndex < NumConstraints; ++ConstraintIndex) {
            const PhysicsRuntime::UPhysicsConstraintTemplate* ConstraintSetup{PhysicsAsset->mConstraintSetup[ConstraintIndex].get()};

            if (ConstraintSetup == nullptr) {
                continue;
            }

            auto Constraint{std::make_unique<PhysicsRuntime::FConstraintInstance>()};
            Constraint->CopyConstraintParamsFrom(ConstraintSetup->mDefaultInstance);

            // 관절이 잇는 두 뼈에 모두 바디가 있을 때만 잇습니다.
            const auto Body1{NameToBodyMap.find(Constraint->mConstraintBone1)};
            const auto Body2{NameToBodyMap.find(Constraint->mConstraintBone2)};

            if (Body1 != NameToBodyMap.end() && Body2 != NameToBodyMap.end() && Constraint->InitConstraint(Body1->second, Body2->second, Scale)) {
                mConstraints[ConstraintIndex] = std::move(Constraint);
            }
        }

        // 관절까지 이은 뒤 컴포넌트 설정대로 시뮬레이션을 켭니다.
        SetAllBodiesSimulatePhysics(ShouldSimulateBodies());
        mPostPhysicsTick.SetTickEnabled(!NameToBodyMap.empty());
    }

    /**
     * @brief 컴포넌트 바디 설정에서 물리 시뮬레이션이 켜져 있는지 반환합니다.
     */
    bool USkeletalMeshComponent::ShouldSimulateBodies() const {
        return GetBodyInstance().ShouldSimulatePhysics();
    }

    /**
     * @brief 모든 뼈별 바디의 시뮬레이션 여부를 바꿉니다.
     */
    void USkeletalMeshComponent::SetAllBodiesSimulatePhysics(bool Simulate) {
        for (FArticulatedBody& Body : mBodies) {
            SetBodySimulation(Body, Simulate);
        }
    }

    bool USkeletalMeshComponent::SetBodySimulatePhysics(const Core::FString& BoneName, bool Simulate) {
        const Index_t Index{FindPhysicsBodyIndex(BoneName)};
        return Index != InvalidIndex && SetBodySimulation(mBodies[Index], Simulate);
    }

    bool USkeletalMeshComponent::SetAllBodiesBelowSimulatePhysics(const Core::FString& BoneName, bool Simulate, bool IncludeSelf) {
        bool Found{};
        for (const Index_t Index : FindPhysicsBodiesBelow(BoneName, IncludeSelf)) {
            Found |= SetBodySimulation(mBodies[Index], Simulate);
        }

        return Found;
    }

    bool USkeletalMeshComponent::IsBodySimulatingPhysics(const Core::FString& BoneName) const {
        const Index_t Index{FindPhysicsBodyIndex(BoneName)};
        return Index != InvalidIndex && mBodies[Index].mInstance->ShouldSimulatePhysics();
    }

    bool USkeletalMeshComponent::SetBodyPhysicsBlendWeight(const Core::FString& BoneName, float Weight) {
        const Index_t Index{FindPhysicsBodyIndex(BoneName)};
        if (Index == InvalidIndex || !mBodies[Index].mCanSimulate || !std::isfinite(Weight)) {
            return false;
        }

        mBodies[Index].mBlendWeight = std::clamp(Weight, 0.0f, 1.0f);
        return true;
    }

    bool USkeletalMeshComponent::SetAllBodiesBelowPhysicsBlendWeight(const Core::FString& BoneName, float Weight, bool IncludeSelf) {
        if (!std::isfinite(Weight)) {
            return false;
        }

        const auto Indices{FindPhysicsBodiesBelow(BoneName, IncludeSelf)};
        const float ClampedWeight{std::clamp(Weight, 0.0f, 1.0f)};
        for (const Index_t Index : Indices) {
            mBodies[Index].mBlendWeight = ClampedWeight;
        }

        return !Indices.empty();
    }

    bool USkeletalMeshComponent::AddImpulseToBody(const Core::FString& BoneName, const FVector& Impulse, bool VelocityChange) {
        const Index_t Index{FindPhysicsBodyIndex(BoneName)};
        if (Index == InvalidIndex) {
            return false;
        }

        PhysicsRuntime::FBodyInstance& Body{*mBodies[Index].mInstance};
        return Body.ShouldSimulatePhysics() && Body.AddImpulse(Impulse, VelocityChange);
    }

    bool USkeletalMeshComponent::AddImpulseAtLocationToBody(const Core::FString& BoneName, const FVector& Impulse, const FVector& Location) {
        const Index_t Index{FindPhysicsBodyIndex(BoneName)};
        if (Index == InvalidIndex) {
            return false;
        }

        PhysicsRuntime::FBodyInstance& Body{*mBodies[Index].mInstance};
        return Body.ShouldSimulatePhysics() && Body.AddImpulseAtLocation(Impulse, Location);
    }

    bool USkeletalMeshComponent::ShouldAnimatePhysicsPose() const {
        return !ShouldSimulateBodies() || !mPostPhysicsTick.IsTickEnabled() || std::ranges::any_of(mBodies, [](const FArticulatedBody& Body) {
            return Body.mCanSimulate && (!Body.mInstance->ShouldSimulatePhysics() || Body.mBlendWeight < 1.0f);
        });
    }

    Index_t USkeletalMeshComponent::FindPhysicsBodyIndex(const Core::FString& BoneName) const {
        const Index_t BoneIndex{GetBoneIndex(BoneName)};
        if (BoneIndex == InvalidIndex) {
            return InvalidIndex;
        }

        for (Index_t Index{}; Index < mBodies.size(); ++Index) {
            const auto& Body{mBodies[Index].mInstance};
            if (Body != nullptr && Body->mInstanceBoneIndex == BoneIndex) {
                return Index;
            }
        }

        return InvalidIndex;
    }

    Core::TArray<Index_t> USkeletalMeshComponent::FindPhysicsBodiesBelow(const Core::FString& BoneName, bool IncludeSelf) const {
        const Index_t RootIndex{GetBoneIndex(BoneName)};
        if (RootIndex == InvalidIndex) {
            return {};
        }

        const auto& Bones{mSkeleton->GetBones()};
        Core::TArray<Index_t> Indices{};
        for (Index_t Index{}; Index < mBodies.size(); ++Index) {
            const FArticulatedBody& Body{mBodies[Index]};
            if (!Body.mCanSimulate || (!IncludeSelf && Body.mInstance->mInstanceBoneIndex == RootIndex)) {
                continue;
            }

            Int32 Current{static_cast<Int32>(Body.mInstance->mInstanceBoneIndex)};
            while (Current >= 0 && static_cast<Index_t>(Current) != RootIndex) {
                Current = Bones[Current].GetParentIndex();
            }

            if (Current >= 0) {
                Indices.push_back(Index);
            }
        }

        return Indices;
    }

    bool USkeletalMeshComponent::SetBodySimulation(FArticulatedBody& Body, bool Simulate) {
        if (!Body.mCanSimulate) {
            return false;
        }

        PhysicsRuntime::FBodyInstance& Instance{*Body.mInstance};
        if (Instance.ShouldSimulatePhysics() == Simulate) {
            return true;
        }

        FTransform BoneTransform{};
        if (Simulate && !GetBoneWorldTransform(Instance.mInstanceBoneIndex, BoneTransform)) {
            return false;
        }

        Instance.SetInstanceSimulatePhysics(Simulate);
        if (Simulate) {
            Instance.SetBodyTransform(BoneTransform);
        }

        return true;
    }

    /**
     * @brief 키네마틱 바디를 현재 애니메이션 포즈의 뼈 위치로 옮깁니다.
     */
    void USkeletalMeshComponent::UpdateKinematicBonesToAnim() {
        if (mBodies.empty()) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::KinematicSync};
        Index_t UpdatedBodies{};

        for (const auto& Entry : mBodies) {
            const auto& Body{Entry.mInstance};
            FTransform BoneTransform{};

            if (Body != nullptr && !Body->ShouldSimulatePhysics() && GetBoneWorldTransform(Body->mInstanceBoneIndex, BoneTransform) && Body->SetBodyTransform(BoneTransform)) {
                ++UpdatedBodies;
            }
        }

        PhysicsRuntime::PhysicsStats::KinematicBoneUpdates.Add(static_cast<double>(UpdatedBodies));
    }

    /**
     * @brief 시뮬레이션한 바디 위치를 읽어 뼈 포즈에 씁니다.
     */
    void USkeletalMeshComponent::BlendInPhysics() {
        if (mSkeletalMesh == nullptr || mSkeleton == nullptr) {
            return;
        }

        Index_t SimulatedBodies{};
        bool HasBlendBody{};

        for (const FArticulatedBody& Body : mBodies) {
            if (Body.mInstance != nullptr && Body.mInstance->ShouldSimulatePhysics()) {
                ++SimulatedBodies;
                HasBlendBody |= Body.mBlendWeight > 0.0f;
            }
        }

        if (SimulatedBodies > 0) {
            PhysicsRuntime::PhysicsStats::RagdollComponents.Add();
            PhysicsRuntime::PhysicsStats::SimulatedBoneBodies.Add(static_cast<double>(SimulatedBodies));
        }

        if (!HasBlendBody) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::PoseBlend};
        const Core::TArray<Asset::FMeshImportBone>& Bones{mSkeleton->GetBones()};
        const Core::TArray<FMatrix>& OldGlobals{mPose.GetGlobalTransforms()};
        const auto& BaseLocals{mUnblendedLocalPose.empty() ? mPose.GetLocalTransforms() : mUnblendedLocalPose};
        Core::TArray<FMatrix> Locals{BaseLocals};
        const FMatrix ComponentToWorld{GetTransform().ToMatrix()};
        FMatrix WorldToComponent{};

        if (Locals.size() != Bones.size() || OldGlobals.size() != Bones.size() || !ComponentToWorld.TryInverse(WorldToComponent)) {
            return;
        }

        // 뼈마다 시뮬레이션 중인 바디를 찾아 둡니다.
        Core::TArray<const FArticulatedBody*> BoneToBody{};
        BoneToBody.resize(Bones.size());

        for (const FArticulatedBody& Entry : mBodies) {
            const auto& Body{Entry.mInstance};
            if (Body != nullptr && Body->ShouldSimulatePhysics() && Body->IsValidBodyInstance() && Body->mInstanceBoneIndex < Bones.size()) {
                BoneToBody[Body->mInstanceBoneIndex] = &Entry;
            }
        }

        // 부모부터 내려가며 바디가 있는 뼈는 바디 위치로, 없는 뼈는 부모를 따라가게 합니다.
        Core::TArray<FMatrix> NewGlobals{};
        NewGlobals.resize(Bones.size(), FMatrix::Identity());
        Index_t BlendedBones{};

        for (Index_t BoneIndex{}; BoneIndex < Bones.size(); ++BoneIndex) {
            const Int32 ParentIndex{Bones[BoneIndex].GetParentIndex()};
            const FMatrix ParentGlobal{ParentIndex >= 0 ? NewGlobals[ParentIndex] : FMatrix::Identity()};
            const FArticulatedBody* Body{BoneToBody[BoneIndex]};
            FMatrix ParentInverse{FMatrix::Identity()};

            if (Body == nullptr || Body->mBlendWeight <= 0.0f || (ParentIndex >= 0 && !ParentGlobal.TryInverse(ParentInverse))) {
                NewGlobals[BoneIndex] = Locals[BoneIndex] * ParentGlobal;
                continue;
            }

            // 바디에는 스케일이 없으므로 뼈의 기존 월드 스케일을 그대로 씁니다.
            FTransform OldWorld{};
            (OldGlobals[BoneIndex] * ComponentToWorld).TryGetTransform(OldWorld);

            const FTransform BodyWorld{Body->mInstance->GetBodyTransform()};
            const FTransform NewWorld{BodyWorld.mLocation, BodyWorld.mRotation, OldWorld.mScale};
            const FMatrix PhysicsLocal{NewWorld.ToMatrix() * WorldToComponent * ParentInverse};

            if (Body->mBlendWeight >= 1.0f) {
                Locals[BoneIndex] = PhysicsLocal;
                ++BlendedBones;
            } else {
                FTransform Animation{};
                FTransform Physics{};
                if (Locals[BoneIndex].TryGetTransform(Animation) && PhysicsLocal.TryGetTransform(Physics)) {
                    Physics.mScale = Animation.mScale;
                    Locals[BoneIndex] = Animation.Blend(Physics, Body->mBlendWeight).ToMatrix();
                    ++BlendedBones;
                }
            }

            NewGlobals[BoneIndex] = Locals[BoneIndex] * ParentGlobal;
        }

        if (mUnblendedLocalPose.empty()) {
            mUnblendedLocalPose = BaseLocals;
        }
        if (mPose.SetAnimatedLocalTransforms(*mSkeletalMesh, *mSkeleton, std::move(Locals))) {
            PhysicsRuntime::PhysicsStats::BlendedBones.Add(static_cast<double>(BlendedBones));
            MarkRenderStateDirty();
        }
    }

    /**
     * @brief 관절과 뼈별 바디를 모두 해제합니다.
     */
    void USkeletalMeshComponent::TermArticulated() {
        for (const auto& Constraint : mConstraints) {
            if (Constraint != nullptr) {
                Constraint->TermConstraint();
            }
        }

        mConstraints.clear();

        for (const auto& Body : mBodies) {
            if (Body.mInstance != nullptr) {
                Body.mInstance->TermBody();
            }
        }

        mBodies.clear();
        mPostPhysicsTick.SetTickEnabled(false);
    }

    /**
     * @brief 현재 본 포즈를 반환합니다.
     */
    const Engine::FSkeletalMeshPose& USkeletalMeshComponent::GetPose() const {
        return mPose;
    }

    /**
     * @brief 본 하나의 부모 기준 변환을 변경합니다.
     */
    bool USkeletalMeshComponent::SetBoneLocalTransform(Index_t Bone, const FMatrix& Transform) {
        if (mSkeletalMesh == nullptr || mSkeleton == nullptr || !mPose.SetLocalTransform(*mSkeletalMesh, *mSkeleton, Bone, Transform)) {
            return false;
        }

        // 포즈 변경은 월드 변환이 그대로여도 프록시와 그림자 캐시를 갱신해야 합니다.
        MarkRenderStateDirty();

        return true;
    }

    /**
     * @brief 전체 로컬 포즈를 변경합니다.
     */
    bool USkeletalMeshComponent::SetLocalPose(const Core::TArray<FMatrix>& Transforms) {
        const bool Updated{mSkeletalMesh != nullptr && mSkeleton != nullptr ? mPose.SetLocalTransforms(*mSkeletalMesh, *mSkeleton, Transforms) : mPose.SetLocalTransforms(Transforms)};

        if (!Updated) {
            return false;
        }

        mUnblendedLocalPose.clear();
        MarkRenderStateDirty();

        return true;
    }

    /**
     * @brief 런타임 포즈로 스키닝 행렬만 다시 계산합니다.
     */
    bool USkeletalMeshComponent::SetAnimatedLocalPose(Core::TArray<FMatrix> Transforms) {
        if (mSkeletalMesh == nullptr || mSkeleton == nullptr || !mPose.SetAnimatedLocalTransforms(*mSkeletalMesh, *mSkeleton, std::move(Transforms))) {
            return false;
        }

        mUnblendedLocalPose.clear();
        MarkRenderStateDirty();

        return true;
    }

    /**
     * @brief 모든 본을 바인드 포즈로 복원합니다.
     */
    bool USkeletalMeshComponent::ResetPose() {
        if (mSkeletalMesh == nullptr || mSkeleton == nullptr || !mPose.Reset(*mSkeletalMesh, *mSkeleton)) {
            return false;
        }

        mUnblendedLocalPose.clear();
        MarkRenderStateDirty();

        return true;
    }

    /**
     * @brief 에셋과 포즈가 준비되었는지 검사합니다.
     */
    bool USkeletalMeshComponent::ShouldCreateRenderState() const {
        return mSkeletalMesh != nullptr;
    }

    /**
     * @brief 메시 전체를 하나의 바디로 다룰 충돌 형상을 반환합니다.
     */
    const PhysicsRuntime::UBodySetup* USkeletalMeshComponent::GetBodySetup() const {
        return mSkeletalMesh != nullptr ? mSkeletalMesh->GetBodySetup() : nullptr;
    }

    /**
     * @brief 천 시뮬레이션을 준비하고 섹션에 연결된 천 에셋마다 천을 다시 만듭니다.
     */
    void USkeletalMeshComponent::RecreateClothingActors() {
        ReleaseAllClothingResources();

        if (mSkeletalMesh == nullptr || !IsRegistered()) {
            return;
        }

        const auto& MeshClothingAssets{mSkeletalMesh->GetMeshClothingAssets()};

        // 천 에셋이 없으면 가지고 있던 시뮬레이션도 해제합니다.
        if (MeshClothingAssets.empty()) {
            if (mClothingSimulation != nullptr) {
                mClothingSimulation->Shutdown();
                mClothingSimulation.reset();
            }

            return;
        }

        Engine::UWorld* const World{GetWorld()};

        if (mClothingSimulation == nullptr && World != nullptr && World->GetClothingSystem() != nullptr) {
            mClothingSimulation = World->GetClothingSystem()->CreateSimulation();
        }

        if (mClothingSimulation == nullptr) {
            return;
        }

        // 불러온 천 에셋의 사용 뼈를 이 스켈레톤의 뼈 번호로 다시 찾습니다.
        if (mSkeleton != nullptr) {
            Core::TArray<Core::FString> SkeletonBoneNames{};
            SkeletonBoneNames.reserve(mSkeleton->GetBones().size());

            for (const Asset::FMeshImportBone& Bone : mSkeleton->GetBones()) {
                SkeletonBoneNames.push_back(Bone.GetName());
            }

            for (const std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>& ClothingAsset : MeshClothingAssets) {
                ClothingAsset->RefreshBoneMapping(SkeletonBoneNames);
            }
        }

        // 섹션에 연결된 천 에셋만 천으로 만듭니다.
        Core::TArray<const PhysicsRuntime::UClothingAssetCommon*> AssetsInUse{};
        mSkeletalMesh->GetClothingAssetsInUse(AssetsInUse);

        // 첫 진행에서 순간 이동으로 보지 않도록 지금 기준 뼈 위치를 지난 위치로 둡니다.
        mPrevRootBoneMatrix = GetClothRootBoneWorldMatrix();
        mClothTeleportMode = PhysicsRuntime::EClothingTeleportMode::None;

        UpdateClothSimulationContext({});

        for (Index_t AssetIndex{}; AssetIndex < MeshClothingAssets.size(); ++AssetIndex) {
            const PhysicsRuntime::UClothingAssetCommon* const ClothingAsset{MeshClothingAssets[AssetIndex].get()};

            if (ClothingAsset != nullptr && std::find(AssetsInUse.begin(), AssetsInUse.end(), ClothingAsset) != AssetsInUse.end()) {
                mClothingSimulation->CreateActor(*ClothingAsset, AssetIndex, mClothingSimulationContext);
            }
        }

        WritebackClothingSimulationData();
    }

    /**
     * @brief 만든 천과 마지막 결과를 모두 해제합니다.
     */
    void USkeletalMeshComponent::ReleaseAllClothingResources() {
        RemoveAllClothingActors();
        mCurrentSimulationData.clear();
    }

    /**
     * @brief 만든 천을 모두 해제합니다.
     */
    void USkeletalMeshComponent::RemoveAllClothingActors() {
        if (mClothingSimulation != nullptr) {
            mClothingSimulation->DestroyActors();
        }
    }

    /**
     * @brief 천 시뮬레이션이 있으면 이번 프레임의 입력을 채워 천을 진행합니다.
     */
    void USkeletalMeshComponent::TickClothing(float DeltaTime) {
        if (mSkeletalMesh == nullptr || mClothingSimulation == nullptr) {
            return;
        }

        UpdateClothStateAndSimulate(DeltaTime);
    }

    /**
     * @brief 이번 프레임의 입력을 채워 천을 진행하고 결과를 저장합니다.
     */
    void USkeletalMeshComponent::UpdateClothStateAndSimulate(float DeltaTime) {
        // 시간이 0이면 솔버가 속도를 구할 수 없으므로 진행하지 않습니다.
        if (DeltaTime == 0.0f || mClothingSimulation == nullptr) {
            return;
        }

        UpdateClothSimulationContext(Core::FTime::FromBase(DeltaTime));
        mClothingSimulation->Simulate(mClothingSimulationContext);
        WritebackClothingSimulationData();

        // 렌더러가 새 입자 위치로 천 섹션을 그리도록 프록시를 다시 만듭니다.
        MarkRenderStateDirty();
    }

    /**
     * @brief 현재 포즈, 변환, 시간, 중력으로 천 시뮬레이션 입력을 채웁니다.
     */
    void USkeletalMeshComponent::UpdateClothSimulationContext(Core::FTime DeltaTime) {
        // 한 프레임이 너무 길면 천이 튀므로 물리 한 번에 진행할 최대 시간으로 자릅니다.
        constexpr Core::FTime MaxPhysicsDelta{1.0f / Core::FFrequency::FromBase(30.0f)};

        // 프레임이 최대 시간의 이 배수보다 길면 관성 대신 순간 이동으로 처리합니다.
        constexpr float MaxDeltaTimeTeleportMultiplier{1.5f};

        // 진행할 때만 순간 이동을 검사합니다. 처음 천을 만들 때는 이미 지금 위치에 놓습니다.
        if (DeltaTime > Core::FTime{}) {
            CheckClothTeleport();
        }

        const Engine::UWorld* const World{GetWorld()};
        const PhysicsRuntime::FPhysicsRuntime* const Runtime{World != nullptr ? World->GetPhysicsRuntime() : nullptr};

        mClothingSimulationContext.mBoneTransforms = mPose.GetGlobalTransforms();
        mClothingSimulationContext.mRefToLocals = mPose.GetSkinMatrices();
        mClothingSimulationContext.mComponentToWorld = GetWorldMatrix();
        mClothingSimulationContext.mWorldGravity = Runtime != nullptr ? Runtime->GetSettings().mGravity : FVector{0.0f, 0.0f, -9.81f};
        mClothingSimulationContext.mDeltaTime = std::min(DeltaTime, MaxPhysicsDelta);
        mClothingSimulationContext.mPredictedLod = 0;
        mClothingSimulationContext.mMaxDistanceScale = mClothMaxDistanceScale;

        // 바람이 불면 바람에 맞추는 정도를 매 프레임 조금씩 흔들어 돌풍처럼 보이게 합니다.
        // 월드 바람은 컴포넌트 위치에서 한 번 읽어 직접 지정한 바람에 더한다. 합이 0이면 천 에셋의 기본 바람을 쓴다.
        FVector TotalWind{mClothWindVelocity};

        if (mUseWorldWind && World != nullptr) {
            TotalWind += World->GetWindField().Sample(GetTransform().mLocation);
        }

        mClothingSimulationContext.mWindVelocity = TotalWind;
        mClothingSimulationContext.mWindAdaption = TotalWind.LengthSquared() > 0.0f ? static_cast<float>(std::rand() % 20) * 0.1f : 2.0f;

        mClothingSimulationContext.mTeleportMode = DeltaTime > MaxPhysicsDelta * MaxDeltaTimeTeleportMultiplier ? PhysicsRuntime::EClothingTeleportMode::Teleport : mClothTeleportMode;

        // 요청한 순간 이동은 이번 진행 한 번에만 씁니다.
        if (DeltaTime > Core::FTime{}) {
            mClothTeleportMode = PhysicsRuntime::EClothingTeleportMode::None;
        }
    }

    /**
     * @brief 기준 뼈의 지금 월드 행렬을 구합니다.
     */
    FMatrix USkeletalMeshComponent::GetClothRootBoneWorldMatrix() const {
        const Core::TArray<FMatrix>& BoneTransforms{mPose.GetGlobalTransforms()};

        return BoneTransforms.empty() ? GetWorldMatrix() : BoneTransforms[0] * GetWorldMatrix();
    }

    /**
     * @brief 기준 뼈가 한 프레임에 너무 멀리 움직이거나 많이 돌았으면 천 순간 이동을 요청합니다.
     */
    void USkeletalMeshComponent::CheckClothTeleport() {
        const FMatrix CurRootBoneMatrix{GetClothRootBoneWorldMatrix()};
        const PhysicsRuntime::EClothingTeleportMode RequestedMode{mResetAfterTeleport ? PhysicsRuntime::EClothingTeleportMode::TeleportAndReset : PhysicsRuntime::EClothingTeleportMode::Teleport};
        FTransform CurRoot{};
        FTransform PrevRoot{};
        const bool HasTransforms{CurRootBoneMatrix.TryGetTransform(CurRoot) && mPrevRootBoneMatrix.TryGetTransform(PrevRoot)};

        // 거리 검사입니다.
        if (HasTransforms && mTeleportDistanceThreshold > Core::FLength{} && mClothTeleportMode == PhysicsRuntime::EClothingTeleportMode::None) {
            if (Core::FArea::FromBase((CurRoot.mLocation - PrevRoot.mLocation).LengthSquared()) > mTeleportDistanceThreshold * mTeleportDistanceThreshold) {
                mClothTeleportMode = RequestedMode;
            }
        }

        // 회전 검사입니다. 두 회전 사이 각도의 코사인을 사원수 내적으로 구합니다.
        if (HasTransforms && mTeleportRotationThreshold > Core::FAngle{} && mClothTeleportMode == PhysicsRuntime::EClothingTeleportMode::None) {
            const FQuat& A{CurRoot.mRotation};
            const FQuat& B{PrevRoot.mRotation};
            const float Dot{A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w};
            const float CosineTheta{2.0f * Dot * Dot - 1.0f};
            const float CosineThreshold{Core::Math::Cos(mTeleportRotationThreshold)};

            if (CosineTheta < CosineThreshold) {
                mClothTeleportMode = RequestedMode;
            }
        }

        mPrevRootBoneMatrix = CurRootBoneMatrix;
    }

    /**
     * @brief 천에 부는 월드 좌표 바람 속도를 반환합니다.
     */
    const FVector& USkeletalMeshComponent::GetClothWindVelocity() const {
        return mClothWindVelocity;
    }

    /**
     * @brief 천에 부는 월드 좌표 바람 속도를 설정합니다.
     */
    bool USkeletalMeshComponent::SetClothWindVelocity(const FVector& Velocity) {
        mClothWindVelocity = Velocity;

        return true;
    }

    /**
     * @brief 다음 천 진행에서 천을 새 위치로 옮기되 속도는 유지하게 합니다.
     */
    void USkeletalMeshComponent::ForceClothNextUpdateTeleport() {
        mClothTeleportMode = PhysicsRuntime::EClothingTeleportMode::Teleport;
    }

    /**
     * @brief 다음 천 진행에서 천을 새 위치로 옮기고 입자를 스키닝 위치에 다시 놓게 합니다.
     */
    void USkeletalMeshComponent::ForceClothNextUpdateTeleportAndReset() {
        mClothTeleportMode = PhysicsRuntime::EClothingTeleportMode::TeleportAndReset;
    }

    /**
     * @brief 요청한 천 순간 이동을 취소합니다.
     */
    void USkeletalMeshComponent::ResetClothTeleportMode() {
        mClothTeleportMode = PhysicsRuntime::EClothingTeleportMode::None;
    }

    /**
     * @brief 천 입자의 최대 거리에 곱할 배율을 반환합니다.
     */
    float USkeletalMeshComponent::GetClothMaxDistanceScale() const {
        return mClothMaxDistanceScale;
    }

    /**
     * @brief 천 입자의 최대 거리에 곱할 배율을 설정합니다.
     */
    void USkeletalMeshComponent::SetClothMaxDistanceScale(float Scale) {
        mClothMaxDistanceScale = Scale;
    }

    /**
     * @brief 천 섹션을 그릴 때 시뮬레이션 위치를 섞는 비율을 반환합니다.
     */
    float USkeletalMeshComponent::GetClothBlendWeight() const {
        return mClothBlendWeight;
    }

    /**
     * @brief 천 섹션을 그릴 때 시뮬레이션 위치를 섞는 비율을 설정합니다.
     */
    void USkeletalMeshComponent::SetClothBlendWeight(float BlendWeight) {
        mClothBlendWeight = std::clamp(BlendWeight, 0.0f, 1.0f);
        MarkRenderStateDirty();
    }

    /**
     * @brief 천 시뮬레이션 결과를 컴포넌트에 저장합니다.
     */
    void USkeletalMeshComponent::WritebackClothingSimulationData() {
        if (mClothingSimulation != nullptr) {
            mClothingSimulation->GetSimulationData(mCurrentSimulationData, mClothingSimulationContext);
        }

        mClothRevision = Core::FRevision::Next();
    }

    /**
     * @brief 천마다 시뮬레이션 결과를 새로 꺼냅니다.
     */
    void USkeletalMeshComponent::GetUpdateClothSimulationData(Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& OutClothSimData) const {
        if (mClothingSimulation != nullptr) {
            mClothingSimulation->GetSimulationData(OutClothSimData, mClothingSimulationContext);
        }
    }

    /**
     * @brief 마지막 프레임에 저장한 천 시뮬레이션 결과를 반환합니다.
     */
    const Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& USkeletalMeshComponent::GetCurrentClothingData() const {
        return mCurrentSimulationData;
    }

    /**
     * @brief 마지막 천 시뮬레이션 결과의 삼각형 변을 선분으로 그립니다.
     */
    void USkeletalMeshComponent::DebugDrawClothing(Engine::FPrimitiveDrawInterface& DrawInterface, float Thickness) const {
        if (mSkeletalMesh == nullptr) {
            return;
        }

        const auto& MeshClothingAssets{mSkeletalMesh->GetMeshClothingAssets()};
        const FVector FixedColor{0.95f, 0.2f, 0.2f};
        const FVector MovableColor{0.3f, 0.9f, 0.3f};

        for (const auto& [AssetIndex, ClothData] : mCurrentSimulationData) {
            if (AssetIndex >= MeshClothingAssets.size() || MeshClothingAssets[AssetIndex] == nullptr) {
                continue;
            }

            // 메시 상세 단계 0에 연결된 천 상세 단계의 삼각형으로 그립니다.
            const PhysicsRuntime::UClothingAssetCommon& ClothingAsset{*MeshClothingAssets[AssetIndex]};
            const Index_t LodIndex{ClothingAsset.mLodMap.empty() ? 0 : ClothingAsset.mLodMap[0]};

            if (!ClothingAsset.IsValidLod(LodIndex)) {
                continue;
            }

            const PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{ClothingAsset.mLodData[LodIndex].mPhysicalMeshData};

            if (ClothData.mPositions.size() != PhysMesh.mVertices.size()) {
                continue;
            }

            for (Index_t TriangleBaseIndex{}; TriangleBaseIndex + 2 < PhysMesh.mIndices.size(); TriangleBaseIndex += 3) {
                for (Index_t Corner{}; Corner < 3; ++Corner) {
                    const Uint32 Start{PhysMesh.mIndices[TriangleBaseIndex + Corner]};
                    const Uint32 End{PhysMesh.mIndices[TriangleBaseIndex + (Corner + 1) % 3]};
                    const bool Fixed{PhysMesh.mInverseMasses[Start] == 0.0f && PhysMesh.mInverseMasses[End] == 0.0f};

                    DrawInterface.DrawLine(ClothData.mTransform.TransformPosition(ClothData.mPositions[Start]), ClothData.mTransform.TransformPosition(ClothData.mPositions[End]), Fixed ? FixedColor : MovableColor, true, Thickness);
                }
            }
        }
    }

    /**
     * @brief 뼈별 바디가 현재 자세에서 겹치는 다른 바디를 장면 쿼리로 찾습니다.
     */
    bool USkeletalMeshComponent::ComponentOverlapMulti(Core::TArray<PhysicsRuntime::FBodyInstance*>& OutOverlaps) const {
        if (mBodies.empty()) {
            return Engine::UMeshComponent::ComponentOverlapMulti(OutOverlaps);
        }

        OutOverlaps.clear();

        Core::TArray<PhysicsRuntime::FBodyInstance*> BodyOverlaps{};
        bool Queried{};

        for (const auto& Entry : mBodies) {
            const auto& Body{Entry.mInstance};
            if (Body == nullptr || !Body->OverlapMulti(BodyOverlaps)) {
                continue;
            }

            Queried = true;

            for (PhysicsRuntime::FBodyInstance* Other : BodyOverlaps) {
                if (std::find(OutOverlaps.begin(), OutOverlaps.end(), Other) == OutOverlaps.end()) {
                    OutOverlaps.push_back(Other);
                }
            }
        }

        return Queried;
    }

    /**
     * @brief 재등록 때 재해석하도록 에셋 참조를 해제합니다.
     */
    void USkeletalMeshComponent::UnregisterComponent() {
        Engine::UWorld::FDispatchScope Scope{GetWorld()};

        Engine::UMeshComponent::UnregisterComponent();

        // 시뮬레이션은 천 시스템 모듈이 만들었으므로 월드와 모듈이 정리되기 전에 해제합니다.
        ReleaseAllClothingResources();
        mClothingSimulation.reset();

        mPreviewAssets = false;
        mSkeletalMesh = nullptr;
        mSkeleton = nullptr;
        mUnblendedLocalPose.clear();
        mPose.Invalidate();
    }

    void USkeletalMeshComponent::RegisterComponentTickFunctions(bool Register) {
        Engine::UMeshComponent::RegisterComponentTickFunctions(Register);

        if (!Register) {
            mPostPhysicsTick.UnregisterTickFunction();
        } else if (auto* Level{GetOwner() != nullptr ? GetOwner()->GetLevel() : nullptr}) {
            mPostPhysicsTick.RegisterTickFunction(*Level);
        }
    }

    /**
     * @brief 재생 중인 애니메이션을 진행합니다.
     */
    void USkeletalMeshComponent::Tick(float DeltaTime) {
        Engine::UMeshComponent::Tick(DeltaTime);

        // 에디터 월드는 BeginPlay를 받지 않으므로 애니메이션은 PIE와 게임에서만 진행됩니다.
        if (!HasBegunPlay()) {
            return;
        }

        if (ShouldAnimatePhysicsPose()) {
            TickAnimation(DeltaTime);
            if (!mUnblendedLocalPose.empty()) {
                SetAnimatedLocalPose(std::move(mUnblendedLocalPose));
            }

            UpdateKinematicBonesToAnim();
        }

        // 이번 프레임의 포즈가 정해진 뒤에 천을 진행합니다.
        if (!mPostPhysicsTick.IsTickEnabled()) {
            TickClothing(DeltaTime);
        }
    }

    void USkeletalMeshComponent::BeginPlay() {
        Engine::UMeshComponent::BeginPlay();

        // AnimClass가 있으면 그 그래프를 처음 상태로 초기화해 재생하고, 없으면 Animation 시퀀스를 단일 노드로 재생합니다.
        if (!mAnimClass.empty()) {
            if (mAnimInstance == nullptr) {
                CreateAnimClassInstance();
            }

            InitializeAnimClassInstance();
        } else if (!mAnimationPath.empty()) {
            if (const Asset::UAnimSequence* Sequence{LoadAnimation(mAnimationPath)}) {
                PlayAnimation(Sequence, mLooping);
            }
        }
    }

    void USkeletalMeshComponent::EndPlay() {
        if (mAnimInstance != nullptr) {
            mAnimInstance->ResetRootMotion();
        }

        // AnimClass 인스턴스는 설정 값을 담고 있으므로 남겨 둡니다.
        if (mAnimClass.empty()) {
            mAnimInstance.reset();
        }

        Engine::UMeshComponent::EndPlay();
    }

    const Core::FString& USkeletalMeshComponent::GetAnimClass() const {
        return mAnimClass;
    }

    bool USkeletalMeshComponent::SetAnimClass(const Core::FString& ClassName) {
        if (!ClassName.empty()) {
            const CoreUObject::UClass* Class{CoreUObject::FReflectionRegistry::FindClass(ClassName)};

            if (Class == nullptr || !Class->IsChildOf(Engine::UAnimInstance::StaticClass()) || !Class->CanCreate()) {
                return false;
            }
        }

        if (mAnimClass == ClassName && (ClassName.empty() || (mAnimInstance != nullptr && mAnimInstance->GetClass()->GetName() == ClassName))) {
            return true;
        }

        if (const auto* Graph{CoreUObject::Cast<Engine::UAnimGraphInstance>(mAnimInstance.get())}) {
            mInactiveAnimGraph = Graph->GetAnimGraph();
        }

        mAnimClass = ClassName;
        mAnimInstance.reset();

        if (!ClassName.empty()) {
            CreateAnimClassInstance();
            InitializeAnimClassInstance();
        } else if (HasBegunPlay() && !mAnimationPath.empty()) {
            PlayAnimation(LoadAnimation(mAnimationPath), mLooping);
        }

        return true;
    }

    bool USkeletalMeshComponent::CreateAnimClassInstance() {
        const CoreUObject::UClass* Class{mAnimClass.empty() ? nullptr : CoreUObject::FReflectionRegistry::FindClass(mAnimClass)};

        if (Class == nullptr || !Class->IsChildOf(Engine::UAnimInstance::StaticClass())) {
            return false;
        }

        std::unique_ptr<CoreUObject::UObject> Object{Class->CreateInstance()};
        auto* Instance{CoreUObject::Cast<Engine::UAnimInstance>(Object.get())};

        if (Instance == nullptr) {
            return false;
        }

        Object.release();
        mAnimInstance.reset(Instance);
        mAnimInstance->SetName("AnimInstance");
        mAnimInstance->SetOuter(this);

        if (auto* Graph{CoreUObject::Cast<Engine::UAnimGraphInstance>(Instance)}; Graph != nullptr && !mInactiveAnimGraph.empty()) {
            Graph->SetAnimGraph(mInactiveAnimGraph);
            mInactiveAnimGraph.clear();
        }

        return true;
    }

    void USkeletalMeshComponent::InitializeAnimClassInstance() {
        // 에셋은 월드의 에셋 관리자로 로드하므로, 월드에 등록되기 전이면 ResolveMesh가 메시를 해석할 때 초기화합니다.
        if (mAnimInstance != nullptr && GetWorld() != nullptr) {
            mAnimInstance->InitializeAnimation(this);
        }
    }

    void USkeletalMeshComponent::PlayAnimation(const Asset::UAnimSequence* Sequence, bool Looping) {
        auto* SingleNode{CoreUObject::Cast<Engine::UAnimSingleNodeInstance>(mAnimInstance.get())};

        // 언리얼의 AnimationSingleNode 모드와 같이 단일 노드 인스턴스가 아니면 새로 만듭니다.
        if (SingleNode == nullptr) {
            auto Instance{CoreUObject::NewObject<Engine::UAnimSingleNodeInstance>()};

            SingleNode = Instance.get();
            mAnimInstance = std::move(Instance);
            mAnimInstance->InitializeAnimation(this);
            SingleNode->SetRootMotionMode(mSingleNodeRootMotionMode);
        }

        mLooping = Looping;
        SingleNode->SetAnimation(Sequence);
        SingleNode->SetPlayRate(mPlayRate);
        SingleNode->Play(Looping);
    }

    Engine::UAnimInstance* USkeletalMeshComponent::GetAnimInstance() const {
        return mAnimInstance.get();
    }

    bool USkeletalMeshComponent::SetAnimGraphFloat(const Core::FString& Name, float Value) {
        auto* Instance{CoreUObject::Cast<Engine::UAnimGraphInstance>(mAnimInstance.get())};

        return Instance != nullptr && Instance->SetFloatVariable(Name, Value);
    }

    bool USkeletalMeshComponent::SetAnimGraphBool(const Core::FString& Name, bool Value) {
        auto* Instance{CoreUObject::Cast<Engine::UAnimGraphInstance>(mAnimInstance.get())};

        return Instance != nullptr && Instance->SetBoolVariable(Name, Value);
    }

    bool USkeletalMeshComponent::RestartAnimGraphPlayer(const Core::FString& Name) {
        auto* Instance{CoreUObject::Cast<Engine::UAnimGraphInstance>(mAnimInstance.get())};
        auto* Player{Instance != nullptr ? Instance->FindAssetPlayer(Name) : nullptr};

        if (Player == nullptr) {
            return false;
        }

        Player->Initialize(*Instance);
        Instance->ResetRootMotion();

        return true;
    }

    bool USkeletalMeshComponent::IsAnimGraphPlayerFinished(const Core::FString& Name) const {
        const auto* Instance{CoreUObject::Cast<Engine::UAnimGraphInstance>(mAnimInstance.get())};
        const auto* Player{Instance != nullptr ? Instance->FindAssetPlayer(Name) : nullptr};

        return Player != nullptr && Player->IsFinished();
    }

    const Core::FString& USkeletalMeshComponent::GetAnimationPath() const {
        return mAnimationPath;
    }

    bool USkeletalMeshComponent::SetAnimationPath(const Core::FString& Path) {
        if (!IsAssetPath(Path)) {
            return false;
        }

        mAnimationPath = Path;

        // 플레이 중에 바꾸면 새 애니메이션을 바로 재생하고, 비우면 레퍼런스 포즈를 출력합니다.
        if (HasBegunPlay()) {
            PlayAnimation(Path.empty() ? nullptr : LoadAnimation(Path), mLooping);
        }

        return true;
    }

    bool USkeletalMeshComponent::IsLooping() const {
        return mLooping;
    }

    void USkeletalMeshComponent::SetLooping(bool Looping) {
        mLooping = Looping;

        if (auto* SingleNode{CoreUObject::Cast<Engine::UAnimSingleNodeInstance>(mAnimInstance.get())}) {
            SingleNode->SetLooping(Looping);
        }
    }

    float USkeletalMeshComponent::GetPlayRate() const {
        return mPlayRate;
    }

    void USkeletalMeshComponent::SetPlayRate(float PlayRate) {
        mPlayRate = std::isfinite(PlayRate) ? PlayRate : 1.0f;

        if (auto* SingleNode{CoreUObject::Cast<Engine::UAnimSingleNodeInstance>(mAnimInstance.get())}) {
            SingleNode->SetPlayRate(mPlayRate);
        }
    }

    void USkeletalMeshComponent::TickAnimation(float DeltaTime) {
        if (mAnimInstance == nullptr || mSkeletalMesh == nullptr || mSkeleton == nullptr) {
            return;
        }

        mAnimInstance->UpdateAnimation(DeltaTime);

        const Core::TArray<FTransform>& Pose{mAnimInstance->GetPose()};
        Core::TArray<FMatrix> Locals{};

        Locals.reserve(Pose.size());

        for (const FTransform& Transform : Pose) {
            Locals.push_back(Transform.ToMatrix());
        }

        // 스키닝 행렬만 다시 계산하고 피킹 형상과 경계는 갱신하지 않습니다. 프록시를 다시 만들어 새 스키닝 행렬을 렌더러에 넘깁니다.
        if (SetAnimatedLocalPose(std::move(Locals))) {
            if (mApplyRootMotionToActor) {
                // 시뮬레이션 중에는 물리가 트랜스폼을 결정하므로, 루트 모션이 쌓이지 않도록 소비만 하고 적용하지 않습니다.
                const FTransform RootMotion{mAnimInstance->ConsumeExtractedRootMotion()};

                if (!ShouldSimulateBodies()) {
                    ApplyRootMotion(RootMotion);
                }
            }
        } else {
            mAnimInstance->ResetRootMotion();
        }
    }

    Engine::ERootMotionMode USkeletalMeshComponent::GetSingleNodeRootMotionMode() const {
        return mSingleNodeRootMotionMode;
    }

    void USkeletalMeshComponent::SetSingleNodeRootMotionMode(Engine::ERootMotionMode Mode) {
        if (Mode < Engine::ERootMotionMode::NoRootMotionExtraction || Mode > Engine::ERootMotionMode::RootMotionFromEverything) {
            return;
        }

        mSingleNodeRootMotionMode = Mode;

        if (auto* SingleNode{CoreUObject::Cast<Engine::UAnimSingleNodeInstance>(mAnimInstance.get())}) {
            SingleNode->SetRootMotionMode(Mode);
        }
    }

    bool USkeletalMeshComponent::IsRootMotionAppliedToActor() const {
        return mApplyRootMotionToActor;
    }

    Engine::ERootMotionMode USkeletalMeshComponent::GetRootMotionMode() const {
        return mAnimInstance != nullptr ? mAnimInstance->GetRootMotionMode() : mSingleNodeRootMotionMode;
    }

    void USkeletalMeshComponent::SetRootMotionMode(Engine::ERootMotionMode Mode) {
        if (mAnimInstance != nullptr) {
            mAnimInstance->SetRootMotionMode(Mode);
        }

        if (mAnimInstance == nullptr || CoreUObject::Cast<Engine::UAnimSingleNodeInstance>(mAnimInstance.get()) != nullptr) {
            SetSingleNodeRootMotionMode(Mode);
        }
    }

    void USkeletalMeshComponent::SetApplyRootMotionToActor(bool Apply) {
        if (mApplyRootMotionToActor != Apply) {
            mApplyRootMotionToActor = Apply;

            if (mAnimInstance != nullptr) {
                mAnimInstance->ResetRootMotion();
            }
        }
    }

    FTransform USkeletalMeshComponent::ConsumeRootMotion() {
        return mAnimInstance != nullptr ? mAnimInstance->ConsumeExtractedRootMotion() : FTransform{};
    }

    void USkeletalMeshComponent::ApplyRootMotion(const FTransform& Delta) {
        Engine::AActor* Owner{GetOwner()};

        if (Owner == nullptr || (Delta.mLocation.LengthSquared() <= 1.e-12f && Delta.mRotation.Equals(FQuat{}))) {
            return;
        }

        Owner->SetActorTransform(Engine::FAnimationRuntime::ApplyRootMotionToTransform(Delta, GetTransform(), Owner->GetActorTransform()));
    }

    /**
     * @brief 메시 경로를 에셋 관리자에서 해석합니다.
     */
    Asset::USkeletalMesh* USkeletalMeshComponent::LoadMesh(const Core::FString& Path) const {
        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

        if (Assets == nullptr || Path.empty()) {
            return nullptr;
        }

        Core::FString Error{};
        Asset::USkeletalMesh* Mesh{Assets->LoadSkeletalMesh(Core::FilePath::FromUtf8(Path), Error)};

        if (Mesh == nullptr) {
            Core::ErrorHandler::Report("SkeletalMesh", "Cannot load mesh '" + Path + "': " + Error, Core::ErrorHandler::EErrorLevel::Warning);
        }

        return Mesh;
    }

    Asset::USkeleton* USkeletalMeshComponent::LoadSkeleton(const Core::FString& Path) const {
        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

        if (Assets == nullptr) {
            return nullptr;
        }

        if (Path.empty()) {
            return mMeshPath.empty() ? nullptr : Assets->FindLegacySkeleton(Core::FilePath::FromUtf8(mMeshPath));
        }

        Core::FString Error{};
        Asset::USkeleton* Skeleton{Assets->LoadSkeleton(Core::FilePath::FromUtf8(Path), Error)};

        if (Skeleton == nullptr) {
            Core::ErrorHandler::Report("Skeleton", "Cannot load skeleton '" + Path + "': " + Error, Core::ErrorHandler::EErrorLevel::Warning);
        }

        return Skeleton;
    }

    /**
     * @brief 월드의 에셋 관리자를 통해 물리 에셋을 로드합니다.
     */
    PhysicsRuntime::UPhysicsAsset* USkeletalMeshComponent::LoadPhysicsAsset(const Core::FString& Path) const {
        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

        if (Assets == nullptr || Path.empty()) {
            return nullptr;
        }

        Core::FString Error{};
        PhysicsRuntime::UPhysicsAsset* PhysicsAsset{Assets->LoadPhysicsAsset(Core::FilePath::FromUtf8(Path), Error)};

        if (PhysicsAsset == nullptr) {
            Core::ErrorHandler::Report("PhysicsAsset", "Cannot load physics asset '" + Path + "': " + Error, Core::ErrorHandler::EErrorLevel::Warning);
        }

        return PhysicsAsset;
    }

    const Core::FString& USkeletalMeshComponent::GetAssignedPhysicsAssetPath() const {
        return !mPhysicsAssetOverride.empty() || mSkeletalMesh == nullptr ? mPhysicsAssetOverride : mSkeletalMesh->GetPhysicsAssetPath();
    }

    Asset::UAnimSequence* USkeletalMeshComponent::LoadAnimation(const Core::FString& Path) const {
        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

        if (Assets == nullptr || Path.empty()) {
            return nullptr;
        }

        Core::FString Error{};
        Asset::UAnimSequence* Sequence{Assets->LoadAnimSequence(Core::FilePath::FromUtf8(Path), Error)};

        if (Sequence == nullptr) {
            Core::ErrorHandler::Report("Animation", "Cannot load animation '" + Path + "': " + Error, Core::ErrorHandler::EErrorLevel::Warning);
        }

        return Sequence;
    }

    /**
     * @brief 현재 포즈와 재질을 복사한 프록시를 생성합니다.
     */
    Engine::FPrimitiveSceneProxy Engine::USkeletalMeshComponent::CreateSceneProxy() const {
        Engine::FPrimitiveSceneProxy Proxy{Engine::UMeshComponent::CreateSceneProxy()};

        if (mPose.GetRevision().IsValid()) {
            // 렌더러는 프록시에 복사한 스키닝 행렬만 읽으며, 개정이 같으면 FScene::UploadSkinning이 업로드를 건너뜁니다.
            Proxy.mSkeletalMesh = mSkeletalMesh;
            Proxy.mSkinMatrices = mPose.GetSkinMatrices();
            Proxy.mPoseRevision = mPose.GetRevision();

            // 천 섹션은 렌더러가 이 결과의 입자 위치로 정점을 옮겨 그립니다.
            if (mClothingSimulation != nullptr && !mCurrentSimulationData.empty()) {
                Proxy.mClothSimData = mCurrentSimulationData;
                Proxy.mClothBlendWeight = mClothBlendWeight;
                Proxy.mClothRevision = mClothRevision;
            }
        }

        return Proxy;
    }

    /**
     * @brief 저장 대상인 로컬 본 포즈를 반환합니다.
     */
    const Core::TArray<FMatrix>& Engine::USkeletalMeshComponent::GetSerializedPose() const {
        return mPose.GetLocalTransforms();
    }

    // 전역 행렬과 피킹 형상은 복원 시 다시 계산하고 로컬 포즈만 저장합니다.

    /**
     * @brief 저장된 로컬 본 포즈를 검증하여 복원합니다.
     */
    bool Engine::USkeletalMeshComponent::SetSerializedPose(const Core::TArray<FMatrix>& Transforms) {
        for (const FMatrix& Transform : Transforms) {
            FMatrix Inverse{};

            if (!Transform.IsFinite() || !Transform.TryInverse(Inverse) || !Inverse.IsFinite()) {
                return false;
            }
        }

        return mPose.SetLocalTransforms(Transforms);
    }

    /**
     * @brief 프로퍼티를 직렬화하고 로드한 포즈를 에셋에 다시 연결합니다.
     */
    void Engine::USkeletalMeshComponent::Serialize(Core::FArchive& Archive) {
        if (Archive.IsLoading()) {
            mPreviewAssets = false;
            mInactiveAnimGraph.clear();
            mUnblendedLocalPose.clear();
        }

        Engine::UMeshComponent::Serialize(Archive);

        if (!Archive.HasError() && (!Archive.IsLoading() || Archive.HasField("InactiveAnimGraph"))) {
            Archive.Serialize("InactiveAnimGraph", mInactiveAnimGraph);

            if (Archive.IsLoading() && !IsAssetPath(mInactiveAnimGraph)) {
                Archive.SetError("Invalid inactive animation graph path");
            }
        }

        // AnimClass 인스턴스의 설정 값은 AnimInstance 객체 안에 저장합니다. 식별자와 이름 같은 UObject 공통 프로퍼티는 저장하지 않습니다.
        // 읽을 때는 AnimClass setter가 이미 인스턴스를 만들었습니다.
        Engine::UAnimInstance* Instance{!mAnimClass.empty() && mAnimInstance != nullptr && mAnimInstance->GetClass()->GetName() == mAnimClass ? mAnimInstance.get() : nullptr};

        if (Instance != nullptr && !Archive.HasError() && (!Archive.IsLoading() || Archive.HasField("AnimInstance")) && Archive.BeginObject("AnimInstance")) {
            for (const bool Late : {false, true}) {
                for (const CoreUObject::UClass* Class{Instance->GetClass()}; Class != nullptr && Class->IsChildOf(Engine::UAnimInstance::StaticClass()); Class = Class->GetSuperClass()) {
                    for (const CoreUObject::FProperty* Property : Class->GetProperties()) {
                        if (Property->HasFlag(Core::EPropertyFlags::Serialize) && Property->HasFlag(Core::EPropertyFlags::SerializeLate) == Late) {
                            Property->SerializeValue(Instance, Archive);
                        }
                    }
                }
            }

            Archive.EndObject();

            // 읽은 설정으로 그래프를 다시 만듭니다. 메시가 없으면 아래 ResolveMesh가 다시 초기화하지 않습니다.
            if (Archive.IsLoading() && !Archive.HasError()) {
                InitializeAnimClassInstance();
            }
        }

        if (Archive.IsLoading()) {
            if (!Archive.HasError()) {
                // 경로가 같아도 읽어 온 포즈를 다시 적용해야 하므로 에셋 참조를 비워 강제로 다시 해석합니다.
                mSkeletalMesh = nullptr;
                mSkeleton = nullptr;
                ResolveMesh();
            }

            MarkRenderStateDirty();
        }
    }

}
