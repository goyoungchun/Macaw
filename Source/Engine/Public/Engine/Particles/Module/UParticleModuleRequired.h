/**
 * @file UParticleModuleRequired.h
 * @brief 에미터 전체의 루프·공간·머티리얼 규칙을 담는 필수 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "Engine/Particles/UParticleModule.h"
#include "UParticleModuleRequired.generated.h"

namespace Engine {

    /**
     * @brief SubUV 칸을 고르는 방식입니다.
     */
    UENUM()
    enum class EParticleSubUVInterpMethod : Uint8 {
        None,
        Linear,
        LinearBlend,
        Random,
        RandomBlend
    };

    /**
     * @brief 반투명 파티클을 그리는 순서입니다.
     */
    UENUM()
    enum class EParticleSortMode : Uint8 {
        None,
        ViewProjDepth,
        DistanceToView,
        AgeOldestFirst,
        AgeNewestFirst
    };

    /**
     * @brief 에미터 전체 규칙입니다. 파티클을 직접 바꾸지 않으며 LOD 레벨의 Required 슬롯에만 둡니다.
     */
    UCLASS()
    class UParticleModuleRequired final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleRequired();

        ~UParticleModuleRequired() override;

    public:
        Engine::EParticleModuleType GetModuleType() const override;

        /**
         * @brief 한 루프의 길이이며 0이면 루프 없이 계속 생성합니다.
         */
        Core::FTime GetEmitterDuration() const;

        bool SetEmitterDuration(Core::FTime Duration);

        /**
         * @brief 반복 횟수이며 0이면 무한입니다.
         */
        Uint32 GetEmitterLoops() const;

        void SetEmitterLoops(Uint32 Loops);

        /**
         * @brief 루프 시작 전 생성하지 않고 기다리는 시간입니다.
         */
        Core::FTime GetEmitterDelay() const;

        bool SetEmitterDelay(Core::FTime Delay);

        bool IsDelayFirstLoopOnly() const;

        void SetDelayFirstLoopOnly(bool FirstLoopOnly);

        /**
         * @brief 이번 루프에 적용할 지연입니다.
         * @param LoopIndex 0부터 세는 루프 번호입니다.
         */
        Core::FTime GetDelayForLoop(Uint32 LoopIndex) const;

        /**
         * @brief true면 파티클이 컴포넌트 공간에서 시뮬레이션되어 컴포넌트를 따라 움직입니다.
         */
        bool IsUseLocalSpace() const;

        void SetUseLocalSpace(bool UseLocalSpace);

        /**
         * @brief 동시에 살아 있을 수 있는 최대 수이며 0이면 메모리 한도(65535)까지입니다.
         */
        Uint32 GetMaxActiveParticles() const;

        void SetMaxActiveParticles(Uint32 MaxActiveParticles);

        const Core::FString& GetMaterialPath() const;

        void SetMaterialPath(const Core::FString& Path);

        Engine::EParticleSortMode GetSortMode() const;

        void SetSortMode(Engine::EParticleSortMode SortMode);

        Engine::EParticleSubUVInterpMethod GetSubUVMethod() const;

        Uint32 GetSubImagesHorizontal() const;

        Uint32 GetSubImagesVertical() const;

        Uint32 GetTotalSubImages() const;

        /**
         * @brief Random 방식에서 칸을 다시 뽑는 간격(수명 비율)입니다. 0이면 매 프레임, 1 이상이면 생성 때 한 번입니다.
         */
        float GetRandomImageTime() const;

        bool UsesSubUV() const;

        /**
         * @brief SubUV 설정을 한 번에 바꿉니다. 칸 수는 최소 1입니다.
         */
        void SetSubUV(Engine::EParticleSubUVInterpMethod Method, Uint32 Horizontal, Uint32 Vertical, float RandomImageTime);

    private:
        UPROPERTY(Edit, Serialize, Category = "Duration", Getter = GetEmitterDuration, Setter = SetEmitterDuration)
        Core::FTime mEmitterDuration{};

        UPROPERTY(Edit, Serialize, Category = "Duration", Getter = GetEmitterLoops, Setter = SetEmitterLoops)
        Uint32 mEmitterLoops{};

        UPROPERTY(Edit, Serialize, Category = "Delay", Getter = GetEmitterDelay, Setter = SetEmitterDelay)
        Core::FTime mEmitterDelay{};

        UPROPERTY(Edit, Serialize, Category = "Delay", Getter = IsDelayFirstLoopOnly, Setter = SetDelayFirstLoopOnly)
        bool mDelayFirstLoopOnly{};

        UPROPERTY(Edit, Serialize, Category = "Emitter", Getter = IsUseLocalSpace, Setter = SetUseLocalSpace)
        bool mUseLocalSpace{};

        UPROPERTY(Edit, Serialize, Category = "Emitter", Getter = GetMaxActiveParticles, Setter = SetMaxActiveParticles)
        Uint32 mMaxActiveParticles{};

        UPROPERTY(Edit, Serialize, Category = "Emitter", Name = "Material", Meta = (AssetType = "Material", EmptyLabel = "Default particle material"), Getter = GetMaterialPath, Setter = SetMaterialPath)
        Core::FString mMaterialPath{};

        UPROPERTY(Edit, Serialize, Category = "Rendering", Getter = GetSortMode, Setter = SetSortMode)
        Engine::EParticleSortMode mSortMode{Engine::EParticleSortMode::None};

        UPROPERTY(Edit, Serialize, Category = "SubUV")
        Engine::EParticleSubUVInterpMethod mInterpolationMethod{Engine::EParticleSubUVInterpMethod::None};

        UPROPERTY(Edit, Serialize, Category = "SubUV")
        Uint32 mSubImagesHorizontal{1};

        UPROPERTY(Edit, Serialize, Category = "SubUV")
        Uint32 mSubImagesVertical{1};

        UPROPERTY(Edit, Serialize, Category = "SubUV")
        float mRandomImageTime{};
    };

}
