/**
 * @file UParticleSystem.h
 * @brief 에미터 목록과 LOD 설정을 담는 파티클 시스템 에셋입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "CoreUObject/UObject.h"
#include "CoreUObject/Reflection/TNativeEnum.h"

#include <memory>

#include "UParticleSystem.generated.h"

namespace Engine {

    class UParticleEmitter;

    /**
     * @brief 실행할 LOD 레벨을 고르는 방식입니다.
     */
    UENUM()
    enum class EParticleSystemLODMethod : Uint8 {
        /**
         * @brief 일정 간격으로 카메라 거리를 재서 고릅니다.
         */
        Automatic,
        /**
         * @brief 게임 코드가 직접 정합니다.
         */
        DirectSet,
        /**
         * @brief 활성화할 때 한 번만 거리로 고릅니다.
         */
        ActivateAutomatic
    };

    /**
     * @brief 파티클 시스템 에셋입니다. 에미터 배열 순서가 그리는 순서입니다.
     * @details 에셋 캐시에 올라간 객체는 바꾸지 않습니다. 에디터는 Duplicate()로 만든 복사본을 편집합니다.
     *          구조를 바꾸는 함수(에미터·LOD 추가와 삭제)를 부른 뒤에는 BuildEmitters()를 다시 불러야 하며,
     *          이 객체로 실행 중인 컴포넌트는 다시 초기화해야 합니다.
     */
    UCLASS()
    class UParticleSystem final : public CoreUObject::UObject {
        GENERATED_BODY()

    public:
        UParticleSystem();

        ~UParticleSystem() override;

    public:
        /**
         * @brief 기본 에미터 하나를 가진 새 에셋 내용으로 채웁니다.
         */
        void InitializeDefault();

        /**
         * @brief LOD 설정을 에미터 LOD 수에 맞추고 모든 에미터를 Build합니다.
         */
        void BuildEmitters();

        /**
         * @brief 직렬화로 깊게 복사한 편집용 복사본입니다. Build까지 끝냅니다.
         * @return 실패하면 nullptr입니다.
         */
        std::unique_ptr<Engine::UParticleSystem> Duplicate(Core::FString& Error) const;

        /**
         * @brief LOD 수이며 LODDistances 항목 수와 같습니다.
         */
        Index_t GetNumLODs() const;

        /**
         * @brief LOD i를 쓰기 시작하는 카메라 거리(m)입니다. [0]은 항상 0입니다.
         */
        const Core::TArray<Core::FLength>& GetLODDistances() const;

        Core::FLength GetLODDistance(Index_t Index) const;

        /**
         * @brief LOD 0의 거리는 바꿀 수 없습니다.
         */
        void SetLODDistance(Index_t Index, Core::FLength Distance);

        Engine::EParticleSystemLODMethod GetLODMethod() const;

        void SetLODMethod(Engine::EParticleSystemLODMethod Method);

        /**
         * @brief Automatic에서 거리를 다시 재는 간격입니다.
         */
        Core::FTime GetLODDistanceCheckTime() const;

        bool SetLODDistanceCheckTime(Core::FTime Interval);

        /**
         * @brief Index - 1 레벨을 복사해 Index에 넣습니다(1 <= Index <= GetNumLODs()). 모든 에미터에 적용합니다.
         */
        bool InsertLOD(Index_t Index);

        /**
         * @brief LOD 0은 지울 수 없습니다(1 <= Index < GetNumLODs()). 모든 에미터에 적용합니다.
         */
        bool RemoveLOD(Index_t Index);

        const Core::TArray<std::unique_ptr<Engine::UParticleEmitter>>& GetEmitters() const;

        /**
         * @return 범위 밖이면 nullptr입니다.
         */
        Engine::UParticleEmitter* GetEmitter(Index_t Index) const;

        Index_t GetNumEmitters() const;

        /**
         * @return 없으면 -1입니다.
         */
        Int32 FindEmitterIndex(const Engine::UParticleEmitter* Emitter) const;

        void AddEmitter(std::unique_ptr<Engine::UParticleEmitter> Emitter);

        /**
         * @brief 지정 위치에 넣습니다. 범위를 넘으면 끝에 넣습니다.
         */
        bool InsertEmitter(std::unique_ptr<Engine::UParticleEmitter> Emitter, Index_t Index);

        /**
         * @brief 배열에서 빼서 돌려줍니다.
         * @return 없으면 nullptr입니다.
         */
        std::unique_ptr<Engine::UParticleEmitter> RemoveEmitter(const Engine::UParticleEmitter* Emitter);

        bool MoveEmitter(Index_t From, Index_t To);

        void ClearEmitters();

        void Serialize(Core::FArchive& Archive) override;

    private:
        /**
         * @brief LODDistances를 에미터 LOD 수에 맞추고 어긋난 설정을 경고합니다.
         */
        void FixupLODSettings();

    private:
        Core::TArray<std::unique_ptr<Engine::UParticleEmitter>> mEmitters{};

        /**
         * @brief LOD i는 카메라 거리가 mLODDistances[i](m) 이상일 때 씁니다. [0]은 항상 0입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "LOD")
        Core::TArray<Core::FLength> mLODDistances{};

        UPROPERTY(Edit, Serialize, Category = "LOD", Name = "LODMethod", Getter = GetLODMethod, Setter = SetLODMethod)
        Engine::EParticleSystemLODMethod mLODMethod{Engine::EParticleSystemLODMethod::Automatic};

        UPROPERTY(Edit, Serialize, Category = "LOD", Name = "LODDistanceCheckTime", Getter = GetLODDistanceCheckTime, Setter = SetLODDistanceCheckTime)
        Core::FTime mLODDistanceCheckTime{};
    };

}
