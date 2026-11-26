/**
 * @file FProjectBrowser.cpp
 * @brief 최근 프로젝트 탐색·선택과 템플릿 기반 프로젝트 생성을 제공합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Editor/FProjectBrowser.h"
#include "Asset/UTexture2D.h"
#include "Core/Base/ErrorHandler.h"
#include "Core/FilePath.h"
#include "Editor/FProjectDialog.h"
#include "Platform/Window.h"
#include "Platform/WindowResources.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>

using namespace std::chrono_literals;

/**
 * @brief Win32 메시지를 ImGui 입력 백엔드에 전달합니다.
 * @param[in] Window 마우스 캡처 또는 메시지 처리 대상 Win32 창입니다.
 * @param[in] Message Win32 메시지 식별자입니다.
 * @param[in] WParam Win32 메시지의 첫 번째 매개변수입니다.
 * @param[in] LParam Win32 메시지의 두 번째 매개변수입니다.
 * @return 백엔드의 메시지 처리 결과입니다.
 */
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);

namespace Editor {

    /**
     * @brief FProjectBrowser 구현에 한정되는 내부 형식·상수·도우미 함수입니다.
     */
    namespace {

        /**
         * @brief 프로젝트 브라우저의 강조 색상입니다.
         */
        constexpr ImU32 Accent{IM_COL32(0, 122, 218, 255)};

        /**
         * @brief 프로젝트 브라우저의 보조 텍스트 색상입니다.
         */
        constexpr ImU32 Muted{IM_COL32(146, 149, 155, 255)};

        /**
         * @brief 프로젝트 브라우저의 기본 밝은 텍스트 색상입니다.
         */
        constexpr ImU32 White{IM_COL32(237, 239, 242, 255)};

        /**
         * @brief Windows 알려진 폴더 식별자를 파일 시스템 경로로 해석합니다.
         * @param[in] Folder 조회할 Windows 알려진 폴더 식별자입니다.
         * @return 폴더 경로이며 조회에 실패하면 빈 경로입니다.
         */
        std::filesystem::path KnownFolder(REFKNOWNFOLDERID Folder) {
            PWSTR Buffer{};

            if (FAILED(SHGetKnownFolderPath(Folder, 0, nullptr, &Buffer))) {
                return {};
            }

            const std::filesystem::path Path{Buffer};
            CoTaskMemFree(Buffer);

            return Path;
        }

        /**
         * @brief 최근 프로젝트 목록을 저장할 파일 경로를 계산합니다.
         * @return 사용자별 최근 프로젝트 목록 파일 경로입니다.
         */
        std::filesystem::path RecentProjectsFile() {
            const std::filesystem::path Directory{KnownFolder(FOLDERID_LocalAppData)};

            return Directory.empty() ? std::filesystem::path{} : Directory / "Macaw/RecentProjects.txt";
        }

        /**
         * @brief D3D11 텍스처 뷰 주소를 ImGui 텍스처 식별자로 변환합니다.
         * @param[in] Texture 미리 보기에 사용할 텍스처 또는 D3D11 텍스처 뷰입니다.
         * @return ImGui 이미지 그리기에 사용할 식별자입니다.
         */
        ImTextureID TextureId(ID3D11ShaderResourceView* Texture) {
            return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(Texture));
        }

