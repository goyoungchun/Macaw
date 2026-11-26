#pragma once

#include "Core/Math/FVector4.h"
#include "Engine/Curves/FFloatCurveKey.h"

#include <optional>

namespace Editor {

    class FCurveEditorWidget final {
    public:
        struct FOptions {
            bool mEditable{true};
            std::optional<float> mMinimumTime{};
            Uint32 mColor{};
        };

    public:
        explicit FCurveEditorWidget(Core::FString Id);

    public:
        void DrawFitButton(bool Enabled);
        std::optional<Core::TArray<Engine::FFloatCurveKey>> Draw(const Core::TArray<Engine::FFloatCurveKey>* Keys, const Editor::FCurveEditorWidget::FOptions& Options);
        void Reset();

    private:
        void FitView();
        bool DrawKeyControls(Core::TArray<Engine::FFloatCurveKey>& Keys, const Editor::FCurveEditorWidget::FOptions& Options);
        bool DrawGraph(Core::TArray<Engine::FFloatCurveKey>* Keys, const Editor::FCurveEditorWidget::FOptions& Options);

    private:
        Core::FString mId{};
        Int32 mSelectedKey{-1};
        std::optional<FVector4> mViewRange{};
        bool mDraggingKey{};
    };

}
