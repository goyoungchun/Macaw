#pragma once

#include "Core/Literals/QuantityUnits.h"
#include "Editor/Widget/FEditorWidget.h"

namespace Editor {
    class FEditorQuantityInputWidget final : public Editor::FEditorWidget {
    public:
        FEditorQuantityInputWidget(Core::FString Label, float BaseValue, const Core::FQuantityUnit& Unit);
        ~FEditorQuantityInputWidget() override = default;

    public:
        static bool EditValue(const char* Label, float& BaseValue, const Core::FQuantityUnit& Unit);

        std::optional<float> GetEditedValue() const;
        void SetMixedValue(bool Mixed);
        void SetAxisColor(Uint32 Color);
        void SetInlineUnit(bool Inline);
        FVector2 ComputeDesiredSize() const override;

    private:
        void DrawContent(float DeltaTime) override;
        void DrawNumber(float Width, float Number);
        void DrawUnit(float Width, float Gap) const;

    private:
        float mBaseValue{};
        Core::FQuantityUnit mUnit{};
        std::optional<float> mEditedValue{};
        Uint32 mAxisColor{};
        bool mMixedValue{};
        bool mInlineUnit{};
    };
}
