/**
 * @file FAssetImportController.cpp
 * @brief 파일 선택과 드롭을 가져오기 옵션·대상 디렉터리·완료 통지에 연결합니다.
 * @ingroup Editor
 */

#include "FAssetImportController.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/FilePath.h"
#include "Editor/FEditorContext.h"
#include "Engine/FProject.h"
#include "Asset/Import/IMeshImporter.h"
#include "Platform/Window.h"
#include "Core/Types.h"
#include <commdlg.h>
#include <shellapi.h>
#include <imgui_internal.h>
#include <algorithm>
#include <utility>

using namespace Core::Literals;

using namespace Editor;

/**
 * @brief FAssetImportController 구현에 한정되는 내부 형식·상수·도우미 함수입니다.
 */
namespace {
    /**
     * @brief 콘텐츠 드롭 대상 조회에 사용하는 콘텐츠 브라우저 창 이름입니다.
     */
    constexpr const char* ContentBrowserName{"      Content Browser###Content"};
}

FAssetImportController::FAssetImportController(FEditorContext* Context, Platform::FWindowsApplication* Platform)
	: mContext{Context},
	  mPlatform{Platform} {
}

FAssetImportController::~FAssetImportController() = default;

void FAssetImportController::SetDirectoryProvider(std::function<std::filesystem::path()> Provider) {
    mGetDirectory = std::move(Provider);
}

void FAssetImportController::SetImportedHandler(std::function<void(const std::filesystem::path&, Uint32, const Core::FString&)> Handler) {
    mImported = std::move(Handler);
}

void FAssetImportController::BeginFrame() {
    ImportDroppedFiles();
    mContentDropDirectories.clear();
    mContentDropBounds.clear();
}

void FAssetImportController::ImportAsset() {
    const auto Directory{mGetDirectory ? mGetDirectory() : std::filesystem::path{}};

    if (Core::FilePath::GetContentMount(Directory) == Core::FilePath::EContentMount::Engine) {
        mContext->AddLog("Engine content is read-only");

        return;
    }

    Core::TStaticArray<wchar_t, 32768> Buffer{};
    OPENFILENAMEW Dialog{};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = mPlatform->GetWindowHandle();
    Dialog.lpstrTitle = L"Import Asset";
    Dialog.lpstrFilter = L"Supported Assets\0*.obj;*.fbx;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.dds;*.hdr;*.hlsl\0";
    Dialog.lpstrFile = Buffer.data();
    Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
    Dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&Dialog)) {
        RequestImport({std::filesystem::path{Buffer.data()}}, Directory);
    }
}

void FAssetImportController::RequestImport(Core::TArray<std::filesystem::path> Sources, const std::filesystem::path& Directory) {
    if (!mContext->CanEditWorld() || Sources.empty()) {
        return;
    }

    if (Core::FilePath::GetContentMount(Directory) == Core::FilePath::EContentMount::Engine) {
        mContext->AddLog("Engine content is read-only");

        return;
    }

    const bool HasMesh{std::any_of(Sources.begin(), Sources.end(), [](const std::filesystem::path& Source) {
        return Asset::IMeshImporter::IsSupported(Source);
    })};

    // 이미지 원본만 있으면 받을 옵션이 없으므로 바로 가져옵니다.
    if (!HasMesh) {
        ImportFiles(Sources, Directory, mImportOption);

        return;
    }

    mPendingImports.push_back(std::move(Sources));
    mPendingImportDirectories.push_back(Directory);
}

