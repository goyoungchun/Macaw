#include "FParticleEditorToolkit.h"

#include "Asset/FAssetManager.h"
#include "Core/FilePath.h"
#include "Core/Literals/QuantityTypes.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/FReflectionRegistry.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorToolkitHost.h"
#include "Editor/Widget/FEditorQuantityInputWidget.h"
#include "EditorStyle.h"
#include "Engine/AParticleSystemActor.h"
#include "Engine/AStaticMeshActor.h"
#include "Engine/Component/UParticleSystemComponent.h"
#include "Engine/Component/UStaticMeshComponent.h"
#include "Engine/FProject.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/Particles/Module/UParticleModuleBeamNoise.h"
#include "Engine/Particles/Module/UParticleModuleMeshRotationRate.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleSpawn.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBeam.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataMesh.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataRibbon.h"
#include "Engine/Particles/ParticleAssets.h"
#include "Engine/Particles/UParticleEmitter.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/UParticleSystem.h"
#include "Engine/UWorld.h"
#include "Viewer/FPreviewScene.h"
#include "Viewer/IViewerContent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <imgui_internal.h>
#include <limits>

using namespace Editor;

namespace {

    Core::FString ModuleLabel(const CoreUObject::UClass* Class) {
        if (Class == nullptr) {
            return "Sprite";
        }

        std::string_view Name{Class->GetName()};

        if (Name.starts_with("UParticleModuleTypeData")) {
            Name.remove_prefix(std::string_view{"UParticleModuleTypeData"}.size());
        } else if (Name.starts_with("UParticleModule")) {
            Name.remove_prefix(std::string_view{"UParticleModule"}.size());
        }

        Core::FString Label{};

        for (Index_t Index{}; Index < Name.size(); ++Index) {
            const char Character{Name[Index]};
            const bool Upper{Character >= 'A' && Character <= 'Z'};
            if (Index > 0 && Upper && Name[Index - 1] >= 'a' && Name[Index - 1] <= 'z') {
                Label += ' ';
            }
            Label += Character;
        }

        return Label;
    }

    bool CompatibleModule(const CoreUObject::UClass& Class, const Engine::UParticleModuleTypeDataBase* TypeData) {
        if (Class.IsChildOf(Engine::UParticleModuleBeamNoise::StaticClass())) {
            return TypeData != nullptr && TypeData->IsA<Engine::UParticleModuleTypeDataBeam>();
        }

        if (Class.IsChildOf(Engine::UParticleModuleMeshRotationRate::StaticClass())) {
            return TypeData != nullptr && TypeData->IsA<Engine::UParticleModuleTypeDataMesh>();
        }

        return true;
    }

    bool MatchingModules(const Engine::UParticleEmitter& Emitter) {
        const auto& First{Emitter.GetLODLevel(0)->GetModules()};

        return std::ranges::all_of(Emitter.GetLODLevels(), [&First](const auto& Level) {
            return std::ranges::equal(First, Level->GetModules(), [](const auto& Left, const auto& Right) {
                return Left->GetClass() == Right->GetClass() && Left->RequiredBytes() == Right->RequiredBytes();
            });
        });
    }

}

namespace Editor {

    class FParticleEditorPreview final : public Viewer::IViewerContent {
    public:
        FParticleEditorPreview(RenderCore::IRendererModule* Renderer, Asset::FAssetManager& Assets);

        ~FParticleEditorPreview() override;

    public:
        bool Initialize(PhysicsRuntime::FPhysicsRuntime* Physics, Core::FString& Error);
        bool SetTemplate(Engine::UParticleSystem* Template, Int32 LOD);
        void ResetParticles();
        void SetOrbit(bool Orbit);
        Int32 CountParticles() const;
        bool HasFloor() const;
        bool IsFloorVisible() const;
        void SetFloorVisible(bool Visible);

        bool IsReady() const override;
        const FBox& GetBounds() const override;
        Engine::FSceneInterface* GetScene() const override;
        void Tick(float DeltaTime) override;
        void Synchronize() override;

    private:
        Viewer::FPreviewScene mScene;
        Engine::AParticleSystemActor* mActor{};
        Engine::AStaticMeshActor* mGround{};
        FBox mBounds{{-0.5f, -0.5f, 0.0f}, {0.5f, 0.5f, 1.0f}};
        float mOrbitTime{};
        bool mOrbit{};
    };

}

FParticleEditorPreview::FParticleEditorPreview(RenderCore::IRendererModule* Renderer, Asset::FAssetManager& Assets)
	: mScene{Renderer, &Assets} {
}

FParticleEditorPreview::~FParticleEditorPreview() = default;

bool FParticleEditorPreview::Initialize(PhysicsRuntime::FPhysicsRuntime* Physics, Core::FString& Error) {
    if (!mScene.Initialize(Error)) {
        return false;
    }

    auto& World{*mScene.GetWorld()};
    mActor = CoreUObject::Cast<Engine::AParticleSystemActor>(World.SpawnActor(Engine::AParticleSystemActor::StaticClass()));

    if (mActor == nullptr) {
        Error = "Could not spawn the preview particle system.";
        return false;
    }

    mActor->SetActorLocation({0.0f, 0.0f, 0.35f});
    mActor->GetParticleSystemComponent()->SetEditorTickable(true);

    if (Physics != nullptr) {
        mGround = World.SpawnPrimitive(Engine::EPrimitiveType::Cube);

        if (mGround != nullptr) {
            mGround->SetActorTransform({{0.0f, 0.0f, -0.15f}, {}, {20.0f, 20.0f, 0.3f}});
            mGround->GetStaticMeshComponent()->SetCollisionPreset(PhysicsRuntime::ECollisionPreset::BlockAll);
            World.SetPhysicsRuntime(Physics);
            World.CreatePhysicsScene();
            mGround->SetVisible(false);
        }
    }

    return true;
}

