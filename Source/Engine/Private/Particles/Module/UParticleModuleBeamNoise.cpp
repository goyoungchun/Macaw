#include "Engine/Particles/Module/UParticleModuleBeamNoise.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/ParticleHelper.h"

#include <algorithm>
#include <cmath>

namespace Engine {

    Engine::UParticleModuleBeamNoise::UParticleModuleBeamNoise() {
        SetName("Noise");
        mSpawnModule = true;
        mUpdateModule = true;
    }

    Engine::UParticleModuleBeamNoise::~UParticleModuleBeamNoise() = default;

    void Engine::UParticleModuleBeamNoise::Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float, Engine::FBaseParticle& Particle) const {
        Uint8* Base{reinterpret_cast<Uint8*>(&Particle)};
        Engine::FBeamNoisePoint* Points{GetPoints(Base, Offset)};

        // 첫 프레임부터 휜 모양이 보이도록 목표 위치에서 시작합니다.
        PickTargets(Owner, Points);

        for (Uint32 Index{}; Index <= GetFrequency(); ++Index) {
            Points[Index].mCurrent[0] = Points[Index].mTarget[0];
            Points[Index].mCurrent[1] = Points[Index].mTarget[1];
        }

        GetHeader(Base, Offset).mLockTimer = mNoiseLockTime;
    }

    void Engine::UParticleModuleBeamNoise::Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const {
        const Uint32 Frequency{GetFrequency()};
        const Core::FTime FrameTime{Core::FTime::FromBase(DeltaTime)};
        const Core::FLength MaxStep{mNoiseSpeed * FrameTime};

        Engine::ParticleHelper::ForEachParticle(Owner, [&](Engine::FBaseParticle&, Uint8* Base) {
            Engine::FBeamNoiseHeader& Header{GetHeader(Base, Offset)};
            Engine::FBeamNoisePoint* Points{GetPoints(Base, Offset)};

            Header.mLockTimer -= FrameTime;

            if (Header.mLockTimer <= Core::FTime{}) {
                PickTargets(Owner, Points);
                Header.mLockTimer = mNoiseLockTime;
            }

            for (Uint32 Index{}; Index <= Frequency; ++Index) {
                Engine::FBeamNoisePoint& Point{Points[Index]};
                const float DeltaX{Point.mTarget[0] - Point.mCurrent[0]};
                const float DeltaY{Point.mTarget[1] - Point.mCurrent[1]};
                const Core::FLength Distance{Core::FLength::FromBase(std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY))};

                // 속도가 0이거나 이번 프레임에 닿으면 바로 목표로, 아니면 목표를 넘지 않게 MaxStep만큼 갑니다.
                if (mNoiseSpeed <= Core::FSpeed{} || Distance <= MaxStep || Distance < Core::FLength::FromBase(0.000001f)) {
                    Point.mCurrent[0] = Point.mTarget[0];
                    Point.mCurrent[1] = Point.mTarget[1];
                } else {
                    const float Scale{MaxStep / Distance};

                    Point.mCurrent[0] += DeltaX * Scale;
                    Point.mCurrent[1] += DeltaY * Scale;
                }
            }
        });
    }

    Uint32 Engine::UParticleModuleBeamNoise::RequiredBytes() const {
        return static_cast<Uint32>(sizeof(Engine::FBeamNoiseHeader) + sizeof(Engine::FBeamNoisePoint) * (GetFrequency() + 1));
    }

    Engine::EParticleModuleType Engine::UParticleModuleBeamNoise::GetModuleType() const {
        return Engine::EParticleModuleType::Beam;
    }

    Uint32 Engine::UParticleModuleBeamNoise::GetFrequency() const {
        return std::clamp<Uint32>(mFrequency, 1, 250);
    }

    bool Engine::UParticleModuleBeamNoise::IsSmooth() const {
        return mSmooth;
    }

    Uint32 Engine::UParticleModuleBeamNoise::GetNoiseTessellation() const {
        return std::clamp<Uint32>(mNoiseTessellation, 1, 16);
    }

    Core::FLength Engine::UParticleModuleBeamNoise::GetNoiseRange() const {
        return Core::Math::Abs(mNoiseRange);
    }

    Engine::FBeamNoiseHeader& Engine::UParticleModuleBeamNoise::GetHeader(Uint8* ParticleBase, Int32 Offset) {
        return Engine::ParticleHelper::GetPayload<Engine::FBeamNoiseHeader>(ParticleBase, Offset);
    }

    Engine::FBeamNoisePoint* Engine::UParticleModuleBeamNoise::GetPoints(Uint8* ParticleBase, Int32 Offset) {
        return reinterpret_cast<Engine::FBeamNoisePoint*>(ParticleBase + Offset + sizeof(Engine::FBeamNoiseHeader));
    }

    const Engine::FBeamNoisePoint* Engine::UParticleModuleBeamNoise::GetPoints(const Uint8* ParticleBase, Int32 Offset) {
        return reinterpret_cast<const Engine::FBeamNoisePoint*>(ParticleBase + Offset + sizeof(Engine::FBeamNoiseHeader));
    }

    void Engine::UParticleModuleBeamNoise::PickTargets(Engine::FParticleEmitterInstance& Owner, Engine::FBeamNoisePoint* Points) const {
        const Uint32 Frequency{GetFrequency()};
        const float Range{GetNoiseRange().GetBase()};

        // 양 끝은 시작점과 끝점에 붙어 있어야 합니다.
        Points[0].mTarget[0] = 0.0f;
        Points[0].mTarget[1] = 0.0f;
        Points[Frequency].mTarget[0] = 0.0f;
        Points[Frequency].mTarget[1] = 0.0f;

        for (Uint32 Index{1}; Index < Frequency; ++Index) {
            Points[Index].mTarget[0] = Owner.mRandomStream.FRandRange(-Range, Range);
            Points[Index].mTarget[1] = Owner.mRandomStream.FRandRange(-Range, Range);
        }
    }

}
