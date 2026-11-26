#include "Editor/Widget/FEditorQuantityInputWidget.h"

#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <format>
#include <utility>

using namespace Editor;

FEditorQuantityInputWidget::FEditorQuantityInputWidget(Core::FString Label, float BaseValue, const Core::FQuantityUnit& Unit)
	: FEditorWidget{std::move(Label)},
	  mBaseValue{BaseValue},
	  mUnit{Unit} {
}

bool FEditorQuantityInputWidget::EditValue(const char* Label, float& BaseValue, const Core::FQuantityUnit& Unit) {
    FEditorQuantityInputWidget Input{Label, BaseValue, Unit};
    Input.Draw(0.0f);

    const auto Edited{Input.GetEditedValue()};
    if (!Edited.has_value() || *Edited == BaseValue) {
        return false;
    }

    BaseValue = *Edited;
    return true;
}

std::optional<float> FEditorQuantityInputWidget::GetEditedValue() const {
    return mEditedValue;
}

void FEditorQuantityInputWidget::SetMixedValue(bool Mixed) {
    mMixedValue = Mixed;
}

void FEditorQuantityInputWidget::SetAxisColor(Uint32 Color) {
    mAxisColor = Color;
}

void FEditorQuantityInputWidget::SetInlineUnit(bool Inline) {
    mInlineUnit = Inline;
}

FVector2 FEditorQuantityInputWidget::ComputeDesiredSize() const {
    const float LabelWidth{ImGui::CalcTextSize(GetId().c_str(), nullptr, true).x};

    return {ImGui::CalcItemWidth() + (LabelWidth > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x + LabelWidth : 0.0f), ImGui::GetFrameHeight()};
}

void FEditorQuantityInputWidget::DrawContent(float) {
    mEditedValue.reset();

    if (ImGui::GetCurrentWindow()->SkipItems) {
        return;
    }

    const auto Number{Core::FromQuantityBase(mBaseValue, mUnit)};

    if (!Number.has_value()) {
        ImGui::TextUnformatted("Invalid quantity");

        return;
    }

    const ImGuiStyle& Style{ImGui::GetStyle()};
    const char* Label{GetId().c_str()};
    const char* LabelEnd{ImGui::FindRenderedTextEnd(Label)};
    const bool HasLabel{LabelEnd != Label};
    const float LabelWidth{HasLabel ? ImGui::CalcTextSize(Label, LabelEnd).x + Style.ItemInnerSpacing.x : 0.0f};
    const float Width{HasAllocatedSize() ? std::max(1.0f, GetAllocatedSize().x - LabelWidth) : ImGui::CalcItemWidth()};
    const float Gap{std::max(1.0f, Style.ItemInnerSpacing.x * 0.5f)};
    float UnitWidth{};

    if (!mInlineUnit && !mUnit.mSymbol.empty()) {
        const char* Symbol{mUnit.mSymbol.data()};
        const float SymbolWidth{ImGui::CalcTextSize(Symbol, Symbol + mUnit.mSymbol.size()).x};
        UnitWidth = std::min(SymbolWidth + Style.FramePadding.x * 2.0f, std::max(0.0f, Width - ImGui::GetFrameHeight() - Gap));
    }

    ImGui::BeginGroup();
    if (HasLabel) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Label, LabelEnd);
        ImGui::SameLine(0.0f, Style.ItemInnerSpacing.x);
    }

    ImGui::SetNextItemWidth(Width - (UnitWidth > 0.0f ? UnitWidth + Gap : 0.0f));
    DrawNumber(ImGui::CalcItemWidth(), *Number);

    if (UnitWidth > 0.0f) {
        DrawUnit(UnitWidth, Gap);
    }

    ImGui::EndGroup();

    if (ImGui::BeginPopup("##InvalidQuantity")) {
        ImGui::TextUnformatted("Enter a finite number with a compatible unit.");
        ImGui::EndPopup();
    }
}

