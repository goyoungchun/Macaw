/**
 * @file FParticleMeshEmitterInstance.h
 * @brief 파티클마다 메시를 회전시켜 그리는 메시 에미터 인스턴스입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    class UParticleModuleTypeDataMesh;

    /**
     * @brief 메시 에미터가 파티클마다 붙이는 회전 상태입니다. 단위는 모두 도입니다.
     */
    struct FMeshRotationPayloadData {
    public:
        /**
         * @brief 생성할 때 정한 방향입니다. X = Roll, Y = Pitch, Z = Yaw입니다.
         */
        FVector mInitialOrientation{};

        /**
         * @brief 생성 뒤 누적한 회전입니다.
         */
        FVector mRotation{};

        /**
         * @brief 이번 프레임 회전 속도(도/초)이며 모듈이 바꿀 수 있습니다.
         */
        FVector mRotationRate{};

        /**
         * @brief 매 프레임 mRotationRate를 되돌릴 기준 속도입니다.
         */
        FVector mRotationRateBase{};
    };

    /**
     * @brief TypeDataMesh가 만드는 인스턴스입니다. 회전 상태를 인스턴스 Payload에 둡니다.
     */
    class FParticleMeshEmitterInstance final : public Engine::FParticleEmitterInstance {
    public:
        void Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component) override;
        void SetCurrentLODIndex(Int32 LODIndex) override;

        void FillRenderData(Engine::FParticleRenderData& Out) const override;

        Engine::FMeshRotationPayloadData& GetMeshRotationPayload(Engine::FBaseParticle& Particle) const;

        const Engine::FMeshRotationPayloadData& GetMeshRotationPayload(const Engine::FBaseParticle& Particle) const;

    protected:
        Int32 GetInstancePayloadSize() const override;

        void ResetParticleParameters(float DeltaTime) override;

        void PostSpawn(Engine::FBaseParticle& Particle, float SpawnTime) override;

        void UpdateBoundingBox(float DeltaTime) override;

        float GetParticleRadius(const Engine::FBaseParticle& Particle) const override;

    private:
        const Engine::UParticleModuleTypeDataMesh* mMeshTypeData{};

        /**
         * @brief 메시 원점에서 가장 먼 꼭짓점까지의 거리(m)이며 경계를 갱신할 때마다 다시 구합니다.
         */
        float mMeshRadius{};
    };

}
