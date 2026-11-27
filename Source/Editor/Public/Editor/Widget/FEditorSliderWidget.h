#pragma once

#include "Editor/Widget/FEditorWidget.h"

#include <imgui.h>
#include <span>

namespace Editor {
    class FEditorSliderWidget final : public Editor::FEditorWidget {
    public:
        FEditorSliderWidget(Core::FString Id, Core::FString Label, float Minimum, float Maximum, std::function<float()> Getter, std::function<void(float)> Setter, Core::FString Format = "%.3f");
        ~FEditorSliderWidget() override;

    private:
        FEditorSliderWidget(const char* Label, std::span<float> Values, float Minimum, float Maximum, const char* Format, float DragSpeed, ImGuiSliderFlags Flags);

    public:
        static bool EditValue(const char* Label, float& Value, float Minimum, float Maximum, const char* Format = "%.3f", float DragSpeed = 0.0f, ImGuiSliderFlags Flags = 0);
        static bool EditValue(const char* Label, std::span<float> Values, float Minimum, float Maximum, const char* Format = "%.3f", float DragSpeed = 0.0f, ImGuiSliderFlags Flags = 0);

        void SetDragSpeed(float Speed);
        FVector2 ComputeDesiredSize() const override;

    private:
        void DrawContent(float DeltaTime) override;
        void AfterDraw() override;

    private:
        Core::FString mLabel{};
        Core::FString mFormat{};
        std::function<float()> mGetter{};
        std::function<void(float)> mSetter{};
        std::span<float> mValues{};
        float mMinimum{};
        float mMaximum{};
        float mDragSpeed{};
        float mEditedValue{};
        ImGuiSliderFlags mFlags{ImGuiSliderFlags_AlwaysClamp};
        bool mChanged{};
    };
}
