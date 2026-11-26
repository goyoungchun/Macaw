#include "Editor/Widget/FCurveEditorWidget.h"

#include "EditorStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <limits>
#include <utility>

using namespace Editor;

namespace {

    float ClampTime(float Time, const FCurveEditorWidget::FOptions& Options) {
        return std::max(Time, Options.mMinimumTime.value_or(std::numeric_limits<float>::lowest()));
    }

    FVector4 GetCurveRange(const Core::TArray<Engine::FFloatCurveKey>& Keys) {
        FVector4 Range{0.0f, 1.0f, 0.0f, 1.0f};

        for (const auto& Key : Keys) {
            Range.x = std::min(Range.x, Key.mTime);
            Range.y = std::max(Range.y, Key.mTime);
            Range.z = std::min(Range.z, Key.mValue);
            Range.w = std::max(Range.w, Key.mValue);
        }

        const float Margin{(Range.w - Range.z) * 0.10f};
        Range.z -= Margin;
        Range.w += Margin;

        return Range;
    }

}

FCurveEditorWidget::FCurveEditorWidget(Core::FString Id)
	: mId{std::move(Id)} {
}

void FCurveEditorWidget::DrawFitButton(bool Enabled) {
    ImGui::PushID(mId.c_str());
    constexpr EditorStyle::EIcon FitIcon{EditorStyle::EIcon::Maximize};
    if (EditorStyle::LabelButton("Fit Curve", &FitIcon, false, Enabled)) {
        FitView();
    }
    ImGui::PopID();
}

std::optional<Core::TArray<Engine::FFloatCurveKey>> FCurveEditorWidget::Draw(const Core::TArray<Engine::FFloatCurveKey>* Keys, const FOptions& Options) {
    std::optional<Core::TArray<Engine::FFloatCurveKey>> Edited{};
    ImGui::PushID(mId.c_str());

    if (Keys == nullptr) {
        DrawGraph(nullptr, Options);
    } else {
        auto Draft{*Keys};
        Engine::CurveKeys::EnsureKeyIds(Draft);
        Engine::CurveKeys::SortKeys(Draft);

        ImGui::BeginDisabled(!Options.mEditable);
        const bool KeyChanged{DrawKeyControls(Draft, Options)};
        ImGui::EndDisabled();
        if (KeyChanged) {
            Engine::CurveKeys::SortKeys(Draft);
        }

        const bool GraphChanged{DrawGraph(&Draft, Options)};
        if (Options.mEditable && (KeyChanged || GraphChanged)) {
            Edited.emplace(std::move(Draft));
        }
    }

    ImGui::PopID();
    return Edited;
}

void FCurveEditorWidget::Reset() {
    mSelectedKey = -1;
    FitView();
}

void FCurveEditorWidget::FitView() {
    mViewRange.reset();
    mDraggingKey = false;
}

