#include "Engine/Particles/Module/UParticleModuleTypeDataMesh.h"
#include "Engine/Particles/FParticleMeshEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleTypeDataMesh::UParticleModuleTypeDataMesh() {
        SetName("Mesh Data");
        mRollPitchYaw.mMode = Engine::EDistributionMode::Uniform;
        mRollPitchYaw.mMinValue = FVector{-180.0f, -180.0f, -180.0f};
        mRollPitchYaw.mMaxValue = FVector{180.0f, 180.0f, 180.0f};
        // 축마다 다른 난수여야 파편처럼 제각각 돕니다.
        mRollPitchYaw.mLockAxes = false;
        BakeDistributions();
    }

    Engine::UParticleModuleTypeDataMesh::~UParticleModuleTypeDataMesh() = default;

    std::unique_ptr<Engine::FParticleEmitterInstance> Engine::UParticleModuleTypeDataMesh::CreateInstance(const Engine::UParticleEmitter& Emitter, Engine::UParticleSystemComponent& Component) const {
        auto Instance{std::make_unique<Engine::FParticleMeshEmitterInstance>()};

        Instance->Init(Emitter, Component);

        return Instance;
    }

    void Engine::UParticleModuleTypeDataMesh::BakeDistributions() {
        mRollPitchYaw.Bake();
    }

    const Core::FString& Engine::UParticleModuleTypeDataMesh::GetMeshPath() const {
        return mMeshPath;
    }

    void Engine::UParticleModuleTypeDataMesh::SetMeshPath(const Core::FString& Path) {
        mMeshPath = Path;
    }

    bool Engine::UParticleModuleTypeDataMesh::IsOverrideMaterial() const {
        return mOverrideMaterial;
    }

    void Engine::UParticleModuleTypeDataMesh::SetOverrideMaterial(bool OverrideMaterial) {
        mOverrideMaterial = OverrideMaterial;
    }

    const Engine::FVectorDistribution& Engine::UParticleModuleTypeDataMesh::GetRollPitchYaw() const {
        return mRollPitchYaw;
    }

    Engine::FVectorDistribution& Engine::UParticleModuleTypeDataMesh::GetRollPitchYaw() {
        return mRollPitchYaw;
    }

}
