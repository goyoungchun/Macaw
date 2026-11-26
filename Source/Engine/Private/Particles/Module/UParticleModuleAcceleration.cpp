#include "Engine/Particles/Module/UParticleModuleAcceleration.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/ParticleHelper.h"

namespace Engine {

    Engine::UParticleModuleAcceleration::UParticleModuleAcceleration() {
        SetName("Acceleration");
        mSpawnModule = true;
        mUpdateModule = true;
        // 기본값은 중력입니다(Z가 위).
        mAcceleration.mConstant = FVector{0.0f, 0.0f, -9.81f};

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleAcceleration::~UParticleModuleAcceleration() = default;

    void Engine::UParticleModuleAcceleration::Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const {
        const FVector Value{mAcceleration.GetValue(Owner.mEmitterTime, Owner.mRandomStream)};
        const FVector Acceleration{mAlwaysInWorldSpace ? Owner.WorldToSimulationVector(Value) : Owner.EmitterToSimulationVector(Value)};

        Engine::ParticleHelper::GetPayload<FVector>(Particle, Offset) = Acceleration;

        // 프레임 안에서 먼저 태어난 파티클은 PostSpawn에서 SpawnTime만큼 전진하므로 그만큼 가속합니다.
        Particle.mVelocity += Acceleration * SpawnTime;
        Particle.mBaseVelocity += Acceleration * SpawnTime;
    }

    void Engine::UParticleModuleAcceleration::Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const {
        Engine::ParticleHelper::ForEachParticle(Owner, [Offset, DeltaTime](Engine::FBaseParticle& Particle, Uint8* Base) {
            const FVector& Acceleration{Engine::ParticleHelper::GetPayload<FVector>(Base, Offset)};

            // 가속은 누적되는 운동 상태라 기준 속도에도 더합니다.
            Particle.mVelocity += Acceleration * DeltaTime;
            Particle.mBaseVelocity += Acceleration * DeltaTime;
        });
    }

    void Engine::UParticleModuleAcceleration::BakeDistributions() {
        mAcceleration.Bake();
    }

    Uint32 Engine::UParticleModuleAcceleration::RequiredBytes() const {
        return sizeof(FVector);
    }

    Engine::FVectorDistribution& Engine::UParticleModuleAcceleration::GetAcceleration() {
        return mAcceleration;
    }

}
