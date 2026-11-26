/**
 * @file FPropertyValue.h
 * @brief 프로퍼티가 지원하는 스칼라·수학 타입과 불투명 주소의 variant입니다.
 * @ingroup CoreUObject
 */

#pragma once

#include "Core/Guid.h"
#include "Core/Literals/FDimension.h"
#include "Core/Math.h"
#include "CoreUObject/FObjectReference.h"

#include <variant>

namespace CoreUObject {

    struct FQuantityPropertyValue {
    public:
        bool operator==(const CoreUObject::FQuantityPropertyValue& Other) const = default;

    public:
        Core::FDimension mDimension{};
        float mBaseValue{};
    };

    /**
     * @brief 프로퍼티가 지원하는 스칼라·수학 타입과 불투명 주소의 variant입니다.
     */
    using FPropertyValue = std::variant<bool, Int32, Uint32, Uint64, float, Core::FString, Core::FGuid, FVector2, FVector, FVector4, FQuat, FRotator, FMatrix, FTransform, FBox, FRay, CoreUObject::FObjectReference, CoreUObject::FQuantityPropertyValue, const void*>;

    /**
     * @brief 프로퍼티 variant에 저장된 실제 값의 주소를 반환합니다.
     * @param[in,out] Value 프로퍼티 타입에 대응하는 variant 값입니다.
     * @return 조회한 값의 비소유 주소이며, 해당 값이 없으면 nullptr입니다.
     */
    void* GetPropertyData(CoreUObject::FPropertyValue& Value);

}
