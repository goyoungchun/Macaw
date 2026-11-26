/**
 * @file UWorld.cpp
 * @brief 영속 레벨, 렌더 장면과 프레임 말 컴포넌트 갱신을 관리합니다.
 * @ingroup Engine
 */

#include "Core/Types.h"
#include "Engine/UWorld.h"
#include "FLuaRuntime.h"
#include "Asset/FAssetManager.h"
#include "Core/FilePath.h"
#include "Engine/Component/UHeightFogComponent.h"
#include "Engine/Component/USkyAtmosphereComponent.h"
#include "Engine/Component/USkyDomeComponent.h"
#include "Engine/Component/UDecalComponent.h"
#include "Engine/EngineStats.h"
#include "Engine/Component/ULightComponentBase.h"

#include "Engine/MeshPicking.h"
#include "Engine/Component/UMeshComponent.h"
#include "Engine/Component/UStaticMeshComponent.h"
#include "Engine/Component/UBillboardComponent.h"
#include "Engine/Component/UCameraComponent.h"
#include "Engine/APlayerCameraManager.h"
#include "Core/Input.h"
#include "Core/Math.h"
#include "Engine/Component/UPrimitiveComponent.h"
#include "Engine/Collision/FHitResult.h"
#include "Engine/AStaticMeshActor.h"
#include "Engine/ULevel.h"
#include "Engine/FWorldHit.h"
#include "CoreUObject/TObjectRef.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FRay.h"
#include "Asset/FMeshData.h"
#include "Engine/FSceneInterface.h"
#include "Engine/EPrimitiveType.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/PrimitiveMesh.h"

