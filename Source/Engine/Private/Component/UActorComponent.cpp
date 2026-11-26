/**
 * @file UActorComponent.cpp
 * @brief 소유 액터와 월드 등록 수명을 갖는 컴포넌트의 기반 클래스입니다.
 * @ingroup Engine
 */

#include "Engine/Component/UActorComponent.h"
#include "Engine/AActor.h"
#include "Engine/UWorld.h"

namespace Engine {

    /**
     * @brief UActorComponent의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Engine::UActorComponent::UActorComponent() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Engine::UActorComponent::~UActorComponent() = default;

    /**
     * @brief 컴포넌트를 소유한 액터를 반환합니다.
     */
    bool Engine::UActorComponent::IsOwnerVisible() const {
        return mOwner == nullptr || mOwner->IsVisible();
    }

    /**
     * @brief 소유 액터의 표시 여부가 바뀐 뒤 렌더 상태를 갱신하는 확장 지점입니다.
     */
    void Engine::UActorComponent::OnOwnerVisibilityChanged() {
    }

    /**
     * @brief 컴포넌트를 소유한 액터를 반환합니다.
     */
    Engine::AActor* Engine::UActorComponent::GetOwner() const {
        return mOwner;
    }

    /**
     * @brief 현재 소속되거나 관리하는 월드의 비소유 주소를 반환합니다.
     */
    Engine::UWorld* Engine::UActorComponent::GetWorld() const {
        return mWorld;
    }

    /**
     * @brief 컴포넌트의 소유 액터를 지정합니다.
     */
    void Engine::UActorComponent::SetOwner(Engine::AActor* Owner) {
        SetOuter(Owner);

        mOwner = Owner;
    }

    /**
     * @brief 컴포넌트를 월드에 연결하고 등록 콜백 이후 Tick 함수를 등록합니다.
     * @details 콜백 이후에도 월드 연결과 소유자가 유효한 경우에 Tick 등록을 진행합니다.
     */
    void Engine::UActorComponent::RegisterComponent(Engine::UWorld* World) {
        Engine::UWorld::FDispatchScope Scope{World};

        if (World == nullptr || mWorld == World || !World->CanAddActor() || (mOwner != nullptr && (mOwner->GetWorld() != World || mOwner->IsPendingDestroy()))) {
            return;
        }

        UnregisterComponent();

        if (!World->CanAddActor() || (mOwner != nullptr && (mOwner->GetWorld() != World || mOwner->IsPendingDestroy()))) {
            return;
        }

        mWorld = World;

        OnRegister();

        if (mWorld == World) {
            CreatePhysicsState();
        }

        if (mWorld == World && mOwner != nullptr) {
            RegisterComponentTickFunctions(true);

            if (mOwner->HasBegunPlay() && World->CanBeginActor()) {
                DispatchBeginPlay();
            }
        }
    }

    /**
     * @brief Tick 등록을 해제한 뒤 종료 콜백을 전달하고 월드 연결과 렌더 상태를 정리합니다.
     */
    void Engine::UActorComponent::UnregisterComponent() {
        Engine::UWorld::FDispatchScope Scope{mWorld};

        if (mWorld != nullptr) {
            RegisterComponentTickFunctions(false);
        }

        DispatchEndPlay();
        DestroyPhysicsState();

        mWorld = nullptr;
    }

    /**
     * @brief 컴포넌트가 월드에 등록되어 있는지 검사합니다.
     */
    bool Engine::UActorComponent::IsRegistered() const {
        return mWorld != nullptr;
    }

    /**
     * @brief 등록된 월드에 물리 장면이 있고 물리 상태가 필요하면 물리 상태를 만듭니다.
     */
    void Engine::UActorComponent::CreatePhysicsState() {
        if (mPhysicsStateCreated || mWorld == nullptr || mWorld->GetPhysicsScene() == nullptr || !ShouldCreatePhysicsState()) {
            return;
        }

        mPhysicsStateCreated = true;

        OnCreatePhysicsState();
    }

    /**
     * @brief 만든 물리 상태를 해제합니다.
     */
    void Engine::UActorComponent::DestroyPhysicsState() {
        if (!mPhysicsStateCreated) {
            return;
        }

        mPhysicsStateCreated = false;

        OnDestroyPhysicsState();
    }

    /**
     * @brief 물리 상태를 해제한 뒤 조건이 맞으면 다시 만듭니다.
     */
    void Engine::UActorComponent::RecreatePhysicsState() {
        DestroyPhysicsState();
        CreatePhysicsState();
    }