bool FParticleEditorPreview::SetTemplate(Engine::UParticleSystem* Template, Int32 LOD) {
    auto& Component{*mActor->GetParticleSystemComponent()};

    if (!Component.SetPreviewTemplate(Template)) {
        return false;
    }

    Component.SetPreviewLODOverride(LOD);
    mOrbitTime = 0.0f;
    mActor->SetActorLocation({0.0f, 0.0f, 0.35f});

    return true;
}

void FParticleEditorPreview::ResetParticles() {
    mActor->GetParticleSystemComponent()->ResetParticles();
}

void FParticleEditorPreview::SetOrbit(bool Orbit) {
    mOrbit = Orbit;
}

Int32 FParticleEditorPreview::CountParticles() const {
    return mActor->GetParticleSystemComponent()->GetActiveParticleCount();
}

bool FParticleEditorPreview::HasFloor() const {
    return mGround != nullptr;
}

bool FParticleEditorPreview::IsFloorVisible() const {
    return mGround != nullptr && mGround->IsVisible();
}

void FParticleEditorPreview::SetFloorVisible(bool Visible) {
    if (mGround != nullptr) {
        mGround->SetVisible(Visible);
    }
}

bool FParticleEditorPreview::IsReady() const {
    return mActor != nullptr && GetScene() != nullptr;
}

const FBox& FParticleEditorPreview::GetBounds() const {
    return mBounds;
}

Engine::FSceneInterface* FParticleEditorPreview::GetScene() const {
    return mScene.GetScene();
}

void FParticleEditorPreview::Tick(float DeltaTime) {
    if (DeltaTime <= 0.0f) {
        return;
    }

    if (mOrbit) {
        mOrbitTime += DeltaTime;
        mActor->SetActorLocation({std::cos(mOrbitTime * 2.0f) - 1.0f, std::sin(mOrbitTime * 2.0f), 0.65f});
    }

    mScene.GetWorld()->Tick(Core::FTime::FromBase(DeltaTime));
    mBounds = mActor->GetParticleSystemComponent()->GetWorldBounds();
}

void FParticleEditorPreview::Synchronize() {
    mScene.Synchronize();
}

