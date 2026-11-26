#include "Engine/Particles/Module/UParticleModuleSubUV.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/ParticleHelper.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"

#include <algorithm>
#include <cmath>

namespace Engine {

    Engine::UParticleModuleSubUV::UParticleModuleSubUV() {
        SetName("SubImage Index");
        mSpawnModule = true;
        mUpdateModule = true;
        mSubImageIndex.mMode = Engine::EDistributionMode::Curve;
        // 4칸 아틀라스를 수명 동안 한 바퀴 돕니다.
        Engine::FFloatCurveKey Start{};
        Engine::FFloatCurveKey End{};

        Start.mKeyId = 0;
        End.mKeyId = 1;
        End.mTime = 1.0f;
        End.mValue = 3.0f;
        mSubImageIndex.mCurveKeys.push_back(Start);
        mSubImageIndex.mCurveKeys.push_back(End);

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleSubUV::~UParticleModuleSubUV() = default;

    Engine::EParticleModuleType Engine::UParticleModuleSubUV::GetModuleType() const {
        return Engine::EParticleModuleType::SubUV;
    }

    void Engine::UParticleModuleSubUV::Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float, Engine::FBaseParticle& Particle) const {
        Engine::FFullSubUVPayload& Payload{Engine::ParticleHelper::GetPayload<Engine::FFullSubUVPayload>(Particle, Offset)};

        Payload = {};

        const Engine::UParticleModuleRequired& Required{*Owner.mCurrentLODLevel->GetRequiredModule()};

        if (Required.UsesSubUV()) {
            Payload.mImageIndex = DetermineImageIndex(Owner, Required, Particle, Payload);
        }
    }

    void Engine::UParticleModuleSubUV::Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float) const {
        const Engine::UParticleModuleRequired& Required{*Owner.mCurrentLODLevel->GetRequiredModule()};

        if (!Required.UsesSubUV()) {
            return;
        }

        Engine::ParticleHelper::ForEachParticle(Owner, [this, &Owner, &Required, Offset](Engine::FBaseParticle& Particle, Uint8* Base) {
            Engine::FFullSubUVPayload& Payload{Engine::ParticleHelper::GetPayload<Engine::FFullSubUVPayload>(Base, Offset)};

            Payload.mImageIndex = DetermineImageIndex(Owner, Required, Particle, Payload);
        });
    }

    Uint32 Engine::UParticleModuleSubUV::RequiredBytes() const {
        return sizeof(Engine::FFullSubUVPayload);
    }

    void Engine::UParticleModuleSubUV::BakeDistributions() {
        mSubImageIndex.Bake();
    }

    Engine::FFloatDistribution& Engine::UParticleModuleSubUV::GetSubImageIndex() {
        return mSubImageIndex;
    }

    float Engine::UParticleModuleSubUV::DetermineImageIndex(Engine::FParticleEmitterInstance& Owner, const Engine::UParticleModuleRequired& Required, const Engine::FBaseParticle& Particle, Engine::FFullSubUVPayload& Payload) const {
        const Engine::EParticleSubUVInterpMethod Method{Required.GetSubUVMethod()};
        const Int32 Total{static_cast<Int32>(Required.GetTotalSubImages())};
        float ImageIndex{Payload.mImageIndex};

        if (Method == Engine::EParticleSubUVInterpMethod::Linear || Method == Engine::EParticleSubUVInterpMethod::LinearBlend) {
            ImageIndex = mSubImageIndex.GetValue(Particle.mRelativeTime, Owner.mRandomStream);
        } else if (Method == Engine::EParticleSubUVInterpMethod::Random || Method == Engine::EParticleSubUVInterpMethod::RandomBlend) {
            const float Interval{Required.GetRandomImageTime()};

            if (Interval == 0.0f || Payload.mRandomImageTime == 0.0f || Particle.mRelativeTime - Payload.mRandomImageTime > Interval) {
                ImageIndex = static_cast<float>(Owner.mRandomStream.RandRange(0, Total - 1));

                // 0은 "아직 뽑지 않음"으로 읽히므로 생성 직후에도 0이 아닌 값을 둡니다.
                Payload.mRandomImageTime = Particle.mRelativeTime > 0.0f ? Particle.mRelativeTime : 0.0001f;
            }
        }

        // 마지막 칸을 넘지 않게 하고, Blend가 아니면 소수부를 버립니다.
        ImageIndex = std::clamp(ImageIndex, 0.0f, static_cast<float>(Total - 1));

        if (Method == Engine::EParticleSubUVInterpMethod::Linear || Method == Engine::EParticleSubUVInterpMethod::Random) {
            ImageIndex = std::trunc(ImageIndex);
        }

        return ImageIndex;
    }

}
