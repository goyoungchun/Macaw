/**
 * @file FAnimSequenceEditor.cpp
 * @brief 애니메이션 시퀀스의 복사본을 편집하고 독립 뷰어에서 재생하는 에디터입니다.
 * @ingroup Editor
 */

#include "FAnimSequenceEditor.h"
#include "Editor/FEditorToolkitHost.h"

#include "Viewer/FAnimationViewerContent.h"
#include "FAnimSequenceViewportClient.h"
#include "Asset/Animation/UAnimSequence.h"
#include "Asset/FAssetManager.h"
#include "Asset/USkeleton.h"
#include "Core/FilePath.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Editor/FAssetThumbnailCache.h"
#include "Editor/FEditorAssetCatalog.h"
#include "Engine/Component/USkeletalMeshComponent.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include "EditorStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

using namespace Editor;

/**
 * @brief 애니메이션 시퀀스의 복사본을 편집하고 독립 뷰어에서 재생하는 에디터의 파일 내부 보조 구현입니다.
 */
namespace {

    /**
     * @brief 표시 이름과 무관하게 창 상태를 유지하는 ImGui 창 ID입니다.
     */

    /**
     * @brief 시퀀스의 루트 모션 추출·고정 설정을 복사합니다.
     * @param Source 복사하거나 읽을 원본 데이터입니다.
     * @param Target 복사하거나 그릴 렌더 타깃입니다.
     */
    void CopySettings(const Asset::UAnimSequence& Source, Asset::UAnimSequence& Target) {
        Target.SetRootMotionEnabled(Source.IsRootMotionEnabled());
        Target.SetForceRootLock(Source.IsRootLockForced());
        Target.SetRootMotionRootLock(Source.GetRootMotionRootLock());
        Target.SetRootMotionBone(Source.GetRootMotionBone());
    }

    /**
     * @brief 시퀀스 파일 이름을 주어진 목록 너비에 맞는 표시 문자열로 만듭니다.
     * @param Path 처리할 에셋 또는 파일 경로입니다.
     * @param Width 출력 또는 UI 영역의 너비(픽셀)입니다.
     * @return 목록에 표시할 애니메이션 이름입니다.
     */
    Core::FString GetAnimationLabel(const std::filesystem::path& Path, float Width) {
        const Core::FString Name{Core::FilePath::ToUtf8(Path.stem())};

        if (ImGui::CalcTextSize(Name.c_str()).x <= Width) {
            return Name;
        }

        const float EllipsisWidth{ImGui::CalcTextSize("...").x};
        const char* Tail{Name.c_str()};

        while (*Tail != '\0' && ImGui::CalcTextSize(Tail).x + EllipsisWidth > Width) {
            ++Tail;

            while ((static_cast<Uint8>(*Tail) & 0xc0) == 0x80) {
                ++Tail;
            }
        }

        return Core::FString{"..."} + Tail;
    }
}

FAnimSequenceEditor::FAnimSequenceEditor(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets, Editor::FEditorAssetCatalog* Catalog, Editor::FAssetThumbnailCache* Thumbnails, FMessageBox& Messages)
	: mRenderer{Renderer},
	  mAssets{Assets},
	  mCatalog{Catalog},
	  mThumbnails{Thumbnails},
	  mMessages{Messages} {
}

FAnimSequenceEditor::~FAnimSequenceEditor() = default;

bool FAnimSequenceEditor::Open(const std::filesystem::path& Path, Core::FString& Error) {
    Error.clear();

    if (mAssets == nullptr) {
        Error = "The animation viewer requires an asset manager.";

        return false;
    }

    const std::filesystem::path Relative{Core::FilePath::FromUtf8(mAssets->GetRelativePath(Path, Error))};

    if (Relative.empty()) {
        return false;
    }

    if (mOpen && Relative == mPath) {
        ImGui::SetWindowFocus(mWindowId.c_str());

        return true;
    }

    if (mOpen && mDirty) {
        mMessages.ConfirmSave({MakeSavePrompt()}, [this, Relative]() {
            OpenNow(Relative, mError);
        });

        return true;
    }

    return OpenNow(Relative, Error);
}

