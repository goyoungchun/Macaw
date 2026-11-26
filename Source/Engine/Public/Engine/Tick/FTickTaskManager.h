/**
 * @file FTickTaskManager.h
 * @brief 월드의 Tick 그룹 실행과 프레임 도중 등록 처리를 선언합니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Tick/FTickFunction.h"

#include <deque>

namespace Engine {

    class UWorld;

    /** @brief 레벨의 등록 목록에서 월드의 프레임 실행 큐를 구성하고 순서대로 Tick을 호출합니다. */
    class FTickTaskManager {
    private:
        /** @brief 큐 끝에 Tick을 추가하는 동안 기존 포인터를 저장한 칸의 주소가 유지되는 큐입니다. */
        using FTickQueue = std::deque<Engine::FTickFunction*>;

    public:
        /** @brief 아직 열린 프레임이 없는 매니저를 초기화합니다. */
        FTickTaskManager() = default;

        /** @brief 열려 있는 프레임의 실행 큐와 레벨 연결을 정리합니다. */
        ~FTickTaskManager();

        /** @brief 레벨과 Tick 함수가 참조하는 매니저의 복사 생성을 금지합니다. */
        FTickTaskManager(const Engine::FTickTaskManager&) = delete;

        /** @brief 매니저 소유 큐의 주소와 연결 상태를 보존하기 위해 복사 대입을 금지합니다. */
        Engine::FTickTaskManager& operator=(const Engine::FTickTaskManager&) = delete;

    public:
        /**
         * @brief 영속 레벨에 등록된 Tick으로 프레임의 첫 실행 목록을 구성합니다.
         * @param[in] World 갱신할 월드입니다.
         * @param[in] DeltaTime Tick에 전달할 초 단위 시간 간격입니다.
         * @param[in] EditorTick 정지한 편집 월드를 갱신할 경우 true입니다.
         */
        void StartFrame(const Engine::UWorld& World, float DeltaTime, bool EditorTick);

        /**
         * @brief 그룹에 배치된 Tick을 실행하고 프레임에 남은 그룹 중 하나에 각 신규 Tick을 배치합니다.
         * @param[in] Group 월드에서 실행할 차례가 된 그룹입니다.
         * @pre StartFrame() 호출 후 월드의 그룹 순서에 따라 호출해야 합니다.
         * @details DuringPhysics 중 등록된 Tick은 EndPhysics 경계까지 기다립니다.
         * LastDemotable 이후에는 NewlySpawned 큐에 새로 추가된 Tick만 제한된 횟수만큼 반복 실행합니다.
         */
        void RunTickGroup(Engine::ETickingGroup Group);

        /**
         * @brief 일시정지 상태에서 실행을 허용한 Tick만 모아 실행합니다.
         * @param[in] World 일시정지된 월드입니다.
         * @param[in] DeltaTime Tick에 전달할 초 단위 시간 간격입니다.
         * @return Tick을 하나 이상 호출했으면 true입니다.
         * @details 이 경로는 그룹과 선행 조건을 사용하지 않습니다. 실행 목록을 만든 뒤 등록한 Tick은 다음 프레임의 후보가 됩니다.
         */
        bool RunPauseFrame(const Engine::UWorld& World, float DeltaTime);

        /** @brief 프레임 큐와 신규 등록 후보를 비우고 레벨의 등록 알림 연결을 해제합니다. */
        void EndFrame();

    private:
        /** @brief 함수가 등록 또는 활성화될 때 매니저에 직접 알릴 수 있게 합니다. */
        friend class FTickFunction;

        /**
         * @brief 활성 일반 프레임에 신규 등록 또는 활성화된 Tick을 후보로 기록합니다.
         * @param[in,out] Function 아직 이번 프레임에 배치하지 않은 경우 약한 참조로 기록할 Tick입니다.
         */
        void RegisterNewTick(Engine::FTickFunction& Function);

        /**
         * @brief 신규 후보 중 살아 있고 현재 레벨에 등록된 Tick을 그룹에 배치합니다.
         * @param[in] EarliestGroup 신규 Tick을 넣을 수 있는 가장 이른 그룹입니다.
         */
        void QueueNewlyRegisteredTicks(Engine::ETickingGroup EarliestGroup);

        /**
         * @brief 실행 가능한 후보의 그룹과 그룹 내 순서를 결정합니다.
         * @param[in] Candidates 이번 배치에서 검사할 Tick 함수 목록입니다.
         * @param[in] EarliestGroup 이 배치에서 사용할 수 있는 가장 이른 그룹입니다.
         * @details 아직 배치되지 않은 선행 Tick을 먼저 처리합니다. 실제 그룹은 설정 그룹, 이번 배치의 가장 이른 그룹, 배치된 선행 Tick의 실제 그룹 중 가장 늦은 값입니다.
         */
        void QueueTicks(const Core::TArray<Engine::FTickFunction*>& Candidates, Engine::ETickingGroup EarliestGroup);

        /**
         * @brief 큐 끝에 함수를 추가하고 함수에 이번 프레임의 배치 위치를 기록합니다.
         * @param[in,out] Function 배치할 Tick 함수입니다.
         * @param[in,out] Ticks 함수를 추가할 실행 큐입니다.
         * @param[in] Group 계산된 실행 그룹입니다. 일시정지 큐에는 Count를 전달합니다.
         */
        void QueueTick(Engine::FTickFunction& Function, Engine::FTickTaskManager::FTickQueue& Ticks, Engine::ETickingGroup Group);

        /**
         * @brief 지정한 위치부터 큐를 순회하며 실행할 수 있는 Tick을 호출합니다.
         * @param[in] Ticks 일반 그룹 또는 일시정지의 실행 큐입니다.
         * @param[in] FirstTick 호출을 시작할 큐 인덱스입니다.
         * @param[in] PauseFrame 일시정지 후보 선정이 끝난 큐이면 true입니다.
         * @return Tick을 하나 이상 호출했으면 true입니다.
         * @details 일반 큐는 활성 상태와 대상의 실행 조건을 다시 확인합니다. 일시정지 큐는 목록 선정 후 대상의 실행 조건을 다시 확인합니다.
         */
        bool RunQueuedTicks(const Engine::FTickTaskManager::FTickQueue& Ticks, std::size_t FirstTick, bool PauseFrame);

        /**
         * @brief 각 함수의 배치 위치를 초기화한 뒤 큐를 비웁니다.
         * @param[in,out] Ticks 이번 프레임에서 사용한 실행 큐입니다.
         */
        void ClearQueue(Engine::FTickTaskManager::FTickQueue& Ticks);

        /**
         * @brief 함수의 허용 상태, 활성화 여부와 현재 월드에서의 실행 조건을 검사합니다.
         * @param[in] Function 실행 조건을 확인할 함수입니다.
         * @return 세 조건을 모두 충족하면 true입니다.
         */
        bool CanExecuteTick(const Engine::FTickFunction& Function) const;

    private:
        /** @brief 프레임을 갱신하는 월드입니다. 프레임을 정리하면 nullptr입니다. */
        const Engine::UWorld* mWorld{};

        /** @brief 일반 프레임 동안 신규 등록과 활성화 알림을 연결한 레벨의 Tick 목록입니다. */
        Engine::FTickTaskLevel* mTaskLevel{};

        /** @brief 현재 프레임의 Tick에 전달할 초 단위 시간 간격입니다. */
        float mDeltaTime{};

        /** @brief 정지한 편집 월드의 Tick 조건을 사용할지 나타냅니다. */
        bool mEditorTick{};

        /** @brief 일반 프레임에서 각 그룹이 호출할 순서대로 배치된 Tick의 큐입니다. */
        Core::TStaticArray<Engine::FTickTaskManager::FTickQueue, static_cast<Uint8>(Engine::ETickingGroup::Count)> mGroups{};

        /** @brief 일시정지 프레임의 시작 시점에 고른 Tick을 보관하는 별도 큐입니다. */
        Engine::FTickTaskManager::FTickQueue mPauseTicks{};

        /** @brief 다음 배치 시점까지 기다리는 신규 등록 또는 활성화된 Tick의 약한 참조입니다. */
        Core::TArray<Engine::FTickFunction::FWeakTickFunction> mNewlyRegisteredTicks{};
    };

}
