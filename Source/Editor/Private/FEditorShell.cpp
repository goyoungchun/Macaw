/**
 * @file FEditorShell.cpp
 * @brief 주 메뉴·도구 모음·상태 표시줄과 실행 제어를 그립니다.
 * @ingroup Editor
 */

#include "FEditorShell.h"
#include "FEditorCommands.h"
#include "FEditorWorkspace.h"
#include "FEditorDrawerManager.h"
#include "FEditorSettings.h"
#include "FEditorViewportLayout.h"
#include "FAssetImportController.h"
#include "FAssetEditorManager.h"
#include "Editor/FEditorTabManager.h"
#include "Editor/FEditorTab.h"
#include "Editor/FEditorToolkitHost.h"
#include "Editor/FImGuiUIScale.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorViewport.h"
#include "Editor/FPlayInEditorSession.h"
#include "EditorStyle.h"
#include "Core/Stats.h"
#include "Engine/FProject.h"
#include "Engine/EPrimitiveType.h"
#include "Engine/AActor.h"
#include "Engine/UWorld.h"
#include "Engine/FWorldContext.h"
#include "Asset/UTexture2D.h"
#include "Platform/Window.h"
#include "Platform/WindowResources.h"
#include "RenderCore/IRendererModule.h"
#include "Core/Types.h"
#include <d3d11.h>
#include <imgui_internal.h>
#include <algorithm>
#include <span>

using namespace Editor;

FEditorShell::FEditorShell(FEditorContext* Context, FEditorViewport* Viewport, FPlayInEditorSession* PlaySession, Platform::FWindowsApplication* Platform, FEditorViewportLayout& ViewportLayout, FEditorTabManager& Tabs, FEditorWorkspace& Workspace, FEditorSettings& Settings, FEditorCommands& Commands, FAssetImportController& Imports, FAssetEditorManager& AssetEditors, FEditorDrawerManager& Drawers, FImGuiUIScale& UIScale)
	: mContext{Context},
	  mViewport{Viewport},
	  mPlaySession{PlaySession},
	  mPlatform{Platform},
	  mViewportLayout{ViewportLayout},
	  mTabs{Tabs},
	  mWorkspace{Workspace},
	  mSettings{Settings},
	  mCommands{Commands},
	  mImports{Imports},
	  mAssetEditors{AssetEditors},
	  mDrawers{Drawers},
	  mUIScale{UIScale},
	  mStatusCommand{Context},
	  mSaveButton{"SaveLevel", {}, [this]() {
          mCommands.SaveLevel(false);
      }},
	  mAddButton{"QuickAdd", {}, []() {
          ImGui::OpenPopup("QuickAddMenu");
      }},
	  mSequenceButton{"Cinematics", {}, []() {
          ImGui::OpenPopup("CinematicsMenu");
      }},
	  mSettingsButton{"Settings", "Settings", []() {
          ImGui::OpenPopup("SettingsMenu");
      }},
	  mPlayButton{"Play", {}, [this]() {
          if (mPlaySession->IsPaused()) {
              mPlaySession->SetPaused(false);
          } else {
              mCommands.RequestPlay();
          }
      }},
	  mPauseButton{"Pause", {}, [this]() {
          mPlaySession->SetPaused(true);
      }},
	  mStepButton{"Step", {}, [this]() {
          mPlaySession->Step();
      }},
	  mStopButton{"Stop", {}, [this]() {
          mPlaySession->Stop();
      }},
	  mSimulateButton{"Simulate", {}, [this]() {
          mPlaySession->Start(nullptr, FPlayInEditorSession::EMode::Simulate);
      }},
	  mPlayOptionsButton{"PlayOptions", {}, []() {
          ImGui::OpenPopup("PlayOptionsMenu");
      }} {
    const std::pair<FEditorButtonWidget*, EEditorIcon> Buttons[]{{&mSaveButton, EEditorIcon::Save}, {&mAddButton, EEditorIcon::Add}, {&mSequenceButton, EEditorIcon::Cinematics}, {&mPlayButton, EEditorIcon::Play}, {&mPauseButton, EEditorIcon::Pause}, {&mStepButton, EEditorIcon::Step}, {&mStopButton, EEditorIcon::Stop}, {&mSimulateButton, EEditorIcon::Simulate}, {&mPlayOptionsButton, EEditorIcon::More}};

    for (const auto& [Button, Icon] : Buttons) {
        Button->SetStyle(EEditorButtonStyle::Icon);
        Button->SetIcon(Icon);
    }

    mAddButton.SetDropdown(true);
    mSequenceButton.SetDropdown(true);
    mSettingsButton.SetStyle(EEditorButtonStyle::Label);
    mSettingsButton.SetIcon(EEditorIcon::Settings);
    mSettingsButton.SetDropdown(true);
    mSaveButton.SetToolTip("현재 레벨 저장 (Ctrl+S)");
    mAddButton.SetToolTip("프로젝트에 빠르게 추가");
    mSequenceButton.SetToolTip("시퀀스 메뉴");
    mPauseButton.SetToolTip("일시 정지");
    mStepButton.SetToolTip("한 프레임 진행");
    mStopButton.SetToolTip("중지 (Esc)");
    mSimulateButton.SetToolTip("시뮬레이션 실행");
    mPlayOptionsButton.SetToolTip("PIE 실행 옵션");
}

