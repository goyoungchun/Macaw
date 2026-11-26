#include "Engine/Particles/FParticleBeamEmitterInstance.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleRenderData.h"
#include "Engine/Particles/ParticleHelper.h"
#include "Engine/Particles/UParticleEmitter.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/Module/UParticleModuleBeamNoise.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBeam.h"
#include "Engine/Component/UParticleSystemComponent.h"
#include "Asset/UMaterial.h"
#include "Core/Math/FMatrix.h"
#include "CoreUObject/Reflection/Cast.h"

#include "ParticleCurveMath.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Engine {

    namespace {

        /**
         * @brief 노이즈 점을 빔 축에 수직인 평면으로 펼쳐 경로에 붙입니다.
         */
        void AppendNoisePath(const FVector& Source, const FVector& Target, const Engine::UParticleModuleBeamNoise& Noise, const Engine::FBeamNoisePoint* Points, Core::TArray<FVector>& Path) {
            const Uint32 Frequency{Noise.GetFrequency()};
            const FVector Axis{Target - Source};
            const FVector Direction{Axis.Normalized()};

            // 오프셋을 펼칠 축은 카메라와 무관해야 카메라를 돌려도 번개 모양이 그대로입니다.
            FVector Right{Direction.Cross({0.0f, 0.0f, 1.0f})};

            if (Right.Length() < 0.0001f) {
                // 빔이 Z축과 평행할 때입니다.
                Right = Direction.Cross({0.0f, 1.0f, 0.0f});
            }

            Right = Right.Normalized();

            const FVector Up{Direction.Cross(Right)};
            Core::TArray<FVector> Noisy{};

            Noisy.reserve(Frequency + 1);

            for (Uint32 Index{}; Index <= Frequency; ++Index) {
                const FVector Base{Source + Axis * (static_cast<float>(Index) / static_cast<float>(Frequency))};

                Noisy.push_back(Base + Right * Points[Index].mCurrent[0] + Up * Points[Index].mCurrent[1]);
            }

            if (!Noise.IsSmooth()) {
                Path.insert(Path.end(), Noisy.begin(), Noisy.end());

                return;
            }

            // Catmull-Rom은 이웃 점으로 접선을 정하는 Hermite라 노이즈 점을 반드시 지납니다.
            const Uint32 Tessellation{Noise.GetNoiseTessellation()};

            for (Uint32 Index{}; Index < Frequency; ++Index) {
                const FVector& P0{Noisy[Index > 0 ? Index - 1 : 0]};
                const FVector& P1{Noisy[Index]};
                const FVector& P2{Noisy[Index + 1]};
                const FVector& P3{Noisy[std::min(Index + 2, Frequency)]};
                const FVector T1{(P2 - P0) * 0.5f};
                const FVector T2{(P3 - P1) * 0.5f};

                for (Uint32 Step{}; Step < Tessellation; ++Step) {
                    Path.push_back(Engine::ParticleCurveMath::CubicInterp(P1, T1, P2, T2, static_cast<float>(Step) / static_cast<float>(Tessellation)));
                }
            }

            Path.push_back(Noisy[Frequency]);
        }

    }

    void Engine::FParticleBeamEmitterInstance::Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component) {
        Engine::FParticleEmitterInstance::Init(Template, Component);
        mBeamTypeData = CoreUObject::Cast<Engine::UParticleModuleTypeDataBeam>(GetCurrentTypeData());
    }

    void Engine::FParticleBeamEmitterInstance::SetCurrentLODIndex(Int32 LODIndex) {
        Engine::FParticleEmitterInstance::SetCurrentLODIndex(LODIndex);
        mBeamTypeData = CoreUObject::Cast<Engine::UParticleModuleTypeDataBeam>(GetCurrentTypeData());
    }

    void Engine::FParticleBeamEmitterInstance::FillRenderData(Engine::FParticleRenderData& Out) const {
        if (mBeamTypeData == nullptr || mComponent == nullptr || mCurrentLODLevel == nullptr) {
            return;
        }

        const Engine::UParticleModuleRequired& Required{*mCurrentLODLevel->GetRequiredModule()};
        const Engine::UParticleModuleBeamNoise* Noise{FindNoiseModule()};
        const Int32 NoiseOffset{Noise != nullptr ? mTemplate->GetModuleOffset(Noise) : 0};
        const Uint32 Segments{mBeamTypeData->GetSegments()};
        const float Scale{GetComponentScale().x};
        Engine::FParticleTrailBatch& Batch{Out.mTrails.emplace_back()};
        Core::TArray<FVector> Path{};

        Batch.mMaterial = mComponent->ResolveMaterial(Required.GetMaterialPath());
        Batch.mSortMode = Required.GetSortMode();
        Batch.mSheets = mBeamTypeData->GetSheets();

        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            const Uint8* Base{mParticleData + static_cast<Index_t>(mParticleIndices[Index]) * static_cast<Index_t>(mParticleStride)};
            const Engine::FBaseParticle& Particle{*reinterpret_cast<const Engine::FBaseParticle*>(Base)};
            const Engine::FBeamPayloadData& Payload{GetBeamPayload(Particle)};
            const FVector Axis{Payload.mTargetPoint - Payload.mSourcePoint};

            if (Axis.Length() < 0.0001f) {
                continue;
            }

            Path.clear();

            if (Noise != nullptr && NoiseOffset > 0) {
                AppendNoisePath(Payload.mSourcePoint, Payload.mTargetPoint, *Noise, Engine::UParticleModuleBeamNoise::GetPoints(Base, NoiseOffset), Path);
            } else {
                for (Uint32 Segment{}; Segment <= Segments; ++Segment) {
                    Path.push_back(Payload.mSourcePoint + Axis * (static_cast<float>(Segment) / static_cast<float>(Segments)));
                }
            }

            const float HalfWidth{std::abs(Particle.mSize.x * Scale) * 0.5f};
            const float LastIndex{static_cast<float>(std::max<Index_t>(Path.size() - 1, 1))};

            for (Index_t Point{}; Point < Path.size(); ++Point) {
                Batch.mPoints.push_back({Path[Point], Particle.mColor, HalfWidth, static_cast<float>(Point) / LastIndex * mBeamTypeData->GetTextureTile()});
            }

            Batch.mStripEnds.push_back(static_cast<Uint32>(Batch.mPoints.size()));
        }
    }

    Engine::FBeamPayloadData& Engine::FParticleBeamEmitterInstance::GetBeamPayload(Engine::FBaseParticle& Particle) const {
        return Engine::ParticleHelper::GetPayload<Engine::FBeamPayloadData>(Particle, mInstancePayloadOffset);
    }

    const Engine::FBeamPayloadData& Engine::FParticleBeamEmitterInstance::GetBeamPayload(const Engine::FBaseParticle& Particle) const {
        return Engine::ParticleHelper::GetPayload<Engine::FBeamPayloadData>(Particle, mInstancePayloadOffset);
    }

    Int32 Engine::FParticleBeamEmitterInstance::GetInstancePayloadSize() const {
        return static_cast<Int32>(sizeof(Engine::FBeamPayloadData));
    }

    void Engine::FParticleBeamEmitterInstance::SpawnParticles(Int32 Count, float StartTime, float Increment) {
        if (mBeamTypeData == nullptr) {
            return;
        }

        Count = std::min(Count, static_cast<Int32>(mBeamTypeData->GetMaxBeamCount()) - mActiveParticles);

        if (Count > 0) {
            Engine::FParticleEmitterInstance::SpawnParticles(Count, StartTime, Increment);
        }
    }

    void Engine::FParticleBeamEmitterInstance::UpdateBoundingBox(float DeltaTime) {
        Engine::FParticleEmitterInstance::UpdateBoundingBox(DeltaTime);

        const FVector Scale{GetComponentScale().Abs()};
        const float MaximumScale{std::max({Scale.x, Scale.y, Scale.z})};
        // 노이즈 점은 축에서 (Right, Up) 두 방향으로 최대 NoiseRange씩 벗어납니다.
        const Engine::UParticleModuleBeamNoise* Noise{FindNoiseModule()};
        const float NoiseExtent{Noise != nullptr ? Noise->GetNoiseRange().GetBase() * 1.4143f : 0.0f};
        FVector Minimum{FLT_MAX, FLT_MAX, FLT_MAX};
        FVector Maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};

        // 부모 상자는 파티클 위치만 감싸므로 시작점부터 끝점까지로 다시 만듭니다.
        Engine::ParticleHelper::ForEachParticle(*this, [&](Engine::FBaseParticle& Particle, Uint8*) {
            Engine::FBeamPayloadData& Payload{GetBeamPayload(Particle)};

            ResolveBeamPoints(Payload);

            const float Radius{std::abs(Particle.mSize.x) * MaximumScale * 0.5f + NoiseExtent};
            const FVector Extent{Radius, Radius, Radius};

            Minimum = Minimum.Min(Payload.mSourcePoint - Extent).Min(Payload.mTargetPoint - Extent);
            Maximum = Maximum.Max(Payload.mSourcePoint + Extent).Max(Payload.mTargetPoint + Extent);
        });

        mBoundingBox = FBox{Minimum, Maximum};
    }

    void Engine::FParticleBeamEmitterInstance::ResolveBeamPoints(Engine::FBeamPayloadData& Payload) const {
        if (mBeamTypeData == nullptr || mComponent == nullptr) {
            return;
        }

        // Payload는 월드 공간이라 Local Space에서 원점을 돌려주는 시뮬레이션 공간 함수는 쓰지 않습니다.
        const FMatrix World{mComponent->GetWorldMatrix()};

        Payload.mSourcePoint = World.TransformPosition({});

        if (mBeamTypeData->GetBeamMethod() == Engine::EBeamMethod::Distance) {
            Payload.mTargetPoint = Payload.mSourcePoint + World.TransformVector({1.0f, 0.0f, 0.0f}).Normalized() * mBeamTypeData->GetDistance().GetBase();
        } else {
            // 에미터 기준 위치라 액터를 옮기거나 돌려도 시작점과의 관계가 유지됩니다.
            Payload.mTargetPoint = World.TransformPosition(mBeamTypeData->GetTargetPoint());
        }
    }

    const Engine::UParticleModuleBeamNoise* Engine::FParticleBeamEmitterInstance::FindNoiseModule() const {
        if (mCurrentLODLevel == nullptr) {
            return nullptr;
        }

        for (const auto& Module : mCurrentLODLevel->GetModules()) {
            if (const auto* Noise{CoreUObject::Cast<Engine::UParticleModuleBeamNoise>(Module.get())}; Noise != nullptr && Noise->IsEnabled()) {
                return Noise;
            }
        }

        return nullptr;
    }

}
