/**
 * @file SplashWindow.cpp
 * @brief 로딩 진행·애니메이션용 Win32 스플래시 창을 관리합니다.
 * @ingroup Platform
 */

#include "Platform/SplashWindow.h"
#include "Core/Literals/UnitLiterals.h"
#include "Platform/WindowResources.h"
#include "Core/Base/ErrorHandler.h"
#include "FSplashCanvas.h"

#include <dwmapi.h>
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace Core::Literals;

using namespace std::chrono_literals;

namespace Platform {

    /**
     * @brief 이 파일에서 사용하는 내부 보조 기능과 상태를 묶습니다.
     */
    namespace {
        /**
         * @brief 스플래시 레이아웃 기준 너비입니다.
         */
        constexpr Int32 LogicalWidth{720};

        /**
         * @brief 스플래시 레이아웃 기준 높이입니다.
         */
        constexpr Int32 LogicalHeight{370};

        /**
         * @brief 애니메이션 타이머의 밀리초 간격입니다.
         */
        constexpr auto FrameInterval{16ms};

        /**
         * @brief 완료 후 스플래시 창 유지 시간입니다.
         */
        constexpr float FinishTimeout{(0.4_s).GetBase()};

        /**
         * @brief 스플래시 Win32 창 클래스 이름입니다.
         */
        constexpr wchar_t ClassName[]{L"MacawSplashWindow"};

        /**
         * @brief UTF-8 문자열을 Win32 API용 UTF-16으로 변환합니다.
         * @param[in] Text 해석·변환·탐색할 원문 문자열입니다.
         * @return 해석·변환한 문자열입니다.
         */
        std::wstring Widen(std::string_view Text) {
            if (Text.empty()) {
                return {};
            }

            const Int32 Length{MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<Int32>(Text.size()), nullptr, 0)};
            std::wstring Result{};
            Result.resize(static_cast<std::size_t>(std::max(Length, 0)), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<Int32>(Text.size()), Result.data(), Length);

            return Result;
        }

