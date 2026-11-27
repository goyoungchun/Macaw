#pragma once

#include "Editor/Widget/FEditorWidget.h"

namespace Editor {
    class FEditorCheckBoxWidget final : public Editor::FEditorWidget {
    public:
        FEditorCheckBoxWidget(Core::FString Id, Core::FString Label, std::function<bool()> Getter, std::function<void(bool)> Setter);
        ~FEditorCheckBoxWidget() override;

    public:
        static bool EditValue(const char* Label, bool& Value);

        FVector2 ComputeDesiredSize() const override;

    private:
        void DrawContent(float DeltaTime) override;
        void AfterDraw() override;

    private:
        Core::FString mLabel{};
        std::function<bool()> mGetter{};
        std::function<void(bool)> mSetter{};
        bool mEditedValue{};
        bool mChanged{};
    };
}
