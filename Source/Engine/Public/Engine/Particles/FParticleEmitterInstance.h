/**
 * @file FParticleEmitterInstance.h
 * @brief 에미터 하나를 실행하는 런타임 상태와 파티클 메모리입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FRandomStream.h"
#include "Core/Math/FVector.h"
#include "Engine/Particles/FParticleEventTypes.h"

#include <cstdint>

namespace Engine {

    struct FBaseParticle;
    struct FParticleRenderData;
    class UParticleEmitter;
    class UParticleLODLevel;
    class UParticleModuleTypeDataBase;
    class UParticleSystemComponent;
    class UPrimitiveComponent;

    /**
     * @brief 에미터 템플릿(캐시된 에셋)을 읽기만 하며 파티클을 생성·갱신·제거합니다.
     * @details 파티클은 mParticleStride 간격의 고정 칸에 있고, mParticleIndices의 앞 mActiveParticles개가
     *          살아 있는 칸 번호입니다. 죽은 파티클은 인덱스 끝과 바꿔 지웁니다.
     *          Mesh·Beam·Ribbon 인스턴스가 이 클래스를 상속합니다.
     */
    class FParticleEmitterInstance {
    public:
        FParticleEmitterInstance();

        virtual ~FParticleEmitterInstance();

        FParticleEmitterInstance(const Engine::FParticleEmitterInstance&) = delete;

        Engine::FParticleEmitterInstance& operator=(const Engine::FParticleEmitterInstance&) = delete;

    public:
        /**
         * @brief 템플릿과 컴포넌트를 연결하고 파티클 메모리를 처음 크기로 잡습니다. 다시 부르면 처음부터 시작합니다.
         * @param Template Build가 끝난 에미터이며 인스턴스보다 오래 살아야 합니다.
         * @param Component 이 인스턴스를 소유한 컴포넌트입니다.
         */
        virtual void Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component);

        /**
         * @brief 파티클 칸 수를 늘립니다. 줄이지는 않습니다.
         * @return 65535를 넘으면 false입니다.
         */
        virtual bool Resize(Int32 MaxActiveParticles);

        /**
         * @brief 시간을 진행하고 죽은 파티클 제거, 모듈 갱신, 생성, 이동과 경계 갱신을 차례로 합니다.
         */
        virtual void Tick(float DeltaTime);

        /**
         * @brief 실행할 LOD 레벨을 바꿉니다. 범위 밖이면 마지막 레벨로 맞춥니다.
         * @details 살아 있는 파티클은 그대로 두고 다음 Tick부터 새 레벨의 모듈로 갱신·생성합니다.
         */
        virtual void SetCurrentLODIndex(Int32 LODIndex);

        /**
         * @brief 살아 있는 파티클을 월드 공간 그리기 데이터로 바꿔 Out 끝에 붙입니다.
         * @details 머티리얼·메시 경로는 소유 컴포넌트가 로드합니다.
         */
        virtual void FillRenderData(Engine::FParticleRenderData& Out) const;

        /**
         * @brief 루프를 모두 끝내 더 생성하지 않는 상태입니다.
         */
        bool HasCompletedLoops() const;

        /**
         * @brief 더 생성하지 않고 살아 있는 파티클도 없습니다.
         */
        bool IsComplete() const;

        /**
         * @brief 살아 있는 파티클을 모두 지웁니다. 소멸 이벤트는 보내지 않습니다.
         */
        void KillAllParticles();

        /**
         * @brief 이벤트 Receiver용입니다. 월드 위치에서 Count개를 바로 만들고 Spawn 모듈 속도 위에 WorldVelocity를 더합니다.
         * @details 비활성화했거나 현재 LOD가 꺼졌으면 만들지 않으며, 현재 LOD 상한을 넘지 않습니다.
         */
        void ForceSpawn(Int32 Count, const FVector& WorldLocation, const FVector& WorldVelocity);

        /**
         * @brief 현재 LOD의 켜진 Generator가 이 종류를 내보내면 true입니다.
         */
        bool WantsEvent(Engine::EParticleEventType Type) const;

        /**
         * @brief 충돌 모듈이 충돌을 처리할 때 부릅니다. 위치와 법선은 월드 공간입니다.
         * @param FirstHit 이 파티클의 첫 충돌이면 true입니다.
         * @param LastHit 최대 충돌 횟수에 닿은 충돌이면 true입니다.
         */
        void ReportCollisionEvent(const Engine::FBaseParticle& Particle, const FVector& WorldLocation, const FVector& WorldNormal, float Time, Engine::UPrimitiveComponent* HitComponent, bool FirstHit, bool LastHit);

        /**
         * @brief 시뮬레이션 공간의 에미터 원점입니다. Local Space면 0입니다.
         */
        FVector GetEmitterOrigin() const;

        bool IsLocalSpace() const;

        FVector GetComponentScale() const;

        FVector SimulationToWorld(const FVector& Position) const;

        FVector WorldToSimulation(const FVector& Position) const;

        /**
         * @brief 에미터 기준 방향을 시뮬레이션 공간으로 바꿉니다(회전·스케일만 적용).
         */
        FVector EmitterToSimulationVector(const FVector& Vector) const;

        FVector WorldToSimulationVector(const FVector& Vector) const;

        FVector SimulationToWorldVector(const FVector& Vector) const;

        /**
         * @brief 칸 번호의 파티클 주소입니다.
         */
        Engine::FBaseParticle* GetParticle(Index_t SlotIndex) const;

        /**
         * @brief 할당한 파티클 메모리 크기(바이트)입니다.
         */
        Uint64 GetParticleMemoryBytes() const;

    protected:
        const Engine::UParticleModuleTypeDataBase* GetCurrentTypeData() const;

        /**
         * @brief 상속한 인스턴스가 파티클마다 더 필요한 바이트 수입니다. Init 안에서 부릅니다.
         */
        virtual Int32 GetInstancePayloadSize() const;

        virtual void UpdateEmitterTime(float DeltaTime);

        Int32 ComputeBurstCount();

        virtual void KillParticles();

        /**
         * @brief 속도·크기·회전 속도·색을 기준값으로 되돌리고 수명을 진행합니다.
         */
        virtual void ResetParticleParameters(float DeltaTime);

        /**
         * @brief 이번 프레임에 생성할 개수를 정해 생성합니다.
         */
        virtual void Spawn(float DeltaTime);

        /**
         * @brief 에미터 원점에서 Count개를 생성합니다.
         * @param StartTime 첫 파티클의 생성 시점부터 프레임 끝까지 남은 시간(초)입니다.
         * @param Increment 파티클 사이 생성 간격(초)입니다.
         */
        virtual void SpawnParticles(Int32 Count, float StartTime, float Increment);

        /**
         * @brief 기준 위치와 추가 속도(시뮬레이션 공간)로 Count개를 생성합니다.
         */
        virtual void SpawnParticles(Int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& ExtraVelocity);

        virtual void PreSpawn(Engine::FBaseParticle& Particle, const FVector& InitialLocation);

        virtual void PostSpawn(Engine::FBaseParticle& Particle, float SpawnTime);

        /**
         * @brief 파티클을 이동·회전시키고 월드 공간 경계를 다시 계산합니다.
         */
        virtual void UpdateBoundingBox(float DeltaTime);

        /**
         * @brief 스케일 적용 전 파티클 중심에서 가장 먼 점까지의 거리(m)입니다.
         */
        virtual float GetParticleRadius(const Engine::FBaseParticle& Particle) const;

        void FreeParticleMemory();

        /**
         * @brief 이벤트 공통 부분(에미터, 시간, 월드 위치·속도·방향)을 파티클로 채웁니다.
         */
        void FillEventData(Engine::FParticleEventData& Event, Engine::EParticleEventType Type, const Engine::FBaseParticle& Particle) const;

        /**
         * @brief Generator 설정마다 빈도와 첫·마지막 조건을 보고 이름을 붙여 컴포넌트에 보고합니다.
         */
        template <typename TEvent>
        void SendEvent(TEvent& Event, void (Engine::UParticleSystemComponent::*Report)(const TEvent&), bool FirstHit = true, bool LastHit = true);

    private:
        /**
         * @brief 16바이트 정렬 메모리 칸입니다.
         */
        struct alignas(16) FParticleBlock {
            Uint8 mBytes[16];
        };

    public:
        const Engine::UParticleEmitter* mTemplate{};

        Engine::UParticleSystemComponent* mComponent{};

        const Engine::UParticleLODLevel* mCurrentLODLevel{};

        Int32 mCurrentLODIndex{};

        /**
         * @brief 파티클 칸 메모리의 시작 주소이며 mParticleBlocks를 가리킵니다.
         */
        Uint8* mParticleData{};

        /**
         * @brief 살아 있는 칸 번호 목록이며 앞 mActiveParticles개가 유효합니다.
         */
        std::uint16_t* mParticleIndices{};

        /**
         * @brief 에미터가 계산한 크기에 인스턴스 Payload를 더한 크기입니다.
         */
        Int32 mParticleSize{};

        /**
         * @brief 16바이트로 올림한 실제 칸 간격입니다.
         */
        Int32 mParticleStride{};

        Int32 mActiveParticles{};

        Int32 mMaxActiveParticles{};

        /**
         * @brief 생성 순번이며 mFlags 하위 비트에 기록됩니다.
         */
        Uint32 mParticleCounter{};

        /**
         * @brief 인스턴스 전용 Payload 시작 위치입니다. 0이면 없습니다.
         */
        Int32 mInstancePayloadOffset{};

        /**
         * @brief SubUV 모듈 Payload 위치입니다. 0이면 SubUV 모듈이 없습니다.
         */
        Int32 mSubUVDataOffset{};

        /**
         * @brief 초당 생성에서 남은 소수 부분입니다.
         */
        float mSpawnFraction{};

        /**
         * @brief 이번 루프 안의 시간(초)이며 지연 중에는 음수입니다.
         */
        float mEmitterTime{};

        float mSecondsSinceCreation{};

        /**
         * @brief 끝낸 루프 수입니다.
         */
        Uint32 mLoopCount{};

        /**
         * @brief false면 생성만 멈추고 살아 있는 파티클은 계속 갱신합니다.
         */
        bool mSpawningEnabled{true};

        /**
         * @brief Burst마다 이번 루프에 터졌는지입니다.
         */
        Core::TArray<Uint8> mBurstFired{};

        FRandomStream mRandomStream{};

        /**
         * @brief 파티클 크기와 컴포넌트 스케일까지 포함한 월드 공간 경계입니다.
         */
        FBox mBoundingBox{};

    private:
        Core::TArray<FParticleBlock> mParticleBlocks{};

        Core::TArray<std::uint16_t> mIndexStorage{};

        /**
         * @brief Generator 설정마다 지금까지 일어난 사건 수이며 빈도 판단에 씁니다.
         */
        Core::TArray<Int32> mEventCounters{};
    };

}
