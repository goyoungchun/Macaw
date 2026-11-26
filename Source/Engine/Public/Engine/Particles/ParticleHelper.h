/**
 * @file ParticleHelper.h
 * @brief 모듈이 살아 있는 파티클을 순회하고 Payload에 접근하는 보조 함수입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

#include <cassert>

namespace Engine::ParticleHelper {

    /**
     * @brief Freeze 상태가 아닌 살아 있는 파티클마다 Function(Particle, ParticleBase)를 부릅니다.
     * @details ParticleBase는 이 파티클 칸의 시작 주소이며 Payload 접근에 씁니다.
     */
    template <typename FFunction>
    void ForEachParticle(Engine::FParticleEmitterInstance& Owner, FFunction&& Function) {
        Uint8* const Data{Owner.mParticleData};
        const std::uint16_t* const Indices{Owner.mParticleIndices};
        const Index_t Stride{static_cast<Index_t>(Owner.mParticleStride)};

        for (Int32 Index{Owner.mActiveParticles - 1}; Index >= 0; --Index) {
            Uint8* const Base{Data + Indices[Index] * Stride};
            Engine::FBaseParticle& Particle{*reinterpret_cast<Engine::FBaseParticle*>(Base)};

            if ((Particle.mFlags & Engine::EParticleState::Freeze) != 0) {
                continue;
            }

            Function(Particle, Base);
        }
    }

    /**
     * @brief 칸 시작 주소에서 Offset 위치의 Payload입니다.
     * @param Offset 모듈 Payload의 시작 위치이며 Payload가 있는 모듈이어야 합니다.
     */
    template <typename T>
    T& GetPayload(Uint8* ParticleBase, Int32 Offset) {
        assert(Offset >= static_cast<Int32>(sizeof(Engine::FBaseParticle)) && "GetPayload called on a module without payload");

        return *reinterpret_cast<T*>(ParticleBase + Offset);
    }

    template <typename T>
    T& GetPayload(Engine::FBaseParticle& Particle, Int32 Offset) {
        return GetPayload<T>(reinterpret_cast<Uint8*>(&Particle), Offset);
    }

    template <typename T>
    const T& GetPayload(const Uint8* ParticleBase, Int32 Offset) {
        assert(Offset >= static_cast<Int32>(sizeof(Engine::FBaseParticle)) && "GetPayload called on a module without payload");

        return *reinterpret_cast<const T*>(ParticleBase + Offset);
    }

    template <typename T>
    const T& GetPayload(const Engine::FBaseParticle& Particle, Int32 Offset) {
        return GetPayload<T>(reinterpret_cast<const Uint8*>(&Particle), Offset);
    }

}