void FAssetImportController::DrawImportOptions() {
    static constexpr const char* PopupName{"Import Options"};

    if (mPendingImports.empty()) {
        return;
    }

    if (!ImGui::IsPopupOpen(PopupName)) {
        ImGui::OpenPopup(PopupName);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});

    if (!ImGui::BeginPopupModal(PopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const auto& Sources{mPendingImports.front()};
    const auto Finish{[this]() {
        mPendingImports.erase(mPendingImports.begin());
        mPendingImportDirectories.erase(mPendingImportDirectories.begin());
        ImGui::CloseCurrentPopup();
    }};

    constexpr std::size_t ListedSources{8};

    ImGui::TextUnformatted(Sources.size() == 1 ? "Importing 1 file:" : ("Importing " + std::to_string(Sources.size()) + " files:").c_str());

    for (Index_t Index{}; Index < std::min(Sources.size(), ListedSources); ++Index) {
        ImGui::BulletText("%s", Core::FilePath::ToUtf8(Sources[Index].filename()).c_str());
    }

    if (Sources.size() > ListedSources) {
        ImGui::TextDisabled("... and %zu more", Sources.size() - ListedSources);
    }

    if (mPendingImports.size() > 1) {
        ImGui::TextDisabled("%zu more import batches are waiting.", mPendingImports.size() - 1);
    }

    ImGui::SeparatorText("Transform");
    ImGui::Checkbox("Force Front X Axis", &mImportOption.mForceFrontXAxis);
    ImGui::SetItemTooltip("원본의 앞쪽 축이 +X를 향하도록 맞춥니다.\n끄면 언리얼 FBX 임포터와 같이 앞쪽이 +Y를 향합니다.");
    ImGui::Checkbox("Convert Scene Unit", &mImportOption.mConvertSceneUnit);
    ImGui::SetItemTooltip("원본(FBX)에 기록된 길이 단위를 미터로 변환합니다.");

    Core::TStaticArray<float, 3> Translation{mImportOption.mImportTranslation.x, mImportOption.mImportTranslation.y, mImportOption.mImportTranslation.z};
    Core::TStaticArray<float, 3> Rotation{mImportOption.mImportRotation.pitch, mImportOption.mImportRotation.yaw, mImportOption.mImportRotation.roll};

    if (ImGui::DragFloat3("Import Translation", Translation.data(), 0.01f, 0.0f, 0.0f, "%.3f m")) {
        mImportOption.mImportTranslation = {Translation[0], Translation[1], Translation[2]};
    }

    if (ImGui::DragFloat3("Import Rotation", Rotation.data(), 0.5f, -360.0f, 360.0f, "%.1f deg")) {
        mImportOption.mImportRotation = {Rotation[0] * 1_deg, Rotation[1] * 1_deg, Rotation[2] * 1_deg};
    }

    ImGui::SetItemTooltip("Pitch, Yaw, Roll 순서의 각도(도)입니다.");
    ImGui::DragFloat("Import Uniform Scale", &mImportOption.mImportUniformScale, 0.01f, 0.001f, 1000.0f, "%.3f");

    ImGui::SeparatorText("Mesh");
    ImGui::Checkbox("Import Mesh", &mImportOption.mImportMesh);
    ImGui::SetItemTooltip("메시와 재질을 가져옵니다.\n끄면 스켈레톤이나 애니메이션만 가져옵니다.");
    ImGui::BeginDisabled(!mImportOption.mImportMesh);
    ImGui::Checkbox("Combine Meshes", &mImportOption.mCombineMeshes);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("파일의 모든 메시를 하나의 메시 에셋으로 합칩니다.\n끄면 메시 노드마다 별도의 메시 에셋을 만듭니다.");
    ImGui::BeginDisabled(!mImportOption.mImportMesh);
    ImGui::Checkbox("Generate Convex Collision", &mImportOption.mGenerateConvexCollision);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("정적 메시의 정점을 감싸는 볼록 외곽(최대 255개 꼭짓점)을 물리 충돌 형상으로 만들어 함께 저장합니다.\n평면 메시는 얇은 판 모양 외곽이 되며, 끄거나 외곽을 만들 수 없으면 메시 경계 상자를 사용합니다.");
    ImGui::Checkbox("Import Skeleton", &mImportOption.mImportSkeleton);
    ImGui::SetItemTooltip("원본에 스켈레톤이 있으면 본 계층, 바인드 포즈, 정점별 본 가중치를 읽습니다.\n메시는 스켈레탈 메시 에셋으로 저장되며 배치하거나 메시 뷰어에서 열 수 있습니다.");

    static constexpr Core::TStaticArray<const char*, 3> NormalMethods{"Compute Normals", "Import Normals", "Import Normals and Tangents"};
    int NormalMethod{static_cast<int>(mImportOption.mNormalImportMethod)};

    if (ImGui::Combo("Normal Import Method", &NormalMethod, NormalMethods.data(), static_cast<int>(NormalMethods.size()))) {
        mImportOption.mNormalImportMethod = static_cast<Asset::ENormalImportMethod>(NormalMethod);
    }

    ImGui::SeparatorText("Material");
    ImGui::Checkbox("Import Materials", &mImportOption.mImportMaterials);
    ImGui::SetItemTooltip("원본 재질로 머티리얼 에셋을 만듭니다.\n끄면 메시 슬롯이 기본 머티리얼을 사용합니다.");
    ImGui::BeginDisabled(!mImportOption.mImportMaterials);
    ImGui::Checkbox("Import Textures", &mImportOption.mImportTextures);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Animation");
    ImGui::Checkbox("Import Animations", &mImportOption.mImportAnimations);
    ImGui::SetItemTooltip("FBX의 애니메이션 스택마다 본 트랙을 재샘플링해 애니메이션 시퀀스 에셋을 만듭니다.\n애니메이션을 적용할 스켈레톤을 임포트할 때와 같은 축, 단위, 변환 옵션을 사용하세요.");
    ImGui::BeginDisabled(!mImportOption.mImportAnimations);

    static constexpr Core::TStaticArray<const char*, 3> AnimationLengths{"Exported Time", "Animated Key", "Set Range"};
    int AnimationLength{static_cast<int>(mImportOption.mAnimationLength)};

    if (ImGui::Combo("Animation Length", &AnimationLength, AnimationLengths.data(), static_cast<int>(AnimationLengths.size()))) {
        mImportOption.mAnimationLength = static_cast<Asset::EAnimationLengthImportType>(AnimationLength);
    }

    ImGui::SetItemTooltip("Exported Time: 애니메이션 스택에 기록된 시간 구간을 사용합니다.\nAnimated Key: 애니메이션 키가 있는 구간을 사용합니다.\nSet Range: 아래에 지정한 프레임 구간을 사용합니다.");

    if (mImportOption.mAnimationLength == Asset::EAnimationLengthImportType::SetRange) {
        Core::TStaticArray<int, 2> Range{mImportOption.mFrameImportRangeStart, mImportOption.mFrameImportRangeEnd};

        if (ImGui::DragInt2("Frame Import Range", Range.data(), 1.0f)) {
            mImportOption.mFrameImportRangeStart = Range[0];
            mImportOption.mFrameImportRangeEnd = Range[1];
        }
    }

    ImGui::DragInt("Custom Sample Rate", &mImportOption.mCustomSampleRate, (1_Hz).GetBase(), 0, static_cast<Int32>((1000_Hz).GetBase()), mImportOption.mCustomSampleRate == 0 ? "Source" : "%d fps");
    ImGui::SetItemTooltip("0이면 원본 장면의 프레임 레이트를 사용합니다(없으면 30 fps).");
    ImGui::Checkbox("Snap To Closest Frame Boundary", &mImportOption.mSnapToClosestFrameBoundary);
    ImGui::SetItemTooltip("시작과 끝 시간을 가장 가까운 프레임 경계에 맞춥니다.");
    ImGui::Checkbox("Remove Redundant Keys", &mImportOption.mRemoveRedundantKeys);
    ImGui::SetItemTooltip("값이 변하지 않는 위치, 회전, 스케일 채널은 키 하나만 저장합니다.");
    ImGui::EndDisabled();

    if (std::any_of(Sources.begin(), Sources.end(), [](const std::filesystem::path& Source) {
            return !Asset::IMeshImporter::IsSupported(Source);
        })) {
        ImGui::TextDisabled("Non-mesh files in this batch are imported without these options.");
    }

    ImGui::Separator();

    const bool Valid{mImportOption.IsValid()};

    ImGui::BeginDisabled(!Valid);

    if (ImGui::Button("Import")) {
        const auto Batch{Sources};
        const auto Directory{mPendingImportDirectories.front()};

        Finish();
        ImportFiles(Batch, Directory, mImportOption);
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Reset to Defaults")) {
        mImportOption = Asset::FAssetImportOption{};
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        mContext->AddLog("Import canceled");
        Finish();
    }

    if (!Valid) {
        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "Translation, rotation and scale must be finite, and scale must not be zero.");
    }

    ImGui::EndPopup();
}

