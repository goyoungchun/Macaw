/**
 * @file UParticleModuleTypeDataRibbon.h
 * @brief 에미터가 지나간 자리를 파티클 점으로 이어 띠를 그리는 리본 에미터 TypeData입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"

#include "Core/Types.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBase.h"
#include "UParticleModuleTypeDataRibbon.generated.h"

namespace Engine {

    /**
     * @brief 리본 에미터 설정입니다. 거리 단위는 m입니다.
     */
    UCLASS()
    class UParticleModuleTypeDataRibbon final : public Engine::UParticleModuleTypeDataBase {
        GENERATED_BODY()

    public:
        UParticleModuleTypeDataRibbon();

        ~UParticleModuleTypeDataRibbon() override;

    public:
        std::unique_ptr<Engine::FParticleEmitterInstance> CreateInstance(const Engine::UParticleEmitter& Emitter, Engine::UParticleSystemComponent& Component) const override;

        /**
         * @brief 트레일 하나의 최대 점 수이며 최소 2입니다.
         */
        Uint32 GetMaxParticleInTrailCount() const;

        /**
         * @brief 이 거리만큼 움직일 때마다 점을 놓습니다. 0이면 거리 기준 생성을 끄고 Spawn 모듈을 따릅니다.
         */
        Core::FLength GetSpawnDistance() const;

        /**
         * @brief 텍스처 한 번이 덮는 길이입니다. 0이면 띠 전체에 늘입니다.
         */
        Core::FLength GetTilingDistance() const;

        /**
         * @brief 한 프레임에 이보다 멀리 가면 순간 이동으로 보고 잇지 않습니다. 0이면 검사하지 않습니다.
         */
        Core::FLength GetMaxSpawnDistance() const;

        Uint32 GetSheets() const;

        /**
         * @brief true면 마지막 점에서 에미터 현재 위치까지도 잇습니다.
         */
        bool IsRenderHead() const;

        /**
         * @brief true면 움직이기 시작할 때 SpawnDistance를 채우기 전에 첫 점을 놓습니다.
         */
        bool IsSpawnInitialParticle() const;

        /**
         * @brief 점 사이를 이 간격으로 곡선 점을 채웁니다. 0이면 채우지 않습니다.
         */
        Core::FLength GetDistanceTessellationStepSize() const;

        Uint32 GetMaxTessellationBetweenParticles() const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Trail")
        Uint32 mMaxParticleInTrailCount{64};

        UPROPERTY(Edit, Serialize, Category = "Trail")
        Core::FLength mSpawnDistance{Core::FLength::FromBase(0.1f)};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        Core::FLength mTilingDistance{};

        UPROPERTY(Edit, Serialize, Category = "Trail")
        Core::FLength mMaxSpawnDistance{Core::FLength::FromBase(5.0f)};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        Uint32 mSheets{1};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        bool mRenderHead{true};

        UPROPERTY(Edit, Serialize, Category = "Trail")
        bool mSpawnInitialParticle{true};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        Core::FLength mDistanceTessellationStepSize{};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        Uint32 mMaxTessellationBetweenParticles{8};
    };

}