bool FAnimSequenceEditor::OpenNow(const std::filesystem::path& Path, Core::FString& Error) {
    Asset::UAnimSequence* Source{mAssets->LoadAnimSequence(Path, Error)};

    if (Source == nullptr) {
        return false;
    }

    auto Editing{CoreUObject::NewObject<Asset::UAnimSequence>()};

    if (Editing == nullptr || !Editing->SetDataModel(Source->GetDataModel(), Error)) {
        return false;
    }

    Editing->SetName(Source->GetName());
    CopySettings(*Source, *Editing);
    const bool Playing{mPreview != nullptr && mPreview->IsPlaying()};

    if (mPreview == nullptr) {
        auto Preview{std::make_unique<Viewer::FAnimationViewerContent>(mRenderer, mAssets)};
        mPreview = Preview.get();
        mViewport = std::make_unique<Editor::FAnimSequenceViewportClient>(mRenderer, std::move(Preview));
    }

    mPreview->SetAnimation(Editing.get());
    mEditing = std::move(Editing);
    mTimeline.Reset(*mEditing);
    mSource = Source;
    mPath = Path;
    mDirty = false;
    mSourceChanged = false;
    mError.clear();
    mOpen = true;
    strncpy_s(mRootBone.data(), mRootBone.size(), mEditing->GetRootMotionBone().c_str(), _TRUNCATE);

    Core::FString SavedMesh{};
    Core::FString SavedSkeleton{};

    if (mThumbnails != nullptr && mThumbnails->GetAnimationPreview(mPath, SavedMesh, SavedSkeleton)) {
        SetPreviewMesh(SavedMesh, SavedSkeleton);
    } else if (!mMeshPath.empty()) {
        SetPreviewMesh(mMeshPath, mSkeletonPath);
    }

    if (Playing) {
        mPreview->Play();
    }

    ImGui::SetWindowFocus(mWindowId.c_str());

    return true;
}

bool FAnimSequenceEditor::SetPreviewMesh(const Core::FString& MeshPath, const Core::FString& SkeletonPath) {
    mMeshPath = MeshPath;
    mSkeletonPath = SkeletonPath;

    if (mPreview == nullptr) {
        return false;
    }

    Asset::USkeletalMesh* Mesh{mMeshPath.empty() ? nullptr : mAssets->LoadSkeletalMesh(Core::FilePath::FromUtf8(mMeshPath), mError)};
    Asset::USkeleton* Skeleton{mSkeletonPath.empty() ? mMeshPath.empty() ? nullptr : mAssets->FindLegacySkeleton(Core::FilePath::FromUtf8(mMeshPath)) : mAssets->LoadSkeleton(Core::FilePath::FromUtf8(mSkeletonPath), mError)};

    if ((!mMeshPath.empty() && Mesh == nullptr) || !mPreview->SetMesh(Mesh, Skeleton, mError)) {
        return false;
    }

    mViewport->FrameMesh();

    if (mThumbnails != nullptr && !mPath.empty()) {
        mThumbnails->SetAnimationPreview(mPath, mMeshPath, mSkeletonPath, mError);
    }

    return true;
}

bool FAnimSequenceEditor::Save() {
    mError.clear();
    Asset::UAnimSequence* Source{mEditing != nullptr ? mAssets->LoadAnimSequence(mPath, mError) : nullptr};

    if (Source == nullptr || Source != mSource || mSourceChanged) {
        mError = "The source animation changed. Close and reopen the viewer before saving.";

        return false;
    }

    const bool PreviousEnabled{Source->IsRootMotionEnabled()};
    const bool PreviousForced{Source->IsRootLockForced()};
    const Asset::ERootMotionRootLock PreviousLock{Source->GetRootMotionRootLock()};
    const Core::FString PreviousBone{Source->GetRootMotionBone()};

    CopySettings(*mEditing, *Source);

    if (!mAssets->SaveAnimSequence(mPath, *Source, mError)) {
        Source->SetRootMotionEnabled(PreviousEnabled);
        Source->SetForceRootLock(PreviousForced);
        Source->SetRootMotionRootLock(PreviousLock);
        Source->SetRootMotionBone(PreviousBone);

        return false;
    }

    mDirty = false;

    return true;
}

void FAnimSequenceEditor::Close() {
    if (mPreview != nullptr) {
        mPreview->Pause();
    }

    mOpen = false;
    mDirty = false;
    mPath.clear();
}

bool FAnimSequenceEditor::IsOpen() const {
    return mOpen;
}

void FAnimSequenceEditor::SetWindowId(Core::FString Id) {
    mWindowId = std::move(Id);
}

const Core::FString& FAnimSequenceEditor::GetWindowId() const {
    return mWindowId;
}

