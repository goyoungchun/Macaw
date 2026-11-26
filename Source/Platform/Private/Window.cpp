/**
 * @file Window.cpp
 * @brief Win32 창과 메시지 루프를 관리합니다.
 * @ingroup Platform
 */

#include "Platform/Window.h"
#include "Platform/WindowResources.h"
#include "FMouseCapture.h"
#include "Core/Base/ErrorHandler.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#undef IsMinimized
#undef IsMaximized

namespace Platform {

    /**
     * @brief 이 파일에서 사용하는 내부 보조 기능과 상태를 묶습니다.
     */
    namespace {

        /**
         * @brief 마우스 버튼 하나라도 눌렸는지 검사합니다.
         * @param[in] Input 키·마우스 상태를 조회할 공통 입력입니다.
         * @return 마우스 버튼 하나라도 눌렸으면 true입니다.
         */
        bool IsMouseDown(const Core::FInput& Input) {
            using Core::EKey;

            return Input.IsDown(EKey::MouseLeft) || Input.IsDown(EKey::MouseRight) || Input.IsDown(EKey::MouseMiddle) || Input.IsDown(EKey::MouseX1) || Input.IsDown(EKey::MouseX2);
        }

        /**
         * @brief Win32 마우스 메시지에서 버튼의 공통 키 코드를 구합니다.
         * @param[in] Message 처리할 Win32 메시지 코드입니다.
         * @param[in] WParam Win32 메시지의 키 코드·부가 값입니다.
         * @return 버튼에 대응하는 공통 키이며 없으면 EKey::None입니다.
         */
        Core::EKey GetMouseKey(UINT Message, WPARAM WParam) {
            using Core::EKey;

            switch (Message) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
                return EKey::MouseLeft;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
                return EKey::MouseRight;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
                return EKey::MouseMiddle;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
                return GET_XBUTTON_WPARAM(WParam) == XBUTTON1 ? EKey::MouseX1 : EKey::MouseX2;
            default:
                return EKey::None;
            }
        }

        /**
         * @brief Win32 키 메시지를 공통 키와 좌우 보조키 상태에 반영합니다.
         * @param[in] Input 키·마우스 상태를 조회할 공통 입력입니다.
         * @param[in] WParam Win32 메시지의 키 코드·부가 값입니다.
         * @param[in] LParam Win32 메시지의 추가 상태·좌표 정보입니다.
         * @param[in] Down 누름 상태이면 true, 해제이면 false입니다.
         */
        void SetKeyboardKey(Core::FInput& Input, WPARAM WParam, LPARAM LParam, bool Down) {
            using Core::EKey;

            if (WParam > 0xFF || (Down && (LParam & (1LL << 30)) != 0)) {
                return;
            }

            const bool Extended{(LParam & (1LL << 24)) != 0};

            switch (WParam) {
            case VK_SHIFT:
                Input.SetKey(static_cast<EKey>(MapVirtualKeyW((static_cast<UINT>(LParam) >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX)), Down);
                Input.SetKey(EKey::Shift, Input.IsDown(EKey::LeftShift) || Input.IsDown(EKey::RightShift));

                break;
            case VK_CONTROL:
                Input.SetKey(Extended ? EKey::RightControl : EKey::LeftControl, Down);
                Input.SetKey(EKey::Control, Input.IsDown(EKey::LeftControl) || Input.IsDown(EKey::RightControl));

                break;
            case VK_MENU:
                Input.SetKey(Extended ? EKey::RightAlt : EKey::LeftAlt, Down);
                Input.SetKey(EKey::Alt, Input.IsDown(EKey::LeftAlt) || Input.IsDown(EKey::RightAlt));

                break;
            default:
                Input.SetKey(static_cast<EKey>(WParam), Down);

                break;
            }
        }

    }

