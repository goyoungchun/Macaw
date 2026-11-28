#pragma once

#include "Editor/Widget/FEditorWidget.h"
#include "Editor/EEditorIcon.h"

#include <optional>

namespace Editor {
    enum class EEditorButtonStyle {
        Normal,
        Small,
        Icon,
        Label,
        Radio,
        ArrowLeft,
        Selectable,
        MenuItem
    };

    class FEditorButtonWidget final : public Editor::FEditorWidget {
    public:
        FEditorButtonWidget(Core::FString Id, Core::FString Label, std::function<void()> OnClicked = {});
        ~FEditorButtonWidget() override;

    public:
        static bool Press(const char* Label, Editor::EEditorButtonStyle Style = Editor::EEditorButtonStyle::Normal, bool Selected = false, bool Enabled = true);

        void SetLabel(Core::FString Label);
        void SetIcon(Editor::EEditorIcon Icon);
        void SetStyle(Editor::EEditorButtonStyle Style);
        void SetDropdown(bool Dropdown);
        void SetSelected(bool Selected);
        void SetSize(const FVector2& Size);
        void SetClickedHandler(std::function<void()> Handler);
        bool WasClicked() const;
        FVector2 ComputeDesiredSize() const override;

    private:
        void DrawContent(float DeltaTime) override;
        void AfterDraw() override;

    private:
        Core::FString mLabel{};
        std::function<void()> mOnClicked{};
        std::optional<Editor::EEditorIcon> mIcon{};
        Editor::EEditorButtonStyle mStyle{Editor::EEditorButtonStyle::Normal};
        FVector2 mSize{};
        bool mDropdown{};
        bool mSelected{};
        bool mClicked{};
    };
}