bool FAnimSequenceEditor::IsFocused() const {
    if (mHostTab != nullptr) {
        return mHostTab->IsFocused();
    }

    ImGuiWindow* Window{mOpen ? ImGui::FindWindowByName(mWindowId.c_str()) : nullptr};
    ImGuiWindow* Focused{ImGui::GetCurrentContext()->NavWindow};

    return Window != nullptr && Focused != nullptr && ImGui::IsWindowChildOf(Focused, Window, true, false);
}

void FAnimSequenceEditor::CloseRetired(const std::filesystem::path& Path) {
    if (!mOpen || mAssets == nullptr) {
        return;
    }

    Core::FString Error{};
    const Core::FString Root{Core::FilePath::ToGenericUtf8(Core::FilePath::ToLower(Core::FilePath::FromUtf8(mAssets->GetRelativePath(Path, Error))))};
    const auto Contains{[&](const std::filesystem::path& Target) {
        const Core::FString Relative{Core::FilePath::ToGenericUtf8(Core::FilePath::ToLower(Core::FilePath::FromUtf8(mAssets->GetRelativePath(Target, Error))))};

        return !Root.empty() && !Target.empty() && (Root == "." || Relative == Root || Relative.starts_with(Root + "/"));
    }};

    if (Contains(mPath) || Contains(Core::FilePath::FromUtf8(mMeshPath)) || Contains(Core::FilePath::FromUtf8(mSkeletonPath))) {
        Close();
    }
}

void FAnimSequenceEditor::RefreshAssets() {
    Core::FString Error{};
    Asset::UAnimSequence* Source{mAssets->LoadAnimSequence(mPath, Error)};

    if (Source == nullptr) {
        mPreview->Pause();
        mSourceChanged = true;
        mError = Error;

        return;
    }

    if (Source != mSource) {
        if (mDirty) {
            mSourceChanged = true;
            mPreview->Pause();
        } else {
            const std::filesystem::path Path{mPath};
            OpenNow(Path, mError);
        }
    }

    Engine::USkeletalMeshComponent* Component{mPreview->GetComponent()};

    if (Component != nullptr) {
        const auto* Mesh{mAssets->LoadSkeletalMesh(Core::FilePath::FromUtf8(mMeshPath), Error)};
        const auto* Skeleton{mSkeletonPath.empty() ? mAssets->FindLegacySkeleton(Core::FilePath::FromUtf8(Component->GetMeshPath())) : mAssets->LoadSkeleton(Core::FilePath::FromUtf8(mSkeletonPath), Error)};

        if (Mesh != Component->GetSkeletalMesh() || Skeleton != Component->GetSkeleton()) {
            SetPreviewMesh(mMeshPath, mSkeletonPath);
        }
    }
}

void FAnimSequenceEditor::InitializeToolkit(FEditorTab& Tab) {
    mHostTab = &Tab;
    Tab.SetIcon(EEditorIcon::Skeleton, IM_COL32(80, 123, 255, 255));
    FEditorToolkitHost& Host{Tab.GetToolkitHost()};
    Host.AddWidget("Preview", "Preview", [this](float DeltaTime) {
        if (mPreview == nullptr || mViewport == nullptr) {
            return;
        }

        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
            mPreview->Tick(DeltaTime);
        }

        const ImVec2 Size{ImGui::GetContentRegionAvail()};
        mViewport->SetDisplay(mShowBones, mShowGrid, mWireframe);
        mViewport->DrawUI({Size.x, Size.y});
        mViewport->Render();
    });
    Host.AddWidget("Settings", "Preview Settings", [this](float) {
        if (mEditing != nullptr) {
            DrawSettings(mToolkitCanSave);
        }
    }, EEditorDockRegion::Outliner);
    Host.AddWidget("Browser", "Asset Browser", [this](float) {
        DrawAssetBrowser();
    }, EEditorDockRegion::Details);
    Host.AddWidget("Timeline", "Timeline", [this](float) {
        if (mPreview != nullptr && mEditing != nullptr) {
            mTimeline.Draw(*mPreview, *mEditing, std::max(1.0f, ImGui::GetContentRegionAvail().y));
        }
    }, EEditorDockRegion::Bottom);
}

void FAnimSequenceEditor::UpdateToolkit(float, bool CanSave) {
    mToolkitCanSave = CanSave;

    if (mOpen && mEditing != nullptr) {
        RefreshAssets();
        mHostTab->SetName(EditorStyle::TabIconPadding + Core::FilePath::ToUtf8(mPath.stem()) + (mDirty ? "*" : "") + mWindowId);
    }
}

void FAnimSequenceEditor::HandleCommands(bool CanSave) {
    if (mEditing != nullptr) {
        HandleShortcuts(CanSave);
    }
}

