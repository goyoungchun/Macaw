/**
 * @file FTickTaskManager.cpp
 * @brief 월드의 프레임 Tick을 그룹별로 배치하고 일시정지 프레임을 실행합니다.
 * @ingroup Engine
 */

#include "Engine/Tick/FTickTaskManager.h"
#include "Engine/ULevel.h"
#include "Engine/UWorld.h"

#include <algorithm>
#include <utility>

namespace Engine {

    /**
     * @brief 매니저가 사라지기 전에 Tick 함수에 남은 실행 큐 위치를 해제합니다.
     */
    FTickTaskManager::~FTickTaskManager() {
        EndFrame();
    }

    /**
     * @brief 영속 레벨의 등록 상태를 스냅샷으로 만들고 프레임 도중 등록 알림을 연결합니다.
     */
    void FTickTaskManager::StartFrame(const Engine::UWorld& World, float DeltaTime, bool EditorTick) {
        EndFrame();

        mWorld = &World;
        mDeltaTime = DeltaTime;
        mEditorTick = EditorTick;

        Engine::ULevel* Level{World.GetPersistentLevel()};

        if (Level == nullptr) {
            return;
        }

        mTaskLevel = &Level->mTickTaskLevel;
        mTaskLevel->mActiveManager = this;
        const auto& RegisteredTicks{mTaskLevel->mRegisteredTicks};
        const Core::TArray<Engine::FTickFunction*> Candidates{RegisteredTicks.begin(), RegisteredTicks.end()};
        QueueTicks(Candidates, Engine::ETickingGroup::PrePhysics);
    }

    /**
     * @brief 현재 그룹을 실행한 뒤 늦게 등록된 Tick을 아직 열리지 않은 그룹에 배치합니다.
     * @details 마지막 그룹 뒤에는 신규 등록이 이어져도 한 프레임에 최대 101회만 다시 수집하고 실행합니다.
     */
    void FTickTaskManager::RunTickGroup(Engine::ETickingGroup Group) {
        RunQueuedTicks(mGroups[static_cast<Uint8>(Group)], 0, false);

        if (mWorld->mEndPlayRequested || mWorld->mShutdownRequested || Group == Engine::ETickingGroup::DuringPhysics) {
            return;
        }

        auto EarliestGroup{static_cast<Engine::ETickingGroup>(static_cast<Uint8>(Group) + 1)};

        if (EarliestGroup == Engine::ETickingGroup::StartPhysics || EarliestGroup == Engine::ETickingGroup::EndPhysics) {
            EarliestGroup = static_cast<Engine::ETickingGroup>(static_cast<Uint8>(EarliestGroup) + 1);
        }

        if (EarliestGroup != Engine::ETickingGroup::NewlySpawned) {
            QueueNewlyRegisteredTicks(EarliestGroup);
            return;
        }

        constexpr std::size_t MaxNewlySpawnedPasses{101};
        const auto& Ticks{mGroups[static_cast<Uint8>(Engine::ETickingGroup::NewlySpawned)]};

        for (std::size_t Pass{}; Pass < MaxNewlySpawnedPasses && !mNewlyRegisteredTicks.empty() && !mWorld->mEndPlayRequested && !mWorld->mShutdownRequested; ++Pass) {
            const std::size_t FirstTick{Ticks.size()};
            QueueNewlyRegisteredTicks(Engine::ETickingGroup::NewlySpawned);
            RunQueuedTicks(Ticks, FirstTick, false);
        }
    }

    /**
     * @brief 그룹 없이 실행할 일시정지 후보를 먼저 모은 뒤 한 번 호출합니다.
     */
    bool FTickTaskManager::RunPauseFrame(const Engine::UWorld& World, float DeltaTime) {
        EndFrame();

        mWorld = &World;
        mDeltaTime = DeltaTime;
        mEditorTick = false;

        if (const Engine::ULevel* Level{World.GetPersistentLevel()}) {
            for (Engine::FTickFunction* Function : Level->mTickTaskLevel.mRegisteredTicks) {
                if (Function->IsTickEvenWhenPaused() && CanExecuteTick(*Function)) {
                    QueueTick(*Function, mPauseTicks, Engine::ETickingGroup::Count);
                }
            }
        }

        const bool Executed{RunQueuedTicks(mPauseTicks, 0, true)};
        EndFrame();

        return Executed;
    }

    /**
     * @brief 레벨과의 알림 연결을 끊고 Tick 함수의 프레임 배치 상태를 정리합니다.
     */
    void FTickTaskManager::EndFrame() {
        if (mTaskLevel != nullptr) {
            mTaskLevel->mActiveManager = nullptr;
            mTaskLevel = nullptr;
        }

        for (auto& Group : mGroups) {
            ClearQueue(Group);
        }

        ClearQueue(mPauseTicks);
        mNewlyRegisteredTicks.clear();
        mWorld = nullptr;
    }

    /**
     * @brief 현재 프레임에 예약한 적이 없는 Tick을 약한 참조로 기록합니다.
     */
    void FTickTaskManager::RegisterNewTick(Engine::FTickFunction& Function) {
        if (Function.mQueuedSlot == nullptr) {
            mNewlyRegisteredTicks.push_back(Function.GetWeakReference());
        }
    }

    /**
     * @brief 그룹 경계에서 살아 있고 같은 레벨에 등록된 후보만 꺼내 배치합니다.
     */
    void FTickTaskManager::QueueNewlyRegisteredTicks(Engine::ETickingGroup EarliestGroup) {
        if (mNewlyRegisteredTicks.empty()) {
            return;
        }

        const auto RegisteredTicks{std::exchange(mNewlyRegisteredTicks, {})};
        Core::TArray<Engine::FTickFunction*> Candidates{};
        Candidates.reserve(RegisteredTicks.size());

        for (const auto& Entry : RegisteredTicks) {
            if (!Entry.mLifetime.expired() && Entry.mFunction->mTaskLevel == mTaskLevel) {
                Candidates.push_back(Entry.mFunction);
            }
        }

        QueueTicks(Candidates, EarliestGroup);
    }

