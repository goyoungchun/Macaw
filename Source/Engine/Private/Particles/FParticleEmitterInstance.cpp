#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleRenderData.h"
#include "Engine/Particles/ParticleHelper.h"
#include "Engine/Particles/Module/UParticleModuleSubUV.h"
#include "Engine/Particles/Module/UParticleModuleEventGenerator.h"
#include "Engine/AActor.h"
#include "Engine/Component/UPrimitiveComponent.h"
#include "Engine/Particles/UParticleEmitter.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleSpawn.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBase.h"
#include "Engine/Component/UParticleSystemComponent.h"
#include "Asset/UMaterial.h"
#include "Core/Math/FMatrix.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace Engine {

    namespace {

        /**
         * @brief 칸 번호를 16비트로 저장하므로 이보다 많이 만들 수 없습니다.
         */
        constexpr Int32 MaxParticleSlots{65535};

        constexpr Int32 InitialParticleSlots{64};

        FBox EmptyBox() {
            return FBox{FVector{FLT_MAX, FLT_MAX, FLT_MAX}, FVector{-FLT_MAX, -FLT_MAX, -FLT_MAX}};
        }

        Int32 GetLODLimit(const Engine::UParticleLODLevel& Level) {
            const Uint32 Limit{Level.GetRequiredModule()->GetMaxActiveParticles()};

            return Limit > 0 ? static_cast<Int32>(std::min<Uint32>(Limit, MaxParticleSlots)) : MaxParticleSlots;
        }

    }

    Engine::FParticleEmitterInstance::FParticleEmitterInstance() = default;

    Engine::FParticleEmitterInstance::~FParticleEmitterInstance() = default;

    void Engine::FParticleEmitterInstance::Init(const Engine::UParticleEmitter& Template, Engine::UParticleSystemComponent& Component) {
        // 다시 부르면 칸 크기가 바뀔 수 있으므로 메모리를 새로 잡습니다.
        FreeParticleMemory();

        mTemplate = &Template;
        mComponent = &Component;
        mCurrentLODIndex = 0;
        mCurrentLODLevel = Template.GetLODLevel(0);
        mParticleSize = Template.GetParticleSize();

        // 에미터는 모듈 Payload만 계산하므로 인스턴스 Payload는 그 뒤에 직접 붙입니다.
        const Int32 InstancePayloadSize{GetInstancePayloadSize()};

        mInstancePayloadOffset = InstancePayloadSize > 0 ? mParticleSize : 0;
        mParticleSize += InstancePayloadSize;
        mParticleStride = (mParticleSize + 15) & ~15;
        mParticleCounter = 0;
        mSpawnFraction = 0.0f;
        mSecondsSinceCreation = 0.0f;
        mLoopCount = 0;
        mSpawningEnabled = true;
        mBurstFired.clear();
        mEventCounters.clear();
        mBoundingBox = EmptyBox();
        mRandomStream.GenerateNewSeed();
        mSubUVDataOffset = 0;

        if (mCurrentLODLevel != nullptr) {
            for (const auto& Module : mCurrentLODLevel->GetModules()) {
                if (Module->IsA<Engine::UParticleModuleSubUV>()) {
                    mSubUVDataOffset = Template.GetModuleOffset(Module.get());

                    break;
                }
            }
        }

        // 시간이 음수인 동안(지연)은 생성하지 않습니다.
        mEmitterTime = mCurrentLODLevel != nullptr ? -mCurrentLODLevel->GetRequiredModule()->GetDelayForLoop(0).GetBase() : 0.0f;

        Resize(InitialParticleSlots);
    }

    bool Engine::FParticleEmitterInstance::Resize(Int32 MaxActiveParticles) {
        if (MaxActiveParticles > MaxParticleSlots) {
            return false;
        }

        if (MaxActiveParticles <= mMaxActiveParticles) {
            return true;
        }

        mParticleBlocks.resize(static_cast<Index_t>(MaxActiveParticles) * static_cast<Index_t>(mParticleStride) / sizeof(FParticleBlock));
        mIndexStorage.resize(static_cast<Index_t>(MaxActiveParticles) + 1);

        for (Int32 Index{mMaxActiveParticles}; Index < MaxActiveParticles; ++Index) {
            mIndexStorage[static_cast<Index_t>(Index)] = static_cast<std::uint16_t>(Index);
        }

        mParticleData = reinterpret_cast<Uint8*>(mParticleBlocks.data());
        mParticleIndices = mIndexStorage.data();
        mMaxActiveParticles = MaxActiveParticles;

        return true;
    }

    void Engine::FParticleEmitterInstance::Tick(float DeltaTime) {
        if (mTemplate == nullptr || mCurrentLODLevel == nullptr) {
            return;
        }

        UpdateEmitterTime(DeltaTime);
        KillParticles();
        ResetParticleParameters(DeltaTime);

        for (const Engine::UParticleModule* Module : mCurrentLODLevel->GetUpdateModules()) {
            if (Module->IsEnabled()) {
                Module->Update(*this, mTemplate->GetModuleOffset(Module), DeltaTime);
            }
        }

        Spawn(DeltaTime);
        UpdateBoundingBox(DeltaTime);
    }

    void Engine::FParticleEmitterInstance::SetCurrentLODIndex(Int32 LODIndex) {
        if (mTemplate == nullptr || mTemplate->GetNumLODLevels() == 0) {
            return;
        }

        const Int32 Index{std::clamp(LODIndex, 0, static_cast<Int32>(mTemplate->GetNumLODLevels()) - 1)};
        const Engine::UParticleLODLevel* Level{mTemplate->GetLODLevel(static_cast<Index_t>(Index))};

        if (Level == nullptr || Level == mCurrentLODLevel) {
            return;
        }

        mCurrentLODIndex = Index;
        mCurrentLODLevel = Level;

        if (mSecondsSinceCreation == 0.0f) {
            mEmitterTime = -Level->GetRequiredModule()->GetDelayForLoop(0).GetBase();
            mBurstFired.clear();
            return;
        }

        // Burst 기록은 항목 번호로 남습니다. 항목 수가 다른 레벨로 오면 이미 터진 Burst가 다시
        // 터지지 않도록, 이번 루프에서 시간이 지난 항목은 터진 것으로 둡니다.
        const auto& Bursts{mCurrentLODLevel->GetSpawnModule()->GetBurstList()};

        if (Bursts.size() != mBurstFired.size()) {
            mBurstFired.assign(Bursts.size(), 0);

            for (Index_t Burst{}; Burst < Bursts.size(); ++Burst) {
                mBurstFired[Burst] = mEmitterTime >= Bursts[Burst].mTime ? 1 : 0;
            }
        }
    }

    const Engine::UParticleModuleTypeDataBase* Engine::FParticleEmitterInstance::GetCurrentTypeData() const {
        const auto* Initial{mTemplate != nullptr ? mTemplate->GetLODLevel(0) : nullptr};
        const auto* Expected{Initial != nullptr ? Initial->GetTypeDataModule() : nullptr};
        const auto* Selected{mCurrentLODLevel != nullptr ? mCurrentLODLevel->GetTypeDataModule() : nullptr};

        return Expected != nullptr && Selected != nullptr && Expected->GetClass() == Selected->GetClass() ? Selected : Expected;
    }

    void Engine::FParticleEmitterInstance::FillRenderData(Engine::FParticleRenderData& Out) const {
        if (mCurrentLODLevel == nullptr || mComponent == nullptr) {
            return;
        }

        const Engine::UParticleModuleRequired& Required{*mCurrentLODLevel->GetRequiredModule()};
        const bool SubUV{Required.UsesSubUV() && mSubUVDataOffset > 0};
        const bool LocalSpace{IsLocalSpace() && mComponent != nullptr};
        const FMatrix SimulationToWorldMatrix{LocalSpace ? mComponent->GetWorldMatrix() : FMatrix::Identity()};
        // 사각형은 카메라 축으로 펼쳐 월드 행렬의 스케일을 받지 못하므로 크기에 직접 곱합니다.
        const FVector Scale{GetComponentScale()};
        Engine::FParticleSpriteBatch& Batch{Out.mSprites.emplace_back()};

        Batch.mMaterial = mComponent->ResolveMaterial(Required.GetMaterialPath());
        Batch.mSortMode = Required.GetSortMode();
        Batch.mSubImagesHorizontal = SubUV ? Required.GetSubImagesHorizontal() : 1;
        Batch.mSubImagesVertical = SubUV ? Required.GetSubImagesVertical() : 1;
        Batch.mInstances.resize(static_cast<Index_t>(mActiveParticles));

        // Freeze는 갱신만 멈추는 상태라 멈춘 자리에 계속 보여야 하므로 건너뛰지 않습니다.
        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            const Uint8* Base{mParticleData + static_cast<Index_t>(mParticleIndices[Index]) * static_cast<Index_t>(mParticleStride)};
            const Engine::FBaseParticle& Particle{*reinterpret_cast<const Engine::FBaseParticle*>(Base)};
            const FVector Position{LocalSpace ? SimulationToWorldMatrix.TransformPosition(Particle.mLocation) : Particle.mLocation};
            Engine::FParticleSpriteInstance& Instance{Batch.mInstances[static_cast<Index_t>(Index)]};

            Instance.mPosition[0] = Position.x;
            Instance.mPosition[1] = Position.y;
            Instance.mPosition[2] = Position.z;
            Instance.mRotation = Particle.mRotation;
            Instance.mSize[0] = std::abs(Particle.mSize.x * Scale.x);
            Instance.mSize[1] = std::abs(Particle.mSize.y * Scale.y);
            Instance.mSubImageIndex = SubUV ? Engine::ParticleHelper::GetPayload<Engine::FFullSubUVPayload>(Base, mSubUVDataOffset).mImageIndex : 0.0f;
            Instance.mCounter = Particle.mFlags & Engine::EParticleState::CounterMask;
            Instance.mColor[0] = Particle.mColor.x;
            Instance.mColor[1] = Particle.mColor.y;
            Instance.mColor[2] = Particle.mColor.z;
            Instance.mColor[3] = Particle.mColor.w;
        }
    }

    bool Engine::FParticleEmitterInstance::HasCompletedLoops() const {
        if (mCurrentLODLevel == nullptr) {
            return false;
        }

        const Uint32 Loops{mCurrentLODLevel->GetRequiredModule()->GetEmitterLoops()};

        return Loops > 0 && mLoopCount >= Loops;
    }

    bool Engine::FParticleEmitterInstance::IsComplete() const {
        return (HasCompletedLoops() || !mSpawningEnabled) && mActiveParticles == 0;
    }

    void Engine::FParticleEmitterInstance::KillAllParticles() {
        // 인덱스 배열은 그대로 두면 빈 칸 목록으로 다시 쓰입니다.
        mActiveParticles = 0;
    }

    void Engine::FParticleEmitterInstance::ForceSpawn(Int32 Count, const FVector& WorldLocation, const FVector& WorldVelocity) {
        if (Count <= 0 || !mSpawningEnabled || mCurrentLODLevel == nullptr || !mCurrentLODLevel->IsEnabled()) {
            return;
        }

        // Spawn()과 같은 규칙으로 현재 LOD 상한과 메모리 한도 안에서만 만듭니다.
        const Int32 LODLimit{GetLODLimit(*mCurrentLODLevel)};
        const Int32 Needed{std::min(mActiveParticles + Count, LODLimit)};

        if (Needed > mMaxActiveParticles) {
            Resize(std::min({static_cast<Int32>(static_cast<float>(Needed) * 1.5f), LODLimit, MaxParticleSlots}));
        }

        Count = std::min(Count, std::max(std::min(mMaxActiveParticles, LODLimit) - mActiveParticles, 0));

        if (Count > 0) {
            SpawnParticles(Count, 0.0f, 0.0f, WorldToSimulation(WorldLocation), WorldToSimulationVector(WorldVelocity));
        }
    }

    bool Engine::FParticleEmitterInstance::WantsEvent(Engine::EParticleEventType Type) const {
        if (mComponent == nullptr || mCurrentLODLevel == nullptr) {
            return false;
        }

        const Engine::UParticleModuleEventGenerator* Generator{mCurrentLODLevel->GetEventGenerator()};

        return Generator != nullptr && Generator->IsEnabled() && Generator->HasEvent(Type);
    }

    void Engine::FParticleEmitterInstance::ReportCollisionEvent(const Engine::FBaseParticle& Particle, const FVector& WorldLocation, const FVector& WorldNormal, float Time, Engine::UPrimitiveComponent* HitComponent, bool FirstHit, bool LastHit) {
        if (!WantsEvent(Engine::EParticleEventType::Collision)) {
            return;
        }

        Engine::FParticleEventCollisionData Event{};

        FillEventData(Event, Engine::EParticleEventType::Collision, Particle);
        Event.mLocation = WorldLocation;
        Event.mNormal = WorldNormal;
        Event.mTime = Time;
        Event.mParticleTime = Particle.mOneOverMaxLifetime > 0.0f ? Particle.mRelativeTime / Particle.mOneOverMaxLifetime : 0.0f;
        Event.mHitComponent = HitComponent;
        Event.mHitActor = HitComponent != nullptr ? HitComponent->GetOwner() : nullptr;
        SendEvent(Event, &Engine::UParticleSystemComponent::ReportEventCollision, FirstHit, LastHit);
    }

    void Engine::FParticleEmitterInstance::FillEventData(Engine::FParticleEventData& Event, Engine::EParticleEventType Type, const Engine::FBaseParticle& Particle) const {
        Event.mType = Type;
        Event.mEmitterIndex = mComponent != nullptr ? mComponent->GetEmitterIndex(this) : -1;
        Event.mEmitterTime = mEmitterTime;
        Event.mLocation = SimulationToWorld(Particle.mLocation);
        Event.mVelocity = SimulationToWorldVector(Particle.mVelocity);
        Event.mDirection = Event.mVelocity.Normalized();
    }

    template <typename TEvent>
    void Engine::FParticleEmitterInstance::SendEvent(TEvent& Event, void (Engine::UParticleSystemComponent::*Report)(const TEvent&), bool FirstHit, bool LastHit) {
        const Engine::UParticleModuleEventGenerator* Generator{mCurrentLODLevel != nullptr ? mCurrentLODLevel->GetEventGenerator() : nullptr};

        if (mComponent == nullptr || Generator == nullptr || !Generator->IsEnabled()) {
            return;
        }

        const auto& Infos{Generator->GetEvents()};

        if (mEventCounters.size() != Infos.size()) {
            mEventCounters.assign(Infos.size(), 0);
        }

        // 같은 종류의 설정이 여러 개면 설정마다 하나씩, 이름이 다른 이벤트로 보냅니다.
        for (Index_t Index{}; Index < Infos.size(); ++Index) {
            const Engine::FParticleEventGeneratorInfo& Info{Infos[Index]};

            if (Info.mType != Event.mType || (Event.mType == Engine::EParticleEventType::Collision && ((Info.mFirstTimeOnly && !FirstHit) || (Info.mLastTimeOnly && !LastHit)))) {
                continue;
            }

            if ((mEventCounters[Index]++ % std::max(Info.mFrequency, 1)) != 0) {
                continue;
            }

            Event.mEventName = Generator->GetEventName(Index);
            Event.mCustomData = Info.mCustomData;
            (mComponent->*Report)(Event);
        }
    }

    FVector Engine::FParticleEmitterInstance::GetEmitterOrigin() const {
        if (IsLocalSpace() || mComponent == nullptr) {
            return {};
        }

        return mComponent->GetWorldMatrix().TransformPosition({});
    }

    bool Engine::FParticleEmitterInstance::IsLocalSpace() const {
        return mCurrentLODLevel != nullptr && mCurrentLODLevel->GetRequiredModule()->IsUseLocalSpace();
    }

    FVector Engine::FParticleEmitterInstance::GetComponentScale() const {
        if (mComponent == nullptr) {
            return {1.0f, 1.0f, 1.0f};
        }

        const FMatrix World{mComponent->GetWorldMatrix()};

        return {World.TransformVector({1.0f, 0.0f, 0.0f}).Length(), World.TransformVector({0.0f, 1.0f, 0.0f}).Length(), World.TransformVector({0.0f, 0.0f, 1.0f}).Length()};
    }

    FVector Engine::FParticleEmitterInstance::SimulationToWorld(const FVector& Position) const {
        return IsLocalSpace() && mComponent != nullptr ? mComponent->GetWorldMatrix().TransformPosition(Position) : Position;
    }

    FVector Engine::FParticleEmitterInstance::WorldToSimulation(const FVector& Position) const {
        FMatrix Inverse{};

        return IsLocalSpace() && mComponent != nullptr && mComponent->GetWorldMatrix().TryInverse(Inverse) ? Inverse.TransformPosition(Position) : Position;
    }

    FVector Engine::FParticleEmitterInstance::EmitterToSimulationVector(const FVector& Vector) const {
        return IsLocalSpace() || mComponent == nullptr ? Vector : mComponent->GetWorldMatrix().TransformVector(Vector);
    }

    FVector Engine::FParticleEmitterInstance::WorldToSimulationVector(const FVector& Vector) const {
        FMatrix Inverse{};

        return IsLocalSpace() && mComponent != nullptr && mComponent->GetWorldMatrix().TryInverse(Inverse) ? Inverse.TransformVector(Vector) : Vector;
    }

    FVector Engine::FParticleEmitterInstance::SimulationToWorldVector(const FVector& Vector) const {
        return IsLocalSpace() && mComponent != nullptr ? mComponent->GetWorldMatrix().TransformVector(Vector) : Vector;
    }

    Engine::FBaseParticle* Engine::FParticleEmitterInstance::GetParticle(Index_t SlotIndex) const {
        return reinterpret_cast<Engine::FBaseParticle*>(mParticleData + SlotIndex * static_cast<Index_t>(mParticleStride));
    }

    Uint64 Engine::FParticleEmitterInstance::GetParticleMemoryBytes() const {
        return static_cast<Uint64>(mParticleBlocks.size()) * sizeof(FParticleBlock) + static_cast<Uint64>(mIndexStorage.size()) * sizeof(std::uint16_t);
    }

    Int32 Engine::FParticleEmitterInstance::GetInstancePayloadSize() const {
        return 0;
    }

    void Engine::FParticleEmitterInstance::UpdateEmitterTime(float DeltaTime) {
        mSecondsSinceCreation += DeltaTime;
        mEmitterTime += DeltaTime;

        const Engine::UParticleModuleRequired& Required{*mCurrentLODLevel->GetRequiredModule()};
        const float Duration{Required.GetEmitterDuration().GetBase()};

        // 길이가 0이면 루프 없이 시간이 계속 증가합니다.
        if (Duration <= 0.0f) {
            return;
        }

        // 큰 DeltaTime에 루프를 여러 번 넘을 수 있으므로 반복합니다.
        while (mEmitterTime >= Duration && !HasCompletedLoops()) {
            ++mLoopCount;
            mEmitterTime -= Duration + Required.GetDelayForLoop(mLoopCount).GetBase();
            std::ranges::fill(mBurstFired, Uint8{0});
        }
    }

    Int32 Engine::FParticleEmitterInstance::ComputeBurstCount() {
        const Engine::UParticleModuleSpawn& SpawnModule{*mCurrentLODLevel->GetSpawnModule()};
        const auto& Bursts{SpawnModule.GetBurstList()};

        if (mBurstFired.size() != Bursts.size()) {
            mBurstFired.assign(Bursts.size(), 0);
        }

        Int32 Total{};

        for (Index_t Index{}; Index < Bursts.size(); ++Index) {
            if (mBurstFired[Index] == 0 && mEmitterTime >= Bursts[Index].mTime) {
                const Int32 Count{SpawnModule.GetBurstCount(Index, mRandomStream)};

                Total += Count;
                mBurstFired[Index] = 1;

                if (WantsEvent(Engine::EParticleEventType::Burst)) {
                    // 에미터 단위 이벤트라 위치는 에미터 원점입니다.
                    Engine::FParticleEventBurstData Event{};

                    Event.mType = Engine::EParticleEventType::Burst;
                    Event.mEmitterIndex = mComponent->GetEmitterIndex(this);
                    Event.mEmitterTime = mEmitterTime;
                    Event.mLocation = mComponent->GetWorldMatrix().TransformPosition({});
                    Event.mParticleCount = Count;
                    SendEvent(Event, &Engine::UParticleSystemComponent::ReportEventBurst);
                }
            }
        }

        return Total;
    }

    void Engine::FParticleEmitterInstance::KillParticles() {
        const bool ReportDeath{WantsEvent(Engine::EParticleEventType::Death)};

        for (Int32 Index{mActiveParticles - 1}; Index >= 0; --Index) {
            const Engine::FBaseParticle* Particle{GetParticle(mParticleIndices[Index])};

            if (Particle->mRelativeTime > 1.0f) {
                // 지운 칸은 다른 파티클이 다시 쓰므로 지우기 전에 값을 복사해 보냅니다.
                if (ReportDeath) {
                    Engine::FParticleEventDeathData Event{};

                    FillEventData(Event, Engine::EParticleEventType::Death, *Particle);
                    Event.mParticleTime = Particle->mOneOverMaxLifetime > 0.0f ? Particle->mRelativeTime / Particle->mOneOverMaxLifetime : 0.0f;
                    SendEvent(Event, &Engine::UParticleSystemComponent::ReportEventDeath);
                }

                std::swap(mParticleIndices[Index], mParticleIndices[mActiveParticles - 1]);
                --mActiveParticles;
            }
        }
    }

    void Engine::FParticleEmitterInstance::ResetParticleParameters(float DeltaTime) {
        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            Engine::FBaseParticle& Particle{*GetParticle(mParticleIndices[Index])};

            Particle.mVelocity = Particle.mBaseVelocity;
            Particle.mSize = Particle.mBaseSize;
            Particle.mRotationRate = Particle.mBaseRotationRate;
            Particle.mColor = Particle.mBaseColor;
            Particle.mRelativeTime += DeltaTime * Particle.mOneOverMaxLifetime;
        }
    }

    void Engine::FParticleEmitterInstance::Spawn(float DeltaTime) {
        // 꺼진 LOD 레벨은 생성만 멈춥니다. mSpawningEnabled(비활성화)와 따로 둬야 꺼진 레벨에서
        // 돌아왔을 때 비활성화한 이펙트가 다시 살아나지 않습니다.
        if (!mSpawningEnabled || !mCurrentLODLevel->IsEnabled() || mEmitterTime < 0.0f || HasCompletedLoops()) {
            return;
        }

        const Engine::UParticleModuleSpawn& SpawnModule{*mCurrentLODLevel->GetSpawnModule()};

        if (!SpawnModule.IsEnabled()) {
            return;
        }

        const float Rate{SpawnModule.GetSpawnRate(mEmitterTime, mRandomStream)};
        const float OldLeftover{mSpawnFraction};
        const float NewLeftover{OldLeftover + DeltaTime * Rate};
        Int32 Number{static_cast<Int32>(std::floor(NewLeftover))};
        const float Increment{Rate > 0.0f ? 1.0f / Rate : 0.0f};
        const float StartTime{DeltaTime + OldLeftover * Increment - Increment};

        mSpawnFraction = NewLeftover - static_cast<float>(Number);

        Int32 BurstNumber{ComputeBurstCount()};

        // 메모리는 이전 LOD에서 더 커졌을 수 있으므로 생성 수만 현재 LOD 상한으로 자릅니다.
        const Int32 LODLimit{GetLODLimit(*mCurrentLODLevel)};
        const Int32 Needed{std::min(mActiveParticles + Number + BurstNumber, LODLimit)};

        if (Needed > mMaxActiveParticles) {
            Resize(std::min({static_cast<Int32>(static_cast<float>(Needed) * 1.5f), LODLimit, MaxParticleSlots}));
        }

        // 상한보다 많이 살아 있으면(상한이 낮은 LOD로 바뀐 직후) 줄어들 때까지 생성하지 않습니다.
        // 칸이 모자라면 초당 생성을 먼저 채우고 남은 칸에 Burst를 넣습니다.
        const Int32 SpawnLimit{std::min(mMaxActiveParticles, LODLimit)};

        Number = std::min(Number, std::max(SpawnLimit - mActiveParticles, 0));

        if (Number > 0) {
            SpawnParticles(Number, StartTime, Increment);
        }

        BurstNumber = std::min(BurstNumber, std::max(SpawnLimit - mActiveParticles, 0));

        if (BurstNumber > 0) {
            SpawnParticles(BurstNumber, 0.0f, 0.0f);
        }
    }

    void Engine::FParticleEmitterInstance::SpawnParticles(Int32 Count, float StartTime, float Increment) {
        SpawnParticles(Count, StartTime, Increment, GetEmitterOrigin(), {});
    }

    void Engine::FParticleEmitterInstance::SpawnParticles(Int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& ExtraVelocity) {
        const bool ReportSpawn{WantsEvent(Engine::EParticleEventType::Spawn)};

        for (Int32 Index{}; Index < Count; ++Index) {
            Engine::FBaseParticle& Particle{*GetParticle(mParticleIndices[mActiveParticles])};
            const float SpawnTime{StartTime - Increment * static_cast<float>(Index)};

            PreSpawn(Particle, InitialLocation);

            for (const Engine::UParticleModule* Module : mCurrentLODLevel->GetSpawnModules()) {
                if (Module->IsEnabled()) {
                    Module->Spawn(*this, mTemplate->GetModuleOffset(Module), SpawnTime, Particle);
                }
            }

            // 이벤트로 생성할 때 물려받는 속도이며 Spawn 모듈이 정한 속도 위에 더합니다.
            Particle.mVelocity += ExtraVelocity;
            Particle.mBaseVelocity += ExtraVelocity;
            PostSpawn(Particle, SpawnTime);
            ++mActiveParticles;

            if (ReportSpawn) {
                Engine::FParticleEventSpawnData Event{};

                FillEventData(Event, Engine::EParticleEventType::Spawn, Particle);
                SendEvent(Event, &Engine::UParticleSystemComponent::ReportEventSpawn);
            }
        }
    }

    void Engine::FParticleEmitterInstance::PreSpawn(Engine::FBaseParticle& Particle, const FVector& InitialLocation) {
        std::memset(static_cast<void*>(&Particle), 0, static_cast<Index_t>(mParticleStride));
        Particle.mOldLocation = InitialLocation;
        Particle.mLocation = InitialLocation;
    }

    void Engine::FParticleEmitterInstance::PostSpawn(Engine::FBaseParticle& Particle, float SpawnTime) {
        // 프레임 중간에 태어난 만큼 미리 이동합니다.
        Particle.mOldLocation = Particle.mLocation;
        Particle.mLocation += Particle.mVelocity * SpawnTime;
        Particle.mFlags |= mParticleCounter++ & Engine::EParticleState::CounterMask;
        Particle.mFlags |= Engine::EParticleState::JustSpawned;
    }

    void Engine::FParticleEmitterInstance::UpdateBoundingBox(float DeltaTime) {
        const bool LocalSpace{IsLocalSpace() && mComponent != nullptr};
        const FMatrix SimulationToWorldMatrix{LocalSpace ? mComponent->GetWorldMatrix() : FMatrix::Identity()};
        const FVector Scale{GetComponentScale()};
        const float MaximumScale{std::max({std::abs(Scale.x), std::abs(Scale.y), std::abs(Scale.z)})};
        FVector Minimum{FLT_MAX, FLT_MAX, FLT_MAX};
        FVector Maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};

        for (Int32 Index{}; Index < mActiveParticles; ++Index) {
            Engine::FBaseParticle& Particle{*GetParticle(mParticleIndices[Index])};

            Particle.mOldLocation = Particle.mLocation;

            // 이번 프레임에 생성된 파티클은 PostSpawn에서 이미 이동했습니다.
            const bool JustSpawned{(Particle.mFlags & Engine::EParticleState::JustSpawned) != 0};

            Particle.mFlags &= ~Engine::EParticleState::JustSpawned;

            // Freeze 계열은 여기서 적분하지 않아야 실제로 멈춥니다. 수명은 계속 흐릅니다.
            const bool Frozen{(Particle.mFlags & Engine::EParticleState::Freeze) != 0};

            if (!JustSpawned && !Frozen && (Particle.mFlags & Engine::EParticleState::FreezeTranslation) == 0) {
                Particle.mLocation += Particle.mVelocity * DeltaTime;
            }

            if (!JustSpawned && !Frozen && (Particle.mFlags & Engine::EParticleState::FreezeRotation) == 0) {
                Particle.mRotation += Particle.mRotationRate * DeltaTime;
            }

            const FVector Center{LocalSpace ? SimulationToWorldMatrix.TransformPosition(Particle.mLocation) : Particle.mLocation};
            const float Radius{GetParticleRadius(Particle) * MaximumScale};
            const FVector Extent{Radius, Radius, Radius};

            Minimum = Minimum.Min(Center - Extent);
            Maximum = Maximum.Max(Center + Extent);
        }

        mBoundingBox = FBox{Minimum, Maximum};
    }

    float Engine::FParticleEmitterInstance::GetParticleRadius(const Engine::FBaseParticle& Particle) const {
        // 카메라 평면 안에서 회전하는 사각형의 반대각선입니다.
        return 0.5f * std::sqrt(Particle.mSize.x * Particle.mSize.x + Particle.mSize.y * Particle.mSize.y);
    }

    void Engine::FParticleEmitterInstance::FreeParticleMemory() {
        mParticleBlocks.clear();
        mParticleBlocks.shrink_to_fit();
        mIndexStorage.clear();
        mIndexStorage.shrink_to_fit();
        mParticleData = nullptr;
        mParticleIndices = nullptr;
        mMaxActiveParticles = 0;
        mActiveParticles = 0;
    }

}
