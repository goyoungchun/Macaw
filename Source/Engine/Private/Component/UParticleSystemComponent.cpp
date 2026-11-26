#include "Engine/Component/UParticleSystemComponent.h"
#include "Engine/Component/UCameraComponent.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/FParticleRenderData.h"
#include "Engine/Particles/ParticleAssets.h"
#include "Engine/Particles/UParticleEmitter.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/UParticleSystem.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleEventReceiverBase.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Engine/UWorld.h"
#include "Asset/FAssetManager.h"
#include "Asset/FMeshData.h"
#include "Asset/UMaterial.h"
#include "Asset/UStaticMesh.h"
#include "Core/FilePath.h"
#include "Core/Log/Log.h"

#include <algorithm>
#include <cfloat>

DEFINE_LOG_CATEGORY_STATIC(LogParticles, Log, All);

namespace Engine {

    namespace {

        /**
         * @brief 파티클이 없을 때 쓰는 경계 상자의 반 크기(m)입니다.
         */
        constexpr float EmptyBoundsHalfExtent{0.1f};

    }

    Engine::UParticleSystemComponent::UParticleSystemComponent() {
        // 에디터 월드에서도 배치한 자리에서 미리 보이도록 Tick합니다.
        SetEditorTickable(true);
    }

    Engine::UParticleSystemComponent::~UParticleSystemComponent() = default;

    const Core::FString& Engine::UParticleSystemComponent::GetTemplatePath() const {
        return mTemplatePath;
    }

    bool Engine::UParticleSystemComponent::SetTemplatePath(const Core::FString& Path) {
        if (Path == mTemplatePath) {
            return true;
        }

        mTemplatePath = Path;

        if (IsRegistered()) {
            ResolveTemplate();
            RestartForTemplate();
        }

        return true;
    }

    bool Engine::UParticleSystemComponent::SetPreviewTemplate(Engine::UParticleSystem* Template) {
        const Engine::UWorld* World{GetWorld()};

        if (World == nullptr || World->GetWorldType() != Engine::EWorldType::EditorPreview || !IsRegistered() || !mTemplatePath.empty()) {
            return false;
        }

        mTemplate = Template;
        mMaterials.clear();
        mMeshes.clear();
        RestartForTemplate();

        return true;
    }

    Engine::UParticleSystem* Engine::UParticleSystemComponent::GetTemplate() const {
        return mTemplate.Get();
    }

    bool Engine::UParticleSystemComponent::IsAutoActivate() const {
        return mAutoActivate;
    }

    void Engine::UParticleSystemComponent::SetAutoActivate(bool AutoActivate) {
        mAutoActivate = AutoActivate;
    }

    void Engine::UParticleSystemComponent::InitializeSystem() {
        ResetParticles();
        mInitializedTemplate = mTemplate;

        if (mTemplate == nullptr) {
            return;
        }

        for (const auto& Emitter : mTemplate->GetEmitters()) {
            if (auto Instance{Emitter->CreateInstance(*this)}) {
                mEmitterInstances.push_back(std::move(Instance));
            }
        }

        mWasCompleted = false;

        // 이전 템플릿에서 쌓인 이벤트를 새 인스턴스로 배달하지 않습니다.
        ClearEvents();

        // 새 인스턴스는 LOD 0으로 시작하므로 지금 LOD를 다시 적용합니다.
        ApplyLODLevel(IsPreviewLODActive() ? mPreviewLODOverride : mLODLevel);
        UpdateRenderData();
    }

    void Engine::UParticleSystemComponent::ActivateSystem(bool Reset) {
        // 인스턴스가 다른 템플릿으로 만들어졌으면 다시 만듭니다.
        if (mEmitterInstances.empty() || Reset || mInitializedTemplate.Get() != mTemplate.Get()) {
            InitializeSystem();
        }

        for (const auto& Instance : mEmitterInstances) {
            Instance->mSpawningEnabled = true;
        }

        mWasCompleted = false;
        mLODCheckTimer = {};
        mPendingActivateLOD = false;

        // 멀리서 터진 이펙트가 LOD 0 Burst로 시작하지 않도록 첫 Tick 전에 거리로 정합니다.
        if (mTemplate != nullptr && !IsPreviewLODActive() && mTemplate->GetLODMethod() != Engine::EParticleSystemLODMethod::DirectSet) {
            const Int32 Desired{ComputeDesiredLOD()};

            if (Desired >= 0) {
                ApplyLODLevel(Desired);
            } else {
                // 아직 뷰가 없으면 뷰가 생기는 첫 Tick에서 정합니다.
                mPendingActivateLOD = true;
            }
        }
    }

