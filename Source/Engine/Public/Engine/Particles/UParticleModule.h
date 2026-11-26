/**
 * @file UParticleModule.h
 * @brief 파티클 생성·갱신 단계에 끼어드는 모듈의 기반 클래스입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/UObject.h"
#include "UParticleModule.generated.h"

namespace Engine {

    struct FBaseParticle;
    class FParticleEmitterInstance;

    /**
     * @brief 에디터의 모듈 분류와 전용 슬롯 판단에 쓰는 모듈 종류입니다.
     */
    enum class EParticleModuleType : Uint8 {
        /**
         * @brief 모든 에미터에서 쓰는 일반 모듈입니다.
         */
        General,
        /**
         * @brief 에미터 종류를 바꾸는 TypeData 모듈입니다.
         */
        TypeData,
        Beam,
        Trail,
        /**
         * @brief 모든 에미터에 하나씩 있는 Spawn 모듈입니다.
         */
        Spawn,
        /**
         * @brief 모든 에미터에 하나씩 있는 Required 모듈입니다.
         */
        Required,
        Event,
        Light,
        SubUV
    };

    /**
     * @brief 파티클을 만들 때(Spawn)와 매 프레임(Update) 파티클 값을 바꾸는 모듈입니다.
     * @details 모듈은 캐시된 에셋의 일부라 실행 중에 바뀌지 않습니다. 파티클마다 따로 저장할 값은
     *          RequiredBytes()만큼의 Payload에 둡니다. 설정이 바뀌면 BakeDistributions()로 분포 표를 다시 굽습니다.
     */
    UCLASS(Abstract)
    class UParticleModule : public CoreUObject::UObject {
        GENERATED_BODY()

    public:
        UParticleModule();

        ~UParticleModule() override;

    public:
        /**
         * @brief 파티클 하나가 생성될 때 부릅니다. IsSpawnModule()이 true일 때만 불립니다.
         * @param Owner 파티클을 소유한 에미터 인스턴스입니다.
         * @param Offset 이 모듈 Payload의 시작 오프셋이며 Payload가 없으면 0입니다.
         * @param SpawnTime 이번 프레임 안에서 생성 시점부터 프레임 끝까지 남은 시간(초)입니다.
         * @param Particle 생성 중인 파티클입니다.
         */
        virtual void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const;

        /**
         * @brief 매 프레임 살아 있는 파티클을 갱신합니다. IsUpdateModule()이 true일 때만 불립니다.
         */
        virtual void Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const;

        /**
         * @brief 파티클마다 필요한 Payload 크기(바이트)입니다.
         */
        virtual Uint32 RequiredBytes() const;

        /**
         * @brief 설정한 분포를 표로 굽습니다.
         */
        virtual void BakeDistributions();

        virtual Engine::EParticleModuleType GetModuleType() const;

        bool IsSpawnModule() const;

        bool IsUpdateModule() const;

        bool IsEnabled() const;

        void SetEnabled(bool Enabled);

        /**
         * @brief 하위 객체라 식별자는 저장하지 않고 프로퍼티만 읽고 씁니다.
         */
        void Serialize(Core::FArchive& Archive) override;

        /**
         * @brief 값이 바뀌면 분포를 다시 굽습니다.
         */
        void PostEditChangeProperty(const CoreUObject::FPropertyChangedEvent& Event) override;

    protected:
        /**
         * @brief true면 파티클 생성 때 Spawn()을 부릅니다.
         */
        bool mSpawnModule{};

        /**
         * @brief true면 매 프레임 Update()를 부릅니다.
         */
        bool mUpdateModule{};

    private:
        UPROPERTY(Edit, Serialize, Category = "Module", Getter = IsEnabled, Setter = SetEnabled)
        bool mEnabled{true};
    };

}
