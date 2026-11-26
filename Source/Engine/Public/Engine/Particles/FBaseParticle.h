/**
 * @file FBaseParticle.h
 * @brief 모든 파티클이 공유하는 상태와 상태 플래그입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector4.h"

namespace Engine {

    /**
     * @brief 파티클 한 칸의 앞부분에 놓이는 공통 상태입니다.
     * @details 모듈 Payload는 이 구조체 바로 뒤에 붙습니다. 위치·속도·크기는 m 단위이며
     *          시뮬레이션 공간(Local Space면 컴포넌트 기준, 아니면 월드)에 있습니다.
     */
    struct alignas(16) FBaseParticle {
    public:
        FVector mOldLocation{};

        FVector mLocation{};

        /**
         * @brief 매 프레임 mVelocity를 되돌릴 기준 속도입니다(m/s).
         */
        FVector mBaseVelocity{};

        FVector mVelocity{};

        /**
         * @brief 매 프레임 mSize를 되돌릴 기준 크기입니다(m).
         */
        FVector mBaseSize{};

        FVector mSize{};

        FVector4 mColor{};

        FVector4 mBaseColor{};

        /**
         * @brief 수명 진행도입니다. 0에서 시작해 1을 넘으면 파티클이 죽습니다.
         */
        float mRelativeTime{};

        /**
         * @brief 수명(초)의 역수이며 0이면 죽지 않습니다.
         */
        float mOneOverMaxLifetime{};

        /**
         * @brief 화면 평면 안의 회전입니다(rad).
         */
        float mRotation{};

        float mBaseRotationRate{};

        float mRotationRate{};

        /**
         * @brief 상위 비트는 EParticleState 값, 하위 비트는 생성 순번입니다.
         */
        Uint32 mFlags{};

        float mPadding[2]{};
    };

    static_assert(sizeof(Engine::FBaseParticle) % 16 == 0, "FBaseParticle must keep the 16-byte particle stride");

    /**
     * @brief FBaseParticle::mFlags의 상태 비트입니다.
     */
    namespace EParticleState {
        inline constexpr Uint32 JustSpawned{0x02000000u};
        inline constexpr Uint32 Freeze{0x04000000u};
        inline constexpr Uint32 IgnoreCollisions{0x08000000u};
        inline constexpr Uint32 FreezeTranslation{0x10000000u};
        inline constexpr Uint32 FreezeRotation{0x20000000u};
        inline constexpr Uint32 DelayCollisions{0x40000000u};
        inline constexpr Uint32 CollisionHasOccurred{0x80000000u};
        inline constexpr Uint32 Mask{0xFE000000u};
        inline constexpr Uint32 CounterMask{~Mask};
    }

}
