/**
 * @file PhysicsConversion.h
 * @brief 엔진 수학 타입과 PhysX 수학 타입 사이의 변환을 제공합니다.
 * @details 엔진과 PhysX는 같은 단위(1유닛 = 1m)와 축을 사용하므로 성분을 그대로 옮깁니다.
 *          행렬은 저장 순서가 다르므로 PxMat44를 FMatrix로 직접 복사하지 않고 위치·회전 단위로 변환합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Core/Math/FQuat.h"
#include "Core/Math/FTransform.h"

#include <PxPhysicsAPI.h>

namespace PhysicsRuntime {

    /**
     * @brief 엔진 벡터를 PhysX 벡터로 변환합니다.
     * @param[in] Vector 변환할 엔진 벡터입니다.
     * @return 같은 성분을 가진 PhysX 벡터입니다.
     */
    physx::PxVec3 ToPxVec3(const FVector& Vector);

    /**
     * @brief PhysX 벡터를 엔진 벡터로 변환합니다.
     * @param[in] Vector 변환할 PhysX 벡터입니다.
     * @return 같은 성분을 가진 엔진 벡터입니다.
     */
    FVector ToFVector(const physx::PxVec3& Vector);

    /**
     * @brief 엔진 쿼터니언을 PhysX 쿼터니언으로 변환합니다.
     * @param[in] Quat 변환할 엔진 쿼터니언입니다.
     * @return 같은 성분을 가진 PhysX 쿼터니언입니다.
     * @note PhysX 변환에 쓰려면 단위 쿼터니언이어야 합니다.
     */
    physx::PxQuat ToPxQuat(const FQuat& Quat);

    /**
     * @brief PhysX 쿼터니언을 엔진 쿼터니언으로 변환합니다.
     * @param[in] Quat 변환할 PhysX 쿼터니언입니다.
     * @return 같은 성분을 가진 엔진 쿼터니언입니다.
     */
    FQuat ToFQuat(const physx::PxQuat& Quat);

    /**
     * @brief 엔진 변환의 위치와 회전을 PhysX 변환으로 변환합니다.
     * @param[in] Transform 변환할 엔진 변환입니다.
     * @return 위치와 정규화한 회전을 가진 PhysX 변환입니다.
     * @note PhysX 액터는 스케일을 갖지 않으므로 스케일은 형상 크기에 따로 반영해야 합니다.
     */
    physx::PxTransform ToPxTransform(const FTransform& Transform);

    /**
     * @brief PhysX 변환과 별도로 보관한 스케일로 엔진 변환을 만듭니다.
     * @param[in] Transform 변환할 PhysX 변환입니다.
     * @param[in] Scale 결과에 적용할 엔진 스케일입니다.
     * @return 위치·회전은 PhysX 변환에서, 스케일은 인수에서 가져온 엔진 변환입니다.
     */
    FTransform ToFTransform(const physx::PxTransform& Transform, const FVector& Scale);

    /**
     * @brief 엔진의 로컬 Z축 캡슐을 PhysX의 로컬 X축 캡슐로 맞추는 회전을 반환합니다.
     * @return Y축을 기준으로 -90도 회전하여 X축을 Z축으로 보내는 쿼터니언입니다.
     * @note 캡슐 형상의 로컬 자세 회전 뒤에 곱해 사용합니다.
     */
    physx::PxQuat GetSphylBasis();

}
