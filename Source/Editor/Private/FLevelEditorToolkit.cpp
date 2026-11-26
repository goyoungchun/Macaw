/**
 * @file FLevelEditorToolkit.cpp
 * @brief 레벨 편집기의 뷰포트·탭·드로어와 공유 서비스를 구성하고 구동합니다.
 * @ingroup Editor
 */

#include "FLevelEditorToolkit.h"
#include "FEditorCommands.h"
#include "FEditorWorkspace.h"
#include "FEditorDrawerManager.h"
#include "FEditorSettings.h"
#include "FEditorShell.h"
#include "FAssetImportController.h"
#include "FContentBrowserWidget.h"
#include "FAssetEditorManager.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorViewport.h"
#include "Editor/FPlayInEditorSession.h"
#include "Editor/FEditorAssetCatalog.h"
#include "Editor/FAssetThumbnailCache.h"
#include "Editor/FEditorToolkitHost.h"
#include "EditorStyle.h"
#include "Engine/FProject.h"
#include "Engine/UWorld.h"
#include "Engine/ULevel.h"
#include "Engine/AActor.h"
#include "Engine/Component/ULightComponentBase.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Platform/Window.h"
#include "Core/Types.h"
#include <shellapi.h>
#include <imgui_internal.h>

using namespace Editor;

FLevelEditorToolkit::FLevelEditorToolkit(FEditorContext* Context, FEditorViewport* Viewport, FPlayInEditorSession* PlaySession)
	: mContext{Context},
	  mViewport{Viewport},
	  mPlaySession{PlaySession},
	  mViewportLayout{Context, Viewport, PlaySession},
	  mTopLevelTab{"###LevelEditor", false, EEditorTabRole::TopLevel},
	  mTabs{mTopLevelTab.GetToolkitHost().GetTabManager()},
	  mPlaceActorsWidget{Context},
	  mOutlinerWidget{Context},
	  mDetailsWidget{Context},
	  mConsoleWidget{Context},
	  mSequencerWidget{Context},
	  mAnimGraphWidget{Context} {
}

FLevelEditorToolkit::~FLevelEditorToolkit() {
    Shutdown();
}

bool FLevelEditorToolkit::Initialize(RenderCore::IRendererModule* Renderer, Platform::FWindowsApplication* Platform, const Core::FString& IniPath) {
    mPlatform = Platform;
    mUIScale.Initialize(16.0f);
    mUIScale.Update(*Platform);
    if (!mViewportLayout.Initialize(Renderer)) {
        return false;
    }

    mAssetCatalog = std::make_unique<FEditorAssetCatalog>(mContext->GetProject()->GetAssets());
    mThumbnails = std::make_unique<FAssetThumbnailCache>(*mAssetCatalog, Renderer);
    mWorkspace = std::make_unique<FEditorWorkspace>(mContext, mTopLevelTab, mUIScale);
    mCommands = std::make_unique<FEditorCommands>(mContext, mPlaySession, mViewportLayout, Platform, mMessages);
    mAssetEditors = std::make_unique<FAssetEditorManager>(mContext, Platform, *mAssetCatalog, *mThumbnails, *mWorkspace, *mCommands, mMessages);
    mImports = std::make_unique<FAssetImportController>(mContext, Platform);
    RegisterTabs();
    mDrawers = std::make_unique<FEditorDrawerManager>(mTabs, mUIScale, *mWorkspace);
    mDrawers->RegisterDrawer("Content", "Content Drawer", 380.0f);
    mDrawers->RegisterDrawer("Console", "Output Log", 300.0f);
    mSettings = std::make_unique<FEditorSettings>(mContext, Platform, mPlaySession, mViewportLayout, *mWorkspace, mTabs, *mDrawers, mUIScale, IniPath);
    mShell = std::make_unique<FEditorShell>(mContext, mViewport, mPlaySession, Platform, mViewportLayout, mTabs, *mWorkspace, *mSettings, *mCommands, *mImports, *mAssetEditors, *mDrawers, mUIScale);
    ConnectServices();
    mSettings->InitializeSettings();
    mBrowser->LoadContentIcons(Renderer);
    mShell->LoadBranding(Renderer);
    mBrowser->RefreshContent();
    mInitialized = true;
    if (mContext->GetWorld() != nullptr && mContext->GetWorld()->GetPersistentLevel() != nullptr) {
        for (const auto& Actor : mContext->GetWorld()->GetPersistentLevel()->GetActors()) {
            for (const auto& Component : Actor->GetComponents()) {
                if (auto* Light{CoreUObject::Cast<Engine::ULightComponentBase>(Component.get())}) {
                    Light->SynchronizeLight();
                }
            }
        }
    }

    DragAcceptFiles(Platform->GetWindowHandle(), TRUE);
    return true;
}

