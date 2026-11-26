/**
 * @file FParticleRibbonEmitterInstance.h
 * @brief 에미터가 움직인 길을 따라 점을 놓고 띠로 잇는 리본 에미터 인스턴스입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"

#include "Core/Math/FVector.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    class UParticleModuleTypeDataRibbon;

    /**
     * @brief 리본 파티클마다 붙는 값입니다.
     */
    struct FRibbonPayloadData {
    public:
        /**
         * @brief 생성 시점까지의 누적 이동 거리를 타일 거리로 나눈 텍스처 좌표입니다.
         */
        float mTiledU{};

        /**
         * @brief 순간 이동이나 재활성화로 끊길 때마다 바뀝니다. 같은 값끼리만 잇습니다.
         */
        Uint32 mTrailId{};

        float mPadding[2]{};
    };

    /**
     * @brief TypeDataRibbon이 만드는 인스턴스입니다. 트레일 하나의 수명과 크기는 첫 점에서 한 번만 뽑습니다.
     */
    class FParticleRibbonEmitterInstance final : public Engine::FParticleEmitterInstance {
    public:
        void Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component) override;
        void SetCurrentLODIndex(Int32 LODIndex) override;

        void FillRenderData(Engine::FParticleRenderData& Out) const override;

        Engine::FRibbonPayloadData& GetRibbonPayload(Engine::FBaseParticle& Particle) const;

        const Engine::FRibbonPayloadData& GetRibbonPayload(const Engine::FBaseParticle& Particle) const;

    protected:
        Int32 GetInstancePayloadSize() const override;

        /**
         * @brief SpawnDistance가 있으면 이동 거리마다 점을 놓고, 없으면 Spawn 모듈을 따릅니다.
         */
        void Spawn(float DeltaTime) override;

        void SpawnParticles(Int32 Count, float StartTime, float Increment) override;

        void UpdateBoundingBox(float DeltaTime) override;

    private:
        void SpawnRibbonParticle(const FVector& Position, float SpawnTime);

        /**
         * @brief 상한을 넘지 않도록 가장 오래된 점부터 Count개를 지웁니다.
         */
        void KillOldest(Int32 Count);

        void BreakTrail();

    private:
        const Engine::UParticleModuleTypeDataRibbon* mRibbonTypeData{};

        FVector mLastSourcePosition{};

        FVector mLastSpawnPosition{};

        /**
         * @brief 아직 점 하나 분량이 안 된 이동 거리입니다.
         */
        Core::FLength mDistanceAccumulated{};

        /**
         * @brief 현재 트레일의 누적 이동 거리입니다.
         */
        Core::FLength mTiledDistance{};

        float mTrailOneOverLifetime{};

        FVector mTrailBaseSize{1.0f, 1.0f, 1.0f};

        Uint32 mTrailId{};

        bool mHasLastSource{};

        bool mTrailHasPoints{};
    };

}
