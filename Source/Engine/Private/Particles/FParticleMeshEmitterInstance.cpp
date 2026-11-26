#include "Engine/Particles/FParticleMeshEmitterInstance.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleRenderData.h"
#include "Engine/Particles/ParticleHelper.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataMesh.h"
#include "Engine/Component/UParticleSystemComponent.h"
#include "Asset/UMaterial.h"
#include "Asset/UStaticMesh.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FQuat.h"
#include "Core/Math/FRotator.h"
#include "Core/Math/FTransform.h"
#include "CoreUObject/Reflection/Cast.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Engine {

    namespace {

        // 오일러 각(도)은 X = Roll, Y = Pitch, Z = Yaw이고 FRotator는 (Pitch, Yaw, Roll) 순서입니다.
        FQuat EulerToQuat(const FVector& Euler) {
            return FRotator{Euler.y, Euler.z, Euler.x}.Quaternion();
        }

    }

    void Engine::FParticleMeshEmitterInstance::Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component) {
        // 부모가 인스턴스 Payload까지 포함한 칸 크기와 현재 LOD를 정합니다.
        Engine::FParticleEmitterInstance::Init(Template, Component);
        mMeshTypeData = CoreUObject::Cast<Engine::UParticleModuleTypeDataMesh>(GetCurrentTypeData());
    }

    void Engine::FParticleMeshEmitterInstance::SetCurrentLODIndex(Int32 LODIndex) {
        Engine::FParticleEmitterInstance::SetCurrentLODIndex(LODIndex);
        mMeshTypeData = CoreUObject::Cast<Engine::UParticleModuleTypeDataMesh>(GetCurrentTypeData());
    }

    void Engine::FParticleMeshEmitterInstance::FillRenderData(Engine::FParticleRenderData& Out) const {
        Asset::UStaticMesh* Mesh{mMeshTypeData != nullptr && mComponent != nullptr ? mComponent->ResolveMesh(mMeshTypeData->GetMeshPath()) : nullptr};

        if (Mesh == nullptr || mCurrentLODLevel == nullptr) {
            return;
        }

        Engine::FParticleMeshBatch& Batch{Out.mMeshes.emplace_back()};
        const auto& Slots{Mesh->GetMeshData().GetMaterialSlots()};
        Asset::UMaterial* Override{mMeshTypeData->IsOverrideMaterial() ? mComponent->ResolveMaterial(mCurrentLODLevel->GetRequiredModule()->GetMaterialPath()) : nullptr};

        Batch.mMesh = Mesh;
        Batch.mMaterials.reserve(Slots.size());

        for (const auto& Slot : Slots) {
            Batch.mMaterials.emplace_back(Override != nullptr ? Override : mComponent->ResolveMaterial(Slot.GetMaterialPath()));
        }

        const bool LocalSpace{IsLocalSpace()};
        const FMatrix ComponentToWorld{mComponent->GetWorldMatrix()};
        // Local Space는 컴포넌트 행렬에 스케일이 있고, World Space는 위치를 빼고 크기에만 곱합니다.
        const FVector WorldScale{GetComponentScale()};
        const FBox& MeshBounds{Mesh->GetBounds()};
        constexpr float RadiansToDegrees{180.0f / std::numbers::pi_v<float>};

        Batch.mInstances.reserve(static_cast<Index_t>(mActiveParticles));

        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            const Engine::FBaseParticle& Particle{*GetParticle(mParticleIndices[Index])};
            const Engine::FMeshRotationPayloadData& Payload{GetMeshRotationPayload(Particle)};
            // 누적 회전과 스프라이트 회전(rad, 세 축 공통)은 오일러 각끼리 더합니다.
            const FVector Spin{Payload.mRotation + FVector{1.0f, 1.0f, 1.0f} * (Particle.mRotation * RadiansToDegrees)};
            const FVector Size{LocalSpace ? Particle.mSize : Particle.mSize * WorldScale};
            FMatrix World{FMatrix::FromTransform(FTransform{Particle.mLocation, EulerToQuat(Spin), Size})};

            // 초기 방향은 각도에 더하지 않고 별도 회전으로 맨 앞에 곱합니다. 오일러 각 덧셈은 한 축 회전일 때만 맞습니다.
            if (Payload.mInitialOrientation.x != 0.0f || Payload.mInitialOrientation.y != 0.0f || Payload.mInitialOrientation.z != 0.0f) {
                World = FMatrix::FromTransform(FTransform{{}, EulerToQuat(Payload.mInitialOrientation), {1.0f, 1.0f, 1.0f}}) * World;
            }

            if (LocalSpace) {
                World = World * ComponentToWorld;
            }

            // 크기가 0인 파티클은 역행렬이 없어 렌더러가 다룰 수 없으므로 뺍니다.
            FMatrix Inverse{};

            if (World.TryInverse(Inverse)) {
                Batch.mInstances.push_back({World, Inverse, MeshBounds.Transformed(World), Particle.mColor});
            }
        }
    }

    Engine::FMeshRotationPayloadData& Engine::FParticleMeshEmitterInstance::GetMeshRotationPayload(Engine::FBaseParticle& Particle) const {
        return Engine::ParticleHelper::GetPayload<Engine::FMeshRotationPayloadData>(Particle, mInstancePayloadOffset);
    }

    const Engine::FMeshRotationPayloadData& Engine::FParticleMeshEmitterInstance::GetMeshRotationPayload(const Engine::FBaseParticle& Particle) const {
        return Engine::ParticleHelper::GetPayload<Engine::FMeshRotationPayloadData>(Particle, mInstancePayloadOffset);
    }

    Int32 Engine::FParticleMeshEmitterInstance::GetInstancePayloadSize() const {
        return static_cast<Int32>(sizeof(Engine::FMeshRotationPayloadData));
    }

    void Engine::FParticleMeshEmitterInstance::ResetParticleParameters(float DeltaTime) {
        Engine::FParticleEmitterInstance::ResetParticleParameters(DeltaTime);

        Engine::ParticleHelper::ForEachParticle(*this, [this](Engine::FBaseParticle& Particle, Uint8*) {
            Engine::FMeshRotationPayloadData& Payload{GetMeshRotationPayload(Particle)};

            Payload.mRotationRate = Payload.mRotationRateBase;
        });
    }

    void Engine::FParticleMeshEmitterInstance::PostSpawn(Engine::FBaseParticle& Particle, float SpawnTime) {
        Engine::FParticleEmitterInstance::PostSpawn(Particle, SpawnTime);

        // 회전 속도는 PreSpawn이 0으로 지운 뒤 회전 속도 모듈이 넣었을 수 있으므로 건드리지 않습니다.
        if (mMeshTypeData != nullptr) {
            GetMeshRotationPayload(Particle).mInitialOrientation = mMeshTypeData->GetRollPitchYaw().GetValue(mEmitterTime, mRandomStream);
        }
    }

    void Engine::FParticleMeshEmitterInstance::UpdateBoundingBox(float DeltaTime) {
        // 부모가 JustSpawned 플래그를 지우므로 회전 적분을 먼저 합니다.
        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            Engine::FBaseParticle& Particle{*GetParticle(mParticleIndices[Index])};

            // 위치와 같은 규칙으로 이번 프레임에 생성됐거나 Freeze 계열이면 돌지 않습니다.
            if ((Particle.mFlags & (Engine::EParticleState::JustSpawned | Engine::EParticleState::Freeze | Engine::EParticleState::FreezeRotation)) != 0) {
                continue;
            }

            Engine::FMeshRotationPayloadData& Payload{GetMeshRotationPayload(Particle)};

            Payload.mRotation += Payload.mRotationRate * DeltaTime;
        }

        mMeshRadius = 0.0f;

        if (const Asset::UStaticMesh* Mesh{mMeshTypeData != nullptr && mComponent != nullptr ? mComponent->ResolveMesh(mMeshTypeData->GetMeshPath()) : nullptr}) {
            const FBox& Bounds{Mesh->GetBounds()};

            mMeshRadius = Bounds.mMinimum.Abs().Max(Bounds.mMaximum.Abs()).Length();
        }

        Engine::FParticleEmitterInstance::UpdateBoundingBox(DeltaTime);
    }

    float Engine::FParticleMeshEmitterInstance::GetParticleRadius(const Engine::FBaseParticle& Particle) const {
        const FVector Size{Particle.mSize.Abs()};

        return mMeshRadius * std::max({Size.x, Size.y, Size.z});
    }

}
