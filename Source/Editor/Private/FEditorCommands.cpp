/**
 * @file FEditorCommands.cpp
 * @brief 레벨·프로젝트 전환과 저장·실행·내보내기·종료 명령을 조정합니다.
 * @ingroup Editor
 */

#include "FEditorCommands.h"
#include "FEditorViewportLayout.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorViewport.h"
#include "Editor/FPlayInEditorSession.h"
#include "Editor/FGameExporter.h"
#include "Editor/FProjectDialog.h"
#include "Core/FAtomicFileWriter.h"
#include "Core/FilePath.h"
#include "Engine/FProject.h"
#include "Engine/FRenderTarget.h"
#include "Engine/FSceneViewFamily.h"
#include "Engine/UWorld.h"
#include "Platform/Window.h"
#include "RenderCore/IRendererModule.h"
#include "Core/Types.h"
#include <commdlg.h>
#include <d3d11.h>
#include <DirectXTex.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <utility>

using namespace Editor;

namespace {

    constexpr Uint32 ThumbnailSize{192};

    bool IsStartupLevel(const Engine::FProject& Project, const std::filesystem::path& ScenePath) {
        const std::filesystem::path Startup{Project.GetAssets()->GetContentRoot() / Core::FilePath::FromUtf8(Project.GetStartupScene())};
        std::error_code Code{};

        return std::filesystem::equivalent(ScenePath, Startup, Code);
    }

    bool EncodeProjectThumbnail(const Engine::FRenderTarget& Target, const RenderCore::IRendererModule& Renderer, DirectX::Blob& Png) {
        auto* View{static_cast<ID3D11ShaderResourceView*>(Target.GetNativeTexture())};
        auto* Device{static_cast<ID3D11Device*>(Renderer.GetNativeDevice())};
        auto* Context{static_cast<ID3D11DeviceContext*>(Renderer.GetNativeContext())};

        if (View == nullptr || Device == nullptr || Context == nullptr) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Resource> Resource{};
        DirectX::ScratchImage Captured{};
        DirectX::ScratchImage Resized{};
        View->GetResource(Resource.GetAddressOf());

        if (FAILED(DirectX::CaptureTexture(Device, Context, Resource.Get(), Captured)) || FAILED(DirectX::Resize(*Captured.GetImage(0, 0, 0), ThumbnailSize, ThumbnailSize, DirectX::TEX_FILTER_FANT, Resized))) {
            return false;
        }

        return SUCCEEDED(DirectX::SaveToWICMemory(*Resized.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB, GUID_ContainerFormatPng, Png, &GUID_WICPixelFormat24bppBGR));
    }

}

FEditorCommands::FEditorCommands(FEditorContext* Context, FPlayInEditorSession* PlaySession, FEditorViewportLayout& ViewportLayout, Platform::FWindowsApplication* Platform, FMessageBox& Messages)
	: mContext{Context},
	  mPlaySession{PlaySession},
	  mViewportLayout{ViewportLayout},
	  mPlatform{Platform},
	  mMessages{Messages} {
}

FEditorCommands::~FEditorCommands() = default;

void FEditorCommands::SetContentChanged(std::function<void()> Handler) {
    mContentChanged = std::move(Handler);
}

void FEditorCommands::SetCollectAssetSavePrompts(std::function<Core::TArray<FMessageBox::FSavePrompt>()> Handler) {
    mCollectAssetSavePrompts = std::move(Handler);
}

bool FEditorCommands::WantsExit() const {
    return mExit;
}

void FEditorCommands::RequestExit() {
    ConfirmSceneChange([this]() {
        mExit = true;
    });
}

std::filesystem::path FEditorCommands::ChooseFile(bool Save) {
    Core::TStaticArray<wchar_t, 4096> Buffer{};

    if (Save) {
        const std::wstring Filename{mContext->GetScenePath().empty() ? L"Scene.masset" : mContext->GetScenePath().filename().wstring()};

        std::copy_n(Filename.begin(), std::min(Filename.size(), Buffer.size() - 1), Buffer.begin());
    }

    const std::filesystem::path Directory{mContext->GetProject()->GetAssets()->GetContentRoot() / "Maps"};

    std::error_code Error{};

    std::filesystem::create_directories(Directory, Error);

    OPENFILENAMEW Dialog{};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.hwndOwner = mPlatform->GetWindowHandle();
    Dialog.lpstrFilter = L"Macaw Scene (*.masset)\0*.masset\0";
    Dialog.lpstrFile = Buffer.data();
    Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
    Dialog.lpstrInitialDir = Directory.c_str();
    Dialog.lpstrDefExt = L"masset";
    Dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    if (Save ? GetSaveFileNameW(&Dialog) : GetOpenFileNameW(&Dialog)) {
        return std::filesystem::path{Buffer.data()};
    }

    return {};
}

void FEditorCommands::SaveLevel(bool SaveAs) {
    const auto Path{SaveAs || mContext->GetScenePath().empty() ? ChooseFile(true) : mContext->GetScenePath()};

    if (!Path.empty()) {
        mContext->SaveScene(Path);
        if (mContentChanged) {
            mContentChanged();
        }
    }
}

