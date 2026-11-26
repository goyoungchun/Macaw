/**
 * @file UVehicleMovementComponent.cpp
 * @brief 차체 수명, 차량 입력과 물리 결과를 연결합니다.
 * @ingroup Engine
 */

#include "Engine/Component/UVehicleMovementComponent.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Engine/AActor.h"
#include "Engine/Component/UPrimitiveComponent.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Engine/Component/UStaticMeshComponent.h"
#include "Asset/USkeleton.h"
#include "Engine/UWorld.h"
#include "PhysicsRuntime/FPhysicsScene.h"
#include "PhysicsRuntime/FVehicleManager.h"
#include "PhysicsRuntime/PhysicsConversion.h"

#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleUtilSetup.h>
#include <vehicle/PxVehicleWheels.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace Engine {

    namespace {
        template <Core::FDimension D>
        bool IsPositiveFinite(Core::TQuantity<D, float> Value) {
            return std::isfinite(Value.GetBase()) && Value > decltype(Value){};
        }

        Core::FMomentOfInertia ComputeWheelInertia(const FVehicleWheelSettings& Wheel) {
            return 0.5f * Wheel.mMass * Wheel.mRadius * Wheel.mRadius;
        }

        bool IsValidWheel(const FVehicleWheelSettings& Wheel) {
            return Wheel.mCenterOffset.IsFinite() && IsPositiveFinite(Wheel.mRadius) && IsPositiveFinite(Wheel.mWidth) &&
                   IsPositiveFinite(Wheel.mMass) && IsPositiveFinite(ComputeWheelInertia(Wheel)) &&
                   IsPositiveFinite(Wheel.mMaxCompression) && IsPositiveFinite(Wheel.mMaxDroop) &&
                   IsPositiveFinite(Wheel.mSpringStrength) &&
                   std::isfinite(Wheel.mDamperRate.GetBase()) && Wheel.mDamperRate >= Core::FLinearDampingCoefficient{};
        }

        bool ConfigureWheels(physx::PxVehicleWheelsSimData& Data, const Core::TArray<FVehicleWheelSettings>& Settings,
                             const physx::PxRigidDynamic& Chassis) {
            const auto Count{Data.getNbWheels()};
            const auto CenterOfMass{Chassis.getCMassLocalPose()};
            std::array<physx::PxVec3, PX_MAX_NB_WHEELS> WheelCenters{};
            std::array<physx::PxReal, PX_MAX_NB_WHEELS> SprungMasses{};

            for (physx::PxU32 Wheel{}; Wheel < Count; ++Wheel) {
                WheelCenters[Wheel] = PhysicsRuntime::ToPxVec3(Settings[Wheel].mCenterOffset);
            }

            constexpr physx::PxU32 GravityAxisZ{2};
            physx::PxVehicleComputeSprungMasses(Count, WheelCenters.data(), CenterOfMass.p, Chassis.getMass(), GravityAxisZ, SprungMasses.data());

            if (!std::all_of(SprungMasses.begin(), SprungMasses.begin() + Count, [](physx::PxReal Mass) {
                return IsPositiveFinite(Core::FMass::FromBase(Mass));
            })) {
                return false;
            }

            for (physx::PxU32 Wheel{}; Wheel < Count; ++Wheel) {
                const FVehicleWheelSettings& Setting{Settings[Wheel]};
                physx::PxVehicleWheelData WheelData{};
                WheelData.mRadius = Setting.mRadius.GetBase();
                WheelData.mWidth = Setting.mWidth.GetBase();
                WheelData.mMass = Setting.mMass.GetBase();
                WheelData.mMOI = ComputeWheelInertia(Setting).GetBase();

                physx::PxVehicleSuspensionData Suspension{};
                Suspension.mMaxCompression = Setting.mMaxCompression.GetBase();
                Suspension.mMaxDroop = Setting.mMaxDroop.GetBase();
                Suspension.mSpringStrength = Setting.mSpringStrength.GetBase();
                Suspension.mSpringDamperRate = Setting.mDamperRate.GetBase();
                Suspension.mSprungMass = SprungMasses[Wheel];

                const auto Offset{CenterOfMass.transformInv(WheelCenters[Wheel])};
                Data.setWheelData(Wheel, WheelData);
                Data.setSuspensionData(Wheel, Suspension);
                Data.setWheelCentreOffset(Wheel, Offset);
                Data.setSuspTravelDirection(Wheel, CenterOfMass.q.rotateInv({0.0f, 0.0f, -1.0f}));
                Data.setSuspForceAppPointOffset(Wheel, Offset);
                Data.setTireForceAppPointOffset(Wheel, Offset);

                // 서스펜션 레이와 별도로 차체의 기존 충돌 형상을 보존합니다.
                Data.setWheelShapeMapping(Wheel, -1);
            }

            return true;
        }
    }

    UVehicleMovementComponent::UVehicleMovementComponent() = default;

    const Core::TArray<FVehicleWheelSettings>& UVehicleMovementComponent::GetWheelSettings() const {
        return mWheelSettings;
    }

    bool UVehicleMovementComponent::SetWheelSettings(const Core::TArray<FVehicleWheelSettings>& Wheels) {
        if (Wheels.size() > PX_MAX_NB_WHEELS || !std::ranges::all_of(Wheels, IsValidWheel)) {
            return false;
        }

        if (mWheelSettings != Wheels) {
            UnregisterVehicle();
            mWheelSettings = Wheels;
            TryRegisterVehicle();
        }

        return true;
    }

    bool UVehicleMovementComponent::GetWheelState(Uint32 WheelIndex, FVehicleWheelState& OutState) const {
        const auto* Body{GetRegisteredBody()};
        const auto* Manager{GetVehicleManager()};

        if (Body == nullptr || Manager == nullptr) {
            return false;
        }

        const auto Results{Manager->GetWheelQueryResults(*Body)};
        if (WheelIndex >= Results.size()) {
            return false;
        }

        const auto& Wheel{Results[WheelIndex]};
        OutState = {PhysicsRuntime::ToFTransform(Wheel.localPose, {1.0f, 1.0f, 1.0f}), Core::FLength::FromBase(Wheel.suspJounce), Wheel.isInAir};

        return true;
    }

    bool UVehicleMovementComponent::SetWheelMesh(Uint32 WheelIndex, Engine::UStaticMeshComponent* Mesh) {
        if (WheelIndex >= GetWheelSettings().size()) {
            return false;
        }

        const auto Reference{CoreUObject::MakeObjectRef(Mesh)};
        if (Mesh != nullptr && (GetOwner() == nullptr || Mesh->GetOwner() != GetOwner() || Mesh->IsCollisionEnabled() ||
                               (GetWheelMesh(WheelIndex) != Mesh && std::ranges::find(mWheelMeshes, Reference) != mWheelMeshes.end()))) {
            return false;
        }

        if (WheelIndex >= mWheelMeshes.size()) {
            mWheelMeshes.resize(WheelIndex + 1);
        }

        mWheelMeshes[WheelIndex] = Reference;
        return true;
    }

    Engine::UStaticMeshComponent* UVehicleMovementComponent::GetWheelMesh(Uint32 WheelIndex) const {
        if (WheelIndex >= GetWheelSettings().size() || WheelIndex >= mWheelMeshes.size()) {
            return nullptr;
        }

        auto* Mesh{mWheelMeshes[WheelIndex].Get()};

        return Mesh != nullptr && Mesh->GetOwner() == GetOwner() && !Mesh->IsCollisionEnabled() ? Mesh : nullptr;
    }

    bool UVehicleMovementComponent::SetWheelSkeletalMesh(Engine::USkeletalMeshComponent* Mesh) {
        // 충돌이 켜진 메시는 본마다 키네마틱 바디를 만들어 차체와 겹치므로 차체를 계속 밀어냅니다.
        if (Mesh != nullptr && (GetOwner() == nullptr || Mesh->GetOwner() != GetOwner() || Mesh == GetUpdatedComponent() || Mesh->IsCollisionEnabled())) {
            return false;
        }

        mWheelSkeletalMesh = CoreUObject::MakeObjectRef(Mesh);
        return true;
    }

    Engine::USkeletalMeshComponent* UVehicleMovementComponent::GetWheelSkeletalMesh() const {
        auto* Mesh{mWheelSkeletalMesh.Get()};

        return Mesh != nullptr && Mesh->GetOwner() == GetOwner() && Mesh != GetUpdatedComponent() && !Mesh->IsCollisionEnabled() ? Mesh : nullptr;
    }

    bool UVehicleMovementComponent::SetWheelBone(Uint32 WheelIndex, const Core::FString& BoneName) {
        if (WheelIndex >= GetWheelSettings().size()) {
            return false;
        }

        // 메시가 이미 있으면 존재하는 본만 받고, 같은 본을 두 바퀴에 연결하지 않습니다.
        const auto* Mesh{GetWheelSkeletalMesh()};
        if (!BoneName.empty()) {
            if (Mesh != nullptr && Mesh->GetBoneIndex(BoneName) == InvalidIndex) {
                return false;
            }

            for (Uint32 Other{}; Other < mWheelBones.size(); ++Other) {
                if (Other != WheelIndex && mWheelBones[Other] == BoneName) {
                    return false;
                }
            }
        }

        if (WheelIndex >= mWheelBones.size()) {
            mWheelBones.resize(WheelIndex + 1);
        }

        mWheelBones[WheelIndex] = BoneName;
        return true;
    }

    const Core::FString& UVehicleMovementComponent::GetWheelBone(Uint32 WheelIndex) const {
        static const Core::FString Empty{};

        return WheelIndex < GetWheelSettings().size() && WheelIndex < mWheelBones.size() ? mWheelBones[WheelIndex] : Empty;
    }

    bool UVehicleMovementComponent::SetDriveInput(float Throttle, float Brake, float Steering) {
        const auto* Body{GetRegisteredBody()};
        auto* Manager{GetVehicleManager()};

        return Body != nullptr && Manager != nullptr && Manager->SetDriveInput(*Body, Throttle, Brake, Steering);
    }

    bool UVehicleMovementComponent::ResetDrive() {
        const auto* Body{GetRegisteredBody()};
        auto* Manager{GetVehicleManager()};

        if (Body == nullptr || Manager == nullptr || !Manager->ResetDrive(*Body)) {
            return false;
        }

        SetVelocity({});
        return true;
    }

    Int32 UVehicleMovementComponent::GetCurrentGear() const {
        const auto* Body{GetRegisteredBody()};
        const auto* Manager{GetVehicleManager()};

        return Body != nullptr && Manager != nullptr ? Manager->GetDriveGear(*Body) : 0;
    }

    Core::FSpeed UVehicleMovementComponent::GetForwardSpeed() const {
        const auto* Chassis{GetUpdatedPrimitive()};
        if (Chassis == nullptr) {
            return {};
        }

        const auto Forward{Chassis->GetTransform().mRotation.RotateVector({1.0f, 0.0f, 0.0f})};
        return Core::FSpeed::FromBase(GetVelocity().Dot(Forward));
    }

    void UVehicleMovementComponent::Tick(float DeltaTime) {
        if (!HasBegunPlay() || ShouldSkipUpdate(Core::FTime::FromBase(DeltaTime))) {
            return;
        }

        const FVector Input{ConsumeInputVector()};
        const auto* Body{GetRegisteredBody()};
        auto* Manager{GetVehicleManager()};

        if (Body != nullptr && Manager != nullptr) {
            ApplyVehicleInput(*Manager, *Body, Input);
        }
    }

    void UVehicleMovementComponent::ApplyVehicleInput(PhysicsRuntime::FVehicleManager&, const PhysicsRuntime::FBodyInstance&, const FVector&) {
    }

    void UVehicleMovementComponent::UpdateVehicleVisuals() {
        const auto* Chassis{GetUpdatedPrimitive()};

        if (Chassis == nullptr) {
            return;
        }

        // 바퀴 오프셋은 이미 바디 로컬 미터 단위입니다. 차체 메시의 스케일을 다시 곱하지 않습니다.
        const FTransform ChassisPose{Chassis->GetTransform().mLocation, Chassis->GetTransform().mRotation, {1.0f, 1.0f, 1.0f}};
        for (Uint32 Wheel{}; Wheel < std::min(mWheelMeshes.size(), GetWheelSettings().size()); ++Wheel) {
            auto* Mesh{GetWheelMesh(Wheel)};
            FVehicleWheelState State{};

            if (Mesh == nullptr || Mesh->GetWorld() != GetWorld() || !GetWheelState(Wheel, State)) {
                continue;
            }

            FTransform WheelPose{State.mLocalPose * ChassisPose};
            WheelPose.mScale = Mesh->GetTransform().mScale;
            Mesh->SetTransform(WheelPose);
        }

        UpdateWheelBones(ChassisPose);
    }

    void UVehicleMovementComponent::UpdateWheelBones(const FTransform& ChassisPose) {
        auto* Mesh{GetWheelSkeletalMesh()};
        const auto* Skeleton{Mesh != nullptr ? Mesh->GetSkeleton() : nullptr};

        if (Mesh == nullptr || Skeleton == nullptr || Mesh->GetWorld() != GetWorld()) {
            return;
        }

        // 메시 공간 → 차체 바디 공간 변환 K입니다. 행 벡터 규약이므로 왼쪽 행렬이 먼저 적용됩니다.
        FMatrix ChassisInverse{};
        FMatrix MeshInverse{};
        if (!ChassisPose.ToMatrix().TryInverse(ChassisInverse) || !Mesh->GetTransform().ToMatrix().TryInverse(MeshInverse)) {
            return;
        }

        const FMatrix MeshToChassis{Mesh->GetTransform().ToMatrix() * ChassisInverse};
        const FMatrix ChassisToMesh{ChassisPose.ToMatrix() * MeshInverse};

        const auto& Bones{Skeleton->GetBones()};
        const auto& BindGlobals{Skeleton->GetGlobalBindPoses()};
        const auto& PoseGlobals{Mesh->GetPose().GetGlobalTransforms()};
        Core::TArray<FMatrix> Locals{Mesh->GetPose().GetLocalTransforms()};
        bool Changed{};

        if (Locals.size() != Bones.size() || BindGlobals.size() != Bones.size() || PoseGlobals.size() != Bones.size()) {
            return;
        }

        for (Uint32 Wheel{}; Wheel < std::min(mWheelBones.size(), GetWheelSettings().size()); ++Wheel) {
            const Index_t Bone{mWheelBones[Wheel].empty() ? InvalidIndex : Mesh->GetBoneIndex(mWheelBones[Wheel])};
            FVehicleWheelState State{};

            if (Bone == InvalidIndex || Bone >= Bones.size() || !GetWheelState(Wheel, State)) {
                continue;
            }

            // 바인드 포즈를 mCenterOffset에 놓인 정지 바퀴로 보고, 바퀴 중심 기준 변화량 D만 본에 더합니다.
            // 본 축이 모델링 도구마다 달라도 바인드 회전을 유지하므로 결과가 바뀌지 않습니다.
            const FMatrix WheelDelta{FMatrix::Translation(-GetWheelSettings()[Wheel].mCenterOffset) * State.mLocalPose.ToMatrix()};
            const FMatrix BoneInMesh{BindGlobals[Bone] * MeshToChassis * WheelDelta * ChassisToMesh};

            // 부모 본은 이번 프레임 포즈(애니메이션 포함)를 기준으로 로컬 변환을 구합니다.
            const Int32 Parent{Bones[Bone].GetParentIndex()};
            FMatrix ParentInverse{FMatrix::Identity()};
            if (Parent >= 0 && (static_cast<Index_t>(Parent) >= PoseGlobals.size() || !PoseGlobals[Parent].TryInverse(ParentInverse))) {
                continue;
            }

            Locals[Bone] = BoneInMesh * ParentInverse;
            Changed = true;
        }

        if (Changed) {
            Mesh->SetAnimatedLocalPose(std::move(Locals));
        }
    }

    void UVehicleMovementComponent::EndPlay() {
        UnregisterVehicle();
        UPawnMovementComponent::EndPlay();
    }

    bool UVehicleMovementComponent::SetTickGroup(Engine::ETickingGroup Group) {
        return Group == Engine::ETickingGroup::PrePhysics && UPawnMovementComponent::SetTickGroup(Group);
    }

    void UVehicleMovementComponent::RegisterComponentTickFunctions(bool Register) {
        UPawnMovementComponent::RegisterComponentTickFunctions(Register);

        if (!Register) {
            mPostPhysicsTick.UnregisterTickFunction();
        } else if (auto* Level{GetOwner() != nullptr ? GetOwner()->GetLevel() : nullptr}) {
            mPostPhysicsTick.RegisterTickFunction(*Level);
        }
    }

    bool UVehicleMovementComponent::CanUseUpdatedComponent(const Engine::USceneComponent& Component) const {
        return UPawnMovementComponent::CanUseUpdatedComponent(Component) && CoreUObject::Cast<Engine::UPrimitiveComponent>(&Component) != nullptr;
    }

    void UVehicleMovementComponent::OnUpdatedComponentChanged(Engine::USceneComponent* Component) {
        auto* Chassis{CoreUObject::Cast<Engine::UPrimitiveComponent>(Component)};
        const auto Target{CoreUObject::MakeObjectRef(Chassis)};

        if (mChassis == Target) {
            return;
        }

        UnregisterVehicle();

        if (auto* Previous{mChassis.Get()}) {
            Previous->mOnBodyStateChanged.Remove(mBodyStateHandle);
        }

        mChassis = Target;
        mBodyStateHandle = {};

        if (Chassis != nullptr) {
            mBodyStateHandle = Chassis->mOnBodyStateChanged.AddDynamic(this, &UVehicleMovementComponent::OnBodyStateChanged);
            TryRegisterVehicle();
        }
    }

    void UVehicleMovementComponent::OnBodyStateChanged(Engine::UPrimitiveComponent* Chassis, bool Created) {
        if (Chassis != mChassis.Get()) {
            return;
        }

        if (Created) {
            TryRegisterVehicle();
        } else {
            UnregisterVehicle();
        }
    }

    PhysicsRuntime::FVehicleManager* UVehicleMovementComponent::GetVehicleManager() const {
        const auto* World{GetWorld()};
        const auto* Scene{World != nullptr ? World->GetPhysicsScene() : nullptr};

        return Scene != nullptr ? Scene->GetVehicleManager() : nullptr;
    }

    const PhysicsRuntime::FBodyInstance* UVehicleMovementComponent::GetRegisteredBody() const {
        const auto* Chassis{mVehicleRegistered ? GetUpdatedPrimitive() : nullptr};

        return Chassis != nullptr ? &Chassis->GetBodyInstance() : nullptr;
    }

    void UVehicleMovementComponent::TryRegisterVehicle() {
        if (mVehicleRegistered || mWheelSettings.empty()) {
            return;
        }

        const auto* Chassis{GetUpdatedPrimitive()};
        auto* Manager{GetVehicleManager()};
        const auto* Body{Chassis != nullptr ? &Chassis->GetBodyInstance() : nullptr};
        const auto* Actor{Body != nullptr ? Body->GetPxRigidDynamic() : nullptr};

        if (Manager == nullptr || Actor == nullptr || Actor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)) {
            return;
        }

        std::unique_ptr<physx::PxVehicleWheelsSimData, decltype([](auto* Data) { Data->free(); })> Wheels{
            physx::PxVehicleWheelsSimData::allocate(static_cast<physx::PxU32>(mWheelSettings.size()))};

        if (Wheels != nullptr && ConfigureWheels(*Wheels, mWheelSettings, *Actor) && CreateVehicle(*Manager, *Body, *Wheels)) {
            mVehicleRegistered = true;
            mPostPhysicsTick.SetTickEnabled(true);
            SynchronizeVelocity();
        }
    }

    void UVehicleMovementComponent::UnregisterVehicle() {
        if (mVehicleRegistered) {
            mVehicleRegistered = false;

            const auto* Chassis{mChassis.Get()};
            if (auto* Manager{GetVehicleManager()}; Manager != nullptr && Chassis != nullptr) {
                Manager->UnregisterVehicle(Chassis->GetBodyInstance());
            }
        }

        mPostPhysicsTick.SetTickEnabled(false);
        UMovementComponent::SetVelocity({});
    }

    void UVehicleMovementComponent::SynchronizeVelocity() {
        const auto* Body{GetRegisteredBody()};
        UMovementComponent::SetVelocity(Body != nullptr ? Body->GetLinearVelocity() : FVector{});
    }

    UVehicleMovementComponent::FPostPhysicsTick::FPostPhysicsTick(UVehicleMovementComponent& Target)
        : mTarget{Target} {
        SetTickGroup(Engine::ETickingGroup::PostPhysics);
        SetTickEnabled(false);
    }

    const CoreUObject::UObject* UVehicleMovementComponent::FPostPhysicsTick::GetTarget() const {
        return &mTarget;
    }

    void UVehicleMovementComponent::FPostPhysicsTick::ExecuteTick(float) {
        mTarget.SynchronizeVelocity();
        mTarget.UpdateVehicleVisuals();
    }

}
