#include "Engine/Particles/FParticleRibbonEmitterInstance.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleRenderData.h"
#include "Engine/Particles/ParticleHelper.h"
#include "Engine/Particles/UParticleEmitter.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataRibbon.h"
#include "Engine/Component/UParticleSystemComponent.h"
#include "Asset/UMaterial.h"
#include "Core/Math/FMatrix.h"
#include "CoreUObject/Reflection/Cast.h"

#include "Core/Literals/UnitLiterals.h"
#include "ParticleCurveMath.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    namespace {

        /**
         * @brief 겹친 점을 넘기면 접선이 0이 되어 띠가 뒤틀리므로 이보다 가까운 점은 뺍니다.
         */
        constexpr Core::FLength MinimumPointDistance{1_mm};

        /**
         * @brief 점 사이를 Catmull-Rom 곡선 점으로 채웁니다.
         */
        void Tessellate(const Core::TArray<Engine::FParticleTrailPoint>& Points, Core::FLength StepSize, Uint32 MaxSteps, Core::TArray<Engine::FParticleTrailPoint>& Out) {
            const Index_t Count{Points.size()};

            for (Index_t Index{}; Index + 1 < Count; ++Index) {
                const Engine::FParticleTrailPoint& A{Points[Index]};
                const Engine::FParticleTrailPoint& B{Points[Index + 1]};

                Out.push_back(A);

                const Core::FLength SegmentLength{Core::FLength::FromBase(A.mPosition.Distance(B.mPosition))};
                const Uint32 Extra{std::min(static_cast<Uint32>(SegmentLength / StepSize), MaxSteps)};

                if (Extra == 0) {
                    continue;
                }

                const FVector& P0{Points[Index > 0 ? Index - 1 : 0].mPosition};
                const FVector& P3{Points[std::min(Index + 2, Count - 1)].mPosition};
                const FVector T1{(B.mPosition - P0) * 0.5f};
                const FVector T2{(P3 - A.mPosition) * 0.5f};

                for (Uint32 Step{1}; Step <= Extra; ++Step) {
                    const float Alpha{static_cast<float>(Step) / static_cast<float>(Extra + 1)};
                    Engine::FParticleTrailPoint Point{};

                    Point.mPosition = Engine::ParticleCurveMath::CubicInterp(A.mPosition, T1, B.mPosition, T2, Alpha);
                    Point.mHalfWidth = A.mHalfWidth + (B.mHalfWidth - A.mHalfWidth) * Alpha;
                    Point.mColor = A.mColor + (B.mColor - A.mColor) * Alpha;
                    Point.mU = A.mU + (B.mU - A.mU) * Alpha;
                    Out.push_back(Point);
                }
            }

            Out.push_back(Points.back());
        }

    }

    void Engine::FParticleRibbonEmitterInstance::Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component) {
        Engine::FParticleEmitterInstance::Init(Template, Component);
        mRibbonTypeData = CoreUObject::Cast<Engine::UParticleModuleTypeDataRibbon>(GetCurrentTypeData());
        mHasLastSource = false;
        mTrailHasPoints = false;
        mTrailId = 0;
        mTiledDistance = {};
        mDistanceAccumulated = {};
    }

    void Engine::FParticleRibbonEmitterInstance::SetCurrentLODIndex(Int32 LODIndex) {
        Engine::FParticleEmitterInstance::SetCurrentLODIndex(LODIndex);
        mRibbonTypeData = CoreUObject::Cast<Engine::UParticleModuleTypeDataRibbon>(GetCurrentTypeData());
    }

    void Engine::FParticleRibbonEmitterInstance::FillRenderData(Engine::FParticleRenderData& Out) const {
        if (mRibbonTypeData == nullptr || mComponent == nullptr || mCurrentLODLevel == nullptr) {
            return;
        }

        const Engine::UParticleModuleRequired& Required{*mCurrentLODLevel->GetRequiredModule()};
        const float Scale{GetComponentScale().x};
        const Core::FLength TilingDistance{mRibbonTypeData->GetTilingDistance()};
        const bool Tiling{TilingDistance > 0_m};
        const Core::FLength StepSize{mRibbonTypeData->GetDistanceTessellationStepSize()};
        const Uint32 MaxSteps{mRibbonTypeData->GetMaxTessellationBetweenParticles()};
        const FVector HeadPosition{mComponent->GetWorldMatrix().TransformPosition({})};
        const bool HeadValid{mRibbonTypeData->IsRenderHead() && mHasLastSource && mTrailHasPoints};
        Engine::FParticleTrailBatch& Batch{Out.mTrails.emplace_back()};

        Batch.mMaterial = mComponent->ResolveMaterial(Required.GetMaterialPath());
        // 트레일 순서는 점 연결이 정하므로 띠 단위 정렬은 하지 않습니다.
        Batch.mSortMode = Engine::EParticleSortMode::None;
        Batch.mSheets = mRibbonTypeData->GetSheets();

        struct FEntry {
            Uint32 mTrailId;
            Uint32 mCounter;
            const Engine::FBaseParticle* mParticle;
        };

        Core::TArray<FEntry> Entries{};

        Entries.reserve(static_cast<Index_t>(mActiveParticles));

        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            const Engine::FBaseParticle* Particle{GetParticle(mParticleIndices[Index])};

            Entries.push_back({GetRibbonPayload(*Particle).mTrailId, Particle->mFlags & Engine::EParticleState::CounterMask, Particle});
        }

        // 같은 트레일끼리 모으고, 트레일 안에서는 새 점부터 둡니다.
        std::ranges::sort(Entries, [](const FEntry& A, const FEntry& B) {
            return A.mTrailId != B.mTrailId ? A.mTrailId < B.mTrailId : A.mCounter > B.mCounter;
        });

        Core::TArray<Engine::FParticleTrailPoint> Points{};

        for (Index_t Begin{}; Begin < Entries.size();) {
            const Uint32 TrailId{Entries[Begin].mTrailId};
            Index_t End{Begin};

            while (End < Entries.size() && Entries[End].mTrailId == TrailId) {
                ++End;
            }

            Points.clear();

            const Engine::FBaseParticle& Newest{*Entries[Begin].mParticle};

            if (HeadValid && TrailId == mTrailId) {
                const FVector NewestPosition{SimulationToWorld(Newest.mLocation)};

                if (Core::FLength::FromBase((HeadPosition - NewestPosition).Length()) > MinimumPointDistance) {
                    // 가장 새 점의 U에 거기서 머리까지 거리를 더해야 머리 구간 텍스처가 이어집니다.
                    const Core::FLength HeadDistance{Core::FLength::FromBase(HeadPosition.Distance(SimulationToWorld(mLastSpawnPosition)))};
                    const float HeadU{Tiling ? static_cast<float>((mTiledDistance + HeadDistance) / TilingDistance) : 0.0f};

                    Points.push_back({HeadPosition, Newest.mColor, std::abs(Newest.mSize.x * Scale) * 0.5f, HeadU});
                }
            }

            for (Index_t Index{Begin}; Index < End; ++Index) {
                const Engine::FBaseParticle& Particle{*Entries[Index].mParticle};
                const FVector Position{SimulationToWorld(Particle.mLocation)};

                if (!Points.empty() && Core::FLength::FromBase((Position - Points.back().mPosition).Length()) < MinimumPointDistance) {
                    continue;
                }

                Points.push_back({Position, Particle.mColor, std::abs(Particle.mSize.x * Scale) * 0.5f, GetRibbonPayload(Particle).mTiledU});
            }

            Begin = End;

            if (Points.size() < 2) {
                continue;
            }

            // 늘이기 방식도 타일 방식과 같은 방향으로 머리 쪽 U가 큽니다.
            if (!Tiling) {
                const float LastIndex{static_cast<float>(Points.size() - 1)};

                for (Index_t Point{}; Point < Points.size(); ++Point) {
                    Points[Point].mU = 1.0f - static_cast<float>(Point) / LastIndex;
                }
            }

            if (StepSize > 0_m && MaxSteps > 0) {
                Tessellate(Points, StepSize, MaxSteps, Batch.mPoints);
            } else {
                Batch.mPoints.insert(Batch.mPoints.end(), Points.begin(), Points.end());
            }

            Batch.mStripEnds.push_back(static_cast<Uint32>(Batch.mPoints.size()));
        }
    }

    Engine::FRibbonPayloadData& Engine::FParticleRibbonEmitterInstance::GetRibbonPayload(Engine::FBaseParticle& Particle) const {
        return Engine::ParticleHelper::GetPayload<Engine::FRibbonPayloadData>(Particle, mInstancePayloadOffset);
    }

    const Engine::FRibbonPayloadData& Engine::FParticleRibbonEmitterInstance::GetRibbonPayload(const Engine::FBaseParticle& Particle) const {
        return Engine::ParticleHelper::GetPayload<Engine::FRibbonPayloadData>(Particle, mInstancePayloadOffset);
    }

    Int32 Engine::FParticleRibbonEmitterInstance::GetInstancePayloadSize() const {
        return static_cast<Int32>(sizeof(Engine::FRibbonPayloadData));
    }

    void Engine::FParticleRibbonEmitterInstance::Spawn(float DeltaTime) {
        if (mRibbonTypeData == nullptr || mRibbonTypeData->GetSpawnDistance() <= 0_m) {
            Engine::FParticleEmitterInstance::Spawn(DeltaTime);

            return;
        }

        const FVector Current{GetEmitterOrigin()};

        if (!mSpawningEnabled || !mCurrentLODLevel->IsEnabled() || mEmitterTime < 0.0f || HasCompletedLoops()) {
            mHasLastSource = false;

            return;
        }

        if (!mHasLastSource) {
            BreakTrail();
            mLastSourcePosition = Current;
            mHasLastSource = true;

            return;
        }

        const FVector Delta{Current - mLastSourcePosition};
        const Core::FLength Step{Core::FLength::FromBase(Delta.Length())};

        if (Step < 0.1_mm) {
            return;
        }

        const Core::FLength MaxSpawnDistance{mRibbonTypeData->GetMaxSpawnDistance()};

        if (MaxSpawnDistance > 0_m && Step > MaxSpawnDistance) {
            BreakTrail();
            mLastSourcePosition = Current;

            return;
        }

        const Int32 MaxCount{static_cast<Int32>(mRibbonTypeData->GetMaxParticleInTrailCount())};
        const Core::FLength SpawnDistance{mRibbonTypeData->GetSpawnDistance()};

        if (!mTrailHasPoints && mRibbonTypeData->IsSpawnInitialParticle()) {
            KillOldest(mActiveParticles + 1 - MaxCount);
            SpawnRibbonParticle(mLastSourcePosition, DeltaTime);
        }

        // 이번 프레임 이동 구간에서 SpawnDistance 간격으로 점이 놓일 자리를 찾습니다.
        const Core::FLength OldAccumulated{mDistanceAccumulated};
        const Core::FLength Total{OldAccumulated + Step};
        const Int32 Number{static_cast<Int32>(std::floor(static_cast<float>(Total / SpawnDistance)))};

        mDistanceAccumulated = Total - static_cast<float>(Number) * SpawnDistance;

        const Int32 First{Number > MaxCount ? Number - MaxCount + 1 : 1};
        const Int32 SpawnCount{Number - First + 1};

        if (SpawnCount > 0) {
            KillOldest(mActiveParticles + SpawnCount - MaxCount);

            for (Int32 Index{First}; Index <= Number; ++Index) {
                const float T{std::clamp(static_cast<float>((static_cast<float>(Index) * SpawnDistance - OldAccumulated) / Step), 0.0f, 1.0f)};

                SpawnRibbonParticle(mLastSourcePosition + Delta * T, DeltaTime * (1.0f - T));
            }
        }

        mLastSourcePosition = Current;
    }

    void Engine::FParticleRibbonEmitterInstance::SpawnParticles(Int32 Count, float StartTime, float Increment) {
        if (mRibbonTypeData == nullptr) {
            Engine::FParticleEmitterInstance::SpawnParticles(Count, StartTime, Increment);

            return;
        }

        const Int32 MaxCount{static_cast<Int32>(mRibbonTypeData->GetMaxParticleInTrailCount())};

        Count = std::min(Count, MaxCount);
        KillOldest(mActiveParticles + Count - MaxCount);

        const FVector Origin{GetEmitterOrigin()};

        for (Int32 Index{}; Index < Count; ++Index) {
            SpawnRibbonParticle(Origin, StartTime - Increment * static_cast<float>(Index));
        }
    }

    void Engine::FParticleRibbonEmitterInstance::UpdateBoundingBox(float DeltaTime) {
        Engine::FParticleEmitterInstance::UpdateBoundingBox(DeltaTime);

        if (mRibbonTypeData == nullptr || !mRibbonTypeData->IsRenderHead() || mActiveParticles == 0 || mComponent == nullptr) {
            return;
        }

        const FVector Scale{GetComponentScale().Abs()};
        const float Radius{std::abs(mTrailBaseSize.x) * std::max({Scale.x, Scale.y, Scale.z}) * 0.5f};
        const FVector Head{mComponent->GetWorldMatrix().TransformPosition({})};
        const FVector Extent{Radius, Radius, Radius};

        mBoundingBox = FBox{mBoundingBox.mMinimum.Min(Head - Extent), mBoundingBox.mMaximum.Max(Head + Extent)};
    }

    void Engine::FParticleRibbonEmitterInstance::SpawnRibbonParticle(const FVector& Position, float SpawnTime) {
        if (mActiveParticles >= mMaxActiveParticles) {
            const Int32 NewMaximum{std::min(mMaxActiveParticles * 2, 65535)};

            if (NewMaximum <= mMaxActiveParticles || !Resize(NewMaximum)) {
                return;
            }
        }

        Engine::FBaseParticle& Particle{*GetParticle(mParticleIndices[mActiveParticles])};

        PreSpawn(Particle, Position);

        for (const Engine::UParticleModule* Module : mCurrentLODLevel->GetSpawnModules()) {
            if (Module->IsEnabled()) {
                Module->Spawn(*this, mTemplate->GetModuleOffset(Module), SpawnTime, Particle);
            }
        }

        // 수명과 크기는 트레일 첫 점에서 한 번만 뽑습니다. 점마다 다르면 꼬리 중간이 먼저 사라집니다.
        if (!mTrailHasPoints) {
            mTrailOneOverLifetime = Particle.mOneOverMaxLifetime;
            mTrailBaseSize = Particle.mBaseSize;
            mTiledDistance = {};
        } else {
            mTiledDistance += Core::FLength::FromBase((Position - mLastSpawnPosition).Length());
        }

        Particle.mOneOverMaxLifetime = mTrailOneOverLifetime;
        Particle.mRelativeTime = SpawnTime * mTrailOneOverLifetime;
        Particle.mBaseSize = mTrailBaseSize;
        Particle.mSize = mTrailBaseSize;
        PostSpawn(Particle, SpawnTime);

        Engine::FRibbonPayloadData& Payload{GetRibbonPayload(Particle)};
        const Core::FLength TilingDistance{mRibbonTypeData->GetTilingDistance()};

        Payload.mTiledU = TilingDistance > 0_m ? static_cast<float>(mTiledDistance / TilingDistance) : 0.0f;
        Payload.mTrailId = mTrailId;
        mLastSpawnPosition = Position;
        mTrailHasPoints = true;
        ++mActiveParticles;
    }

    void Engine::FParticleRibbonEmitterInstance::KillOldest(Int32 Count) {
        if (Count <= 0 || mActiveParticles == 0) {
            return;
        }

        Count = std::min(Count, mActiveParticles);

        Core::TArray<Engine::FBaseParticle*> Particles{};

        Particles.reserve(static_cast<Index_t>(mActiveParticles));

        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            Particles.push_back(GetParticle(mParticleIndices[Index]));
        }

        std::partial_sort(Particles.begin(), Particles.begin() + Count, Particles.end(), [](const Engine::FBaseParticle* A, const Engine::FBaseParticle* B) {
            return (A->mFlags & Engine::EParticleState::CounterMask) < (B->mFlags & Engine::EParticleState::CounterMask);
        });

        // 수명을 넘긴 것으로 표시해 일반 제거 경로로 지웁니다.
        for (Int32 Index{}; Index < Count; ++Index) {
            Particles[static_cast<Index_t>(Index)]->mRelativeTime = 1.1f;
        }

        KillParticles();
    }

    void Engine::FParticleRibbonEmitterInstance::BreakTrail() {
        ++mTrailId;
        mTrailHasPoints = false;
        mDistanceAccumulated = {};
    }

}