void FLevelEditorToolkit::RegisterTabs() {
    mTopLevelTab.SetIcon(EEditorIcon::World, IM_COL32(255, 170, 60, 255));
    mTopLevelTab.GetToolkitHost().AddWidget("Viewport", "Viewport", [this](float DeltaTime) {
        mViewportLayout.DrawContents(mPlatform->GetInput(), DeltaTime);
    },
        EEditorDockRegion::Center, false, mViewport->GetName());
    FEditorTab* ViewportTab{mTabs.FindTab("Viewport")};
    ViewportTab->SetWindowFlags(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
    ViewportTab->SetContentPadding({});
    ViewportTab->SetAutoHideTabBar(true);

    mTabs.RegisterWidget("PlaceActors", "Place Actors", mPlaceActorsWidget, EEditorDockRegion::Left, EEditorTabRole::Panel, false);
    mTabs.RegisterWidget("Outliner", "Outliner", mOutlinerWidget, EEditorDockRegion::Outliner, EEditorTabRole::Panel, false, "Outliner###World Outliner");
    mTabs.RegisterWidget("Details", "Details", mDetailsWidget, EEditorDockRegion::Details, EEditorTabRole::Panel, false);
    mTabs.RegisterWidget("Console", "Output Log", mConsoleWidget, EEditorDockRegion::Bottom, EEditorTabRole::Drawer, true, "Output Log###Console");
    mTabs.RegisterWidget("Sequencer", "Sequencer", mSequencerWidget);
    mTabs.RegisterWidget("AnimGraph", "Anim Graph", mAnimGraphWidget);
    mTabs.RegisterTabSpawner("Content", "Content Browser", [this]() {
        return std::make_unique<FContentBrowserWidget>(mContext, mPlatform, *mAssetCatalog, *mThumbnails, mUIScale, *mWorkspace, *mImports, *mAssetEditors);
    },
        EEditorTabRole::Drawer, true, "Content Browser###Content");
    mBrowser = static_cast<FContentBrowserWidget*>(mTabs.SpawnTab("Content"));
    mTabs.FindTab("PlaceActors")->SetIcon(EEditorIcon::Add);
    mTabs.FindTab("Outliner")->SetIcon(EEditorIcon::Outliner);
    mTabs.FindTab("Details")->SetIcon(EEditorIcon::Details);
    mTabs.FindTab("Console")->SetIcon(EEditorIcon::Log);
    mTabs.FindTab("Sequencer")->SetIcon(EEditorIcon::Sequence);
    mTabs.FindTab("AnimGraph")->SetIcon(EEditorIcon::Component);
    mTabs.FindTab("Content")->SetIcon(EEditorIcon::Folder);
    mTabs.SetDefaultVisible("PlaceActors", false);
    mTabs.SetDefaultVisible("Sequencer", false);
    mTabs.SetDefaultVisible("AnimGraph", false);
}

void FLevelEditorToolkit::ConnectServices() {
    mContext->SetSequencerOpener([this](CoreUObject::TObjectRef<Engine::ALevelSequenceActor> Actor) {
        mSequencerWidget.SetActor(Actor);
        mTabs.TryInvokeTab("Sequencer");
    });
    mViewport->SetPlayInputCaptureHandlers([this](const FVector2& Origin, const FVector2& Size) {
        POINT Minimum{static_cast<LONG>(Origin.x), static_cast<LONG>(Origin.y)};
        POINT Maximum{static_cast<LONG>(Origin.x + Size.x), static_cast<LONG>(Origin.y + Size.y)};
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
            ScreenToClient(mPlatform->GetWindowHandle(), &Minimum);
            ScreenToClient(mPlatform->GetWindowHandle(), &Maximum);
        }
        const RECT Bounds{Minimum.x, Minimum.y, Maximum.x, Maximum.y};
        return mPlatform->CaptureMouse(Bounds);
    },
        [this]() {
            mPlatform->ReleaseMouseCapture();
        });
    mDrawers->SetChangedHandler([this]() {
        mSettings->MarkDirty();
    });
    mWorkspace->SetToolbar([this]() {
        mShell->DrawEditorToolbar();
    });
    mWorkspace->SetLayoutChanged([this]() {
        mSettings->MarkDirty();
    });
    mCommands->SetContentChanged([this]() {
        mBrowser->MarkRefresh();
    });
    mContext->SetSceneSavedHandler([this]() {
        mCommands->CaptureProjectThumbnail();
    });
    mCommands->SetCollectAssetSavePrompts([this]() {
        return mAssetEditors->CollectSavePrompts();
    });
    mAssetEditors->SetChangedHandler([this]() {
        mBrowser->MarkRefresh();
    });
    mBrowser->SetCatalogChanged([this]() {
        mPlaceActorsWidget.InvalidateIcons();
    });
    mImports->SetDirectoryProvider([this]() {
        return mBrowser->GetDirectory();
    });
    mImports->SetImportedHandler([this](const std::filesystem::path& Path, Uint32 Count, const Core::FString& Error) {
        mBrowser->OnImported(Path, Count, Error);
    });
    mDetailsWidget.SetAssetServices(mAssetCatalog.get(), mThumbnails.get());
    mDetailsWidget.SetAssetNavigation([this]() {
        return mBrowser->GetSelectedPath();
    },
        [this](const std::filesystem::path& Path) {
            mBrowser->Reveal(Path);
        });
    mDetailsWidget.SetAnimGraphOpener([this]() {
        mTabs.TryInvokeTab("AnimGraph");
    });
    EditorStyle::SetAssetBrowseHandler([this](const std::filesystem::path& Path) {
        mBrowser->Reveal(Path);
    });
}