std::optional<FMessageBox::FSavePrompt> FAnimSequenceEditor::GetClosePrompt() {
    if (!mOpen || !mDirty) {
        return {};
    }

    auto Prompt{MakeSavePrompt()};

    if (Prompt.mSave) {
        Prompt.mSave = [this, SaveSettings = std::move(Prompt.mSave)]() {
            if (!SaveSettings()) {
                return false;
            }

            Close();

            return true;
        };
    }

    Prompt.mDiscard = [this]() {
        Close();

        return true;
    };

    return Prompt;
}

FMessageBox::FSavePrompt FAnimSequenceEditor::MakeSavePrompt() {
    FMessageBox::FSavePrompt Prompt{"Animation Settings", "Save changes to " + Core::FilePath::ToUtf8(mPath.filename()) + "?"};

    if (mToolkitCanSave && !mSourceChanged) {
        Prompt.mSave = [this]() {
            const bool Saved{mToolkitCanSave && Save()};

            if (!Saved && mHostTab != nullptr) {
                mHostTab->Open();
            }

            return Saved;
        };
    } else if (mSourceChanged) {
        Prompt.mMessage += "\nThe source animation changed. Reopen the editor before saving.";
    } else {
        Prompt.mMessage += "\nStop PIE before saving animation settings.";
    }

    return Prompt;
}

void FAnimSequenceEditor::DrawSettings(bool CanSave) {
    ImGui::SeparatorText("Preview Settings");

    if (mCatalog != nullptr && mThumbnails != nullptr) {
        Core::FString MeshPath{mMeshPath};
        Core::FString SkeletonPath{mSkeletonPath};
        const bool MeshChanged{mPicker.Draw(*mCatalog, *mThumbnails, "Mesh", MeshPath, Asset::EAssetType::SkeletalMesh, "Select skeletal mesh")};
        const bool SkeletonChanged{mPicker.Draw(*mCatalog, *mThumbnails, "Skeleton", SkeletonPath, Asset::EAssetType::Skeleton, "Embedded skeleton")};

        if (MeshChanged || SkeletonChanged) {
            SetPreviewMesh(MeshPath, SkeletonPath);
        }
    }

    if (ImGui::Button("Frame Mesh")) {
        mViewport->FrameMesh();
    }

    ImGui::Checkbox("Show Bones", &mShowBones);
    ImGui::Checkbox("Show Grid", &mShowGrid);
    ImGui::Checkbox("Wireframe", &mWireframe);

    int Mode{mPreview->GetPreviewMode()};
    ImGui::SetNextItemWidth(-1.0f);

    if (ImGui::Combo("##RootMotionPreview", &Mode, "Original pose\0Extract in place\0Apply root motion\0")) {
        mPreview->SetPreviewMode(Mode);
    }

    ImGui::TextDisabled("Root motion resets on each loop.");
    ImGui::SeparatorText("Animation Info");

    const Asset::FAnimDataModel& Model{mEditing->GetDataModel()};

    ImGui::TextWrapped("%s", Core::FilePath::ToGenericUtf8(mPath).c_str());
    ImGui::Text("Length: %.3f s", mEditing->GetPlayLength());
    ImGui::Text("Sample rate: %.3f fps", Model.GetFrameRate().AsDecimal());
    ImGui::Text("Last frame: %u | Keys: %u", Model.GetNumberOfFrames(), Model.GetNumberOfKeys());
    ImGui::Text("Tracks: %zu", Model.GetBoneAnimationTracks().size());

    if (const Engine::USkeletalMeshComponent* Component{mPreview->GetComponent()}) {
        const auto& Bones{Component->GetSkeleton()->GetBones()};
        std::unordered_set<Core::FString> Names{};

        for (const auto& Bone : Bones) {
            Names.insert(Bone.GetName());
        }

        std::size_t Matched{};

        for (const auto& Track : Model.GetBoneAnimationTracks()) {
            Matched += Names.contains(Track.mName) ? 1 : 0;
        }

        ImGui::TextWrapped("Bones: %zu | Matched tracks: %zu / %zu", Bones.size(), Matched, Model.GetBoneAnimationTracks().size());

        if (Matched < Model.GetBoneAnimationTracks().size()) {
            ImGui::TextWrapped("Some tracks have no matching bone. Unanimated bones use the reference pose.");
        }
    }

    ImGui::SeparatorText("Root Motion Settings");
    ImGui::BeginDisabled(!CanSave || mSourceChanged);

    bool Changed{};
    bool Enabled{mEditing->IsRootMotionEnabled()};
    bool Forced{mEditing->IsRootLockForced()};
    int Lock{static_cast<int>(mEditing->GetRootMotionRootLock())};

    Changed |= ImGui::Checkbox("Enable Root Motion", &Enabled);
    ImGui::SetNextItemWidth(-1.0f);
    Changed |= ImGui::Combo("##RootLock", &Lock, "Ref Pose\0Anim First Frame\0Zero\0");
    Changed |= ImGui::Checkbox("Force Root Lock", &Forced);
    ImGui::SetNextItemWidth(-1.0f);
    Changed |= ImGui::InputTextWithHint("##RootBone", "Root bone (empty: single root)", mRootBone.data(), mRootBone.size());

    if (Changed) {
        mEditing->SetRootMotionEnabled(Enabled);
        mEditing->SetForceRootLock(Forced);
        mEditing->SetRootMotionRootLock(static_cast<Asset::ERootMotionRootLock>(Lock));
        mEditing->SetRootMotionBone(mRootBone.data());
        mDirty = true;
        mPreview->RefreshPose();
    }

    ImGui::EndDisabled();
    ImGui::TextDisabled(mDirty ? "Unsaved changes (Ctrl+S)" : "Ctrl+S: Save settings");

    if (mSourceChanged) {
        ImGui::TextWrapped("The source asset changed. Close and reopen the viewer to use the new version.");
    }

    if (!mError.empty()) {
        ImGui::TextWrapped("%s", mError.c_str());
    }
}

