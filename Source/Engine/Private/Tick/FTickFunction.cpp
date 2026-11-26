/**
 * @file FTickFunction.cpp
 * @brief Tick 함수의 레벨 등록, 실행 조건, 선행 관계와 실제 객체 콜백을 연결합니다.
 * @ingroup Engine
 */

#include "Engine/Tick/FTickFunction.h"
#include "Engine/AActor.h"
#include "Engine/Component/UActorComponent.h"
#include "Engine/EngineStats.h"
#include "Engine/Tick/FTickTaskManager.h"
#include "Engine/ULevel.h"
#include "CoreUObject/Reflection/Cast.h"

#include <algorithm>
#include <unordered_set>

namespace Engine {

    namespace {

        /**
         * @brief 월드에 속하고 파괴 대기 중이 아닌 액터의 기본 실행 조건을 검사합니다.
         * @param[in] Actor 검사할 액터입니다.
         * @param[in] World 현재 Tick을 전달하는 월드입니다.
         * @param[in] EditorTick BeginPlay 이전에도 실행 후보로 허용하는 편집 갱신이면 true입니다.
         * @return 월드 소속, 파괴 상태 및 BeginPlay 조건이 맞으면 true입니다.
         */
        bool IsActorReady(const Engine::AActor& Actor, const Engine::UWorld& World, bool EditorTick) {
            return Actor.GetWorld() == &World && !Actor.IsPendingDestroy() && (EditorTick || Actor.HasBegunPlay());
        }

        /**
         * @brief 액터의 기본 실행 조건과 편집 Tick 허용 설정을 검사합니다.
         * @param[in] Actor 검사할 액터입니다.
         * @param[in] World 현재 Tick을 전달하는 월드입니다.
         * @param[in] EditorTick 정지한 편집 월드의 갱신이면 true입니다.
         * @return 이 갱신에서 액터 Tick을 실행할 수 있으면 true입니다.
         */
        bool CanTickActor(const Engine::AActor& Actor, const Engine::UWorld& World, bool EditorTick) {
            return IsActorReady(Actor, World, EditorTick) && (!EditorTick || Actor.IsEditorTickable());
        }

        /**
         * @brief 소유 액터와 컴포넌트의 월드 연결 및 실행 상태를 확인합니다.
         * @param[in] Component 검사할 컴포넌트입니다.
         * @param[in] World 현재 Tick을 전달하는 월드입니다.
         * @param[in] EditorTick 정지한 편집 월드의 갱신이면 true입니다.
         * @return 컴포넌트의 BeginPlay 또는 자체 편집 Tick 조건을 만족하면 true입니다.
         */
        bool CanTickComponent(const Engine::UActorComponent& Component, const Engine::UWorld& World, bool EditorTick) {
            const Engine::AActor* Actor{Component.GetOwner()};

            return Actor != nullptr && IsActorReady(*Actor, World, EditorTick) && Component.GetWorld() == &World && (EditorTick ? Component.IsEditorTickable() : Component.HasBegunPlay());
        }

    }

    /**
     * @brief 레벨에서 이미 해제된 함수라도 프레임 큐에 남아 있으면 포인터를 먼저 무효화합니다.
     */
    FTickFunction::~FTickFunction() {
        ClearQueuedTick();
        UnregisterTickFunction();
    }

    /**
     * @brief 설정에 저장된 그룹을 반환합니다.
     */
    Engine::ETickingGroup FTickFunction::GetTickGroup() const {
        return mTickGroup;
    }

    /**
     * @brief 액터와 컴포넌트에 지정할 수 있는 네 그룹만 설정값으로 받습니다.
     */
    bool FTickFunction::SetTickGroup(Engine::ETickingGroup Group) {
        if (Group != Engine::ETickingGroup::PrePhysics && Group != Engine::ETickingGroup::DuringPhysics && Group != Engine::ETickingGroup::PostPhysics && Group != Engine::ETickingGroup::PostUpdateWork) {
            return false;
        }

        mTickGroup = Group;

        return true;
    }

    /**
     * @brief Tick 함수가 실행 후보가 될 수 있는지 조회합니다.
     */
    bool FTickFunction::CanEverTick() const {
        return mCanEverTick;
    }

    /**
     * @brief 함수가 실행 후보로 새로 허용되면 현재 프레임의 매니저에 알립니다.
     */
    void FTickFunction::SetCanEverTick(bool CanEverTick) {
        const bool WasTickable{mCanEverTick};
        mCanEverTick = CanEverTick;

        if (mCanEverTick && !WasTickable) {
            NotifyTickEnabled();
        }
    }

