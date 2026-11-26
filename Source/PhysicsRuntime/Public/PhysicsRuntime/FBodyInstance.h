/**
 * @file FBodyInstance.h
 * @brief 컴포넌트 하나에 대응하는 PhysX 강체와 그 시뮬레이션 설정을 관리합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/ReflectionMacros.h"
#include "Core/Math/FTransform.h"
#include "Core/Math/FVector.h"
#include "PhysicsRuntime/FPhysXRelease.h"
#include "PhysicsRuntime/CollisionTypes.h"

#include <functional>
#include <memory>

#include "FBodyInstance.generated.h"

namespace physx {
    class PxActor;
    class PxRigidActor;
    class PxRigidDynamic;
}

namespace Engine {
    class UPrimitiveComponent;
}

namespace PhysicsRuntime {
    class FPhysicsScene;
    class UBodySetup;
}

namespace PhysicsRuntime {

    /**
     * @brief 바디를 만들 때 강체 종류를 정하는 옵션입니다.
     * @ingroup PhysicsRuntime
     */
    struct FInitBodySpawnParams {
    public:
        /**
         * @brief true이면 고정된 강체를 만들고, false이면 움직이는 강체를 만듭니다.
         * @note 움직이는 강체가 시뮬레이션하지 않으면 코드로만 옮기는 키네마틱 강체가 됩니다.
         */
        bool mStaticPhysics{true};
    };

    struct FBodyInstance;

    /**
     * @brief 바디를 이동 경로로 쓸었을 때 처음 막힌 지점의 정보입니다.
     * @ingroup PhysicsRuntime
     */
    struct FBodySweepHit {
    public:
        /**
         * @brief 막은 바디이며, FBodyInstance가 만들지 않은 강체이면 nullptr입니다.
         */
        PhysicsRuntime::FBodyInstance* mBody{};

        /**
         * @brief 이동 시작점에서 막힌 지점까지 이동한 거리(m)입니다.
         */
        Core::FLength mDistance{};

        /**
         * @brief 두 형상이 닿은 월드 위치입니다.
         */
        FVector mImpactPoint{};

        /**
         * @brief 막은 표면에서 이동한 바디 쪽을 향하는 법선입니다.
         */
        FVector mImpactNormal{};

        /**
         * @brief 시작부터 겹쳐 있었을 때의 침투 깊이(m)입니다.
         */
        Core::FLength mPenetrationDepth{};

        /**
         * @brief 이동을 시작할 때 이미 겹쳐 있었는지 여부입니다.
         */
        bool mStartPenetrating{};
    };

    /**
     * @brief 쿼리에서 제외할 바디를 고르는 판정 함수입니다.
     */
    using FIgnoreBodyPredicate = std::function<bool(const PhysicsRuntime::FBodyInstance&)>;

    /**
     * @brief 컴포넌트 하나에 대응하는 PhysX 강체와 그 시뮬레이션 설정을 관리합니다.
     * @details 물리 시뮬레이션을 켜면 동적 강체를, 끄면 움직이지 않는 정적 강체를 만듭니다.
     *          만든 강체의 userData에 이 객체의 주소를 넣어 시뮬레이션 결과와 접촉에서 소유 컴포넌트를 찾을 수 있게 합니다.
     *          설정 Setter는 실제 바디가 있으면 즉시 반영하지만, 시뮬레이션 여부는 강체 종류가 바뀌므로 바디를 다시 만들어야 반영됩니다.
     * @note 복사와 이동은 설정만 옮기고 실제 바디는 옮기지 않습니다. 대상 객체의 기존 바디는 해제합니다.
     * @ingroup PhysicsRuntime
     */
    USTRUCT()
    struct FBodyInstance {
        GENERATED_BODY()

    public:

        /**
         * @brief 실제 바디가 없는 기본 설정의 바디 인스턴스를 구성합니다.
         */
        FBodyInstance();

        /**
         * @brief 다른 바디 인스턴스의 설정만 복사합니다.
         * @param[in] Other 설정을 복사할 바디 인스턴스입니다.
         */
        FBodyInstance(const FBodyInstance& Other);

        /**
         * @brief 이 객체의 실제 바디를 해제하고 다른 바디 인스턴스의 설정만 복사합니다.
         * @param[in] Other 설정을 복사할 바디 인스턴스입니다.
         * @return 이 객체의 참조입니다.
         */
        FBodyInstance& operator=(const FBodyInstance& Other);

        /**
         * @brief 실제 바디를 해제합니다.
         */
        ~FBodyInstance();

    public:

        /**
         * @brief 바디 셋업의 충돌 형상으로 PhysX 강체를 만들어 장면에 추가합니다.
         * @param[in] Setup 강체에 붙일 충돌 형상을 가진 바디 셋업입니다.
         * @param[in] Transform 바디의 월드 위치·회전과 형상에 반영할 스케일입니다.
         * @param[in] OwnerComponent 바디를 소유하는 컴포넌트이며, 이 객체는 주소만 보관합니다.
         * @param[in,out] Scene 강체를 추가할 초기화된 물리 장면입니다.
         * @return 강체를 만들어 장면에 추가했으면 true, 형상이 없거나 생성에 실패하면 false입니다.
         * @note 이미 바디가 있으면 먼저 해제합니다. 장면은 이 바디가 해제될 때까지 유지되어야 합니다.
         */
        bool InitBody(const PhysicsRuntime::UBodySetup& Setup, const FTransform& Transform, Engine::UPrimitiveComponent* OwnerComponent, PhysicsRuntime::FPhysicsScene& Scene);

        /**
         * @brief 강체 종류를 직접 정해 바디 셋업의 충돌 형상으로 PhysX 강체를 만들어 장면에 추가합니다.
         * @param[in] Setup 강체에 붙일 충돌 형상을 가진 바디 셋업입니다.
         * @param[in] Transform 바디의 월드 위치·회전과 형상에 반영할 스케일입니다.
         * @param[in] OwnerComponent 바디를 소유하는 컴포넌트이며, 이 객체는 주소만 보관합니다.
         * @param[in,out] Scene 강체를 추가할 초기화된 물리 장면입니다.
         * @param[in] SpawnParams 정적 강체로 만들지 정하는 옵션입니다.
         * @return 강체를 만들어 장면에 추가했으면 true, 형상이 없거나 생성에 실패하면 false입니다.
         * @note 움직이는 강체는 시뮬레이션 설정이 꺼져 있으면 키네마틱으로 만듭니다.
         */
        bool InitBody(const PhysicsRuntime::UBodySetup& Setup, const FTransform& Transform, Engine::UPrimitiveComponent* OwnerComponent, PhysicsRuntime::FPhysicsScene& Scene, const PhysicsRuntime::FInitBodySpawnParams& SpawnParams);

        /**
         * @brief 실제 바디를 장면에서 제거하고 해제합니다.
         * @note 바디가 없으면 아무 일도 하지 않습니다.
         */
        void TermBody();

        /**
         * @brief 실제 바디가 있는지 반환합니다.
         * @return InitBody에 성공한 뒤 해제하지 않았으면 true입니다.
         */
        bool IsValidBodyInstance() const;

        /**
         * @brief 실제 바디가 시뮬레이션되는 동적 강체인지 반환합니다.
         * @return 동적 강체가 있으면 true, 정적 강체이거나 바디가 없으면 false입니다.
         */
        bool IsDynamic() const;

        /**
         * @brief 바디를 소유하는 컴포넌트를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 바디가 없으면 nullptr입니다.
         */
        Engine::UPrimitiveComponent* GetOwnerComponent() const;

        /**
         * @brief PhysX 강체를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 바디가 없으면 nullptr입니다.
         */
        physx::PxRigidActor* GetPxRigidActor() const;

        /**
         * @brief 동적 PhysX 강체를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 정적 강체이거나 바디가 없으면 nullptr입니다.
         */
        physx::PxRigidDynamic* GetPxRigidDynamic() const;

        /**
         * @brief 바디의 현재 월드 트랜스폼을 반환합니다.
         * @return 강체의 위치·회전과 바디를 만들 때 쓴 스케일이며, 바디가 없으면 항등 트랜스폼입니다.
         */
        FTransform GetBodyTransform() const;

        /**
         * @brief 형상 크기에 반영한 스케일을 반환합니다.
         * @return 마지막 InitBody에서 사용한 스케일입니다.
         */
        const FVector& GetBodyScale() const;

        /**
         * @brief 바디를 지정한 위치·회전으로 순간 이동합니다.
         * @param[in] Transform 바디의 새 월드 트랜스폼입니다.
         * @return 바디가 있고 위치·회전이 유효하여 적용했으면 true입니다.
         * @note 스케일은 반영하지 않으므로, 스케일이 바뀌면 바디를 다시 만들어야 합니다. 동적 강체의 속도는 유지합니다.
         */
        bool SetBodyTransform(const FTransform& Transform);

        /**
         * @brief 동적 강체의 질량 중심에 힘을 더합니다.
         * @param[in] Force 더할 힘(N)이며, AccelChange가 true이면 가속도(m/s²)입니다.
         * @param[in] AccelChange 질량과 무관하게 가속도로 적용할지 여부입니다.
         * @return 동적 강체가 있고 값이 유한하여 적용했으면 true입니다.
         */
        bool AddForce(const FVector& Force, bool AccelChange = false);

        /**
         * @brief 동적 강체의 질량 중심에 충격량을 더합니다.
         * @param[in] Impulse 더할 충격량(N·s)이며, VelocityChange가 true이면 속도 변화량(m/s)입니다.
         * @param[in] VelocityChange 질량과 무관하게 속도 변화량으로 적용할지 여부입니다.
         * @return 동적 강체가 있고 값이 유한하여 적용했으면 true입니다.
         */
        bool AddImpulse(const FVector& Impulse, bool VelocityChange = false);
        bool AddImpulseAtLocation(const FVector& Impulse, const FVector& Location);

        /**
         * @brief 동적 강체의 선속도를 반환합니다.
         * @return 선속도(m/s)이며, 동적 강체가 없으면 영벡터입니다.
         */
        FVector GetLinearVelocity() const;

        /**
         * @brief 동적 강체의 선속도를 설정합니다.
         * @param[in] Velocity 새 선속도(m/s)입니다.
         * @return 동적 강체가 있고 값이 유한하여 적용했으면 true입니다.
         */
        bool SetLinearVelocity(const FVector& Velocity);

        /**
         * @brief 동적 강체의 각속도를 반환합니다.
         * @return 각속도(rad/s)이며, 동적 강체가 없으면 영벡터입니다.
         */
        FVector GetAngularVelocity() const;

        /**
         * @brief 동적 강체의 각속도를 설정합니다.
         * @param[in] Velocity 새 각속도(rad/s)입니다.
         * @return 동적 강체가 있고 값이 유한하여 적용했으면 true입니다.
         */
        bool SetAngularVelocity(const FVector& Velocity);

        /**
         * @brief 바디의 현재 자세에서 각 형상과 겹치는 다른 바디를 장면 쿼리로 찾습니다.
         * @param[out] OutOverlaps 겹친 바디 인스턴스의 비소유 주소 목록이며, 중복 없이 채웁니다.
         * @return 바디가 있어 쿼리를 수행했으면 true입니다.
         * @note 시뮬레이션과 무관한 장면 쿼리이므로 정적 바디끼리도 찾습니다. 두 바디의 응답이 Overlap인 바디만 찾습니다.
         *       이 바디가 쿼리에 참여하지 않으면 찾지 않습니다. 자기 강체는 제외하고, FBodyInstance가 만들지 않은 강체는 무시합니다.
         */
        bool OverlapMulti(Core::TArray<FBodyInstance*>& OutOverlaps) const;

        /**
         * @brief 바디의 형상을 현재 자세에서 Delta만큼 쓸어 처음 막히는 바디를 찾습니다.
         * @param[in] Delta 이동할 월드 변위(m)입니다.
         * @param[out] OutHit 막힌 지점의 정보이며, 막히지 않으면 바꾸지 않습니다.
         * @param[in] ShouldIgnore true를 반환한 바디는 무시합니다. 비어 있으면 자기 강체만 무시합니다.
         * @return 응답이 Block인 바디에 막혔으면 true입니다.
         * @note 쿼리에 참여하지 않는 바디는 쓸지 않습니다. 시작부터 겹친 바디는 그 바디에서 멀어지는 방향이면 무시합니다.
         */
        bool Sweep(const FVector& Delta, PhysicsRuntime::FBodySweepHit& OutHit, const PhysicsRuntime::FIgnoreBodyPredicate& ShouldIgnore = {}, const FVector& StartOffset = {}) const;

        /**
         * @brief PhysX 강체에서 그 강체를 만든 바디 인스턴스를 찾습니다.
         * @param[in] Actor 조회할 PhysX 강체입니다.
         * @return 바디 인스턴스의 비소유 주소이며, userData가 비어 있으면 nullptr입니다.
         * @note FBodyInstance가 만든 강체에만 사용해야 합니다.
         */
        static FBodyInstance* FromPxActor(const physx::PxActor& Actor);

    public:

        /**
         * @brief 바디를 만들 때 동적 강체로 시뮬레이션할지 여부를 반환합니다.
         * @return 시뮬레이션 설정 값입니다.
         */
        bool ShouldSimulatePhysics() const;

        /**
         * @brief 바디를 만들 때 동적 강체로 시뮬레이션할지 설정합니다.
         * @param[in] Simulate 시뮬레이션 여부입니다.
         * @note 실제 바디의 종류는 바꾸지 않으므로, IsDynamic과 다르면 바디를 다시 만들어야 합니다.
         */
        void SetSimulatePhysics(bool Simulate);

        /**
         * @brief 동적 강체에 중력을 적용할지 반환합니다.
         * @return 중력 적용 설정 값입니다.
         */
        bool IsGravityEnabled() const;

        /**
         * @brief 동적 강체에 중력을 적용할지 설정하고 실제 바디에 반영합니다.
         * @param[in] Enabled 중력 적용 여부입니다.
         */
        void SetGravityEnabled(bool Enabled);

        /**
         * @brief 동적 강체에 연속 충돌 검출(CCD)을 사용할지 반환합니다.
         * @return CCD 설정 값입니다.
         */
        bool IsCCDEnabled() const;

        /**
         * @brief 동적 강체에 연속 충돌 검출(CCD)을 사용할지 설정하고 실제 바디에 반영합니다.
         * @param[in] Enabled CCD 사용 여부입니다.
         * @note 장면 설정에서도 CCD를 허용해야 적용됩니다.
         */
        void SetCCDEnabled(bool Enabled);

        /**
         * @brief 동적 강체의 지정 질량을 반환합니다.
         * @return 질량(kg)이며, 0이면 형상 부피와 기본 밀도로 계산합니다.
         */
        Core::FMass GetMassInKg() const;

        /**
         * @brief 동적 강체의 질량을 설정하고 실제 바디의 질량과 관성을 다시 계산합니다.
         * @param[in] MassInKg 질량(kg)이며, 0이면 형상 부피와 기본 밀도로 계산합니다.
         * @return 0 이상의 유한값이어서 적용했으면 true입니다.
         */
        bool SetMassInKg(Core::FMass MassInKg);

        /**
         * @brief 동적 강체의 선형 감쇠 계수를 반환합니다.
         * @return 선형 감쇠 계수입니다.
         */
        Core::FFrequency GetLinearDamping() const;

        /**
         * @brief 동적 강체의 선형 감쇠 계수를 설정하고 실제 바디에 반영합니다.
         * @param[in] Damping 선형 감쇠 계수입니다.
         * @return 0 이상의 유한값이어서 적용했으면 true입니다.
         */
        bool SetLinearDamping(Core::FFrequency Damping);

        /**
         * @brief 동적 강체의 회전 감쇠 계수를 반환합니다.
         * @return 회전 감쇠 계수입니다.
         */
        Core::FFrequency GetAngularDamping() const;

        /**
         * @brief 동적 강체의 회전 감쇠 계수를 설정하고 실제 바디에 반영합니다.
         * @param[in] Damping 회전 감쇠 계수입니다.
         * @return 0 이상의 유한값이어서 적용했으면 true입니다.
         */
        bool SetAngularDamping(Core::FFrequency Damping);

        /**
         * @brief 동적 강체의 관성 배율을 반환합니다.
         * @return 형상에서 계산한 관성 모멘트에 곱하는 값입니다. 1이면 형상 그대로입니다.
         */
        float GetInertiaScale() const;

        /**
         * @brief 동적 강체의 관성 배율을 설정하고 실제 바디의 관성을 다시 계산합니다.
         * @param[in] Scale 0보다 큰 유한값입니다. 클수록 같은 토크에 회전이 둔해져 가는 형상의 회전 떨림이 줄어듭니다.
         * @return 유효한 값이어서 적용했으면 true입니다.
         */
        bool SetInertiaScale(float Scale);

        /**
         * @brief 이미 만든 움직이는 강체의 시뮬레이션 여부를 다시 만들지 않고 바꿉니다.
         * @param[in] Simulate true이면 시뮬레이션하고, false이면 키네마틱으로 바꿉니다.
         * @note 정적 강체는 설정만 바꾸고 실제 강체에는 반영하지 않습니다.
         */
        void SetInstanceSimulatePhysics(bool Simulate);

        /**
         * @brief 이 바디의 모든 PhysX 형상에 충돌 필터 데이터를 설정합니다.
         * @param[in] GroupId 같은 물리 에셋 인스턴스의 바디가 공유하는 충돌 그룹 번호이며 0이 아니어야 쌍 목록이 적용됩니다.
         * @param[in] BodyIndex 물리 에셋 안의 바디 번호입니다.
         * @param[in] CollisionEnabled false이면 이 바디는 어떤 형상과도 충돌하지 않습니다. 뷰포트 선택(레이캐스트)에는 영향이 없습니다.
         * @note 바디를 만든 직후, 첫 시뮬레이션 전에 호출해야 합니다.
         */
        void SetCollisionFilter(Uint32 GroupId, Uint32 BodyIndex, bool CollisionEnabled);

        /**
         * @brief 충돌 프리셋을 반환합니다.
         * @return 참여 방식이나 응답을 직접 바꿨으면 Custom입니다.
         */
        PhysicsRuntime::ECollisionPreset GetCollisionPreset() const;

        /**
         * @brief 충돌 프리셋을 적용하고 실제 바디의 형상에 반영합니다.
         * @param[in] Preset 적용할 프리셋이며, Custom이면 현재 참여 방식과 응답을 유지합니다.
         */
        void SetCollisionPreset(PhysicsRuntime::ECollisionPreset Preset);

        /**
         * @brief 쿼리·시뮬레이션 참여 방식을 반환합니다.
         * @return 참여 방식입니다.
         */
        PhysicsRuntime::ECollisionEnabled GetCollisionEnabled() const;

        /**
         * @brief 쿼리·시뮬레이션 참여 방식을 바꾸고 실제 바디의 형상에 반영합니다.
         * @param[in] CollisionEnabled 새 참여 방식입니다.
         * @note 프리셋의 값과 다르면 프리셋을 Custom으로 바꿉니다.
         */
        void SetCollisionEnabled(PhysicsRuntime::ECollisionEnabled CollisionEnabled);

        /**
         * @brief 다른 모든 바디에 대한 응답을 반환합니다.
         * @return 응답입니다.
         */
        PhysicsRuntime::ECollisionResponse GetCollisionResponse() const;

        /**
         * @brief 다른 모든 바디에 대한 응답을 바꾸고 실제 바디의 형상에 반영합니다.
         * @param[in] Response 새 응답입니다.
         * @note 프리셋의 값과 다르면 프리셋을 Custom으로 바꿉니다.
         */
        void SetCollisionResponse(PhysicsRuntime::ECollisionResponse Response);

        /**
         * @brief 다른 바디 인스턴스의 충돌 설정만 복사하고 실제 바디의 형상에 반영합니다.
         * @param[in] Source 충돌 설정을 복사할 바디 인스턴스입니다.
         */
        void CopyCollisionSettings(const FBodyInstance& Source);

        /**
         * @brief 장면 쿼리에 참여하는지 반환합니다.
         * @return QueryOnly 또는 QueryAndPhysics이면 true입니다.
         */
        bool IsQueryCollisionEnabled() const;

    public:

        /**
         * @brief 물리 에셋 안에서 이 바디의 설계도 번호입니다.
         * @note InvalidIndex가 아니면 뼈 바디이므로 시뮬레이션 결과를 컴포넌트 트랜스폼에 바로 반영하지 않습니다.
         */
        Index_t mInstanceBodyIndex{InvalidIndex};

        /**
         * @brief 이 바디가 붙은 뼈 번호입니다.
         */
        Index_t mInstanceBoneIndex{InvalidIndex};

    private:

        /**
         * @brief 질량·감쇠·중력·CCD 설정을 동적 강체에 반영합니다.
         * @param[in,out] Dynamic 설정을 반영할 동적 강체입니다.
         */
        void ApplyDynamicSettings(physx::PxRigidDynamic& Dynamic) const;

        /**
         * @brief 질량 설정으로 동적 강체의 질량과 관성을 다시 계산합니다.
         * @param[in,out] Dynamic 질량을 계산할 동적 강체입니다.
         */
        void UpdateMassProperties(physx::PxRigidDynamic& Dynamic) const;

        /**
         * @brief 충돌 참여 방식과 응답을 실제 바디의 모든 형상 플래그와 필터 데이터에 반영합니다.
         * @note 바디가 없으면 아무 일도 하지 않습니다.
         */
        void ApplyCollisionSettings() const;

    private:

        /**
         * @brief 바디를 만들 때 동적 강체로 시뮬레이션할지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics", Getter = ShouldSimulatePhysics, Setter = SetSimulatePhysics)
        bool mSimulatePhysics{};

        /**
         * @brief 동적 강체에 중력을 적용할지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics", Getter = IsGravityEnabled, Setter = SetGravityEnabled)
        bool mEnableGravity{true};

        /**
         * @brief 동적 강체에 연속 충돌 검출(CCD)을 사용할지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics", Getter = IsCCDEnabled, Setter = SetCCDEnabled)
        bool mUseCCD{};

        /**
         * @brief 동적 강체의 질량(kg)이며, 0이면 형상 부피와 기본 밀도로 계산합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics", Getter = GetMassInKg, Setter = SetMassInKg)
        Core::FMass mMassInKg{};

        /**
         * @brief 동적 강체의 선형 감쇠 계수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics", Getter = GetLinearDamping, Setter = SetLinearDamping, Meta = (DisplayUnit = "1/s"))
        Core::FFrequency mLinearDamping{};

        /**
         * @brief 동적 강체의 회전 감쇠 계수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics", Getter = GetAngularDamping, Setter = SetAngularDamping, Meta = (DisplayUnit = "1/s"))
        Core::FFrequency mAngularDamping{};

        /**
         * @brief 형상에서 계산한 관성 모멘트에 곱하는 배율입니다. 피직스 에셋의 바디 셋업이 정하며 저장하지 않습니다.
         */
        float mInertiaScale{1.0f};

        /**
         * @brief true이면 메시나 바디 셋업의 재질 대신 아래 마찰과 반발 값으로 이 컴포넌트의 재질을 만듭니다. 바디를 만들 때 적용합니다.
         * @note 두 형상이 접촉하면 PhysX가 두 재질의 값을 합쳐(기본은 평균) 쓰므로 바닥과 물체의 값이 모두 영향을 줍니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics")
        bool mOverridePhysicalMaterial{};

        /**
         * @brief 정지 마찰 계수입니다. 물체가 멈춘 상태에서 밀기 시작할 때 필요한 힘을 정합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics")
        float mStaticFriction{0.5f};

        /**
         * @brief 동적 마찰 계수입니다. 미끄러지는 동안 속도를 줄이는 정도를 정합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics")
        float mDynamicFriction{0.5f};

        /**
         * @brief 반발 계수입니다. 0이면 튕기지 않고 1이면 에너지를 잃지 않고 튕깁니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Physics")
        float mRestitution{0.6f};

        /**
         * @brief 충돌 프리셋이며, 참여 방식이나 응답을 직접 바꾸면 Custom입니다.
         * @note 직렬화할 때 먼저 읽도록 참여 방식과 응답보다 앞에 둡니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision", Getter = GetCollisionPreset, Setter = SetCollisionPreset)
        PhysicsRuntime::ECollisionPreset mCollisionPreset{PhysicsRuntime::ECollisionPreset::BlockAll};

        /**
         * @brief 쿼리·시뮬레이션 참여 방식입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision", Getter = GetCollisionEnabled, Setter = SetCollisionEnabled)
        PhysicsRuntime::ECollisionEnabled mCollisionEnabled{PhysicsRuntime::ECollisionEnabled::QueryAndPhysics};

        /**
         * @brief 다른 모든 바디에 대한 응답입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision", Getter = GetCollisionResponse, Setter = SetCollisionResponse)
        PhysicsRuntime::ECollisionResponse mCollisionResponse{PhysicsRuntime::ECollisionResponse::Block};

        /**
         * @brief 이 객체가 단독 소유하는 PhysX 강체입니다.
         */
        std::unique_ptr<physx::PxRigidActor, PhysicsRuntime::FPhysXRelease> mActor{};

        /**
         * @brief 강체를 추가한 물리 장면이며, 바디가 해제될 때까지 유지됩니다.
         */
        PhysicsRuntime::FPhysicsScene* mScene{};

        /**
         * @brief 바디를 소유하는 컴포넌트이며, 바디가 해제될 때까지 유지됩니다.
         */
        Engine::UPrimitiveComponent* mOwnerComponent{};

        /**
         * @brief 마지막 InitBody에서 형상 크기에 반영한 스케일입니다.
         */
        FVector mBodyScale{1.0f, 1.0f, 1.0f};
    };

}