void FAssetImportController::ImportFiles(const Core::TArray<std::filesystem::path>& Sources, const std::filesystem::path& Directory, const Asset::FAssetImportOption& Option) {
    if (!mContext->CanEditWorld()) {
        return;
    }

    const std::filesystem::path Destination{Directory};
    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};
    std::filesystem::path LastImported{};
    Core::FString Failures{};
    Uint32 Imported{};

    for (const auto& Source : Sources) {
        std::filesystem::path Target{};
        Core::FString Error{};

        if (!Assets->ImportAsset(Source, Destination, Option, Target, Error)) {
            const Core::FString Failure{"Cannot import '" + Core::FilePath::ToUtf8(Source.filename()) + "': " + Error};

            mContext->AddLog(Failure);

            Failures += (Failures.empty() ? Core::FString{} : "\n") + Failure;

            continue;
        }

        ++Imported;
        LastImported = Target;

        mContext->AddLog("Imported " + Core::FilePath::ToGenericUtf8(Target));
    }

    if (mImported) {
        mImported(Imported == 1 ? LastImported : Destination, Imported, Failures);
    }
}

void FAssetImportController::QueueFileDrop(void* Handle) {
    const HDROP Drop{static_cast<HDROP>(Handle)};
    POINT Point{};

    if (Drop == nullptr) {
        return;
    }

    ImGuiContext* Context{ImGui::GetCurrentContext()};

    if (!mContext->CanEditWorld() || Context == nullptr || !DragQueryPoint(Drop, &Point) || Context->OpenPopupStack.Size != 0) {
        DragFinish(Drop);

        return;
    }

    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        ClientToScreen(mPlatform->GetWindowHandle(), &Point);
    }

    const ImVec2 Position{static_cast<float>(Point.x), static_cast<float>(Point.y)};
    ImGuiWindow* Browser{ImGui::FindWindowByName(ContentBrowserName)};
    ImGuiWindow* Hovered{};

    for (int Index{Context->Windows.Size}; Index > 0; --Index) {
        ImGuiWindow* Window{Context->Windows[Index - 1]};

        if (Window->WasActive && !Window->Hidden && (Window->Flags & ImGuiWindowFlags_NoMouseInputs) == 0 && Window->OuterRectClipped.Contains(Position)) {
            Hovered = Window;

            break;
        }
    }

    if (Browser == nullptr || Browser->Collapsed || Hovered == nullptr || !ImGui::IsWindowChildOf(Hovered, Browser, false, false)) {
        DragFinish(Drop);

        return;
    }

    std::filesystem::path Directory{};
    bool Found{};

    for (Index_t Index{static_cast<Index_t>(mContentDropBounds.size())}; Index > 0; --Index) {
        const FVector4& Bounds{mContentDropBounds[Index - 1]};

        if (Position.x >= Bounds.x && Position.y >= Bounds.y && Position.x < Bounds.z && Position.y < Bounds.w) {
            Directory = mContentDropDirectories[Index - 1];
            Found = true;

            break;
        }
    }

    if (!Found) {
        DragFinish(Drop);

        return;
    }

    Core::TArray<std::filesystem::path> Sources{};
    const UINT Count{DragQueryFileW(Drop, 0xFFFFFFFF, nullptr, 0)};

    for (UINT Index{}; Index < Count; ++Index) {
        const UINT Length{DragQueryFileW(Drop, Index, nullptr, 0)};
        std::wstring Path{};
        Path.resize(static_cast<std::size_t>(Length) + 1);

        if (Length != 0 && DragQueryFileW(Drop, Index, Path.data(), Length + 1) == Length) {
            Path.resize(Length);
            Sources.emplace_back(Path);
        }
    }

    DragFinish(Drop);

    if (!Sources.empty()) {
        mDropDirectories.push_back(std::move(Directory));
        mDroppedFiles.push_back(std::move(Sources));
    }
}

void FAssetImportController::ImportDroppedFiles() {
    auto Files{std::move(mDroppedFiles)};
    auto Directories{std::move(mDropDirectories)};

    mDroppedFiles.clear();
    mDropDirectories.clear();

    for (Index_t Index{}; Index < Files.size(); ++Index) {
        RequestImport(std::move(Files[Index]), Directories[Index]);
    }
}

void FAssetImportController::RegisterContentDropTarget(const std::filesystem::path& Directory, const FVector4& Bounds) {
    const ImRect Clip{ImGui::GetCurrentWindow()->ClipRect};
    const FVector4 Visible{std::max(Bounds.x, Clip.Min.x), std::max(Bounds.y, Clip.Min.y), std::min(Bounds.z, Clip.Max.x), std::min(Bounds.w, Clip.Max.y)};

    if (Visible.z > Visible.x && Visible.w > Visible.y) {
        mContentDropDirectories.push_back(Directory);
        mContentDropBounds.push_back(Visible);
    }
}
