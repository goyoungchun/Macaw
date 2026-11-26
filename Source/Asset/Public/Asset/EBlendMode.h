/**
 * @file EBlendMode.h
 * @brief 머티리얼 표면의 불투명·반투명 합성 방식을 구분합니다.
 * @ingroup Asset
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "EBlendMode.generated.h"

namespace Asset {

    /**
     * @brief 머티리얼 표면의 불투명·반투명 합성 방식을 구분합니다.
     */
    UENUM()
    enum class EBlendMode : Uint8 {
        /**
         * @brief 깊이를 기록하는 불투명 표면입니다.
         */
        Opaque,

        /**
         * @brief 깊이를 읽고 배경과 알파 합성하는 반투명 표면입니다.
         */
        Translucent,
        Additive
    };

}
