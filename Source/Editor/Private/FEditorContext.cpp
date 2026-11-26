/**
 * @file FEditorContext.cpp
 * @brief UI와 독립적인 선택, 장면 상태와 공통 편집 연산을 구현합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Core/Literals/AngleFunctions.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorViewport.h"
#include "Core/FilePath.h"
#include "Engine/FProject.h"
#include "Engine/Component/UMeshComponent.h"
#include "Engine/Component/UStaticMeshComponent.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Engine/ASkeletalMeshActor.h"
#include "Engine/Component/UBillboardTextComponent.h"
#include "Engine/Component/UBillboardComponent.h"
#include "Engine/Engine.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Asset/FClothingAssetFactory.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "ClothEditing.h"
#include "PhysicsRuntime/FClothingSimulationNv.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"
#include "RenderCore/IRendererModule.h"
#include "Engine/UWorld.h"
#include "Engine/ULevel.h"
#include "Engine/FWorldContext.h"
#include "Engine/FSceneViewport.h"
#include "Engine/AActor.h"
#include "Engine/AStaticMeshActor.h"
#include "Engine/ACameraActor.h"
#include "Engine/ALevelSequenceActor.h"
#include "Engine/Component/UCameraComponent.h"
#include "Engine/EPrimitiveType.h"
#include "Engine/Component/UActorComponent.h"
#include "Engine/Component/USceneComponent.h"
#include "Engine/SceneSerializer.h"
#include "Engine/FWorldCloner.h"
#include "Engine/FWorldSerializer.h"
#include "Serialization/MemoryArchive.h"
#include <algorithm>
#include <limits>
#include "CoreUObject/FObjectRegistry.h"
#include "CoreUObject/FPropertyChangedEvent.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "CoreUObject/Reflection/FProperty.h"
#include "CoreUObject/Reflection/UClass.h"
#include "CoreUObject/Reflection/FReflectionRegistry.h"
#include "Core/Base/ErrorHandler.h"
#include "Core/Log/OutputDeviceRedirector.h"

#include <Windows.h>
#include <utility>

using namespace Core::Literals;

namespace Editor {

    Editor::FEditorContext::FSceneEditScope::FSceneEditScope(Editor::FEditorContext& Context)
		: mContext{Context},
		  mToken{Context.BeginSceneEdit()} {
    }

    Editor::FEditorContext::FSceneEditScope::~FSceneEditScope() {
        mContext.EndSceneEdit(mToken);
    }

    bool Editor::FEditorContext::FSceneEditCommand::Undo(Editor::FEditorContext& Context) const {
        return Context.RestoreSceneMemento(mBefore);
    }

    bool Editor::FEditorContext::FSceneEditCommand::Redo(Editor::FEditorContext& Context) const {
        return Context.RestoreSceneMemento(mAfter);
    }

    /**
     * @brief 범위 동안 재진입 제한 플래그를 설정합니다.
     */
    Editor::FEditorContext::FOperationScope::FOperationScope(bool* Flag)
		: mFlag{Flag} {
        *mFlag = true;
    }

    /**
     * @brief 범위가 끝나면 재진입 제한을 해제합니다.
     */
    Editor::FEditorContext::FOperationScope::~FOperationScope() {
        *mFlag = false;
    }

    /**
     * @brief 편집에 필요한 엔진의 비소유 연결을 구성합니다.
     */
    Editor::FEditorContext::FEditorContext(Engine::UEngine* Engine)
		: mEngine{Engine} {
        RegisterConsoleCommands();
    }

    /**
     * @brief 편집 상태와 콜백 저장소를 정리합니다.
     */
    Editor::FEditorContext::~FEditorContext() = default;

    /**
     * @brief 명시적인 편집 월드 연결을 변경합니다.
     */
    bool Editor::FEditorContext::SetWorld(Engine::FWorldHandle Handle) {
        if (mPlayLocked || IsChangingState() || mEngine == nullptr || mEngine->GetWorld(Handle) == nullptr || mEngine->IsDispatching()) {
            return false;
        }

        FOperationScope Scope{&mChangingState};

        mWorld = Handle;

        AssignSelection({}, {});
        mScenePath.clear();
        ClearSceneHistory();
        mSavedSceneRevision = mSceneRevision;

        return true;
    }

    /**
     * @brief UI와 월드 사이의 비소유 편집 연결을 해제합니다.
     */
    void Editor::FEditorContext::Reset() {
        // 엔진이 천 팩토리를 해제하기 전에 테스트 천을 먼저 정리합니다.
        SetClothGridTest(false);
        ClearClothAssetTest();
        ClearClothSimTest();

        if (mPlayLocked || IsChangingState()) {
            return;
        }

        FOperationScope Scope{&mChangingState};

        AssignSelection({}, {});

        mWorld = {};
        mScenePath.clear();
        ClearSceneHistory();
        mSavedSceneRevision = mSceneRevision;

        ClearLogs();
    }

    /**
     * @brief 편집 대상 월드의 식별자를 반환합니다.
     */
    Engine::FWorldHandle Editor::FEditorContext::GetWorldHandle() const {
        return mWorld;
    }

    /**
     * @brief 엔진에서 편집 월드의 세대 핸들을 해석합니다.
     */
    Engine::UWorld* Editor::FEditorContext::GetWorld() const {
        return mEngine != nullptr ? mEngine->GetWorld(mWorld) : nullptr;
    }

    /**
     * @brief 정지한 편집 용도 월드인지 검사합니다.
     */
    bool Editor::FEditorContext::CanEditWorld() const {
        const Engine::UWorld* World{GetWorld()};

        return !mPlayLocked && World != nullptr && (World->GetWorldType() == Engine::EWorldType::Editor || World->GetWorldType() == Engine::EWorldType::EditorPreview) && World->GetPlayState() == Engine::EWorldPlayState::Stopped;
    }

    bool Editor::FEditorContext::CanModifyScene() const {
        return CanEditWorld() && !IsChangingState() && !mEngine->IsDispatching();
    }

    Uint64 Editor::FEditorContext::BeginSceneEdit() {
        if (IsSceneEditActive() || !CanModifyScene()) {
            return {};
        }

        FOperationScope Scope{&mChangingState};
        RestorePreview();

        FSceneMemento State{};

        if (!CaptureSceneMemento(State)) {
            return {};
        }

        mPendingSceneEdit = std::move(State);
        mSceneEditToken = ++mSceneEditSerial;

        return mSceneEditToken;
    }

    void Editor::FEditorContext::EndSceneEdit(Uint64 Token) {
        if (Token == 0 || Token != mSceneEditToken) {
            return;
        }

        const bool Changed{mSceneEditChanged};
        FSceneMemento Before{std::exchange(mPendingSceneEdit, {})};
        mSceneEditToken = 0;
        mSceneEditChanged = false;

        if (!Changed) {
            return;
        }

        FOperationScope Scope{&mChangingState};
        RestorePreview();

        FSceneMemento After{};

        if (!CaptureSceneMemento(After)) {
            ClearSceneHistory();

            return;
        }

        if (Before.mBytes == After.mBytes) {
            return;
        }

        mSceneHistory.resize(mSceneHistoryPosition);
        mSceneHistory.push_back({std::move(Before), std::move(After), mSceneRevision, Token});
        mSceneRevision = Token;
        mSceneHistoryPosition = mSceneHistory.size();
        TrimSceneHistory();
    }

    bool Editor::FEditorContext::IsSceneEditActive() const {
        return mSceneEditToken != 0;
    }

    bool Editor::FEditorContext::CanUndo() const {
        return mSceneHistoryPosition != 0 && !IsSceneEditActive() && CanModifyScene();
    }

    bool Editor::FEditorContext::CanRedo() const {
        return mSceneHistoryPosition < mSceneHistory.size() && !IsSceneEditActive() && CanModifyScene();
    }

    bool Editor::FEditorContext::Undo() {
        if (!CanUndo()) {
            return false;
        }

        const FSceneEditCommand& Command{mSceneHistory[mSceneHistoryPosition - 1]};

        if (!Command.Undo(*this)) {
            return false;
        }

        --mSceneHistoryPosition;
        mSceneRevision = Command.mBeforeRevision;

        return true;
    }

    bool Editor::FEditorContext::Redo() {
        if (!CanRedo()) {
            return false;
        }

        const FSceneEditCommand& Command{mSceneHistory[mSceneHistoryPosition]};

        if (!Command.Redo(*this)) {
            return false;
        }

        ++mSceneHistoryPosition;
        mSceneRevision = Command.mAfterRevision;

        return true;
    }

    bool Editor::FEditorContext::CaptureSceneMemento(FSceneMemento& State) {
        Serialization::FMemoryArchive Archive{State.mBytes};
        Engine::FWorldSerializer::Serialize(Archive, mEngine->GetWorldContext(mWorld));

        if (Archive.HasError()) {
            AddLog("Cannot capture scene history: " + Archive.GetError());

            return false;
        }

        for (CoreUObject::TObjectRef<Engine::AActor> Handle : GetSelectedActors()) {
            State.mSelectedActors.push_back(ResolveObject(Handle)->GetGuid());
        }

        if (const CoreUObject::UObject* Object{GetSelectedObject()}) {
            State.mSelectedObject = Object->GetGuid();
        }

        return true;
    }

    bool Editor::FEditorContext::RestoreSceneMemento(const FSceneMemento& State) {
        FOperationScope Scope{&mChangingState};
        Engine::UWorld* Previous{GetWorld()};
        auto Context{std::make_unique<Engine::FWorldContext>(Previous->GetWorldType())};
        Engine::UWorld* World{Context->GetWorld()};
        World->SetAssets(Previous->GetAssets());
        Context->GetEditorCamera() = mEngine->GetWorldContext(mWorld)->GetEditorCamera();

        Serialization::FMemoryArchive Archive{State.mBytes};
        Engine::FWorldSerializer::Serialize(Archive, Context.get());

        if (Archive.HasError() || Archive.GetRemainingBytes() != 0) {
            AddLog(Archive.HasError() ? "Cannot restore scene history: " + Archive.GetError() : "Cannot restore scene history: unread data");

            return false;
        }

        Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> SelectedActors{};
        CoreUObject::TObjectRef<CoreUObject::UObject> SelectedObject{};
        Core::TMap<Core::FGuid, CoreUObject::TObjectRef<Engine::AActor>, Core::FGuidHash> Actors{};

        for (const auto& Actor : World->GetPersistentLevel()->GetActors()) {
            Actors.emplace(Actor->GetGuid(), CoreUObject::MakeObjectRef(Actor));

            if (Actor->GetGuid() == State.mSelectedObject) {
                SelectedObject = CoreUObject::MakeObjectRef(Actor);
            }

            for (const auto& Component : Actor->GetComponents()) {
                if (Component->GetGuid() == State.mSelectedObject) {
                    SelectedObject = CoreUObject::MakeObjectRef(Component);
                }
            }
        }

        for (const Core::FGuid& Guid : State.mSelectedActors) {
            if (const auto Position{Actors.find(Guid)}; Position != Actors.end()) {
                SelectedActors.push_back(Position->second);
            }
        }

        RestorePreview();

        if (!ReplaceWorld(std::move(Context))) {
            AddLog("Cannot replace the editor world for scene history");

            return false;
        }

        AssignSelection(SelectedActors, SelectedObject);

        return true;
    }

    void Editor::FEditorContext::NoteSceneEdit() {
        if (IsSceneEditActive()) {
            mSceneEditChanged = true;
        } else {
            ClearSceneHistory();
        }
    }

    void Editor::FEditorContext::ClearSceneHistory() {
        mSceneHistory.clear();
        mPendingSceneEdit = {};
        mSceneHistoryPosition = 0;
        mSceneEditToken = 0;
        mSceneEditChanged = false;
        mSceneRevision = ++mSceneEditSerial;
    }

    void Editor::FEditorContext::TrimSceneHistory() {
        constexpr std::size_t MaxEntries{128};
        constexpr std::size_t MaxBytes{64 * 1024 * 1024};
        Index_t First{static_cast<Index_t>(mSceneHistory.size() - 1)};
        std::size_t Bytes{mSceneHistory[First].mBefore.mBytes.size() + mSceneHistory[First].mAfter.mBytes.size()};

        while (First != 0 && mSceneHistory.size() - First < MaxEntries && Bytes < MaxBytes) {
            const FSceneEditCommand& Previous{mSceneHistory[First - 1]};
            const std::size_t Size{Previous.mBefore.mBytes.size() + Previous.mAfter.mBytes.size()};

            if (Size > MaxBytes - Bytes) {
                break;
            }

            Bytes += Size;
            --First;
        }

        mSceneHistory.erase(mSceneHistory.begin(), mSceneHistory.begin() + First);
        mSceneHistoryPosition -= First;
    }

    /**
     * @brief 편집 상태 변경을 처리 중이어서 재진입이 제한되는지 반환합니다.
     */
    bool Editor::FEditorContext::IsChangingState() const {
        return mChangingState || mEditingProperty;
    }

    /**
     * @brief 렌더러의 비소유 연결을 반환합니다.
     */
    RenderCore::IRendererModule* Editor::FEditorContext::GetRenderer() const {
        return mEngine != nullptr ? mEngine->GetRenderer() : nullptr;
    }

    /**
     * @brief 패널 순회 중 액터 수명 변경을 분리할 핸들 스냅샷을 만듭니다.
     */
    Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Editor::FEditorContext::GetActorRefs() const {
        Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Handles{};
        const Engine::UWorld* World{GetWorld()};

        if (World != nullptr && World->GetPersistentLevel() != nullptr) {
            for (const auto& Actor : World->GetPersistentLevel()->GetActors()) {
                if (!Actor->IsPendingDestroy()) {
                    Handles.push_back(CoreUObject::MakeObjectRef(Actor));
                }
            }
        }

        return Handles;
    }

    /**
     * @brief 선택에 대응하는 프로퍼티 표시 대상의 핸들 스냅샷을 만듭니다.
     */
    Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>> Editor::FEditorContext::GetDetailsObjects() const {
        Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>> Handles{};
        CoreUObject::UObject* Object{GetSelectedObject()};

        if (Object == nullptr) {
            return Handles;
        }

        Handles.push_back(CoreUObject::MakeObjectRef(Object));

        if (const auto* Actor{CoreUObject::Cast<Engine::AActor>(Object)}) {
            for (const auto& Component : Actor->GetComponents()) {
                Handles.push_back(CoreUObject::MakeObjectRef(Component));
            }
        }

        return Handles;
    }

    /**
     * @brief 객체의 전역 식별과 실제 편집 월드 소속을 함께 검사합니다.
     */
    CoreUObject::UObject* Editor::FEditorContext::ResolveObject(CoreUObject::TObjectRef<CoreUObject::UObject> Handle) const {
        Engine::UWorld* World{GetWorld()};
        CoreUObject::UObject* Object{Handle.Get()};

        return World != nullptr && World->ContainsObject(Object) ? Object : nullptr;
    }

    /**
     * @brief 유효한 액터를 선택하고 프로퍼티 편집 대상으로 지정합니다.
     */
    bool Editor::FEditorContext::SelectActor(CoreUObject::TObjectRef<Engine::AActor> Handle) {
        return SetSelection({Handle}, Handle);
    }

    /**
     * @brief 객체 선택과 소유 액터 선택을 함께 갱신합니다.
     */
    bool Editor::FEditorContext::SelectObject(CoreUObject::TObjectRef<CoreUObject::UObject> Handle) {
        CoreUObject::UObject* Object{ResolveObject(Handle)};

        if (Object == nullptr) {
            return false;
        }

        Engine::AActor* Actor{CoreUObject::Cast<Engine::AActor>(Object)};

        if (auto* Component{CoreUObject::Cast<Engine::UActorComponent>(Object)}) {
            Actor = Component->GetOwner();
        }

        return SetSelection(Actor != nullptr ? Core::TArray<CoreUObject::TObjectRef<Engine::AActor>>{CoreUObject::MakeObjectRef(Actor)} : Core::TArray<CoreUObject::TObjectRef<Engine::AActor>>{}, Handle);
    }

    /**
     * @brief 액터와 객체 선택 핸들을 모두 무효화합니다.
     */
    void Editor::FEditorContext::ClearSelection() {
        SetSelection({}, {});
    }

    /**
     * @brief 요청 대상이 유효한지 확인하고 선택을 적용합니다.
     */
    bool Editor::FEditorContext::SetSelection(const Core::TArray<CoreUObject::TObjectRef<Engine::AActor>>& Actors, CoreUObject::TObjectRef<CoreUObject::UObject> Object) {
        if (mPlayLocked || IsChangingState()) {
            return false;
        }

        FOperationScope Scope{&mChangingState};

        for (auto Position{Actors.begin()}; Position != Actors.end(); ++Position) {
            const CoreUObject::TObjectRef<CoreUObject::UObject> Handle{*Position};

            if (CoreUObject::Cast<Engine::AActor>(ResolveObject(Handle)) == nullptr || std::find(Actors.begin(), Position, Handle) != Position) {
                return false;
            }
        }

        if (Object.IsValid() && ResolveObject(Object) == nullptr) {
            return false;
        }

        if (mSelectedActors != Actors || mSelectedObject != Object) {
            AssignSelection(Actors, Object);
        }

        return true;
    }

    /**
     * @brief 검증한 선택 상태를 저장합니다.
     */
    void Editor::FEditorContext::AssignSelection(const Core::TArray<CoreUObject::TObjectRef<Engine::AActor>>& Actors, CoreUObject::TObjectRef<CoreUObject::UObject> Object) {
        mSelectedActors = Actors;
        mSelectedObject = Object;
        ++mSelectionRevision;
    }

    /**
     * @brief 선택한 액터를 월드 소속과 함께 검증합니다.
     */
    Engine::AActor* Editor::FEditorContext::GetSelectedActor() const {
        for (auto Position{mSelectedActors.rbegin()}; Position != mSelectedActors.rend(); ++Position) {
            if (auto* Actor{CoreUObject::Cast<Engine::AActor>(ResolveObject(*Position))}) {
                return Actor;
            }
        }

        return nullptr;
    }

    /**
     * @brief 프로퍼티 편집 대상을 월드 소속과 함께 검증합니다.
     */
    CoreUObject::UObject* Editor::FEditorContext::GetSelectedObject() const {
        return ResolveObject(mSelectedObject);
    }

    /**
     * @brief 지정한 기본 메시 종류의 액터를 생성하여 영속 레벨에 추가합니다.
     */
    Engine::AStaticMeshActor* Editor::FEditorContext::SpawnPrimitive(Engine::EPrimitiveType Type) {
        if (!CanModifyScene()) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        const FTransform Placement{mPlacementProvider ? mPlacementProvider() : FTransform{}};
        Engine::UWorld* World{GetWorld()};

        if (World == nullptr) {
            return nullptr;
        }

        Engine::AStaticMeshActor* Actor{World->SpawnPrimitive(Type)};

        if (!ApplySpawnPlacement(Actor, Placement)) {
            return nullptr;
        }

        CompleteActorSpawn(Actor, "Spawned ");

        return Actor;
    }

    /**
     * @brief 편집 월드에 기본 카메라 액터를 생성합니다.
     */
    Engine::ACameraActor* Editor::FEditorContext::SpawnCamera() {
        if (!CanModifyScene()) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        const FTransform Placement{mPlacementProvider ? mPlacementProvider() : FTransform{}};
        Engine::UWorld* World{GetWorld()};
        auto* Actor{World != nullptr ? CoreUObject::Cast<Engine::ACameraActor>(World->SpawnActor(Engine::ACameraActor::StaticClass())) : nullptr};

        if (!ApplySpawnPlacement(Actor, Placement)) {
            return nullptr;
        }

        if (World->GetActiveCamera() == nullptr) {
            World->SetActiveCamera(Actor->GetCameraComponent());

            if (World->GetInputTarget() == nullptr) {
                World->SetInputTarget(Actor);
            }
        }

        CompleteActorSpawn(Actor, "Spawned ");

        return Actor;
    }

    /**
     * @brief 배치 위치에 빌보드 텍스트 액터를 생성하고 선택합니다.
     */
    Engine::AActor* Editor::FEditorContext::SpawnText() {
        if (!CanModifyScene()) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Engine::UWorld* World{GetWorld()};
        Engine::AActor* Actor{World != nullptr ? World->SpawnActor(Engine::AActor::StaticClass()) : nullptr};

        if (Actor == nullptr) {
            return nullptr;
        }

        auto Component{CoreUObject::NewObject<Engine::UBillboardTextComponent>()};
        Component->SetName("BillboardTextComponent");

        if (!Actor->AddComponent(std::move(Component))) {
            World->DestroyActor(CoreUObject::MakeObjectRef(Actor));

            return nullptr;
        }

        Actor->SetName("Text");

        const FTransform Placement{mPlacementProvider ? mPlacementProvider() : FTransform{}};

        if (!ApplySpawnPlacement(Actor, Placement)) {
            return nullptr;
        }

        CompleteActorSpawn(Actor, "Spawned ");

        return Actor;
    }

    /**
     * @brief 배치 위치에 텍스처 빌보드 액터를 생성하고 선택과 장면 변경 상태를 갱신합니다.
     */
    Engine::AActor* Editor::FEditorContext::SpawnBillboard(const Core::FString& SpritePath) {
        if (!CanModifyScene()) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Engine::UWorld* World{GetWorld()};
        Engine::AActor* Actor{World != nullptr ? World->SpawnActor(Engine::AActor::StaticClass()) : nullptr};

        if (Actor == nullptr) {
            return nullptr;
        }

        auto Component{CoreUObject::NewObject<Engine::UBillboardComponent>()};
        Component->SetName("BillboardComponent");

        Engine::UBillboardComponent* Billboard{Component.get()};

        if (!Actor->AddComponent(std::move(Component)) || !Billboard->SetTexturePath(SpritePath)) {
            World->DestroyActor(CoreUObject::MakeObjectRef(Actor));

            return nullptr;
        }

        Actor->SetName(SpritePath.empty() ? "Billboard" : Core::FilePath::ToUtf8(Core::FilePath::FromUtf8(SpritePath).stem()));

        const FTransform Placement{mPlacementProvider ? mPlacementProvider() : FTransform{}};

        if (!ApplySpawnPlacement(Actor, Placement)) {
            return nullptr;
        }

        CompleteActorSpawn(Actor, "Spawned ");

        return Actor;
    }

    /**
     * @brief 새 액터에 배치 변환을 적용하고 실패하면 생성한 액터를 제거합니다.
     */
    bool Editor::FEditorContext::ApplySpawnPlacement(Engine::AActor* Actor, const FTransform& Placement) {
        if (Actor == nullptr) {
            return false;
        }

        if (!Actor->SetActorTransform(Placement)) {
            GetWorld()->DestroyActor(CoreUObject::MakeObjectRef(Actor));

            return false;
        }

        return true;
    }

    /**
     * @brief 생성한 액터를 선택하고 장면 변경 상태와 로그를 갱신합니다.
     */
    void Editor::FEditorContext::CompleteActorSpawn(Engine::AActor* Actor, const char* Action) {
        AssignSelection({CoreUObject::MakeObjectRef(Actor)}, CoreUObject::MakeObjectRef(Actor));

        NoteSceneEdit();

        AddLog(Action + Actor->GetName());
    }

    /**
     * @brief 현재 선택 액터와 편집 상태에서 해당 컴포넌트를 생성할 수 있는지 검사합니다.
     */
    bool Editor::FEditorContext::CanAddComponent(const CoreUObject::UClass* Class) const {
        return CanModifyScene() && GetSelectedActors().size() == 1 && Class != nullptr && Class != Engine::UActorComponent::StaticClass() && Class->IsChildOf(Engine::UActorComponent::StaticClass()) && Class->CanCreate();
    }

    /**
     * @brief 선택한 액터에 리플렉션 클래스로 컴포넌트를 생성하여 등록합니다.
     */
    Engine::UActorComponent* Editor::FEditorContext::AddComponent(const CoreUObject::UClass* Class) {
        if (!CanAddComponent(Class)) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Engine::AActor* Actor{GetSelectedActor()};
        const CoreUObject::TObjectRef<Engine::AActor> ActorHandle{CoreUObject::MakeObjectRef(Actor)};
        auto Object{Class->CreateInstance()};

        if (Object == nullptr || Object->GetClass() != Class) {
            return nullptr;
        }

        std::unique_ptr<Engine::UActorComponent> Component{static_cast<Engine::UActorComponent*>(Object.release())};
        Core::FString BaseName{Class->GetName()};

        if (BaseName.starts_with("U")) {
            BaseName.erase(0, 1);
        }

        Core::FString Name{BaseName};
        Uint32 Suffix{2};

        while (CoreUObject::FObjectRegistry::Get()->FindObject(Core::FName{Name}, Actor) != nullptr) {
            Name = BaseName + "_" + std::to_string(Suffix++);
        }

        Component->SetName(Name);

        if (auto* SceneComponent{CoreUObject::Cast<Engine::USceneComponent>(Component.get())}; SceneComponent != nullptr && Actor->GetRootComponent() != nullptr) {
            if (!SceneComponent->SetTransform(Actor->GetRootComponent()->GetTransform())) {
                return nullptr;
            }
        }

        const CoreUObject::TObjectRef<CoreUObject::UObject> ComponentHandle{CoreUObject::MakeObjectRef(Component)};

        if (!Actor->AddComponent(std::move(Component))) {
            return nullptr;
        }

        NoteSceneEdit();

        auto* Added{CoreUObject::Cast<Engine::UActorComponent>(ResolveObject(ComponentHandle))};

        if (Added == nullptr) {
            return nullptr;
        }

        AssignSelection({ActorHandle}, ComponentHandle);

        AddLog("Added " + Name);

        return Added;
    }

    bool Editor::FEditorContext::CanAttachComponent(const Engine::USceneComponent* Component, const Engine::USceneComponent* Parent) const {
        return CanModifyScene() && GetWorld()->ContainsObject(Component) && (Parent == nullptr || GetWorld()->ContainsObject(Parent)) && Component->CanSetAttachParent(Parent);
    }

    bool Editor::FEditorContext::AttachComponent(Engine::USceneComponent* Component, Engine::USceneComponent* Parent) {
        if (!CanAttachComponent(Component, Parent)) {
            return false;
        }

        FSceneEditScope Edit{*this};

        if (!Component->SetAttachParent(Parent)) {
            return false;
        }

        NoteSceneEdit();

        return true;
    }

    bool Editor::FEditorContext::CanPromoteRootComponent(const Engine::USceneComponent* Component) const {
        return CanModifyScene() && GetWorld()->ContainsObject(Component) && Component->GetOwner()->CanPromoteRootComponent(Component);
    }

    bool Editor::FEditorContext::PromoteRootComponent(Engine::USceneComponent* Component) {
        if (!CanPromoteRootComponent(Component)) {
            return false;
        }

        FSceneEditScope Edit{*this};

        if (!Component->GetOwner()->PromoteRootComponent(Component)) {
            return false;
        }

        ++mSelectionRevision;
        NoteSceneEdit();

        return true;
    }

    /**
     * @brief 현재 선택에서 사용할 수 있는 카메라 컴포넌트를 조회합니다.
     */
    Engine::UCameraComponent* Editor::FEditorContext::GetSelectedCamera() const {
        if (auto* Camera{CoreUObject::Cast<Engine::UCameraComponent>(GetSelectedObject())}) {
            return Camera;
        }

        if (const Engine::AActor* Actor{GetSelectedActor()}) {
            for (const auto& Component : Actor->GetComponents()) {
                if (auto* Camera{CoreUObject::Cast<Engine::UCameraComponent>(Component.get())}) {
                    return Camera;
                }
            }
        }

        return nullptr;
    }

    /**
     * @brief 선택한 카메라를 월드의 활성 카메라로 지정합니다.
     */
    bool Editor::FEditorContext::UseSelectedCamera() {
        Engine::UCameraComponent* Camera{GetSelectedCamera()};

        if (!CanModifyScene() || Camera == nullptr) {
            return false;
        }

        FSceneEditScope Edit{*this};

        if (!GetWorld()->SetActiveCamera(Camera)) {
            return false;
        }

        NoteSceneEdit();

        return GetWorld()->SetInputTarget(Camera->GetOwner());
    }

    bool Editor::FEditorContext::SetActorsVisible(const Core::TArray<CoreUObject::TObjectRef<Engine::AActor>>& Actors, bool Visible) {
        if (!CanModifyScene()) {
            return false;
        }

        FSceneEditScope Edit{*this};
        bool Changed{};

        for (CoreUObject::TObjectRef<Engine::AActor> Handle : Actors) {
            if (auto* Actor{CoreUObject::Cast<Engine::AActor>(ResolveObject(Handle))}; Actor != nullptr && Actor->IsVisible() != Visible) {
                Actor->SetVisible(Visible);
                Changed = true;
            }
        }

        if (Changed) {
            NoteSceneEdit();
        }

        return Changed;
    }

    /**
     * @brief 현재 선택한 액터를 제거하고 관련 선택 상태를 초기화합니다.
     */
    bool Editor::FEditorContext::DestroySelectedActor() {
        if (!CanModifyScene()) {
            return false;
        }

        const Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Actors{GetSelectedActors()};

        if (Actors.empty()) {
            return false;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Engine::UWorld* World{GetWorld()};
        bool Removed{};

        for (CoreUObject::TObjectRef<Engine::AActor> Handle : Actors) {
            Removed = World->DestroyActor(Handle) || Removed;
        }

        if (Removed) {
            AssignSelection({}, {});

            NoteSceneEdit();

            AddLog("Deleted selected actors");
        }

        return Removed;
    }

    bool Editor::FEditorContext::DestroySelectedComponent() {
        if (!CanModifyScene() || IsSceneEditActive()) {
            return false;
        }

        auto* Component{CoreUObject::Cast<Engine::UActorComponent>(GetSelectedObject())};

        if (Component == nullptr) {
            return false;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Engine::AActor* Actor{Component->GetOwner()};
        const CoreUObject::TObjectRef<Engine::AActor> ActorHandle{CoreUObject::MakeObjectRef(Actor)};
        const Core::FString Name{Component->GetName()};

        if (!Actor->RemoveComponent(Component)) {
            return false;
        }

        AssignSelection({ActorHandle}, ActorHandle);
        NoteSceneEdit();
        AddLog("Deleted " + Name);

        return true;
    }

    bool Editor::FEditorContext::DestroySelection() {
        if (CoreUObject::Cast<Engine::UActorComponent>(GetSelectedObject()) != nullptr) {
            return DestroySelectedComponent();
        }

        return DestroySelectedActor();
    }

    /**
     * @brief 편집 월드, 객체, 프로퍼티 소속과 편집 권한을 함께 검사합니다.
     */
    bool Editor::FEditorContext::CanEditProperty(CoreUObject::TObjectRef<CoreUObject::UObject> Handle, const CoreUObject::FProperty* Property) const {
        CoreUObject::UObject* Object{ResolveObject(Handle)};

        return !mEditingProperty && CanEditWorld() && Object != nullptr && Property != nullptr && Object->GetClass()->FindClassProperty(Property->GetName()) == Property && Property->HasFlag(Core::EPropertyFlags::Edit) && !Property->HasFlag(Core::EPropertyFlags::ReadOnly);
    }

    /**
     * @brief 프로퍼티 소속과 편집 권한을 확인하고 변경 전후 알림과 함께 값을 씁니다.
     */
    bool Editor::FEditorContext::EditProperty(CoreUObject::TObjectRef<CoreUObject::UObject> Handle, const CoreUObject::FProperty* Property, const CoreUObject::FPropertyValue& Value, bool Interactive) {
        if (!CanEditProperty(Handle, Property)) {
            return false;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Editing{&mEditingProperty};
        Engine::UWorld* World{GetWorld()};
        Engine::UWorld::FDispatchScope Scope{World};
        CoreUObject::UObject* Object{ResolveObject(Handle)};
        Object->PreEditChange(Property);

        if (ResolveObject(Handle) != Object) {
            return false;
        }

        if (!Property->WriteValue(Object, Value)) {
            AddLog("Rejected invalid property value: " + Property->GetName());

            return false;
        }

        Object->PostEditChangeProperty(CoreUObject::FPropertyChangedEvent{Property, Interactive});

        NoteSceneEdit();

        return true;
    }

    /**
     * @brief 액터 루트의 변환 프로퍼티를 에디터 편집 경로로 변경합니다.
     */
    bool Editor::FEditorContext::ApplyTransform(CoreUObject::TObjectRef<CoreUObject::UObject> Handle, const FTransform& Transform, bool Interactive) {
        CoreUObject::UObject* Object{ResolveObject(Handle)};
        Engine::AActor* Actor{CoreUObject::Cast<Engine::AActor>(Object)};
        Engine::USceneComponent* Root{Actor != nullptr ? Actor->GetRootComponent() : CoreUObject::Cast<Engine::USceneComponent>(Object)};
        const CoreUObject::FProperty* Property{Root != nullptr ? Root->GetClass()->FindClassProperty("Transform") : nullptr};

        return Property != nullptr && EditProperty(CoreUObject::MakeObjectRef(Root), Property, CoreUObject::FPropertyValue{Transform}, Interactive);
    }

    /**
     * @brief 편집 월드를 지정한 장면 파일로 저장하고 변경 표시를 해제합니다.
     */
    bool Editor::FEditorContext::SaveScene(const std::filesystem::path& Path) {
        if (mPlayLocked) {
            AddLog("Stop PIE before saving the scene");

            return false;
        }

        if (Path.empty() || IsSceneEditActive() || !CanModifyScene()) {
            return false;
        }

        RestorePreview();

        FOperationScope Scope{&mChangingState};
        Core::FString Error{};
        Engine::FProject* Project{GetProject()};
        const std::filesystem::path ScenePath{Project != nullptr ? Project->GetAssets()->ResolveWritablePath(Path, Error) : std::filesystem::path{}};

        if (ScenePath.empty()) {
            AddLog(Error.empty() ? "Open a project before saving a scene" : Error);

            return false;
        }

        if (mCameraProvider) {
            mEngine->GetWorldContext(mWorld)->GetEditorCamera() = mCameraProvider();
        }

        if (!Engine::FSceneSerializer::Save(ScenePath, mEngine->GetWorldContext(mWorld), Error)) {
            AddLog(Error);

            return false;
        }

        mScenePath = ScenePath;
        mSavedSceneRevision = mSceneRevision;

        AddLog("Saved " + Core::FilePath::ToUtf8(ScenePath));

        if (mSceneSavedHandler) {
            mSceneSavedHandler();
        }

        return true;
    }

    void Editor::FEditorContext::SetSceneSavedHandler(std::function<void()> Handler) {
        mSceneSavedHandler = std::move(Handler);
    }

    /**
     * @brief 새 컨텍스트를 엔진에 등록하고 기존 편집 월드만 교체합니다.
     */
    bool Editor::FEditorContext::ReplaceScene(std::unique_ptr<Engine::FWorldContext> Context, const std::filesystem::path& Path) {
        RestorePreview();

        if (!ReplaceWorld(std::move(Context))) {
            return false;
        }

        AssignSelection({}, {});
        mScenePath = Path;
        ClearSceneHistory();
        mSavedSceneRevision = mSceneRevision;

        if (mCameraRestorer) {
            mCameraRestorer(mEngine->GetWorldContext(mWorld)->GetEditorCamera());
        }

        return true;
    }

    bool Editor::FEditorContext::ReplaceWorld(std::unique_ptr<Engine::FWorldContext> Context) {
        const Engine::FWorldHandle Previous{mWorld};
        const Engine::FWorldHandle Current{mEngine->GetWorldHandle()};
        const Engine::FWorldHandle Replacement{mEngine->RegisterWorld(std::move(Context))};

        if (!Replacement.IsValid()) {
            return false;
        }

        if (mEngine->GetWorld(Previous) != nullptr && !mEngine->DestroyWorld(Previous)) {
            mEngine->DestroyWorld(Replacement);

            return false;
        }

        if (Current == Previous || !Current.IsValid()) {
            mEngine->SetCurrentWorld(Replacement);
        }

        mWorld = Replacement;

        return true;
    }

    /**
     * @brief 프로퍼티 편집을 거치지 않은 장면 변경을 수정 상태로 표시합니다.
     */
    void Editor::FEditorContext::MarkSceneDirty() {
        ClearSceneHistory();
    }

    /**
     * @brief 시퀀서 프리뷰가 월드에 쓴 값을 되돌리는 콜백을 설정합니다.
     */
    void Editor::FEditorContext::SetPreviewRestorer(std::function<void()> Restorer) {
        mPreviewRestorer = std::move(Restorer);
    }

    /**
     * @brief 시퀀서 프리뷰가 월드에 쓴 값을 되돌립니다.
     */
    void Editor::FEditorContext::RestorePreview() {
        if (mPreviewRestorer) {
            mPreviewRestorer();
        }
    }

    /**
     * @brief 시퀀스 액터를 시퀀서에서 여는 콜백을 설정합니다.
     */
    void Editor::FEditorContext::SetSequencerOpener(std::function<void(CoreUObject::TObjectRef<Engine::ALevelSequenceActor>)> Opener) {
        mSequencerOpener = std::move(Opener);
    }

    /**
     * @brief 객체가 시퀀스 액터이면 시퀀서에서 엽니다.
     */
    bool Editor::FEditorContext::OpenInSequencer(CoreUObject::TObjectRef<Engine::ALevelSequenceActor> Handle) {
        if (!mSequencerOpener || CoreUObject::Cast<Engine::ALevelSequenceActor>(ResolveObject(Handle)) == nullptr) {
            return false;
        }

        mSequencerOpener(Handle);

        return true;
    }

    bool Editor::FEditorContext::FocusActor(CoreUObject::TObjectRef<Engine::AActor> Handle) {
        auto* Actor{CoreUObject::Cast<Engine::AActor>(ResolveObject(Handle))};

        if (Actor == nullptr) {
            return false;
        }

        Editor::FEditorViewport* Viewport{GetActiveViewport()};

        if (Viewport == nullptr || Viewport->GetSceneViewport() == nullptr || Viewport->GetActiveClient() != Viewport->GetClient()) {
            return false;
        }

        Viewport->GetClient()->FocusActor(Actor, Viewport->GetSceneViewport()->GetSize());

        return true;
    }

    /**
     * @brief 공통 편집 작업이 사용할 뷰포트 제공자를 설정합니다.
     */
    void Editor::FEditorContext::SetViewportProvider(std::function<Editor::FEditorViewport*()> Provider) {
        mViewportProvider = std::move(Provider);
    }

    Editor::FEditorViewport* Editor::FEditorContext::GetActiveViewport() const {
        return mViewportProvider ? mViewportProvider() : nullptr;
    }

    /**
     * @brief 원근 편집 뷰포트가 대신 사용할 시퀀서 프리뷰 카메라를 설정합니다.
     */
    void Editor::FEditorContext::SetPreviewCamera(CoreUObject::TObjectRef<Engine::UCameraComponent> Camera) {
        mPreviewCamera = Camera;
    }

    /**
     * @brief 시퀀서 프리뷰 카메라를 조회합니다.
     */
    Engine::UCameraComponent* Editor::FEditorContext::GetPreviewCamera() const {
        return CoreUObject::Cast<Engine::UCameraComponent>(ResolveObject(mPreviewCamera));
    }

    /**
     * @brief 장면 저장·복원에 사용할 원근 카메라 콜백을 연결합니다.
     */
    void Editor::FEditorContext::SetCameraHandlers(std::function<Engine::FEditorCameraState()> Provider, std::function<void(const Engine::FEditorCameraState&)> Restorer) {
        mCameraProvider = std::move(Provider);
        mCameraRestorer = std::move(Restorer);
    }

    /**
     * @brief 슬롯 재질을 변경하고 장면을 수정 상태로 표시합니다.
     */
    bool Editor::FEditorContext::SetMaterialOverride(CoreUObject::TObjectRef<CoreUObject::UObject> Handle, Index_t Slot, const Core::FString& Path) {
        if (!CanModifyScene()) {
            return false;
        }

        FSceneEditScope Edit{*this};
        auto* Component{CoreUObject::Cast<Engine::UMeshComponent>(ResolveObject(Handle))};

        if (Component == nullptr || !Component->SetSlotMaterialPath(Slot, Path)) {
            return false;
        }

        NoteSceneEdit();

        return true;
    }

    /**
     * @brief 장면 파일을 새 컨텍스트에 읽고 편집 대상 월드만 교체합니다.
     */
    bool Editor::FEditorContext::LoadScene(const std::filesystem::path& Path) {
        if (Path.empty() || IsSceneEditActive() || !CanModifyScene()) {
            return false;
        }

        FOperationScope Scope{&mChangingState};
        Core::FString Error{};
        Engine::FProject* Project{GetProject()};
        const std::filesystem::path ScenePath{Project != nullptr ? Project->GetAssets()->ResolvePath(Path, Error) : std::filesystem::path{}};

        if (ScenePath.empty()) {
            AddLog(Error.empty() ? "Open a project before loading a scene" : Error);

            return false;
        }

        auto Context{Engine::FSceneSerializer::Load(ScenePath, Error, Project->GetAssets())};

        if (Context == nullptr) {
            AddLog(Error);

            return false;
        }

        if (!ReplaceScene(std::move(Context), ScenePath)) {
            return false;
        }

        if (!Project->GetAssets()->MigrateAsset(ScenePath, Error)) {
            AddLog("Cannot migrate '" + Core::FilePath::ToUtf8(ScenePath) + "': " + Error);
        }

        AddLog("Loaded " + Core::FilePath::ToUtf8(ScenePath));

        return true;
    }

    /**
     * @brief 빈 편집 월드로 교체하고 선택 및 장면 파일 상태를 초기화합니다.
     */
    bool Editor::FEditorContext::NewScene() {
        if (IsSceneEditActive() || !CanModifyScene()) {
            return false;
        }

        FOperationScope Scope{&mChangingState};

        return ReplaceScene(std::make_unique<Engine::FWorldContext>(), {});
    }

    /**
     * @brief 현재 장면 파일 경로를 반환합니다.
     */
    const std::filesystem::path& Editor::FEditorContext::GetScenePath() const {
        return mScenePath;
    }

    /**
     * @brief 저장 후 변경되어 저장이 필요한 장면인지 검사합니다.
     */
    bool Editor::FEditorContext::IsSceneDirty() const {
        const Engine::FWorldContext* Context{mEngine != nullptr ? mEngine->GetWorldContext(mWorld) : nullptr};

        return mSceneEditChanged || mSceneRevision != mSavedSceneRevision || (Context != nullptr && mCameraProvider && Context->GetEditorCamera() != mCameraProvider());
    }

    /**
     * @brief 현재 연결된 프로젝트의 비소유 주소를 반환합니다.
     */
    Engine::FProject* Editor::FEditorContext::GetProject() const {
        return mEngine != nullptr ? mEngine->GetProject() : nullptr;
    }

    /**
     * @brief 배치 제공자가 반환한 위치에 메시 에셋 액터를 생성합니다.
     */
    Engine::AStaticMeshActor* Editor::FEditorContext::SpawnMesh(const std::filesystem::path& Path) {
        return SpawnMesh(Path, mPlacementProvider ? mPlacementProvider() : FTransform{});
    }

    /**
     * @brief 지정한 변환에 메시 에셋 액터를 생성하고 선택합니다.
     */
    Engine::AStaticMeshActor* Editor::FEditorContext::SpawnMesh(const std::filesystem::path& Path, const FTransform& Placement) {
        if (!CanModifyScene() || !Placement.IsValid()) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Engine::FProject* Project{GetProject()};
        Core::FString Error{};

        if (Project == nullptr || Project->GetAssets()->LoadMesh(Path, Error) == nullptr) {
            AddLog(Error.empty() ? "Cannot place the mesh" : Error);

            return nullptr;
        }

        Engine::AStaticMeshActor* Actor{GetWorld()->SpawnPrimitive(Engine::EPrimitiveType::Cube)};

        if (!ApplySpawnPlacement(Actor, Placement)) {
            return nullptr;
        }

        if (!Actor->GetStaticMeshComponent()->SetMeshPath(Project->GetAssets()->GetRelativePath(Path, Error))) {
            GetWorld()->DestroyActor(CoreUObject::MakeObjectRef(Actor));

            return nullptr;
        }

        Actor->SetName(Core::FilePath::ToUtf8(Path.stem()));
        CompleteActorSpawn(Actor, "Placed ");

        return Actor;
    }

    /**
     * @brief 애플리케이션에 프로젝트 전환을 요청할 콜백을 설정합니다.
     */
    void Editor::FEditorContext::SetProjectRequestHandler(std::function<void(const std::filesystem::path&, bool)> Handler) {
        mProjectRequestHandler = std::move(Handler);
    }

    /**
     * @brief 지정 위치에 스켈레탈 메시 액터를 배치하고 선택합니다.
     */
    Engine::ASkeletalMeshActor* Editor::FEditorContext::SpawnSkeletalMesh(const std::filesystem::path& Path, const FTransform& Placement) {
        if (!CanModifyScene() || !Placement.IsValid() || GetProject() == nullptr) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Core::FString Error{};
        Asset::FAssetManager* Assets{GetProject()->GetAssets()};

        if (Assets->LoadSkeletalMesh(Path, Error) == nullptr) {
            AddLog(Error);

            return nullptr;
        }

        auto* Actor{CoreUObject::Cast<Engine::ASkeletalMeshActor>(GetWorld()->SpawnActor(Engine::ASkeletalMeshActor::StaticClass()))};

        if (!ApplySpawnPlacement(Actor, Placement)) {
            return nullptr;
        }

        if (Actor->GetSkeletalMeshComponent() == nullptr || !Actor->GetSkeletalMeshComponent()->SetMeshPath(Assets->GetRelativePath(Path, Error))) {
            GetWorld()->DestroyActor(CoreUObject::MakeObjectRef(Actor));

            return nullptr;
        }

        Actor->SetName(Core::FilePath::ToUtf8(Path.stem()));
        CompleteActorSpawn(Actor, "Placed ");

        return Actor;
    }

    /**
     * @brief 등록된 처리기에 프로젝트 생성 또는 열기 요청을 전달합니다.
     */
    void Editor::FEditorContext::RequestProject(const std::filesystem::path& Path, bool Create) {
        if (CanEditWorld() && mProjectRequestHandler) {
            mProjectRequestHandler(Path, Create);
        }
    }

    /**
     * @brief 콘솔 로그 모델에 메시지를 추가합니다.
     */
    void Editor::FEditorContext::AddLog(const Core::FString& Message) {
        SyncGlobalLogs();
        AppendLog(Message);

        OutputDebugStringA((Message + "\n").c_str());
    }

    void Editor::FEditorContext::SyncGlobalLogs() {
        for (const Core::FLogRecord& Record : GLog->ReadBacklog(mGlobalLogSequence)) {
            AppendLog(Record.ToString());
        }
    }

    void Editor::FEditorContext::AppendLog(Core::FString Message) {
        if (mLogs.size() >= 500) {
            mLogs.erase(mLogs.begin(), mLogs.begin() + 100);
        }

        mLogs.push_back(std::move(Message));

        ++mLogRevision;
    }

    /**
     * @brief 수집한 콘솔 로그를 모두 지웁니다.
     */
    void Editor::FEditorContext::ClearLogs() {
        mLogs.clear();

        ++mLogRevision;
    }

    /**
     * @brief UI가 표시할 로그 목록을 반환합니다.
     */
    const Core::TArray<Core::FString>& Editor::FEditorContext::GetLogs() const {
        return mLogs;
    }

    /**
     * @brief 로그의 변경 세대를 반환합니다.
     */
    Uint64 Editor::FEditorContext::GetLogRevision() const {
        return mLogRevision;
    }

    /**
     * @brief 그림자 아틀라스 검사 창의 표시 상태를 반환합니다.
     */
    bool Editor::FEditorContext::IsShadowAtlasOpen() const {
        return mShadowAtlasOpen;
    }

    /**
     * @brief 그림자 아틀라스 검사 창의 표시 상태를 변경합니다.
     */
    void Editor::FEditorContext::SetShadowAtlasOpen(bool Open) {
        mShadowAtlasOpen = Open;
    }

    /**
     * @brief 격자 천 테스트를 켜거나 끕니다.
     */
    void Editor::FEditorContext::SetClothGridTest(bool Enable) {
        if (!Enable) {
            mClothGridTest.reset();

            return;
        }

        if (mClothGridTest != nullptr || mEngine == nullptr) {
            return;
        }

        // 1m 정사각형 천을 2m 높이에 눕히고 Y=0 쪽 변을 고정합니다.
        constexpr Uint32 GridSize{20};
        constexpr float Spacing{1.0f / static_cast<float>(GridSize - 1)};
        auto Simulation{std::make_unique<PhysicsRuntime::FClothingSimulationNv>()};

        if (!Simulation->Initialize(mEngine->GetClothingSystem()) || !Simulation->CreateGridActor({-0.5f, 0.0f, 2.0f}, GridSize, GridSize, Spacing)) {
            AddLog("Cannot create the cloth grid. See the Console for details.");

            return;
        }

        mClothGridTest = std::move(Simulation);
    }

    /**
     * @brief 격자 천 테스트가 켜져 있으면 천을 한 프레임 진행합니다.
     */
    void Editor::FEditorContext::TickClothGridTest(float DeltaTime) {
        if (mClothGridTest != nullptr) {
            mClothGridTest->Simulate(DeltaTime);
        }
    }

    /**
     * @brief 격자 천 테스트의 입자 사이를 선분으로 그립니다.
     */
    void Editor::FEditorContext::DrawClothGridTest(Engine::FPrimitiveDrawInterface& DrawInterface, float Thickness) const {
        if (mClothGridTest == nullptr) {
            return;
        }

        Core::TArray<FVector> Positions{};
        const Uint32 Width{mClothGridTest->GetGridWidth()};
        const Uint32 Height{mClothGridTest->GetGridHeight()};

        if (!mClothGridTest->GetParticles(0, Positions) || Positions.size() != static_cast<Index_t>(Width) * Height) {
            return;
        }

        const FVector Color{0.95f, 0.45f, 0.75f};

        for (Uint32 Row{}; Row < Height; ++Row) {
            for (Uint32 Column{}; Column < Width; ++Column) {
                const Index_t Index{static_cast<Index_t>(Row) * Width + Column};

                if (Column + 1 < Width) {
                    DrawInterface.DrawLine(Positions[Index], Positions[Index + 1], Color, true, Thickness);
                }

                if (Row + 1 < Height) {
                    DrawInterface.DrawLine(Positions[Index], Positions[Index + Width], Color, true, Thickness);
                }
            }
        }
    }

    /**
     * @brief 선택한 컴포넌트가 스켈레탈 메시 컴포넌트이면 그것을, 액터이면 그 액터의 첫 스켈레탈 메시 컴포넌트를 찾습니다.
     */
    Engine::USkeletalMeshComponent* Editor::FEditorContext::FindSelectedSkeletalMeshComponent() {
        if (auto* Component{CoreUObject::Cast<Engine::USkeletalMeshComponent>(GetSelectedObject())}) {
            return Component;
        }

        for (const CoreUObject::TObjectRef<Engine::AActor>& Actor : GetSelectedActors()) {
            if (Actor.Get() == nullptr) {
                continue;
            }

            for (const auto& ActorComponent : Actor->GetComponents()) {
                if (auto* Component{CoreUObject::Cast<Engine::USkeletalMeshComponent>(ActorComponent.get())}) {
                    return Component;
                }
            }
        }

        return nullptr;
    }

    /**
     * @brief 선택한 스켈레탈 메시를 천 데이터와 함께 메시 파일에 저장합니다.
     */
    bool Editor::FEditorContext::SaveClothMesh() {
        Engine::USkeletalMeshComponent* Component{FindSelectedSkeletalMeshComponent()};
        Asset::USkeletalMesh* Mesh{Component != nullptr ? Component->GetSkeletalMesh() : nullptr};
        Engine::FProject* Project{GetProject()};

        if (Mesh == nullptr || Project == nullptr || Component->GetMeshPath().empty()) {
            AddLog("Select a skeletal mesh actor or component first.");

            return false;
        }

        const std::filesystem::path MeshPath{Core::FilePath::FromUtf8(Component->GetMeshPath())};

        // 뼈를 파일 안에 넣은 예전 메시는 새 형식으로 저장하면 뼈가 사라지므로 막습니다.
        if (Project->GetAssets()->FindLegacySkeleton(MeshPath) != nullptr) {
            AddLog("This mesh uses an embedded skeleton, so its file cannot be saved safely. Reimport it with a separate skeleton first.");

            return false;
        }

        Core::FString Error{};

        if (!Project->GetAssets()->SaveSkeletalMesh(MeshPath, *Mesh, Error)) {
            AddLog("Cannot save the skeletal mesh. " + Error);

            return false;
        }

        AddLog("Saved " + Component->GetMeshPath() + " with " + std::to_string(Mesh->GetMeshClothingAssets().size()) + " clothing assets.");

        return true;
    }

    /**
     * @brief 선택한 스켈레탈 메시의 섹션에서 천을 빼고, 이 메시를 쓰는 월드의 컴포넌트 천을 다시 만듭니다.
     */
    bool Editor::FEditorContext::RemoveClothTest(Index_t SubMeshIndex) {
        Engine::USkeletalMeshComponent* Component{FindSelectedSkeletalMeshComponent()};
        Asset::USkeletalMesh* Mesh{Component != nullptr ? Component->GetSkeletalMesh() : nullptr};
        const Asset::FClothingSectionData* SectionData{Mesh != nullptr ? Mesh->GetSectionClothingData(SubMeshIndex) : nullptr};

        if (SectionData == nullptr || !SectionData->IsValid()) {
            AddLog("The selected mesh has no cloth on section " + std::to_string(SubMeshIndex) + ".");

            return false;
        }

        // 이 메시로 천을 만든 컴포넌트는 지울 에셋을 가리키므로, 에셋을 빼기 전에 천을 해제합니다.
        Core::TArray<Engine::USkeletalMeshComponent*> Users{};
        Engine::UWorld* World{GetWorld()};

        if (World != nullptr && World->GetPersistentLevel() != nullptr) {
            for (const auto& Actor : World->GetPersistentLevel()->GetActors()) {
                for (const auto& ActorComponent : Actor->GetComponents()) {
                    if (auto* User{CoreUObject::Cast<Engine::USkeletalMeshComponent>(ActorComponent.get())}; User != nullptr && User->GetSkeletalMesh() == Mesh) {
                        User->ReleaseAllClothingResources();
                        Users.push_back(User);
                    }
                }
            }
        }

        if (mClothAssetTestComponent.Get() != nullptr && mClothAssetTestComponent->GetSkeletalMesh() == Mesh) {
            ClearClothAssetTest();
        }

        if (mClothSimTestComponent.Get() != nullptr && mClothSimTestComponent->GetSkeletalMesh() == Mesh) {
            ClearClothSimTest();
        }

        Mesh->RemoveClothingAsset(SectionData->mAssetGuid);

        for (Engine::USkeletalMeshComponent* User : Users) {
            User->RecreateClothingActors();
        }

        AddLog("Removed the cloth from section " + std::to_string(SubMeshIndex) + ". Run cloth.save to keep the change.");

        return true;
    }

    /**
     * @brief 선택한 스켈레탈 메시의 천 에셋들의 설정 하나를 바꾸고 천을 다시 만듭니다.
     */
    bool Editor::FEditorContext::SetClothConfigTest(const Core::FString& Name, const Core::TArray<float>& Values) {
        Engine::USkeletalMeshComponent* Component{FindSelectedSkeletalMeshComponent()};
        Asset::USkeletalMesh* Mesh{Component != nullptr ? Component->GetSkeletalMesh() : nullptr};

        if (Mesh == nullptr || Mesh->GetMeshClothingAssets().empty()) {
            AddLog("Select a skeletal mesh with a clothing asset first. Run cloth.asset to create one.");

            return false;
        }

        // 설정 이름과 값의 위치를 한곳에 모아 보여 주기와 바꾸기에 함께 씁니다.
        const auto& Fields{Editor::ClothEditing::GetConfigFields()};
        const auto GetVectors{[](PhysicsRuntime::UClothConfigNv& Config) {
            return Editor::ClothEditing::GetConfigVectors(Config);
        }};

        // 이름이 없으면 첫 천 에셋의 설정 값을 보여 줍니다.
        if (Name.empty()) {
            PhysicsRuntime::UClothConfigNv* Config{Mesh->GetMeshClothingAssets().front()->GetClothConfig()};

            if (Config == nullptr) {
                return false;
            }

            for (const auto& Field : Fields) {
                if (Field.HasScalarValue()) {
                    AddLog(Core::FString{Field.mKey} + " = " + std::to_string(Field.GetScalarValue(*Config)));
                }
            }

            for (const auto& [EntryName, Value] : GetVectors(*Config)) {
                AddLog(EntryName + " = " + std::to_string(Value->x) + " " + std::to_string(Value->y) + " " + std::to_string(Value->z));
            }

            AddLog(Core::FString{"usegravityoverride = "} + (Config->mUseGravityOverride ? "1" : "0"));
            AddLog(Core::FString{"windmethod = "} + (Config->mClothingWindMethod == PhysicsRuntime::EClothingWindMethodNv::Accurate ? "1 (accurate)" : "0 (legacy)"));

            return true;
        }

        if (Values.empty()) {
            AddLog("Give a value for " + Name + ".");

            return false;
        }

        bool Found{};

        for (const std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>& ClothingAsset : Mesh->GetMeshClothingAssets()) {
            PhysicsRuntime::UClothConfigNv* Config{ClothingAsset->GetClothConfig()};

            if (Config == nullptr) {
                continue;
            }

            for (const auto& Field : Fields) {
                if (Field.HasScalarValue() && Name == Field.mKey) {
                    Field.SetScalarValue(*Config, Values[0]);
                    Found = true;
                }
            }

            for (const auto& [EntryName, Value] : GetVectors(*Config)) {
                if (EntryName == Name) {
                    *Value = Values.size() >= 3 ? FVector{Values[0], Values[1], Values[2]} : FVector{Values[0], Values[0], Values[0]};
                    Found = true;
                }
            }

            if (Name == "usegravityoverride") {
                Config->mUseGravityOverride = Values[0] != 0.0f;
                Found = true;
            }

            if (Name == "windmethod") {
                Config->mClothingWindMethod = Values[0] != 0.0f ? PhysicsRuntime::EClothingWindMethodNv::Accurate : PhysicsRuntime::EClothingWindMethodNv::Legacy;
                Found = true;
            }

            // 천끼리 충돌 거리가 바뀌었을 수 있으므로 부딪힐 입자를 다시 고릅니다.
            ClothingAsset->BuildSelfCollisionData();
        }

        if (!Found) {
            AddLog("Unknown cloth config '" + Name + "'. Run cloth.config without arguments to see the names.");

            return false;
        }

        // 설정은 천을 만들 때 들어가므로 천을 다시 만듭니다.
        Component->RecreateClothingActors();
        AddLog("Cloth config " + Name + " set.");

        return true;
    }

    /**
     * @brief 선택한 스켈레탈 메시의 천 에셋들이 부딪힐 물리 에셋을 바꾸고 천을 다시 만듭니다.
     */
    bool Editor::FEditorContext::SetClothPhysicsAssetTest(const Core::FString& Path) {
        Engine::USkeletalMeshComponent* Component{FindSelectedSkeletalMeshComponent()};

        if (Component == nullptr) {
            AddLog("Select a skeletal mesh actor or component first.");

            return false;
        }

        Asset::USkeletalMesh* Mesh{Component->GetSkeletalMesh()};
        const Asset::USkeleton* Skeleton{Component->GetSkeleton()};

        if (Mesh == nullptr || Skeleton == nullptr) {
            AddLog("The selected component has no skeletal mesh or skeleton.");

            return false;
        }

        if (Mesh->GetMeshClothingAssets().empty()) {
            AddLog("The selected mesh has no clothing asset. Run cloth.asset first.");

            return false;
        }

        PhysicsRuntime::UPhysicsAsset* PhysicsAsset{};

        Core::FString PhysicsAssetPath{Path};

        if (Path == "mesh") {
            PhysicsAsset = Component->GetPhysicsAsset();
            PhysicsAssetPath = !Component->GetPhysicsAssetOverride().empty() ? Component->GetPhysicsAssetOverride() : Mesh->GetPhysicsAssetPath();
        } else if (Path == "none") {
            PhysicsAssetPath.clear();
            // 물리 에셋을 비우면 천이 몸과 부딪히지 않습니다.
            PhysicsAsset = nullptr;
        } else {
            Engine::FProject* Project{GetProject()};
            Core::FString Error{};
            PhysicsAsset = Project != nullptr ? Project->GetAssets()->LoadPhysicsAsset(Core::FilePath::FromUtf8(Path), Error) : nullptr;

            if (PhysicsAsset == nullptr) {
                AddLog("Cannot load the physics asset '" + Path + "'. " + Error);

                return false;
            }
        }

        Core::TArray<Core::FString> SkeletonBoneNames{};
        SkeletonBoneNames.reserve(Skeleton->GetBones().size());

        for (const Asset::FMeshImportBone& Bone : Skeleton->GetBones()) {
            SkeletonBoneNames.push_back(Bone.GetName());
        }

        Index_t NumShapes{};

        if (PhysicsAsset != nullptr) {
            for (const auto& BodySetup : PhysicsAsset->mSkeletalBodySetups) {
                if (BodySetup != nullptr) {
                    NumShapes += BodySetup->GetAggGeom().mSphereElems.size() + BodySetup->GetAggGeom().mSphylElems.size() * 2;
                }
            }
        }

        for (const std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>& ClothingAsset : Mesh->GetMeshClothingAssets()) {
            ClothingAsset->SetPhysicsAsset(PhysicsAsset, SkeletonBoneNames);
            ClothingAsset->mPhysicsAssetPath = PhysicsAssetPath;
        }

        // 이미 만든 천은 예전 충돌체를 가지고 있으므로 다시 만듭니다.
        Component->RecreateClothingActors();
        AddLog("Cloth physics asset set to " + (PhysicsAsset != nullptr ? Path : Core::FString{"none"}) + " with " + std::to_string(NumShapes) + " collision spheres. NvCloth uses up to 32.");

        return true;
    }

    /**
     * @brief 선택한 스켈레탈 메시의 섹션 하나로 천 에셋을 만들어 연결하고 그리기 대상으로 정합니다.
     */
    bool Editor::FEditorContext::SetClothAssetTest(Index_t SubMeshIndex) {
        Engine::USkeletalMeshComponent* Component{FindSelectedSkeletalMeshComponent()};

        if (Component == nullptr) {
            AddLog("Select a skeletal mesh actor or component first.");

            return false;
        }

        Asset::USkeletalMesh* Mesh{Component->GetSkeletalMesh()};
        const Asset::USkeleton* Skeleton{Component->GetSkeleton()};

        if (Mesh == nullptr || Skeleton == nullptr) {
            AddLog("The selected component has no skeletal mesh or skeleton.");

            return false;
        }

        const Index_t NumSubMeshes{Mesh->GetMeshData().GetSubMeshes().size()};

        if (SubMeshIndex >= NumSubMeshes) {
            AddLog("Section " + std::to_string(SubMeshIndex) + " does not exist. The mesh has " + std::to_string(NumSubMeshes) + " sections.");

            return false;
        }

        const Asset::FClothingSectionData* SectionData{Mesh->GetSectionClothingData(SubMeshIndex)};

        if (SectionData == nullptr || !SectionData->IsValid()) {
            // 따로 정하기 전까지는 컴포넌트가 쓰는 물리 에셋과 부딪히게 합니다. 저장한 물리 에셋이면 경로도 남깁니다.
            const Core::FString PhysicsAssetPath{!Component->GetPhysicsAssetOverride().empty() ? Component->GetPhysicsAssetOverride() : Mesh->GetPhysicsAssetPath()};
            Core::FString CreateError{};

            if (Editor::ClothEditing::CreateClothFromSection(*Mesh, *Skeleton, SubMeshIndex, Component->GetPhysicsAsset(), PhysicsAssetPath, CreateError) == nullptr) {
                AddLog(CreateError);

                return false;
            }

            SectionData = Mesh->GetSectionClothingData(SubMeshIndex);
        }

        const PhysicsRuntime::UClothingAssetCommon* ClothingAsset{Mesh->GetClothingAsset(SectionData->mAssetGuid)};

        if (ClothingAsset == nullptr || !ClothingAsset->IsValidLod(SectionData->mAssetLodIndex)) {
            AddLog("The clothing data of section " + std::to_string(SubMeshIndex) + " is broken.");

            return false;
        }

        const PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{ClothingAsset->mLodData[SectionData->mAssetLodIndex].mPhysicalMeshData};
        const auto NumSkinnedOnly{std::count_if(SectionData->mClothMappingData.begin(), SectionData->mClothMappingData.end(), [](const PhysicsRuntime::FMeshToMeshVertData& VertData) {
            return VertData.mSourceMeshVertIndices[3] == PhysicsRuntime::FMeshToMeshVertData::NoSimulationFlag;
        })};

        AddLog("Cloth section " + std::to_string(SubMeshIndex) + ": " + std::to_string(SectionData->mClothMappingData.size()) + " render verts (" + std::to_string(NumSkinnedOnly) + " skinned only) -> "
            + std::to_string(PhysMesh.mVertices.size()) + " particles, " + std::to_string(PhysMesh.mIndices.size() / 3) + " triangles, " + std::to_string(PhysMesh.mNumFixedVerts) + " fixed, "
            + std::to_string(ClothingAsset->mUsedBoneNames.size()) + " bones.");

        mClothAssetTestComponent = CoreUObject::MakeObjectRef(Component);
        mClothAssetTestSubMesh = SubMeshIndex;

        return true;
    }

    /**
     * @brief 천 에셋 테스트의 그리기 대상을 비웁니다.
     */
    void Editor::FEditorContext::ClearClothAssetTest() {
        mClothAssetTestComponent.Reset();
        mClothAssetTestSubMesh = InvalidIndex;
    }

    /**
     * @brief 천 에셋 테스트 대상의 천 메시를 바인드 포즈로 그립니다.
     */
    void Editor::FEditorContext::DrawClothAssetTest(Engine::FPrimitiveDrawInterface& DrawInterface, float Thickness) const {
        const Engine::USkeletalMeshComponent* Component{mClothAssetTestComponent.Get()};

        if (Component == nullptr || Component->GetSkeletalMesh() == nullptr) {
            return;
        }

        const Asset::USkeletalMesh& Mesh{*Component->GetSkeletalMesh()};
        const Asset::FClothingSectionData* SectionData{Mesh.GetSectionClothingData(mClothAssetTestSubMesh)};

        if (SectionData == nullptr || !SectionData->IsValid()) {
            return;
        }

        const PhysicsRuntime::UClothingAssetCommon* ClothingAsset{Mesh.GetClothingAsset(SectionData->mAssetGuid)};

        if (ClothingAsset == nullptr || !ClothingAsset->IsValidLod(SectionData->mAssetLodIndex)) {
            return;
        }

        const PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{ClothingAsset->mLodData[SectionData->mAssetLodIndex].mPhysicalMeshData};
        const FMatrix WorldMatrix{Component->GetWorldMatrix()};
        const FVector FixedColor{0.95f, 0.2f, 0.2f};
        const FVector MovableColor{0.3f, 0.9f, 0.3f};

        for (Index_t TriangleBaseIndex{}; TriangleBaseIndex + 2 < PhysMesh.mIndices.size(); TriangleBaseIndex += 3) {
            for (Index_t Corner{}; Corner < 3; ++Corner) {
                const Uint32 Start{PhysMesh.mIndices[TriangleBaseIndex + Corner]};
                const Uint32 End{PhysMesh.mIndices[TriangleBaseIndex + (Corner + 1) % 3]};
                const bool Fixed{PhysMesh.mInverseMasses[Start] == 0.0f && PhysMesh.mInverseMasses[End] == 0.0f};

                DrawInterface.DrawLine(WorldMatrix.TransformPosition(PhysMesh.mVertices[Start]), WorldMatrix.TransformPosition(PhysMesh.mVertices[End]), Fixed ? FixedColor : MovableColor, true, Thickness);
            }
        }
    }

    /**
     * @brief 선택한 스켈레탈 메시의 섹션 하나를 천으로 시뮬레이션합니다.
     */
    bool Editor::FEditorContext::SetClothSimTest(Index_t SubMeshIndex) {
        ClearClothSimTest();

        if (mEngine == nullptr) {
            return false;
        }

        // 천 에셋이 없으면 만들고 연결합니다. 실패하면 그 함수가 이유를 기록합니다.
        if (!SetClothAssetTest(SubMeshIndex)) {
            return false;
        }

        Engine::USkeletalMeshComponent* Component{mClothAssetTestComponent.Get()};
        ClearClothAssetTest();

        if (Component == nullptr || Component->GetSkeletalMesh() == nullptr) {
            return false;
        }

        const Asset::USkeletalMesh& Mesh{*Component->GetSkeletalMesh()};
        const Asset::FClothingSectionData* SectionData{Mesh.GetSectionClothingData(SubMeshIndex)};
        const PhysicsRuntime::UClothingAssetCommon* ClothingAsset{SectionData != nullptr ? Mesh.GetClothingAsset(SectionData->mAssetGuid) : nullptr};

        if (ClothingAsset == nullptr) {
            return false;
        }

        // 컴포넌트 좌표 뼈 행렬과 스키닝 행렬이 있어야 입자를 현재 자세로 놓을 수 있습니다.
        const Engine::FSkeletalMeshPose& Pose{Component->GetPose()};

        if (Pose.GetGlobalTransforms().empty() || Pose.GetSkinMatrices().empty()) {
            AddLog("The selected component has no evaluated pose yet.");

            return false;
        }

        auto Simulation{std::make_unique<PhysicsRuntime::FClothingSimulationNv>()};

        if (!Simulation->Initialize(mEngine->GetClothingSystem()) || !Simulation->CreateActor(*ClothingAsset, SectionData->mCorrespondClothAssetIndex, Component->GetWorldMatrix(), Pose.GetGlobalTransforms(), Pose.GetSkinMatrices())) {
            AddLog("Cannot create the cloth actor. See the Console for details.");

            return false;
        }

        mClothSimTest = std::move(Simulation);
        mClothSimTestComponent = CoreUObject::MakeObjectRef(Component);
        mClothSimTestSubMesh = SubMeshIndex;
        AddLog("Cloth sim on for section " + std::to_string(SubMeshIndex) + ". Use stat cloth for timing.");

        return true;
    }

    /**
     * @brief 천 시뮬레이션 테스트를 끄고 천과 솔버를 해제합니다.
     */
    void Editor::FEditorContext::ClearClothSimTest() {
        mClothSimTest.reset();
        mClothSimTestComponent.Reset();
        mClothSimTestSubMesh = InvalidIndex;
    }

    /**
     * @brief 천 시뮬레이션 테스트가 켜져 있으면 천을 한 프레임 진행합니다.
     */
    void Editor::FEditorContext::TickClothSimTest(float DeltaTime) {
        if (mClothSimTest != nullptr) {
            mClothSimTest->Simulate(DeltaTime);
        }
    }

    /**
     * @brief 천 시뮬레이션 테스트의 입자 사이를 선분으로 그립니다.
     */
    void Editor::FEditorContext::DrawClothSimTest(Engine::FPrimitiveDrawInterface& DrawInterface, float Thickness) const {
        const Engine::USkeletalMeshComponent* Component{mClothSimTestComponent.Get()};

        if (mClothSimTest == nullptr || Component == nullptr || Component->GetSkeletalMesh() == nullptr) {
            return;
        }

        const Asset::USkeletalMesh& Mesh{*Component->GetSkeletalMesh()};
        const Asset::FClothingSectionData* SectionData{Mesh.GetSectionClothingData(mClothSimTestSubMesh)};
        const PhysicsRuntime::UClothingAssetCommon* ClothingAsset{SectionData != nullptr && SectionData->IsValid() ? Mesh.GetClothingAsset(SectionData->mAssetGuid) : nullptr};

        if (ClothingAsset == nullptr || !ClothingAsset->IsValidLod(SectionData->mAssetLodIndex)) {
            return;
        }

        Core::TArray<FVector> Particles{};
        const PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{ClothingAsset->mLodData[SectionData->mAssetLodIndex].mPhysicalMeshData};

        if (!mClothSimTest->GetParticles(0, Particles) || Particles.size() != PhysMesh.mVertices.size()) {
            return;
        }

        // 입자는 기준 뼈 좌표이므로 기준 뼈의 월드 변환으로 월드 위치를 구합니다.
        const Core::TArray<FMatrix>& GlobalTransforms{Component->GetPose().GetGlobalTransforms()};
        const Index_t ReferenceBoneIndex{ClothingAsset->mReferenceBoneIndex};
        const FMatrix WorldMatrix{Component->GetWorldMatrix()};
        const FMatrix SimRootMatrix{ReferenceBoneIndex < GlobalTransforms.size() ? GlobalTransforms[ReferenceBoneIndex] * WorldMatrix : WorldMatrix};
        FTransform SimRootTransform{};

        if (!SimRootMatrix.TryGetTransform(SimRootTransform)) {
            return;
        }

        const FVector FixedColor{0.95f, 0.2f, 0.2f};
        const FVector MovableColor{0.3f, 0.9f, 0.3f};

        for (Index_t TriangleBaseIndex{}; TriangleBaseIndex + 2 < PhysMesh.mIndices.size(); TriangleBaseIndex += 3) {
            for (Index_t Corner{}; Corner < 3; ++Corner) {
                const Uint32 Start{PhysMesh.mIndices[TriangleBaseIndex + Corner]};
                const Uint32 End{PhysMesh.mIndices[TriangleBaseIndex + (Corner + 1) % 3]};
                const bool Fixed{PhysMesh.mInverseMasses[Start] == 0.0f && PhysMesh.mInverseMasses[End] == 0.0f};

                DrawInterface.DrawLine(SimRootTransform.TransformPositionNoScale(Particles[Start]), SimRootTransform.TransformPositionNoScale(Particles[End]), Fixed ? FixedColor : MovableColor, true, Thickness);
            }
        }
    }

    /**
     * @brief 컴포넌트 천 시뮬레이션 결과를 뷰포트에 그릴지 반환합니다.
     */
    bool Editor::FEditorContext::IsClothDebugDrawEnabled() const {
        return mClothDebugDrawEnabled;
    }

    /**
     * @brief 컴포넌트 천 시뮬레이션 결과를 뷰포트에 그릴지 설정합니다.
     */
    void Editor::FEditorContext::SetClothDebugDrawEnabled(bool Enable) {
        mClothDebugDrawEnabled = Enable;
    }

    /**
     * @brief 새 액터 배치 변환 제공자를 저장합니다.
     */
    void Editor::FEditorContext::SetPlacementProvider(std::function<FTransform()> Provider) {
        mPlacementProvider = std::move(Provider);
    }

    /**
     * @brief 아직 살아 있는 선택 액터들의 세대 핸들을 수집합니다.
     */
    Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Editor::FEditorContext::GetSelectedActors() const {
        Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Result{};

        for (CoreUObject::TObjectRef<Engine::AActor> Handle : mSelectedActors) {
            if (CoreUObject::Cast<Engine::AActor>(ResolveObject(Handle)) != nullptr) {
                Result.push_back(Handle);
            }
        }

        return Result;
    }

    /**
     * @brief 선택 컴포넌트 또는 선택 액터들의 루트 변환 대상을 수집합니다.
     */
    Core::TArray<CoreUObject::TObjectRef<Engine::USceneComponent>> Editor::FEditorContext::GetTransformTargets() const {
        if (auto* Component{CoreUObject::Cast<Engine::USceneComponent>(GetSelectedObject())}; Component != nullptr) {
            return {CoreUObject::MakeObjectRef(Component)};
        }

        Core::TArray<CoreUObject::TObjectRef<Engine::USceneComponent>> Result{};

        for (CoreUObject::TObjectRef<Engine::AActor> Handle : GetSelectedActors()) {
            const auto* Actor{CoreUObject::Cast<Engine::AActor>(ResolveObject(Handle))};

            if (Actor->GetRootComponent() != nullptr) {
                Result.push_back(CoreUObject::MakeObjectRef(Actor->GetRootComponent()));
            }
        }

        return Result;
    }

    /**
     * @brief 유효한 액터 핸들들로 현재 다중 선택을 교체합니다.
     */
    bool Editor::FEditorContext::SetSelectedActors(const Core::TArray<CoreUObject::TObjectRef<Engine::AActor>>& Actors) {
        return SetSelection(Actors, Actors.empty() ? CoreUObject::TObjectRef<CoreUObject::UObject>{} : Actors.back());
    }

    /**
     * @brief 지정 액터의 다중 선택 포함 상태를 반전합니다.
     */
    bool Editor::FEditorContext::ToggleActorSelection(CoreUObject::TObjectRef<Engine::AActor> Actor) {
        Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Actors{GetSelectedActors()};
        const auto Position{std::find(Actors.begin(), Actors.end(), Actor)};

        if (Position != Actors.end()) {
            Actors.erase(Position);
        } else {
            Actors.push_back(Actor);
        }

        return SetSelectedActors(Actors);
    }

    /**
     * @brief 선택 대상 변경을 추적할 개정 번호를 반환합니다.
     */
    Uint64 Editor::FEditorContext::GetSelectionRevision() const {
        return mSelectionRevision;
    }

    /**
     * @brief 현재 선택 액터들을 새 GUID로 복제하고 복제본들을 선택합니다.
     */
    Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Editor::FEditorContext::DuplicateSelectedActors() {
        if (!CanModifyScene()) {
            return {};
        }

        FSceneEditScope Edit{*this};
        Core::FString Error{};
        Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Copies{};

        {
            FOperationScope Scope{&mChangingState};

            Copies = Engine::FWorldCloner::DuplicateActors(GetWorld(), GetSelectedActors(), Error);
        }

        if (!Copies.empty()) {
            SetSelectedActors(Copies);

            NoteSceneEdit();
        } else if (!Error.empty()) {
            AddLog(Error);
        }

        return Copies;
    }

    /**
     * @brief 지정한 프리셋의 액터를 생성·배치하고 선택과 장면 변경 상태를 갱신합니다.
     */
    Engine::AActor* Editor::FEditorContext::SpawnActorPreset(const Core::FString& Preset) {
        FTransform Placement{mPlacementProvider ? mPlacementProvider() : FTransform{}};

        if (Preset.find("DirectionalLight") != Core::FString::npos || Preset.find("SpotLight") != Core::FString::npos) {
            Placement.mRotation = FRotator{-45_deg, 0_deg, 0_deg}.Quaternion();
        }

        return SpawnActorPreset(Preset, Placement);
    }

    /**
     * @brief 지정한 프리셋의 액터를 생성·배치하고 선택과 장면 변경 상태를 갱신합니다.
     */
    Engine::AActor* Editor::FEditorContext::SpawnActorPreset(const Core::FString& Preset, const FTransform& Placement) {
        if (!CanModifyScene() || !Placement.IsValid()) {
            return nullptr;
        }

        FSceneEditScope Edit{*this};
        FOperationScope Scope{&mChangingState};
        Engine::UWorld* World{GetWorld()};
        const bool Primitive{Preset == "Cube" || Preset == "Sphere" || Preset == "Plane"};
        const bool Text{Preset == "Text"};
        const bool Billboard{Preset == "Billboard"};
        Engine::AActor* Actor{Primitive ? World->SpawnPrimitive(Preset == "Sphere" ? Engine::EPrimitiveType::Sphere : Preset == "Plane" ? Engine::EPrimitiveType::Plane : Engine::EPrimitiveType::Cube) : World->SpawnActor(Text || Billboard ? Engine::AActor::StaticClass() : CoreUObject::FReflectionRegistry::FindClass(Preset))};

        if (Actor == nullptr) {
            return nullptr;
        }

        if (Actor->GetRootComponent() == nullptr) {
            std::unique_ptr<Engine::UActorComponent> Component{Text ? std::unique_ptr<Engine::UActorComponent>{CoreUObject::NewObject<Engine::UBillboardTextComponent>()} : Billboard ? std::unique_ptr<Engine::UActorComponent>{CoreUObject::NewObject<Engine::UBillboardComponent>()} : std::unique_ptr<Engine::UActorComponent>{CoreUObject::NewObject<Engine::USceneComponent>()}};
            Component->SetName(Text ? "BillboardTextComponent" : Billboard ? "BillboardComponent" : "SceneComponent");

            if (!Actor->AddComponent(std::move(Component))) {
                World->DestroyActor(CoreUObject::MakeObjectRef(Actor));

                return nullptr;
            }
        }

        if (!ApplySpawnPlacement(Actor, Placement)) {
            return nullptr;
        }

        if (auto* Sprite{CoreUObject::Cast<Engine::UBillboardComponent>(Actor->GetRootComponent())}) {
            Sprite->SetTexturePath("/Engine/Editor/Icon/Pawn_64x.png");
        }

        CompleteActorSpawn(Actor, "Placed ");

        return Actor;
    }

}
