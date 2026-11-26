/**
 * @file UParticleModuleTypeDataBeam.h
 * @brief 에미터에서 목표까지 띠를 그리는 빔 에미터 TypeData입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"

#include "Core/Types.h"
#include "Core/Math/FVector.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBase.h"
#include "UParticleModuleTypeDataBeam.generated.h"

namespace Engine {

    /**
     * @brief 빔 끝점을 정하는 방식입니다.
     */
    UENUM()
    enum class EBeamMethod : Uint8 {
        /**
         * @brief 에미터 X축 방향으로 Distance만큼 갑니다.
         */
        Distance,
        /**
         * @brief 에미터 기준 TargetPoint로 갑니다.
         */
        Target
    };

    /**
     * @brief 빔 에미터 설정입니다. 파티클 하나가 빔 하나입니다.
     */
    UCLASS()
    class UParticleModuleTypeDataBeam final : public Engine::UParticleModuleTypeDataBase {
        GENERATED_BODY()

    public:
        UParticleModuleTypeDataBeam();

        ~UParticleModuleTypeDataBeam() override;

    public:
        std::unique_ptr<Engine::FParticleEmitterInstance> CreateInstance(const Engine::UParticleEmitter& Emitter, Engine::UParticleSystemComponent& Component) const override;

        Engine::EParticleModuleType GetModuleType() const override;

        Engine::EBeamMethod GetBeamMethod() const;

        /**
         * @brief 동시에 있을 수 있는 빔 수이며 최소 1입니다.
         */
        Uint32 GetMaxBeamCount() const;

        /**
         * @brief 빔을 축 둘레로 나눠 겹쳐 그릴 판 수이며 최소 1입니다.
         */
        Uint32 GetSheets() const;

        /**
         * @brief 노이즈가 없을 때 빔을 나누는 칸 수이며 최소 1입니다.
         */
        Uint32 GetSegments() const;

        /**
         * @brief 빔 전체 길이에 텍스처가 반복되는 횟수입니다.
         */
        float GetTextureTile() const;

        /**
         * @brief Distance 방식의 빔 길이(m)입니다.
         */
        Core::FLength GetDistance() const;

        /**
         * @brief Target 방식의 에미터 기준 끝점(m)입니다.
         */
        const FVector& GetTargetPoint() const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Beam")
        Engine::EBeamMethod mBeamMethod{Engine::EBeamMethod::Distance};

        UPROPERTY(Edit, Serialize, Category = "Beam")
        Uint32 mMaxBeamCount{1};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        Uint32 mSheets{1};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        Uint32 mSegments{10};

        UPROPERTY(Edit, Serialize, Category = "Rendering")
        float mTextureTile{1.0f};

        UPROPERTY(Edit, Serialize, Category = "Beam")
        Core::FLength mDistance{Core::FLength::FromBase(1.0f)};

        UPROPERTY(Edit, Serialize, Category = "Beam")
        FVector mTargetPoint{1.0f, 0.0f, 0.0f};
    };

}