    void Engine::UParticleSystemComponent::DeactivateSystem() {
        for (const auto& Instance : mEmitterInstances) {
            Instance->mSpawningEnabled = false;
        }
    }

    void Engine::UParticleSystemComponent::ResetParticles() {
        mEmitterInstances.clear();
        UpdateRenderData();
    }

    bool Engine::UParticleSystemComponent::HasCompleted() const {
        return mWasCompleted;
    }

    const Core::TArray<std::unique_ptr<Engine::FParticleEmitterInstance>>& Engine::UParticleSystemComponent::GetEmitterInstances() const {
        return mEmitterInstances;
    }

    Int32 Engine::UParticleSystemComponent::GetEmitterIndex(const Engine::FParticleEmitterInstance* Instance) const {
        for (Index_t Index{}; Index < mEmitterInstances.size(); ++Index) {
            if (mEmitterInstances[Index].get() == Instance) {
                return static_cast<Int32>(Index);
            }
        }

        return -1;
    }

    Int32 Engine::UParticleSystemComponent::GetLODLevel() const {
        return mLODLevel;
    }

    void Engine::UParticleSystemComponent::SetLODLevel(Int32 LODLevel) {
        ApplyLODLevel(LODLevel);
    }

    Int32 Engine::UParticleSystemComponent::GetPreviewLODOverride() const {
        return mPreviewLODOverride;
    }

    void Engine::UParticleSystemComponent::SetPreviewLODOverride(Int32 LODLevel) {
        mPreviewLODOverride = LODLevel < 0 ? -1 : LODLevel;

        if (IsPreviewLODActive()) {
            ApplyLODLevel(mPreviewLODOverride);
        } else if (mTemplate != nullptr) {
            // 미리보기를 끄면 다음 Tick에서 바로 거리로 다시 고르게 합니다.
            mLODCheckTimer = mTemplate->GetLODDistanceCheckTime();
        }
    }

    Int32 Engine::UParticleSystemComponent::GetActiveParticleCount() const {
        Int32 Count{};

        for (const auto& Instance : mEmitterInstances) {
            Count += Instance->mActiveParticles;
        }

        return Count;
    }

    void Engine::UParticleSystemComponent::Tick(float DeltaTime) {
        Engine::UPrimitiveComponent::Tick(DeltaTime);

        // 경로만 바뀌고 인스턴스가 옛 템플릿으로 남은 경우(복제·로드 직후)를 맞춥니다.
        if (mInitializedTemplate.Get() != mTemplate.Get()) {
            RestartForTemplate();
        }

        if (mEmitterInstances.empty()) {
            return;
        }

        // 인스턴스가 이번 Tick을 새 LOD의 모듈로 돌도록 먼저 고릅니다.
        UpdateLODLevel(Core::FTime::FromBase(DeltaTime));

        bool AllComplete{true};

        for (const auto& Instance : mEmitterInstances) {
            Instance->Tick(DeltaTime);
            AllComplete = AllComplete && Instance->IsComplete();
        }

        mWasCompleted = AllComplete;

        // 모든 에미터 Tick이 끝난 뒤 배달해야 Receiver가 순회 중인 파티클 배열을 건드리지 않습니다.
        DispatchEvents();
        UpdateRenderData();

        // 게임 코드 콜백은 이 컴포넌트를 바꾸거나 파괴할 수 있으므로 맨 마지막입니다.
        BroadcastGameEvents();
    }

    void Engine::UParticleSystemComponent::UnregisterComponent() {
        ResetParticles();
        Engine::UPrimitiveComponent::UnregisterComponent();
    }

    Engine::EPrimitiveType Engine::UParticleSystemComponent::GetPrimitiveType() const {
        return Engine::EPrimitiveType::Plane;
    }

    const Asset::FMeshData& Engine::UParticleSystemComponent::GetMeshData() const {
        static const Asset::FMeshData Empty{};

        return Empty;
    }

