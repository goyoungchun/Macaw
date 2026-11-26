/**
 * @file FContentBrowserWidget.cpp
 * @brief 콘텐츠 탐색과 에셋 생성·가져오기·이름 변경·삭제를 제공합니다.
 * @ingroup Editor
 */

#include "FContentBrowserWidget.h"
#include "FEditorWorkspace.h"
#include "FAssetImportController.h"
#include "FAssetEditorManager.h"
#include "Editor/FImGuiUIScale.h"
#include "Editor/FEditorContext.h"
#include "FPhysicsAssetEditing.h"
#include "Editor/FEditorAssetCatalog.h"
#include "Editor/FAssetThumbnailCache.h"
#include "Editor/FAssetDragDrop.h"
#include "EditorStyle.h"
#include "Core/FilePath.h"
#include "Engine/FProject.h"
#include "Engine/FWorldSerializer.h"
#include "Engine/ULevel.h"
#include "Engine/UWorld.h"
#include "Engine/Particles/ParticleAssets.h"
#include "Serialization/JsonArchive.h"
#include "Asset/UMaterial.h"
#include "RenderCore/Shader/UShader.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Viewer/FAssetViewer.h"
#include "Platform/Window.h"
#include "Core/Types.h"
#include <shellapi.h>
#include <imgui_internal.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <string_view>

using namespace Editor;

/**
 * @brief FContentBrowserWidget 구현에 한정되는 내부 형식·상수·도우미 함수입니다.
 */
namespace {

    constexpr char ContentMovePayload[]{"MacawContentMove"};
    constexpr float CutContentOpacity{0.65f};

    bool IsShaderSource(const std::filesystem::path& Path) {
        const auto Extension{Core::FilePath::ToLowerExtension(Path)};

        return Extension == L".hlsl" || Extension == L".hlsli";
    }

    FAssetTileWidget::EContentKind GetContentKind(const std::filesystem::path& Path, bool Directory) {
        if (Directory) {
            return FAssetTileWidget::EContentKind::Folder;
        }

        if (Core::FilePath::ToLowerExtension(Path) == L".lua") {
            return FAssetTileWidget::EContentKind::LuaScript;
        }

        return IsShaderSource(Path) ? FAssetTileWidget::EContentKind::ShaderSource : FAssetTileWidget::EContentKind::File;
    }

    bool DrawFolderEntry(const FAssetTileWidget& AssetTile, const FAssetTileWidget::FStyle& Style, const Core::FString& Name, const ImVec2& Size, float Scale, bool Selected) {
        const ImVec2 Origin{ImGui::GetCursorScreenPos()};
        const bool Pressed{EditorStyle::Selectable("##Folder", Selected, ImGuiSelectableFlags_AllowDoubleClick, Size)};

        if (ImGui::IsItemVisible()) {
            const float Padding{4.0f * Scale};
            const float ImageSize{std::min(96.0f * Scale, Size.x - Padding * 2.0f)};
            const float IconSize{ImageSize * 0.8f};
            const float TypeY{Origin.y + ImageSize + Padding * 2.0f};
            const float NameY{TypeY + ImGui::GetTextLineHeightWithSpacing()};
            const float LineHeight{ImGui::GetTextLineHeight()};
            const float TextLeft{Origin.x + Padding};
            const float TextWidth{Size.x - Padding * 2.0f};
            const ImVec4 NameClip{TextLeft, NameY, TextLeft + TextWidth, std::min(NameY + LineHeight * 2.0f, Origin.y + Size.y)};
            ImDrawList& DrawList{*ImGui::GetWindowDrawList()};

            AssetTile.DrawIcon(DrawList, Style, {Origin.x + (Size.x - IconSize) * 0.5f, Origin.y + Padding + (ImageSize - IconSize) * 0.5f}, IconSize);
            DrawList.AddText({TextLeft, TypeY}, ImGui::GetColorU32(ImGuiCol_TextDisabled), Style.mLabel);
            DrawList.AddText(ImGui::GetFont(), LineHeight, {TextLeft, NameY}, ImGui::GetColorU32(ImGuiCol_Text), Name.c_str(), nullptr, TextWidth, &NameClip);
        }

        return Pressed;
    }

    bool IsContentPathWithin(const std::filesystem::path& Path, const std::filesystem::path& Directory) {
        const auto Relative{Core::FilePath::ToLower(Path).lexically_relative(Core::FilePath::ToLower(Directory))};
        return !Relative.empty() && *Relative.begin() != L"..";
    }

    Core::FString NormalizeReferenceText(Core::FString Text) {
        std::ranges::transform(Text, Text.begin(), [](unsigned char Letter) {
            return Letter == '\\' ? '/' : static_cast<char>(std::tolower(Letter));
        });
        Text.erase(std::unique(Text.begin(), Text.end(), [](char Left, char Right) {
            return Left == '/' && Right == '/';
        }), Text.end());

        return Text;
    }

    bool AssetHasReferenceFields(Asset::EAssetType Type) {
        return Type != Asset::EAssetType::Texture && Type != Asset::EAssetType::Skeleton && Type != Asset::EAssetType::AnimSequence && Type != Asset::EAssetType::PhysicsAsset;
    }

    bool RewriteJsonReferenceValues(rapidjson::Value& Value, std::string_view Field, bool Graph, rapidjson::Document::AllocatorType& Allocator, const std::function<Core::FString(const Core::FString&, bool)>& Remap, bool& Changed, std::size_t Depth) {
        if (Depth == 256) {
            return false;
        }

        if (Value.IsObject()) {
            for (auto Member{Value.MemberBegin()}; Member != Value.MemberEnd(); ++Member) {
                if (!RewriteJsonReferenceValues(Member->value, {Member->name.GetString(), Member->name.GetStringLength()}, Graph, Allocator, Remap, Changed, Depth + 1)) {
                    return false;
                }
            }
        } else if (Value.IsArray()) {
            for (auto& Element : Value.GetArray()) {
                if (!RewriteJsonReferenceValues(Element, Field, Graph, Allocator, Remap, Changed, Depth + 1)) {
                    return false;
                }
            }
        } else if (Value.IsString()) {
            constexpr std::string_view PathFields[]{"Animation", "AnimGraph", "BaseColorTexture", "BlendSpace", "Material", "Mesh", "NormalMap", "Panorama", "PhysicsAsset", "PhysicsAssetOverride", "ScriptPath", "Sequence", "Shader", "SkeletalMesh", "Skeleton", "SlotMaterials", "Sprite", "Template", "Texture"};
            const auto Replacement{Remap(Core::FString{Value.GetString(), Value.GetStringLength()}, std::ranges::find(PathFields, Field) != std::end(PathFields) || (Graph && (Field == "Value" || Field == "Default")))};
            if (!Replacement.empty()) {
                Value.SetString(Replacement.data(), static_cast<rapidjson::SizeType>(Replacement.size()), Allocator);
                Changed = true;
            }
        }

        return true;
    }

    bool RewriteJsonAssetReferences(Asset::EAssetType Type, Core::TArray<Uint8>& Payload, const std::function<Core::FString(const Core::FString&, bool)>& Remap, bool& Changed, Core::FString& Error) {
        rapidjson::Document Document{};
        Document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(reinterpret_cast<const char*>(Payload.data()), Payload.size());
        if (Document.HasParseError()) {
            Error = "Invalid asset JSON";
            return false;
        }

        if (!RewriteJsonReferenceValues(Document, {}, Type == Asset::EAssetType::AnimGraph, Document.GetAllocator(), Remap, Changed, 0)) {
            Error = "The asset JSON exceeds the supported nesting depth";
            return false;
        }
        if (Changed) {
            rapidjson::StringBuffer Buffer{};
            rapidjson::Writer<rapidjson::StringBuffer> Writer{Buffer};
            if (!Document.Accept(Writer)) {
                Error = "Cannot write asset JSON";
                return false;
            }
            Payload.assign(Buffer.GetString(), Buffer.GetString() + Buffer.GetSize());
        }

        return true;
    }

    bool RewriteAssetReferences(const Asset::FAssetManager& Assets, Asset::EAssetType Type, Core::TArray<Uint8>& Payload, const std::function<Core::FString(const Core::FString&, bool)>& Remap, bool& Changed, Core::FString& Error) {
        const bool Json{Type == Asset::EAssetType::Scene || Type == Asset::EAssetType::Sequence || Type == Asset::EAssetType::BlendSpace || Type == Asset::EAssetType::AnimGraph || Type == Asset::EAssetType::ParticleSystem};
        return Json ? RewriteJsonAssetReferences(Type, Payload, Remap, Changed, Error) : Assets.RewriteBinaryAssetReferences(Type, Payload, Remap, Changed, Error);
    }
}

FContentBrowserWidget::FContentBrowserWidget(FEditorContext* Context, Platform::FWindowsApplication* Platform, FEditorAssetCatalog& AssetCatalog, FAssetThumbnailCache& Thumbnails, FImGuiUIScale& UIScale, FEditorWorkspace& Workspace, FAssetImportController& Imports, FAssetEditorManager& AssetEditors)
	: FEditorContextWidget{Context, "ContentBrowser"},
	  mContext{Context},
	  mPlatform{Platform},
	  mAssetCatalog{&AssetCatalog},
	  mThumbnails{&Thumbnails},
	  mUIScale{UIScale},
	  mWorkspace{Workspace},
	  mImports{Imports},
	  mAssetEditors{AssetEditors} {
}

FContentBrowserWidget::~FContentBrowserWidget() = default;

void FContentBrowserWidget::Tick() {
    if (mPendingFixUpDirectory.has_value()) {
        const auto Directory{std::move(*mPendingFixUpDirectory)};
        mPendingFixUpDirectory.reset();
        FixUpRedirectors(Directory);
    }

    if (mRefreshContent) {
        RefreshContent();
    }
}

void FContentBrowserWidget::DrawDragPreview() {
    const ImGuiPayload* Payload{ImGui::GetDragDropPayload()};

    if (Payload == nullptr || Payload->SourceId != mAssetDragSourceId || Payload->IsDelivery()) {
        return;
    }

    const auto Path{Core::FilePath::FromUtf8(static_cast<const char*>(Payload->Data))};
    const auto Type{mAssetTypes.find(Path)};
    const bool HasThumbnail{Type != mAssetTypes.end() && FAssetThumbnailCache::Supports(Type->second)};
    void* Thumbnail{HasThumbnail ? mThumbnails->GetThumbnail(Path, Type->second) : nullptr};
    const bool Directory{mContentFolders.contains(Path)};
    const auto& Style{Type != mAssetTypes.end() ? FAssetTileWidget::GetStyle(Type->second) : FAssetTileWidget::GetStyle(GetContentKind(Path, Directory))};
    const Core::FString Name{Core::FilePath::ToUtf8(Directory ? Path.filename() : Path.stem())};

    if (ImGui::BeginTooltip()) {
        const float Scale{mUIScale.GetScale()};
        mAssetTile.DrawPreview(Name, Style, Thumbnail, FAssetTileWidget::Measure(104.0f * Scale, Scale), Scale);
        ImGui::EndTooltip();
    }
}

