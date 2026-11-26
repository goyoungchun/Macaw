/**
 * @file FObjectHandle.h
 * @brief 전역 슬롯 인덱스와 세대로 구성되어 재사용된 슬롯의 오참조를 막는 핸들입니다.
 * @ingroup CoreUObject
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Types.h"

namespace CoreUObject {

    /**
     * @brief 전역 슬롯 인덱스와 세대로 구성되어 재사용된 슬롯의 오참조를 막는 핸들입니다.
     * @ingroup CoreUObject
     */
    struct FObjectHandle {
    public:
        /**
         * @brief 슬롯 인덱스와 세대가 유효한 핸들 표현인지 검사합니다. 실제 객체 존재 여부는 레지스트리에서 확인해야
         * 합니다.
         * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
         * @note 객체 생존 여부는 FObjectRegistry::Resolve()로 별도 검사합니다.
         */
        bool IsValid() const;

        /**
         * @brief 슬롯 인덱스와 세대가 모두 같은지 검사합니다.
         * @param[in] Other 비교할 상대 값입니다.
         * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool operator==(const CoreUObject::FObjectHandle& Other) const;

    public:
        /**
         * @brief 레지스트리 슬롯 인덱스입니다.
         */
        Int32 mIndex{-1};

        /**
         * @brief 슬롯 재사용을 구분하는 세대입니다.
         */
        Uint32 mGeneration{};
    };

    /**
     * @brief 레지스트리 슬롯 인덱스와 세대를 함께 해시하는 함수 객체입니다.
     */
    struct FObjectHandleHash {
    public:
        /**
         * @brief 객체 참조 또는 핸들의 해시 값을 계산합니다.
         * @param[in] Handle 해석하거나 해시할 런타임 객체 세대 핸들입니다.
         * @return 런타임 핸들에 대응하는 해시 값입니다.
         */
        std::size_t operator()(const CoreUObject::FObjectHandle& Handle) const;
    };

    namespace ObjectHandlePrivate {

        template <typename Key>
        struct TObjectHashType;

        template <>
        struct TObjectHashType<CoreUObject::FObjectHandle> {
        public:
            using Type = CoreUObject::FObjectHandleHash;
        };

    }

    template <typename Key, typename Value>
    using ObjectHashMap = Core::TMap<Key, Value, typename CoreUObject::ObjectHandlePrivate::TObjectHashType<Key>::Type>;

}