        /**
         * @brief 에셋 또는 프로젝트의 미리 보기 이미지를 표시합니다.
         * @param[in] Draw 미리 보기 도형과 이미지를 기록할 ImGui 그리기 목록입니다.
         * @param[in] Texture 미리 보기에 사용할 텍스처 또는 D3D11 텍스처 뷰입니다.
         * @param[in] Mascot 기본 미리 보기에 사용할 마스코트 텍스처 뷰입니다.
         * @param[in] Min 그리기 영역의 왼쪽 위 화면 좌표입니다.
         * @param[in] Max 그리기 영역의 오른쪽 아래 화면 좌표입니다.
         */
        void DrawPreview(ImDrawList* Draw, ID3D11ShaderResourceView* Texture, ID3D11ShaderResourceView* Mascot, ImVec2 Min, ImVec2 Max) {
            Draw->AddRectFilledMultiColor(Min, Max, IM_COL32(30, 37, 46, 255), IM_COL32(34, 51, 63, 255), IM_COL32(21, 25, 32, 255), IM_COL32(24, 27, 34, 255));

            if (Texture != nullptr) {
                Microsoft::WRL::ComPtr<ID3D11Resource> Resource{};
                Microsoft::WRL::ComPtr<ID3D11Texture2D> Image{};
                Texture->GetResource(&Resource);
                ImVec2 Crop{1.0f, 1.0f};

                if (SUCCEEDED(Resource.As(&Image))) {
                    D3D11_TEXTURE2D_DESC Description{};
                    Image->GetDesc(&Description);
                    const float Aspect{static_cast<float>(Description.Width) / static_cast<float>(std::max(Description.Height, 1u))};
                    const float TargetAspect{(Max.x - Min.x) / (Max.y - Min.y)};
                    Crop = {Aspect > TargetAspect ? TargetAspect / Aspect : 1.0f, Aspect > TargetAspect ? 1.0f : Aspect / TargetAspect};
                }

                Draw->AddImage(TextureId(Texture), Min, Max, {(1.0f - Crop.x) * 0.5f, (1.0f - Crop.y) * 0.5f}, {(1.0f + Crop.x) * 0.5f, (1.0f + Crop.y) * 0.5f});
            } else if (Mascot != nullptr) {
                const float Size{std::min(Max.x - Min.x, Max.y - Min.y) * 0.91f};
                const ImVec2 Center{(Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f};
                Draw->AddImage(TextureId(Mascot), {Center.x - Size * 0.5f, Center.y - Size * 0.5f}, {Center.x + Size * 0.5f, Center.y + Size * 0.5f});
            }
        }

        /**
         * @brief 프로젝트 이름이 검색 문자열에 맞는지 대소문자를 무시하고 검사합니다.
         * @param[in] Name 검색 문자열과 비교할 프로젝트 이름입니다.
         * @param[in] Search 이름과 비교할 검색 문자열입니다.
         * @return 검색 조건에 맞으면 true입니다.
         */
        bool MatchesSearch(const Core::FString& Name, const char* Search) {
            Core::FString Haystack{Name};
            Core::FString Needle{Search};
            const auto Lower{[](unsigned char Character) {
                return static_cast<char>(std::tolower(Character));
            }};
            std::transform(Haystack.begin(), Haystack.end(), Haystack.begin(), Lower);
            std::transform(Needle.begin(), Needle.end(), Needle.begin(), Lower);

            return Haystack.find(Needle) != Core::FString::npos;
        }

        /**
         * @brief 프로젝트 정보의 레이블과 값을 지정한 배율로 표시합니다.
         * @param[in] Label 메뉴 또는 컨트롤에 표시할 레이블입니다.
         * @param[in] Value 프로젝트 속성에 표시할 문자열입니다.
         * @param[in] Scale UI 표시 크기에 적용할 배율입니다.
         */
        void DrawProperty(const char* Label, const char* Value, float Scale) {
            ImGui::TextDisabled("%s", Label);
            ImGui::SameLine(130.0f * Scale);
            ImGui::TextUnformatted(Value);
        }
    }

    FProjectBrowser::FProjectBrowser() {
        const Core::FString Location{Core::FilePath::ToUtf8(KnownFolder(FOLDERID_Documents) / "Macaw Projects")};
        std::copy_n(Location.data(), std::min(Location.size(), mLocation.size() - 1), mLocation.data());
        const Core::FString Name{"MyProject"};
        std::copy(Name.begin(), Name.end(), mProjectName.begin());
    }

    FProjectBrowser::~FProjectBrowser() = default;