void FEditorQuantityInputWidget::DrawNumber(float Width, float Number) {
    constexpr const char* InputLabel{"##Value"};
    ImGuiContext& Context{*ImGui::GetCurrentContext()};
    const ImGuiStyle& Style{Context.Style};
    const ImGuiID Id{ImGui::GetID(InputLabel)};
    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const ImRect Frame{Origin, {Origin.x + Width, Origin.y + ImGui::GetFrameHeight()}};

    ImGui::ItemSize(Frame, Style.FramePadding.y);

    if (!ImGui::ItemAdd(Frame, Id, &Frame, ImGuiItemFlags_Inputable)) {
        return;
    }

    Number = Number == 0.0f ? 0.0f : Number;
    const bool TextActive{ImGui::TempInputIsActive(Id)};
    const bool InlineUnit{mInlineUnit && !mUnit.mSymbol.empty()};
    const float MarkerWidth{mAxisColor != 0 ? Style.ColorMarkerSize : 0.0f};
    const ImVec2 Padding{Style.FramePadding.x + MarkerWidth, Style.FramePadding.y};
    const ImVec2 TextMin{Frame.Min.x + Padding.x, Frame.Min.y};
    const ImVec2 TextMax{Frame.Max.x - Padding.x, Frame.Max.y};
    bool Hovered{};
    bool Held{};
    const bool Pressed{!TextActive && ImGui::ButtonBehavior(Frame, Id, &Hovered, &Held)};
    const bool ClickedToEdit{Pressed && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left, Context.IO.MouseDragThreshold * 0.5f)};
    const bool NavRequested{Context.NavActivateId == Id && (Context.LastItemData.ItemFlags & ImGuiItemFlags_Disabled) == 0};

    if (TextActive || ClickedToEdit || NavRequested) {
        const Core::FString Text{mMixedValue ? Core::FString{} : std::format("{}", Number)};
        std::array<char, 96> Buffer{};
        Text.copy(Buffer.data(), Buffer.size() - 1);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Padding);
        const bool Submitted{ImGui::TempInputText(Frame, Id, InputLabel, Buffer.data(), Buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)};
        ImGui::PopStyleVar();

        if (InlineUnit) {
            const ImGuiInputTextState* State{ImGui::GetInputTextState(Id)};
            const char* Typed{State != nullptr ? State->TextA.Data : Buffer.data()};
            const float Scroll{State != nullptr ? State->Scroll.x : 0.0f};
            const float SuffixX{TextMin.x + std::max(0.0f, ImGui::CalcTextSize(Typed).x - Scroll) + Style.ItemInnerSpacing.x * 0.5f};

            if (SuffixX < TextMax.x) {
                const char* Symbol{mUnit.mSymbol.data()};
                ImGui::RenderTextClipped({SuffixX, TextMin.y}, TextMax, Symbol, Symbol + mUnit.mSymbol.size(), nullptr, {0.0f, 0.5f});
            }
        }

        if (Submitted) {
            mEditedValue = Core::ParseQuantity(Buffer.data(), mUnit);

            if (!mEditedValue.has_value()) {
                ImGui::OpenPopup("##InvalidQuantity");
            }
        }
    } else {
        const float DragSpeed{mUnit.mDimension == Core::Dim::Angle ? 0.25f : 0.025f};

        if (ImGui::DragBehavior(Id, ImGuiDataType_Float, &Number, DragSpeed, nullptr, nullptr, "%.7g", ImGuiSliderFlags_NoRoundToFormat)) {
            mEditedValue = Core::ToQuantityBase(Number, mUnit);

            if (mEditedValue.has_value()) {
                ImGui::MarkItemEdited(Id);
            }
        }

        if (Hovered || Held) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        const ImGuiCol Color{Held ? ImGuiCol_FrameBgActive : Hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg};
        const bool Mixed{mMixedValue && !mEditedValue.has_value()};
        Core::FString Text{Mixed ? "Multiple" : std::format("{:.7g}", Number)};

        if (!Mixed && mAxisColor != 0 && Text.find_first_of(".e") == Core::FString::npos) {
            Text += ".0";
        }

        if (!Mixed && InlineUnit) {
            Text.append(mUnit.mSymbol);
        }

        ImGui::RenderNavCursor(Frame, Id);
        ImGui::RenderFrame(Frame.Min, Frame.Max, ImGui::GetColorU32(Color), true, Style.FrameRounding);
        ImGui::RenderTextClipped(TextMin, TextMax, Text.c_str(), nullptr, nullptr, {0.0f, 0.5f});
    }

    if (MarkerWidth > 0.0f) {
        ImGui::RenderColorComponentMarker(Frame, ImGui::GetColorU32(mAxisColor), Style.FrameRounding);
    }
}

void FEditorQuantityInputWidget::DrawUnit(float Width, float Gap) const {
    const ImGuiStyle& Style{ImGui::GetStyle()};

    ImGui::SameLine(0.0f, Gap);
    ImGui::Dummy({Width, ImGui::GetFrameHeight()});

    const ImVec2 Min{ImGui::GetItemRectMin()};
    const ImVec2 Max{ImGui::GetItemRectMax()};
    const ImVec4 Background{ImLerp(Style.Colors[ImGuiCol_FrameBg], Style.Colors[ImGuiCol_Text], 0.08f)};
    const ImVec4 Foreground{ImLerp(Style.Colors[ImGuiCol_TextDisabled], Style.Colors[ImGuiCol_Text], 0.35f)};
    const char* Symbol{mUnit.mSymbol.data()};

    ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, ImGui::GetColorU32(Background), Style.FrameRounding);
    ImGui::PushStyleColor(ImGuiCol_Text, Foreground);
    ImGui::RenderTextClipped(Min, Max, Symbol, Symbol + mUnit.mSymbol.size(), nullptr, {0.5f, 0.5f});
    ImGui::PopStyleColor();
}