    /**
     * @brief 물리 상태를 만들었는지 반환합니다.
     */
    bool Engine::UActorComponent::IsPhysicsStateCreated() const {
        return mPhysicsStateCreated;
    }

    /**
     * @brief 이 컴포넌트가 물리 상태를 필요로 하는지 반환하는 확장 지점입니다.
     */
    bool Engine::UActorComponent::ShouldCreatePhysicsState() const {
        return false;
    }

    /**
     * @brief 월드에 등록된 컴포넌트가 참조하는 에셋과 렌더 상태를 준비합니다.
     */
    void Engine::UActorComponent::OnRegister() {
    }

    /**
     * @brief 물리 장면에 이 컴포넌트의 물리 상태를 만드는 확장 지점입니다.
     */
    void Engine::UActorComponent::OnCreatePhysicsState() {
    }

    /**
     * @brief 이 컴포넌트의 물리 상태를 물리 장면에서 해제하는 확장 지점입니다.
     */
    void Engine::UActorComponent::OnDestroyPhysicsState() {
    }

    /**
     * @brief 컴포넌트의 게임 실행 시작 시 호출되는 확장 지점입니다.
     */
    void Engine::UActorComponent::BeginPlay() {
    }

    /**
     * @brief 기본 컴포넌트 Tick 함수가 실행 차례에 호출하는 갱신 확장 지점입니다.
     */
    void Engine::UActorComponent::Tick(float) {
    }

    /**
     * @brief 컴포넌트의 게임 실행 종료 시 호출되는 확장 지점입니다.
     */
    void Engine::UActorComponent::EndPlay() {
    }

    /**
     * @brief 컴포넌트가 실행 시작 전달에 진입했는지 검사합니다.
     */
    bool Engine::UActorComponent::HasBegunPlay() const {
        return mHasBegunPlay;
    }

    /**
     * @brief 정지한 편집 월드에서도 Tick을 허용하는지 반환합니다.
     */
    bool Engine::UActorComponent::IsEditorTickable() const {
        return mEditorTickable;
    }

    /**
     * @brief 정지한 편집 월드에서의 Tick 허용 여부를 설정합니다.
     */
    void Engine::UActorComponent::SetEditorTickable(bool EditorTickable) {
        mEditorTickable = EditorTickable;
    }

    /**
     * @brief 컴포넌트가 직접 소유한 기본 Tick 함수를 반환합니다.
     */
    Engine::FActorComponentTickFunction& Engine::UActorComponent::GetPrimaryComponentTick() {
        return mPrimaryComponentTick;
    }

    /**
     * @brief 기본 컴포넌트 Tick에 지정한 그룹을 조회합니다.
     */
    Engine::ETickingGroup Engine::UActorComponent::GetTickGroup() const {
        return mPrimaryComponentTick.GetTickGroup();
    }

    /**
     * @brief 검증과 그룹 설정을 기본 컴포넌트 Tick에 전달합니다.
     */
    bool Engine::UActorComponent::SetTickGroup(Engine::ETickingGroup Group) {
        return mPrimaryComponentTick.SetTickGroup(Group);
    }

    /**
     * @brief 등록을 요청하면 소유 액터의 레벨을 사용하고, 해제 시에는 기존 등록을 끊습니다.
     */
    void Engine::UActorComponent::RegisterComponentTickFunctions(bool Register) {
        if (!Register) {
            mPrimaryComponentTick.UnregisterTickFunction();
            return;
        }

        if (Engine::ULevel* Level{mOwner != nullptr ? mOwner->GetLevel() : nullptr}) {
            mPrimaryComponentTick.RegisterTickFunction(*Level);
        }
    }

    /**
     * @brief 시작 상태를 설정하고 중복되지 않도록 시작 콜백을 전달합니다.
     */
    void Engine::UActorComponent::DispatchBeginPlay() {
        if (!mHasBegunPlay && mWorld != nullptr && mOwner != nullptr && !mOwner->IsPendingDestroy()) {
            mHasBegunPlay = true;

            BeginPlay();
        }
    }

    /**
     * @brief 시작 상태를 먼저 해제하여 종료 콜백의 중복 전달을 막습니다.
     */
    void Engine::UActorComponent::DispatchEndPlay() {
        if (mHasBegunPlay) {
            mHasBegunPlay = false;

            EndPlay();
        }
    }

}
