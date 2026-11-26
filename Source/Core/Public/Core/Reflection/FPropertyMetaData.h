/**
 * @file FPropertyMetaData.h
 * @brief 프로퍼티에 붙는 에디터용 키·값 메타데이터 목록입니다.
 * @ingroup Core
 */

#pragma once

#include "Core/Types.h"

#include <utility>

namespace Core {

    /**
     * @brief UPROPERTY의 Category와 Meta = (...) 지정자로 적은 키·값 메타데이터를 적은 순서대로 담습니다.
     * @details 에디터 표시 방식처럼 런타임 동작과 무관한 정보를 문자열로 보관합니다.
     *          값의 해석은 메타데이터를 읽는 쪽이 정하므로, 상위 모듈의 타입도 이름 문자열로 가리킬 수 있습니다.
     * @ingroup Core
     */
    using FPropertyMetaData = Core::TArray<std::pair<Core::FString, Core::FString>>;

}