void FLevelEditorToolkit::ApplyAppearance() {
    mSettings->ApplyAppearance();
}

void FLevelEditorToolkit::Tick(float DeltaTime) {
    mViewportLayout.BeginFrame();
    mDrawers->Tick();
    mDetailsWidget.FinishInactiveSceneEdit();
    mThumbnails->BeginFrame();
    mImports->BeginFrame();
    mBrowser->Tick();
    mSettings->Tick(DeltaTime);
    mContext->TickClothGridTest(DeltaTime);
    mContext->TickClothSimTest(DeltaTime);
}

void FLevelEditorToolkit::Draw(float DeltaTime) {
    mShell->DrawMenu();
    mShell->DrawStatusBar();
    mWorkspace->BuildLayout();
    FEditorContext* Inspection{mPlaySession->IsActive() ? mPlaySession->GetInspectionContext() : mContext};
    mOutlinerWidget.SetEditorContext(Inspection);
    mDetailsWidget.SetEditorContext(Inspection);
    mAnimGraphWidget.SetEditorContext(Inspection);
    mTopLevelTab.Draw(DeltaTime);
    mDetailsWidget.FinishInactiveSceneEdit();
    FStatGraphs& Graphs{mContext->GetStatGraphs()};
    GetActiveViewport()->GetStatistics()->DrawPanel(mSettings->GetStatisticsVisibility(), Graphs);
    Graphs.Draw();
    mSettings->DrawSettings();
    mImports->DrawImportOptions();
    mAssetEditors->Draw(DeltaTime);
    mDrawers->Draw(DeltaTime);
    mBrowser->DrawDragPreview();

    if (mCloseRequested) {
        mCloseRequested = false;
        mCommands->RequestExit();
    }

    mMessages.Draw();
    mPlaySession->ProcessInput(DeltaTime, !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup));
}

void FLevelEditorToolkit::Render(float DeltaTime) {
    mViewportLayout.Draw(DeltaTime);
    mPlaySession->DrawPlayWindow();
    mShell->DrawShadowAtlas();
    mDrawers->FinishDragFrame();
}

void FLevelEditorToolkit::RequestClose() {
    if (!mMessages.IsOpen()) {
        mCloseRequested = true;
    }
}

void FLevelEditorToolkit::QueueFileDrop(void* Handle) {
    if (mImports != nullptr) {
        mImports->QueueFileDrop(Handle);
    }
}

bool FLevelEditorToolkit::WantsExit() const {
    return mCommands == nullptr || mCommands->WantsExit();
}

FEditorViewport* FLevelEditorToolkit::GetActiveViewport() const {
    return mViewportLayout.GetActiveViewport();
}

void FLevelEditorToolkit::Shutdown() {
    if (mInitialized && mSettings != nullptr) {
        mSettings->SaveSettings();
    }

    if (mThumbnails != nullptr) {
        mThumbnails->FlushSavedThumbnails();
    }

    mViewport->SetPlayInputCaptureHandlers({}, {});
    mContext->SetSceneSavedHandler({});
    mContext->SetSequencerOpener({});
    EditorStyle::SetAssetBrowseHandler({});
    mDetailsWidget.SetAnimGraphOpener({});
    mDetailsWidget.SetAssetServices(nullptr, nullptr);
    mDetailsWidget.SetAssetNavigation({}, {});
    if (mPlatform != nullptr && mPlatform->GetWindowHandle() != nullptr) {
        DragAcceptFiles(mPlatform->GetWindowHandle(), FALSE);
        MSG Message{};
        while (PeekMessageW(&Message, mPlatform->GetWindowHandle(), WM_DROPFILES, WM_DROPFILES, PM_REMOVE)) {
            DragFinish(reinterpret_cast<HDROP>(Message.wParam));
        }
    }

    mMessages.Reset();
    mCloseRequested = false;
    mPlaceActorsWidget.ClearIcons();
    mShell.reset();
    mSettings.reset();
    mDrawers.reset();
    mTabs.Clear();
    mBrowser = nullptr;
    mAssetEditors.reset();
    mImports.reset();
    mCommands.reset();
    mWorkspace.reset();
    mThumbnails.reset();
    mAssetCatalog.reset();
    mViewportLayout.Shutdown();
    mPlatform = nullptr;
    mInitialized = false;
}
