/**
 * @file FTickTaskLevel.cpp
 * @brief 레벨이 보관하는 Tick 함수 포인터의 추가와 제거를 처리합니다.
 * @ingroup Engine
 */

#include "Engine/Tick/FTickTaskLevel.h"
#include "Engine/Tick/FTickFunction.h"

namespace Engine {

    /**
     * @brief 등록된 Tick이 레벨의 목록 위치를 참조하지 않도록 등록을 해제합니다.
     */
    FTickTaskLevel::~FTickTaskLevel() {
        while (!mRegisteredTicks.empty()) {
            mRegisteredTicks.back()->UnregisterTickFunction();
        }
    }

    /**
     * @brief 함수 포인터를 목록 끝에 넣어 등록 순서를 유지합니다.
     */
    Engine::FTickTaskLevel::FTickList::iterator FTickTaskLevel::Register(Engine::FTickFunction& Function) {
        return mRegisteredTicks.insert(mRegisteredTicks.end(), &Function);
    }

    /**
     * @brief 저장된 반복자로 함수를 찾아 레벨의 등록 목록에서 제거합니다.
     */
    void FTickTaskLevel::Unregister(Engine::FTickTaskLevel::FTickList::iterator Position) {
        mRegisteredTicks.erase(Position);
    }

}
