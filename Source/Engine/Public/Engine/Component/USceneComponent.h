/**
 * @file USceneComponent.h
 * @brief 검증된 위치, 회전과 스케일을 보관하는 공간 컴포넌트입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Engine/Component/UActorComponent.h"
#include "Core/Math/FTransform.h"
#include "USceneComponent.generated.h"

namespace Engine {

    struct FHitResult;

    /**
     * @brief 검증된 위치, 회전과 스케일을 보관하는 공간 컴포넌트입니다.
     * @ingroup Engine
     */
    UCLASS()
    class USceneComponent : public Engine::UActorComponent {
        GENERATED_BODY()

    public:
        /**
         * @brief USceneComponent의 초기 상태와 전달받은 의존성을 구성합니다.
         */
        USceneComponent();

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~USceneComponent() override;

    public:
        /**
         * @brief 컴포넌트의 위치·회전·스케일을 반환합니다.
         * @return 소유 객체가 유지하는 값의 참조입니다.
         */
        const FTransform& GetTransform() const;

        /**
         * @brief 유한하고 유효한 변환을 검증하여 공간 컴포넌트에 적용합니다.
         * @param[in] Transform 적용할 위치, 회전과 스케일입니다.
         * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
         */
        virtual bool SetTransform(const FTransform& Transform);

        /**
         * @brief 위치, 회전과 스케일이 적용된 월드 행렬을 반환합니다.
         * @return 위치, 회전과 스케일이 적용된 월드 행렬입니다.
         */
        FMatrix GetWorldMatrix() const;

        Engine::USceneComponent* GetAttachParent() const;
        bool CanSetAttachParent(const Engine::USceneComponent* Parent) const;
        bool SetAttachParent(Engine::USceneComponent* Parent);

        /**
         * @brief 컴포넌트를 월드 변위만큼 옮기고, 요청하면 이동 경로를 쓸어 막히는 지점에서 멈춥니다.
         * @param[in] Delta 이동할 월드 변위(m)입니다.
         * @param[in] Sweep true이면 이동 경로를 쓸어 막히는 지점에서 멈춥니다.
         * @param[out] OutHit 막힌 지점의 정보를 받을 주소이며, nullptr이면 받지 않습니다. 막히지 않으면 BlockingHit이 false입니다.
         * @return 이동을 적용했으면 true입니다. 막혀서 덜 움직였어도 true입니다.
         * @note 소유 액터의 루트이면 액터 전체를 옮기고, 아니면 이 컴포넌트만 옮깁니다.
         */
        bool MoveComponent(const FVector& Delta, bool Sweep, Engine::FHitResult* OutHit = nullptr);

    protected:
        /**
         * @brief MoveComponent의 실제 이동을 처리합니다. 기본 구현은 쓸지 않고 Delta만큼 옮깁니다.
         * @param[in] Delta 이동할 월드 변위(m)입니다.
         * @param[in] Sweep 이동 경로를 쓸지 여부입니다.
         * @param[out] OutHit 막힌 지점의 정보이며, 호출 전에 초기화되어 있습니다.
         * @return 이동을 적용했으면 true입니다.
         */
        virtual bool MoveComponentImpl(const FVector& Delta, bool Sweep, Engine::FHitResult& OutHit);

        /**
         * @brief 위치만 Delta만큼 옮깁니다. 소유 액터의 루트이면 액터 전체를 옮깁니다.
         * @param[in] Delta 이동할 월드 변위(m)입니다.
         * @return 트랜스폼을 적용했으면 true입니다.
         */
        bool ApplyMove(const FVector& Delta);

    private:
        friend class AActor;

        const Engine::USceneComponent* GetEffectiveAttachParent() const;
        void UpdateAttachParent(Engine::USceneComponent* Parent);
        void ApplyParentTransform(const FTransform& Previous, const FTransform& Current);
        void DetachExternalAttachments();

    private:
        /**
         * @brief 컴포넌트의 위치·회전·스케일입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Transform", Getter = GetTransform, Setter = SetTransform)
        FTransform mTransform{};

        Engine::USceneComponent* mAttachParent{};
        Core::TArray<Engine::USceneComponent*> mAttachChildren{};
    };

}
