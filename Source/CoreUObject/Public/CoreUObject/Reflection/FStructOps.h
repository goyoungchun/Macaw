/**
 * @file FStructOps.h
 * @brief 타입이 지워진 네이티브 구조체 값을 복제하고 파괴하는 함수 테이블입니다.
 * @ingroup CoreUObject
 */

#pragma once

#include "Core/Types.h"

namespace CoreUObject {

    /**
     * @brief 타입이 지워진 네이티브 구조체 값을 복제하고 파괴하는 함수 테이블입니다.
     * @details 단일 구조체 프로퍼티를 로드하거나 편집할 때 현재 값을 작업용 사본으로 복제하고,
     *          사본을 채운 뒤 setter로 한 번에 커밋하는 데 사용합니다.
     * @ingroup CoreUObject
     */
    struct FStructOps {
    public:
        /**
         * @brief 원본 값을 복사 생성한 작업용 값을 힙에 만듭니다.
         */
        void* (*mClone)(const void* Source);

        /**
         * @brief mClone으로 만든 값을 파괴합니다.
         */
        void (*mDestroy)(void* Value);
    };

}
