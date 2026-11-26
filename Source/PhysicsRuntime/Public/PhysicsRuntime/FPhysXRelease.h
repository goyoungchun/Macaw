/**
 * @file FPhysXRelease.h
 * @brief PhysX 객체를 release()로 해제하는 std::unique_ptr 삭제자입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

namespace PhysicsRuntime {

    /**
     * @brief PhysX 객체를 release()로 해제하는 std::unique_ptr 삭제자입니다.
     * @details PhysX 객체는 delete가 아니라 각 객체의 release()로 해제해야 하므로 단독 소유하는 멤버에 사용합니다.
     * @ingroup PhysicsRuntime
     */
    struct FPhysXRelease {
        /**
         * @brief PhysX 객체의 release()를 호출합니다.
         * @param[in] Object 해제할 PhysX 객체입니다.
         */
        template <typename ObjectType>
        void operator()(ObjectType* Object) const noexcept;
    };

    /**
     * @brief PhysX 객체의 release()를 호출합니다.
     */
    template <typename ObjectType>
    void PhysicsRuntime::FPhysXRelease::operator()(ObjectType* Object) const noexcept {
        Object->release();
    }

}