    bool FProjectBrowser::Show(Platform::FWindowsApplication& Platform, Renderer::FRenderer& Renderer, std::filesystem::path& ProjectFile, bool& Create, const std::filesystem::path& TemplatesDirectory, std::filesystem::path& TemplateDirectory, const Core::FString& Error) {
        mCreatePage = Create;
        mSubmitted = false;
        mCancelled = false;
        mSelectedFile = ProjectFile;
        mSearch.fill(0);
        mPreviews.clear();
        mTemplatePicker.Load(TemplatesDirectory, TemplateDirectory, mTemplateError);

        if (Create && !ProjectFile.empty()) {
            const Core::FString Name{Core::FilePath::ToUtf8(ProjectFile.stem())};
            const Core::FString Location{Core::FilePath::ToUtf8(ProjectFile.parent_path().filename() == ProjectFile.stem() ? ProjectFile.parent_path().parent_path() : ProjectFile.parent_path())};
            mProjectName.fill(0);
            mLocation.fill(0);
            std::copy_n(Name.data(), std::min(Name.size(), mProjectName.size() - 1), mProjectName.data());
            std::copy_n(Location.data(), std::min(Location.size(), mLocation.size() - 1), mLocation.data());
        }

        LoadRecentProjects();

        if (!Platform.BeginPresentation()) {
            Core::ErrorHandler::Fatal("ProjectBrowser", "Cannot prepare the project browser window");
        }

        Platform.SetDarkMode(true);
        Platform.SetTitle(L"Macaw Project Browser");
        Platform.SetMinimumClientSize(1000, 640);
        Platform.Restore();
        Platform.SetCenteredClientSize(1280, 800);

        if (!Platform.PumpMessages()) {
            return false;
        }

        const HWND Window{Platform.GetWindowHandle()};

        if (!Renderer.InitializeUi(Window, Platform.GetWidth(), Platform.GetHeight())) {
            Core::ErrorHandler::Fatal("ProjectBrowser", "Cannot initialize the project browser renderer");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& Input{ImGui::GetIO()};
        Input.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        Input.IniFilename = nullptr;
        Input.LogFilename = nullptr;
        const HMODULE Module{GetModuleHandleW(nullptr)};
        const HRSRC FontResource{FindResourceW(Module, MAKEINTRESOURCEW(MacawSplashRegularFontResource), RT_RCDATA)};
        const HGLOBAL FontData{FontResource != nullptr ? LoadResource(Module, FontResource) : nullptr};
        void* FontBytes{FontData != nullptr ? LockResource(FontData) : nullptr};

        if (FontBytes != nullptr) {
            ImFontConfig Config{};
            Config.FontDataOwnedByAtlas = false;
            Input.Fonts->AddFontFromMemoryTTF(FontBytes, static_cast<int>(SizeofResource(Module, FontResource)), 16.0f, &Config);
        } else {
            Input.Fonts->AddFontDefault();
        }

        ImGui::StyleColorsDark();
        ImGuiStyle& Style{ImGui::GetStyle()};
        Style.WindowPadding = {0.0f, 0.0f};
        Style.FramePadding = {10.0f, 7.0f};
        Style.ItemSpacing = {10.0f, 12.0f};
        Style.ItemInnerSpacing = {8.0f, 6.0f};
        Style.FrameRounding = 2.0f;
        Style.ChildRounding = 3.0f;
        Style.FrameBorderSize = 1.0f;
        Style.ScrollbarSize = 10.0f;
        Style.Colors[ImGuiCol_WindowBg] = {0.125f, 0.125f, 0.125f, 1.0f};
        Style.Colors[ImGuiCol_ChildBg] = {0.105f, 0.105f, 0.105f, 1.0f};
        Style.Colors[ImGuiCol_Text] = {0.91f, 0.92f, 0.94f, 1.0f};
        Style.Colors[ImGuiCol_TextDisabled] = {0.56f, 0.58f, 0.61f, 1.0f};
        Style.Colors[ImGuiCol_Border] = {0.22f, 0.22f, 0.23f, 1.0f};
        Style.Colors[ImGuiCol_FrameBg] = {0.058f, 0.058f, 0.062f, 1.0f};
        Style.Colors[ImGuiCol_FrameBgHovered] = {0.10f, 0.11f, 0.12f, 1.0f};
        Style.Colors[ImGuiCol_FrameBgActive] = {0.08f, 0.11f, 0.15f, 1.0f};
        Style.Colors[ImGuiCol_Button] = {0.21f, 0.21f, 0.22f, 1.0f};
        Style.Colors[ImGuiCol_ButtonHovered] = {0.28f, 0.28f, 0.30f, 1.0f};
        Style.Colors[ImGuiCol_ButtonActive] = {0.10f, 0.34f, 0.53f, 1.0f};
        Style.Colors[ImGuiCol_Header] = {0.0f, 0.40f, 0.73f, 1.0f};
        Style.Colors[ImGuiCol_HeaderHovered] = {0.12f, 0.28f, 0.39f, 1.0f};
        Style.Colors[ImGuiCol_HeaderActive] = {0.0f, 0.44f, 0.78f, 1.0f};
        Style.Colors[ImGuiCol_NavCursor] = {0.14f, 0.65f, 0.95f, 1.0f};
        mUIScale.SetUserScale(1.0f);
        mUIScale.Initialize(16.0f);
        mUIScale.Update(Platform);
        LoadBranding(Renderer.GetNativeDevice());
        const bool Win32Ready{ImGui_ImplWin32_Init(Window)};
        const bool DxReady{Win32Ready && ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(Renderer.GetNativeDevice()), static_cast<ID3D11DeviceContext*>(Renderer.GetNativeContext()))};

        if (DxReady) {
            Platform.SetMessageHandler([](HWND Handle, UINT Message, WPARAM WParam, LPARAM LParam) {
                return ImGui_ImplWin32_WndProcHandler(Handle, Message, WParam, LParam) != 0;
            });

            const auto DrawFrame{[this, &Platform, &Renderer, &Error]() {
                Renderer.BeginFrame(Platform.GetWidth(), Platform.GetHeight());
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                mUIScale.Update(Platform);
                ImGui::NewFrame();
                Draw(Platform, Renderer, Error);
                ImGui::Render();
                Renderer.BeginUiRender();
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                return Renderer.PresentFrame();
            }};

            while (!mSubmitted && !mCancelled && Platform.PumpMessages()) {
                if (Platform.IsMinimized()) {
                    Platform.WaitForMessages(static_cast<Uint32>((16ms).count()));

                    continue;
                }

                if (Platform.IsPresentationPending()) {
                    if (Platform.RevealAfterFrame(DrawFrame)) {
                        Platform.Activate();
                    }
                } else {
                    DrawFrame();
                }
            }
        }

        Platform.SetMessageHandler({});
        mTemplatePicker.ReleasePreviews();
        mPreviews.clear();
        mMascot.Reset();

        if (DxReady) {
            ImGui_ImplDX11_Shutdown();
        }

        if (Win32Ready) {
            ImGui_ImplWin32_Shutdown();
        }

        ImGui::DestroyContext();
        Renderer.Shutdown();

        if (!DxReady) {
            Core::ErrorHandler::Fatal("ProjectBrowser", "Cannot initialize the project browser UI");
        }

        if (mSubmitted) {
            Create = mCreatePage;
            ProjectFile = mSelectedFile;

            if (Create) {
                TemplateDirectory = mTemplatePicker.GetSelectedDirectory();
            }
        }

        return mSubmitted;
    }

