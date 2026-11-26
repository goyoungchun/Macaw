/**
 * @file FClothCollisionData.h
 * @brief 천이 부딪히는 구, 캡슐, 볼록 다면체 목록을 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector4.h"

namespace PhysicsRuntime {

    /**
     * @brief 천이 부딪히는 구 하나이며, 혼자 쓰이거나 캡슐의 한쪽 끝으로 쓰입니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothCollisionPrim_Sphere {
        /**
         * @brief 구가 따라 움직이는 뼈의 천 에셋 사용 뼈 번호이며, InvalidIndex이면 컴포넌트 좌표에 고정됩니다.
         */
        Index_t mBoneIndex{InvalidIndex};

        /**
         * @brief 구의 반지름(m)입니다.
         */
        Core::FLength mRadius{};

        /**
         * @brief 뼈 좌표의 구 중심입니다.
         */
        FVector mLocalPosition{};
    };

    /**
     * @brief 구 두 개를 이어 만든 캡슐입니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothCollisionPrim_SphereConnection {
        /**
         * @brief 같은 충돌 목록 안에서 캡슐 양 끝 구의 번호입니다.
         */
        Core::TStaticArray<Index_t, 2> mSphereIndices{InvalidIndex, InvalidIndex};
    };

    /**
     * @brief 평면 여러 장으로 둘러싼 볼록 다면체이며, 천은 그 바깥에 머물려고 합니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothCollisionPrim_Convex {
        /**
         * @brief 컴포넌트 좌표의 평면이며 XYZ는 바깥쪽 법선, W는 법선과 평면 위 점의 내적입니다.
         */
        Core::TArray<FVector4> mPlanes{};

        /**
         * @brief 다면체가 따라 움직이는 뼈의 천 에셋 사용 뼈 번호이며 지금은 쓰지 않습니다.
         */
        Index_t mBoneIndex{InvalidIndex};
    };

    /**
     * @brief 천이 부딪히는 구, 캡슐, 볼록 다면체 목록입니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothCollisionData {
        /**
         * @brief 모든 목록을 비웁니다.
         */
        void Reset();

        /**
         * @brief 다른 목록을 뒤에 붙이고, 붙인 캡슐의 구 번호를 붙인 위치에 맞게 옮깁니다.
         * @param[in] Other 붙일 목록입니다.
         */
        void Append(const FClothCollisionData& Other);

        /**
         * @brief 구 목록입니다.
         */
        Core::TArray<FClothCollisionPrim_Sphere> mSpheres{};

        /**
         * @brief 구 목록의 번호로 두 구를 이은 캡슐 목록입니다.
         */
        Core::TArray<FClothCollisionPrim_SphereConnection> mSphereConnections{};

        /**
         * @brief 볼록 다면체 목록입니다.
         */
        Core::TArray<FClothCollisionPrim_Convex> mConvexes{};
    };
}
