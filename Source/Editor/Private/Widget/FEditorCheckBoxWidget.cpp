#include "Editor/Widget/FEditorCheckBoxWidget.h"

#include <imgui.h>
#include <utility>

using namespace Editor;

FEditorCheckBoxWidget::FEditorCheckBoxWidget(Core::FString Id, Core::FString Label, std::function<bool()> Getter, std::function<void(bool)> Setter)
	: FEditorWidget{std::move(Id)},
	  mLabel{std::move(Label) + "###Value"},
	  mGetter{std::move(Getter)},
	  mSetter{std::move(Setter)} {
}

FEditorCheckBoxWidget::~FEditorCheckBoxWidget() = default;

bool FEditorCheckBoxWidget::EditValue(const char* Label, bool& Value) {
    bool Changed{};
    FEditorCheckBoxWidget CheckBox{Label, Label, [&Value]() {
        return Value;
    }, [&Value, &Changed](bool Edited) {
        Value = Edited;
        Changed = true;
    }};
    CheckBox.Draw(0.0f);

    return Changed;
}

FVector2 FEditorCheckBoxWidget::ComputeDesiredSize() const {
    return {ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(mLabel.c_str(), nullptr, true).x, ImGui::GetFrameHeight()};
}

void FEditorCheckBoxWidget::DrawContent(float) {
    mChanged = false;

    if (mGetter && mSetter) {
        mEditedValue = mGetter();
        mChanged = ImGui::Checkbox(mLabel.c_str(), &mEditedValue);
    }
}

void FEditorCheckBoxWidget::AfterDraw() {
    if (mChanged) {
        mSetter(mEditedValue);
    }
}