    FBox Engine::UParticleSystemComponent::GetWorldBounds() const {
        // 인스턴스가 자기 공간(Local/World)에 맞춰 월드 상자를 만들어 두므로 합치기만 합니다.
        FVector Minimum{FLT_MAX, FLT_MAX, FLT_MAX};
        FVector Maximum{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        bool Any{};

        for (const auto& Instance : mEmitterInstances) {
            if (Instance->mActiveParticles == 0) {
                continue;
            }

            Minimum = Minimum.Min(Instance->mBoundingBox.mMinimum);
            Maximum = Maximum.Max(Instance->mBoundingBox.mMaximum);
            Any = true;
        }

        if (!Any) {
            const FVector Location{GetWorldMatrix().TransformPosition({})};
            const FVector Extent{EmptyBoundsHalfExtent, EmptyBoundsHalfExtent, EmptyBoundsHalfExtent};

            return FBox{Location - Extent, Location + Extent};
        }

        return FBox{Minimum, Maximum};
    }

    bool Engine::UParticleSystemComponent::ShouldCreateRenderState() const {
        return mTemplate != nullptr;
    }

    Engine::FPrimitiveSceneProxy Engine::UParticleSystemComponent::CreateSceneProxy() const {
        Engine::FPrimitiveSceneProxy Proxy{Engine::UPrimitiveComponent::CreateSceneProxy()};

        // 반투명 스프라이트라 그림자·데칼과 섞이지 않습니다.
        Proxy.mCastShadow = false;
        Proxy.mReceiveShadow = false;
        Proxy.mReceivesDecals = false;
        Proxy.mParticles = mRenderData;

        return Proxy;
    }

    void Engine::UParticleSystemComponent::OnRegister() {
        ResolveTemplate();
        RestartForTemplate();
    }

    void Engine::UParticleSystemComponent::ResolveTemplate() {
        mTemplate.Reset();
        mMaterials.clear();
        mMeshes.clear();

        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

        if (mTemplatePath.empty() || Assets == nullptr) {
            return;
        }

        Core::FString Error{};

        mTemplate = Engine::LoadParticleSystem(*Assets, Core::FilePath::FromUtf8(mTemplatePath), Error);

        if (mTemplate == nullptr) {
            MACAW_LOG(LogParticles, Warning, "Cannot load particle system '{}': {}", mTemplatePath, Error);
        }
    }

    void Engine::UParticleSystemComponent::RestartForTemplate() {
        if (mAutoActivate) {
            ActivateSystem(true);
        } else {
            InitializeSystem();
            DeactivateSystem();
        }
    }

    void Engine::UParticleSystemComponent::UpdateLODLevel(Core::FTime DeltaTime) {
        if (mTemplate == nullptr || IsPreviewLODActive()) {
            return;
        }

        if (mPendingActivateLOD) {
            const Int32 Desired{ComputeDesiredLOD()};

            if (Desired >= 0) {
                ApplyLODLevel(Desired);
                mPendingActivateLOD = false;
                mLODCheckTimer = {};
            }

            return;
        }

        // DirectSet은 코드가 정한 값, ActivateAutomatic은 활성화 때 정한 값을 유지합니다.
        if (mTemplate->GetLODMethod() != Engine::EParticleSystemLODMethod::Automatic) {
            return;
        }

        mLODCheckTimer += DeltaTime;

        if (mLODCheckTimer < mTemplate->GetLODDistanceCheckTime()) {
            return;
        }

        mLODCheckTimer = {};

        const Int32 Desired{ComputeDesiredLOD()};

        if (Desired >= 0 && Desired != mLODLevel) {
            ApplyLODLevel(Desired);
        }
    }

    void Engine::UParticleSystemComponent::ApplyLODLevel(Int32 LODLevel) {
        const Int32 Count{mTemplate != nullptr ? static_cast<Int32>(mTemplate->GetNumLODs()) : 1};

        mLODLevel = std::clamp(LODLevel, 0, std::max(Count - 1, 0));

        for (const auto& Instance : mEmitterInstances) {
            Instance->SetCurrentLODIndex(mLODLevel);
        }
    }

    Int32 Engine::UParticleSystemComponent::ComputeDesiredLOD() const {
        const Engine::UWorld* World{GetWorld()};
        const Engine::UCameraComponent* Camera{World != nullptr ? World->GetActiveCamera() : nullptr};

        // 지금은 월드의 활성 카메라만 뷰로 봅니다. 활성 카메라가 없는 편집 월드는 LOD를 바꾸지 않습니다.
        if (mTemplate == nullptr || Camera == nullptr) {
            return -1;
        }

        const FVector ViewLocation{Camera->GetWorldMatrix().TransformPosition({})};
        const Core::FArea DistanceSquared{Core::FArea::FromBase(ViewLocation.DistanceSquared(GetWorldMatrix().TransformPosition({})))};
        const auto& Distances{mTemplate->GetLODDistances()};

        for (Index_t Index{Distances.size()}; Index-- > 1;) {
            if (DistanceSquared >= Distances[Index] * Distances[Index]) {
                return static_cast<Int32>(Index);
            }
        }

        return 0;
    }

    bool Engine::UParticleSystemComponent::IsPreviewLODActive() const {
        const Engine::UWorld* World{GetWorld()};

        return mPreviewLODOverride >= 0 && (World == nullptr || World->GetWorldType() == Engine::EWorldType::Editor || World->GetWorldType() == Engine::EWorldType::EditorPreview);
    }

    void Engine::UParticleSystemComponent::UpdateRenderData() {
        if (mEmitterInstances.empty()) {
            mRenderData.reset();
        } else {
            auto Data{std::make_shared<Engine::FParticleRenderData>()};

            for (const auto& Instance : mEmitterInstances) {
                Instance->FillRenderData(*Data);
            }

            mRenderData = std::move(Data);
        }

        MarkRenderStateDirty();
    }

    Asset::UMaterial* Engine::UParticleSystemComponent::ResolveMaterial(const Core::FString& Path) {
        if (Path.empty()) {
            return nullptr;
        }

        if (const auto Found{mMaterials.find(Path)}; Found != mMaterials.end()) {
            return Found->second.Get();
        }

        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};
        Asset::UMaterial* Material{};

        if (Assets != nullptr) {
            Core::FString Error{};

            Material = Assets->LoadMaterial(Core::FilePath::FromUtf8(Path), Error);

            if (Material == nullptr) {
                MACAW_LOG(LogParticles, Warning, "Cannot load particle material '{}': {}", Path, Error);
            }
        }

        // 실패한 경로도 기억해 매 Tick 다시 로드하지 않습니다.
        mMaterials.emplace(Path, Material);

        return Material;
    }