FEditorShell::~FEditorShell() = default;

void FEditorShell::LoadBranding(RenderCore::IRendererModule* Renderer) {
    const HMODULE Module{GetModuleHandleW(nullptr)};
    const HRSRC Resource{FindResourceW(Module, MAKEINTRESOURCEW(MacawBrandResource), RT_RCDATA)};
    const HGLOBAL Data{Resource != nullptr ? LoadResource(Module, Resource) : nullptr};
    const auto* Bytes{Data != nullptr ? static_cast<const Uint8*>(LockResource(Data)) : nullptr};
    auto* Device{static_cast<ID3D11Device*>(Renderer->GetNativeDevice())};
    Asset::UTexture2D Texture{};

    if (Bytes != nullptr && Device != nullptr && Texture.LoadFromMemory({Bytes, SizeofResource(Module, Resource)}, "Macaw.png", false) && Texture.CreateResources(Device)) {
        mBrandLogo = Texture.GetShaderResourceView();
    }
}

void FEditorShell::DrawMenu() {
    FEditorToolkitHost* ActiveHost{mAssetEditors.GetActiveToolkitHost()};
    const float Scale{mUIScale.GetScale()};
    const float HeaderHeight{std::max(EditorStyle::MenuBarHeight * Scale, ImGui::GetFontSize() + 8.0f * Scale)};
    const float TabBarHeight{EditorStyle::GetTopLevelTabHeight()};

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x, std::max(ImGui::GetStyle().FramePadding.y, (HeaderHeight - ImGui::GetFontSize()) * 0.5f)});

    if (ImGui::BeginMainMenuBar()) {
        const ImVec2 Position{ImGui::GetWindowPos()};
        const float BrandHeight{ImGui::GetWindowHeight() + TabBarHeight};
        const float LogoSize{std::min(EditorStyle::BrandAreaWidth * Scale, BrandHeight) - 12.0f * Scale};
        const ImVec2 LogoMinimum{Position.x + (EditorStyle::BrandAreaWidth * Scale - LogoSize) * 0.5f, Position.y + (BrandHeight - LogoSize) * 0.5f};
        ImDrawList* Branding{ImGui::GetForegroundDrawList()};

        Branding->AddRectFilled(Position, ImVec2{Position.x + EditorStyle::BrandAreaWidth * Scale, Position.y + BrandHeight}, ImGui::GetColorU32(ImGuiCol_MenuBarBg));

        if (mBrandLogo != nullptr) {
            Branding->AddImage(reinterpret_cast<ImTextureID>(mBrandLogo.Get()), LogoMinimum, ImVec2{LogoMinimum.x + LogoSize, LogoMinimum.y + LogoSize});
        }

        ImGui::Dummy(ImVec2{(EditorStyle::BrandAreaWidth - 8.0f) * Scale, ImGui::GetFontSize()});

        if (ImGui::BeginMenu("File")) {
            const bool CanSave{!mContext->IsSceneEditActive()};

            if (ActiveHost != nullptr) {
                if (ImGui::MenuItem("Find Asset in Content Browser")) {
                    mAssetEditors.BrowseActiveAsset();
                }

                ImGui::Separator();
            }

            ImGui::BeginDisabled(!mContext->CanEditWorld());

            if (ImGui::MenuItem("New Level", "Ctrl+N")) {
                mCommands.NewLevel();
            }

            if (ImGui::MenuItem("Open Level...", "Ctrl+O")) {
                mCommands.OpenLevel();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save Current Level", "Ctrl+S", false, CanSave)) {
                mCommands.SaveLevel(false);
            }

            if (ImGui::MenuItem("Save Current Level As...", "Ctrl+Alt+S", false, CanSave)) {
                mCommands.SaveLevel(true);
            }

            if (ImGui::MenuItem("Set Current Level as Startup", nullptr, false, CanSave && !mContext->GetScenePath().empty())) {
                mCommands.SetCurrentLevelAsStartup();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Import Into Level...", nullptr, false, mContext->GetProject() != nullptr)) {
                mImports.ImportAsset();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("New Project...")) {
                mCommands.ChooseProject(true);
            }

            if (ImGui::MenuItem("Open Project...")) {
                mCommands.ChooseProject(false);
            }

            if (ImGui::MenuItem("Package Project...", nullptr, false, mContext->GetProject() != nullptr)) {
                mCommands.ExportGame();
            }

            ImGui::EndDisabled();
            ImGui::Separator();

            if (ImGui::MenuItem("Exit")) {
                mCommands.RequestExit();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, mContext->CanUndo())) {
                mContext->Undo();
            }

            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, mContext->CanRedo())) {
                mContext->Redo();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Editor Preferences...", "Ctrl+,")) {
                mSettings.OpenSettings(0);
            }

            if (ImGui::MenuItem("Project Settings...")) {
                mSettings.OpenSettings(5);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            if (ActiveHost != nullptr) {
                ActiveHost->DrawWindowMenu();
                ImGui::Separator();

                if (ImGui::BeginMenu("Level Editor")) {
                    mTabs.DrawMenu();
                    ImGui::EndMenu();
                }
            } else {
                mTabs.DrawMenu();
            }

            ImGui::MenuItem("Statistics", nullptr, &mSettings.GetStatisticsVisibility());

            if (ImGui::BeginMenu("Stat Overlay")) {
                Editor::FViewportStatistics* Statistics{mViewportLayout.GetActiveViewport()->GetStatistics()};

                for (const Core::FString& Group : Core::FStats::Get().GetGroups()) {
                    if (ImGui::MenuItem(Group.c_str(), nullptr, Statistics->IsEnabled(Group))) {
                        Core::FString Output{};

                        Statistics->ExecuteCommand("stat " + Group, Output);
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Load Layout")) {
                if (ImGui::MenuItem(ActiveHost != nullptr ? "Default Asset Editor Layout" : "Default Editor Layout")) {
                    if (ActiveHost != nullptr) {
                        ActiveHost->GetTabManager().RestoreDefaults();
                        ActiveHost->RequestLayoutReset();
                    } else {
                        mWorkspace.SetLayoutVersion(0);
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        const Core::FString& Project{mContext->GetProject()->GetName()};
        const float ControlsWidth{138.0f * Scale};
        const float ControlsLeft{ImGui::GetWindowWidth() - ControlsWidth};
        const float DragLeft{ImGui::GetCursorPosX()};
        const RECT DragBounds{static_cast<LONG>(DragLeft), 0, static_cast<LONG>(ControlsLeft), static_cast<LONG>(ImGui::GetWindowHeight())};

        mPlatform->SetTitleBarDragBounds(DragBounds);

        if (ControlsLeft - DragLeft > ImGui::CalcTextSize(Project.c_str()).x + 24.0f * Scale) {
            ImGui::SameLine(ControlsLeft - ImGui::CalcTextSize(Project.c_str()).x - 16.0f * Scale);
            ImGui::TextDisabled("%s", Project.c_str());
        }

        DrawWindowControls();
        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar();

    using Core::EKey;

    const Core::FInput& Input{mPlatform->GetInput()};

    if (Input.IsDown(EKey::Control) && ImGui::GetTopMostPopupModal() == nullptr) {
        if (Input.WasPressed(EKey::Space) && !mAssetEditors.IsSourceEditorFocused()) {
            mDrawers.ToggleDrawer("Content");
        } else if (Input.WasPressed(EKey::Comma) && !ImGui::GetIO().WantTextInput) {
            mSettings.OpenSettings(0);
        }
    }

    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
        if (Input.IsDown(EKey::Alt) && Input.WasPressed(EKey::P) && mViewportLayout.CanChangeLayout()) {
            mCommands.RequestPlay();
        } else if (mPlaySession->IsActive() && !mDrawers.HasOpenDrawer() && Input.WasPressed(EKey::Escape)) {
            mPlaySession->Stop();
        }
    }

    if (Input.IsDown(EKey::Control) && !ImGui::GetIO().WantTextInput && ImGui::GetTopMostPopupModal() == nullptr && mContext->CanEditWorld()) {
        const bool AssetEditorFocused{mAssetEditors.IsFocused()};
        const ImGuiWindow* Focused{ImGui::GetCurrentContext()->NavWindow};
        const ImGuiWindow* Root{Focused != nullptr ? Focused->RootWindow : nullptr};
        const bool SceneFocused{Root != nullptr && (Root == ImGui::FindWindowByName(mViewport->GetName().c_str()) || Root == ImGui::FindWindowByName(mTabs.FindTab("Outliner")->GetName().c_str()) || Root == ImGui::FindWindowByName(mTabs.FindTab("Details")->GetName().c_str()) || Root == ImGui::FindWindowByName(mTabs.FindTab("PlaceActors")->GetName().c_str()))};

        if (SceneFocused && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
            if (Input.WasPressed(EKey::Y) || (Input.WasPressed(EKey::Z) && Input.IsDown(EKey::Shift))) {
                mContext->Redo();
            } else if (Input.WasPressed(EKey::Z)) {
                mContext->Undo();
            }
        }

        if (Input.WasPressed(EKey::S) && !AssetEditorFocused) {
            mCommands.SaveLevel(Input.IsDown(EKey::Alt));
        } else if (Input.WasPressed(EKey::O)) {
            mCommands.OpenLevel();
        } else if (Input.WasPressed(EKey::N)) {
            mCommands.NewLevel();
        }
    }
}

void FEditorShell::DrawWindowControls() {
    const float Scale{mUIScale.GetScale()};
    const float Width{46.0f * Scale};
    const float Height{ImGui::GetWindowHeight()};
    const ImVec2 Origin{ImGui::GetWindowPos()};
    const float Left{Origin.x + ImGui::GetWindowWidth() - Width * 3.0f};
    ImDrawList* Draw{ImGui::GetWindowDrawList()};

    for (int Index{}; Index < 3; ++Index) {
        const ImVec2 Minimum{Left + Width * Index, Origin.y};
        const ImVec2 Maximum{Minimum.x + Width, Minimum.y + Height};
        const ImVec2 Center{Minimum.x + Width * 0.5f, Minimum.y + Height * 0.5f};

        ImGui::SetCursorScreenPos(Minimum);
        ImGui::PushID(Index);

        const bool Pressed{ImGui::InvisibleButton("##WindowControl", ImVec2{Width, Height})};
        const bool Hovered{ImGui::IsItemHovered()};

        if (Hovered) {
            Draw->AddRectFilled(Minimum, Maximum, Index == 2 ? IM_COL32(196, 43, 50, 255) : ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));
        }

        const ImU32 Color{Index == 2 && Hovered ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text)};
        const float Radius{4.5f * Scale};
        const float Thickness{std::max(1.0f, Scale)};

        if (Index == 0) {
            Draw->AddLine(ImVec2{Center.x - Radius, Center.y}, ImVec2{Center.x + Radius, Center.y}, Color, Thickness);
        } else if (Index == 1) {
            if (mPlatform->IsMaximized()) {
                Draw->AddRect(ImVec2{Center.x - Radius + 2.0f * Scale, Center.y - Radius}, ImVec2{Center.x + Radius, Center.y + Radius - 2.0f * Scale}, Color, 0.0f, 0, Thickness);
                Draw->AddRectFilled(ImVec2{Center.x - Radius, Center.y - Radius + 2.0f * Scale}, ImVec2{Center.x + Radius - 2.0f * Scale, Center.y + Radius}, ImGui::GetColorU32(Hovered ? ImGuiCol_ButtonHovered : ImGuiCol_MenuBarBg));
                Draw->AddRect(ImVec2{Center.x - Radius, Center.y - Radius + 2.0f * Scale}, ImVec2{Center.x + Radius - 2.0f * Scale, Center.y + Radius}, Color, 0.0f, 0, Thickness);
            } else {
                Draw->AddRect(ImVec2{Center.x - Radius, Center.y - Radius}, ImVec2{Center.x + Radius, Center.y + Radius}, Color, 0.0f, 0, Thickness);
            }
        } else {
            Draw->AddLine(ImVec2{Center.x - Radius, Center.y - Radius}, ImVec2{Center.x + Radius, Center.y + Radius}, Color, Thickness);
            Draw->AddLine(ImVec2{Center.x + Radius, Center.y - Radius}, ImVec2{Center.x - Radius, Center.y + Radius}, Color, Thickness);
        }

        if (Pressed) {
            if (Index == 2) {
                PostMessageW(mPlatform->GetWindowHandle(), WM_CLOSE, 0, 0);
            } else {
                PostMessageW(mPlatform->GetWindowHandle(), WM_SYSCOMMAND, Index == 0 ? SC_MINIMIZE : (mPlatform->IsMaximized() ? SC_RESTORE : SC_MAXIMIZE), 0);
            }
        }

        ImGui::PopID();
    }
}

void FEditorShell::DrawEditorToolbar() {
    Editor::EditorStyle::PushViewportToolbar();

    const float Scale{mUIScale.GetScale()};

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0f * Scale, 4.0f * Scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f * Scale, 4.0f * Scale});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::GetTopLevelTabColor());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{});

    const float Height{ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f};

    if (ImGui::BeginChild("##EditorToolbar", ImVec2{0.0f, Height}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const bool CanEdit{mContext->CanEditWorld()};
        const float ButtonWidth{ImGui::GetFrameHeight() + 12.0f * Scale};

        mSaveButton.SetEnabled(CanEdit && !mContext->IsSceneEditActive());
        mSaveButton.SetSize({ButtonWidth, 0.0f});
        mSaveButton.Draw(0.0f);

        ImGui::SameLine();

        mAddButton.SetEnabled(CanEdit);
        mAddButton.SetSelected(ImGui::IsPopupOpen("QuickAddMenu"));
        mAddButton.SetSize({ButtonWidth, 0.0f});
        mAddButton.Draw(0.0f);

        ImGui::SetNextWindowPos(ImVec2{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f * Scale}, ImGuiCond_Appearing);

        if (EditorStyle::BeginPopup("QuickAddMenu")) {
            DrawAddMenu();
            EditorStyle::EndPopup();
        }

        ImGui::SameLine();

        mSequenceButton.SetSelected(ImGui::IsPopupOpen("CinematicsMenu"));
        mSequenceButton.SetSize({ButtonWidth, 0.0f});
        mSequenceButton.Draw(0.0f);

        ImGui::SetNextWindowPos(ImVec2{ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f * Scale}, ImGuiCond_Appearing);

        if (EditorStyle::BeginPopup("CinematicsMenu")) {
            if (ImGui::MenuItem("Open Sequencer", nullptr, mTabs.IsVisible("Sequencer"))) {
                mTabs.TryInvokeTab("Sequencer");
            }

            if (ImGui::MenuItem("Add Level Sequence Actor", nullptr, false, CanEdit)) {
                mContext->SpawnActorPreset("LevelSequenceActor");
            }

            EditorStyle::EndPopup();
        }

        ImGui::SameLine();
        DrawPlayControls();

        if (mPlaySession->IsActive()) {
            const bool Paused{mPlaySession->IsPaused()};
            const char* Status{mPlaySession->IsSimulating() ? (Paused ? "Simulation Paused" : "Simulating") : (Paused ? "Paused" : "Playing")};

            ImGui::SameLine(0.0f, 12.0f * Scale);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", Status);
        }


        const float SettingsWidth{ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Settings").x + 12.0f * Scale};

        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - SettingsWidth - ImGui::GetStyle().WindowPadding.x));

        mSettingsButton.Draw(0.0f);

        ImGui::SetNextWindowPos(ImVec2{ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 4.0f * Scale}, ImGuiCond_Appearing, ImVec2{1.0f, 0.0f});

        if (EditorStyle::BeginPopup("SettingsMenu")) {
            mSettings.DrawSettingsMenu();
            EditorStyle::EndPopup();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    Editor::EditorStyle::PopViewportToolbar();
}

void FEditorShell::DrawAddMenu() {
    ImGui::SeparatorText("Shapes");

    if (ImGui::MenuItem("Cube")) {
        mContext->SpawnPrimitive(Engine::EPrimitiveType::Cube);
    }

    if (ImGui::MenuItem("Sphere")) {
        mContext->SpawnPrimitive(Engine::EPrimitiveType::Sphere);
    }

    if (ImGui::MenuItem("Plane")) {
        mContext->SpawnPrimitive(Engine::EPrimitiveType::Plane);
    }

    ImGui::SeparatorText("Basic");

    if (ImGui::MenuItem("Camera Actor")) {
        mContext->SpawnCamera();
    }

    if (ImGui::MenuItem("Text")) {
        mContext->SpawnText();
    }

    if (ImGui::MenuItem("Billboard")) {
        mContext->SpawnBillboard();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Place Actors Panel", nullptr, mTabs.IsVisible("PlaceActors"))) {
        mTabs.TryInvokeTab("PlaceActors");
    }

    if (ImGui::MenuItem("Import Content...", nullptr, false, mContext->GetProject() != nullptr)) {
        mImports.ImportAsset();
    }
}

void FEditorShell::DrawStatusBar() {
    const float Scale{mUIScale.GetScale()};

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f * Scale, 3.0f * Scale});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0f * Scale, 2.0f * Scale});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});

    const float Height{ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f};
    const ImGuiWindowFlags Flags{ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing};

    if (ImGui::BeginViewportSideBar("##EditorStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, Height, Flags)) {
        const Editor::EditorStyle::EIcon FolderIcon{Editor::EditorStyle::EIcon::Folder};
        const Editor::EditorStyle::EIcon LogIcon{Editor::EditorStyle::EIcon::Log};

        ImGui::PushStyleColor(ImGuiCol_Button, mDrawers.IsDrawerOpen("Content") ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive) : ImVec4{});

        if (Editor::EditorStyle::LabelButton("Content Drawer", &FolderIcon, false, true, "콘텐츠 드로어 열기 / 닫기 (Ctrl+Space)")) {
            mDrawers.ToggleDrawer("Content");
        }

        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 4.0f * Scale);
        ImGui::PushStyleColor(ImGuiCol_Button, mDrawers.IsDrawerOpen("Console") ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive) : ImVec4{});

        if (Editor::EditorStyle::LabelButton("Output Log", &LogIcon, false, true, "출력 로그 열기 / 닫기")) {
            mDrawers.ToggleDrawer("Console");
        }

        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 10.0f * Scale);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Cmd");
        ImGui::SameLine(0.0f, 6.0f * Scale);
        mStatusCommand.Draw("StatusCommand", 300.0f * Scale);

        const bool Dirty{mContext->IsSceneDirty()};
        const char* Saved{Dirty ? "1 Unsaved" : "All Saved"};
        const float RightWidth{ImGui::CalcTextSize(Saved).x};

        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - RightWidth - ImGui::GetStyle().WindowPadding.x - 4.0f * Scale));
        ImGui::AlignTextToFramePadding();

        if (Dirty) {
            ImGui::TextColored({1.0f, 0.72f, 0.0f, 1.0f}, "%s", Saved);
        } else {
            ImGui::TextDisabled("%s", Saved);
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

void FEditorShell::DrawPlayControls() {
    ImGui::PushID("PIEControls");

    const float Scale{mUIScale.GetScale()};
    const bool Active{mPlaySession->IsActive()};
    const bool Paused{mPlaySession->IsPaused()};
    const bool CanStartSession{!Active && mViewportLayout.CanChangeLayout()};
    const bool CanStart{CanStartSession && mPlaySession->CanStart()};
    const bool Simulating{mPlaySession->IsSimulating()};
    const float Height{ImGui::GetFrameHeight()};
    const float Padding{2.0f * Scale};
    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const float Width{(Height - Padding * 2.0f) * 5.0f + Padding * 2.0f};
    const ImVec4 Background{ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg), ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.12f)};

    ImGui::BeginGroup();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2{Origin.x + Width, Origin.y + Height}, ImGui::GetColorU32(Background), 4.0f * Scale);
    ImGui::Dummy(ImVec2{Width, Height});
    ImGui::SetCursorScreenPos(ImVec2{Origin.x + Padding, Origin.y + Padding});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x, std::max(0.0f, ImGui::GetStyle().FramePadding.y - Padding)});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f * Scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});

    // 언리얼과 같이 재생 중에는 첫 버튼이 일시 정지로 바뀌고, 정지 버튼은 항상 같은 자리에 둡니다.
    if (Active && !Paused) {
        mPauseButton.Draw(0.0f);
    } else {
        const char* PlayTooltip{Paused ? (Simulating ? "시뮬레이션 재개" : "PIE 재개") : CanStart ? "PIE 실행 (Alt+P)" : "PIE를 실행하려면 활성 월드 카메라가 있고 에디터가 다른 작업 중이 아니어야 합니다"};

        mPlayButton.SetEnabled(Paused || CanStart);
        mPlayButton.SetToolTip(PlayTooltip);
        mPlayButton.Draw(0.0f);
    }

    ImGui::SameLine(0.0f, 0.0f);

    mStepButton.SetEnabled(Active && Paused);
    mStepButton.Draw(0.0f);

    ImGui::SameLine(0.0f, 0.0f);

    mStopButton.SetEnabled(Active);
    mStopButton.Draw(0.0f);

    ImGui::SameLine(0.0f, 0.0f);
    mSimulateButton.SetEnabled(CanStartSession && mPlaySession->CanStart(FPlayInEditorSession::EMode::Simulate));
    mSimulateButton.SetSelected(Simulating);
    mSimulateButton.Draw(0.0f);

    ImGui::SameLine(0.0f, 0.0f);

    mPlayOptionsButton.SetSelected(ImGui::IsPopupOpen("PlayOptionsMenu"));
    mPlayOptionsButton.Draw(0.0f);

    const ImVec2 MenuPosition{ImGui::GetItemRectMin().x, Origin.y + Height + 4.0f * Scale};

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndGroup();
    ImGui::SetNextWindowPos(MenuPosition, ImGuiCond_Appearing);

    if (EditorStyle::BeginPopup("PlayOptionsMenu")) {
        DrawPlayOptions();
        EditorStyle::EndPopup();
    }

    ImGui::PopID();
}

