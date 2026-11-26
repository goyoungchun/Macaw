/**
 * @file FParticleBeamEmitterInstance.h
 * @brief 파티클 하나를 에미터에서 끝점까지 이어진 빔으로 그리는 인스턴스입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    class UParticleModuleBeamNoise;
    class UParticleModuleTypeDataBeam;

    /**
     * @brief 빔 파티클마다 붙는 양 끝점입니다. 월드 공간(m)입니다.
     */
    struct FBeamPayloadData {
    public:
        FVector mSourcePoint{};

        FVector mTargetPoint{};
    };

    /**
     * @brief TypeDataBeam이 만드는 인스턴스입니다. 끝점은 경계를 갱신할 때마다 컴포넌트 위치로 다시 구합니다.
     */
    class FParticleBeamEmitterInstance final : public Engine::FParticleEmitterInstance {
    public:
        void Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component) override;
        void SetCurrentLODIndex(Int32 LODIndex) override;

        void FillRenderData(Engine::FParticleRenderData& Out) const override;

        Engine::FBeamPayloadData& GetBeamPayload(Engine::FBaseParticle& Particle) const;

        const Engine::FBeamPayloadData& GetBeamPayload(const Engine::FBaseParticle& Particle) const;

    protected:
        Int32 GetInstancePayloadSize() const override;

        /**
         * @brief 초당 생성과 Burst가 같은 프레임에 겹쳐도 최대 빔 수를 넘지 않게 자릅니다.
         */
        void SpawnParticles(Int32 Count, float StartTime, float Increment) override;

        void UpdateBoundingBox(float DeltaTime) override;

    private:
        void ResolveBeamPoints(Engine::FBeamPayloadData& Payload) const;

        /**
         * @brief 현재 LOD에서 켜진 노이즈 모듈입니다. 없으면 nullptr입니다.
         */
        const Engine::UParticleModuleBeamNoise* FindNoiseModule() const;

    private:
        const Engine::UParticleModuleTypeDataBeam* mBeamTypeData{};
    };

}