    /**
     * @brief 명시적인 스택으로 선행 Tick을 먼저 방문하고 그룹별 실행 순서를 결정합니다.
     * @details 이미 배치된 선행 Tick의 실제 그룹을 읽습니다. 미배치 선행 Tick이 이번 후보에 있으면 스택에 올려 먼저 처리합니다.
     */
    void FTickTaskManager::QueueTicks(const Core::TArray<Engine::FTickFunction*>& Candidates, Engine::ETickingGroup EarliestGroup) {
        /**
         * @brief 선행 Tick을 순회하면서 한 후보의 그룹과 검사 위치를 기록합니다.
         */
        struct FQueuedTick {
            /**
             * @brief 이번 배치에서 방문하는 Tick 함수입니다.
             */
            Engine::FTickFunction* mFunction{};
            /**
             * @brief 지금까지 확인한 조건을 반영한 실행 그룹입니다.
             */
            Engine::ETickingGroup mGroup{};
            /**
             * @brief 아직 검사하지 않은 첫 선행 Tick의 인덱스입니다.
             */
            std::size_t mNextPrerequisite{};
        };

        Core::TArray<FQueuedTick> Ticks{};
        Core::TMap<Engine::FTickFunction*, std::size_t> Indices{};
        Ticks.reserve(Candidates.size());
        Indices.reserve(Candidates.size());

        for (Engine::FTickFunction* Function : Candidates) {
            if (Function->mQueuedSlot == nullptr && CanExecuteTick(*Function) && Indices.emplace(Function, Ticks.size()).second) {
                Ticks.push_back({Function, std::max(Function->GetTickGroup(), EarliestGroup)});
            }
        }

        Core::TArray<std::size_t> Pending{};

        for (std::size_t Index{}; Index < Ticks.size(); ++Index) {
            if (Ticks[Index].mFunction->mQueuedSlot != nullptr) {
                continue;
            }

            Pending.push_back(Index);
            while (!Pending.empty()) {
                FQueuedTick& Tick{Ticks[Pending.back()]};
                const auto& Prerequisites{Tick.mFunction->mPrerequisites};

                if (Tick.mNextPrerequisite == Prerequisites.size()) {
                    QueueTick(*Tick.mFunction, mGroups[static_cast<Uint8>(Tick.mGroup)], Tick.mGroup);
                    Pending.pop_back();
                    continue;
                }

                const auto& Entry{Prerequisites[Tick.mNextPrerequisite]};

                if (!Entry.mLifetime.expired() && Entry.mFunction->mTaskLevel == mTaskLevel) {
                    if (Entry.mFunction->mQueuedSlot != nullptr) {
                        Tick.mGroup = std::max(Tick.mGroup, Entry.mFunction->mQueuedGroup);
                    } else if (const auto Position{Indices.find(Entry.mFunction)}; Position != Indices.end()) {
                        Pending.push_back(Position->second);
                        continue;
                    }
                }

                ++Tick.mNextPrerequisite;
            }
        }
    }

    /**
     * @brief 함수 포인터를 큐에 넣고 프레임 끝까지 사용할 배치 위치를 함수에 기록합니다.
     */
    void FTickTaskManager::QueueTick(Engine::FTickFunction& Function, Engine::FTickTaskManager::FTickQueue& Ticks, Engine::ETickingGroup Group) {
        Ticks.push_back(&Function);
        Function.mQueuedSlot = &Ticks.back();
        Function.mQueuedWorld = mWorld;
        Function.mQueuedGroup = Group;
    }

    /**
     * @brief 종료 요청, 비어 있는 큐 칸, 대상 실행 조건을 검사하며 큐를 순회합니다.
     * @details NewlySpawned 반복에서는 이번에 추가된 위치부터 순회하여 기존 Tick을 다시 호출하지 않습니다.
     */
    bool FTickTaskManager::RunQueuedTicks(const Engine::FTickTaskManager::FTickQueue& Ticks, std::size_t FirstTick, bool PauseFrame) {
        bool Executed{};

        for (std::size_t Index{FirstTick}; Index < Ticks.size(); ++Index) {
            if (mWorld->mEndPlayRequested || mWorld->mShutdownRequested) {
                break;
            }

            Engine::FTickFunction* Function{Ticks[Index]};

            if (Function != nullptr && (PauseFrame ? Function->CanExecuteTick(*mWorld, false) : CanExecuteTick(*Function))) {
                Executed = true;
                Function->ExecuteTick(mDeltaTime);
            }
        }

        return Executed;
    }

    /**
     * @brief Tick 함수의 큐 위치를 먼저 무효화한 뒤 저장 공간을 비웁니다.
     */
    void FTickTaskManager::ClearQueue(Engine::FTickTaskManager::FTickQueue& Ticks) {
        for (Engine::FTickFunction* Function : Ticks) {
            if (Function != nullptr) {
                Function->ClearQueuedTick();
            }
        }

        Ticks.clear();
    }

    /**
     * @brief 일반 실행 후보의 Tick 허용, 활성 상태와 대상 실행 조건을 모두 확인합니다.
     */
    bool FTickTaskManager::CanExecuteTick(const Engine::FTickFunction& Function) const {
        return Function.CanEverTick() && Function.IsTickEnabled() && Function.CanExecuteTick(*mWorld, mEditorTick);
    }

}