    void FProjectBrowser::LoadBranding(void* Device) {
        mMascot.Reset();
        const HMODULE Module{GetModuleHandleW(nullptr)};
        const HRSRC Resource{FindResourceW(Module, MAKEINTRESOURCEW(MacawBrandResource), RT_RCDATA)};
        const HGLOBAL Data{Resource != nullptr ? LoadResource(Module, Resource) : nullptr};
        const auto* Bytes{Data != nullptr ? static_cast<const Uint8*>(LockResource(Data)) : nullptr};
        Asset::UTexture2D Texture{};

        if (Bytes != nullptr && Texture.LoadFromMemory({Bytes, SizeofResource(Module, Resource)}, "Macaw.png", false) && Texture.CreateResources(static_cast<ID3D11Device*>(Device))) {
            mMascot = Texture.GetShaderResourceView();
        }
    }

    ID3D11ShaderResourceView* FProjectBrowser::GetProjectPreview(const std::filesystem::path& File, void* Device) {
        const auto Existing{std::find_if(mPreviews.begin(), mPreviews.end(), [&File](const FProjectPreview& Preview) {
            return Preview.mFile == File;
        })};

        if (Existing != mPreviews.end()) {
            return Existing->mTexture.Get();
        }

        FProjectPreview Preview{};
        Preview.mFile = File;

        for (const auto& Image : {File.parent_path() / "Saved/ProjectThumbnail.png", File.parent_path() / (File.stem().wstring() + L".png"), File.parent_path() / "Saved/AutoScreenshot.png", File.parent_path() / "Preview.png"}) {
            std::error_code Code{};

            if (!std::filesystem::is_regular_file(Image, Code)) {
                continue;
            }

            Asset::UTexture2D Texture{};

            if (Texture.LoadFromFile(Image, false) && Texture.CreateResources(static_cast<ID3D11Device*>(Device))) {
                Preview.mTexture = Texture.GetShaderResourceView();

                break;
            }
        }

        mPreviews.push_back(std::move(Preview));

        return mPreviews.back().mTexture.Get();
    }