FParticleEditorToolkit::FParticleEditorToolkit(FEditorContext& Context, const std::filesystem::path& Path, FEditorAssetCatalog& Catalog, FAssetThumbnailCache& Thumbnails, FMessageBox& Messages)
	: FAssetEditorToolkit{Path, Asset::EAssetType::ParticleSystem, "###ParticleEditor:" + Core::FilePath::ToGenericUtf8(Path)},
	  mContext{Context},
	  mAssets{*Context.GetProject()->GetAssets()},
	  mCatalog{Catalog},
	  mThumbnails{Thumbnails},
	  mMessages{Messages},
	  mViewer{Context.GetRenderer(), &mAssets} {
    GetTab().SetIcon(EEditorIcon::ParticleSystem, IM_COL32(238, 187, 106, 255));

    auto& Host{GetToolkitHost()};
    Host.SetDockSpaceName("ParticleWorkspace");
    Host.SetLayoutBuilder([this](Uint32 DockSpace) {
        BuildLayout(DockSpace);
    });
    Host.SetToolbar([this]() {
        DrawToolbar();
    });
    Host.AddWidget("Preview", "Particle Preview", [this](float) {
        DrawPreview();
    }, EEditorDockRegion::Center, false);
    Host.GetTabManager().FindTab("Preview")->SetWindowFlags(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Host.AddWidget("Emitters", "Emitters", [this](float) {
        DrawEmitters();
    }, EEditorDockRegion::Left, false);
    Host.GetTabManager().FindTab("Emitters")->SetWindowFlags(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Host.AddWidget("Properties", "Details", [this](float) {
        DrawDetails();
    }, EEditorDockRegion::Details, false);
    Host.AddWidget("Curves", "Curve Editor", [this](float) {
        DrawCurve();
    }, EEditorDockRegion::Bottom, false);
    GetTab().SetCanCloseHandler([this]() {
        return RequestClose();
    });
    mViewer.SetGridSpacing(0.25f);
    UpdateTitle();
}

FParticleEditorToolkit::~FParticleEditorToolkit() = default;

bool FParticleEditorToolkit::Open(Engine::UParticleSystem& Asset, Core::FString& Error) {
    mWorkingCopy = Asset.Duplicate(Error);

    if (mWorkingCopy == nullptr) {
        return false;
    }

    auto Preview{std::make_unique<FParticleEditorPreview>(mContext.GetRenderer(), mAssets)};
    auto* World{mContext.GetWorld()};

    if (!Preview->Initialize(World != nullptr ? World->GetPhysicsRuntime() : nullptr, Error)) {
        return false;
    }

    mPreview = Preview.get();

    if (!mViewer.SetContent(std::move(Preview))) {
        mPreview = nullptr;
        Error = mViewer.GetError();
        return false;
    }

    mOrbit = std::ranges::any_of(mWorkingCopy->GetEmitters(), [](const auto& Emitter) {
        return CoreUObject::Cast<Engine::UParticleModuleTypeDataRibbon>(Emitter->GetLODLevel(0)->GetTypeDataModule()) != nullptr;
    });

    mPreview->SetOrbit(mOrbit);
    RestartPreview();

    if (!mError.empty()) {
        Error = mError;
        return false;
    }

    return true;
}

void FParticleEditorToolkit::CloseRetired(const std::filesystem::path& Absolute) {
    Core::FString Error{};
    const auto OwnPath{Core::FilePath::ToLower(mAssets.ResolvePath(GetAssetPath(), Error))};
    const auto Relative{OwnPath.lexically_relative(Core::FilePath::ToLower(Absolute))};

    if (!Relative.empty() && *Relative.begin() != L"..") {
        GetTab().SetVisible(false);
    }
}

std::optional<FMessageBox::FSavePrompt> FParticleEditorToolkit::GetClosePrompt() {
    if (!mDirty || !GetTab().IsVisible()) {
        return {};
    }

    auto SaveAction{[this]() {
        return Save();
    }};
    auto DiscardAction{[this]() {
        mDirty = false;
        UpdateTitle();
        return true;
    }};

    return FMessageBox::FSavePrompt{"Particle System", "Save changes to " + Core::FilePath::ToUtf8(GetAssetPath().filename()) + "?", std::move(SaveAction), std::move(DiscardAction)};
}

bool FParticleEditorToolkit::RequestClose() {
    auto Prompt{GetClosePrompt()};

    if (!Prompt) {
        return true;
    }

    mMessages.ConfirmSave({std::move(*Prompt)}, [this]() {
        GetTab().SetVisible(false);
    });
    return false;
}

bool FParticleEditorToolkit::Save() {
    if (!mDirty) {
        return true;
    }

    mError.clear();

    if (!mAssets.SaveRegisteredAsset(GetAssetPath(), Asset::EAssetType::ParticleSystem, *mWorkingCopy, mError, true)) {
        mContext.AddLog(mError);
        return false;
    }

    mDirty = false;
    UpdateTitle();
    NotifyChanged();

    return true;
}

bool FParticleEditorToolkit::Reload(Core::FString& Error) {
    auto* Asset{Engine::LoadParticleSystem(mAssets, GetAssetPath(), Error)};
    auto Model{Asset != nullptr ? Asset->Duplicate(Error) : nullptr};

    if (Model == nullptr) {
        return false;
    }

    mPreview->ResetParticles();
    mWorkingCopy = std::move(Model);
    mLOD = 0;
    mEmitter = 0;
    Select(ESelection::System);
    RestartPreview();
    mDirty = false;
    UpdateTitle();

    return true;
}

void FParticleEditorToolkit::UpdateTitle() {
    const Core::FString Id{Core::FilePath::ToGenericUtf8(GetAssetPath())};
    GetTab().SetName(EditorStyle::TabIconPadding + Core::FilePath::ToUtf8(GetAssetPath().stem()) + (mDirty ? " *" : "") + "###ParticleEditor:" + Id);
}

void FParticleEditorToolkit::HandleCommands() {
    if (mDirty && mContext.CanEditWorld() && GetTab().IsFocused() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        Save();
    }
}

void FParticleEditorToolkit::UpdateEditor(float DeltaTime) {
    if (mNeedsRestart) {
        RestartPreview();
    }

    if (mStepRequested || !mPaused) {
        const float SimulationDelta{mStepRequested ? 1.0f / 60.0f : std::clamp(DeltaTime, 0.0f, 0.1f) * mSpeed};

        for (float Remaining{SimulationDelta}; Remaining > 0.0f;) {
            const float Step{std::min(Remaining, 1.0f / 30.0f)};

            mViewer.Tick(Step);
            Remaining -= Step;
        }

        if (mNeedsFit) {
            mFitDelay += SimulationDelta;
        }
    }

    mStepRequested = false;
}

void FParticleEditorToolkit::RestartPreview() {
    mPreview->ResetParticles();
    mWorkingCopy->BuildEmitters();
    mLOD = std::min(mLOD, mWorkingCopy->GetNumLODs() - 1);

    if (!mPreview->SetTemplate(mWorkingCopy.get(), static_cast<Int32>(mLOD))) {
        mError = "Could not start the particle preview.";
    }

    mNeedsRestart = false;
}

void FParticleEditorToolkit::MarkDirty() {
    mNeedsRestart = true;
    mError.clear();

    if (!mDirty) {
        mDirty = true;
        UpdateTitle();
    }
}

void FParticleEditorToolkit::ResetCurveSelection() {
    mCurveProperty = nullptr;
    mCurveAxis = 0;
    mCurveEditor.Reset();
}

void FParticleEditorToolkit::Select(ESelection Selection, Index_t Module) {
    mSelection = Selection;
    mModule = Module;
    ResetCurveSelection();
}

void FParticleEditorToolkit::SelectEmitter(Index_t Emitter, ESelection Selection, Index_t Module) {
    if (mEmitter != Emitter || mSelection != Selection || mModule != Module) {
        mEmitter = Emitter;
        Select(Selection, Module);
    }
}

Engine::UParticleEmitter* FParticleEditorToolkit::GetEmitter() const {
    return mWorkingCopy->GetEmitter(mEmitter);
}

Engine::UParticleLODLevel* FParticleEditorToolkit::GetLevel() const {
    auto* Emitter{GetEmitter()};
    return Emitter != nullptr ? GetLevel(*Emitter) : nullptr;
}

Engine::UParticleLODLevel* FParticleEditorToolkit::GetLevel(Engine::UParticleEmitter& Emitter) const {
    return Emitter.GetNumLODLevels() > 0 ? Emitter.GetLODLevel(std::min(mLOD, Emitter.GetNumLODLevels() - 1)) : nullptr;
}

CoreUObject::UObject* FParticleEditorToolkit::GetSelectedObject() const {
    if (mSelection == ESelection::System) {
        return mWorkingCopy.get();
    }

    if (mSelection == ESelection::Emitter) {
        return GetEmitter();
    }

    auto* Level{GetLevel()};

    if (Level == nullptr) {
        return nullptr;
    }

    switch (mSelection) {
    case ESelection::Required:
        return Level->GetRequiredModule();
    case ESelection::Spawn:
        return Level->GetSpawnModule();
    case ESelection::TypeData:
        return Level->GetTypeDataModule();
    case ESelection::Module:
        return mModule < Level->GetModules().size() ? Level->GetModules()[mModule].get() : nullptr;
    default:
        return nullptr;
    }
}

void FParticleEditorToolkit::BuildLayout(Uint32 DockSpace) {
    ImGuiID Right{};
    const ImGuiID Left{ImGui::DockBuilderSplitNode(DockSpace, ImGuiDir_Left, 0.40f, nullptr, &Right)};
    ImGuiID Preview{};
    ImGuiID Emitters{};
    const ImGuiID Details{ImGui::DockBuilderSplitNode(Left, ImGuiDir_Down, 0.44f, nullptr, &Preview)};
    const ImGuiID Curves{ImGui::DockBuilderSplitNode(Right, ImGuiDir_Down, 0.46f, nullptr, &Emitters)};
    GetToolkitHost().GetTabManager().ApplyLayout(Emitters, Emitters, Details, Curves, Preview);
}

void FParticleEditorToolkit::DrawToolbar() {
    EditorStyle::PushViewportToolbar();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y);

    ImGui::BeginDisabled(!mContext.CanEditWorld() || !mDirty);
    constexpr EditorStyle::EIcon SaveIcon{EditorStyle::EIcon::Save};
    if (EditorStyle::LabelButton("Save", &SaveIcon)) {
        Save();
    }
    ImGui::SameLine();
    if (EditorStyle::LabelButton("Revert")) {
        ImGui::OpenPopup("Revert Particle System");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Revert Particle System", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Discard unsaved edits and reload the saved particle system?");
        if (ImGui::Button("Revert Edits")) {
            mError.clear();
            Reload(mError);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    constexpr EditorStyle::EIcon RestartIcon{EditorStyle::EIcon::Rotate};
    if (EditorStyle::LabelButton("Restart Preview", &RestartIcon)) {
        mNeedsRestart = true;
    }
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 19.0f) {
        ImGui::NewLine();
    }
    ImGui::BeginDisabled(!mContext.CanEditWorld());
    DrawLODControls();
    ImGui::EndDisabled();
    EditorStyle::PopViewportToolbar();

    if (!mError.empty()) {
        ImGui::TextWrapped("%s", mError.c_str());
    }
}

void FParticleEditorToolkit::DrawPreviewControls() {
    EditorStyle::PushViewportToolbar();
    constexpr EditorStyle::EIcon ViewIcon{EditorStyle::EIcon::Camera};
    if (EditorStyle::LabelButton("View", &ViewIcon, true)) {
        ImGui::OpenPopup("Preview View");
    }

    if (EditorStyle::BeginPopup("Preview View")) {
        if (ImGui::MenuItem("Fit Particles")) {
            mNeedsFit = true;
            mFitDelay = 0.25f;
        }
        if (ImGui::MenuItem("Grid", nullptr, &mShowGrid)) {
            mViewer.SetGridSpacing(mShowGrid ? 0.25f : 0.0f);
        }
        ImGui::MenuItem("Origin Axes", nullptr, &mShowOrigin);
        ImGui::MenuItem("Bounds", nullptr, &mShowBounds);
        const bool FloorVisible{mPreview->IsFloorVisible()};
        if (ImGui::MenuItem("Floor", nullptr, FloorVisible, mPreview->HasFloor())) {
            mPreview->SetFloorVisible(!FloorVisible);
        }
        EditorStyle::EndPopup();
    }

    ImGui::SameLine();
    if (EditorStyle::IconButton(mPaused ? "Play Preview" : "Pause Preview", mPaused ? EditorStyle::EIcon::Play : EditorStyle::EIcon::Pause)) {
        mPaused = !mPaused;
    }
    ImGui::SameLine();
    if (EditorStyle::IconButton("Step Preview", EditorStyle::EIcon::Step, false, mPaused)) {
        mStepRequested = true;
    }
    ImGui::SameLine();
    EditorStyle::DrawFieldLabel("Speed", ImGui::CalcTextSize("Speed").x);
    const Core::FString Speed{std::format("{:.2f}x###PlaybackSpeed", mSpeed)};
    if (EditorStyle::LabelButton(Speed.c_str(), nullptr, true, true, "Preview Playback")) {
        ImGui::OpenPopup("Preview Playback");
    }

    if (EditorStyle::BeginPopup("Preview Playback")) {
        const float LabelWidth{ImGui::CalcTextSize("Move Emitter").x};
        EditorStyle::DrawFieldLabel("Speed", LabelWidth, ImGui::GetFontSize() * 8.0f);
        ImGui::SliderFloat("##Speed", &mSpeed, 0.05f, 4.0f, "%.2fx");
        EditorStyle::DrawFieldLabel("Move Emitter", LabelWidth);
        if (ImGui::Checkbox("##MoveEmitter", &mOrbit)) {
            mPreview->SetOrbit(mOrbit);
            mNeedsRestart = true;
        }
        EditorStyle::EndPopup();
    }
    EditorStyle::PopViewportToolbar();
}

void FParticleEditorToolkit::DrawPreview() {
    DrawPreviewControls();

    const ImVec2 Size{ImGui::GetContentRegionAvail()};
    if (Size.x < 1.0f || Size.y < 1.0f || !mViewer.Resize(static_cast<Uint32>(Size.x), static_cast<Uint32>(Size.y))) {
        return;
    }

    if (mNeedsFit && (mFitDelay >= 0.25f || mPaused)) {
        mNeedsFit = !mViewer.Fit(1.3f);
    }

    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    ImGui::InvisibleButton("Particle View", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    auto& Camera{mViewer.GetCamera()};
    const auto& IO{ImGui::GetIO()};

    if (ImGui::IsItemHovered() && IO.MouseWheel != 0.0f) {
        Camera.Zoom(IO.MouseWheel);
    }

    if (ImGui::IsItemActive()) {
        const FVector2 Delta{IO.MouseDelta.x, IO.MouseDelta.y};
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            Camera.Pan(Delta, Size.y);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            Camera.Orbit(Delta);
        }
    }

    Engine::FPrimitiveDrawInterface Overlay{};
    if (mShowOrigin) {
        Overlay.DrawLine({}, {0.5f, 0.0f, 0.0f}, {0.9f, 0.28f, 0.28f}, false, 2.0f);
        Overlay.DrawLine({}, {0.0f, 0.5f, 0.0f}, {0.30f, 0.78f, 0.42f}, false, 2.0f);
        Overlay.DrawLine({}, {0.0f, 0.0f, 0.5f}, {0.35f, 0.59f, 0.94f}, false, 2.0f);
    }
    if (mShowBounds) {
        Overlay.DrawBox(mPreview->GetBounds(), {0.24f, 0.83f, 0.76f}, false);
    }

    if (mViewer.Render(mShowBounds || mShowOrigin ? &Overlay : nullptr)) {
        const ImTextureRef Texture{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mViewer.GetNativeTexture()))};
        auto* Draw{ImGui::GetWindowDrawList()};
        const ImVec2 End{Origin.x + Size.x, Origin.y + Size.y};
        Draw->AddImage(Texture, Origin, End);

        const Core::FString Status{std::format("LOD {}   /   {} particles", mLOD, mPreview->CountParticles())};
        const ImVec2 TextSize{ImGui::CalcTextSize(Status.c_str())};
        const ImVec2 TextAt{Origin.x + 12.0f, End.y - TextSize.y - 8.0f};
        Draw->PushClipRect(Origin, End, true);
        Draw->AddRectFilled({TextAt.x - 6.0f, TextAt.y - 3.0f}, {TextAt.x + TextSize.x + 6.0f, TextAt.y + TextSize.y + 3.0f}, IM_COL32(13, 18, 24, 225), 4.0f);
        Draw->AddText(TextAt, IM_COL32(225, 234, 243, 255), Status.c_str());
        Draw->PopClipRect();
    }
}

