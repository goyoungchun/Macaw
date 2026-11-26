/**
 * @file UParticleLODLevel.h
 * @brief 에미터 LOD 한 단계의 모듈 구성입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/UObject.h"

#include <memory>

#include "UParticleLODLevel.generated.h"

namespace Engine {

    class UParticleModule;
    class UParticleModuleRequired;
    class UParticleModuleSpawn;
    class UParticleModuleTypeDataBase;
    class UParticleModuleEventGenerator;
    class UParticleModuleEventReceiverBase;

    /**
     * @brief LOD 한 단계에서 실행할 모듈을 소유합니다.
     * @details Required·Spawn·TypeData는 전용 슬롯에 두고 일반 모듈 목록(mModules)에는 넣지 않습니다.
     *          모든 LOD 레벨은 LOD 0과 같은 순서·종류의 모듈을 가져야 Payload 칸이 맞습니다.
     */
    UCLASS()
    class UParticleLODLevel final : public CoreUObject::UObject {
        GENERATED_BODY()

    public:
        UParticleLODLevel();

        ~UParticleLODLevel() override;

    public:
        /**
         * @brief Required·Spawn 모듈을 기본값으로 채운 레벨을 만듭니다.
         */
        static std::unique_ptr<Engine::UParticleLODLevel> CreateDefault();

        /**
         * @brief 일반 모듈 목록에서 생성·갱신 단계 목록을 다시 만듭니다.
         */
        void UpdateModuleLists();

        /**
         * @brief 모든 모듈의 분포를 다시 굽습니다.
         */
        void BakeDistributions();

        /**
         * @brief 직렬화로 모듈까지 깊게 복사합니다.
         * @return 실패하면 nullptr입니다.
         */
        std::unique_ptr<Engine::UParticleLODLevel> Duplicate() const;

        Int32 GetLevel() const;

        void SetLevel(Int32 Level);

        bool IsEnabled() const;

        void SetEnabled(bool Enabled);

        Engine::UParticleModuleRequired* GetRequiredModule() const;

        Engine::UParticleModuleSpawn* GetSpawnModule() const;

        /**
         * @brief TypeData 모듈이며 없으면 기본 스프라이트 에미터입니다.
         */
        Engine::UParticleModuleTypeDataBase* GetTypeDataModule() const;

        void SetTypeDataModule(std::unique_ptr<Engine::UParticleModuleTypeDataBase> Module);

        const Core::TArray<std::unique_ptr<Engine::UParticleModule>>& GetModules() const;

        /**
         * @brief 모듈 인덱스를 찾습니다.
         * @return 없으면 -1입니다.
         */
        Int32 FindModuleIndex(const Engine::UParticleModule* Module) const;

        /**
         * @brief 일반 모듈을 목록 끝에 붙입니다. 전용 슬롯 종류는 거부합니다.
         */
        bool AddModule(std::unique_ptr<Engine::UParticleModule> Module);

        /**
         * @brief 일반 모듈을 지정 위치에 넣습니다. 범위를 넘으면 끝에 넣습니다.
         */
        bool InsertModule(std::unique_ptr<Engine::UParticleModule> Module, Index_t Index);

        /**
         * @brief 모듈을 목록에서 빼서 돌려줍니다.
         * @return 없으면 nullptr입니다.
         */
        std::unique_ptr<Engine::UParticleModule> RemoveModule(const Engine::UParticleModule* Module);

        bool MoveModule(Index_t From, Index_t To);

        /**
         * @brief UpdateModuleLists()가 채운 생성 단계 모듈입니다(꺼진 모듈 포함).
         */
        const Core::TArray<Engine::UParticleModule*>& GetSpawnModules() const;

        /**
         * @brief UpdateModuleLists()가 채운 갱신 단계 모듈입니다(꺼진 모듈 포함).
         */
        const Core::TArray<Engine::UParticleModule*>& GetUpdateModules() const;

        /**
         * @brief 모듈 중 첫 이벤트 Generator입니다(꺼진 것 포함). 없으면 nullptr입니다.
         */
        Engine::UParticleModuleEventGenerator* GetEventGenerator() const;

        /**
         * @brief 모듈 중 이벤트 Receiver입니다(꺼진 것 포함).
         */
        const Core::TArray<Engine::UParticleModuleEventReceiverBase*>& GetEventReceivers() const;

        void Serialize(Core::FArchive& Archive) override;

    private:
        /**
         * @brief 에미터 안에서의 LOD 번호이며 에미터가 배열 순서로 맞춥니다.
         */
        Int32 mLevel{};

        UPROPERTY(Edit, Serialize, Category = "LOD", Getter = IsEnabled, Setter = SetEnabled)
        bool mEnabled{true};

        std::unique_ptr<Engine::UParticleModuleRequired> mRequiredModule{};

        std::unique_ptr<Engine::UParticleModuleSpawn> mSpawnModule{};

        std::unique_ptr<Engine::UParticleModuleTypeDataBase> mTypeDataModule{};

        Core::TArray<std::unique_ptr<Engine::UParticleModule>> mModules{};

        Core::TArray<Engine::UParticleModule*> mSpawnModules{};

        Core::TArray<Engine::UParticleModule*> mUpdateModules{};

        Engine::UParticleModuleEventGenerator* mEventGenerator{};

        Core::TArray<Engine::UParticleModuleEventReceiverBase*> mEventReceivers{};
    };

}
