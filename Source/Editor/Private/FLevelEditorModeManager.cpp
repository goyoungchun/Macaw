#include "FLevelEditorModeManager.h"
#include "Editor/FEditorTabManager.h"

using namespace Editor;

FLevelEditorModeManager::FLevelEditorModeManager(FEditorContext* Context, FEditorTabManager& Tabs)
    : mTabs{Tabs},
      mLandscapeWidget{Context} {
}

void FLevelEditorModeManager::RegisterTabs() {
    mActiveMode = ELevelEditorMode::Select;
    mTabs.RegisterWidget("Landscape", "Landscape", mLandscapeWidget, EEditorDockRegion::Left, EEditorTabRole::ModePanel, true);
    mTabs.FindTab("Landscape")->SetIcon(EEditorIcon::Landscape);
    mTabs.SetDefaultVisible("Landscape", false);
}

ELevelEditorMode FLevelEditorModeManager::GetActiveMode() const {
    return mActiveMode;
}

void FLevelEditorModeManager::ActivateMode(ELevelEditorMode Mode) {
    mActiveMode = Mode;

    if (Mode == ELevelEditorMode::Select) {
        mTabs.SetVisible("Landscape", false);

        return;
    }

    mTabs.FindTab("Landscape")->Open();
}
