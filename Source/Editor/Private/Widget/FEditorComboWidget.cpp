#include "Editor/Widget/FEditorComboWidget.h"
#include "Editor/Widget/FEditorButtonWidget.h"

#include <imgui_internal.h>
#include <utility>

using namespace Editor;

FEditorComboWidget::FEditorComboWidget(Core::FString Label, Core::FString Preview, std::function<void()> DrawItems, ImGuiComboFlags Flags)
	: FEditorWidget{Label},
	  mLabel{std::move(Label) + "###Combo"},
	  mPreview{std::move(Preview)},
	  mDrawItems{std::move(DrawItems)},
	  mFlags{Flags} {
}

void FEditorComboWidget::DrawPopup(const char* Label, const char* Preview, std::function<void()> DrawItems, ImGuiComboFlags Flags) {
    FEditorComboWidget Combo{Label, Preview, std::move(DrawItems), Flags};
    Combo.Draw(0.0f);
}

bool FEditorComboWidget::EditIndex(const char* Label, int& SelectedIndex, std::span<const char* const> Items) {
    const int Count{static_cast<int>(Items.size())};
    const char* Preview{SelectedIndex >= 0 && SelectedIndex < Count ? Items[SelectedIndex] : ""};
    bool Changed{};

    DrawPopup(Label, Preview, [&]() {
        for (int Index{}; Index < Count; ++Index) {
            const bool Selected{Index == SelectedIndex};
            ImGui::PushID(Index);

            if (FEditorButtonWidget::Press(Items[Index], EEditorButtonStyle::Selectable, Selected) && !Selected) {
                SelectedIndex = Index;
                Changed = true;
            }

            if (Selected) {
                ImGui::SetItemDefaultFocus();
            }

            ImGui::PopID();
        }
    });

    if (Changed) {
        ImGui::MarkItemEdited(ImGui::GetItemID());
    }

    return Changed;
}

void FEditorComboWidget::DrawContent(float) {
    if (ImGui::BeginCombo(mLabel.c_str(), mPreview.c_str(), mFlags)) {
        if (mDrawItems) {
            mDrawItems();
        }

        ImGui::EndCombo();
    }
}