void FAnimSequenceEditor::DrawAssetBrowser() {
    ImGui::SeparatorText("Animations");

    if (mCatalog == nullptr) {
        ImGui::TextDisabled("No animation catalog available.");

        return;
    }

    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x, 1.0f));
    ImGui::InputTextWithHint("##AnimationSearch", "Search name or path...", mAnimationSearch.data(), mAnimationSearch.size());
    ImGui::SameLine();

    if (ImGui::Button("X", {ImGui::GetFrameHeight(), ImGui::GetFrameHeight()})) {
        mAnimationSearch.fill('\0');
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear search");
    }

    const ImGuiTextFilter Filter{mAnimationSearch.data()};
    std::size_t Total{};
    std::size_t Matched{};

    for (const Editor::FEditorAssetEntry& Entry : mCatalog->GetEntries()) {
        if (Entry.GetType() == Asset::EAssetType::AnimSequence) {
            ++Total;
            Matched += Filter.PassFilter(Core::FilePath::ToGenericUtf8(Entry.GetPath()).c_str()) ? 1 : 0;
        }
    }

    ImGui::TextDisabled("%zu / %zu animations", Matched, Total);
    std::filesystem::path SelectedPath{};

    if (ImGui::BeginChild("##AnimationList", {}, ImGuiChildFlags_Borders)) {
        if (Matched == 0) {
            ImGui::TextDisabled("No matching animations.");
        }

        for (const Editor::FEditorAssetEntry& Entry : mCatalog->GetEntries()) {
            if (Entry.GetType() != Asset::EAssetType::AnimSequence) {
                continue;
            }

            const Core::FString Path{Core::FilePath::ToGenericUtf8(Entry.GetPath())};

            if (!Filter.PassFilter(Path.c_str())) {
                continue;
            }

            const bool Selected{Entry.GetPath() == mPath};
            const Core::FString Label{GetAnimationLabel(Entry.GetPath(), std::max(ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2.0f, 1.0f))};
            ImGui::PushID(Path.c_str());

            if (ImGui::Selectable(Label.c_str(), Selected) && !Selected) {
                SelectedPath = Entry.GetPath();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Path.c_str());
            }

            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    if (!SelectedPath.empty()) {
        Open(SelectedPath, mError);
    }
}

void FAnimSequenceEditor::StepFrame(Int32 Direction) {
    mTimeline.StepFrame(*mPreview, *mEditing, Direction);
}

void FAnimSequenceEditor::HandleShortcuts(bool CanSave) {
    if (!IsFocused() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
        return;
    }

    if (CanSave && !mSourceChanged && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        Save();
    }

    if (!mPreview->IsReady() || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyAlt) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (mPreview->IsPlaying()) {
            mPreview->Pause();
        } else {
            mPreview->Play();
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        StepFrame(-1);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        StepFrame(1);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        mPreview->Seek(0.0);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        mPreview->Seek(mEditing->GetPlayLength());
    }
}