    /**
     * @brief 등록과 별도로 유지하는 실행 활성 상태를 조회합니다.
     */
    bool FTickFunction::IsTickEnabled() const {
        return mTickEnabled;
    }

    /**
     * @brief 비활성 상태에서 활성 상태로 바뀌면 새 Tick 후보로 등록할 수 있도록 알립니다.
     */
    void FTickFunction::SetTickEnabled(bool Enabled) {
        const bool WasEnabled{mTickEnabled};
        mTickEnabled = Enabled;

        if (mTickEnabled && !WasEnabled) {
            NotifyTickEnabled();
        }
    }

    /**
     * @brief 일시정지 목록을 만들 때 사용할 허용 값을 조회합니다.
     */
    bool FTickFunction::IsTickEvenWhenPaused() const {
        return mTickEvenWhenPaused;
    }

    /**
     * @brief 이후 일시정지 프레임의 후보 선정에 사용할 허용 값을 저장합니다.
     */
    void FTickFunction::SetTickEvenWhenPaused(bool Enabled) {
        mTickEvenWhenPaused = Enabled;
    }

    /**
     * @brief 대상과 레벨의 소속을 검증하고 등록 목록의 위치를 보관합니다.
     * @details 다른 월드에서 잡힌 프레임 큐 위치를 지운 뒤, 활성 프레임이 있으면 신규 등록을 알립니다.
     */
    bool FTickFunction::RegisterTickFunction(Engine::ULevel& Level) {
        const CoreUObject::UObject* Target{GetTarget()};
        const Engine::UActorComponent* Component{CoreUObject::Cast<Engine::UActorComponent>(Target)};
        const Engine::AActor* Actor{Component != nullptr ? Component->GetOwner() : CoreUObject::Cast<Engine::AActor>(Target)};

        if (Level.mWorld == nullptr || Actor == nullptr || Actor->GetWorld() != Level.mWorld || Actor->GetOuter() != &Level || Actor->IsPendingDestroy() || (Component != nullptr && Component->GetWorld() != Level.mWorld)) {
            return false;
        }

        if (mTaskLevel != nullptr) {
            return mTaskLevel == &Level.mTickTaskLevel;
        }

        if (mQueuedWorld != nullptr && mQueuedWorld != Level.mWorld) {
            ClearQueuedTick();
        }

        mRegistrationPosition = Level.mTickTaskLevel.Register(*this);
        mTaskLevel = &Level.mTickTaskLevel;
        NotifyTickEnabled();

        return true;
    }

    /**
     * @brief 저장한 목록 위치로 레벨 등록을 해제합니다.
     * @details 이미 실행 큐에 배치한 항목은 이번 프레임의 호출 조건에 따라 처리됩니다.
     */
    void FTickFunction::UnregisterTickFunction() {
        if (mTaskLevel != nullptr) {
            mTaskLevel->Unregister(mRegistrationPosition);
            mTaskLevel = nullptr;
            mRegistrationPosition = {};
        }
    }

    /**
     * @brief 레벨 등록을 관리하는 Tick 목록에 연결되어 있는지 확인합니다.
     */
    bool FTickFunction::IsTickFunctionRegistered() const {
        return mTaskLevel != nullptr;
    }

    /**
     * @brief 순환이 생기는지 검사하고 유효한 선행 Tick의 약한 참조를 추가합니다.
     */
    bool FTickFunction::AddPrerequisite(Engine::FTickFunction& Prerequisite) {
        if (Prerequisite.DependsOn(*this)) {
            return false;
        }

        std::erase_if(mPrerequisites, [](const Engine::FTickFunction::FWeakTickFunction& Entry) {
            return Entry.mLifetime.expired();
        });

        if (std::ranges::any_of(mPrerequisites, [&Prerequisite](const Engine::FTickFunction::FWeakTickFunction& Entry) {
            return Entry.mFunction == &Prerequisite;
        })) {
            return true;
        }

        mPrerequisites.push_back(Prerequisite.GetWeakReference());

        return true;
    }

    /**
     * @brief 지정한 함수와 주소가 같고 아직 살아 있는 선행 항목을 제거합니다.
     */
    bool FTickFunction::RemovePrerequisite(const Engine::FTickFunction& Prerequisite) {
        return std::erase_if(mPrerequisites, [&Prerequisite](const Engine::FTickFunction::FWeakTickFunction& Entry) {
            return Entry.mFunction == &Prerequisite && !Entry.mLifetime.expired();
        }) != 0;
    }

