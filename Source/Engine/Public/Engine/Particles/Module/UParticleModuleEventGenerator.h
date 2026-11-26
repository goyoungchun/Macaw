/**
 * @file UParticleModuleEventGenerator.h
 * @brief 어떤 사건을 어떤 이름의 이벤트로 내보낼지 정하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/FName.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleEventTypes.h"
#include "UParticleModuleEventGenerator.generated.h"

namespace Engine {

    /**
     * @brief 내보낼 이벤트 하나의 설정입니다.
     */
    USTRUCT()
    struct FParticleEventGeneratorInfo {
        GENERATED_BODY()

    public:
        UPROPERTY(Edit, Serialize)
        Engine::EParticleEventType mType{Engine::EParticleEventType::Collision};

        /**
         * @brief Receiver가 이 이름으로 골라 받습니다.
         */
        UPROPERTY(Edit, Serialize)
        Core::FString mCustomName{};

        /**
         * @brief N번째 사건마다 하나만 보냅니다. 1 이하면 매번입니다.
         */
        UPROPERTY(Edit, Serialize)
        Int32 mFrequency{1};

        /**
         * @brief Collision 전용입니다. 그 파티클의 첫 충돌만 보냅니다.
         */
        UPROPERTY(Edit, Serialize)
        bool mFirstTimeOnly{};

        /**
         * @brief Collision 전용입니다. 마지막 충돌(최대 충돌 횟수 도달)만 보냅니다.
         */
        UPROPERTY(Edit, Serialize)
        bool mLastTimeOnly{};

        UPROPERTY(Edit, Serialize)
        Int32 mCustomData{};
    };

    /**
     * @brief 파티클을 직접 바꾸지 않습니다. 에미터 인스턴스가 생성·제거·충돌·Burst를 처리할 때 이 설정을 보고 컴포넌트에 보고합니다.
     */
    UCLASS()
    class UParticleModuleEventGenerator final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleEventGenerator();

        ~UParticleModuleEventGenerator() override;

    public:
        Engine::EParticleModuleType GetModuleType() const override;

        /**
         * @brief 이벤트 이름을 FName으로 바꿔 둡니다.
         */
        void BakeDistributions() override;

        bool HasEvent(Engine::EParticleEventType Type) const;

        const Core::TArray<Engine::FParticleEventGeneratorInfo>& GetEvents() const;

        /**
         * @brief Events[Index]의 이름입니다. 범위 밖이면 빈 이름입니다.
         */
        Core::FName GetEventName(Index_t Index) const;

        void AddEvent(const Engine::FParticleEventGeneratorInfo& Info);

    private:
        UPROPERTY(Edit, Serialize, Category = "Events")
        Core::TArray<Engine::FParticleEventGeneratorInfo> mEvents{};

        Core::TArray<Core::FName> mEventNames{};
    };

}