void FParticleEditorToolkit::DrawLODControls() {
    if (EditorStyle::IconButton("Higher Detail LOD", EditorStyle::EIcon::JumpStart, false, mLOD > 0)) {
        ChangeLOD(mLOD - 1);
    }
    ImGui::SameLine();
    EditorStyle::DrawFieldLabel("LOD", ImGui::CalcTextSize("LOD").x, ImGui::GetFontSize() * 3.5f);
    const Core::FString Current{std::to_string(mLOD)};

    if (ImGui::BeginCombo("##LOD", Current.c_str())) {
        for (Index_t Index{}; Index < mWorkingCopy->GetNumLODs(); ++Index) {
            const Core::FString Label{"LOD " + std::to_string(Index)};

            if (ImGui::Selectable(Label.c_str(), Index == mLOD)) {
                ChangeLOD(Index);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (EditorStyle::IconButton("Lower Detail LOD", EditorStyle::EIcon::JumpEnd, false, mLOD + 1 < mWorkingCopy->GetNumLODs())) {
        ChangeLOD(mLOD + 1);
    }
    ImGui::SameLine();
    constexpr EditorStyle::EIcon AddIcon{EditorStyle::EIcon::Add};
    if (EditorStyle::LabelButton("Add LOD", &AddIcon, false, true, "Duplicate the current LOD")) {
        mPreview->ResetParticles();
        if (mWorkingCopy->InsertLOD(mLOD + 1)) {
            ChangeLOD(mLOD + 1);
            MarkDirty();
        }
    }

    ImGui::SameLine();
    if (EditorStyle::LabelButton("Remove LOD", nullptr, false, mLOD > 0)) {
        mPreview->ResetParticles();
        if (mWorkingCopy->RemoveLOD(mLOD)) {
            ChangeLOD(mLOD - 1);
            MarkDirty();
        }
    }
}

void FParticleEditorToolkit::DrawLODDistance() {
    float Distance{mWorkingCopy->GetLODDistance(mLOD).GetBase()};
    ImGui::BeginDisabled(mLOD == 0);
    if (FEditorQuantityInputWidget::EditValue("##LOD Distance", Distance, Core::QuantityUnits::Meter)) {
        const float Minimum{std::nextafter(mWorkingCopy->GetLODDistance(mLOD - 1).GetBase(), std::numeric_limits<float>::infinity())};
        const float Maximum{mLOD + 1 < mWorkingCopy->GetNumLODs() ? std::nextafter(mWorkingCopy->GetLODDistance(mLOD + 1).GetBase(), 0.0f) : std::numeric_limits<float>::max()};

        if (Minimum <= Maximum) {
            mWorkingCopy->SetLODDistance(mLOD, Core::FLength::FromBase(std::clamp(Distance, Minimum, Maximum)));
            MarkDirty();
        }
    }
    ImGui::EndDisabled();
}

void FParticleEditorToolkit::DrawEmitters() {
    ImGui::BeginDisabled(!mContext.CanEditWorld());
    constexpr EditorStyle::EIcon AddIcon{EditorStyle::EIcon::Add};
    if (EditorStyle::LabelButton("Add Emitter", &AddIcon)) {
        AddEmitter();
    }
    ImGui::SameLine();
    if (EditorStyle::Button("System Settings", {}, mSelection == ESelection::System)) {
        Select(ESelection::System);
    }

    std::function<void()> Command{};
    const bool HasEmitters{mWorkingCopy->GetNumEmitters() > 0};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(22, 27, 35, 255));
    if (ImGui::BeginChild("Emitter Board", {}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (!HasEmitters) {
            ImGui::TextDisabled("Add an emitter to build this particle system.");
        }

        const float LaneWidth{ImGui::GetFontSize() * 16.0f};
        const float LaneHeight{std::max(1.0f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize)};

        for (Index_t Index{}; Index < mWorkingCopy->GetNumEmitters(); ++Index) {
            if (Index > 0) {
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(Index));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(33, 41, 52, 255));
            if (ImGui::BeginChild("Emitter Lane", {LaneWidth, LaneHeight}, ImGuiChildFlags_Borders)) {
                DrawEmitter(Index, *mWorkingCopy->GetEmitter(Index), Command);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (mScrollToEmitter && Index == mEmitter) {
                ImGui::SetScrollHereX(1.0f);
                mScrollToEmitter = false;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    if (Command) {
        Command();
    }
}

void FParticleEditorToolkit::DrawEmitter(Index_t Index, Engine::UParticleEmitter& Emitter, std::function<void()>& Command) {
    auto* Level{GetLevel(Emitter)};
    const auto* TypeData{Level != nullptr ? Level->GetTypeDataModule() : nullptr};
    const auto* CurrentType{TypeData != nullptr ? TypeData->GetClass() : nullptr};
    const Core::FString Name{Emitter.GetName().empty() ? "Emitter " + std::to_string(Index + 1) : Emitter.GetName()};
    const bool Selected{mSelection != ESelection::System && mEmitter == Index};
    const float FontSize{ImGui::GetFontSize()};
    const float HeadingWidth{std::max(1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 2.05f)};
    const ImVec2 Heading{ImGui::GetCursorScreenPos()};
    const bool Pressed{EditorStyle::Selectable("##Emitter", Selected, 0, {HeadingWidth, FontSize * 2.5f})};
    if (Pressed) {
        SelectEmitter(Index, ESelection::Emitter);
    }
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(Name.c_str()).x > HeadingWidth - 26.0f) {
        ImGui::SetTooltip("%s", Name.c_str());
    }

    auto* Draw{ImGui::GetWindowDrawList()};
    const ImU32 Accent{IM_COL32(81, 202, 188, 255)};
    Draw->PushClipRect(Heading, {Heading.x + HeadingWidth, Heading.y + FontSize * 2.5f}, true);
    EditorStyle::DrawIcon(EditorStyle::EIcon::Sprite, {Heading.x + 2.0f, Heading.y + FontSize * 0.65f}, FontSize, Accent);
    Draw->AddText({Heading.x + FontSize + 9.0f, Heading.y + 2.0f}, ImGui::GetColorU32(ImGuiCol_Text), Name.c_str());
    Draw->AddText({Heading.x + FontSize + 9.0f, Heading.y + FontSize + 5.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), ModuleLabel(CurrentType).c_str());
    Draw->PopClipRect();

    ImGui::SameLine();
    const bool Enabled{Level != nullptr && Level->IsEnabled()};
    if (EditorStyle::IconButton("Emitter Enabled", Enabled ? EditorStyle::EIcon::Visible : EditorStyle::EIcon::Hidden, false, Level != nullptr, false, Enabled ? "Disable this emitter at the current LOD" : "Enable this emitter at the current LOD")) {
        Level->SetEnabled(!Enabled);
        MarkDirty();
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (EditorStyle::IconButton("Emitter Actions", EditorStyle::EIcon::More)) {
        ImGui::OpenPopup("Emitter Actions");
    }

    if (EditorStyle::BeginPopup("Emitter Actions")) {
        if (Level != nullptr && ImGui::BeginMenu("Emitter Type")) {
            const std::array<const CoreUObject::UClass*, 4> Types{nullptr, Engine::UParticleModuleTypeDataMesh::StaticClass(), Engine::UParticleModuleTypeDataBeam::StaticClass(), Engine::UParticleModuleTypeDataRibbon::StaticClass()};
            for (const auto* Type : Types) {
                if (ImGui::MenuItem(ModuleLabel(Type).c_str(), nullptr, Type == CurrentType, Type != CurrentType)) {
                    Command = [this, Index, Type]() {
                        SelectEmitter(Index, ESelection::Emitter);
                        SetTypeData(Type);
                    };
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Move Left", nullptr, false, Index > 0)) {
            Command = [this, Index]() {
                SelectEmitter(Index, ESelection::Emitter);
                MoveEmitter(Index - 1);
            };
        }
        if (ImGui::MenuItem("Move Right", nullptr, false, Index + 1 < mWorkingCopy->GetNumEmitters())) {
            Command = [this, Index]() {
                SelectEmitter(Index, ESelection::Emitter);
                MoveEmitter(Index + 1);
            };
        }
        if (ImGui::MenuItem("Remove Emitter")) {
            Command = [this, Index]() {
                SelectEmitter(Index, ESelection::Emitter);
                RemoveEmitter();
            };
        }
        EditorStyle::EndPopup();
    }

    ImGui::Separator();
    if (Level == nullptr) {
        return;
    }

    DrawModuleRow(Index, ESelection::Required, 0, Level->GetRequiredModule(), "Required", IM_COL32(223, 189, 105, 255), false);
    DrawModuleRow(Index, ESelection::Spawn, 0, Level->GetSpawnModule(), "Spawn", IM_COL32(89, 196, 180, 255), false);
    if (TypeData != nullptr) {
        DrawModuleRow(Index, ESelection::TypeData, 0, Level->GetTypeDataModule(), ModuleLabel(CurrentType).c_str(), IM_COL32(139, 171, 237, 255), false);
    }

    const auto& Modules{Level->GetModules()};
    for (Index_t Slot{}; Slot < Modules.size(); ++Slot) {
        auto* Module{Modules[Slot].get()};
        const bool Compatible{CompatibleModule(*Module->GetClass(), TypeData)};
        const Core::FString Label{ModuleLabel(Module->GetClass()) + (Compatible ? "" : " (inactive)")};
        DrawModuleRow(Index, ESelection::Module, Slot, Module, Label.c_str(), IM_COL32(102, 121, 148, 255), true);
    }

    DrawModuleMenu(Index, *Level);
}

void FParticleEditorToolkit::DrawModuleRow(Index_t Emitter, ESelection Selection, Index_t Index, Engine::UParticleModule* Module, const char* Label, Uint32 Accent, bool ToggleEnabled) {
    ImGui::PushID(static_cast<int>(Selection));
    ImGui::PushID(static_cast<int>(Index));
    const bool Selected{mEmitter == Emitter && mSelection == Selection && mModule == Index};
    const bool HasCurve{Module != nullptr && HasCurveTrack(*Module)};
    const float CheckboxWidth{ToggleEnabled ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f};
    const float CurveWidth{HasCurve ? ImGui::CalcTextSize("Curve").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x : 0.0f};
    const float Width{std::max(1.0f, ImGui::GetContentRegionAvail().x - CheckboxWidth - CurveWidth)};
    const float Height{ImGui::GetFrameHeight()};
    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    auto* Draw{ImGui::GetWindowDrawList()};
    Draw->AddRectFilled(Origin, {Origin.x + Width, Origin.y + Height}, IM_COL32(42, 52, 66, 255), 3.0f);
    if (EditorStyle::Selectable("##Module", Selected, 0, {Width, Height})) {
        SelectEmitter(Emitter, Selection, Index);
    }
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(Label).x > Width - 11.0f) {
        ImGui::SetTooltip("%s", Label);
    }

    Draw->PushClipRect(Origin, {Origin.x + Width, Origin.y + Height}, true);
    Draw->AddRectFilled({Origin.x, Origin.y + 2.0f}, {Origin.x + 3.0f, Origin.y + Height - 2.0f}, Accent, 1.0f);
    Draw->AddText({Origin.x + 8.0f, Origin.y + (Height - ImGui::GetFontSize()) * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), Label);
    Draw->PopClipRect();

    if (ToggleEnabled) {
        ImGui::SameLine();
        bool Enabled{Module->IsEnabled()};
        if (ImGui::Checkbox("##Enabled", &Enabled)) {
            Module->SetEnabled(Enabled);
            MarkDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable or disable %s at this LOD", Label);
        }
    }

    if (HasCurve) {
        ImGui::SameLine();
        if (EditorStyle::Button("Curve", {}, Selected && mCurveProperty != nullptr)) {
            SelectEmitter(Emitter, Selection, Index);
            GetToolkitHost().GetTabManager().TryInvokeTab("Curves");
        }
    }

    ImGui::PopID();
    ImGui::PopID();
}

void FParticleEditorToolkit::DrawModuleMenu(Index_t Emitter, const Engine::UParticleLODLevel& Level) {
    const bool Matching{MatchingModules(*mWorkingCopy->GetEmitter(Emitter))};
    constexpr EditorStyle::EIcon AddIcon{EditorStyle::EIcon::Add};
    if (EditorStyle::LabelButton("Module", &AddIcon, true, Matching, Matching ? "Add a module" : "Module layout differs between LODs")) {
        ImGui::OpenPopup("Particle Modules");
    }

    const bool ModuleSelected{mEmitter == Emitter && mSelection == ESelection::Module && mModule < Level.GetModules().size()};
    if (EditorStyle::BeginPopup("Particle Modules")) {
        if (ImGui::BeginMenu("Add Module", Matching)) {
            for (const auto* Class : CoreUObject::FReflectionRegistry::GetDerivedClasses(Engine::UParticleModule::StaticClass())) {
                if (!Class->CanCreate() || Class == Engine::UParticleModuleRequired::StaticClass() || Class == Engine::UParticleModuleSpawn::StaticClass() || Class->IsChildOf(Engine::UParticleModuleTypeDataBase::StaticClass()) || !CompatibleModule(*Class, Level.GetTypeDataModule())) {
                    continue;
                }
                if (ImGui::MenuItem(ModuleLabel(Class).c_str())) {
                    SelectEmitter(Emitter, ESelection::Emitter);
                    AddModule(*Class);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Move Module Up", nullptr, false, Matching && ModuleSelected && mModule > 0)) {
            MoveModule(mModule - 1);
        }
        if (ImGui::MenuItem("Move Module Down", nullptr, false, Matching && ModuleSelected && mModule + 1 < Level.GetModules().size())) {
            MoveModule(mModule + 1);
        }
        if (ImGui::MenuItem("Remove Module", nullptr, false, Matching && ModuleSelected)) {
            RemoveModule();
        }
        EditorStyle::EndPopup();
    }
}

void FParticleEditorToolkit::AddEmitter() {
    auto Emitter{Engine::UParticleEmitter::CreateDefault()};
    Emitter->SetName("Emitter " + std::to_string(mWorkingCopy->GetNumEmitters() + 1));

    for (Index_t Index{1}; Index < mWorkingCopy->GetNumLODs(); ++Index) {
        if (Emitter->DuplicateLODLevel(0, Index) == nullptr) {
            mError = "Could not copy the emitter to every LOD.";
            return;
        }
    }

    mEmitter = mWorkingCopy->GetNumEmitters();
    mWorkingCopy->AddEmitter(std::move(Emitter));
    mScrollToEmitter = true;
    Select(ESelection::Emitter);
    MarkDirty();
}

void FParticleEditorToolkit::RemoveEmitter() {
    mPreview->ResetParticles();

    if (mWorkingCopy->RemoveEmitter(GetEmitter()) != nullptr) {
        mEmitter = std::min(mEmitter, mWorkingCopy->GetNumEmitters() > 0 ? mWorkingCopy->GetNumEmitters() - 1 : Index_t{});
        Select(GetEmitter() != nullptr ? ESelection::Emitter : ESelection::System);
        MarkDirty();
    }
}

void FParticleEditorToolkit::MoveEmitter(Index_t Target) {
    if (mWorkingCopy->MoveEmitter(mEmitter, Target)) {
        mEmitter = Target;
        mScrollToEmitter = true;
        MarkDirty();
    }
}

void FParticleEditorToolkit::SetTypeData(const CoreUObject::UClass* Class) {
    const auto& Levels{GetEmitter()->GetLODLevels()};
    Core::TArray<std::unique_ptr<Engine::UParticleModuleTypeDataBase>> Modules{};
    Modules.reserve(Levels.size());

    for (const auto& Level : Levels) {
        const auto* Current{Level->GetTypeDataModule()};
        auto Object{Class != nullptr && (Current == nullptr || Current->GetClass() != Class) ? Class->CreateInstance() : nullptr};

        if (Class != nullptr && (Current == nullptr || Current->GetClass() != Class) && Object == nullptr) {
            mError = "Could not create the selected emitter type.";
            return;
        }

        Modules.emplace_back(static_cast<Engine::UParticleModuleTypeDataBase*>(Object.release()));
    }

    mPreview->ResetParticles();

    for (Index_t Index{}; Index < Levels.size(); ++Index) {
        if (Class == nullptr || Modules[Index] != nullptr) {
            Levels[Index]->SetTypeDataModule(std::move(Modules[Index]));
        }
    }

    Select(Class != nullptr ? ESelection::TypeData : ESelection::Emitter);
    MarkDirty();
}

void FParticleEditorToolkit::AddModule(const CoreUObject::UClass& Class) {
    const auto& Levels{GetEmitter()->GetLODLevels()};
    Core::TArray<std::unique_ptr<Engine::UParticleModule>> Modules{};
    Modules.reserve(Levels.size());

    for (Index_t Index{}; Index < Levels.size(); ++Index) {
        auto Object{Class.CreateInstance()};
        if (Object == nullptr) {
            mError = "Could not create the selected module.";
            return;
        }
        Modules.emplace_back(static_cast<Engine::UParticleModule*>(Object.release()));
    }

    mPreview->ResetParticles();

    const Index_t Slot{GetLevel()->GetModules().size()};
    for (Index_t Index{}; Index < Levels.size(); ++Index) {
        Levels[Index]->AddModule(std::move(Modules[Index]));
    }

    Select(ESelection::Module, Slot);
    MarkDirty();
}

void FParticleEditorToolkit::RemoveModule() {
    mPreview->ResetParticles();

    for (const auto& Level : GetEmitter()->GetLODLevels()) {
        Level->RemoveModule(Level->GetModules()[mModule].get());
    }

    const Index_t Count{GetLevel()->GetModules().size()};
    Select(Count > 0 ? ESelection::Module : ESelection::Emitter, Count > 0 ? std::min(mModule, Count - 1) : Index_t{});
    MarkDirty();
}

void FParticleEditorToolkit::MoveModule(Index_t Target) {
    mPreview->ResetParticles();

    for (const auto& Level : GetEmitter()->GetLODLevels()) {
        Level->MoveModule(mModule, Target);
    }

    mModule = Target;
    MarkDirty();
}

void FParticleEditorToolkit::ChangeLOD(Index_t Target) {
    mLOD = Target;
    mNeedsRestart = true;
    ResetCurveSelection();
}
