/**
 * @file EWindSourceType.h
 * @brief 바람 원천의 종류를 구분합니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "EWindSourceType.generated.h"

namespace Engine {

    /**
     * @brief 바람 원천의 종류를 구분합니다.
     * @ingroup Engine
     */
    UENUM()
    enum class EWindSourceType : Uint8 {
        /**
         * @brief 월드 어디에서나 같은 방향과 세기로 부는 바람입니다. 방향은 컴포넌트의 앞쪽(X축)입니다.
         */
        Directional,
        /**
         * @brief 컴포넌트 위치에서 반경 안으로 퍼져 나가는 바람입니다. 거리가 멀수록 약해집니다.
         */
        Point,
        /**
         * @brief 컴포넌트 앞쪽으로 원뿔 범위 안에서만 부는 바람이며 선풍기에 씁니다. 거리와 원뿔 가장자리로 갈수록 약해집니다.
         */
        Cone
    };

}
