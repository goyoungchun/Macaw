#pragma once

#include "Editor/Landscape/FLandscapeWidget.h"

namespace Editor {
    class FEditorContext;
    class FEditorTabManager;

    enum class ELevelEditorMode {
        Select,
        Landscape
    };

    class FLevelEditorModeManager {
    public:
        FLevelEditorModeManager(Editor::FEditorContext* Context, Editor::FEditorTabManager& Tabs);

    public:
        void RegisterTabs();

        Editor::ELevelEditorMode GetActiveMode() const;
        void ActivateMode(Editor::ELevelEditorMode Mode);

    private:
        Editor::FEditorTabManager& mTabs;
        Editor::FLandscapeWidget mLandscapeWidget;
        Editor::ELevelEditorMode mActiveMode{Editor::ELevelEditorMode::Select};
    };
}
