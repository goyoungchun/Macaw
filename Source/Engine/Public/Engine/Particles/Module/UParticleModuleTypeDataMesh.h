/**
 * @file UParticleModuleTypeDataMesh.h
 * @brief 파티클마다 스태틱 메시를 그리는 메시 에미터 TypeData입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBase.h"
#include "UParticleModuleTypeDataMesh.generated.h"

namespace Engine {

    /**
     * @brief 메시 에미터 설정입니다. 메시는 경로로 두고 실행하는 컴포넌트가 로드합니다.
     * @details 메시 파티클은 그림자를 드리우지 않습니다.
     */
    UCLASS()
    class UParticleModuleTypeDataMesh final : public Engine::UParticleModuleTypeDataBase {
        GENERATED_BODY()

    public:
        UParticleModuleTypeDataMesh();

        ~UParticleModuleTypeDataMesh() override;

    public:
        std::unique_ptr<Engine::FParticleEmitterInstance> CreateInstance(const Engine::UParticleEmitter& Emitter, Engine::UParticleSystemComponent& Component) const override;

        void BakeDistributions() override;

        const Core::FString& GetMeshPath() const;

        void SetMeshPath(const Core::FString& Path);

        /**
         * @brief true면 메시 슬롯 머티리얼 대신 Required 모듈의 머티리얼로 모든 슬롯을 그립니다.
         */
        bool IsOverrideMaterial() const;

        void SetOverrideMaterial(bool OverrideMaterial);

        /**
         * @brief 생성할 때의 초기 회전(도)입니다. X = Roll, Y = Pitch, Z = Yaw입니다.
         */
        const Engine::FVectorDistribution& GetRollPitchYaw() const;

        Engine::FVectorDistribution& GetRollPitchYaw();

    private:
        UPROPERTY(Edit, Serialize, Category = "Mesh", Name = "Mesh", Meta = (AssetType = "Mesh", EmptyLabel = "None"), Getter = GetMeshPath, Setter = SetMeshPath)
        Core::FString mMeshPath{};

        UPROPERTY(Edit, Serialize, Category = "Mesh", Getter = IsOverrideMaterial, Setter = SetOverrideMaterial)
        bool mOverrideMaterial{};

        UPROPERTY(Edit, Serialize, Category = "Orientation")
        Engine::FVectorDistribution mRollPitchYaw{};
    };

}