    void FProjectBrowser::Draw(Platform::FWindowsApplication& Platform, Renderer::FRenderer& Renderer, const Core::FString& Error) {
        const float Scale{mUIScale.GetScale()};
        const ImGuiViewport* Viewport{ImGui::GetMainViewport()};
        const ImVec2 Size{Viewport->WorkSize};
        ImGui::SetNextWindowPos(Viewport->WorkPos);
        ImGui::SetNextWindowSize(Size);
        ImGui::Begin("Project Browser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 Origin{ImGui::GetWindowPos()};
        ImDrawList* Draw{ImGui::GetWindowDrawList()};
        Draw->AddRectFilled(Origin, {Origin.x + Size.x, Origin.y + 74.0f * Scale}, IM_COL32(25, 25, 26, 255));
        Draw->AddLine({Origin.x, Origin.y + 74.0f * Scale}, {Origin.x + Size.x, Origin.y + 74.0f * Scale}, IM_COL32(53, 53, 55, 255));

        if (mMascot != nullptr) {
            Draw->AddImage(TextureId(mMascot.Get()), {Origin.x + 20.0f * Scale, Origin.y + 4.0f * Scale}, {Origin.x + 84.0f * Scale, Origin.y + 68.0f * Scale});
        }

        Draw->AddText(ImGui::GetFont(), 27.0f * Scale, {Origin.x + 89.0f * Scale, Origin.y + 15.0f * Scale}, White, "MACAW");
        Draw->AddText(ImGui::GetFont(), 11.0f * Scale, {Origin.x + 91.0f * Scale, Origin.y + 46.0f * Scale}, Muted, "G A M E   E N G I N E");
        DrawSidebar();
        const float FooterHeight{mCreatePage ? 146.0f * Scale : 112.0f * Scale};
        ImGui::SetCursorPos({256.0f * Scale, 94.0f * Scale});
        ImGui::BeginChild("MainContent", {Size.x - 280.0f * Scale, Size.y - 114.0f * Scale - FooterHeight}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (mCreatePage) {
            DrawNewProject(Platform, Renderer.GetNativeDevice());
        } else {
            DrawOpenProject(Platform, Renderer.GetNativeDevice());
        }

        ImGui::EndChild();
        ImGui::SetCursorPos({0.0f, Size.y - FooterHeight});
        ImGui::BeginChild("ProjectFooter", {Size.x, FooterHeight}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        DrawFooter(Platform, Error);
        ImGui::EndChild();
        ImGui::End();
    }

    void FProjectBrowser::DrawSidebar() {
        const float Scale{mUIScale.GetScale()};
        ImGui::SetCursorPos({18.0f * Scale, 96.0f * Scale});
        ImGui::BeginChild("Categories", {214.0f * Scale, ImGui::GetWindowHeight() - 260.0f * Scale}, ImGuiChildFlags_None);
        ImGui::SetCursorPos({14.0f * Scale, 14.0f * Scale});
        ImGui::TextDisabled("PROJECTS");
        ImGui::SetCursorPos({10.0f * Scale, 46.0f * Scale});

        if (ImGui::Selectable("Recent Projects", !mCreatePage, ImGuiSelectableFlags_None, {194.0f * Scale, 38.0f * Scale})) {
            mCreatePage = false;
        }

        ImGui::SetCursorPos({10.0f * Scale, 96.0f * Scale});

        if (ImGui::Selectable("New Project", mCreatePage, ImGuiSelectableFlags_None, {194.0f * Scale, 38.0f * Scale})) {
            mCreatePage = true;
        }

        ImGui::EndChild();
    }

    void FProjectBrowser::DrawOpenProject(Platform::FWindowsApplication& Platform, void* Device) {
        const float Scale{mUIScale.GetScale()};
        const float Available{ImGui::GetContentRegionAvail().x};
        const float DetailsWidth{std::min(330.0f * Scale, Available * 0.38f)};
        const float GridWidth{Available - DetailsWidth - 24.0f * Scale};
        ImGui::BeginChild("ProjectGridColumn", {GridWidth, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushFont(nullptr, 23.0f * Scale);
        ImGui::TextUnformatted("Recent Projects");
        ImGui::PopFont();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##ProjectSearch", "Search projects...", mSearch.data(), mSearch.size());
        ImGui::BeginChild("ProjectGrid", {0.0f, -40.0f * Scale}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        const float CardWidth{140.0f * Scale};
        const float CardHeight{177.0f * Scale};
        const float Gap{14.0f * Scale};
        const int Columns{std::max(1, static_cast<int>((GridWidth - 16.0f * Scale + Gap) / (CardWidth + Gap)))};
        Index_t Visible{};

        for (const auto& Path : mRecentProjects) {
            const Core::FString Name{Core::FilePath::ToUtf8(Path.stem())};

            if (!MatchesSearch(Name, mSearch.data())) {
                continue;
            }

            ImGui::PushID(static_cast<int>(Visible));
            ImGui::SetCursorPos({static_cast<float>(Visible % Columns) * (CardWidth + Gap) + 2.0f, static_cast<float>(Visible / Columns) * (CardHeight + Gap) + 2.0f});
            const ImVec2 Min{ImGui::GetCursorScreenPos()};
            const ImVec2 Max{Min.x + CardWidth, Min.y + CardHeight};
            const bool Selected{mSelectedFile == Path};

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
            const bool Pressed{ImGui::Selectable("##Project", Selected, ImGuiSelectableFlags_AllowDoubleClick, {CardWidth, CardHeight})};
            ImGui::PopStyleVar();

            if (Pressed) {
                mSelectedFile = Path;
                mSubmitted = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            }

            const bool Hovered{ImGui::IsItemHovered()};
            ImGui::SetItemTooltip("%s", Core::FilePath::ToUtf8(Path).c_str());
            ImDrawList* Draw{ImGui::GetWindowDrawList()};
            Draw->AddRectFilled(Min, Max, Selected ? Accent : IM_COL32(48, 48, 50, 255), 2.0f);
            DrawPreview(Draw, GetProjectPreview(Path, Device), mMascot.Get(), {Min.x + 2.0f, Min.y + 2.0f}, {Max.x - 2.0f, Min.y + CardWidth - 2.0f});
            const ImVec4 TextBounds{Min.x + 9.0f * Scale, Min.y + CardWidth + 9.0f * Scale, Max.x - 8.0f * Scale, Max.y};
            Draw->AddText(ImGui::GetFont(), 14.0f * Scale, {TextBounds.x, TextBounds.y}, White, Name.c_str(), nullptr, 0.0f, &TextBounds);
            Draw->AddRect(Min, Max, Selected ? Accent : Hovered ? IM_COL32(122, 130, 140, 255) : IM_COL32(59, 59, 62, 255), 2.0f, 0, Selected ? 2.0f : 1.0f);
            ImGui::PopID();
            ++Visible;
        }

        if (Visible == 0) {
            ImGui::SetCursorPos({8.0f * Scale, 26.0f * Scale});
            ImGui::TextUnformatted(mRecentProjects.empty() ? "Your next project starts here." : "No matching projects.");
            ImGui::TextDisabled(mRecentProjects.empty() ? "Browse for a .mproject file or choose New Project." : "Try a different project name.");
        }

        ImGui::EndChild();
        ImGui::TextDisabled("%zu project%s", Visible, Visible == 1 ? "" : "s");
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 24.0f * Scale);
        ImGui::BeginChild("ProjectDetails", {DetailsWidth, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        const ImVec2 PreviewMin{ImGui::GetCursorScreenPos()};
        DrawPreview(ImGui::GetWindowDrawList(), mSelectedFile.empty() ? nullptr : GetProjectPreview(mSelectedFile, Device), mMascot.Get(), PreviewMin, {PreviewMin.x + DetailsWidth, PreviewMin.y + DetailsWidth * 0.60f});
        ImGui::Dummy({DetailsWidth, DetailsWidth * 0.60f + 4.0f * Scale});
        ImGui::PushFont(nullptr, 24.0f * Scale);
        ImGui::TextWrapped("%s", mSelectedFile.empty() ? "Select a project" : Core::FilePath::ToUtf8(mSelectedFile.stem()).c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("MACAW PROJECT");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", mSelectedFile.empty() ? "Open an existing world or create a new one from a game template." : "Double-click the project card, or choose Open to continue in the editor.");

        if (!mSelectedFile.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("PROJECT LOCATION");
            ImGui::TextWrapped("%s", Core::FilePath::ToUtf8(mSelectedFile.parent_path()).c_str());

            if (ImGui::Button("Show in Explorer", {-1.0f, 32.0f * Scale})) {
                ShellExecuteW(Platform.GetWindowHandle(), L"open", mSelectedFile.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }

        ImGui::EndChild();
    }

    void FProjectBrowser::DrawNewProject(Platform::FWindowsApplication&, void* Device) {
        const float Scale{mUIScale.GetScale()};
        const float Available{ImGui::GetContentRegionAvail().x};
        const float DetailsWidth{std::min(360.0f * Scale, Available * 0.42f)};
        ImGui::BeginChild("Templates", {Available - DetailsWidth - 24.0f * Scale, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::PushFont(nullptr, 23.0f * Scale);
        ImGui::TextUnformatted("New Project");
        ImGui::PopFont();
        ImGui::TextDisabled("Choose a template to get started");
        ImGui::Spacing();
        mTemplatePicker.Draw(Device);
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 24.0f * Scale);
        ImGui::BeginChild("TemplateDetails", {DetailsWidth, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        mTemplatePicker.DrawSelectedPreview(Device, DetailsWidth, DetailsWidth * 0.56f);
        ImGui::Spacing();
        ImGui::PushFont(nullptr, 24.0f * Scale);
        ImGui::TextUnformatted(mTemplatePicker.GetSelectedName().c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(mTemplatePicker.GetSelectedDescription().c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 112.0f * Scale));
        ImGui::SeparatorText("Project Defaults");
        DrawProperty("Target Platform", "Windows Desktop", Scale);
        DrawProperty("Renderer", "Direct3D 11", Scale);
        ImGui::EndChild();
    }

    void FProjectBrowser::DrawFooter(Platform::FWindowsApplication& Platform, const Core::FString& Error) {
        const float Scale{mUIScale.GetScale()};
        const float Width{ImGui::GetWindowWidth()};
        const ImVec2 Origin{ImGui::GetWindowPos()};
        ImGui::GetWindowDrawList()->AddLine(Origin, {Origin.x + Width, Origin.y}, IM_COL32(58, 58, 60, 255));
        ImGui::SetCursorPos({24.0f * Scale, 18.0f * Scale});

        if (mCreatePage) {
            ImGui::TextDisabled("Project Location");
            ImGui::SameLine(142.0f * Scale);
            ImGui::SetNextItemWidth(std::max(180.0f * Scale, Width - 620.0f * Scale));
            ImGui::InputText("##Location", mLocation.data(), mLocation.size());
            ImGui::SameLine();

            if (ImGui::Button("...##BrowseLocation", {34.0f * Scale, 0.0f})) {
                const auto Path{FProjectDialog::ChooseDirectory(Platform.GetWindowHandle())};

                if (!Path.empty()) {
                    const Core::FString Location{Core::FilePath::ToUtf8(Path)};
                    mLocation.fill(0);
                    std::copy_n(Location.data(), std::min(Location.size(), mLocation.size() - 1), mLocation.data());
                }
            }

            ImGui::SameLine(Width - 394.0f * Scale);
            ImGui::TextDisabled("Project Name");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(Width - ImGui::GetCursorPosX() - 24.0f * Scale);
            ImGui::InputText("##ProjectName", mProjectName.data(), mProjectName.size());
            ImGui::SetCursorPos({142.0f * Scale, 60.0f * Scale});
            const Core::FString Validation{GetCreationError()};
            const Core::FString Message{!Error.empty() ? Error : !mTemplateError.empty() ? mTemplateError : Validation};
            ImGui::PushTextWrapPos(Width - 24.0f * Scale);

            if (!Message.empty()) {
                ImGui::TextColored({1.0f, 0.43f, 0.39f, 1.0f}, "%s", Message.c_str());
            } else {
                ImGui::TextDisabled("%s", Core::FilePath::ToUtf8(GetNewProjectFile()).c_str());
            }

            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextDisabled("Project File");
            ImGui::SameLine(142.0f * Scale);
            ImGui::PushTextWrapPos(Width - 24.0f * Scale);
            ImGui::TextUnformatted(mSelectedFile.empty() ? "Select a recent project, or browse for a .mproject file." : Core::FilePath::ToUtf8(mSelectedFile).c_str());
            ImGui::PopTextWrapPos();

            if (!Error.empty()) {
                ImGui::SetCursorPos({142.0f * Scale, 44.0f * Scale});
                ImGui::TextColored({1.0f, 0.43f, 0.39f, 1.0f}, "%s", Error.c_str());
            }
        }

        ImGui::SetCursorPos({Width - (mCreatePage ? 224.0f : 344.0f) * Scale, ImGui::GetWindowHeight() - 52.0f * Scale});

        if (!mCreatePage) {
            if (ImGui::Button("Browse...", {110.0f * Scale, 32.0f * Scale})) {
                const auto Path{FProjectDialog::Choose(Platform.GetWindowHandle(), false)};

                if (!Path.empty()) {
                    mSelectedFile = Path;
                }
            }

            ImGui::SameLine();
        }

        std::error_code Code{};
        const bool CanSubmit{mCreatePage ? GetCreationError().empty() && mTemplateError.empty() && !mTemplatePicker.GetSelectedDirectory().empty() : !mSelectedFile.empty() && std::filesystem::is_regular_file(mSelectedFile, Code)};
        ImGui::BeginDisabled(!CanSubmit);
        ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.43f, 0.77f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.02f, 0.53f, 0.90f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.0f, 0.34f, 0.65f, 1.0f});

        if (ImGui::Button(mCreatePage ? "Create" : "Open", {110.0f * Scale, 32.0f * Scale})) {
            mSelectedFile = mCreatePage ? GetNewProjectFile() : mSelectedFile;
            mSubmitted = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("Cancel", {80.0f * Scale, 32.0f * Scale})) {
            mCancelled = true;
        }
    }

    std::filesystem::path FProjectBrowser::GetNewProjectFile() const {
        const std::filesystem::path Name{Core::FilePath::FromUtf8(mProjectName.data())};

        return Core::FilePath::FromUtf8(mLocation.data()) / Name / (Name.wstring() + L".mproject");
    }

    bool FProjectBrowser::IsProjectNameValid() const {
        const Core::FString Name{mProjectName.data()};

        if (Name.empty() || Name == "." || Name == ".." || Name.front() == ' ' || Name.back() == ' ' || Name.back() == '.') {
            return false;
        }

        for (const unsigned char Character : Name) {
            if (Character < 32 || Core::FString{"<>:\"/\\|?*"}.find(static_cast<char>(Character)) != Core::FString::npos) {
                return false;
            }
        }

        Core::FString Stem{Name.substr(0, Name.find('.'))};
        std::transform(Stem.begin(), Stem.end(), Stem.begin(), [](unsigned char Character) {
            return static_cast<char>(std::toupper(Character));
        });

        if (Stem == "CON" || Stem == "PRN" || Stem == "AUX" || Stem == "NUL" || (Stem.size() == 4 && (Stem.starts_with("COM") || Stem.starts_with("LPT")) && Stem[3] >= '1' && Stem[3] <= '9')) {
            return false;
        }

        return true;
    }

    Core::FString FProjectBrowser::GetCreationError() const {
        if (!IsProjectNameValid()) {
            return "Enter a valid project name. Reserved names and file path characters cannot be used.";
        }

        if (mLocation[0] == '\0' || !Core::FilePath::FromUtf8(mLocation.data()).is_absolute()) {
            return "Choose an absolute project location.";
        }

        std::error_code Code{};
        const auto Directory{GetNewProjectFile().parent_path()};

        if (std::filesystem::exists(Directory, Code) && (!std::filesystem::is_directory(Directory, Code) || !std::filesystem::is_empty(Directory, Code))) {
            return "The project folder already exists and is not empty. Choose a different name or location.";
        }

        return Code ? "The project location cannot be accessed." : Core::FString{};
    }

    void FProjectBrowser::LoadRecentProjects() {
        mRecentProjects.clear();
        std::ifstream Input{RecentProjectsFile(), std::ios::binary};
        Core::FString Line{};

        while (mRecentProjects.size() < 24 && std::getline(Input, Line)) {
            std::error_code Code{};
            const std::filesystem::path Path{Core::FilePath::FromUtf8(Line.c_str())};

            if (Path.extension() == L".mproject" && std::filesystem::is_regular_file(Path, Code) && std::find(mRecentProjects.begin(), mRecentProjects.end(), Path) == mRecentProjects.end()) {
                mRecentProjects.push_back(Path);
            }
        }

        if (mSelectedFile.empty() && !mRecentProjects.empty()) {
            mSelectedFile = mRecentProjects.front();
        }
    }

    void FProjectBrowser::RememberProject(const std::filesystem::path& ProjectFile) {
        FProjectBrowser Browser{};
        Browser.LoadRecentProjects();
        std::erase(Browser.mRecentProjects, ProjectFile);
        Browser.mRecentProjects.insert(Browser.mRecentProjects.begin(), ProjectFile);
        const std::filesystem::path File{RecentProjectsFile()};
        std::error_code Code{};

        if (File.empty()) {
            return;
        }

        std::filesystem::create_directories(File.parent_path(), Code);

        if (Code) {
            return;
        }

        std::ofstream Output{File, std::ios::binary | std::ios::trunc};

        for (Index_t Index{}; Index < std::min<std::size_t>(24, Browser.mRecentProjects.size()); ++Index) {
            Output << Core::FilePath::ToUtf8(Browser.mRecentProjects[Index]) << '\n';
        }
    }
}
