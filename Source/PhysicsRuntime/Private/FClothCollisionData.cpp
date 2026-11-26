/**
 * @file FClothCollisionData.cpp
 * @brief 천이 부딪히는 구, 캡슐, 볼록 다면체 목록을 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FClothCollisionData.h"

namespace PhysicsRuntime {

    /**
     * @brief 모든 목록을 비웁니다.
     */
    void FClothCollisionData::Reset() {
        mSpheres.clear();
        mSphereConnections.clear();
        mConvexes.clear();
    }

    /**
     * @brief 다른 목록을 뒤에 붙이고, 붙인 캡슐의 구 번호를 붙인 위치에 맞게 옮깁니다.
     */
    void FClothCollisionData::Append(const FClothCollisionData& Other) {
        const Index_t NumSpheresBefore{mSpheres.size()};
        const Index_t NumSphereConnectionsBefore{mSphereConnections.size()};

        mSpheres.insert(mSpheres.end(), Other.mSpheres.begin(), Other.mSpheres.end());
        mSphereConnections.insert(mSphereConnections.end(), Other.mSphereConnections.begin(), Other.mSphereConnections.end());

        // 붙인 캡슐은 붙이기 전의 구 개수만큼 뒤의 구를 가리켜야 합니다.
        if (NumSpheresBefore > 0) {
            for (Index_t ConnectionIndex{NumSphereConnectionsBefore}; ConnectionIndex < mSphereConnections.size(); ++ConnectionIndex) {
                FClothCollisionPrim_SphereConnection& Connection{mSphereConnections[ConnectionIndex]};
                Connection.mSphereIndices[0] += NumSpheresBefore;
                Connection.mSphereIndices[1] += NumSpheresBefore;
            }
        }

        mConvexes.insert(mConvexes.end(), Other.mConvexes.begin(), Other.mConvexes.end());
    }
}