#include "CoreUObject/FObjectRegistry.h"
#include "CoreUObject/Reflection/UClass.h"
#include "Core/Log/Log.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/FPhysicsScene.h"
#include "PhysicsRuntime/PhysicsStats.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace Engine {

    /**
     * @brief 월드의 로그 범주입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogWorld, Log, All);
    }

    /**
     * @brief UWorld의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Engine::UWorld::UWorld() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Engine::UWorld::~UWorld() {
        Shutdown();
    }

    bool Engine::UWorld::OpenScene(const Core::FString& ScenePath) {
        if ((mWorldType != Engine::EWorldType::Game && mWorldType != Engine::EWorldType::PIE) || !mWorldHandle.IsValid() || !CanBeginActor() || mAssets == nullptr || ScenePath.empty() || ScenePath.find('\0') != Core::FString::npos) {
            return false;
        }

        Core::FString Error{};
        const auto Path{mAssets->ResolvePath(Core::FilePath::FromUtf8(ScenePath), Error)};
        std::error_code Code{};

        if (Path.empty() || !std::filesystem::is_regular_file(Path, Code)) {
            return false;
        }

        mPendingScenePath = ScenePath;

        return true;
    }

    /**
     * @brief 월드의 에셋 로드에 사용할 관리자의 비소유 주소를 설정합니다.
     */
    void Engine::UWorld::SetAssets(Asset::FAssetManager* Assets) {
        if (mAssets == Assets) {
            return;
        }

        mAssets = Assets;

        if (mPersistentLevel != nullptr) {
            for (const auto& Actor : mPersistentLevel->GetActors()) {
                for (const auto& Component : Actor->GetComponents()) {
                    if (auto* Mesh{CoreUObject::Cast<Engine::UMeshComponent>(Component.get())}) {
                        Mesh->ResolveMesh();
                        Mesh->ResolveMaterial();
                    }

                    if (auto* Sprite{CoreUObject::Cast<Engine::UBillboardComponent>(Component.get())}) {
                        Sprite->ResolveTexture();
                    }

                    if (auto* Decal{CoreUObject::Cast<Engine::UDecalComponent>(Component.get())}) {
                        Decal->ResolveTexture();
                    }

                    if (auto* SkyDome{CoreUObject::Cast<Engine::USkyDomeComponent>(Component.get())}) {
                        SkyDome->ResolvePanorama();
                    }

                    if (auto* Light{CoreUObject::Cast<Engine::ULightComponentBase>(Component.get())}) {
                        Light->SynchronizeLight();
                    }

                    if (auto* Fog{CoreUObject::Cast<Engine::UHeightFogComponent>(Component.get())}) {
                        Fog->SynchronizeFog();
                    }
                }
            }
        }
    }

    /**
     * @brief 프로젝트의 에셋 관리자를 비소유 주소로 반환합니다.
     */
    Asset::FAssetManager* Engine::UWorld::GetAssets() const {
        return mAssets;
    }

    /**
     * @brief 물리 장면 생성에 사용할 PhysX 런타임의 비소유 주소를 설정합니다.
     */
    void Engine::UWorld::SetPhysicsRuntime(PhysicsRuntime::FPhysicsRuntime* Runtime) {
        if (mPhysicsRuntime == Runtime) {
            return;
        }

        DestroyPhysicsScene();
        mPhysicsRuntime = Runtime;
    }

    /**
     * @brief 물리 장면 생성에 사용할 PhysX 런타임을 반환합니다.
     */
    PhysicsRuntime::FPhysicsRuntime* Engine::UWorld::GetPhysicsRuntime() const {
        return mPhysicsRuntime;
    }

    /**
     * @brief 천 시뮬레이션을 만들 때 쓸 천 시스템 모듈의 비소유 주소를 설정합니다.
     */
    void Engine::UWorld::SetClothingSystem(PhysicsRuntime::FClothingSystemRuntimeModuleNv* ClothingSystem) {
        mClothingSystem = ClothingSystem;
    }

    /**
     * @brief 천 시뮬레이션을 만들 때 쓸 천 시스템 모듈을 반환합니다.
     */
    PhysicsRuntime::FClothingSystemRuntimeModuleNv* Engine::UWorld::GetClothingSystem() const {
        return mClothingSystem;
    }

    /**
     * @brief 월드가 소유한 물리 장면을 반환합니다.
     */
    PhysicsRuntime::FPhysicsScene* Engine::UWorld::GetPhysicsScene() const {
        return mPhysicsScene.get();
    }

    /**
     * @brief 월드의 바람장을 반환합니다.
     */
    Engine::FWindField& Engine::UWorld::GetWindField() {
        return mWindField;
    }

    /**
     * @brief 월드의 바람장을 반환합니다.
     */
    const Engine::FWindField& Engine::UWorld::GetWindField() const {
        return mWindField;
    }

    /**
     * @brief 설정된 런타임으로 물리 장면을 만들고 고정 스텝 누적 시간을 초기화합니다.
     */
    bool Engine::UWorld::CreatePhysicsScene() {
        if (mPhysicsScene != nullptr) {
            return true;
        }

        if (mPhysicsRuntime == nullptr) {
            return false;
        }

        auto Scene{std::make_unique<PhysicsRuntime::FPhysicsScene>()};

        if (!Scene->Initialize(*mPhysicsRuntime)) {
            MACAW_LOG(LogWorld, Warning, "Cannot create the physics scene; the world runs without physics.");

            return false;
        }

        mPhysicsScene = std::move(Scene);
        mPhysicsTimeAccumulator = {};

        // 장면보다 먼저 등록된 컴포넌트는 등록 시점에 바디를 만들지 못했으므로 여기서 만듭니다.
        UpdateComponentPhysicsStates(true);

        return true;
    }

    /**
     * @brief 컴포넌트의 물리 상태를 먼저 해제한 뒤 물리 장면을 해제합니다.
     */
    void Engine::UWorld::DestroyPhysicsScene() {
        if (mPhysicsScene != nullptr) {
            UpdateComponentPhysicsStates(false);
        }

        mPhysicsScene.reset();
        mPhysicsTimeAccumulator = {};
        mOverlaps.clear();
    }

    /**
     * @brief 영속 레벨의 모든 액터 컴포넌트에 물리 상태 생성 또는 해제를 요청합니다.
     */
    void Engine::UWorld::UpdateComponentPhysicsStates(bool Create) {
        if (mPersistentLevel == nullptr) {
            return;
        }

        for (const auto& Actor : mPersistentLevel->GetActors()) {
            for (const auto& Component : Actor->GetComponents()) {
                if (Create) {
                    Component->CreatePhysicsState();
                } else {
                    Component->DestroyPhysicsState();
                }
            }
        }
    }

    /**
     * @brief 프레임 시간을 고정 스텝으로 나누어 물리 장면을 진행합니다.
     */
    void Engine::UWorld::StepPhysics(Core::FTime DeltaTime) {
        if (mPhysicsScene == nullptr || !DeltaTime.IsFinite() || DeltaTime <= Core::FTime{}) {
            return;
        }

        const PhysicsRuntime::FPhysicsSettings& Settings{mPhysicsRuntime->GetSettings()};
        const Core::FTime Step{Settings.mFixedStep};

        mPhysicsTimeAccumulator = std::min(mPhysicsTimeAccumulator + DeltaTime, Step * static_cast<float>(Settings.mMaxStepsPerFrame));

        // 접촉 목록과 움직인 바디 목록은 다음 Simulate에서 바뀌므로 스텝마다 결과를 전달합니다.
        while (mPhysicsScene != nullptr && !mEndPlayRequested && !mShutdownRequested && mPhysicsTimeAccumulator >= Step && mPhysicsScene->Simulate(Step)) {
            mPhysicsTimeAccumulator -= Step;

            DispatchPhysicsResults();
        }
    }

    /**
     * @brief 마지막 물리 스텝에서 움직인 바디를 컴포넌트에 반영하고 새 접촉을 충돌 이벤트로 전달합니다.
     */
    void Engine::UWorld::DispatchPhysicsResults() {
        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::Dispatch};

        /**
         * @brief 컴포넌트 핸들로 바꾼 접촉 한 건입니다.
         */
        struct FPhysicsHit {
            CoreUObject::TObjectRef<Engine::UPrimitiveComponent> mFirst{};
            CoreUObject::TObjectRef<Engine::UPrimitiveComponent> mSecond{};
            FVector mPoint{};
            FVector mNormal{};
        };

        const auto OwnerOf{[](const physx::PxActor* Actor) -> Engine::UPrimitiveComponent* {
            const PhysicsRuntime::FBodyInstance* Body{Actor != nullptr ? PhysicsRuntime::FBodyInstance::FromPxActor(*Actor) : nullptr};

            return Body != nullptr ? Body->GetOwnerComponent() : nullptr;
        }};

        // 이벤트 처리 중 바디가 다시 만들어지면 PhysX 주소가 무효화되므로 전달 전에 모두 핸들로 바꿉니다.
        Core::TArray<CoreUObject::TObjectRef<Engine::UPrimitiveComponent>> Moved{};
        Core::TArray<FPhysicsHit> Hits{};

        for (const physx::PxActor* Actor : mPhysicsScene->GetActiveActors()) {
            const PhysicsRuntime::FBodyInstance* Body{Actor != nullptr ? PhysicsRuntime::FBodyInstance::FromPxActor(*Actor) : nullptr};

            // 뼈 바디는 컴포넌트 전체가 아니라 뼈 하나의 자세이므로 컴포넌트가 직접 읽어 갑니다.
            if (Body == nullptr || Body->mInstanceBodyIndex != InvalidIndex) {
                continue;
            }

            if (Engine::UPrimitiveComponent* Component{Body->GetOwnerComponent()}) {
                Moved.push_back(CoreUObject::MakeObjectRef(Component));
            }
        }

        for (const PhysicsRuntime::FPhysicsContact& Contact : mPhysicsScene->GetContacts()) {
            Engine::UPrimitiveComponent* First{OwnerOf(Contact.mActor0)};
            Engine::UPrimitiveComponent* Second{OwnerOf(Contact.mActor1)};

            if (First != nullptr && Second != nullptr) {
                Hits.push_back({CoreUObject::MakeObjectRef(First), CoreUObject::MakeObjectRef(Second), Contact.mPoint, Contact.mNormal});
            }
        }

        for (const CoreUObject::TObjectRef<Engine::UPrimitiveComponent> Handle : Moved) {
            if (Engine::UPrimitiveComponent* Component{Handle.Get()}) {
                Component->SyncComponentToBody();
            }
        }

        for (const FPhysicsHit& Contact : Hits) {
            Engine::UPrimitiveComponent* First{Contact.mFirst.Get()};
            Engine::UPrimitiveComponent* Second{Contact.mSecond.Get()};

            if (First == nullptr || Second == nullptr) {
                continue;
            }

            // 접촉 법선은 두 번째 바디에서 첫 번째 바디를 향하므로 첫 번째 컴포넌트가 받은 충격면의 법선과 같습니다.
            Engine::FHitResult Hit{0.0f};
            Hit.mBlockingHit = true;
            Hit.mLocation = Contact.mPoint;
            Hit.mImpactPoint = Contact.mPoint;
            Hit.mNormal = Contact.mNormal;
            Hit.mImpactNormal = Contact.mNormal;
            Hit.SetComponent(Second);
            Hit.SetActor(Second->GetOwner());
            First->OnHit(Second->GetOwner(), Second, Hit);

            // 첫 번째 이벤트 처리 중 상대가 사라졌을 수 있으므로 핸들을 다시 해석합니다.
            First = Contact.mFirst.Get();
            Second = Contact.mSecond.Get();

            if (First != nullptr && Second != nullptr) {
                Second->OnHit(First->GetOwner(), First, Engine::FHitResult::GetReversedHit(Hit, First, First->GetOwner()));
            }
        }
    }

    /**
     * @brief 아직 레벨이 없는 월드에 영속 레벨의 소유권을 연결합니다.
     */
    bool Engine::UWorld::SetPersistentLevel(std::unique_ptr<Engine::ULevel> Level) {
        if (mPersistentLevel != nullptr || Level == nullptr || Level->mWorld != nullptr || mShuttingDown || IsDispatching() || mPlayState != Engine::EWorldPlayState::Stopped) {
            return false;
        }

        mPersistentLevel = std::move(Level);
        mPersistentLevel->SetOuter(this);
        mPersistentLevel->SetWorld(this);

        return true;
    }

    /**
     * @brief 월드가 소유한 영속 레벨을 반환합니다.
     */
    Engine::ULevel* Engine::UWorld::GetPersistentLevel() const {
        return mPersistentLevel.get();
    }

    /**
     * @brief 객체의 실제 소유 관계가 이 월드에 속하는지 검사합니다.
     */
    bool Engine::UWorld::ContainsObject(const CoreUObject::UObject* Object) const {
        if (Object == nullptr) {
            return false;
        }

        if (Object == this || Object == mPersistentLevel.get()) {
            return true;
        }

        if (const auto* Actor{CoreUObject::Cast<Engine::AActor>(Object)}) {
            return Actor->GetWorld() == this && !Actor->IsPendingDestroy();
        }

        const auto* Component{CoreUObject::Cast<Engine::UActorComponent>(Object)};

        if (Component != nullptr) {
            return Component->GetOwner() != nullptr && ContainsObject(Component->GetOwner());
        }

        // 컴포넌트가 소유하는 하위 객체(예: 애니메이션 인스턴스)는 Outer 컴포넌트가 월드에 있으면 월드에 속합니다.
        const CoreUObject::UObject* Outer{Object->GetOuter()};

        return Outer != nullptr && CoreUObject::Cast<Engine::UActorComponent>(Outer) != nullptr && ContainsObject(Outer);
    }

    /**
     * @brief 월드에 렌더 장면을 연결하고 기존 프리미티브를 동기화합니다.
     */
    bool Engine::UWorld::AttachScene(std::unique_ptr<Engine::FSceneInterface> Scene) {
        if (mScene != nullptr || Scene == nullptr || mShuttingDown || IsDispatching()) {
            return false;
        }

        mScene = std::move(Scene);

        if (mPersistentLevel != nullptr) {
            for (const auto& Actor : mPersistentLevel->GetActors()) {
                for (const auto& Component : Actor->GetComponents()) {
                    if (auto* Light{CoreUObject::Cast<Engine::ULightComponentBase>(Component.get())}) {
                        Light->SynchronizeLight();
                    }

                    if (auto* Fog{CoreUObject::Cast<Engine::UHeightFogComponent>(Component.get())}) {
                        Fog->SynchronizeFog();
                    }

                    if (auto* Atmosphere{CoreUObject::Cast<Engine::USkyAtmosphereComponent>(Component.get())}) {
                        Atmosphere->SynchronizeSkyAtmosphere();
                    }

                    if (auto* SkyDome{CoreUObject::Cast<Engine::USkyDomeComponent>(Component.get())}) {
                        SkyDome->SynchronizeSkyDome();
                    }

                    if (auto* Decal{CoreUObject::Cast<Engine::UDecalComponent>(Component.get())}) {
                        Decal->SynchronizeDecal();
                    }

                    if (auto* Primitive{CoreUObject::Cast<Engine::UPrimitiveComponent>(Component.get())}) {
                        Primitive->MarkRenderStateDirty();
                    }
                }
            }
        }

        SendAllEndOfFrameUpdates();

        return true;
    }

    /**
     * @brief 연결된 렌더 장면을 반환합니다.
     */
    Engine::FSceneInterface* Engine::UWorld::GetScene() const {
        return mScene.get();
    }

    /**
     * @brief 지정한 액터 클래스의 기본 컴포넌트를 생성하고 영속 레벨에 추가합니다.
     */
    Engine::AActor* Engine::UWorld::SpawnActor(const CoreUObject::UClass* Class) {
        if (Class == nullptr || !Class->IsChildOf(Engine::AActor::StaticClass()) || !Class->CanCreate() || mPersistentLevel == nullptr || !CanAddActor()) {
            return nullptr;
        }

        auto Object{Class->CreateInstance()};

        if (Object == nullptr || Object->GetClass() != Class) {
            return nullptr;
        }

        std::unique_ptr<Engine::AActor> Actor{static_cast<Engine::AActor*>(Object.release())};
        Core::FString Name{};

        do {
            Name = Class->GetName() + "_" + std::to_string(++mSpawnSerial);
        } while (CoreUObject::FObjectRegistry::Get()->FindObject(Core::FName{Name}, mPersistentLevel.get()) != nullptr);
        Actor->SetName(Name);

        if (!Actor->InitializeDefaultComponents()) {
            return nullptr;
        }

        const CoreUObject::TObjectRef<Engine::AActor> Handle{CoreUObject::MakeObjectRef(Actor)};

        return mPersistentLevel->AddActor(std::move(Actor)) ? ResolveActor(Handle) : nullptr;
    }

    /**
     * @brief 지정한 기본 메시 종류의 액터를 생성하여 영속 레벨에 추가합니다.
     */
    Engine::AStaticMeshActor* Engine::UWorld::SpawnPrimitive(Engine::EPrimitiveType Type) {
        if (Type >= Engine::EPrimitiveType::Count || mPersistentLevel == nullptr || !CanAddActor()) {
            return nullptr;
        }

        auto Actor{CoreUObject::NewObject<Engine::AStaticMeshActor>()};

        if (Actor == nullptr || !Actor->InitializeDefaultComponents()) {
            return nullptr;
        }

        Core::FString Name{};

        do {
            Name = Engine::PrimitiveName(Type) + "_" + std::to_string(++mSpawnSerial);
        } while (CoreUObject::FObjectRegistry::Get()->FindObject(Core::FName{Name}, mPersistentLevel.get()) != nullptr);

        Actor->SetName(Name);
        Actor->GetStaticMeshComponent()->SetPrimitiveType(Type);

        const CoreUObject::TObjectRef<Engine::AActor> Handle{CoreUObject::MakeObjectRef(Actor)};

        return mPersistentLevel->AddActor(std::move(Actor)) ? CoreUObject::Cast<Engine::AStaticMeshActor>(ResolveActor(Handle)) : nullptr;
    }

    /**
     * @brief 핸들에 해당하는 액터의 컴포넌트를 등록 해제하고 액터를 제거합니다.
     */
    bool Engine::UWorld::DestroyActor(CoreUObject::TObjectRef<Engine::AActor> Handle) {
        Engine::AActor* Actor{ResolveActor(Handle)};

        if (Actor == nullptr || Actor->IsPendingDestroy()) {
            return false;
        }

        Actor->mPendingDestroy = true;

        mPendingActors.push_back(Handle);
        FlushPendingActors();

        return true;
    }

    /**
     * @brief 컴포넌트 핸들을 프레임 말 렌더 갱신 큐에 추가합니다.
     */
    void Engine::UWorld::EnqueueRenderUpdate(CoreUObject::TObjectRef<Engine::UPrimitiveComponent> Handle) {
        mDirtyComponents.push_back(Handle);
    }

    /**
     * @brief 큐에 쌓인 컴포넌트 핸들을 해석하여 지연된 렌더 갱신을 반영합니다.
     */
    void Engine::UWorld::SendAllEndOfFrameUpdates() {
        const Core::FScopedStatTimer StatTimer{Engine::EngineStats::RenderUpdates};

        if (mScene == nullptr) {
            return;
        }

        const std::size_t Count{mDirtyComponents.size()};

        for (Index_t Index{}; Index < Count; ++Index) {
            const CoreUObject::TObjectRef<Engine::UPrimitiveComponent> Handle{mDirtyComponents[Index]};

            if (auto* Component{CoreUObject::Cast<Engine::UPrimitiveComponent>(Handle.Get())}; Component != nullptr && Component->GetWorld() == this) {
                Component->SendRenderUpdates();
            }
        }

        mDirtyComponents.erase(mDirtyComponents.begin(), mDirtyComponents.begin() + static_cast<std::ptrdiff_t>(Count));
    }

    /**
     * @brief 월드 프리미티브의 메시를 검사하여 광선에 가장 가까운 충돌을 찾습니다.
     */
    bool Engine::UWorld::Raycast(const FRay& Ray, Engine::FWorldHit& Hit, const Engine::AActor* Ignored) const {
        const Core::FScopedStatTimer StatTimer{Engine::EngineStats::Raycast};
        FRay NormalizedRay{Ray};

        if (mPersistentLevel == nullptr || !NormalizedRay.mOrigin.IsFinite() || !NormalizedRay.mDirection.Normalize()) {
            return false;
        }

        bool Found{};
        float Closest{std::numeric_limits<float>::max()};

        for (const auto& Actor : mPersistentLevel->GetActors()) {
            if (Actor->IsPendingDestroy() || Actor.get() == Ignored) {
                continue;
            }

            for (const auto& Component : Actor->GetComponents()) {
                auto* Primitive{CoreUObject::Cast<Engine::UPrimitiveComponent>(Component.get())};

                if (Primitive != nullptr && Engine::RaycastMesh(&Primitive->GetMeshData(), Primitive->GetWorldMatrix(), NormalizedRay, Closest)) {
                    Hit = {CoreUObject::MakeObjectRef(Actor), CoreUObject::MakeObjectRef(Primitive), NormalizedRay.mOrigin + NormalizedRay.mDirection * Closest, Core::FLength::FromBase(Closest)};
                    Found = true;
                }
            }
        }

        return Found;
    }

    /**
     * @brief 광선과 처음 만나는 메시를 찾아 값으로 반환합니다.
     */
    Engine::FWorldHit Engine::UWorld::Raycast(const FVector& Origin, const FVector& Direction, const Engine::AActor* Ignored) const {
        Engine::FWorldHit Hit{};

        Raycast(FRay{Origin, Direction}, Hit, Ignored);

        return Hit;
    }

    /**
     * @brief 이름으로 살아 있는 액터를 찾습니다.
     */
    Engine::AActor* Engine::UWorld::FindActor(const Core::FString& Name) const {
        if (mPersistentLevel != nullptr) {
            for (const auto& Actor : mPersistentLevel->GetActors()) {
                if (!Actor->IsPendingDestroy() && Actor->GetName() == Name) {
                    return Actor.get();
                }
            }
        }

        return nullptr;
    }

    /**
     * @brief 종속 객체를 역순으로 정리하고 여러 번 호출해도 안전한 종료 상태로 만듭니다.
     */
    void Engine::UWorld::Shutdown() {
        if (IsDispatching()) {
            mShutdownRequested = true;

            return;
        }

        mShutdownRequested = false;

        EndPlay();
        DestroyPhysicsScene();

        mShuttingDown = true;

        if (mPersistentLevel != nullptr) {
            mPersistentLevel->SetWorld(nullptr);
        }

        mPendingActors.clear();
        mDirtyComponents.clear();
        mActiveCamera = {};
        mInputTarget = {};
        mPlayerCameraManager = {};
        mPersistentLevel.reset();
        mScene.reset();
    }

    /**
     * @brief 월드의 콜백 전달 깊이를 증가시킵니다.
     */
    Engine::UWorld::FDispatchScope::FDispatchScope(Engine::UWorld* World)
		: mWorld{World} {
        if (mWorld != nullptr) {
            ++mWorld->mDispatchDepth;
        }
    }

    /**
     * @brief 전달 깊이를 감소시키고 안전한 경계에서 지연 작업을 처리합니다.
     */
    Engine::UWorld::FDispatchScope::~FDispatchScope() {
        if (mWorld != nullptr) {
            mWorld->FinishDispatch();
        }
    }

    /**
     * @brief 이 월드를 엔진에 등록할 때 발급한 핸들을 반환합니다.
     */
    Engine::FWorldHandle Engine::UWorld::GetWorldHandle() const {
        return mWorldHandle;
    }

    /**
     * @brief 월드의 편집 또는 실행 용도를 반환합니다.
     */
    Engine::EWorldType Engine::UWorld::GetWorldType() const {
        return mWorldType;
    }

    /**
     * @brief 현재 게임 실행 생명주기 상태를 반환합니다.
     */
    Engine::EWorldPlayState Engine::UWorld::GetPlayState() const {
        return mPlayState;
    }

    /**
     * @brief 정지 상태의 Game 또는 PIE 월드에서 게임 생명주기를 시작합니다.
     */
    bool Engine::UWorld::BeginPlay() {
        if (mPlayState != Engine::EWorldPlayState::Stopped || IsDispatching() || mShuttingDown || (mWorldType != Engine::EWorldType::Game && mWorldType != Engine::EWorldType::PIE)) {
            return false;
        }

        FDispatchScope Scope{this};

        mPlayState = Engine::EWorldPlayState::Starting;
        mTime = {};
        mTickCount = 0;

        // 액터의 BeginPlay에서 강체를 다룰 수 있도록 물리 장면을 먼저 만듭니다. 실패하면 물리 없이 진행합니다.
        CreatePhysicsScene();

        // 액터의 BeginPlay에서 카메라 관리자를 조회할 수 있도록 먼저 만듭니다.
        if (auto CameraManager{CoreUObject::NewObject<Engine::APlayerCameraManager>()}; CameraManager != nullptr && CameraManager->InitializeDefaultComponents() && mPersistentLevel != nullptr) {
            CameraManager->SetName("PlayerCameraManager");

            const CoreUObject::TObjectRef<Engine::APlayerCameraManager> Handle{CoreUObject::MakeObjectRef(CameraManager)};

            if (mPersistentLevel->AddActor(std::move(CameraManager))) {
                mPlayerCameraManager = Handle;
            }
        }

        const auto Handles{GetActorRefs()};

        for (const CoreUObject::TObjectRef<Engine::AActor> Handle : Handles) {
            if (!CanBeginActor()) {
                break;
            }

            if (Engine::AActor* Actor{ResolveActor(Handle)}) {
                Actor->DispatchBeginPlay();
            }
        }

        // BeginPlay에서 뷰 타깃을 정하지 않았으면 편집기에서 지정한 활성 카메라를 씁니다.
        if (Engine::APlayerCameraManager* CameraManager{GetPlayerCameraManager()}) {
            if (CameraManager->GetViewTarget() == nullptr) {
                if (const Engine::UCameraComponent* Camera{GetActiveCamera()}) {
                    CameraManager->SetViewTarget(Camera->GetOwner());
                }
            }

            CameraManager->UpdateCamera({});
        }

        mPlayState = Engine::EWorldPlayState::Playing;

        return true;
    }

    /**
     * @brief 시작된 액터와 컴포넌트의 실행을 종료합니다.
     */
    void Engine::UWorld::EndPlay() {
        mPendingScenePath.clear();

        if (mDispatchDepth != 0) {
            mEndPlayRequested = true;

            return;
        }

        mEndPlayRequested = false;

        if (mPlayState == Engine::EWorldPlayState::Stopped || mPlayState == Engine::EWorldPlayState::Stopping) {
            return;
        }

        FDispatchScope Scope{this};

        mPlayState = Engine::EWorldPlayState::Stopping;

        const auto Handles{GetActorRefs()};

        for (const CoreUObject::TObjectRef<Engine::AActor> Handle : Handles) {
            if (Engine::AActor* Actor{ResolveActor(Handle)}) {
                Actor->DispatchEndPlay();
            }
        }

        DestroyActor(mPlayerCameraManager);
        mPlayerCameraManager = {};
        mLuaRuntime.reset();
        DestroyPhysicsScene();
        mPlayState = Engine::EWorldPlayState::Stopped;
    }

    /**
     * @brief 월드 상태에 따라 일반 게임, 편집 또는 일시정지 Tick을 수행합니다.
     */
    bool Engine::UWorld::Tick(Core::FTime DeltaTime) {
        const bool EditorTick{mPlayState == Engine::EWorldPlayState::Stopped && (mWorldType == Engine::EWorldType::Editor || mWorldType == Engine::EWorldType::EditorPreview)};

        const bool PauseTick{mPlayState == Engine::EWorldPlayState::Paused};

        return (mPlayState == Engine::EWorldPlayState::Playing || EditorTick || PauseTick) && TickActors(DeltaTime, EditorTick, PauseTick);
    }

    /**
     * @brief Playing과 Paused 사이의 상태를 전환합니다.
     */
    bool Engine::UWorld::SetPaused(bool Paused) {
        if (mPlayState != Engine::EWorldPlayState::Playing && mPlayState != Engine::EWorldPlayState::Paused) {
            return false;
        }

        mPlayState = Paused ? Engine::EWorldPlayState::Paused : Engine::EWorldPlayState::Playing;

        return true;
    }

    /**
     * @brief Paused 상태에서 일반 게임 프레임을 한 번 수행합니다.
     */
    bool Engine::UWorld::Step(Core::FTime DeltaTime) {
        return mPlayState == Engine::EWorldPlayState::Paused && DeltaTime > Core::FTime{} && TickActors(DeltaTime, false, false);
    }

    Core::FTime Engine::UWorld::GetTime() const {
        return mTime;
    }

    /**
     * @brief 마지막 실행 시작 이후 누적 게임 시간을 반환합니다.
     */
    float Engine::UWorld::GetTimeSeconds() const {
        return GetTime().GetBase();
    }

    /**
     * @brief 마지막 실행 시작 이후 수행한 게임 갱신 횟수를 반환합니다.
     */
    Uint64 Engine::UWorld::GetTickCount() const {
        return mTickCount;
    }

    /**
     * @brief 콜백 전달 또는 지연된 액터 정리를 처리 중인지 검사합니다.
     */
    bool Engine::UWorld::IsDispatching() const {
        return mDispatchDepth != 0 || mFlushing;
    }

    /**
     * @brief 월드 소속을 검증하여 활성 카메라를 선택하거나 해제합니다.
     */
    bool Engine::UWorld::SetActiveCamera(Engine::UCameraComponent* Camera) {
        if (!CanAddActor() || (Camera != nullptr && (Camera->GetWorld() != this || !ContainsObject(Camera)))) {
            return false;
        }

        if (Engine::APlayerCameraManager* CameraManager{GetPlayerCameraManager()}) {
            Engine::AActor* Target{Camera != nullptr ? Camera->GetOwner() : nullptr};

            if ((CameraManager->GetViewTarget() != Target || CameraManager->IsBlending()) && !CameraManager->SetViewTarget(Target)) {
                return false;
            }
        }

        mActiveCamera = Camera;

        return true;
    }

    /**
     * @brief 월드에 등록된 활성 카메라를 핸들로 검증하여 조회합니다.
     */
    Engine::UCameraComponent* Engine::UWorld::GetActiveCamera() const {
        auto* Camera{CoreUObject::Cast<Engine::UCameraComponent>(mActiveCamera.Get())};

        return Camera != nullptr && Camera->GetWorld() == this && ContainsObject(Camera) ? Camera : nullptr;
    }

    /**
     * @brief 월드 소속을 검증하여 입력 대상 액터를 선택하거나 해제합니다.
     */
    bool Engine::UWorld::SetInputTarget(Engine::AActor* Actor) {
        if (!CanAddActor() || (Actor != nullptr && !ContainsObject(Actor))) {
            return false;
        }

        mInputTarget = Actor;

        return true;
    }

    /**
     * @brief 월드의 뷰포트 입력을 받을 액터를 핸들로 검증하여 조회합니다.
     */
    Engine::AActor* Engine::UWorld::GetInputTarget() const {
        Engine::AActor* Actor{ResolveActor(mInputTarget)};

        return Actor != nullptr && !Actor->IsPendingDestroy() ? Actor : nullptr;
    }

    Engine::APlayerCameraManager* Engine::UWorld::GetPlayerCameraManager() const {
        return CoreUObject::Cast<Engine::APlayerCameraManager>(ResolveActor(mPlayerCameraManager));
    }

    /**
     * @brief 뷰포트 입력을 받아 프레임 시간에 따라 액터의 동작을 갱신합니다.
     */
    bool Engine::UWorld::Input(const Core::FInput& Input, float DeltaTime) {
        if ((mWorldType != Engine::EWorldType::Game && mWorldType != Engine::EWorldType::PIE) || mPlayState != Engine::EWorldPlayState::Playing || IsDispatching() || !CanBeginActor() || !std::isfinite(DeltaTime) || DeltaTime < 0.0f) {
            return false;
        }

        if (mLuaRuntime != nullptr) {
            mLuaRuntime->SetFrameInput(Input);
        }

        Engine::AActor* Actor{GetInputTarget()};
        if (!Input.IsGameInputEnabled() || Actor == nullptr || !Actor->HasBegunPlay()) {
            return false;
        }

        FDispatchScope Scope{this};

        Actor->Input(Input, DeltaTime);

        return true;
    }

    /**
     * @brief 시간과 실행 상태를 검증하고 일반 그룹 또는 일시정지 Tick을 실행합니다.
     * @details 일시정지는 별도 큐를 실행합니다. 일반 게임 프레임은 PostPhysics 뒤에 겹침과 카메라를 갱신한 다음 나머지 그룹을 처리합니다.
     */
    bool Engine::UWorld::TickActors(Core::FTime DeltaTime, bool EditorTick, bool PauseTick) {
        if (!DeltaTime.IsFinite() || DeltaTime < Core::FTime{} || (!EditorTick && !PauseTick && !(mTime + DeltaTime).IsFinite()) || IsDispatching() || mShuttingDown) {
            return false;
        }

        const Core::FScopedStatTimer StatTimer{Engine::EngineStats::WorldTick};

        Engine::EngineStats::WorldTicks.Add();

        FDispatchScope Scope{this};

        if (PauseTick) {
            return mTickTaskManager.RunPauseFrame(*this, DeltaTime.GetBase());
        }

        if (!EditorTick) {
            if (mLuaRuntime != nullptr) {
                mLuaRuntime->ReloadChangedScripts();
            }
            mTime += DeltaTime;
            ++mTickCount;
        }

        mTickTaskManager.StartFrame(*this, DeltaTime.GetBase(), EditorTick);
        mTickTaskManager.RunTickGroup(Engine::ETickingGroup::PrePhysics);
        mTickTaskManager.RunTickGroup(Engine::ETickingGroup::StartPhysics);
        mTickTaskManager.RunTickGroup(Engine::ETickingGroup::DuringPhysics);

        // StartPhysics·EndPhysics는 사용자 Tick이 없는 경계 그룹이므로 그 사이에서 물리 장면을 동기로 진행합니다.
        if (!EditorTick && !mEndPlayRequested && !mShutdownRequested) {
            StepPhysics(DeltaTime);
        }

        mTickTaskManager.RunTickGroup(Engine::ETickingGroup::EndPhysics);
        mTickTaskManager.RunTickGroup(Engine::ETickingGroup::PostPhysics);

        if (!EditorTick && !mEndPlayRequested && !mShutdownRequested) {
            UpdateOverlaps();
        }

        // 카메라는 이번 프레임의 이동과 겹침 처리가 모두 끝난 뒤에 갱신합니다.
        if (Engine::APlayerCameraManager* CameraManager{GetPlayerCameraManager()}; !EditorTick && !mEndPlayRequested && !mShutdownRequested && CameraManager != nullptr) {
            CameraManager->UpdateCamera(DeltaTime);
        }

        mTickTaskManager.RunTickGroup(Engine::ETickingGroup::PostUpdateWork);
        mTickTaskManager.RunTickGroup(Engine::ETickingGroup::LastDemotable);
        mTickTaskManager.EndFrame();

        if (!EditorTick && mLuaRuntime != nullptr) {
            mLuaRuntime->ClearFrameInput();
        }

        return true;
    }

    /**
     * @brief 겹침 이벤트를 켠 컴포넌트의 바디 겹침을 장면 쿼리로 갱신하고 시작과 끝 이벤트를 전달합니다.
     */
    void Engine::UWorld::UpdateOverlaps() {
        using FOverlapPair = std::pair<CoreUObject::TObjectRef<Engine::UPrimitiveComponent>, CoreUObject::TObjectRef<Engine::UPrimitiveComponent>>;

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::OverlapUpdate};
        Core::TArray<FOverlapPair> Current{};

        if (mPhysicsScene != nullptr && mPersistentLevel != nullptr) {
            Core::TArray<PhysicsRuntime::FBodyInstance*> Bodies{};

            for (const auto& Actor : mPersistentLevel->GetActors()) {
                if (Actor->IsPendingDestroy()) {
                    continue;
                }

                for (const auto& Component : Actor->GetComponents()) {
                    auto* Primitive{CoreUObject::Cast<Engine::UPrimitiveComponent>(Component.get())};

                    if (Primitive == nullptr || !Primitive->GetGenerateOverlapEvents() || !Primitive->ComponentOverlapMulti(Bodies)) {
                        continue;
                    }

                    for (const PhysicsRuntime::FBodyInstance* Body : Bodies) {
                        Engine::UPrimitiveComponent* Other{Body->GetOwnerComponent()};

                        if (Other == nullptr || Other == Primitive || !Other->GetGenerateOverlapEvents() || Other->GetOwner() == Primitive->GetOwner()) {
                            continue;
                        }

                        // 두 컴포넌트의 쿼리에서 같은 쌍이 나오므로 순서를 고정해 한 번만 남깁니다.
                        FOverlapPair Pair{std::less<>{}(Primitive, Other) ? FOverlapPair{Primitive, Other} : FOverlapPair{Other, Primitive}};

                        if (std::find(Current.begin(), Current.end(), Pair) == Current.end()) {
                            Current.push_back(std::move(Pair));
                        }
                    }
                }
            }
        }

        PhysicsRuntime::PhysicsStats::OverlapPairs.Add(static_cast<double>(Current.size()));

        const auto Contains{[](const auto& Pairs, const auto& Pair) {
            return std::find(Pairs.begin(), Pairs.end(), Pair) != Pairs.end();
        }};
        const auto Resolve{[](CoreUObject::TObjectRef<Engine::UPrimitiveComponent> Handle) {
            return CoreUObject::Cast<Engine::UPrimitiveComponent>(Handle.Get());
        }};

        // 이벤트 안에서 액터가 파괴될 수 있으므로 호출할 때마다 핸들을 다시 해석합니다.
        const auto Previous{std::exchange(mOverlaps, Current)};

        for (const auto& Pair : Previous) {
            if (!Contains(Current, Pair)) {
                if (Engine::UPrimitiveComponent* Primitive{Resolve(Pair.first)}) {
                    Primitive->OnEndOverlap(Resolve(Pair.second));
                }

                if (Engine::UPrimitiveComponent* Primitive{Resolve(Pair.second)}) {
                    Primitive->OnEndOverlap(Resolve(Pair.first));
                }
            }
        }

        for (const auto& Pair : Current) {
            if (!Contains(Previous, Pair)) {
                Engine::UPrimitiveComponent* First{Resolve(Pair.first)};
                Engine::UPrimitiveComponent* Second{Resolve(Pair.second)};

                if (First != nullptr && Second != nullptr) {
                    First->OnBeginOverlap(Second);
                }

                First = Resolve(Pair.first);
                Second = Resolve(Pair.second);

                if (First != nullptr && Second != nullptr) {
                    Second->OnBeginOverlap(First);
                }
            }
        }
    }

    /**
     * @brief 전달 깊이를 줄이고 안전한 경계에서 지연 작업을 처리합니다.
     */
    void Engine::UWorld::FinishDispatch() {
        --mDispatchDepth;

        FlushPendingActors();
    }

    /**
     * @brief 가장 바깥 전달 범위 이후 종료 요청과 액터 삭제 및 월드 종료를 처리합니다.
     */
    void Engine::UWorld::FlushPendingActors() {
        if (IsDispatching()) {
            return;
        }

        mFlushing = true;

        if (mEndPlayRequested) {
            EndPlay();
        }

        while (!mPendingActors.empty() && mPersistentLevel != nullptr) {
            auto Pending{std::move(mPendingActors)};

            mPendingActors.clear();

            for (const CoreUObject::TObjectRef<Engine::AActor> Handle : Pending) {
                mPersistentLevel->RemoveActor(Handle);
            }
        }

        mFlushing = false;

        if (mShutdownRequested) {
            Shutdown();
        }
    }

    /**
     * @brief 월드 상태에 맞추어 새 액터의 시작 콜백을 전달합니다.
     */
    void Engine::UWorld::NotifyActorAdded(Engine::AActor* Actor) {
        FDispatchScope Scope{this};

        if (CanBeginActor()) {
            Actor->DispatchBeginPlay();
        }
    }

    /**
     * @brief 월드가 새 액터나 컴포넌트 등록을 수락할 수 있는지 검사합니다.
     */
    bool Engine::UWorld::CanAddActor() const {
        return !mShuttingDown && !mShutdownRequested && mPlayState != Engine::EWorldPlayState::Stopping;
    }

    /**
     * @brief 월드가 액터 시작 콜백을 전달할 수 있는지 검사합니다.
     */
    bool Engine::UWorld::CanBeginActor() const {
        return !mEndPlayRequested && !mShutdownRequested && (mPlayState == Engine::EWorldPlayState::Starting || mPlayState == Engine::EWorldPlayState::Playing || mPlayState == Engine::EWorldPlayState::Paused);
    }

    /**
     * @brief 컨테이너 변경에 영향받지 않는 액터 핸들 스냅샷을 만듭니다.
     */
    Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Engine::UWorld::GetActorRefs() const {
        Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Handles{};

        if (mPersistentLevel != nullptr) {
            Handles.reserve(mPersistentLevel->GetActors().size());

            for (const auto& Actor : mPersistentLevel->GetActors()) {
                Handles.push_back(CoreUObject::MakeObjectRef(Actor));
            }
        }

        return Handles;
    }

    /**
     * @brief 전역 핸들을 해석하고 이 월드의 액터인지 확인합니다.
     */
    Engine::AActor* Engine::UWorld::ResolveActor(CoreUObject::TObjectRef<Engine::AActor> Handle) const {
        Engine::AActor* Actor{CoreUObject::Cast<Engine::AActor>(Handle.Get())};

        return Actor != nullptr && Actor->GetWorld() == this ? Actor : nullptr;
    }

}