    /**
     * @brief 대상 타입에 맞춰 액터 또는 컴포넌트의 실행 조건을 검사합니다.
     */
    bool FTickFunction::CanExecuteTick(const Engine::UWorld& World, bool EditorTick) const {
        const CoreUObject::UObject* Target{GetTarget()};

        if (const auto* Component{CoreUObject::Cast<Engine::UActorComponent>(Target)}) {
            return CanTickComponent(*Component, World, EditorTick);
        }

        const auto* Actor{CoreUObject::Cast<Engine::AActor>(Target)};

        return Actor != nullptr && CanTickActor(*Actor, World, EditorTick);
    }

    /**
     * @brief 실행 큐가 보관한 포인터와 함수에 저장된 배치 상태를 함께 비웁니다.
     */
    void FTickFunction::ClearQueuedTick() {
        if (mQueuedSlot != nullptr) {
            *mQueuedSlot = nullptr;
            mQueuedSlot = nullptr;
            mQueuedWorld = nullptr;
            mQueuedGroup = Engine::ETickingGroup::Count;
        }
    }

    /**
     * @brief 등록과 활성 조건을 만족하면 레벨의 활성 매니저에 후보 등록을 요청합니다.
     */
    void FTickFunction::NotifyTickEnabled() {
        if (mCanEverTick && mTickEnabled && mTaskLevel != nullptr && mTaskLevel->mActiveManager != nullptr) {
            mTaskLevel->mActiveManager->RegisterNewTick(*this);
        }
    }

    /**
     * @brief 첫 약한 참조를 만들 때 수명 토큰을 생성하고 이후에는 같은 토큰을 사용합니다.
     */
    Engine::FTickFunction::FWeakTickFunction FTickFunction::GetWeakReference() {
        if (!mLifetime) {
            mLifetime = std::make_shared<Engine::FTickFunction::FLifetime>();
        }

        return {this, mLifetime};
    }

    /**
     * @brief 살아 있는 선행 Tick을 깊이 우선으로 탐색하여 자기 자신을 포함한 도달 여부를 검사합니다.
     */
    bool FTickFunction::DependsOn(const Engine::FTickFunction& Function) const {
        Core::TArray<const Engine::FTickFunction*> Pending{this};
        std::unordered_set<const Engine::FTickFunction*> Visited{};

        while (!Pending.empty()) {
            const Engine::FTickFunction* Current{Pending.back()};
            Pending.pop_back();

            if (Current == &Function) {
                return true;
            }

            if (!Visited.insert(Current).second) {
                continue;
            }

            for (const Engine::FTickFunction::FWeakTickFunction& Entry : Current->mPrerequisites) {
                if (!Entry.mLifetime.expired()) {
                    Pending.push_back(Entry.mFunction);
                }
            }
        }

        return false;
    }

    /**
     * @brief 기본 Tick을 호출할 액터의 참조를 보관합니다.
     */
    FActorTickFunction::FActorTickFunction(Engine::AActor& Target)
		: mTarget{Target} {
    }

    /**
     * @brief 등록과 실행 검증에 사용할 대상 액터의 주소를 반환합니다.
     */
    const CoreUObject::UObject* FActorTickFunction::GetTarget() const {
        return &mTarget;
    }

    /**
     * @brief 대상 액터에 액터 Tick 실행 조건을 적용합니다.
     */
    bool FActorTickFunction::CanExecuteTick(const Engine::UWorld& World, bool EditorTick) const {
        return CanTickActor(mTarget, World, EditorTick);
    }

    /**
     * @brief 호출 횟수를 기록한 뒤 대상 액터가 구현한 Tick()에 시간 간격을 전달합니다.
     */
    void FActorTickFunction::ExecuteTick(float DeltaTime) {
        Engine::EngineStats::ActorTicks.Add();
        mTarget.Tick(DeltaTime);
    }

    /**
     * @brief 기본 Tick을 호출할 컴포넌트의 참조를 보관합니다.
     */
    FActorComponentTickFunction::FActorComponentTickFunction(Engine::UActorComponent& Target)
		: mTarget{Target} {
    }

    /**
     * @brief 등록과 실행 검증에 사용할 대상 컴포넌트의 주소를 반환합니다.
     */
    const CoreUObject::UObject* FActorComponentTickFunction::GetTarget() const {
        return &mTarget;
    }

    /**
     * @brief 소유 액터와 대상 컴포넌트의 실행 조건을 함께 적용합니다.
     */
    bool FActorComponentTickFunction::CanExecuteTick(const Engine::UWorld& World, bool EditorTick) const {
        return CanTickComponent(mTarget, World, EditorTick);
    }

    /**
     * @brief 호출 횟수를 기록한 뒤 대상 컴포넌트가 구현한 Tick()에 시간 간격을 전달합니다.
     */
    void FActorComponentTickFunction::ExecuteTick(float DeltaTime) {
        Engine::EngineStats::ComponentTicks.Add();
        mTarget.Tick(DeltaTime);
    }

}
