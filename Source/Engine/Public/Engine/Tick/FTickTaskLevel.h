/**
 * @file FTickTaskLevel.h
 * @brief 레벨 단위의 Tick 함수 등록 상태를 선언합니다.
 * @ingroup Engine
 */

#pragma once

#include <list>

namespace Engine {

    class FTickFunction;
    class FTickTaskManager;

    /** @brief 레벨에 등록된 Tick 함수의 비소유 포인터 목록을 관리합니다. */
    class FTickTaskLevel {
    private:
        /** @brief 등록 순서를 유지하고 다른 함수를 추가하거나 지워도 기존 위치가 유효한 목록입니다. */
        using FTickList = std::list<Engine::FTickFunction*>;

    public:
        /** @brief 등록된 Tick이 없는 레벨 목록을 초기화합니다. */
        FTickTaskLevel() = default;

        /** @brief 목록을 파괴하기 전에 아직 연결된 함수의 레벨 등록을 해제합니다. */
        ~FTickTaskLevel();

        /** @brief 등록된 함수가 참조하는 레벨 목록의 복사 생성을 금지합니다. */
        FTickTaskLevel(const Engine::FTickTaskLevel&) = delete;

        /** @brief 함수가 기억하는 등록 위치를 보존하기 위해 복사 대입을 금지합니다. */
        Engine::FTickTaskLevel& operator=(const Engine::FTickTaskLevel&) = delete;

    private:
        /** @brief Tick 함수가 자신을 등록하거나 등록을 해제할 수 있게 합니다. */
        friend class FTickFunction;

        /** @brief 매니저가 후보를 수집하고 현재 프레임의 알림을 연결할 수 있게 합니다. */
        friend class FTickTaskManager;

        /**
         * @brief 함수 포인터를 등록 목록 끝에 추가합니다.
         * @param[in] Function 등록할 Tick 함수입니다.
         * @return 등록 해제에 사용할 목록 위치입니다.
         */
        Engine::FTickTaskLevel::FTickList::iterator Register(Engine::FTickFunction& Function);

        /**
         * @brief 레벨의 등록 목록에서 지정한 항목을 제거합니다.
         * @param[in] Position Register()가 반환한 목록 위치입니다.
         */
        void Unregister(Engine::FTickTaskLevel::FTickList::iterator Position);

    private:
        /** @brief 프레임 사이에도 유지되는 등록 순서의 Tick 함수 포인터 목록입니다. */
        Engine::FTickTaskLevel::FTickList mRegisteredTicks{};

        /** @brief 일반 프레임을 구성 중인 매니저이며, 프레임 도중 등록과 활성화 알림을 받습니다. */
        Engine::FTickTaskManager* mActiveManager{};
    };

}