bool FCurveEditorWidget::DrawKeyControls(Core::TArray<Engine::FFloatCurveKey>& Keys, const FOptions& Options) {
    bool Changed{};

    constexpr EditorStyle::EIcon AddIcon{EditorStyle::EIcon::Add};
    if (EditorStyle::LabelButton("Key", &AddIcon, false, true, "Add a curve key")) {
        const float Time{ClampTime(Keys.empty() ? 0.0f : Keys.back().mTime + 0.1f, Options)};
        mSelectedKey = Engine::CurveKeys::NewKeyId(Keys);
        Keys.push_back({mSelectedKey, Time, Engine::CurveKeys::Evaluate(Keys, Time)});
        mViewRange.reset();
        Changed = true;
    }

    ImGui::SameLine();
    Int32 Selected{Engine::CurveKeys::FindKeyIndex(Keys, mSelectedKey)};
    if (EditorStyle::LabelButton("Delete Key", nullptr, false, Selected >= 0)) {
        Keys.erase(Keys.begin() + Selected);
        mSelectedKey = -1;
        Selected = -1;
        Changed = true;
    }

    ImGui::SameLine();
    if (EditorStyle::LabelButton("Key Properties", nullptr, true, Selected >= 0)) {
        ImGui::OpenPopup("Curve Key Properties");
    }

    if (Selected < 0 || !EditorStyle::BeginPopup("Curve Key Properties")) {
        return Changed;
    }

    auto& Key{Keys[static_cast<Index_t>(Selected)]};
    const float LabelWidth{ImGui::CalcTextSize("Interpolation").x};
    const float FieldWidth{ImGui::GetFontSize() * 8.0f};
    EditorStyle::DrawFieldLabel("Time", LabelWidth, FieldWidth);
    float Time{Key.mTime};
    if (ImGui::DragFloat("##Time", &Time, 0.01f) && std::isfinite(Time)) {
        Key.mTime = ClampTime(Time, Options);
        Changed = true;
    }
    EditorStyle::DrawFieldLabel("Value", LabelWidth, FieldWidth);
    float Value{Key.mValue};
    if (ImGui::DragFloat("##Value", &Value, 0.01f) && std::isfinite(Value)) {
        Key.mValue = Value;
        Changed = true;
    }
    EditorStyle::DrawFieldLabel("Interpolation", LabelWidth, FieldWidth);
    int Mode{static_cast<int>(Key.mInterpMode)};
    if (ImGui::Combo("##Interpolation", &Mode, "Constant\0Linear\0Cubic\0")) {
        Key.mInterpMode = static_cast<Engine::ECurveInterpMode>(Mode);
        Changed = true;
    }

    if (Key.mInterpMode == Engine::ECurveInterpMode::Cubic) {
        EditorStyle::DrawFieldLabel("Arrive", LabelWidth, FieldWidth);
        float Arrive{Key.mArriveTangent};
        if (ImGui::DragFloat("##Arrive", &Arrive, 0.01f) && std::isfinite(Arrive)) {
            Key.mArriveTangent = Arrive;
            Changed = true;
        }
        EditorStyle::DrawFieldLabel("Leave", LabelWidth, FieldWidth);
        float Leave{Key.mLeaveTangent};
        if (ImGui::DragFloat("##Leave", &Leave, 0.01f) && std::isfinite(Leave)) {
            Key.mLeaveTangent = Leave;
            Changed = true;
        }
        if (ImGui::SmallButton("Auto Tangent")) {
            Engine::CurveKeys::SetAutoTangent(Keys, static_cast<Index_t>(Selected));
            Changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Flat Tangent")) {
            Engine::CurveKeys::SetFlatTangent(Key);
            Changed = true;
        }
    }

    EditorStyle::EndPopup();
    return Changed;
}