    /**
     * @brief FWindowsApplication의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Platform::FWindowsApplication::FWindowsApplication() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Platform::FWindowsApplication::~FWindowsApplication() {
        Shutdown();
    }

    /**
     * @brief 지정한 제목과 클라이언트 크기로 Win32 창을 생성합니다.
     */
    bool Platform::FWindowsApplication::Create(const wchar_t* Title, Uint32 Width, Uint32 Height, bool Visible) {
        if (mWindow != nullptr) {
            Core::ErrorHandler::Fatal("Window", "The application window already exists");
        }

        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        const HINSTANCE Instance{GetModuleHandleW(nullptr)};

        WNDCLASSEXW Class{};
        Class.cbSize = sizeof(Class);
        Class.style = CS_HREDRAW | CS_VREDRAW;
        Class.lpfnWndProc = &WindowProcedure;
        Class.hInstance = Instance;
        Class.hIcon = static_cast<HICON>(LoadImageW(Instance, MAKEINTRESOURCEW(MacawIconResource), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
        Class.hIconSm = static_cast<HICON>(LoadImageW(Instance, MAKEINTRESOURCEW(MacawIconResource), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Class.lpszClassName = L"MacawWindow";

        if (RegisterClassExW(&Class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            Core::ErrorHandler::ReportWin32(GetLastError(), "Window", "Window class registration failed", Core::ErrorHandler::EErrorLevel::Critical);
        }

        RECT Rectangle{0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height)};

        AdjustWindowRectEx(&Rectangle, WS_OVERLAPPEDWINDOW, false, 0);

        mWindow = CreateWindowExW(0, Class.lpszClassName, Title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, Rectangle.right - Rectangle.left, Rectangle.bottom - Rectangle.top, nullptr, nullptr, Instance, this);

        if (mWindow == nullptr) {
            Core::ErrorHandler::ReportWin32(GetLastError(), "Window", "Window creation failed", Core::ErrorHandler::EErrorLevel::Critical);
            Core::ErrorHandler::Fatal("Window", "Window creation failed");
        }

        SendMessageW(mWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(Class.hIcon));
        SendMessageW(mWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(Class.hIconSm));

        RECT Client{};

        GetClientRect(mWindow, &Client);

        mWidth = static_cast<Uint32>(Client.right);
        mHeight = static_cast<Uint32>(Client.bottom);

        mRunning = true;

        if (Visible) {
            ShowWindow(mWindow, SW_SHOWDEFAULT);
            UpdateWindow(mWindow);
        }

        return true;
    }

    void Platform::FWindowsApplication::SetQuitOnClose(bool QuitOnClose) {
        mQuitOnClose = QuitOnClose;
    }

    /**
     * @brief 대기 중인 Windows 메시지를 처리하고 실행 지속 여부를 반환합니다.
     */
    bool Platform::FWindowsApplication::PumpMessages() {
        mInput.BeginFrame();

        MSG Message{};

        while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE)) {
            if (Message.message == WM_QUIT) {
                mRunning = false;

                break;
            }

            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }

        return mRunning;
    }

    /**
     * @brief 종속 객체를 역순으로 정리하고 여러 번 호출해도 안전한 종료 상태로 만듭니다.
     */
    void Platform::FWindowsApplication::Shutdown() {
        ReleaseMouseCapture();
        mMouseCapture.reset();
        mMessageHandler = {};

        if (mWindow != nullptr && IsWindow(mWindow)) {
            DestroyWindow(mWindow);
        }

        mWindow = nullptr;
        mRunning = false;
        mPresentationPending = false;
        mCustomTitleBar = false;
        mTitleBarDragBounds = {};
        mMinimumWidth = 0;
        mMinimumHeight = 0;
        mInput = {};
    }

    bool FWindowsApplication::CaptureMouse(const RECT& Bounds) {
        if (!mInput.IsFocused()) {
            ReleaseMouseCapture();
            return false;
        }
        if (mMouseCapture == nullptr) {
            mMouseCapture = std::make_unique<FMouseCapture>();
        }
        return mMouseCapture->Capture(mWindow, Bounds);
    }

    void FWindowsApplication::ReleaseMouseCapture() {
        if (mMouseCapture != nullptr) {
            mMouseCapture->Release();
        }
    }

    bool FWindowsApplication::IsMouseCaptured() const {
        return mMouseCapture != nullptr && mMouseCapture->IsCaptured();
    }

    void FWindowsApplication::Show() {
        if (IsRunning()) {
            ShowWindow(mWindow, SW_SHOW);
        }
    }

    void FWindowsApplication::Hide() {
        if (IsRunning()) {
            ShowWindow(mWindow, SW_HIDE);
        }
    }

    void FWindowsApplication::Restore() {
        if (IsRunning()) {
            ShowWindow(mWindow, SW_RESTORE);
        }
    }

    void FWindowsApplication::Minimize() {
        if (IsRunning()) {
            ShowWindow(mWindow, SW_MINIMIZE);
        }
    }

    void FWindowsApplication::Maximize() {
        if (IsRunning()) {
            ShowWindow(mWindow, SW_SHOWMAXIMIZED);
        }
    }

    void FWindowsApplication::Activate() {
        if (IsRunning() && !mPresentationPending) {
            SetForegroundWindow(mWindow);
            SetFocus(mWindow);
        }
    }

    void FWindowsApplication::SetTitle(std::wstring_view Title) {
        const std::wstring Text{Title};

        if (IsRunning()) {
            SetWindowTextW(mWindow, Text.c_str());
        }
    }

    void FWindowsApplication::SetDarkMode(bool Enabled) {
        const BOOL Dark{Enabled};

        if (IsRunning()) {
            DwmSetWindowAttribute(mWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &Dark, sizeof(Dark));
        }
    }

    void FWindowsApplication::SetCustomTitleBar(bool Enabled) {
        if (mCustomTitleBar == Enabled) {
            return;
        }

        mCustomTitleBar = Enabled;
        mTitleBarDragBounds = {};

        if (IsRunning()) {
            SetWindowPos(mWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    void FWindowsApplication::SetTitleBarDragBounds(const RECT& Bounds) {
        mTitleBarDragBounds = Bounds;
    }

    void FWindowsApplication::SetCenteredClientSize(Uint32 Width, Uint32 Height) {
        if (!IsRunning() || Width == 0 || Height == 0) {
            return;
        }

        const UINT Dpi{GetDpiForWindow(mWindow)};
        const float Scale{static_cast<float>(Dpi) / 96.0f};
        const HMONITOR Monitor{MonitorFromWindow(mWindow, MONITOR_DEFAULTTONEAREST)};
        MONITORINFO Info{};
        Info.cbSize = sizeof(Info);

        if (!GetMonitorInfoW(Monitor, &Info)) {
            return;
        }

        RECT Bounds{0, 0, static_cast<LONG>(std::lround(Width * Scale)), static_cast<LONG>(std::lround(Height * Scale))};
        if (!mCustomTitleBar) {
            AdjustWindowRectExForDpi(&Bounds, static_cast<DWORD>(GetWindowLongPtrW(mWindow, GWL_STYLE)), false, static_cast<DWORD>(GetWindowLongPtrW(mWindow, GWL_EXSTYLE)), Dpi);
        }
        const LONG WindowWidth{std::min(Bounds.right - Bounds.left, Info.rcWork.right - Info.rcWork.left)};
        const LONG WindowHeight{std::min(Bounds.bottom - Bounds.top, Info.rcWork.bottom - Info.rcWork.top)};
        SetWindowPos(mWindow, nullptr, Info.rcWork.left + (Info.rcWork.right - Info.rcWork.left - WindowWidth) / 2, Info.rcWork.top + (Info.rcWork.bottom - Info.rcWork.top - WindowHeight) / 2, WindowWidth, WindowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void FWindowsApplication::SetMinimumClientSize(Uint32 Width, Uint32 Height) {
        mMinimumWidth = Width;
        mMinimumHeight = Height;
    }

    bool FWindowsApplication::BeginPresentation() {
        if (!IsRunning()) {
            return false;
        }

        if (mPresentationPending) {
            return true;
        }

        const BOOL Cloaked{true};

        if (FAILED(DwmSetWindowAttribute(mWindow, DWMWA_CLOAK, &Cloaked, sizeof(Cloaked)))) {
            return false;
        }

        mPresentationPending = true;
        Hide();

        return true;
    }

    bool FWindowsApplication::RevealAfterFrame(const FFramePresenter& Presenter) {
        if (!IsRunning() || !mPresentationPending || !Presenter || IsIconic(mWindow)) {
            return false;
        }

        const HWND Window{mWindow};
        Show();
        RECT Before{};

        if (!GetClientRect(mWindow, &Before) || Before.right == 0 || Before.bottom == 0) {
            return false;
        }

        if (!Presenter() || !IsRunning() || mWindow != Window || !mPresentationPending || IsIconic(mWindow) || !IsWindowVisible(mWindow)) {
            return false;
        }

        RECT After{};

        if (!GetClientRect(mWindow, &After) || !EqualRect(&Before, &After) || FAILED(DwmFlush())) {
            return false;
        }

        const BOOL Cloaked{false};

        if (FAILED(DwmSetWindowAttribute(mWindow, DWMWA_CLOAK, &Cloaked, sizeof(Cloaked)))) {
            return false;
        }

        mPresentationPending = false;
        DwmFlush();

        return true;
    }

    bool FWindowsApplication::IsPresentationPending() const {
        return mPresentationPending;
    }

    bool FWindowsApplication::IsRunning() const {
        return mRunning && mWindow != nullptr;
    }

    void FWindowsApplication::WaitForMessages(Uint32 TimeoutMilliseconds) const {
        MsgWaitForMultipleObjects(0, nullptr, false, TimeoutMilliseconds, QS_ALLINPUT);
    }

    /**
     * @brief 기본 창 처리 전에 호출할 외부 메시지 핸들러를 교체합니다.
     */
    void Platform::FWindowsApplication::SetMessageHandler(FMessageHandler Handler) {
        mMessageHandler = std::move(Handler);
    }

    /**
     * @brief 플랫폼 메시지로 갱신하는 공통 입력 상태를 반환합니다.
     */
    Core::FInput& Platform::FWindowsApplication::GetInput() {
        return mInput;
    }

    /**
     * @brief 플랫폼 메시지로 갱신하는 공통 입력 상태를 반환합니다.
     */
    const Core::FInput& Platform::FWindowsApplication::GetInput() const {
        return mInput;
    }

    /**
     * @brief 네이티브 Win32 창 핸들을 반환합니다.
     */
    HWND Platform::FWindowsApplication::GetWindowHandle() const {
        return mWindow;
    }

    /**
     * @brief 가로 픽셀 수를 반환합니다.
     */
    Uint32 Platform::FWindowsApplication::GetWidth() const {
        return mWidth;
    }

    /**
     * @brief 세로 픽셀 수를 반환합니다.
     */
    Uint32 Platform::FWindowsApplication::GetHeight() const {
        return mHeight;
    }

    /**
     * @brief 창의 클라이언트 영역이 최소화된 크기인지 검사합니다.
     */
    bool Platform::FWindowsApplication::IsMinimized() const {
        return mWidth == 0 || mHeight == 0;
    }

    bool FWindowsApplication::IsMaximized() const {
        return mWindow != nullptr && IsZoomed(mWindow);
    }

    LRESULT FWindowsApplication::HitTestCustomFrame(LPARAM Position) const {
        POINT Cursor{GET_X_LPARAM(Position), GET_Y_LPARAM(Position)};
        RECT Bounds{};

        if (!ScreenToClient(mWindow, &Cursor) || !GetClientRect(mWindow, &Bounds) || !PtInRect(&Bounds, Cursor)) {
            return HTNOWHERE;
        }

        if (!IsMaximized()) {
            const UINT Dpi{GetDpiForWindow(mWindow)};
            const int BorderX{GetSystemMetricsForDpi(SM_CXSIZEFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi)};
            const int BorderY{GetSystemMetricsForDpi(SM_CYSIZEFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi)};
            const bool Left{Cursor.x < BorderX};
            const bool Right{Cursor.x >= Bounds.right - BorderX};
            const bool Top{Cursor.y < BorderY};
            const bool Bottom{Cursor.y >= Bounds.bottom - BorderY};

            if (Top) {
                return Left ? HTTOPLEFT : (Right ? HTTOPRIGHT : HTTOP);
            }

            if (Bottom) {
                return Left ? HTBOTTOMLEFT : (Right ? HTBOTTOMRIGHT : HTBOTTOM);
            }

            if (Left || Right) {
                return Left ? HTLEFT : HTRIGHT;
            }
        }

        return PtInRect(&mTitleBarDragBounds, Cursor) ? HTCAPTION : HTCLIENT;
    }

    /**
     * @brief Win32 입력·포커스 메시지를 공통 입력 상태에 반영합니다.
     */
    void Platform::FWindowsApplication::ProcessInputMessage(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
        const Core::EKey MouseKey{GetMouseKey(Message, WParam)};

        if (MouseKey != Core::EKey::None) {
            const bool Down{Message != WM_LBUTTONUP && Message != WM_RBUTTONUP && Message != WM_MBUTTONUP && Message != WM_XBUTTONUP};

            if (Down && GetFocus() != Window) {
                SetFocus(Window);
            }

            mInput.SetMousePosition({static_cast<float>(GET_X_LPARAM(LParam)), static_cast<float>(GET_Y_LPARAM(LParam))}, !IsMouseCaptured());
            mInput.SetKey(MouseKey, Down);

            if (Down && GetCapture() == nullptr && mInput.IsFocused()) {
                SetCapture(Window);
            } else if (!Down && GetCapture() == Window && !IsMouseDown(mInput) && !IsMouseCaptured()) {
                ReleaseCapture();
            }

            return;
        }

        switch (Message) {
        case WM_SETFOCUS:
            mInput.SetFocused(true);

            break;
        case WM_KILLFOCUS:
            ReleaseMouseCapture();
            mInput.SetFocused(false);

            if (GetCapture() == Window) {
                ReleaseCapture();
            }

            break;
        case WM_CANCELMODE:
            ReleaseMouseCapture();
            mInput.ReleaseMouse();

            break;
        case WM_CAPTURECHANGED:
            if (GetCapture() != Window) {
                ReleaseMouseCapture();
            }

            if (IsMouseDown(mInput)) {
                mInput.ReleaseMouse();
            }

            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            SetKeyboardKey(mInput, WParam, LParam, Message == WM_KEYDOWN || Message == WM_SYSKEYDOWN);

            break;
        case WM_MOUSEMOVE:
            mInput.SetMousePosition({static_cast<float>(GET_X_LPARAM(LParam)), static_cast<float>(GET_Y_LPARAM(LParam))}, !IsMouseCaptured());

            break;
        case WM_MOUSEWHEEL: {
            POINT Position{GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam)};

            if (ScreenToClient(Window, &Position)) {
                mInput.SetMousePosition({static_cast<float>(Position.x), static_cast<float>(Position.y)}, !IsMouseCaptured());
            }

            if (mInput.IsFocused()) {
                mInput.AddMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(WParam)) / WHEEL_DELTA);
            }

            break;
        }

        case WM_INPUT:
            if (mMouseCapture != nullptr) {
                mMouseCapture->ReadInput(LParam, mInput);
            }

            break;

        default:
            break;
        }
    }

    /**
     * @brief 창 메시지를 인스턴스에 전달하고 크기 및 종료 상태를 갱신합니다.
     */
    LRESULT CALLBACK Platform::FWindowsApplication::WindowProcedure(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
        auto* Application{reinterpret_cast<Platform::FWindowsApplication*>(GetWindowLongPtrW(Window, GWLP_USERDATA))};

        if (Message == WM_NCCREATE) {
            const auto* Creation{reinterpret_cast<const CREATESTRUCTW*>(LParam)};

            Application = static_cast<Platform::FWindowsApplication*>(Creation->lpCreateParams);

            SetWindowLongPtrW(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Application));
        }

        if (Application != nullptr) {
            Application->ProcessInputMessage(Window, Message, WParam, LParam);

            if (Application->mCustomTitleBar) {
                if (Message == WM_NCCALCSIZE) {
                    auto* Bounds{WParam != 0 ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(LParam)->rgrc[0] : reinterpret_cast<RECT*>(LParam)};

                    if (IsZoomed(Window)) {
                        const HMONITOR Monitor{MonitorFromRect(Bounds, MONITOR_DEFAULTTONEAREST)};
                        MONITORINFO Info{};
                        Info.cbSize = sizeof(Info);

                        RECT ClientBounds{};
                        if (GetMonitorInfoW(Monitor, &Info) && IntersectRect(&ClientBounds, Bounds, &Info.rcWork)) {
                            *Bounds = ClientBounds;
                        }
                    }

                    return 0;
                }

                if (Message == WM_NCHITTEST) {
                    return Application->HitTestCustomFrame(LParam);
                }

                if (Message == WM_DPICHANGED) {
                    const auto* Bounds{reinterpret_cast<const RECT*>(LParam)};
                    SetWindowPos(Window, nullptr, Bounds->left, Bounds->top, Bounds->right - Bounds->left, Bounds->bottom - Bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);

                    return 0;
                }
            }

            if (Message == WM_INPUT) {
                return DefWindowProcW(Window, Message, WParam, LParam);
            }

            if (Message == WM_GETMINMAXINFO && (Application->mMinimumWidth != 0 || Application->mMinimumHeight != 0)) {
                auto* Info{reinterpret_cast<MINMAXINFO*>(LParam)};
                const UINT Dpi{GetDpiForWindow(Window)};
                const float Scale{static_cast<float>(Dpi) / 96.0f};
                RECT Bounds{0, 0, static_cast<LONG>(std::lround(Application->mMinimumWidth * Scale)), static_cast<LONG>(std::lround(Application->mMinimumHeight * Scale))};
                if (!Application->mCustomTitleBar) {
                    AdjustWindowRectExForDpi(&Bounds, static_cast<DWORD>(GetWindowLongPtrW(Window, GWL_STYLE)), false, static_cast<DWORD>(GetWindowLongPtrW(Window, GWL_EXSTYLE)), Dpi);
                }

                if (Application->mMinimumWidth != 0) {
                    Info->ptMinTrackSize.x = Bounds.right - Bounds.left;
                }

                if (Application->mMinimumHeight != 0) {
                    Info->ptMinTrackSize.y = Bounds.bottom - Bounds.top;
                }

                return 0;
            }

            if (Message == WM_SIZE) {
                Application->mWidth = WParam == SIZE_MINIMIZED ? 0 : LOWORD(LParam);
                Application->mHeight = WParam == SIZE_MINIMIZED ? 0 : HIWORD(LParam);

                if (Application->mWidth == 0 || Application->mHeight == 0) {
                    Application->ReleaseMouseCapture();
                }
            }

            if (Message == WM_WINDOWPOSCHANGED) {
                RECT Client{};

                if (IsIconic(Window)) {
                    Application->mWidth = 0;
                    Application->mHeight = 0;
                } else if (GetClientRect(Window, &Client)) {
                    Application->mWidth = static_cast<Uint32>(Client.right);
                    Application->mHeight = static_cast<Uint32>(Client.bottom);
                }
            }

            if (Message == WM_DESTROY) {
                Application->ReleaseMouseCapture();
                Application->mRunning = false;
                Application->mWindow = nullptr;
                Application->mPresentationPending = false;

                const FMessageHandler Handler{Application->mMessageHandler};

                if (Handler) {
                    Handler(Window, Message, WParam, LParam);
                }

                if (Application->mQuitOnClose) {
                    PostQuitMessage(0);
                }

                return 0;
            }

            if (Application->mMessageHandler && Application->mMessageHandler(Window, Message, WParam, LParam)) {
                return 1;
            }

            if (Message == WM_SIZE) {
                return 0;
            }

            if (Message == WM_CLOSE) {
                DestroyWindow(Window);

                return 0;
            }
        }

        return DefWindowProcW(Window, Message, WParam, LParam);
    }

}