void FEditorCommands::SetCurrentLevelAsStartup() {
    Engine::FProject* Project{mContext->GetProject()};
    const std::filesystem::path Path{mContext->GetScenePath()};

    if (Project == nullptr || Path.empty()) {
        return;
    }

    const bool WasStartup{IsStartupLevel(*Project, Path)};
    Core::FString Error{};

    if (!mContext->SaveScene(Path)) {
        return;
    }

    if (!Project->SetStartupScene(Path, Error)) {
        mContext->AddLog(Error);
    } else if (!WasStartup) {
        CaptureProjectThumbnail();
    }
}

void FEditorCommands::OpenLevel() {
    if (mMessages.IsOpen()) {
        return;
    }

    const auto Path{ChooseFile(false)};

    if (!Path.empty()) {
        OpenLevel(Path);
    }
}

void FEditorCommands::OpenLevel(const std::filesystem::path& Path) {
    ConfirmSceneChange([this, Path]() {
        mContext->LoadScene(Path);
    });
}

void FEditorCommands::NewLevel() {
    ConfirmSceneChange([this]() {
        mContext->NewScene();
    });
}

void FEditorCommands::RequestPlay() {
    if (mPlaySession->IsActive()) {
        return;
    }

    if (mViewportLayout.CanChangeLayout()) {
        mPlaySession->Start(&mPlatform->GetInput());
    }
}

void FEditorCommands::ExportGame() {
    if (mMessages.IsOpen()) {
        return;
    }

    const auto Destination{FProjectDialog::ChooseDirectory(mPlatform->GetWindowHandle())};

    if (!Destination.empty()) {
        ConfirmSceneChange([this, Destination]() {
            ExportGameTo(Destination);
        });
    }
}

void FEditorCommands::ExportGameTo(const std::filesystem::path& Destination) {
    Core::TStaticArray<wchar_t, 32768> Executable{};
    const DWORD Length{GetModuleFileNameW(nullptr, Executable.data(), static_cast<DWORD>(Executable.size()))};
    Core::FString Error{};
    bool Exported{};

    if (Length == 0 || Length >= Executable.size()) {
        Error = "Cannot locate the game executable";
    } else {
        const auto Runtime{std::filesystem::path{Executable.data()}.parent_path() / "GameRuntime"};

        Exported = Editor::FGameExporter::Export(*mContext->GetProject(), Runtime, Destination, Error);
    }

    const Core::FString Message{Exported ? "Exported game to " + Core::FilePath::ToUtf8(Destination) : Error};

    mContext->AddLog(Message);
    mMessages.Show(Exported ? "Game Exported" : "Cannot Export Game", Message);
}

void FEditorCommands::ConfirmSceneChange(std::function<void()> Action) {
    if (mMessages.IsOpen()) {
        return;
    }

    auto Prompts{mCollectAssetSavePrompts ? mCollectAssetSavePrompts() : Core::TArray<FMessageBox::FSavePrompt>{}};

    if (mContext->IsSceneDirty()) {
        Prompts.push_back({"Scene", "Save changes to the current scene?", [this]() {
            const auto Path{mContext->GetScenePath().empty() ? ChooseFile(true) : mContext->GetScenePath()};
            const bool Saved{!Path.empty() && mContext->SaveScene(Path)};

            if (Saved && mContentChanged) {
                mContentChanged();
            }

            return Saved;
        }});
    }

    mMessages.ConfirmSave(std::move(Prompts), std::move(Action));
}

void FEditorCommands::ChooseProject(bool Create) {
    ConfirmSceneChange([this, Create]() {
        mContext->RequestProject({}, Create);
    });
}

void FEditorCommands::CaptureProjectThumbnail() {
    const Engine::FProject* Project{mContext->GetProject()};

    if (Project == nullptr || !IsStartupLevel(*Project, mContext->GetScenePath())) {
        return;
    }

    constexpr Uint32 RenderSize{ThumbnailSize * 2};
    Engine::UWorld* World{mContext->GetWorld()};
    RenderCore::IRendererModule* Renderer{mContext->GetRenderer()};
    FEditorViewport* Viewport{mViewportLayout.GetViewport(0)};

    if (World == nullptr || World->GetScene() == nullptr || Renderer == nullptr || Viewport == nullptr) {
        return;
    }

    const auto Target{Renderer->CreateRenderTarget(RenderSize, RenderSize)};

    if (Target == nullptr) {
        mContext->AddLog("Cannot create the project thumbnail render target");

        return;
    }

    FEditorViewportClient Camera{nullptr};
    Camera.SetPerspectiveCamera(Viewport->GetClient()->GetPerspectiveCamera());
    const Engine::FSceneView View{Camera.CalcSceneView({RenderSize, RenderSize})};
    World->SendAllEndOfFrameUpdates();
    Renderer->BeginRenderingViewFamily({World->GetScene(), Target.get(), View});

    DirectX::Blob Png{};

    if (!EncodeProjectThumbnail(*Target, *Renderer, Png)) {
        mContext->AddLog("Cannot capture the project thumbnail");

        return;
    }

    Core::FAtomicFileWriter Writer{};
    Core::FString Error{};
    const std::filesystem::path Path{Project->GetRootPath() / "Saved/ProjectThumbnail.png"};

    if (Writer.Open(Path, Error)) {
        Writer.GetStream().write(reinterpret_cast<const char*>(Png.GetBufferPointer()), static_cast<std::streamsize>(Png.GetBufferSize()));
        Writer.Commit(Error);
    }

    if (!Error.empty()) {
        mContext->AddLog("Cannot save the project thumbnail: " + Error);
    } else {
        mContext->AddLog("Saved project thumbnail to " + Core::FilePath::ToUtf8(Path));
    }
}