bool FCurveEditorWidget::DrawGraph(Core::TArray<Engine::FFloatCurveKey>* Keys, const FOptions& Options) {
    const ImVec2 Size{ImGui::GetContentRegionAvail()};
    const float FontSize{ImGui::GetFontSize()};
    if (Size.x < FontSize * 6.0f || Size.y < FontSize * 3.0f) {
        return false;
    }

    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const ImVec2 End{Origin.x + Size.x, Origin.y + Size.y};
    const ImVec2 Plot{Origin.x + FontSize * 3.2f, Origin.y + FontSize};
    const ImVec2 Extent{End.x - Plot.x - FontSize, End.y - Plot.y - FontSize * 1.5f};
    const ImVec2 PlotEnd{Plot.x + Extent.x, Plot.y + Extent.y};
    FVector4 Range{mViewRange.value_or(Keys != nullptr ? GetCurveRange(*Keys) : FVector4{0.0f, 1.0f, -1.0f, 1.0f})};
    const auto ToScreen{[&](float Time, float Value) {
        return ImVec2{Plot.x + Extent.x * ((Time - Range.x) / (Range.y - Range.x)), Plot.y + Extent.y * ((Range.w - Value) / (Range.w - Range.z))};
    }};

    ImGui::InvisibleButton("Curve Graph", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool Hovered{ImGui::IsItemHovered()};
    const auto& IO{ImGui::GetIO()};
    const auto Mouse{IO.MousePos};
    const bool InPlot{Mouse.x >= Plot.x && Mouse.x <= PlotEnd.x && Mouse.y >= Plot.y && Mouse.y <= PlotEnd.y};

    if (Keys != nullptr && Hovered && InPlot && !mDraggingKey && IO.MouseWheel != 0.0f) {
        const float Scale{std::pow(0.8f, IO.MouseWheel)};
        const float Time{std::lerp(Range.x, Range.y, (Mouse.x - Plot.x) / Extent.x)};
        const float Value{std::lerp(Range.w, Range.z, (Mouse.y - Plot.y) / Extent.y)};
        const FVector4 Zoomed{std::lerp(Time, Range.x, Scale), std::lerp(Time, Range.y, Scale), std::lerp(Value, Range.z, Scale), std::lerp(Value, Range.w, Scale)};
        if (Zoomed.y - Zoomed.x >= 0.0001f && Zoomed.w - Zoomed.z >= 0.0001f && Zoomed.y - Zoomed.x <= 1.0e8f && Zoomed.w - Zoomed.z <= 1.0e8f) {
            Range = Zoomed;
            mViewRange = Range;
        }
    }

    const float TimeSpan{Range.y - Range.x};
    const float ValueSpan{Range.w - Range.z};
    if (Keys != nullptr && ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        const float TimeOffset{IO.MouseDelta.x * TimeSpan / Extent.x};
        const float ValueOffset{IO.MouseDelta.y * ValueSpan / Extent.y};
        Range = {Range.x - TimeOffset, Range.y - TimeOffset, Range.z + ValueOffset, Range.w + ValueOffset};
        mViewRange = Range;
    }

    bool Changed{};
    if (Keys != nullptr && Options.mEditable && Hovered && InPlot && ImGui::IsItemClicked()) {
        mSelectedKey = -1;

        for (const auto& Key : *Keys) {
            const auto Point{ToScreen(Key.mTime, Key.mValue)};
            if (std::abs(Mouse.x - Point.x) <= 7.0f && std::abs(Mouse.y - Point.y) <= 7.0f) {
                mSelectedKey = Key.mKeyId;
                mDraggingKey = true;
                mViewRange = Range;
                break;
            }
        }

        if (mSelectedKey < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const float Time{ClampTime(Range.x + (Mouse.x - Plot.x) / Extent.x * TimeSpan, Options)};
            const float Value{Range.w - (Mouse.y - Plot.y) / Extent.y * ValueSpan};
            mSelectedKey = Engine::CurveKeys::NewKeyId(*Keys);
            Keys->push_back({mSelectedKey, Time, Value});
            Engine::CurveKeys::SortKeys(*Keys);
            Changed = true;
        }
    }

    if (Keys != nullptr && Options.mEditable && mDraggingKey && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        const Int32 Key{Engine::CurveKeys::FindKeyIndex(*Keys, mSelectedKey)};
        if (Key >= 0) {
            auto& Current{(*Keys)[static_cast<Index_t>(Key)]};
            Current.mTime = ClampTime(Current.mTime + IO.MouseDelta.x / Extent.x * TimeSpan, Options);
            Current.mValue -= IO.MouseDelta.y / Extent.y * ValueSpan;
            Engine::CurveKeys::SortKeys(*Keys);
            Changed = true;
        }
    }

    if (!ImGui::IsItemActive()) {
        mDraggingKey = false;
    }

    auto* Draw{ImGui::GetWindowDrawList()};
    Draw->AddRectFilled(Origin, End, IM_COL32(20, 26, 35, 255), 4.0f);
    Draw->AddRectFilled(Plot, PlotEnd, IM_COL32(31, 39, 51, 255));
    const ImU32 Text{ImGui::GetColorU32(ImGuiCol_TextDisabled)};
    const ImU32 Grid{IM_COL32(57, 70, 87, 255)};
    const ImU32 Axis{IM_COL32(92, 118, 142, 255)};
    std::array<char, 48> Label{};

    for (int Line{}; Line <= 8; ++Line) {
        const float Alpha{static_cast<float>(Line) / 8.0f};
        const float X{Plot.x + Extent.x * Alpha};
        Draw->AddLine({X, Plot.y}, {X, PlotEnd.y}, Grid);
        if (Line % 2 == 0) {
            std::snprintf(Label.data(), Label.size(), "%.3g", std::lerp(Range.x, Range.y, Alpha));
            const float LabelWidth{ImGui::CalcTextSize(Label.data()).x};
            Draw->AddText({std::min(X, End.x - LabelWidth - 2.0f), PlotEnd.y + 3.0f}, Text, Label.data());
        }
    }

    for (int Line{}; Line <= 4; ++Line) {
        const float Alpha{static_cast<float>(Line) / 4.0f};
        const float Y{Plot.y + Extent.y * Alpha};
        Draw->AddLine({Plot.x, Y}, {PlotEnd.x, Y}, Grid);
        std::snprintf(Label.data(), Label.size(), "%.3g", std::lerp(Range.w, Range.z, Alpha));
        Draw->AddText({Origin.x + 4.0f, Y - FontSize * 0.5f}, Text, Label.data());
    }

    if (Range.z <= 0.0f && Range.w >= 0.0f) {
        const float ZeroY{ToScreen(Range.x, 0.0f).y};
        Draw->AddLine({Plot.x, ZeroY}, {PlotEnd.x, ZeroY}, Axis);
    }

    if (Keys == nullptr) {
        return Changed;
    }

    Draw->PushClipRect({Plot.x - 5.0f, Plot.y - 5.0f}, {PlotEnd.x + 5.0f, PlotEnd.y + 5.0f}, true);
    const ImU32 Curve{Options.mColor != 0 ? Options.mColor : IM_COL32(102, 220, 198, 255)};

    for (Index_t Index{1}; Index < Keys->size(); ++Index) {
        const auto& Left{(*Keys)[Index - 1]};
        const auto& Right{(*Keys)[Index]};
        ImVec2 Previous{ToScreen(Left.mTime, Left.mValue)};

        if (Left.mInterpMode == Engine::ECurveInterpMode::Constant) {
            const auto Corner{ToScreen(Right.mTime, Left.mValue)};
            Draw->AddLine(Previous, Corner, Curve, 1.7f);
            Draw->AddLine(Corner, ToScreen(Right.mTime, Right.mValue), Curve, 1.7f);
            continue;
        }

        for (int Sample{1}; Sample <= 24; ++Sample) {
            const float Time{std::lerp(Left.mTime, Right.mTime, static_cast<float>(Sample) / 24.0f)};
            const auto Point{ToScreen(Time, Engine::CurveKeys::EvaluateSegment(Left, Right, Time))};
            Draw->AddLine(Previous, Point, Curve, 1.7f);
            Previous = Point;
        }
    }

    for (const auto& Key : *Keys) {
        const auto Point{ToScreen(Key.mTime, Key.mValue)};
        Draw->AddCircleFilled(Point, Key.mKeyId == mSelectedKey ? 5.0f : 3.5f, Key.mKeyId == mSelectedKey ? ImGui::GetColorU32(ImGuiCol_Text) : Curve);
    }

    Draw->PopClipRect();
    if (Hovered && InPlot && !ImGui::IsItemActive()) {
        ImGui::SetItemTooltip("%s", Options.mEditable ? "Scroll to zoom. Right-drag to pan.\nDouble-click to add a key. Drag a key to move it." : "Scroll to zoom. Right-drag to pan.");
    }

    return Changed;
}
