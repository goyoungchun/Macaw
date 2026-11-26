/**
 * @file UParticleModuleBeamNoise.h
 * @brief 빔을 따라 놓은 노이즈 점을 축에 수직인 방향으로 흔드는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"

#include "Core/Types.h"
#include "Engine/Particles/UParticleModule.h"
#include "UParticleModuleBeamNoise.generated.h"

namespace Engine {

    /**
     * @brief 노이즈 Payload의 고정 앞부분입니다. 바로 뒤에 FBeamNoisePoint가 (빈도 + 1)개 붙습니다.
     */
    struct FBeamNoiseHeader {
    public:
        /**
         * @brief 0 이하가 되면 새 목표를 뽑습니다.
         */
        Core::FTime mLockTimer{};

        float mPadding[3]{};
    };

    static_assert(sizeof(Engine::FBeamNoiseHeader) == 16);

    /**
     * @brief 노이즈 점 하나입니다. 빔 축에 수직인 평면의 (Right, Up) 오프셋(m)입니다.
     */
    struct FBeamNoisePoint {
    public:
        /**
         * @brief 지금 위치이며 그리기 데이터가 읽습니다.
         */
        float mCurrent[2]{};

        /**
         * @brief 움직여 갈 목표입니다.
         */
        float mTarget[2]{};
    };

    static_assert(sizeof(Engine::FBeamNoisePoint) == 16);

    /**
     * @brief 노이즈 점만 움직이고 경로는 빔 인스턴스가 그리기 데이터를 만들 때 계산합니다.
     */
    UCLASS()
    class UParticleModuleBeamNoise final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleBeamNoise();

        ~UParticleModuleBeamNoise() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const override;

        /**
         * @brief 빈도에 따라 크기가 바뀌므로 빈도를 바꾸면 에미터를 다시 Build해야 합니다.
         */
        Uint32 RequiredBytes() const override;

        Engine::EParticleModuleType GetModuleType() const override;

        /**
         * @brief 꺾이는 지점 수이며 1~250입니다. 노이즈 점은 양 끝을 포함해 (빈도 + 1)개입니다.
         */
        Uint32 GetFrequency() const;

        bool IsSmooth() const;

        /**
         * @brief Smooth일 때 노이즈 점 사이를 나누는 칸 수이며 1~16입니다.
         */
        Uint32 GetNoiseTessellation() const;

        /**
         * @brief 빔 옆으로 벗어나는 최대 거리(m)입니다.
         */
        Core::FLength GetNoiseRange() const;

        static Engine::FBeamNoiseHeader& GetHeader(Uint8* ParticleBase, Int32 Offset);

        static Engine::FBeamNoisePoint* GetPoints(Uint8* ParticleBase, Int32 Offset);

        static const Engine::FBeamNoisePoint* GetPoints(const Uint8* ParticleBase, Int32 Offset);

    private:
        void PickTargets(Engine::FParticleEmitterInstance& Owner, Engine::FBeamNoisePoint* Points) const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Noise")
        Uint32 mFrequency{8};

        UPROPERTY(Edit, Serialize, Category = "Noise")
        Core::FLength mNoiseRange{Core::FLength::FromBase(0.2f)};

        /**
         * @brief 새 목표를 뽑는 간격(초)이며 0이면 매 프레임입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Noise")
        Core::FTime mNoiseLockTime{Core::FTime::FromBase(0.08f)};

        /**
         * @brief 목표로 가는 속도(m/s)이며 0이면 바로 이동합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Noise")
        Core::FSpeed mNoiseSpeed{};

        /**
         * @brief true면 노이즈 점 사이를 곡선으로 잇습니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Noise")
        bool mSmooth{true};

        UPROPERTY(Edit, Serialize, Category = "Noise")
        Uint32 mNoiseTessellation{4};
    };

}