        /**
         * @brief 스플래시 창의 타이머·페인트·종료 메시지를 처리합니다.
         * @param[in] Window 메시지를 받은 Win32 창입니다.
         * @param[in] Message 처리할 Win32 메시지 코드입니다.
         * @param[in] WParam Win32 메시지의 키 코드·부가 값입니다.
         * @param[in] LParam Win32 메시지의 추가 상태·좌표 정보입니다.
         * @return 처리한 메시지의 Win32 반환값입니다.
         */
        LRESULT CALLBACK SplashProcedure(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
            switch (Message) {
            case WM_ERASEBKGND:
                return 1;
            case WM_CLOSE:
                return 0;
            case WM_NCHITTEST:
                return HTCAPTION;
            default:
                return DefWindowProcW(Window, Message, WParam, LParam);
            }
        }
    }

    /**
     * @brief 표시하지 않은 상태로 스플래시 창을 구성합니다.
     */
    Platform::FSplashWindow::FSplashWindow() = default;

    /**
     * @brief 창이 남아 있으면 닫고 스레드를 정리합니다.
     */
    Platform::FSplashWindow::~FSplashWindow() {
        Close();
    }

    /**
     * @brief 스플래시 스레드를 시작하고 첫 화면이 그려질 때까지 기다립니다.
     */
    void Platform::FSplashWindow::Show(HWND Anchor, std::wstring_view ProjectName, const std::filesystem::path& ProjectDirectory) {
        if (mThread.joinable()) {
            return;
        }

        mProjectName = ProjectName;
        mBackgroundImage.clear();

        for (const wchar_t* Extension : {L".png", L".jpg", L".bmp"}) {
            std::filesystem::path Candidate{ProjectDirectory / L"Content" / L"Splash" / L"EdSplash"};
            std::error_code Code{};

            Candidate += Extension;

            if (!ProjectDirectory.empty() && std::filesystem::is_regular_file(Candidate, Code)) {
                mBackgroundImage = std::move(Candidate);

                break;
            }
        }

        mStage = L"Loading...";
        mProgress = 0.0f;
        mStarted = false;
        mFinishRequested = false;
        mFinished = false;
        mCloseRequested = false;

        const HMONITOR Monitor{MonitorFromWindow(Anchor, MONITOR_DEFAULTTOPRIMARY)};
        mThread = std::thread{[this, Monitor]() {
            ThreadMain(Monitor);
        }};

        std::unique_lock Lock{mMutex};
        mSignal.wait(Lock, [this]() {
            return mStarted;
        });
    }

    /**
     * @brief 진행률을 끝까지 채우는 애니메이션이 끝날 때까지 기다립니다. 창은 남겨 둡니다.
     */
    void Platform::FSplashWindow::Finish() {
        if (!mThread.joinable()) {
            return;
        }

        std::unique_lock Lock{mMutex};

        mFinishRequested = true;
        mProgress = 1.0f;
        mStage = L"Opening editor...";
        mSignal.wait(Lock, [this]() {
            return mFinished;
        });
    }

    /**
     * @brief 창을 파괴하고 스플래시 스레드가 끝날 때까지 기다립니다.
     */
    void Platform::FSplashWindow::Close() {
        if (!mThread.joinable()) {
            return;
        }

        {
            const std::lock_guard Lock{mMutex};

            mCloseRequested = true;
        }

        mThread.join();
    }

    /**
     * @brief 표시할 로딩 단계와 진행률을 교체합니다.
     */
    void Platform::FSplashWindow::OnStage(std::string_view Stage, float Progress) {
        std::wstring Text{Widen(Stage)};
        const std::lock_guard Lock{mMutex};

        mStage = std::move(Text);
        mProgress = std::max(mProgress, std::clamp(Progress, 0.0f, 1.0f));
    }

    void Platform::FSplashWindow::OnFile(std::string_view, const std::filesystem::path&) {
    }

    /**
     * @brief 스플래시 창을 만들고 닫기 요청까지 메시지 처리와 그리기를 반복합니다.
     */
    void Platform::FSplashWindow::ThreadMain(HMONITOR Monitor) {
        // 프로젝트 스플래시 이미지를 WIC로 읽으므로 이 스레드에도 COM이 필요합니다.
        const bool ComReady{SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))};
        const HINSTANCE Instance{GetModuleHandleW(nullptr)};

        WNDCLASSEXW Class{};
        Class.cbSize = sizeof(Class);
        Class.style = CS_DROPSHADOW;
        Class.lpfnWndProc = &SplashProcedure;
        Class.hInstance = Instance;
        Class.hIcon = static_cast<HICON>(LoadImageW(Instance, MAKEINTRESOURCEW(MacawIconResource), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
        Class.hIconSm = static_cast<HICON>(LoadImageW(Instance, MAKEINTRESOURCEW(MacawIconResource), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        Class.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
        Class.lpszClassName = ClassName;

        if (RegisterClassExW(&Class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            Core::ErrorHandler::ReportWin32(GetLastError(), "Splash", "Splash window class registration failed", Core::ErrorHandler::EErrorLevel::Warning);
        }

        MONITORINFO Info{};
        Info.cbSize = sizeof(Info);

        GetMonitorInfoW(Monitor, &Info);

        const LONG CenterX{(Info.rcWork.left + Info.rcWork.right) / 2};
        const LONG CenterY{(Info.rcWork.top + Info.rcWork.bottom) / 2};
        const HWND Window{CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, ClassName, L"Macaw", WS_POPUP, CenterX, CenterY, 1, 1, nullptr, nullptr, Instance, nullptr)};
        FSplashCanvas Canvas{};
        bool Ready{};

        if (Window != nullptr) {
            const float Scale{static_cast<float>(GetDpiForWindow(Window)) / 96.0f};
            const Int32 Width{static_cast<Int32>(std::lround(LogicalWidth * Scale))};
            const Int32 Height{static_cast<Int32>(std::lround(LogicalHeight * Scale))};
            const DWM_WINDOW_CORNER_PREFERENCE Corners{DWMWCP_ROUNDSMALL};

            DwmSetWindowAttribute(Window, DWMWA_WINDOW_CORNER_PREFERENCE, &Corners, sizeof(Corners));
            SetWindowPos(Window, nullptr, CenterX - Width / 2, CenterY - Height / 2, Width, Height, SWP_NOZORDER | SWP_NOACTIVATE);

            Ready = Canvas.Initialize(Width, Height, Scale, mProjectName, mBackgroundImage);
        }

        if (!Ready) {
            Core::ErrorHandler::Report("Splash", "Cannot create the splash window", Core::ErrorHandler::EErrorLevel::Warning);
        }

        auto Previous{std::chrono::steady_clock::now()};
        float Displayed{};
        float FinishElapsed{};
        bool Shown{};

        while (Ready) {
            MSG Message{};

            while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&Message);
                DispatchMessageW(&Message);
            }

            std::wstring Stage{};
            float Target{};
            bool Finishing{};

            {
                const std::lock_guard Lock{mMutex};

                if (mCloseRequested) {
                    break;
                }

                Stage = mStage;
                Target = mProgress;
                Finishing = mFinishRequested && !mFinished;
            }

            const auto Now{std::chrono::steady_clock::now()};
            const float Delta{std::chrono::duration<float>(Now - Previous).count()};

            Previous = Now;

            Displayed += (Target - Displayed) * (1.0f - std::exp(-Delta * (Finishing ? 14.0f : 5.0f)));

            if (Finishing) {
                FinishElapsed += Delta;

                if (Displayed > 0.995f || FinishElapsed > FinishTimeout) {
                    Displayed = 1.0f;

                    const std::lock_guard Lock{mMutex};

                    mFinished = true;
                    mSignal.notify_all();
                }
            }

            const HDC Surface{GetDC(Window)};

            Canvas.Draw(Surface, Stage, Displayed);
            ReleaseDC(Window, Surface);
            ValidateRect(Window, nullptr);

            if (!Shown) {
                ShowWindow(Window, SW_SHOWNORMAL);
                Shown = true;

                const std::lock_guard Lock{mMutex};

                mStarted = true;
                mSignal.notify_all();
            }

            MsgWaitForMultipleObjects(0, nullptr, false, static_cast<DWORD>(FrameInterval.count()), QS_ALLINPUT);
        }

        if (Window != nullptr) {
            DestroyWindow(Window);
        }

        if (ComReady) {
            CoUninitialize();
        }

        const std::lock_guard Lock{mMutex};

        mStarted = true;
        mFinished = true;
        mSignal.notify_all();
    }

}