void FEditorShell::DrawPlayOptions() {
    FPlayInEditorSession::FOptions Options{mPlaySession->GetOptions()};
    bool Changed{};

    ImGui::BeginDisabled(mPlaySession->IsActive());
    ImGui::SeparatorText("Play Mode");

    if (ImGui::MenuItem("Editor Viewport", nullptr, !Options.mNewWindow)) {
        Options.mNewWindow = false;
        Changed = true;
    }

    if (ImGui::MenuItem("New Editor Window", nullptr, Options.mNewWindow)) {
        Options.mNewWindow = true;
        Changed = true;
    }

    ImGui::SeparatorText("Start Location");

    if (ImGui::MenuItem("Active World Camera", nullptr, !Options.mStartAtEditorCamera)) {
        Options.mStartAtEditorCamera = false;
        Changed = true;
    }

    if (ImGui::MenuItem("Current Editor Camera", nullptr, Options.mStartAtEditorCamera)) {
        Options.mStartAtEditorCamera = true;
        Changed = true;
    }

    ImGui::SeparatorText("New Window Size");
    ImGui::BeginDisabled(!Options.mNewWindow);

    Core::TStaticArray<int, 2> Size{static_cast<int>(Options.mWindowWidth), static_cast<int>(Options.mWindowHeight)};

    ImGui::SetNextItemWidth(240.0f * mUIScale.GetScale());

    if (ImGui::InputInt2("##PlayWindowSize", Size.data())) {
        Options.mWindowWidth = static_cast<Uint32>(std::clamp(Size[0], 320, 7680));
        Options.mWindowHeight = static_cast<Uint32>(std::clamp(Size[1], 180, 4320));
        Changed = true;
    }

    if (ImGui::BeginMenu("Resolution Presets")) {
        for (const Core::TStaticArray<Uint32, 2>& Preset : {Core::TStaticArray<Uint32, 2>{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}}) {
            const Core::FString Label{std::format("{} x {}", Preset[0], Preset[1])};

            if (ImGui::MenuItem(Label.c_str(), nullptr, Options.mWindowWidth == Preset[0] && Options.mWindowHeight == Preset[1])) {
                Options.mWindowWidth = Preset[0];
                Options.mWindowHeight = Preset[1];
                Changed = true;
            }
        }

        ImGui::EndMenu();
    }

    ImGui::EndDisabled();
    ImGui::SeparatorText("Input & Playback");
    Changed |= ImGui::Checkbox("Capture Input on Start", &Options.mCaptureInput);
    Changed |= ImGui::Checkbox("Start Paused", &Options.mStartPaused);
    ImGui::EndDisabled();

    if (Changed && mPlaySession->SetOptions(Options)) {
        mSettings.MarkDirty();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Alt+P: Play | Esc: Stop | Shift+F1: Release input");
}

void FEditorShell::DrawShadowAtlas() {
    if (!mContext->IsShadowAtlasOpen()) {
        return;
    }

    bool Open{true};

    ImGui::SetNextWindowSize({760.0f, 840.0f}, ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Shadow Atlas", &Open)) {
        const RenderCore::FShadowAtlasView Atlas{mContext->GetRenderer()->GetShadowAtlasView()};
        const RenderCore::FRenderStats Stats{mContext->GetRenderer()->GetStats()};

        ImGui::Text("%u x %u | D24S8 | %u maps", Atlas.mSize, Atlas.mSize, Stats.mShadowMaps);
        ImGui::Text("Resources: %.1f MiB | Allocated tiles: %.1f MiB", Stats.mShadowAtlasBytes / (1024.0 * 1024.0), Stats.mShadowAllocatedBytes / (1024.0 * 1024.0));
        ImGui::Text("Directional: %u | Point: %u | Spot: %u", Stats.mDirectionalLights, Stats.mPointLights, Stats.mSpotLights);
        ImGui::Text("Reduced: %u | Unallocated lights: %u | Skipped lights: %u | Cached tiles: %u", Stats.mShadowReducedLights, Stats.mShadowDroppedLights, Stats.mShadowSkippedLights, Stats.mShadowCachedTiles);
        ImGui::SliderFloat("Zoom", &mShadowAtlasZoom, 1.0f, 8.0f, "%.1fx");

        if (Atlas.mTexture != nullptr) {
            const float Size{std::max(256.0f, ImGui::GetContentRegionAvail().x - 20.0f) * mShadowAtlasZoom};

            if (ImGui::BeginChild("AtlasImage", {}, 0, ImGuiWindowFlags_HorizontalScrollbar)) {
                const ImVec2 Origin{ImGui::GetCursorScreenPos()};

                ImGui::Image(reinterpret_cast<ImTextureID>(Atlas.mTexture), {Size, Size});

                ImDrawList* Draw{ImGui::GetWindowDrawList()};
                Draw->AddRect(Origin, {Origin.x + Size, Origin.y + Size}, IM_COL32(255, 230, 0, 255));

                for (const RenderCore::FShadowAtlasTile& Tile : Atlas.mTiles) {
                    const ImVec2 Minimum{Origin.x + Tile.mRectangle.x * Size, Origin.y + Tile.mRectangle.y * Size};
                    const ImVec2 Maximum{Minimum.x + Tile.mRectangle.z * Size, Minimum.y + Tile.mRectangle.w * Size};

                    Draw->AddRect(Minimum, Maximum, IM_COL32(255, 230, 0, 255), 0.0f, 0, 1.5f);

                    if (ImGui::IsMouseHoveringRect(Minimum, Maximum)) {
                        ImGui::SetTooltip("맵 %u | %u x %u", Tile.mFace, Tile.mResolution, Tile.mResolution);
                    }
                }
            }

            ImGui::EndChild();
        } else {
            ImGui::TextWrapped("No active shadow maps. Enable Shadows in a lit viewport and enable CastShadows on a light.");
        }
    }

    ImGui::End();
    mContext->SetShadowAtlasOpen(Open);
}
