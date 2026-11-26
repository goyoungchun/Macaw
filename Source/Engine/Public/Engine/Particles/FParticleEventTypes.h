/**
 * @file FParticleEventTypes.h
 * @brief 파티클 에미터가 내보내고 받는 이벤트의 종류와 내용입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/FName.h"
#include "Core/Math/FVector.h"
#include "CoreUObject/TObjectRef.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "FParticleEventTypes.generated.h"

namespace Engine {

    class AActor;
    class UPrimitiveComponent;

    /**
     * @brief 파티클 이벤트 종류입니다.
     */
    UENUM()
    enum class EParticleEventType : Uint8 {
        /**
         * @brief Receiver가 종류를 가리지 않을 때 씁니다.
         */
        Any,
        Spawn,
        /**
         * @brief 수명이 끝났습니다.
         */
        Death,
        Collision,
        /**
         * @brief Burst가 터졌습니다(에미터 단위).
         */
        Burst
    };

    /**
     * @brief 모든 이벤트의 공통 부분입니다. 위치·속도·방향은 월드 공간입니다.
     * @details 파티클 칸은 지운 뒤 다른 파티클이 다시 쓰므로 값으로 복사해 둡니다.
     */
    struct FParticleEventData {
    public:
        Engine::EParticleEventType mType{Engine::EParticleEventType::Any};

        /**
         * @brief Generator가 붙인 이름이며 Receiver가 이 이름으로 골라 받습니다.
         */
        Core::FName mEventName{};

        /**
         * @brief 이벤트를 낸 에미터 번호(컴포넌트의 인스턴스 순서)입니다.
         */
        Int32 mEmitterIndex{-1};

        float mEmitterTime{};

        FVector mLocation{};

        FVector mVelocity{};

        FVector mDirection{};

        /**
         * @brief Generator 설정에서 그대로 실어 보내는 값입니다.
         */
        Int32 mCustomData{};
    };

    struct FParticleEventSpawnData : Engine::FParticleEventData {
    };

    struct FParticleEventDeathData : Engine::FParticleEventData {
    public:
        /**
         * @brief 죽은 파티클의 나이(초)입니다.
         */
        float mParticleTime{};
    };

    struct FParticleEventCollisionData : Engine::FParticleEventData {
    public:
        FVector mNormal{};

        /**
         * @brief 이번 프레임 이동 경로에서의 충돌 위치(0~1)입니다.
         */
        float mTime{};

        /**
         * @brief 충돌한 파티클의 나이(초)입니다.
         */
        float mParticleTime{};

        /**
         * @brief 닿은 컴포넌트이며 파괴되면 비게 됩니다.
         */
        CoreUObject::TObjectRef<Engine::UPrimitiveComponent> mHitComponent{};

        CoreUObject::TObjectRef<Engine::AActor> mHitActor{};
    };

    struct FParticleEventBurstData : Engine::FParticleEventData {
    public:
        Int32 mParticleCount{};
    };

}
