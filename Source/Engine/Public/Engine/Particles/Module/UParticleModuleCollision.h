/**
 * @file UParticleModuleCollision.h
 * @brief 파티클의 이번 프레임 이동 경로를 물리 장면에 쓸어 닿으면 튕기는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"

#include "Core/Types.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleCollision.generated.h"

namespace Engine {

    /**
     * @brief 최대 충돌 횟수에 닿았을 때 할 일입니다.
     */
    UENUM()
    enum class EParticleCollisionComplete : Uint8 {
        Kill,
        /**
         * @brief 이동·회전·모듈 갱신을 모두 멈춥니다.
         */
        Freeze,
        /**
         * @brief 충돌 검사만 끄고 계속 움직입니다.
         */
        HaltCollisions,
        FreezeTranslation,
        FreezeRotation,
        /**
         * @brief 이동과 회전을 멈추고 모듈 갱신은 계속합니다.
         */
        FreezeMovement
    };

    /**
     * @brief 파티클마다 다른 충돌 상태입니다. 생성할 때 채웁니다.
     */
    struct FParticleCollisionPayload {
    public:
        /**
         * @brief 법선 방향 속도에 곱하는 반발 계수입니다.
         */
        float mRestitution{};

        /**
         * @brief 접선 방향 속도를 줄이는 비율입니다.
         */
        float mFriction{};

        /**
         * @brief 충돌마다 회전 속도에 곱합니다.
         */
        float mRotationDamping{};

        /**
         * @brief 이 나이(초) 전에는 충돌하지 않습니다.
         */
        float mDelay{};

        /**
         * @brief 남은 충돌 횟수이며 최대 충돌 횟수가 0이면 쓰지 않습니다.
         */
        Int32 mRemainingCollisions{};

        /**
         * @brief 이 모듈이 꺼진 LOD에서 태어난 파티클은 0이라 검사하지 않습니다.
         */
        Uint32 mInitialized{};
    };

    /**
     * @brief 모든 속도 변경 모듈 뒤의 최종 속도로 검사해야 하므로 모듈 목록 맨 아래에 둡니다.
     * @details 물리 장면의 Block 응답 형상과만 충돌하며, 충돌하면 에미터 인스턴스를 통해 Collision 이벤트를 보고합니다.
     */
    UCLASS()
    class UParticleModuleCollision final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleCollision();

        ~UParticleModuleCollision() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const override;

        void BakeDistributions() override;

        Uint32 RequiredBytes() const override;

    private:
        /**
         * @brief 0이면 달라붙고 1이면 같은 속도로 튑니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        Engine::FFloatDistribution mRestitution{};

        /**
         * @brief 0이면 계속 미끄러지고 1이면 표면 방향 속도를 모두 잃습니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        Engine::FFloatDistribution mFriction{};

        UPROPERTY(Edit, Serialize, Category = "Collision")
        Engine::FFloatDistribution mRotationDamping{};

        /**
         * @brief 생성 뒤 이 시간(초) 동안은 충돌하지 않습니다. 생성 위치가 물체 안일 때 씁니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        Engine::FFloatDistribution mDelayAmount{};

        /**
         * @brief 0이면 제한이 없습니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        Int32 mMaxCollisions{3};

        UPROPERTY(Edit, Serialize, Category = "Collision")
        Engine::EParticleCollisionComplete mCompletionOption{Engine::EParticleCollisionComplete::Kill};

        /**
         * @brief 충돌 반지름은 max(Size.X, Size.Y)에 이 값을 곱합니다. 크기는 지름이라 0.5면 스프라이트 크기입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        float mRadiusScale{0.5f};

        /**
         * @brief 튕긴 뒤 법선 방향 속력이 이보다 작으면 0으로 만들어 바닥에서 떨지 않게 합니다(m/s).
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        Core::FSpeed mMinBounceSpeed{Core::FSpeed::FromBase(0.2f)};

        /**
         * @brief 충돌 지점에서 법선 방향으로 띄우는 거리(m)이며 다음 프레임에 시작부터 겹치지 않게 합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        Core::FLength mSurfaceOffset{Core::FLength::FromBase(0.001f)};

        /**
         * @brief true면 이펙트를 낸 액터와는 충돌하지 않습니다(총구 화염과 총).
         */
        UPROPERTY(Edit, Serialize, Category = "Collision")
        bool mIgnoreSourceActor{true};
    };

}