    Asset::UStaticMesh* Engine::UParticleSystemComponent::ResolveMesh(const Core::FString& Path) {
        if (Path.empty()) {
            return nullptr;
        }

        if (const auto Found{mMeshes.find(Path)}; Found != mMeshes.end()) {
            return Found->second.Get();
        }

        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};
        Asset::UStaticMesh* Mesh{};

        if (Assets != nullptr) {
            Core::FString Error{};

            Mesh = Assets->LoadMesh(Core::FilePath::FromUtf8(Path), Error);

            if (Mesh == nullptr) {
                MACAW_LOG(LogParticles, Warning, "Cannot load particle mesh '{}': {}", Path, Error);
            }
        }

        mMeshes.emplace(Path, Mesh);

        return Mesh;
    }

    void Engine::UParticleSystemComponent::ReportEventSpawn(const Engine::FParticleEventSpawnData& Event) {
        mSpawnEvents.push_back(Event);
    }

    void Engine::UParticleSystemComponent::ReportEventDeath(const Engine::FParticleEventDeathData& Event) {
        mDeathEvents.push_back(Event);
    }

    void Engine::UParticleSystemComponent::ReportEventCollision(const Engine::FParticleEventCollisionData& Event) {
        mCollisionEvents.push_back(Event);
    }

    void Engine::UParticleSystemComponent::ReportEventBurst(const Engine::FParticleEventBurstData& Event) {
        mBurstEvents.push_back(Event);
    }

    Engine::UParticleSystemComponent::FParticleSpawnDelegate& Engine::UParticleSystemComponent::GetOnParticleSpawn() {
        return mOnParticleSpawn;
    }

    Engine::UParticleSystemComponent::FParticleDeathDelegate& Engine::UParticleSystemComponent::GetOnParticleDeath() {
        return mOnParticleDeath;
    }

    Engine::UParticleSystemComponent::FParticleCollideDelegate& Engine::UParticleSystemComponent::GetOnParticleCollide() {
        return mOnParticleCollide;
    }

    Engine::UParticleSystemComponent::FParticleBurstDelegate& Engine::UParticleSystemComponent::GetOnParticleBurst() {
        return mOnParticleBurst;
    }

    void Engine::UParticleSystemComponent::DispatchEvents() {
        // 지난 Tick에 배달한 목록이 이번 Tick 델리게이트로 다시 나가지 않게 먼저 비웁니다.
        mDispatchingSpawnEvents.clear();
        mDispatchingDeathEvents.clear();
        mDispatchingCollisionEvents.clear();
        mDispatchingBurstEvents.clear();

        if (mSpawnEvents.empty() && mDeathEvents.empty() && mCollisionEvents.empty() && mBurstEvents.empty()) {
            return;
        }

        // 배달할 목록을 꺼내고 버퍼를 비웁니다. 배달 중 새로 생긴 이벤트는 버퍼에 쌓여 다음 Tick에 배달됩니다.
        mDispatchingSpawnEvents.swap(mSpawnEvents);
        mDispatchingDeathEvents.swap(mDeathEvents);
        mDispatchingCollisionEvents.swap(mCollisionEvents);
        mDispatchingBurstEvents.swap(mBurstEvents);

        for (const auto& Instance : mEmitterInstances) {
            // 꺼진 LOD 레벨의 에미터는 이벤트를 받지 않습니다.
            if (Instance->mCurrentLODLevel == nullptr || !Instance->mCurrentLODLevel->IsEnabled()) {
                continue;
            }

            for (const Engine::UParticleModuleEventReceiverBase* Receiver : Instance->mCurrentLODLevel->GetEventReceivers()) {
                if (!Receiver->IsEnabled()) {
                    continue;
                }

                const auto Deliver{[&](const auto& Events) {
                    for (const auto& Event : Events) {
                        if (Receiver->WillProcessEvent(Event)) {
                            Receiver->ProcessEvent(*Instance, Event);
                        }
                    }
                }};

                Deliver(mDispatchingSpawnEvents);
                Deliver(mDispatchingDeathEvents);
                Deliver(mDispatchingCollisionEvents);
                Deliver(mDispatchingBurstEvents);
            }
        }
    }

    void Engine::UParticleSystemComponent::BroadcastGameEvents() {
        if (!mOnParticleSpawn.IsBound() && !mOnParticleDeath.IsBound() && !mOnParticleCollide.IsBound() && !mOnParticleBurst.IsBound()) {
            return;
        }

        // 콜백이 템플릿을 바꾸거나(버퍼를 비움) 액터를 파괴할 수 있으므로 보낼 목록을 지역 복사본으로 옮기고
        // 콜백마다 이 컴포넌트가 아직 살아 있는지 확인합니다. 닿은 대상은 약한 참조라 파괴되면 비게 됩니다.
        const auto Collisions{mDispatchingCollisionEvents};
        const auto Spawns{mDispatchingSpawnEvents};
        const auto Deaths{mDispatchingDeathEvents};
        const auto Bursts{mDispatchingBurstEvents};
        const CoreUObject::TObjectRef<Engine::UParticleSystemComponent> Self{this};

        for (const auto& Event : Collisions) {
            mOnParticleCollide.Broadcast(this, Event);

            if (Self.Get() == nullptr) {
                return;
            }
        }

        for (const auto& Event : Spawns) {
            mOnParticleSpawn.Broadcast(this, Event);

            if (Self.Get() == nullptr) {
                return;
            }
        }

        for (const auto& Event : Deaths) {
            mOnParticleDeath.Broadcast(this, Event);

            if (Self.Get() == nullptr) {
                return;
            }
        }

        for (const auto& Event : Bursts) {
            mOnParticleBurst.Broadcast(this, Event);

            if (Self.Get() == nullptr) {
                return;
            }
        }
    }

    void Engine::UParticleSystemComponent::ClearEvents() {
        mSpawnEvents.clear();
        mDeathEvents.clear();
        mCollisionEvents.clear();
        mBurstEvents.clear();
        mDispatchingSpawnEvents.clear();
        mDispatchingDeathEvents.clear();
        mDispatchingCollisionEvents.clear();
        mDispatchingBurstEvents.clear();
    }

}
