#include "Editor/Widget/FEditorSliderWidget.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Editor;

FEditorSliderWidget::FEditorSliderWidget(Core::FString Id, Core::FString Label, float Minimum, float Maximum, std::function<float()> Getter, std::function<void(float)> Setter, Core::FString Format)
	: FEditorWidget{std::move(Id)},
	  mLabel{std::move(Label) + "###Value"},
	  mFormat{std::move(Format)},
	  mGetter{std::move(Getter)},
	  mSetter{std::move(Setter)},
	  mMinimum{Minimum},
	  mMaximum{std::max(Minimum, Maximum)} {
}

FEditorSliderWidget::~FEditorSliderWidget() = default;

FEditorSliderWidget::FEditorSliderWidget(const char* Label, std::span<float> Values, float Minimum, float Maximum, const char* Format, float DragSpeed, ImGuiSliderFlags Flags)
	: FEditorWidget{Label},
	  mLabel{Core::FString{Label} + "###Value"},
	  mFormat{Format},
	  mValues{Values},
	  mMinimum{Minimum},
	  mMaximum{std::max(Minimum, Maximum)},
	  mDragSpeed{std::max(0.0f, DragSpeed)},
	  mFlags{Flags} {
}

bool FEditorSliderWidget::EditValue(const char* Label, float& Value, float Minimum, float Maximum, const char* Format, float DragSpeed, ImGuiSliderFlags Flags) {
    return EditValue(Label, std::span<float>{&Value, 1}, Minimum, Maximum, Format, DragSpeed, Flags);
}

bool FEditorSliderWidget::EditValue(const char* Label, std::span<float> Values, float Minimum, float Maximum, const char* Format, float DragSpeed, ImGuiSliderFlags Flags) {
    FEditorSliderWidget Slider{Label, Values, Minimum, Maximum, Format, DragSpeed, Flags};
    Slider.Draw(0.0f);

    return Slider.mChanged;
}

void FEditorSliderWidget::SetDragSpeed(float Speed) {
    mDragSpeed = std::max(0.0f, Speed);
}

FVector2 FEditorSliderWidget::ComputeDesiredSize() const {
    return {ImGui::CalcItemWidth() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(mLabel.c_str(), nullptr, true).x, ImGui::GetFrameHeight()};
}

void FEditorSliderWidget::DrawContent(float) {
    mChanged = false;
    std::span<float> Values{mValues};

    if (Values.empty()) {
        if (!mGetter || !mSetter) {
            return;
        }

        mEditedValue = mGetter();
        Values = std::span<float>{&mEditedValue, 1};
    }

    if (HasAllocatedSize()) {
        const float LabelWidth{ImGui::CalcTextSize(mLabel.c_str(), nullptr, true).x};
        ImGui::SetNextItemWidth(std::max(1.0f, GetAllocatedSize().x - (LabelWidth > 0.0f ? LabelWidth + ImGui::GetStyle().ItemInnerSpacing.x : 0.0f)));
    }

    if (Values.size() == 1) {
        mChanged = mDragSpeed > 0.0f ? ImGui::DragFloat(mLabel.c_str(), Values.data(), mDragSpeed, mMinimum, mMaximum, mFormat.c_str(), mFlags) : ImGui::SliderFloat(mLabel.c_str(), Values.data(), mMinimum, mMaximum, mFormat.c_str(), mFlags);
    } else {
        const int Count{static_cast<int>(Values.size())};
        mChanged = mDragSpeed > 0.0f ? ImGui::DragScalarN(mLabel.c_str(), ImGuiDataType_Float, Values.data(), Count, mDragSpeed, &mMinimum, &mMaximum, mFormat.c_str(), mFlags) : ImGui::SliderScalarN(mLabel.c_str(), ImGuiDataType_Float, Values.data(), Count, &mMinimum, &mMaximum, mFormat.c_str(), mFlags);
    }
}

void FEditorSliderWidget::AfterDraw() {
    if (mChanged && mSetter && std::isfinite(mEditedValue)) {
        mSetter(std::clamp(mEditedValue, mMinimum, mMaximum));
    }
}
