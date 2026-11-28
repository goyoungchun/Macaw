#pragma once

#include "Editor/Landscape/FLandscapeCreateSettings.h"
#include "Editor/Widget/FEditorContextWidget.h"

namespace Editor {
    class FLandscapeWidget final : public Editor::FEditorContextWidget {
    public:
        explicit FLandscapeWidget(Editor::FEditorContext* Context);
        ~FLandscapeWidget() override;

    private:
        void DrawContent(float DeltaTime) override;

    private:
        Editor::FLandscapeCreateSettings mSettings{};
        Core::FString mError{};
    };
}
