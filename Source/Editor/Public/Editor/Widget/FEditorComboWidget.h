#pragma once

#include "Editor/Widget/FEditorWidget.h"

#include <imgui.h>
#include <span>

namespace Editor {
    class FEditorComboWidget final : public Editor::FEditorWidget {
    public:
        FEditorComboWidget(Core::FString Label, Core::FString Preview, std::function<void()> DrawItems, ImGuiComboFlags Flags = 0);
        ~FEditorComboWidget() override = default;

    public:
        static void DrawPopup(const char* Label, const char* Preview, std::function<void()> DrawItems, ImGuiComboFlags Flags = 0);
        static bool EditIndex(const char* Label, int& SelectedIndex, std::span<const char* const> Items);

    private:
        void DrawContent(float DeltaTime) override;

    private:
        Core::FString mLabel{};
        Core::FString mPreview{};
        std::function<void()> mDrawItems{};
        ImGuiComboFlags mFlags{};
    };
}
