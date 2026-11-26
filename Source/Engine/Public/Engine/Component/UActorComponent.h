/**
 * @file UActorComponent.h
 * @brief 소유 액터와 월드 등록 수명을 갖는 컴포넌트의 기반 클래스입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "CoreUObject/UObject.h"
#include "Engine/Tick/FTickFunction.h"
#include "UActorComponent.generated.h"

namespace Engine {

    class AActor;
    class UWorld;

    /**
     * @brief 소유 액터와 월드 등록 수명을 갖는 컴포넌트의 기반 클래스입니다.
     * @ingroup Engine
     */
    UCLASS()
    class UActorComponent : public CoreUObject::UObject {
        GENERATED_BODY()

    public:
        /**
         * @brief UActorComponent의 초기 상태와 전달받은 의존성을 구성합니다.
         */
        UActorComponent();

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~UActorComponent() override;

    public:
        /**
         * @brief 컴포넌트를 소유한 액터를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 해당 값이 없으면 nullptr입니다.
         */
        Engine::AActor* GetOwner() const;

        /**
         * @brief 소유 액터가 표시 상태인지 검사합니다.
         * @return 소유 액터가 없거나 표시 상태이면 true입니다.
         */
        bool IsOwnerVisible() const;

        /**
         * @brief 소유 액터의 표시 여부가 바뀐 뒤 렌더 상태를 갱신하는 확장 지점입니다.
         */
        virtual void OnOwnerVisibilityChanged();

        /**
         * @brief 현재 소속되거나 관리하는 월드의 비소유 주소를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 해당 값이 없으면 nullptr입니다.
         */
        Engine::UWorld* GetWorld() const;

        /**
         * @brief 컴포넌트를 월드에 연결하고 렌더 상태, 물리 상태와 Tick 함수를 등록합니다.
         * @param[in,out] World 컴포넌트를 등록할 월드입니다.
         */
        virtual void RegisterComponent(Engine::UWorld* World);

        /**
         * @brief 렌더 상태, Tick 등록, 물리 상태와 월드 연결을 해제합니다.
         */
        virtual void UnregisterComponent();

        /**
         * @brief 컴포넌트가 월드에 등록되어 있는지 검사합니다.
         * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool IsRegistered() const;

        /**
         * @brief 컴포넌트의 게임 실행 시작 시 호출되는 확장 지점입니다.
         * @note 생명주기 상태는 월드의 전달 경로가 관리합니다.
         */
        virtual void BeginPlay();

        /**
         * @brief 기본 컴포넌트 Tick 함수가 실행 차례에 호출하는 갱신 확장 지점입니다.
         * @param[in] DeltaTime 호출에 전달된 초 단위 시간 간격입니다.
         */
        virtual void Tick(float DeltaTime);

        /**
         * @brief 컴포넌트의 게임 실행 종료 시 호출되는 확장 지점입니다.
         */
        virtual void EndPlay();

        /**
         * @brief 컴포넌트가 실행 시작 전달에 진입했는지 검사합니다.
         * @return 실행 시작 전달 이후 종료 전달 전이면 true입니다.
         */
        bool HasBegunPlay() const;

        /**
         * @brief 정지한 편집 월드에서도 Tick을 허용하는지 반환합니다.
         * @return 검사 조건을 만족하면 true, 그렇지 않으면 false입니다.
         */
        bool IsEditorTickable() const;

        /**
         * @brief 정지한 편집 월드에서의 Tick 허용 여부를 설정합니다.
         * @param[in] EditorTickable 정지한 편집 월드에서 Tick을 허용할지 나타냅니다.
         */
        void SetEditorTickable(bool EditorTickable);

        /** @brief 이 컴포넌트의 Tick()을 호출하는 기본 Tick 함수의 참조를 반환합니다. */
        Engine::FActorComponentTickFunction& GetPrimaryComponentTick();

        /** @brief 컴포넌트의 기본 Tick에 설정된 그룹을 반환합니다. */
        Engine::ETickingGroup GetTickGroup() const;

        /**
         * @brief 컴포넌트의 기본 Tick에 설정할 그룹을 지정합니다.
         * @param[in] Group 컴포넌트의 기본 Tick이 시작할 수 있는 가장 이른 그룹입니다.
         * @return 지정할 수 있는 그룹이면 true입니다.
         */
        virtual bool SetTickGroup(Engine::ETickingGroup Group);

        /**
         * @brief 이 컴포넌트의 기본 Tick을 소유 액터의 레벨에 등록하거나 등록을 해제합니다.
         * @param[in] Register 등록할 때 true이고 해제할 때 false입니다.
         * @details 별도의 Tick 함수를 소유하는 파생 클래스는 이 함수를 재정의해 함께 등록하고 해제할 수 있습니다.
         */
        virtual void RegisterComponentTickFunctions(bool Register);

        /**
         * @brief 등록된 월드에 물리 장면이 있고 물리 상태가 필요하면 물리 상태를 만듭니다.
         * @details 컴포넌트 등록 시와 월드가 물리 장면을 만든 직후 호출되므로, 바디 생성은 액터 BeginPlay가 아니라
         *          "등록됨 + 물리 장면 있음" 조건을 따릅니다. 이미 만들었으면 아무 일도 하지 않습니다.
         */
        void CreatePhysicsState();

        /**
         * @brief 만든 물리 상태를 해제합니다.
         * @note 컴포넌트 등록 해제 시와 월드가 물리 장면을 해제하기 직전에 호출됩니다.
         */
        void DestroyPhysicsState();

        /**
         * @brief 물리 상태를 해제한 뒤 조건이 맞으면 다시 만듭니다.
         */
        void RecreatePhysicsState();

        /**
         * @brief 물리 상태를 만들었는지 반환합니다.
         * @return CreatePhysicsState로 만든 뒤 해제하지 않았으면 true입니다.
         */
        bool IsPhysicsStateCreated() const;

        /**
         * @brief 이 컴포넌트가 물리 상태를 필요로 하는지 반환하는 확장 지점입니다.
         * @return 기본 구현은 false입니다.
         */
        virtual bool ShouldCreatePhysicsState() const;

    private:
        /**
         * @brief 월드에 등록된 컴포넌트가 참조하는 에셋과 렌더 상태를 준비합니다.
         */
        virtual void OnRegister();

        /**
         * @brief 물리 장면에 이 컴포넌트의 물리 상태를 만드는 확장 지점입니다.
         */
        virtual void OnCreatePhysicsState();

        /**
         * @brief 이 컴포넌트의 물리 상태를 물리 장면에서 해제하는 확장 지점입니다.
         */
        virtual void OnDestroyPhysicsState();

        /**
         * @brief 컴포넌트의 소유 액터를 지정합니다.
         * @param[in] Owner 컴포넌트를 소유할 액터입니다.
         */
        void SetOwner(Engine::AActor* Owner);

        /**
         * @brief 소유 액터가 컴포넌트의 소유 관계와 실행 전달을 관리하도록 허용합니다.
         */
        friend class AActor;

        /**
         * @brief 시작 상태를 설정하고 중복되지 않도록 시작 콜백을 전달합니다.
         */
        void DispatchBeginPlay();

        /**
         * @brief 시작 상태를 먼저 해제하여 종료 콜백의 중복 전달을 막습니다.
         */
        void DispatchEndPlay();

    private:
        /**
         * @brief 컴포넌트를 소유한 액터입니다.
         */
        Engine::AActor* mOwner{};

        /**
         * @brief 등록된 월드이며, 등록 전에는 nullptr입니다.
         */
        Engine::UWorld* mWorld{};

        /**
         * @brief 컴포넌트가 시작 전달에 진입하여 아직 종료되지 않았는지 나타냅니다.
         */
        bool mHasBegunPlay{};

        /**
         * @brief 물리 상태를 만들어 아직 해제하지 않았는지 나타냅니다.
         */
        bool mPhysicsStateCreated{};

        /**
         * @brief 정지한 편집 월드에서도 이 객체의 Tick을 허용하는 설정입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Component Tick")
        bool mEditorTickable{};

        /** @brief 컴포넌트의 Tick()을 호출하는 기본 함수이며, 설정 그룹을 편집 및 직렬화에 노출합니다. */
        UPROPERTY(Edit, Serialize, Category = "Component Tick", Name = "TickGroup", Getter = GetTickGroup, Setter = SetTickGroup)
        Engine::FActorComponentTickFunction mPrimaryComponentTick{*this};
    };

}
