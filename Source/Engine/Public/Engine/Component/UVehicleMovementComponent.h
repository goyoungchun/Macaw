/**
 * @file UVehicleMovementComponent.h
 * @brief Pawn의 조작 입력과 PhysX 차체를 연결하는 차량 이동 기반 클래스입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Math/FTransform.h"
#include "Core/Literals/QuantityTypes.h"
#include "CoreUObject/TDelegate.h"
#include "Engine/Component/UPawnMovementComponent.h"
#include "UVehicleMovementComponent.generated.h"

namespace physx {
    class PxVehicleWheelsSimData;
}

namespace PhysicsRuntime {
    struct FBodyInstance;
    class FVehicleManager;
}

namespace Engine {

    class USkeletalMeshComponent;
    class UStaticMeshComponent;

    /** @brief 차체 로컬 좌표와 미터·킬로그램 단위로 지정하는 바퀴 및 서스펜션 설정입니다. */
    USTRUCT()
    struct FVehicleWheelSettings {
        GENERATED_BODY()

        UPROPERTY(Edit, Serialize)
        FVector mCenterOffset{};

        UPROPERTY(Edit, Serialize)
        Core::FLength mRadius{Core::FLength::FromBase(0.32f)};

        UPROPERTY(Edit, Serialize)
        Core::FLength mWidth{Core::FLength::FromBase(0.22f)};

        UPROPERTY(Edit, Serialize)
        Core::FMass mMass{Core::FMass::FromBase(20.0f)};

        UPROPERTY(Edit, Serialize)
        Core::FLength mMaxCompression{Core::FLength::FromBase(0.25f)};

        UPROPERTY(Edit, Serialize)
        Core::FLength mMaxDroop{Core::FLength::FromBase(0.2f)};

        UPROPERTY(Edit, Serialize)
        Core::FSpringStiffness mSpringStrength{Core::FSpringStiffness::FromBase(35000.0f)};

        UPROPERTY(Edit, Serialize)
        Core::FLinearDampingCoefficient mDamperRate{Core::FLinearDampingCoefficient::FromBase(4500.0f)};

        bool operator==(const FVehicleWheelSettings&) const = default;
    };

    /** @brief 최근 서스펜션 업데이트에서 얻은 차체 로컬 자세와 접지 상태입니다. */
    struct FVehicleWheelState {
        FTransform mLocalPose{};
        Core::FLength mSuspensionJounce{};
        bool mInAir{true};
    };

    /** @brief 등록된 강체의 수명에 맞춰 차량을 연결하고 고정 스텝에 조작 입력을 넘깁니다. */
    UCLASS(Abstract)
    class UVehicleMovementComponent : public Engine::UPawnMovementComponent {
        GENERATED_BODY()

    public:
        UVehicleMovementComponent();

        const Core::TArray<Engine::FVehicleWheelSettings>& GetWheelSettings() const;

        /** @brief 바퀴 설정을 저장하고 이미 연결된 차량을 갱신합니다. 빈 목록이면 차량 등록을 해제합니다. */
        bool SetWheelSettings(const Core::TArray<Engine::FVehicleWheelSettings>& Wheels);

        /** @brief 등록된 차량의 바퀴 결과를 값으로 복사합니다. */
        bool GetWheelState(Uint32 WheelIndex, Engine::FVehicleWheelState& OutState) const;

        /** @brief 같은 Pawn의 충돌이 꺼진 메시를 바퀴에 연결합니다. nullptr로 연결을 해제합니다. */
        bool SetWheelMesh(Uint32 WheelIndex, Engine::UStaticMeshComponent* Mesh);
        Engine::UStaticMeshComponent* GetWheelMesh(Uint32 WheelIndex) const;

        /**
         * @brief 바퀴 본을 움직일 같은 Pawn의 스켈레탈 메시입니다. nullptr로 연결을 해제합니다.
         * @details 차체(이동 대상) 자신과 충돌이 켜진 메시는 지정할 수 없습니다. 본 이름 목록은 유지합니다.
         */
        bool SetWheelSkeletalMesh(Engine::USkeletalMeshComponent* Mesh);
        Engine::USkeletalMeshComponent* GetWheelSkeletalMesh() const;

        /**
         * @brief 바퀴에 스켈레탈 메시의 본을 연결합니다. 빈 이름으로 연결을 해제합니다.
         * @details 바인드 포즈를 정지 상태로 보고, 바퀴 중심(mCenterOffset) 기준의 조향·굴림·서스펜션 변화만 본에 더합니다.
         */
        bool SetWheelBone(Uint32 WheelIndex, const Core::FString& BoneName);
        const Core::FString& GetWheelBone(Uint32 WheelIndex) const;

        /** @brief 엔진이 있는 차량의 입력입니다. 가속과 조향은 [-1, 1], 제동은 [0, 1]이며 음수 가속은 후진을 요청합니다. NoDrive 차량은 false를 반환합니다. */
        bool SetDriveInput(float Throttle, float Brake, float Steering);

        /** @brief 차체 위치를 보존하면서 속도와 구동계 상태를 초기화합니다. NoDrive 차량은 false를 반환합니다. */
        bool ResetDrive();

        /** @brief 후진은 -1, 중립은 0, 전진은 1부터 시작합니다. NoDrive 차량은 0입니다. */
        Int32 GetCurrentGear() const;

        /** @brief 차체 전방 기준의 부호 있는 속도(m/s)입니다. */
        Core::FSpeed GetForwardSpeed() const;

        void Tick(float DeltaTime) override;
        void EndPlay() override;
        bool SetTickGroup(Engine::ETickingGroup Group) override;
        void RegisterComponentTickFunctions(bool Register) override;

    protected:
        bool CanUseUpdatedComponent(const Engine::USceneComponent& Component) const override;
        void OnUpdatedComponentChanged(Engine::USceneComponent* Component) override;

        /** @brief 준비된 바퀴 설정으로 파생 클래스의 구동 방식을 장면에 등록합니다. */
        virtual bool CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                                   const physx::PxVehicleWheelsSimData& Wheels) = 0;

        /** @brief PrePhysics에서 Pawn의 이번 입력을 차량에 전달합니다. */
        virtual void ApplyVehicleInput(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                                       const FVector& Input);

        /** @brief PostPhysics에서 차체 자세와 속도를 반영한 뒤 호출합니다. */
        virtual void UpdateVehicleVisuals();

        PhysicsRuntime::FVehicleManager* GetVehicleManager() const;
        const PhysicsRuntime::FBodyInstance* GetRegisteredBody() const;

    private:
        class FPostPhysicsTick final : public Engine::FTickFunction {
        public:
            explicit FPostPhysicsTick(UVehicleMovementComponent& Target);

        private:
            const CoreUObject::UObject* GetTarget() const override;
            void ExecuteTick(float DeltaTime) override;

            UVehicleMovementComponent& mTarget;
        };

        void OnBodyStateChanged(Engine::UPrimitiveComponent* Chassis, bool Created);
        void TryRegisterVehicle();
        void UnregisterVehicle();
        void SynchronizeVelocity();
        void UpdateWheelBones(const FTransform& ChassisPose);

        UPROPERTY(Edit, Serialize, Getter = GetWheelSettings, Setter = SetWheelSettings)
        Core::TArray<Engine::FVehicleWheelSettings> mWheelSettings{};

        UPROPERTY(Serialize, Name = "WheelMeshes")
        Core::TArray<CoreUObject::TObjectRef<Engine::UStaticMeshComponent>> mWheelMeshes{};

        UPROPERTY(Serialize, Name = "WheelSkeletalMesh")
        CoreUObject::TObjectRef<Engine::USkeletalMeshComponent> mWheelSkeletalMesh{};

        UPROPERTY(Serialize, Name = "WheelBones")
        Core::TArray<Core::FString> mWheelBones{};

        CoreUObject::TObjectRef<Engine::UPrimitiveComponent> mChassis{};
        CoreUObject::FDelegateHandle mBodyStateHandle{};
        bool mVehicleRegistered{};
        FPostPhysicsTick mPostPhysicsTick{*this};
    };

}
