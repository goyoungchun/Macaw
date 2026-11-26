/**
 * @file FEditorUI.cpp
 * @brief ImGui 컨텍스트와 Win32·D3D11 백엔드를 초기화하고 레벨 편집기 프레임을 구동합니다.
 * @ingroup Editor
 */

#include "FEditorUI.h"
#include "FLevelEditorToolkit.h"
#include "FEditorAnimation.h"
#include "Core/FilePath.h"
#include "Core/Stats.h"
#include "Editor/FEditorContext.h"
#include "Engine/FProject.h"
#include "Platform/Window.h"
#include "RenderCore/IRendererModule.h"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <filesystem>

/**
 * @brief Win32 메시지를 ImGui 입력 백엔드에 전달합니다.
 * @param[in] Window 마우스 캡처 또는 메시지 처리 대상 Win32 창입니다.
 * @param[in] Message Win32 메시지 식별자입니다.
 * @param[in] WParam Win32 메시지의 첫 번째 매개변수입니다.
 * @param[in] LParam Win32 메시지의 두 번째 매개변수입니다.
 * @return 백엔드의 메시지 처리 결과입니다.
 */
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);

using namespace Editor;

FEditorUI::FEditorUI(FEditorContext* Context, FEditorViewport* Viewport, FPlayInEditorSession* PlaySession)
	: mContext{Context},
	  mViewport{Viewport},
	  mPlaySession{PlaySession} {
}

FEditorUI::~FEditorUI() {
    Shutdown();
}

bool FEditorUI::Initialize(RenderCore::IRendererModule* Renderer, Platform::FWindowsApplication* Platform) {
    if (Renderer == nullptr || Platform == nullptr || mContextCreated || mContext->GetProject() == nullptr) {
        return false;
    }

    mPlatform = Platform;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    mContextCreated = true;
    ImGuiIO& Input{ImGui::GetIO()};
    Input.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    const std::filesystem::path Layout{mContext->GetProject()->GetRootPath() / "Saved/EditorLayout.ini"};
    std::error_code Error{};
    std::filesystem::create_directories(Layout.parent_path(), Error);
    mIniPath = Core::FilePath::ToUtf8(Layout);
    Input.IniFilename = mIniPath.c_str();
    Input.LogFilename = nullptr;
    if (std::filesystem::exists("C:/Windows/Fonts/malgun.ttf")) {
        Input.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 16.0f);
    } else if (std::filesystem::exists("C:/Windows/Fonts/segoeui.ttf")) {
        Input.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 16.0f);
    } else {
        Input.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& Style{ImGui::GetStyle()};
    Style.WindowRounding = 3.0f;
    Style.FrameRounding = 3.0f;
    Style.GrabRounding = 3.0f;
    mWin32Ready = ImGui_ImplWin32_Init(Platform->GetWindowHandle());
    mDxReady = mWin32Ready && ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(Renderer->GetNativeDevice()), static_cast<ID3D11DeviceContext*>(Renderer->GetNativeContext()));
    if (!mDxReady) {
        Shutdown();
        return false;
    }

    mToolkit = std::make_unique<FLevelEditorToolkit>(mContext, mViewport, mPlaySession);
    if (!mToolkit->Initialize(Renderer, Platform, mIniPath)) {
        Shutdown();
        return false;
    }

    ImGui::LoadIniSettingsFromDisk(mIniPath.c_str());

    Platform->SetMessageHandler([this](HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
        if (Message == WM_CLOSE) {
            if (mPlatform->IsMinimized()) {
                mPlatform->Restore();
            }

            mToolkit->RequestClose();

            return true;
        }

        if (Message == WM_DROPFILES) {
            mToolkit->QueueFileDrop(reinterpret_cast<void*>(WParam));
            return true;
        }

        return ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam) != 0;
    });
    Platform->SetCustomTitleBar(true);
    mInitialized = true;
    return true;
}

void FEditorUI::Shutdown() {
    if (mPlatform != nullptr) {
        mPlatform->SetMessageHandler({});
        mPlatform->SetCustomTitleBar(false);
    }

    if (mContextCreated && ImGui::GetIO().IniFilename != nullptr) {
        if (ImGui::GetCurrentContext()->SettingsLoaded) {
            ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
        }
    }

    mToolkit.reset();

    if (mContextCreated) {
        ImGui::GetIO().IniFilename = nullptr;
    }

    if (mDxReady) {
        ImGui_ImplDX11_Shutdown();
        mDxReady = false;
    }

    if (mWin32Ready) {
        ImGui_ImplWin32_Shutdown();
        mWin32Ready = false;
    }

    if (mContextCreated) {
        FEditorAnimation::Shutdown();
        ImGui::DestroyContext();
        mContextCreated = false;
    }

    mPlatform = nullptr;
    mInitialized = false;
}

bool FEditorUI::BeginFrame(float DeltaTime) {
    static Core::FStat UiTime{"unit", "Editor UI CPU", Core::EStatUnit::Milliseconds};
    const Core::FScopedStatTimer StatTimer{UiTime};
    if (!mInitialized) {
        return false;
    }

    mContext->GetRenderer()->BeginFrame(mPlatform->GetWidth(), mPlatform->GetHeight());
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    mToolkit->ApplyAppearance();
    ImGui::NewFrame();
    FEditorAnimation::BeginFrame();
    mToolkit->Tick(DeltaTime);
    mToolkit->Draw(DeltaTime);
    return true;
}

bool FEditorUI::EndFrame(float DeltaTime) {
    if (!mInitialized) {
        return false;
    }

    mToolkit->Render(DeltaTime);
    static Core::FStat SubmitTime{"unit", "ImGui submission CPU", Core::EStatUnit::Milliseconds};
    const Core::FScopedStatTimer Timer{SubmitTime};
    ImGui::Render();
    mContext->GetRenderer()->BeginUiRender();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return true;
}

bool FEditorUI::WantsExit() const {
    return mToolkit == nullptr || mToolkit->WantsExit();
}

FEditorViewport* FEditorUI::GetActiveViewport() const {
    return mToolkit != nullptr ? mToolkit->GetActiveViewport() : mViewport;
}