const std::filesystem::path& FContentBrowserWidget::GetDirectory() const {
    return mContentDirectory;
}

const std::filesystem::path& FContentBrowserWidget::GetSelectedPath() const {
    return mSelectedContentPath;
}

void FContentBrowserWidget::MarkRefresh() {
    mRefreshContent = true;
}

void FContentBrowserWidget::SetCatalogChanged(std::function<void()> Handler) {
    mCatalogChanged = std::move(Handler);
}

void FContentBrowserWidget::Reveal(const std::filesystem::path& Path) {
    mContentFilter.reset();
    mContentSearch.fill('\0');
    NavigateContent(Path.parent_path());
    mSelectedContentPath = Path;
    mWorkspace.FocusContentBrowser();
}

void FContentBrowserWidget::OnImported(const std::filesystem::path& Path, Uint32 Count, const Core::FString& Error) {
    mContentError = Error;
    if (Count != 0) {
        mContentFilter.reset();
        mContentSearch.fill('\0');
        MarkRefresh();
        RefreshContent();
        NavigateContent(Count == 1 ? Path.parent_path() : Path);
        mSelectedContentPath = Path;
        mContentError = Error;
    }
}

void FContentBrowserWidget::DrawContent(float) {
    DrawContent();
}

void FContentBrowserWidget::LoadContentIcons(RenderCore::IRendererModule* Renderer) {
    mAssetTile.LoadIcons(Renderer);
}

void FContentBrowserWidget::DrawContent() {
    const ImVec2 Minimum{ImGui::GetWindowPos()};
    const ImVec2 Size{ImGui::GetWindowSize()};

    mImports.RegisterContentDropTarget(mContentDirectory, {Minimum.x, Minimum.y, Minimum.x + Size.x, Minimum.y + Size.y});
    ImGui::TextUnformatted(mContext->GetProject()->GetName().c_str());
    ImGui::SameLine();

    if (EditorStyle::SmallButton("Refresh")) {
        mRefreshContent = true;
    }

    ImGui::Separator();
    DrawContentBreadcrumbs();
    ImGui::SetNextItemWidth(120.0f);

    const char* FilterName{!mContentFilter.has_value() ? "All Assets" : *mContentFilter == Asset::EAssetType::Mesh         ? "Meshes" : *mContentFilter == Asset::EAssetType::SkeletalMesh ? "Skeletal Meshes" : *mContentFilter == Asset::EAssetType::Skeleton ? "Skeletons" : *mContentFilter == Asset::EAssetType::PhysicsAsset ? "Physics Assets" :*mContentFilter == Asset::EAssetType::Shader ? "Shaders" : *mContentFilter == Asset::EAssetType::Material ? "Materials" : *mContentFilter == Asset::EAssetType::Texture ? "Textures" : *mContentFilter == Asset::EAssetType::Sequence ? "Sequences" : *mContentFilter == Asset::EAssetType::AnimSequence ? "Animation Sequences" : *mContentFilter == Asset::EAssetType::BlendSpace ? "Blend Spaces" : *mContentFilter == Asset::EAssetType::AnimGraph ? "Anim Graphs" : *mContentFilter == Asset::EAssetType::ParticleSystem ? "Particle Systems" : "Scenes"};

    if (ImGui::BeginCombo("##AssetType", FilterName)) {
        if (ImGui::Selectable("All Assets", !mContentFilter.has_value())) {
            mContentFilter.reset();
        }

        for (Asset::EAssetType Type : {Asset::EAssetType::Mesh, Asset::EAssetType::SkeletalMesh, Asset::EAssetType::Skeleton, Asset::EAssetType::PhysicsAsset, Asset::EAssetType::Material, Asset::EAssetType::Shader, Asset::EAssetType::Texture, Asset::EAssetType::Scene, Asset::EAssetType::Sequence, Asset::EAssetType::AnimSequence, Asset::EAssetType::BlendSpace, Asset::EAssetType::AnimGraph, Asset::EAssetType::ParticleSystem}) {
            const char* Name{Type == Asset::EAssetType::Mesh ? "Meshes" : Type == Asset::EAssetType::SkeletalMesh ? "Skeletal Meshes" : Type == Asset::EAssetType::Skeleton ? "Skeletons" : Type == Asset::EAssetType::PhysicsAsset ? "Physics Assets" : Type == Asset::EAssetType::Shader ? "Shaders" : Type == Asset::EAssetType::Material ? "Materials" : Type == Asset::EAssetType::Texture ? "Textures" : Type == Asset::EAssetType::Sequence ? "Sequences" : Type == Asset::EAssetType::AnimSequence ? "Animation Sequences" : Type == Asset::EAssetType::BlendSpace ? "Blend Spaces" : Type == Asset::EAssetType::AnimGraph ? "Anim Graphs" : Type == Asset::EAssetType::ParticleSystem ? "Particle Systems" : "Scenes"};

            if (ImGui::Selectable(Name, mContentFilter == Type)) {
                mContentFilter = Type;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##ContentSearch", "Search assets...", mContentSearch.data(), mContentSearch.size());

    if (!mContentError.empty()) {
        ImGui::TextWrapped("%s", mContentError.c_str());
    }

    // 테이블 행의 위아래 셀 여백만큼 줄여 창 스크롤바가 생기지 않게 합니다.
    const float Height{std::max(64.0f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().CellPadding.y * 2.0f)};
    bool FolderPaneFocused{};

    if (ImGui::BeginTable("Content Browser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("Content Folders", ImVec2{0.0f, Height});
        DrawContentTree({});
        DrawContentTree(L"/Engine");
        FolderPaneFocused = ImGui::IsWindowFocused();

        mRevealContentDirectory = false;

        if (ImGui::BeginPopupContextWindow("ContentFolderMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            DrawContentContextMenu(mContentDirectory);
            ImGui::EndPopup();
        }

        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("Content Files", ImVec2{0.0f, Height});
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            mContextPasteDirectory = mContentDirectory;
        }
        DrawContentEntries();

        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && !mSelectedContentPath.empty() && mContext->CanEditWorld()) {
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                BeginContentAction(EContentAction::Rename);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                BeginContentAction(EContentAction::Delete);
            }
        }

        if (ImGui::BeginPopupContextWindow("ContentFileMenu")) {
            DrawContentContextMenu(mContextPasteDirectory);
            ImGui::EndPopup();
        }

        ImGui::EndChild();
        ImGui::EndTable();
    }

    DrawContentClipboardShortcuts(FolderPaneFocused);
    DrawContentActionPopup();
}

void FContentBrowserWidget::DrawContentClipboardShortcuts(bool FolderPaneFocused) {
    const ImGuiIO& Input{ImGui::GetIO()};
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || Input.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) || mOpenContentAction) {
        return;
    }

    if (Input.KeyCtrl && !Input.KeyShift && !Input.KeyAlt && !Input.KeySuper) {
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            CutContent(FolderPaneFocused ? mContentDirectory : mSelectedContentPath);
        } else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            PasteContent(mContentDirectory);
        }
    } else if (!mCutContentPath.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##CutContentCancel"), ImGuiInputFlags_LockThisFrame);
        mCutContentPath.clear();
        mContentError.clear();
    }
}

std::filesystem::path FContentBrowserWidget::GetContentActionPath(const std::filesystem::path& Path) const {
    Core::FString Error{};

    return mContext->GetProject()->GetAssets()->ResolveWritablePath(Path, Error);
}

void FContentBrowserWidget::BeginContentAction(EContentAction Action) {
    const std::filesystem::path Absolute{GetContentActionPath(mSelectedContentPath)};
    if (Absolute.empty()) {
        return;
    }

    mContentAction = Action;
    mContentActionTarget = mSelectedContentPath;
    mMoveDestinationDirectory = mContentDirectory;
    mContentActionError.clear();
    mContentActionReferences.clear();
    mDeleteReplacement.clear();
    mConfirmForceDelete = false;
    mOpenContentAction = true;

    std::error_code Code{};
    const bool Directory{std::filesystem::is_directory(Absolute, Code)};
    const Core::FString Name{Core::FilePath::ToUtf8(Directory ? mContentActionTarget.filename() : mContentActionTarget.stem())};

    mContentActionName.fill('\0');
    std::copy_n(Name.begin(), std::min(Name.size(), mContentActionName.size() - 1), mContentActionName.begin());

    if (Action != EContentAction::Delete) {
        return;
    }

    CheckDeleteReferences();
}

Core::FString FContentBrowserWidget::FindContentActionBlocker(const std::filesystem::path& Absolute) const {
    if (Absolute.empty()) {
        return "Cannot edit this content path";
    }

    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};
    Core::FString Error{};
    std::error_code Code{};
    const auto Contains{[&](const std::filesystem::path& Open) {
        if (Open.empty()) {
            return false;
        }

        const std::filesystem::path Resolved{Assets->ResolvePath(Open, Error)};
        const auto Relative{std::filesystem::weakly_canonical(Resolved, Code).lexically_relative(std::filesystem::weakly_canonical(Absolute, Code))};

        return !Resolved.empty() && !Relative.empty() && *Relative.begin() != L"..";
    }};

    if (!mContext->CanEditWorld()) {
        return "PIE 중에는 바꿀 수 없습니다.";
    }

    if (Contains(mContext->GetScenePath())) {
        return "지금 열려 있는 씬입니다. 다른 씬을 연 뒤에 다시 시도하세요.";
    }

    if (mAssetEditors.HasOpenAsset(Asset::EAssetType::Material, Contains)) {
        return "편집 중인 머티리얼입니다. 머티리얼 편집기를 닫은 뒤에 다시 시도하세요.";
    }

    if (mAssetEditors.HasOpenAsset(Asset::EAssetType::ParticleSystem, Contains)) {
        return "편집 중인 파티클 시스템입니다. 파티클 편집기를 닫은 뒤에 다시 시도하세요.";
    }

    if (mAssetEditors.HasUnsavedSource(Contains)) {
        return "편집기에서 소스 파일의 변경 사항을 저장하거나 취소한 뒤 다시 시도하세요.";
    }

    return {};
}

