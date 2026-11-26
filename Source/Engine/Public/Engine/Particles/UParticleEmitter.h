/**
 * @file UParticleEmitter.h
 * @brief 파티클 시스템 안의 에미터 하나와 LOD 레벨 목록입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/UObject.h"

#include <memory>

#include "UParticleEmitter.generated.h"

namespace Engine {

    class UParticleLODLevel;
    class UParticleModule;
    class UParticleSystemComponent;
    class FParticleEmitterInstance;

    /**
     * @brief LOD 레벨을 소유하고 파티클 한 칸의 메모리 배치(모듈 Payload 오프셋)를 계산합니다.
     * @details 모듈 구성이 바뀌면 Build()를 다시 불러야 합니다. 캐시된 에셋은 로드할 때 한 번만 Build합니다.
     */
    UCLASS()
    class UParticleEmitter final : public CoreUObject::UObject {
        GENERATED_BODY()

    public:
        UParticleEmitter();

        ~UParticleEmitter() override;

    public:
        /**
         * @brief Required·Spawn과 기본 모듈(Lifetime, Size, Velocity, Color)을 가진 LOD 0 하나로 에미터를 만듭니다.
         */
        static std::unique_ptr<Engine::UParticleEmitter> CreateDefault();

        /**
         * @brief 분포를 굽고 모듈 목록과 Payload 배치를 다시 계산합니다.
         */
        void Build();

        /**
         * @brief 이 에미터를 실행할 인스턴스를 만들고 초기화합니다. TypeData가 있으면 그 종류로 만듭니다.
         */
        std::unique_ptr<Engine::FParticleEmitterInstance> CreateInstance(Engine::UParticleSystemComponent& Component) const;

        const Core::TArray<std::unique_ptr<Engine::UParticleLODLevel>>& GetLODLevels() const;

        /**
         * @return 범위 밖이면 nullptr입니다.
         */
        Engine::UParticleLODLevel* GetLODLevel(Index_t Index) const;

        Index_t GetNumLODLevels() const;

        void AddLODLevel(std::unique_ptr<Engine::UParticleLODLevel> Level);

        /**
         * @brief SourceIndex 레벨을 모듈까지 복사해 InsertIndex에 넣습니다. Build()는 호출자가 부릅니다.
         */
        Engine::UParticleLODLevel* DuplicateLODLevel(Index_t SourceIndex, Index_t InsertIndex);

        /**
         * @brief 레벨을 지웁니다. 마지막 하나는 지우지 않습니다. Build()는 호출자가 부릅니다.
         */
        bool RemoveLODLevel(Index_t Index);

        /**
         * @brief 파티클 한 칸의 크기(바이트)입니다. 16바이트 정렬 전 값입니다.
         */
        Int32 GetParticleSize() const;

        /**
         * @brief 모듈 Payload의 시작 오프셋입니다.
         * @return Payload가 없거나 이 에미터의 모듈이 아니면 0입니다.
         */
        Int32 GetModuleOffset(const Engine::UParticleModule* Module) const;

        void Serialize(Core::FArchive& Archive) override;

    private:
        /**
         * @brief 파티클 칸 크기와 모듈별 Payload 오프셋을 계산합니다.
         */
        void CacheEmitterModuleInfo();

        /**
         * @brief LOD 1 이상이 LOD 0과 같은 구조인지 검사하고 어긋나면 경고합니다.
         */
        void ValidateLODLevels() const;

        /**
         * @brief 각 레벨의 번호를 배열 순서에 맞춥니다.
         */
        void RenumberLODLevels();

    private:
        Core::TArray<std::unique_ptr<Engine::UParticleLODLevel>> mLODLevels{};

        Int32 mParticleSize{};

        Core::TMap<const Engine::UParticleModule*, Int32> mModuleOffsets{};
    };

}
