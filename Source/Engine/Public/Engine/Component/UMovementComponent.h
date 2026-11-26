/**
 * @file UMovementComponent.h
 * @brief 공간 컴포넌트의 이동 대상과 월드 속도를 관리합니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FVector.h"
#include "CoreUObject/TObjectRef.h"
#include "Engine/Component/UActorComponent.h"
#include "UMovementComponent.generated.h"

namespace Engine {

    class USceneComponent;
    class UPrimitiveComponent;
    struct FHitResult;

    /** @brief 이동 대상 지정과 월드 공간 이동을 제공하는 컴포넌트입니다. */
    UCLASS()
    class UMovementComponent : public Engine::UActorComponent {
        GENERATED_BODY()

    public:
        UMovementComponent();

        /** @brief 명시적 대상을 지정합니다. nullptr을 전달하면 루트 자동 선택도 해제합니다. */
        virtual void SetUpdatedComponent(Engine::USceneComponent* Component);

        /** @brief 등록된 월드에서 사용할 수 있는 이동 대상을 반환합니다. */
        Engine::USceneComponent* GetUpdatedComponent() const;

        /** @brief 이동 대상이 프리미티브이면 해당 컴포넌트를 반환합니다. */
        Engine::UPrimitiveComponent* GetUpdatedPrimitive() const;

        const FVector& GetVelocity() const;
        virtual bool SetVelocity(const FVector& Velocity);

        /** @brief 유효한 시간 간격과 이동 대상이 있는지 확인합니다. */
        bool ShouldSkipUpdate(Core::FTime DeltaTime) const;

        /**
         * @brief 월드 위치 변화량과 새 회전을 적용합니다.
         * @details 스윕은 시작 자세에서 차단 가능한 대상 형상의 평행 이동을 검사합니다.
         * @return 변환을 적용하면 true입니다. 차단 여부는 OutHit에 기록합니다.
         */
        bool MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool Sweep = false, Engine::FHitResult* OutHit = nullptr);

        /**
         * @brief 초기 관통에서 빠져나갈 공간이 있으면 보정 위치에서 이동을 다시 시도합니다.
         * @details 보정에 성공하면 재시도 결과를, 실패하면 최초 관통 결과를 OutHit에 기록합니다.
         */
        bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool Sweep, Engine::FHitResult& OutHit);

        /** @brief 차단 충돌 뒤 남은 변위를 충돌면에 투영하고 스윕으로 이동합니다. */
        bool SlideAlongSurface(const FVector& Delta, const Engine::FHitResult& Hit, Engine::FHitResult* OutHit = nullptr);

        /** @brief 현재 월드 속도를 0으로 만듭니다. */
        virtual void StopMovementImmediately();

        void RegisterComponentTickFunctions(bool Register) override;

    protected:
        /** @brief 자동으로 선택하거나 직렬화로 복원한 이동 대상을 검사합니다. */
        virtual bool CanUseUpdatedComponent(const Engine::USceneComponent& Component) const;

        /** @brief 대상 설정이 바뀌거나 등록이 해제될 때 호출합니다. 새 대상은 아직 월드 등록 전일 수 있습니다. */
        virtual void OnUpdatedComponentChanged(Engine::USceneComponent* Component);

    private:
        friend class AActor;

        Engine::USceneComponent* ResolveUpdatedComponent() const;
        void UpdateTickEnabledState();
        void SetExplicitUpdatedComponent(const CoreUObject::TObjectRef<Engine::USceneComponent>& Component);
        bool MoveUpdatedComponentInternal(const FVector& Delta, const FQuat& NewRotation, bool Sweep, bool ResolvePenetration, Engine::FHitResult* OutHit);

    private:
        /** @brief 처음에는 소유 액터의 현재 루트를 이동 대상으로 사용합니다. */
        UPROPERTY(Serialize)
        bool mUseOwnerRoot{true};

        /** @brief 명시적 대상의 GUID와 로드 후 연결된 핸들을 보관합니다. */
        UPROPERTY(Serialize, Name = "UpdatedComponent", Setter = SetExplicitUpdatedComponent)
        CoreUObject::TObjectRef<Engine::USceneComponent> mExplicitUpdatedComponent{};

        FVector mVelocity{};
    };

}