void FContentBrowserWidget::DrawContentActionPopup() {
    constexpr const char* RenameName{"Rename Asset"};
    constexpr const char* DeleteName{"Delete Asset"};
    const char* PopupName{mContentAction == EContentAction::Rename ? RenameName : mContentAction == EContentAction::Move ? "Move Asset" : DeleteName};

    if (mOpenContentAction) {
        ImGui::OpenPopup(PopupName);

        mOpenContentAction = false;
    }

    if (!ImGui::BeginPopupModal(PopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const std::filesystem::path Absolute{GetContentActionPath(mContentActionTarget)};
    std::error_code Code{};
    const bool Directory{std::filesystem::is_directory(Absolute, Code)};
    const Core::FString Display{Core::FilePath::ToGenericUtf8(mContentActionTarget)};
    bool Confirmed{};

    if (mContentAction == EContentAction::Rename) {
        ImGui::Text("%s의 새 이름", Display.c_str());

        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        Confirmed = ImGui::InputText("##ContentRename", mContentActionName.data(), mContentActionName.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    } else if (mContentAction == EContentAction::Move) {
        const Core::FString Current{mMoveDestinationDirectory.empty() ? "Content" : Core::FilePath::ToGenericUtf8(mMoveDestinationDirectory)};
        ImGui::Text("%s을(를) %s 폴더로 옮길까요?", Display.c_str(), Current.c_str());
    } else {
        ImGui::Text("%s%s을(를) 휴지통으로 옮길까요?", Display.c_str(), Directory ? " 폴더와 그 안의 모든 파일" : "");
        const auto Source{mAssetCatalog->FindTextureSource(mContentActionTarget)};
        if (!Source.empty()) {
            ImGui::Text("원본 이미지 %s도 함께 옮깁니다.", Core::FilePath::ToGenericUtf8(Source).c_str());
        }
    }

    const bool HasDeleteReferences{mContentAction == EContentAction::Delete && (!mContentActionReferences.empty() || mDeleteLiveReference || mDeleteStartupReference || !mDeleteCheckError.empty())};
    if (HasDeleteReferences) {
        ImGui::Spacing();
        ImGui::TextColored({1.0f, 0.75f, 0.3f, 1.0f}, "삭제 대상의 참조를 확인하세요.");

        if (mDeleteLiveReference) {
            ImGui::BulletText("현재 열린 씬");
        }
        if (mDeleteStartupReference) {
            ImGui::BulletText("프로젝트 시작 씬");
        }

        for (Index_t Index{}; Index < std::min<std::size_t>(mContentActionReferences.size(), 6); ++Index) {
            ImGui::BulletText("%s", Core::FilePath::ToGenericUtf8(mContentActionReferences[Index]).c_str());
        }

        if (mContentActionReferences.size() > 6) {
            ImGui::TextDisabled("외 %zu개", mContentActionReferences.size() - 6);
        }

        if (!mDeleteCheckError.empty()) {
            ImGui::TextWrapped("%s", mDeleteCheckError.c_str());
        }
    }

    if (!mContentActionError.empty()) {
        ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", mContentActionError.c_str());
    }

    ImGui::Spacing();

    bool Replace{};
    bool Force{};
    if (mContentAction == EContentAction::Delete && HasDeleteReferences) {
        if (mConfirmForceDelete) {
            ImGui::TextWrapped("강제 삭제하면 기존 참조가 끊어질 수 있습니다.");
            if (mDeleteStartupReference) {
                ImGui::TextWrapped("시작 씬을 삭제하면 다음 프로젝트 실행 전에 새 시작 씬을 지정해야 합니다.");
            }
            Force = EditorStyle::Button("Confirm Force Delete");
        } else {
            if (mDeleteType.has_value()) {
                const Core::FString Label{mDeleteReplacement.empty() ? "Select an asset" : Core::FilePath::ToGenericUtf8(mDeleteReplacement)};
                ImGui::SetNextItemWidth(280.0f * mUIScale.GetScale());
                if (ImGui::BeginCombo("Replacement", Label.c_str())) {
                    for (const auto& Entry : mAssetCatalog->GetEntries()) {
                        const auto& Path{Entry.GetPath()};
                        if (Entry.GetType() == mDeleteType && !Path.has_root_directory() && Core::FilePath::ToLowerExtension(Path) == L".masset" && Path != mContentActionTarget) {
                            const auto Name{Core::FilePath::ToGenericUtf8(Path)};
                            if (ImGui::Selectable(Name.c_str(), Path == mDeleteReplacement)) {
                                mDeleteReplacement = Path;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::BeginDisabled(mDeleteReplacement.empty());
                Replace = EditorStyle::Button("Replace References");
                ImGui::EndDisabled();
                ImGui::SameLine();
            }
            if (EditorStyle::Button("Force Delete")) {
                mConfirmForceDelete = true;
            }
        }
    } else {
        ImGui::BeginDisabled(mContentAction == EContentAction::Move && mMoveDestinationDirectory == mContentActionTarget.parent_path());
        Confirmed |= EditorStyle::Button(mContentAction == EContentAction::Rename ? "Rename" : mContentAction == EContentAction::Move ? "Move Here" : "Delete");
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (EditorStyle::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    } else if (Replace) {
        mContentActionError = FindContentActionBlocker(Absolute);
        if (mContentActionError.empty() && CheckDeleteReferences(mDeleteReplacement) && DeleteContent(false)) {
            ImGui::CloseCurrentPopup();
        }
    } else if (Force && DeleteContent(true)) {
        ImGui::CloseCurrentPopup();
    } else if (Confirmed && (mContentAction == EContentAction::Rename ? RenameContent() : mContentAction == EContentAction::Move ? RelocateContent(mContentActionTarget, mMoveDestinationDirectory / mContentActionTarget.filename(), mContentActionError) : DeleteContent(false))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool FContentBrowserWidget::RenameContent() {
    const std::filesystem::path Absolute{GetContentActionPath(mContentActionTarget)};
    const Core::FString NewName{mContentActionName.data()};
    std::error_code Code{};
    const bool Directory{std::filesystem::is_directory(Absolute, Code)};

    if (NewName.empty() || NewName.find_first_of("\\/:*?\"<>|") != Core::FString::npos || NewName.back() == '.' || NewName.back() == ' ') {
        mContentActionError = "이름이 비어 있거나 쓸 수 없는 문자가 들어 있습니다.";

        return false;
    }

    const auto Source{mAssetCatalog->FindTextureSource(mContentActionTarget)};
    auto NewFile{Core::FilePath::FromUtf8(NewName.c_str())};
    if (!Source.empty() && Core::FilePath::ToLowerExtension(NewFile) != Core::FilePath::ToLowerExtension(Source)) {
        NewFile += Source.extension();
    }

    if (!Directory) {
        NewFile += mContentActionTarget.extension();
    }

    return RelocateContent(mContentActionTarget, mContentActionTarget.parent_path() / NewFile, mContentActionError);
}

bool FContentBrowserWidget::RelocateContent(const std::filesystem::path& SourcePath, const std::filesystem::path& Target, Core::FString& Error) {
    const auto Absolute{GetContentActionPath(SourcePath)};
    if (Error = FindContentActionBlocker(Absolute); !Error.empty()) {
        return false;
    }

    const auto Parent{GetContentActionPath(Target.parent_path().empty() ? std::filesystem::path{L"."} : Target.parent_path())};
    std::error_code Code{};
    if (Parent.empty() || !std::filesystem::is_directory(Parent, Code)) {
        Error = "이동할 폴더를 찾을 수 없습니다.";
        return false;
    }

    const auto Destination{Parent / Target.filename()};
    if (Destination == Absolute) {
        return true;
    }

    // 대소문자만 바꾸는 경우는 같은 파일로 보이므로 존재 검사를 건너뜁니다.
    if (std::filesystem::exists(Destination, Code) && !std::filesystem::equivalent(Destination, Absolute, Code)) {
        Error = "같은 이름의 항목이 이미 있습니다.";

        return false;
    }

    const auto Source{mAssetCatalog->FindTextureSource(SourcePath)};
    const auto SourceAbsolute{Source.empty() ? std::filesystem::path{} : GetContentActionPath(Source)};
    const auto SourceDestination{Destination.parent_path() / Destination.stem()};
    if (!SourceAbsolute.empty() && SourceAbsolute != SourceDestination) {
        if (std::filesystem::exists(SourceDestination, Code) && !std::filesystem::equivalent(SourceDestination, SourceAbsolute, Code)) {
            Error = "같은 이름의 원본 이미지가 이미 있습니다.";
            return false;
        }

        std::filesystem::rename(SourceAbsolute, SourceDestination, Code);
        if (Code) {
            Error = "원본 이미지를 옮기지 못했습니다: " + Code.message();
            return false;
        }
    }

    auto* Assets{mContext->GetProject()->GetAssets()};
    if (!Assets->RenameContentPath(Absolute, Destination, Error)) {
        if (!SourceAbsolute.empty() && SourceAbsolute != SourceDestination) {
            std::filesystem::rename(SourceDestination, SourceAbsolute, Code);
            if (Code) {
                Error += " 원본 이미지를 되돌리지 못했습니다: " + Code.message();
            }
        }

        return false;
    }

    if (!SourceAbsolute.empty()) {
        // 옛 경로의 캐시 객체는 바뀌지 않으므로 은퇴시켜, 나중에 같은 경로를 다시 쓸 때 새로 로드하게 합니다.
        Assets->RetireAssets(SourceAbsolute);
        mAssetEditors.CloseRetired(SourceAbsolute);
    }

    mAssetEditors.CloseRetired(Absolute);
    mAssetEditors.CloseRetired(Destination);
    mSelectedContentPath = Target;
    mRefreshContent = true;

    if (mContentDirectory == SourcePath) {
        mContentDirectory = mSelectedContentPath;
    } else if (SourcePath.parent_path() != Target.parent_path()) {
        mContentDirectory = Target.parent_path();
    }

    mContext->AddLog("Moved " + Core::FilePath::ToGenericUtf8(SourcePath) + " to " + Core::FilePath::ToGenericUtf8(Target));
    if (!mCutContentPath.empty() && IsContentPathWithin(mCutContentPath, SourcePath)) {
        mCutContentPath.clear();
    }

    return true;
}

bool FContentBrowserWidget::CanCutContent(const std::filesystem::path& Path) const {
    return mContext->CanEditWorld() && !Path.empty() && !Path.has_root_directory() && (mContentFolders.contains(Path) || (mAssetTypes.contains(Path) && Core::FilePath::ToLowerExtension(Path) == L".masset"));
}

bool FContentBrowserWidget::CanPasteContent(const std::filesystem::path& Directory) const {
    return mContext->CanEditWorld() && !mCutContentPath.empty() && !Directory.has_root_directory() && mContentFolders.contains(Directory) && Core::FilePath::ToLower(Directory) != Core::FilePath::ToLower(mCutContentPath.parent_path()) && !IsContentPathWithin(Directory, mCutContentPath);
}

void FContentBrowserWidget::CutContent(const std::filesystem::path& Path) {
    if (CanCutContent(Path)) {
        mCutContentPath = Path;
        mContentError.clear();
    }
}

void FContentBrowserWidget::PasteContent(const std::filesystem::path& Directory) {
    if (mCutContentPath.empty() || !mContext->CanEditWorld()) {
        return;
    }

    if (!CanPasteContent(Directory)) {
        mContentError = "이 폴더에 붙여넣을 수 없습니다. 다른 프로젝트 콘텐츠 폴더를 선택하세요.";
        return;
    }

    RelocateContent(mCutContentPath, Directory / mCutContentPath.filename(), mContentError);
}

void FContentBrowserWidget::AcceptContentMove(const std::filesystem::path& Directory) {
    const auto* Payload{ImGui::GetDragDropPayload()};
    if (Directory.has_root_directory() || Payload == nullptr || Payload->SourceId != mAssetDragSourceId || Payload->Data == nullptr || !mContext->CanEditWorld()) {
        return;
    }

    const auto Path{Core::FilePath::FromUtf8(static_cast<const char*>(Payload->Data))};
    if (!CanCutContent(Path) || Path.parent_path() == Directory || IsContentPathWithin(Directory, Path)) {
        return;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload(Payload->DataType) != nullptr) {
            mSelectedContentPath = Path;
            BeginContentAction(EContentAction::Move);
            mMoveDestinationDirectory = Directory;
        }
        ImGui::EndDragDropTarget();
    }
}

bool FContentBrowserWidget::CheckDeleteReferences(const std::filesystem::path& Replacement) {
    mContentActionReferences.clear();
    mDeleteType.reset();
    mDeleteCheckError.clear();
    mDeleteLiveReference = false;
    mDeleteStartupReference = false;

    auto& Project{*mContext->GetProject()};
    auto& Assets{*Project.GetAssets()};
    const auto& Root{Assets.GetContentRoot()};
    const auto Absolute{GetContentActionPath(mContentActionTarget)};
    const auto Source{mAssetCatalog->FindTextureSource(mContentActionTarget)};
    const auto SourceAbsolute{Source.empty() ? std::filesystem::path{} : GetContentActionPath(Source)};
    const bool Replacing{!Replacement.empty()};
    std::error_code Code{};
    if (Absolute.empty() || !std::filesystem::exists(Absolute, Code) || Code) {
        mDeleteCheckError = "삭제할 콘텐츠를 찾을 수 없습니다.";
        return false;
    }

    const auto IsDeleted{[&](const std::filesystem::path& Path) {
        return !Path.empty() && (IsContentPathWithin(Path, Absolute) || (!SourceAbsolute.empty() && Core::FilePath::ToLower(Path) == Core::FilePath::ToLower(SourceAbsolute)));
    }};
    const auto Report{[&](const std::filesystem::path& File, const Core::FString& Error) {
        if (mDeleteCheckError.empty()) {
            mDeleteCheckError = Core::FilePath::ToGenericUtf8(File.lexically_relative(Root)) + " · " + Error;
        }
    }};

    struct FReferenceFile {
        std::filesystem::path mPath{};
        std::optional<Asset::EAssetType> mType{};
    };
    struct FReferenceEdit {
        std::filesystem::path mPath{};
        Asset::EAssetType mType{};
        Core::TArray<Uint8> mPayload{};
        std::filesystem::file_time_type mTime{};
    };

    Core::TArray<FReferenceFile> Files{};
    Core::TArray<Core::FGuid> Guids{};
    Core::TArray<Core::FString> Literals{};
    Core::FString Error{};
    const bool Directory{std::filesystem::is_directory(Absolute, Code)};
    const auto AddLiteral{[&](const std::filesystem::path& Path, bool Prefix) {
        const auto Relative{Path.lexically_relative(Root)};
        if (!Relative.empty()) {
            Literals.push_back(NormalizeReferenceText(Core::FilePath::ToGenericUtf8(Relative) + (Prefix ? "/" : "")));
        }
    }};
    AddLiteral(Absolute, Directory);
    if (!SourceAbsolute.empty()) {
        AddLiteral(SourceAbsolute, false);
    }

    std::filesystem::recursive_directory_iterator Iterator{Root, Code};
    const std::filesystem::recursive_directory_iterator End{};
    for (; !Code && Iterator != End; Iterator.increment(Code)) {
        if (Iterator->is_symlink(Code)) {
            Iterator.disable_recursion_pending();
            continue;
        }
        if (Code || !Iterator->is_regular_file(Code)) {
            continue;
        }

        const auto& Path{Iterator->path()};
        const auto Extension{Core::FilePath::ToLowerExtension(Path)};
        if (Extension == L".masset") {
            Asset::EAssetType Type{};
            auto Guid{Core::FGuid::Null()};
            if (!Asset::FAssetFile::GetIdentity(Path, Type, Guid, Error)) {
                Report(Path, Error);
                continue;
            }

            if (IsDeleted(Path)) {
                if (Core::FilePath::ToLower(Path) == Core::FilePath::ToLower(Absolute) && Type != Asset::EAssetType::Redirector) {
                    mDeleteType = Type;
                }
                if (Guid.IsValid()) {
                    Guids.push_back(Guid);
                    Literals.push_back(NormalizeReferenceText(Guid.ToString()));
                }
            } else if (AssetHasReferenceFields(Type)) {
                Files.push_back({Path, Type});
            }
        } else if (!IsDeleted(Path) && (Extension == L".lua" || IsShaderSource(Path))) {
            Files.push_back({Path, {}});
        }
    }

    if (Code) {
        Report(Root, Code.message());
    }

    Core::FString ReplacementPath{};
    std::filesystem::path ReplacementAbsolute{};
    auto ReplacementGuid{Core::FGuid::Null()};
    if (Replacing) {
        ReplacementAbsolute = GetContentActionPath(Replacement);
        Asset::EAssetType Type{};
        if (ReplacementAbsolute.empty() || IsDeleted(ReplacementAbsolute) || !mDeleteType.has_value() || !Asset::FAssetFile::GetIdentity(ReplacementAbsolute, Type, ReplacementGuid, Error) || Type != *mDeleteType) {
            mDeleteCheckError = "같은 타입의 다른 에셋을 선택하세요.";
            return false;
        }

        if (mContext->IsSceneDirty() || mContext->IsSceneEditActive()) {
            mDeleteCheckError = "참조를 교체하기 전에 현재 씬을 저장하세요.";
            return false;
        }

        ReplacementPath = Assets.GetRelativePath(ReplacementAbsolute, Error);
        if (ReplacementPath.empty()) {
            mDeleteCheckError = Error;
            return false;
        }
    }

    Core::TArray<FReferenceEdit> Edits{};
    for (const auto& File : Files) {
        if (File.mType != Asset::EAssetType::Redirector) {
            continue;
        }

        const auto Resolved{Assets.ResolvePath(File.mPath, Error)};
        const bool FolderAlias{File.mPath.filename() == L".redirector.masset"};
        if (IsDeleted(Resolved)) {
            mContentActionReferences.push_back(File.mPath.lexically_relative(Root));
            AddLiteral(FolderAlias ? File.mPath.parent_path() : File.mPath, FolderAlias);
            if (!FolderAlias) {
                auto Raw{File.mPath};
                Raw.replace_extension();
                if (Asset::FAssetManager::IsTextureSource(Raw)) {
                    AddLiteral(Raw, false);
                }
            }

            if (Replacing && !FolderAlias) {
                const auto Time{std::filesystem::last_write_time(File.mPath, Code)};
                if (Code) {
                    Report(File.mPath, Code.message());
                } else {
                    Edits.push_back({File.mPath, *File.mType, {ReplacementPath.begin(), ReplacementPath.end()}, Time});
                }
            } else if (Replacing) {
                Report(File.mPath, "폴더 Redirector는 에셋 하나로 교체할 수 없습니다.");
            }
        } else if (FolderAlias && !Resolved.empty() && IsContentPathWithin(Absolute, Resolved)) {
            const auto Relative{Core::FilePath::ToLower(Absolute).lexically_relative(Core::FilePath::ToLower(Resolved))};
            AddLiteral(File.mPath.parent_path() / Relative, Directory);
        }
    }

    Core::TMap<Core::FString, bool> Paths{};
    const auto Match{[&](const Core::FString& Value, bool& GuidReference) {
        GuidReference = false;
        auto Guid{Core::FGuid::Null()};
        if (Value.size() == 36 && Core::FGuid::Parse(Value, Guid)) {
            GuidReference = true;
            return std::ranges::find(Guids, Guid) != Guids.end();
        }
        if (Value.find('.') == Core::FString::npos || Value.find('\0') != Core::FString::npos) {
            return false;
        }

        if (const auto Existing{Paths.find(Value)}; Existing != Paths.end()) {
            return Existing->second;
        }

        bool Found{};
        try {
            const auto Path{Core::FilePath::FromUtf8(Value)};
            Core::FString PathError{};
            Found = Path.has_extension() && IsDeleted(Assets.ResolvePath(Path, PathError));
        } catch (const std::filesystem::filesystem_error&) {
        }
        Paths.emplace(Value, Found);
        return Found;
    }};

    bool IgnoredGuid{};
    mDeleteStartupReference = Match(Project.GetStartupScene(), IgnoredGuid);
    if (const auto* World{mContext->GetWorld()}; World != nullptr && World->GetPersistentLevel() != nullptr) {
        Core::TArray<Engine::AActor*> Actors{};
        for (const auto& Actor : World->GetPersistentLevel()->GetActors()) {
            if (Actor != nullptr) {
                Actors.push_back(Actor.get());
            }
        }
        Serialization::FJsonArchive Archive{Core::EArchiveMode::Saving};
        Engine::FWorldSerializer::SaveActors(Archive, Actors);
        const auto Json{Archive.ToString()};
        Core::TArray<Uint8> Payload{Json.begin(), Json.end()};
        const auto Inspect{[&](const Core::FString& Value, bool) -> Core::FString {
            bool Guid{};
            mDeleteLiveReference |= Match(Value, Guid);
            return {};
        }};
        bool Changed{};
        if (Archive.HasError() || !RewriteJsonAssetReferences(Asset::EAssetType::Scene, Payload, Inspect, Changed, Error)) {
            Report(mContext->GetScenePath(), Archive.HasError() ? Archive.GetError() : Error);
        }
        if (Replacing && mDeleteLiveReference && mContext->GetScenePath().empty()) {
            Report(Root, "열린 씬을 저장한 뒤 참조를 교체하세요.");
        }
    }

    if (!mAssetEditors.CollectSavePrompts().empty()) {
        Report(Root, "열린 에셋 편집기의 변경 사항을 저장한 뒤 다시 시도하세요.");
    }

    for (const auto& File : Files) {
        if (File.mType == Asset::EAssetType::Redirector) {
            continue;
        }
        if (!File.mType.has_value()) {
            std::ifstream Input{File.mPath, std::ios::binary};
            if (!Input) {
                Report(File.mPath, "소스 파일을 읽을 수 없습니다.");
                continue;
            }
            const auto Text{NormalizeReferenceText(Core::FString{std::istreambuf_iterator<char>{Input}, std::istreambuf_iterator<char>{}})};
            if (std::ranges::any_of(Literals, [&](const Core::FString& Value) {
                return Text.find(Value) != Core::FString::npos;
            })) {
                mContentActionReferences.push_back(File.mPath.lexically_relative(Root));
                if (Replacing) {
                    Report(File.mPath, "소스에 있는 참조는 직접 바꿔야 합니다.");
                }
            }
            continue;
        }

        const auto Type{*File.mType};
        const auto Time{std::filesystem::last_write_time(File.mPath, Code)};
        const auto Size{Code ? 0 : std::filesystem::file_size(File.mPath, Code)};
        const auto Cached{mReferenceCache.find(File.mPath)};
        if (!Code && Cached != mReferenceCache.end() && Cached->second.mTime == Time && Cached->second.mSize == Size && Cached->second.mType == Type) {
            const bool Found{std::ranges::any_of(Cached->second.mValues, [&](const Core::FString& Value) {
                bool Guid{};
                return Match(Value, Guid);
            })};
            if (!Replacing || !Found) {
                if (Found) {
                    mContentActionReferences.push_back(File.mPath.lexically_relative(Root));
                }
                continue;
            }
        }

        Core::TArray<Uint8> Payload{};
        if (Code || !Asset::FAssetFile::Load(File.mPath, Type, Payload, Error)) {
            Report(File.mPath, Code ? Code.message() : Error);
            continue;
        }

        bool Found{};
        Core::TArray<Core::FString> Values{};
        const auto Remap{[&](const Core::FString& Value, bool Editable) -> Core::FString {
            if (Value.size() == 36 || Value.find('.') != Core::FString::npos) {
                Values.push_back(Value);
            }
            bool Guid{};
            if (!Match(Value, Guid)) {
                return {};
            }
            Found = true;
            if (!Replacing) {
                return {};
            }
            if (!Editable || (Guid && !ReplacementGuid.IsValid())) {
                Report(File.mPath, "참조를 안전하게 교체할 수 없습니다.");
                return {};
            }
            return Guid ? ReplacementGuid.ToString() : ReplacementPath;
        }};
        bool Changed{};
        if (!RewriteAssetReferences(Assets, Type, Payload, Remap, Changed, Error)) {
            Report(File.mPath, Error);
        } else {
            mReferenceCache.insert_or_assign(File.mPath, FReferenceCacheEntry{Time, Size, Type, std::move(Values)});
        }
        if (Found) {
            mContentActionReferences.push_back(File.mPath.lexically_relative(Root));
            if (Replacing && IsContentPathWithin(File.mPath, ReplacementAbsolute)) {
                Report(File.mPath, "대체 에셋이 삭제할 에셋을 사용 중입니다.");
            }
        }
        if (Changed) {
            Edits.push_back({File.mPath, Type, std::move(Payload), Time});
        }
    }

    if (!Replacing || !mDeleteCheckError.empty()) {
        return mDeleteCheckError.empty();
    }

    for (const auto& Edit : Edits) {
        if (mAssetEditors.HasOpenAsset(Edit.mType, [&](const std::filesystem::path& Path) {
            Core::FString PathError{};
            return Core::FilePath::ToLower(Assets.ResolvePath(Path, PathError)) == Core::FilePath::ToLower(Edit.mPath);
        })) {
            Report(Edit.mPath, "에셋 편집기를 닫은 뒤 참조를 교체하세요.");
            return false;
        }
    }

    for (const auto& Edit : Edits) {
        const auto Writable{GetContentActionPath(Edit.mPath)};
        if (Writable.empty() || std::filesystem::last_write_time(Writable, Code) != Edit.mTime || Code) {
            Report(Edit.mPath, "검사 중 참조 파일이 변경됐습니다.");
            return false;
        }
        if (!Asset::FAssetFile::Save(Writable, Edit.mType, Edit.mPayload, Error)) {
            Report(Edit.mPath, Error);
            return false;
        }
        mReferenceCache.erase(Edit.mPath);
        if (Edit.mType == Asset::EAssetType::Material) {
            if (!Assets.ReloadMaterial(Writable, Error)) {
                Report(Edit.mPath, Error);
                return false;
            }
        } else {
            Assets.RetireAssets(Writable);
        }
    }

    if (mDeleteStartupReference && !Project.SetStartupScene(Replacement, Error)) {
        Report(Project.GetFilePath(), Error);
        return false;
    }

    const auto ScenePath{mContext->GetScenePath()};
    if (!Edits.empty() && !ScenePath.empty() && !mContext->LoadScene(ScenePath)) {
        Report(ScenePath, "열린 씬을 다시 열지 못했습니다.");
        return false;
    }

    return true;
}

bool FContentBrowserWidget::DeleteContent(bool Force) {
    const std::filesystem::path Absolute{GetContentActionPath(mContentActionTarget)};

    if (mContentActionError = FindContentActionBlocker(Absolute); !mContentActionError.empty()) {
        return false;
    }

    if (!Force && (!CheckDeleteReferences() || !mContentActionReferences.empty() || mDeleteLiveReference || mDeleteStartupReference)) {
        mContentActionError = "사용 중인 참조를 교체하거나 Force Delete를 선택하세요.";
        return false;
    }

    // 실수로 지워도 되돌릴 수 있도록 휴지통으로 옮깁니다. 경로 목록은 두 개의 NUL로 끝나야 합니다.
    std::wstring From{Absolute.native()};
    From.push_back(L'\0');
    const auto Source{mAssetCatalog->FindTextureSource(mContentActionTarget)};
    if (!Source.empty()) {
        From += GetContentActionPath(Source).native();
        From.push_back(L'\0');
    }

    SHFILEOPSTRUCTW Operation{};
    Operation.hwnd = mPlatform != nullptr ? mPlatform->GetWindowHandle() : nullptr;
    Operation.wFunc = FO_DELETE;
    Operation.pFrom = From.c_str();
    Operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    if (SHFileOperationW(&Operation) != 0 || Operation.fAnyOperationsAborted) {
        mContentActionError = "휴지통으로 옮기지 못했습니다. 다른 프로그램이 파일을 쓰고 있는지 확인하세요.";
        mRefreshContent = true;

        return false;
    }

    mSelectedContentPath.clear();
    if (!mCutContentPath.empty() && IsContentPathWithin(mCutContentPath, mContentActionTarget)) {
        mCutContentPath.clear();
    }

    // 지운 에셋을 참조하던 객체는 살려 두고, 같은 경로에 다시 임포트하면 새 객체를 로드하게 합니다.
    mContext->GetProject()->GetAssets()->RetireAssets(Absolute);
    if (!Source.empty()) {
        const auto SourceAbsolute{GetContentActionPath(Source)};
        mContext->GetProject()->GetAssets()->RetireAssets(SourceAbsolute);
        mAssetEditors.CloseRetired(SourceAbsolute);
    }


    mAssetEditors.CloseRetired(Absolute);
    mRefreshContent = true;

    mContext->AddLog("Moved " + Core::FilePath::ToGenericUtf8(mContentActionTarget) + " to the Recycle Bin");

    return true;
}

void FContentBrowserWidget::DrawContentContextMenu(const std::filesystem::path& PasteDirectory) {
    const bool CanEdit{mContext->CanEditWorld() && !mContentDirectory.has_root_directory()};
    const bool CanEditSelection{CanEdit && !mSelectedContentPath.has_root_directory()};

    if (ImGui::MenuItem("New Folder", nullptr, false, CanEdit)) {
        CreateContentFolder();
    }

    if (ImGui::MenuItem("New Material", nullptr, false, CanEdit)) {
        CreateMaterial();
    }

    if (ImGui::MenuItem("New Blend Space 1D", nullptr, false, CanEdit)) {
        CreateBlendSpace();
    }

    if (ImGui::MenuItem("New Anim Graph", nullptr, false, CanEdit)) {
        CreateAnimGraph();
    }

    if (ImGui::MenuItem("New Particle System", nullptr, false, CanEdit)) {
        CreateParticleSystem();
    }

    if (ImGui::MenuItem("New Lua Script", nullptr, false, CanEdit)) {
        CreateLuaScript();
    }

    if (const auto Type{mAssetTypes.find(mSelectedContentPath)}; !mSelectedContentPath.empty() && !mSelectedContentPath.has_root_directory() && Type != mAssetTypes.end() && Type->second == Asset::EAssetType::SkeletalMesh) {
        ImGui::Separator();

        // 메시로 물리 에셋 파일을 자동 생성해 저장한 뒤 그 파일을 에디터로 연다.
        if (ImGui::MenuItem("Create Physics Asset", nullptr, false, CanEdit)) {
            std::filesystem::path CreatedPath{};
            Core::FString CreateError{};

            if (PhysicsAssetEditing::CreatePhysicsAssetFile(*mContext->GetProject()->GetAssets(), mSelectedContentPath, CreatedPath, CreateError)) {
                mRefreshContent = true;
                mSelectedContentPath = CreatedPath;
                mAssetEditors.OpenContentAsset(CreatedPath, Asset::EAssetType::PhysicsAsset);
            } else {
                mContext->AddLog(CreateError.empty() ? "Cannot create the physics asset" : CreateError);
            }
        }

        if (ImGui::MenuItem("Physics Asset Editor")) {
            mAssetEditors.OpenContentAsset(mSelectedContentPath, Asset::EAssetType::PhysicsAsset);
        }
    }

    if (!mSelectedContentPath.empty()) {
        ImGui::Separator();

        if (ImGui::MenuItem("Rename...", "F2", false, CanEditSelection)) {
            BeginContentAction(EContentAction::Rename);
        }

        if (ImGui::MenuItem("Cut", "Ctrl+X", false, CanCutContent(mSelectedContentPath))) {
            CutContent(mSelectedContentPath);
        }

        if (ImGui::MenuItem("Delete", "Del", false, CanEditSelection)) {
            BeginContentAction(EContentAction::Delete);
        }
    }

    if (ImGui::MenuItem("Paste Here", "Ctrl+V", false, CanPasteContent(PasteDirectory))) {
        PasteContent(PasteDirectory);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Import...", nullptr, false, CanEdit)) {
        mImports.ImportAsset();
    }

    ImGui::Separator();

    const auto FixUpDirectory{mContentFolders.contains(mSelectedContentPath) ? mSelectedContentPath : mContentDirectory};
    if (ImGui::MenuItem("Fix Up Redirectors in Folder", nullptr, false, CanEdit && !FixUpDirectory.has_root_directory())) {
        mPendingFixUpDirectory = FixUpDirectory;
    }

    if (ImGui::MenuItem("Show in Explorer")) {
        ShowContentInExplorer();
    }

    if (ImGui::MenuItem("Refresh")) {
        mRefreshContent = true;
    }
}

void FContentBrowserWidget::FixUpRedirectors(const std::filesystem::path& Directory) {
    if (!mContext->CanEditWorld()) {
        return;
    }

    mContentError.clear();
    auto& Project{*mContext->GetProject()};
    auto& Assets{*Project.GetAssets()};
    const auto& Root{Assets.GetContentRoot()};
    const auto Scope{GetContentActionPath(Directory.empty() ? std::filesystem::path{L"."} : Directory)};
    if (Scope.empty()) {
        return;
    }

    struct FFixUpFile {
        std::filesystem::path mPath{};
        std::optional<Asset::EAssetType> mType{};
    };

    Core::TArray<FFixUpFile> Files{};
    Core::TArray<std::filesystem::path> Selected{};
    Core::FString Error{};
    std::error_code Code{};
    std::filesystem::recursive_directory_iterator Iterator{Root, Code};
    const std::filesystem::recursive_directory_iterator End{};

    for (; !Code && Iterator != End; Iterator.increment(Code)) {
        if (Iterator->is_symlink(Code)) {
            Iterator.disable_recursion_pending();
        } else if (!Code && Iterator->is_regular_file(Code)) {
            const auto& Path{Iterator->path()};
            const auto Extension{Core::FilePath::ToLowerExtension(Path)};
            if (Extension == L".masset") {
                Asset::EAssetType Type{};
                const bool Valid{Asset::FAssetFile::GetType(Path, Type, Error)};
                Files.push_back({Path, Valid ? std::optional<Asset::EAssetType>{Type} : std::nullopt});
                if (Valid && Type == Asset::EAssetType::Redirector && IsContentPathWithin(Path, Scope)) {
                    Selected.push_back(Path);
                }
            } else if (Extension == L".lua" || Extension == L".hlsl" || Extension == L".hlsli") {
                Files.push_back({Path, {}});
            }
        }
    }

    if (Code) {
        mContentError = "콘텐츠를 읽지 못했습니다. " + Code.message();
        return;
    }

    if (Selected.empty()) {
        mContentError = "이 폴더에 정리할 Redirector가 없습니다.";
        return;
    }

    if (mContext->IsSceneDirty() || mContext->IsSceneEditActive() || mAssetEditors.HasUnsavedSource([](const std::filesystem::path&) {
        return true;
    })) {
        mContentError = "씬과 소스 파일의 변경 사항을 저장한 뒤 Redirector를 정리하세요.";
        return;
    }

    const auto IsSelected{[&](const std::filesystem::path& Path) {
        const auto Key{Core::FilePath::ToLower(Path)};
        return std::ranges::any_of(Selected, [&](const std::filesystem::path& Item) {
            return Core::FilePath::ToLower(Item) == Key;
        });
    }};
    const auto ReportFailure{[&](const std::filesystem::path& Path, const Core::FString& Reason) {
        mContentError = "Redirector 정리를 완료하지 못했습니다. " + Core::FilePath::ToGenericUtf8(Path.lexically_relative(Root)) + " · " + Reason;
        mContext->AddLog(mContentError);
    }};

    Core::TArray<Core::FString> SourceNeedles{};
    for (const auto& Marker : Selected) {
        if (Assets.ResolvePath(Marker, Error).empty()) {
            ReportFailure(Marker, Error);
            return;
        }

        auto Alias{Marker.lexically_relative(Root)};
        if (Alias.filename() == L".redirector.masset") {
            SourceNeedles.push_back(NormalizeReferenceText(Core::FilePath::ToGenericUtf8(Alias.parent_path()) + '/'));
        } else {
            SourceNeedles.push_back(NormalizeReferenceText(Core::FilePath::ToGenericUtf8(Alias)));
            Alias.replace_extension();
            if (Asset::FAssetManager::IsTextureSource(Alias)) {
                SourceNeedles.push_back(NormalizeReferenceText(Core::FilePath::ToGenericUtf8(Alias)));
            }
        }
    }

    Core::FString ReferenceError{};
    const auto Remap{[&](const Core::FString& Value, bool Editable) -> Core::FString {
        if (Value.empty() || Value.find('.') == Core::FString::npos || Value.find('\0') != Core::FString::npos) {
            return {};
        }

        std::filesystem::path Path{};
        try {
            Path = Core::FilePath::FromUtf8(Value);
        } catch (const std::filesystem::filesystem_error&) {
            return {};
        }

        if (!Path.has_extension()) {
            return {};
        }

        Core::TArray<std::filesystem::path> Trace{};
        Core::FString ResolveError{};
        const auto Destination{Assets.ResolveRedirectedPath(Path, Trace, ResolveError)};
        if (!std::ranges::any_of(Trace, IsSelected)) {
            return {};
        }

        if (!Editable || Destination.empty()) {
            ReferenceError = Value + " 경로를 안전하게 갱신할 수 없습니다.";
            return {};
        }

        const auto Relative{Assets.GetRelativePath(Destination, ResolveError)};
        if (Relative.empty()) {
            ReferenceError = Value + " 경로를 안전하게 갱신할 수 없습니다.";
        }
        return Relative;
    }};

    const Core::FString Startup{Remap(Project.GetStartupScene(), true)};
    if (!ReferenceError.empty()) {
        ReportFailure(Project.GetFilePath(), ReferenceError);
        return;
    }

    std::size_t Updated{};
    for (const auto& File : Files) {
        if (!File.mType.has_value()) {
            if (Core::FilePath::ToLowerExtension(File.mPath) == L".masset") {
                ReportFailure(File.mPath, "에셋 형식을 읽을 수 없습니다.");
                return;
            }

            std::ifstream Input{File.mPath, std::ios::binary};
            if (!Input) {
                ReportFailure(File.mPath, "소스 파일을 읽을 수 없습니다.");
                return;
            }
            const auto Text{NormalizeReferenceText(Core::FString{std::istreambuf_iterator<char>{Input}, std::istreambuf_iterator<char>{}})};
            if (std::ranges::any_of(SourceNeedles, [&](const Core::FString& Needle) {
                return Text.find(Needle) != Core::FString::npos;
            })) {
                ReportFailure(File.mPath, "소스에 이전 경로가 남아 있습니다.");
                return;
            }
            continue;
        }

        const auto Type{*File.mType};
        if (!AssetHasReferenceFields(Type)) {
            continue;
        }

        const auto SavedTime{std::filesystem::last_write_time(File.mPath, Code)};
        if (Code) {
            ReportFailure(File.mPath, Code.message());
            return;
        }

        Core::TArray<Uint8> Payload{};
        bool Changed{};
        if (Type == Asset::EAssetType::Redirector) {
            Core::TArray<std::filesystem::path> Trace{};
            const auto Destination{Assets.ResolveRedirectedPath(File.mPath, Trace, Error)};
            if (Trace.size() < 2 || !std::any_of(Trace.begin() + 1, Trace.end(), IsSelected)) {
                continue;
            }
            if (Destination.empty()) {
                ReportFailure(File.mPath, Error);
                return;
            }
            const auto Target{Assets.GetRelativePath(Destination, Error)};
            Payload.assign(Target.begin(), Target.end());
            Changed = true;
        } else {
            if (!Asset::FAssetFile::Load(File.mPath, Type, Payload, Error)) {
                ReportFailure(File.mPath, Error);
                return;
            }

            const bool Valid{RewriteAssetReferences(Assets, Type, Payload, Remap, Changed, Error)};
            if (!Valid || !ReferenceError.empty()) {
                ReportFailure(File.mPath, ReferenceError.empty() ? Error : ReferenceError);
                return;
            }
        }

        if (!Changed) {
            continue;
        }

        if (mAssetEditors.HasOpenAsset(Type, [&](const std::filesystem::path& Path) {
            Core::FString PathError{};
            return Core::FilePath::ToLower(Assets.ResolvePath(Path, PathError)) == Core::FilePath::ToLower(File.mPath);
        })) {
            ReportFailure(File.mPath, "에셋 편집기를 닫은 뒤 다시 시도하세요.");
            return;
        }

        if (std::filesystem::last_write_time(File.mPath, Code) != SavedTime || Code) {
            ReportFailure(File.mPath, "검사 중 파일이 변경됐습니다.");
            return;
        }

        const auto Writable{GetContentActionPath(File.mPath)};
        if (Writable.empty() || !Asset::FAssetFile::Save(Writable, Type, Payload, Error)) {
            ReportFailure(File.mPath, Error);
            return;
        }
        mReferenceCache.erase(File.mPath);

        if (Type == Asset::EAssetType::Material) {
            if (!Assets.ReloadMaterial(Writable, Error)) {
                ReportFailure(File.mPath, Error);
                return;
            }
        } else if (Type == Asset::EAssetType::Shader) {
            auto* Shader{Assets.LoadShader(Writable, Error)};
            const auto Source{Shader != nullptr ? Remap(Shader->GetSourcePath(), true) : Core::FString{}};
            if (Shader == nullptr || !ReferenceError.empty() || (!Source.empty() && !Shader->SetSourcePath(Source))) {
                ReportFailure(File.mPath, ReferenceError.empty() ? Error : ReferenceError);
                return;
            }
        } else {
            Assets.RetireAssets(Writable);
        }
        ++Updated;
    }

    if (!Startup.empty() && !Project.SetStartupScene(Core::FilePath::FromUtf8(Startup), Error)) {
        ReportFailure(Project.GetFilePath(), Error);
        return;
    }

    const auto ScenePath{mContext->GetScenePath()};
    if (Updated != 0 && !ScenePath.empty() && !mContext->LoadScene(ScenePath)) {
        ReportFailure(ScenePath, "편집 중인 씬을 다시 열지 못했습니다.");
        return;
    }

    std::size_t Removed{};
    for (const auto& Marker : Selected) {
        const auto Writable{GetContentActionPath(Marker)};
        Asset::EAssetType Type{};
        if (Writable.empty() || !Asset::FAssetFile::GetType(Writable, Type, Error) || Type != Asset::EAssetType::Redirector) {
            ReportFailure(Marker, "Redirector가 변경됐습니다.");
            return;
        }

        const bool EmptyRedirectorDirectory{Assets.IsRedirectorDirectory(Writable.parent_path())};
        if (!std::filesystem::remove(Writable, Code) || Code) {
            ReportFailure(Marker, Code ? Code.message() : "Redirector를 찾을 수 없습니다.");
            return;
        }

        if (EmptyRedirectorDirectory) {
            std::filesystem::remove(Writable.parent_path(), Code);
        }
        ++Removed;
    }

    mRefreshContent = true;
    mContext->AddLog(std::format("Fix Up Redirectors: updated {} files, removed {} redirectors", Updated + static_cast<std::size_t>(!Startup.empty()), Removed));
}

void FContentBrowserWidget::CreateContentFolder() {
    const std::filesystem::path Root{mContext->GetProject()->GetAssets()->GetContentRoot()};
    std::filesystem::path Path{mContentDirectory / "NewFolder"};
    std::error_code Code{};

    for (Index_t Index{1}; std::filesystem::exists(Root / Path, Code) && !Code; ++Index) {
        Path = mContentDirectory / ("NewFolder_" + std::to_string(Index));
    }

    if (Code || !std::filesystem::create_directory(Root / Path, Code)) {
        mContext->AddLog("Cannot create the folder " + Core::FilePath::ToGenericUtf8(Path));

        return;
    }

    mRefreshContent = true;
    mSelectedContentPath = Path;
}

void FContentBrowserWidget::ShowContentInExplorer() const {
    const auto& Path{mSelectedContentPath.empty() ? mContentDirectory : mSelectedContentPath};
    Core::FString Error{};
    auto Target{mContext->GetProject()->GetAssets()->ResolvePath(Path.empty() ? std::filesystem::path{L"."} : Path, Error)};
    std::error_code Code{};

    if (Target.empty()) {
        return;
    }

    Target.make_preferred();

    // 파일은 상위 폴더를 열고 그 파일을 선택하며, 폴더는 그 폴더를 엽니다.
    const std::wstring Arguments{std::filesystem::is_directory(Target, Code) ? L"\"" + Target.native() + L"\"" : L"/select,\"" + Target.native() + L"\""};

    ShellExecuteW(nullptr, L"open", L"explorer.exe", Arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

void FContentBrowserWidget::RefreshContent() {
    std::erase_if(mReferenceCache, [](const auto& Entry) {
        std::error_code Code{};
        return !std::filesystem::exists(Entry.first, Code) && !Code;
    });
    mAssetPaths.clear();
    mAssetTypes.clear();
    mContentFolders.clear();
    mContentFolders[{}] = {};
    mContentError.clear();

    if (mAssetCatalog == nullptr) {
        return;
    }

    mAssetCatalog->Refresh(mContentError);
    if (mContext->CanEditWorld()) {
        Core::FString ImportError{};
        if (mAssetCatalog->ImportLooseTextures(ImportError)) {
            mAssetCatalog->Refresh(mContentError);
        }

        if (mContentError.empty()) {
            mContentError = std::move(ImportError);
        }
    }

    for (const auto& Directory : mAssetCatalog->GetDirectories()) {
        const auto Parent{Directory.parent_path()};

        if (Parent != Directory.root_path() || !Directory.has_root_directory()) {
            mContentFolders[Parent].push_back(Directory);
        }

        mContentFolders.try_emplace(Directory);
    }

    for (const auto& Entry : mAssetCatalog->GetEntries()) {
        if (mAssetCatalog->IsImportedTextureSource(Entry.GetPath())) {
            continue;
        }

        mAssetPaths.push_back(Entry.GetPath());

        if (Entry.GetType().has_value()) {
            mAssetTypes.emplace(Entry.GetPath(), *Entry.GetType());
        }
    }

    for (auto& Entry : mContentFolders) {
        std::sort(Entry.second.begin(), Entry.second.end());
        Entry.second.erase(std::unique(Entry.second.begin(), Entry.second.end()), Entry.second.end());
    }

    while (!mContentDirectory.empty() && !mContentFolders.contains(mContentDirectory)) {
        mContentDirectory = mContentDirectory.parent_path();
    }

    mThumbnails->Invalidate();
    if (mCatalogChanged) {
        mCatalogChanged();
    }

    mRefreshContent = false;
    mRevealContentDirectory = true;
}

void FContentBrowserWidget::NavigateContent(const std::filesystem::path& Directory) {
    if (mContentFolders.contains(Directory)) {
        mContentDirectory = Directory;
        mSelectedContentPath.clear();
        mRevealContentDirectory = true;
    }
}

void FContentBrowserWidget::DrawContentTree(const std::filesystem::path& Directory) {
    const auto Entry{mContentFolders.find(Directory)};

    if (Entry == mContentFolders.end()) {
        return;
    }

    const Core::FString Label{Directory.empty() ? "Content" : Core::FilePath::ToUtf8(Directory.filename())};
    const Core::FString Id{Core::FilePath::ToUtf8(Directory)};
    ImGuiTreeNodeFlags Flags{ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth};

    if (Directory == mContentDirectory) {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (Entry->second.empty()) {
        Flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (Directory.empty()) {
        Flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    const auto Relative{mContentDirectory.lexically_relative(Directory)};

    if (mRevealContentDirectory && (Directory.empty() || Directory == mContentDirectory || (!Relative.empty() && *Relative.begin() != L".."))) {
        ImGui::SetNextItemOpen(true);
    }

    ImGui::PushID(Id.c_str());

    const bool Cut{!mCutContentPath.empty() && Directory == mCutContentPath};
    if (Cut) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * CutContentOpacity);
    }
    const bool Open{ImGui::TreeNodeEx(Label.c_str(), Flags)};
    if (Cut) {
        ImGui::PopStyleVar();
    }

    const ImVec2 Minimum{ImGui::GetItemRectMin()};
    const ImVec2 Maximum{ImGui::GetItemRectMax()};

    mImports.RegisterContentDropTarget(Directory, {Minimum.x, Minimum.y, Maximum.x, Maximum.y});
    AcceptContentMove(Directory);

    if (ImGui::IsItemClicked()) {
        NavigateContent(Directory);
    }

    if (!Directory.has_root_directory() && ImGui::BeginPopupContextItem("##FolderRedirectors")) {
        if (ImGui::MenuItem("Fix Up Redirectors in Folder", nullptr, false, mContext->CanEditWorld())) {
            mPendingFixUpDirectory = Directory;
        }
        if (!Directory.empty() && ImGui::MenuItem("Cut", "Ctrl+X", false, CanCutContent(Directory))) {
            CutContent(Directory);
        }
        if (ImGui::MenuItem("Paste Here", "Ctrl+V", false, CanPasteContent(Directory))) {
            PasteContent(Directory);
        }
        ImGui::EndPopup();
    }

    if (Open) {
        for (const auto& Child : Entry->second) {
            DrawContentTree(Child);
        }

        ImGui::TreePop();
    }

    ImGui::PopID();
}

void FContentBrowserWidget::DrawContentBreadcrumbs() {
    ImGui::PushID("Content Path");

    const bool Engine{mContentDirectory.has_root_directory()};
    const std::filesystem::path Root{Engine ? std::filesystem::path{L"/Engine"} : std::filesystem::path{}};

    ImGui::BeginDisabled(mContentDirectory == Root);

    if (EditorStyle::SmallButton("Up")) {
        NavigateContent(mContentDirectory.parent_path());
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if (EditorStyle::SmallButton(Engine ? "Engine" : "Content")) {
        NavigateContent(Root);
    }

    if (Engine && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Engine content is read-only");
    }

    const auto Current{Engine ? mContentDirectory.lexically_relative(Root) : mContentDirectory};
    auto Path{Root};

    for (const auto& Part : Current) {
        if (Part == L".") {
            continue;
        }

        Path /= Part;

        const Core::FString Label{Core::FilePath::ToUtf8(Part)};
        const float Right{ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x};

        if (Right - ImGui::GetItemRectMax().x > ImGui::CalcTextSize(Label.c_str()).x + 48.0f) {
            ImGui::SameLine();
        }

        ImGui::TextUnformatted("/");
        ImGui::SameLine();

        const Core::FString Id{Core::FilePath::ToUtf8(Path)};

        ImGui::PushID(Id.c_str());

        if (EditorStyle::SmallButton(Label.c_str())) {
            NavigateContent(Path);
        }

        ImGui::PopID();
    }

    ImGui::PopID();
}

void FContentBrowserWidget::DrawContentEntries() {
    const ImGuiTextFilter Search{mContentSearch.data()};
    const bool Recursive{mContentFilter.has_value() || mContentSearch[0] != '\0'};
    Core::TArray<std::filesystem::path> Folders{};
    Core::TArray<std::filesystem::path> Assets{};
    const auto Folder{mContentFolders.find(mContentDirectory)};

    if (!Recursive && Folder != mContentFolders.end()) {
        Folders = Folder->second;
    }

    for (const auto& Path : mAssetPaths) {
        const auto Type{mAssetTypes.find(Path)};
        const auto Relative{Path.lexically_relative(mContentDirectory)};
        const bool InFolder{Recursive ? (mContentDirectory.empty() && !Path.has_root_directory()) || (!Relative.empty() && *Relative.begin() != L"..") : Path.parent_path() == mContentDirectory};

        const bool MatchesFilter{!mContentFilter.has_value() || (Type != mAssetTypes.end() ? Type->second == *mContentFilter : *mContentFilter == Asset::EAssetType::Shader && IsShaderSource(Path))};

        if (!InFolder || !MatchesFilter || !Search.PassFilter(Core::FilePath::ToGenericUtf8(Path).c_str())) {
            continue;
        }

        Assets.push_back(Path);
    }

    const int Count{static_cast<int>(Folders.size() + Assets.size())};

    if (Count == 0) {
        ImGui::TextDisabled("No assets match this folder or search.");

        return;
    }

    const float Scale{mUIScale.GetScale()};
    const ImGuiStyle& Style{ImGui::GetStyle()};
    const ImVec2 Available{ImGui::GetContentRegionAvail()};
    const ImVec2 StandardTile{FAssetTileWidget::Measure(124.0f * Scale, Scale)};
    const float RowPadding{Style.CellPadding.y * 2.0f + 4.0f * Scale};
    const float ColumnPadding{Style.CellPadding.x * 2.0f};
    const float PreferredWidth{Available.y < StandardTile.y + RowPadding ? 104.0f * Scale : StandardTile.x};
    const float TileWidth{std::min(PreferredWidth, std::max(32.0f * Scale, Available.x - ColumnPadding))};
    const ImVec2 TileSize{FAssetTileWidget::Measure(TileWidth, Scale)};
    const float RowHeight{std::ceil(TileSize.y + RowPadding)};
    const int Columns{std::clamp(static_cast<int>(Available.x / (TileSize.x + ColumnPadding)), 1, 12)};
    bool Navigate{};
    std::filesystem::path Requested{};

    if (ImGui::BeginTable("ContentGrid", Columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
        for (Index_t Column{}; Column < Columns; ++Column) {
            ImGui::TableSetupColumn("##AssetColumn", ImGuiTableColumnFlags_WidthFixed, TileSize.x);
        }

        ImGuiListClipper Clipper{};
        Clipper.Begin((Count + Columns - 1) / Columns, RowHeight);

        while (Clipper.Step()) {
            for (Index_t Row{static_cast<Index_t>(Clipper.DisplayStart)}; Row < Clipper.DisplayEnd; ++Row) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, RowHeight);

                for (Index_t Column{}; Column < Columns; ++Column) {
                    const Index_t Index{static_cast<Index_t>(Row * Columns + Column)};

                    if (Index >= Count) {
                        break;
                    }

                    if (!ImGui::TableSetColumnIndex(static_cast<int>(Column))) {
                        continue;
                    }

                    const bool Directory{Index < Folders.size()};
                    const auto& Path{Directory ? Folders[Index] : Assets[Index - Folders.size()]};
                    const auto Type{mAssetTypes.find(Path)};
                    const bool HasAssetType{!Directory && Type != mAssetTypes.end()};
                    const auto Kind{HasAssetType ? FAssetTileWidget::EContentKind::File : GetContentKind(Path, Directory)};
                    const bool LuaScript{Kind == FAssetTileWidget::EContentKind::LuaScript};
                    const bool ShaderSource{Kind == FAssetTileWidget::EContentKind::ShaderSource};
                    const bool SupportsThumbnail{HasAssetType && FAssetThumbnailCache::Supports(Type->second)};
                    void* Thumbnail{SupportsThumbnail ? mThumbnails->GetThumbnail(Path, Type->second) : nullptr};
                    const Core::FString Actual{Core::FilePath::ToGenericUtf8(Path)};
                    const Core::FString Name{Core::FilePath::ToUtf8(Directory || ShaderSource ? Path.filename() : Path.stem())};
                    auto TileStyle{HasAssetType ? FAssetTileWidget::GetStyle(Type->second) : FAssetTileWidget::GetStyle(Kind)};

                    ImGui::PushID(Actual.c_str());

                    const bool Cut{Path == mCutContentPath};
                    if (Cut) {
                        TileStyle.mAccent = IM_COL32(162, 162, 162, 255);
                        TileStyle.mIconTint = IM_COL32(190, 190, 190, 255);
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * CutContentOpacity);
                    }
                    const bool Selected{mSelectedContentPath == Path};
                    const bool Pressed{Directory ? DrawFolderEntry(mAssetTile, TileStyle, Name, TileSize, Scale, Selected) : mAssetTile.Draw("##AssetTile", Name, TileStyle, Thumbnail, TileSize, Scale, Selected)};
                    if (Cut) {
                        ImGui::PopStyleVar();
                    }

                    if (Pressed) {
                        mSelectedContentPath = Path;

                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            if (Directory) {
                                Requested = Path;
                                Navigate = true;
                            } else if (ShaderSource) {
                                mAssetEditors.OpenShaderSource(Path);
                            } else if (Path.has_root_directory()) {
                                mContext->AddLog("Engine content is read-only");
                            } else if (LuaScript) {
                                mAssetEditors.OpenLuaScript(Path);
                            } else if (Type != mAssetTypes.end() && mContext->CanEditWorld()) {
                                mAssetEditors.OpenContentAsset(Path, Type->second);
                            }
                        }
                    }

                    // 우클릭 메뉴가 이 항목을 대상으로 하도록 먼저 선택합니다.
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        mSelectedContentPath = Path;
                        mContextPasteDirectory = Directory ? Path : mContentDirectory;
                    }

                    const bool Hovered{ImGui::IsItemHovered()};

                    if (Directory) {
                        const ImVec2 Minimum{ImGui::GetItemRectMin()};
                        const ImVec2 Maximum{ImGui::GetItemRectMax()};

                        mImports.RegisterContentDropTarget(Path, {Minimum.x, Minimum.y, Maximum.x, Maximum.y});
                        AcceptContentMove(Path);
                    }

                    // 애니메이션 에셋은 Details의 해당 필드에 놓을 수 있도록 끌기를 허용합니다.
                    const bool AnimSequence{!Directory && Type != mAssetTypes.end() && Type->second == Asset::EAssetType::AnimSequence};
                    const bool BlendSpace{!Directory && Type != mAssetTypes.end() && Type->second == Asset::EAssetType::BlendSpace};
                    const bool AnimGraph{!Directory && Type != mAssetTypes.end() && Type->second == Asset::EAssetType::AnimGraph};

                    const bool Shader{!Directory && Type != mAssetTypes.end() && Type->second == Asset::EAssetType::Shader};

                    const bool HasViewer{HasAssetType && Viewer::FAssetViewer::Supports(Type->second)};

                    const bool Assignable{LuaScript || HasViewer || AnimSequence || BlendSpace || AnimGraph || Shader};
                    if ((Assignable || (Directory && !Path.has_root_directory()) || (HasAssetType && Core::FilePath::ToLowerExtension(Path) == L".masset")) && mContext->CanEditWorld() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
                        const char* Payload{!Assignable ? ContentMovePayload : LuaScript ? Editor::EditorAssetDragDrop::LuaScriptPayload : Shader ? Editor::EditorAssetDragDrop::ShaderPayload : AnimSequence ? Editor::EditorAssetDragDrop::AnimSequencePayload : BlendSpace ? Editor::EditorAssetDragDrop::BlendSpacePayload : AnimGraph ? Editor::EditorAssetDragDrop::AnimGraphPayload : Type->second == Asset::EAssetType::Mesh || Type->second == Asset::EAssetType::SkeletalMesh ? Editor::EditorAssetDragDrop::MeshPayload : Type->second == Asset::EAssetType::Skeleton ? Editor::EditorAssetDragDrop::SkeletonPayload : Type->second == Asset::EAssetType::Material ? Editor::EditorAssetDragDrop::MaterialPayload : Editor::EditorAssetDragDrop::TexturePayload};

                        ImGui::SetDragDropPayload(Payload, Actual.c_str(), Actual.size() + 1);
                        mAssetDragSourceId = ImGui::GetDragDropPayload()->SourceId;
                        ImGui::EndDragDropSource();
                    }

                    if (Hovered && ImGui::BeginTooltip()) {
                        ImGui::TextUnformatted(Actual.c_str());
                        ImGui::TextDisabled("%s", TileStyle.mDescription);
                        ImGui::EndTooltip();
                    }

                    ImGui::PopID();
                }
            }
        }

        ImGui::EndTable();
    }

    if (Navigate) {
        NavigateContent(Requested);
    }
}

void FContentBrowserWidget::CreateMaterial() {
    mAssetEditors.ConfirmClose([this, Directory = mContentDirectory]() {
        CreateMaterialIn(Directory);
    });
}

void FContentBrowserWidget::CreateMaterialIn(const std::filesystem::path& Directory) {
    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};
    std::filesystem::path Path{Directory / "Material.masset"};
    std::error_code Code{};

    for (Index_t Index{1}; std::filesystem::exists(Assets->GetContentRoot() / Path, Code) && !Code; ++Index) {
        Path = Directory / ("Material_" + std::to_string(Index) + ".masset");
    }

    auto Material{CoreUObject::NewObject<Asset::UMaterial>()};
    Material->SetName(Core::FilePath::ToUtf8(Path.stem()));

    if (Code || !Assets->SaveMaterial(Path, *Material, mContentError)) {
        mContext->AddLog(mContentError.empty() ? "Cannot create the material" : mContentError);

        return;
    }

    mRefreshContent = true;
    mSelectedContentPath = Path;

    mAssetEditors.OpenContentAsset(Path, Asset::EAssetType::Material);
}

void FContentBrowserWidget::CreateAnimGraph() {
    Core::FString Error{};
    std::filesystem::path Path{};

    if (mContext->GetProject()->GetAssets()->CreateAnimGraph(mContentDirectory, "AnimGraph", Path, Error) == nullptr) {
        mContext->AddLog(Error.empty() ? "Cannot create the anim graph" : Error);

        return;
    }

    mRefreshContent = true;
    mSelectedContentPath = Path;

    mAssetEditors.OpenContentAsset(Path, Asset::EAssetType::AnimGraph);
}

void FContentBrowserWidget::CreateBlendSpace() {
    Core::FString Error{};
    std::filesystem::path Path{};

    if (mContext->GetProject()->GetAssets()->CreateBlendSpace1D(mContentDirectory, "BlendSpace", Path, Error) == nullptr) {
        mContext->AddLog(Error.empty() ? "Cannot create the blend space" : Error);

        return;
    }

    mRefreshContent = true;
    mSelectedContentPath = Path;

    mAssetEditors.OpenContentAsset(Path, Asset::EAssetType::BlendSpace);
}

void FContentBrowserWidget::CreateParticleSystem() {
    Core::FString Error{};
    std::filesystem::path Path{};

    if (Engine::CreateParticleSystem(*mContext->GetProject()->GetAssets(), mContentDirectory, "ParticleSystem", Path, Error) == nullptr) {
        mContext->AddLog(Error.empty() ? "Cannot create the particle system" : Error);

        return;
    }

    mRefreshContent = true;
    mSelectedContentPath = Path;
}

void FContentBrowserWidget::CreateLuaScript() {
    auto Template{GetContentActionPath(L"Scripts/template.lua")};
    std::error_code Code{};

    if (!std::filesystem::is_regular_file(Template, Code)) {
        Core::TStaticArray<wchar_t, 32768> Executable{};
        const DWORD Length{GetModuleFileNameW(nullptr, Executable.data(), static_cast<DWORD>(Executable.size()))};

        if (Length > 0 && Length < Executable.size()) {
            Template = std::filesystem::path{Executable.data()}.parent_path() / L"Templates/Project/Content/Scripts/template.lua";
        }
    }

    auto Path{mContentDirectory / L"NewScript.lua"};

    for (Index_t Index{1}; !std::filesystem::copy_file(Template, GetContentActionPath(Path), std::filesystem::copy_options::none, Code); ++Index) {
        if (Code != std::errc::file_exists) {
            mContext->AddLog("Could not create Lua script: " + Code.message());

            return;
        }

        Path = mContentDirectory / (L"NewScript_" + std::to_wstring(Index) + L".lua");
    }

    mRefreshContent = true;
    mSelectedContentPath = Path;
}
