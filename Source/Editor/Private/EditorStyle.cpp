/**
 * @file EditorStyle.cpp
 * @brief 에디터 공통 벡터 아이콘과 툴바 버튼 스타일을 구현합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Core/Literals/UnitLiterals.h"
#include "EditorStyle.h"
#include "FEditorAnimation.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Core::Literals;

namespace Editor {

    float EditorStyle::GetTopLevelTabHeight() {
        const float Scale{ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};

        return std::max(36.0f * Scale, ImGui::GetFontSize() + 16.0f * Scale);
    }

    ImVec4 EditorStyle::GetTopLevelTabColor() {
        return ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg), ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.06f);
    }

    void EditorStyle::PushTopLevelTabStyle() {
        const float Scale{ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};
        const ImVec4 Background{ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg)};
        const ImVec4 Selected{GetTopLevelTabColor()};

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{12.0f * Scale, (GetTopLevelTabHeight() - ImGui::GetFontSize()) * 0.5f});
        ImGui::PushStyleVar(ImGuiStyleVar_TabMinWidthBase, 220.0f * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_TabMinWidthShrink, 120.0f * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.0f * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_TabBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_TabBarOverlineSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, Background);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, Background);
        ImGui::PushStyleColor(ImGuiCol_Tab, Background);
        ImGui::PushStyleColor(ImGuiCol_TabDimmed, Background);
        ImGui::PushStyleColor(ImGuiCol_TabSelected, Selected);
        ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, Selected);
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImLerp(Selected, ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.04f));
    }

    void EditorStyle::PopTopLevelTabStyle() {
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(7);
    }

    bool EditorStyle::BeginEditorWindow(const char* Name, bool* Open, ImGuiWindowFlags Flags) {
        const ImGuiNextWindowData& Next{ImGui::GetCurrentContext()->NextWindowData};
        const bool IsTopLevel{(Next.HasFlags & ImGuiNextWindowDataFlags_HasWindowClass) != 0 && Next.WindowClass.ClassId == TopLevelTabClassId};

        if (IsTopLevel) {
            PushTopLevelTabStyle();
        }

        const bool Visible{ImGui::Begin(Name, Open, Flags)};

        if (IsTopLevel) {
            PopTopLevelTabStyle();
        }

        return Visible;
    }

    bool EditorStyle::Button(const char* Label, const ImVec2& Size, bool Selected) {
        if (ImGui::GetCurrentWindow()->SkipItems) {
            return false;
        }

        const ImVec4 Normal{ImGui::GetStyleColorVec4(Selected ? ImGuiCol_HeaderActive : ImGuiCol_Button)};
        const ImVec4 HoveredColor{ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)};
        const ImVec4 ActiveColor{ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)};
        const ImVec4 TextColor{ImGui::GetStyleColorVec4(ImGuiCol_Text)};
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        const bool Pressed{ImGui::ButtonEx(Label, Size, ImGui::GetStyle().FramePadding.y == 0.0f ? ImGuiButtonFlags_AlignTextBaseLine : ImGuiButtonFlags_None)};
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        if (ImGui::IsItemVisible()) {
            const ImGuiID Id{ImGui::GetItemID()};
            const ImRect Bounds{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
            const bool Hovered{ImGui::IsItemHovered()};
            const bool Active{ImGui::IsItemActive()};
            const ImVec4 Target{Active ? ActiveColor : Hovered ? HoveredColor : Normal};
            const ImVec4 Background{FEditorAnimation::Color(Id, "ButtonBackground", Target, Active ? 0.05f : 0.12f, Normal)};
            const ImGuiStyle& Style{ImGui::GetStyle()};
            ImGui::RenderFrame(Bounds.Min, Bounds.Max, ImGui::GetColorU32(Background), true, Style.FrameRounding);
            ImGui::RenderNavCursor(Bounds, Id);
            ImGui::PushStyleColor(ImGuiCol_Text, TextColor);
            ImGui::RenderTextClipped({Bounds.Min.x + Style.FramePadding.x, Bounds.Min.y + Style.FramePadding.y}, {Bounds.Max.x - Style.FramePadding.x, Bounds.Max.y - Style.FramePadding.y}, Label, ImGui::FindRenderedTextEnd(Label), nullptr, Style.ButtonTextAlign, &Bounds);
            ImGui::PopStyleColor();
        }

        return Pressed;
    }

    bool EditorStyle::SmallButton(const char* Label) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x, 0.0f});
        const bool Pressed{Button(Label)};
        ImGui::PopStyleVar();
        return Pressed;
    }

    void EditorStyle::SelectionBackground(ImGuiID Id, const ImVec2& Minimum, const ImVec2& Maximum, bool Selected, bool Hovered) {
        const ImVec4 Target{Selected ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive) : Hovered ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered) : ImVec4{}};
        const ImVec4 Color{FEditorAnimation::Color(Id, "SelectionBackground", Target, 0.12f, {})};
        ImGui::GetWindowDrawList()->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Color), ImGui::GetStyle().FrameRounding);
    }

    bool EditorStyle::Selectable(const char* Label, bool Selected, ImGuiSelectableFlags Flags, const ImVec2& Size) {
        if (ImGui::GetCurrentWindow()->SkipItems) {
            return false;
        }

        const ImVec4 TextColor{ImGui::GetStyleColorVec4(ImGuiCol_Text)};
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{});
        const bool Pressed{ImGui::Selectable(Label, Selected, Flags, Size)};
        ImGui::PopStyleColor(4);

        if (ImGui::IsItemVisible()) {
            const ImRect Bounds{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
            SelectionBackground(ImGui::GetItemID(), Bounds.Min, Bounds.Max, Selected || Pressed, ImGui::IsItemHovered());
            ImGui::RenderNavCursor(Bounds, ImGui::GetItemID());
            ImGui::PushStyleColor(ImGuiCol_Text, TextColor);
            ImGui::RenderTextClipped(Bounds.Min, Bounds.Max, Label, ImGui::FindRenderedTextEnd(Label), nullptr, ImGui::GetStyle().SelectableTextAlign, &Bounds);
            ImGui::PopStyleColor();
        }

        return Pressed;
    }

    bool EditorStyle::BeginPopup(const char* Id, ImGuiWindowFlags Flags) {
        const float Opacity{ImGui::IsPopupOpen(Id) ? FEditorAnimation::Appear(ImGui::GetID(Id)) : 1.0f};
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Opacity);

        if (ImGui::BeginPopup(Id, Flags)) {
            return true;
        }

        ImGui::PopStyleVar();
        return false;
    }

    void EditorStyle::EndPopup() {
        ImGui::EndPopup();
        ImGui::PopStyleVar();
    }

    /**
     * @fn void Editor::EditorStyle::DrawIcon(EIcon Icon, const ImVec2& Origin, float Size, ImU32 Color)
     * @brief 공통 아이콘의 정규화된 도형을 요청한 화면 크기와 색상으로 그립니다.
     */

    /**
     * @brief 현재 창의 드로 목록에 지정한 벡터 아이콘을 그립니다.
     */
    void Editor::EditorStyle::DrawIcon(EIcon Icon, const ImVec2& Origin, float Size, ImU32 Color) {
        Editor::EditorStyle::DrawIcon(ImGui::GetWindowDrawList(), Icon, Origin, Size, Color);
    }

    void Editor::EditorStyle::TabIcon(EIcon Icon, ImU32 Color) {
        const ImGuiWindow* Window{ImGui::GetCurrentWindow()};

        if (!Window->DockIsActive || Window->DockNode == nullptr || Window->DockNode->HostWindow == nullptr || Window->DockNode->IsHiddenTabBar()) {
            return;
        }

        // 호스트 창이 이번 프레임에 그려지지 않으면(다른 최상위 탭이 선택된 동안의 패널 등) 드로 목록이 비어 있거나 ImGui가 메모리를 회수한 상태이므로 그리지 않습니다.
        const ImGuiWindow* Host{Window->DockNode->HostWindow};

        if (Host->LastFrameActive != ImGui::GetFrameCount() || Host->Hidden || Host->DrawList->CmdBuffer.Size == 0) {
            return;
        }

        const ImRect Tab{Window->DC.DockTabItemRect};

        if (Tab.GetWidth() <= 0.0f || Tab.GetHeight() <= 0.0f) {
            return;
        }

        // 탭은 도킹 호스트 창의 드로 목록에 그려지므로 같은 목록에 덧그려야 메뉴나 팝업에 가려집니다.
        ImDrawList* DrawList{Window->DockNode->HostWindow->DrawList};
        const float Size{ImGui::GetFontSize()};
        const float Scale{ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};
        ImVec4 Tint{Color != 0 ? ImGui::ColorConvertU32ToFloat4(Color) : ImGui::GetStyleColorVec4(ImGuiCol_Text)};

        const float Emphasis{FEditorAnimation::Float(Window->ID, "TabEmphasis", Window->DockTabIsVisible ? 1.0f : 0.0f, 0.12f, Window->DockTabIsVisible ? 1.0f : 0.0f)};
        Tint.w *= 0.55f + 0.45f * Emphasis;

        DrawList->PushClipRect(Tab.Min, Tab.Max, false);
        Editor::EditorStyle::DrawIcon(DrawList, Icon, ImVec2{Tab.Min.x + 9.0f * Scale, Tab.Min.y + (Tab.GetHeight() - Size) * 0.5f}, Size, ImGui::ColorConvertFloat4ToU32(Tint));
        if (Window->WindowClass.ClassId != EditorStyle::TopLevelTabClassId) {
            DrawList->AddRectFilled({Tab.Min.x + 4.0f * Scale, Tab.Max.y - 2.0f * Scale}, {Tab.Min.x + 4.0f * Scale + std::max(0.0f, Tab.GetWidth() - 8.0f * Scale) * Emphasis, Tab.Max.y}, ImGui::GetColorU32(ImGuiCol_HeaderActive, Emphasis), Scale);
        }
        DrawList->PopClipRect();
    }

    void Editor::EditorStyle::DrawIcon(ImDrawList* DrawList, EIcon Icon, const ImVec2& Origin, float Size, ImU32 Color) {
        const float Scale{Size / 20.0f};
        const float Thickness{1.6f * Scale};
        const auto Point{[&Origin, Scale](float X, float Y) {
            return ImVec2{Origin.x + X * Scale, Origin.y + Y * Scale};
        }};
        const auto Line{[DrawList, &Point, Color, Thickness](float X1, float Y1, float X2, float Y2) {
            DrawList->AddLine(Point(X1, Y1), Point(X2, Y2), Color, Thickness);
        }};

        switch (Icon) {
        case EIcon::Tools: {
            const Core::TStaticArray<ImVec2, 6> Cube{Point(2.0f, 5.0f), Point(8.0f, 2.0f), Point(14.0f, 5.0f), Point(14.0f, 12.0f), Point(8.0f, 15.0f), Point(2.0f, 12.0f)};

            DrawList->AddPolyline(Cube.data(), static_cast<int>(Cube.size()), Color, Thickness, ImDrawFlags_Closed);
            Line(2.0f, 5.0f, 8.0f, 8.0f);
            Line(8.0f, 8.0f, 14.0f, 5.0f);
            Line(8.0f, 8.0f, 8.0f, 15.0f);
            DrawList->AddTriangleFilled(Point(10.0f, 9.0f), Point(18.0f, 14.0f), Point(12.0f, 16.0f), Color);
            Line(14.0f, 14.0f, 16.0f, 18.0f);

            break;
        }

        case EIcon::Translate:
            Line(10.0f, 3.0f, 10.0f, 17.0f);
            Line(3.0f, 10.0f, 17.0f, 10.0f);
            DrawList->AddTriangleFilled(Point(10.0f, 1.0f), Point(7.0f, 5.0f), Point(13.0f, 5.0f), Color);
            DrawList->AddTriangleFilled(Point(10.0f, 19.0f), Point(7.0f, 15.0f), Point(13.0f, 15.0f), Color);
            DrawList->AddTriangleFilled(Point(1.0f, 10.0f), Point(5.0f, 7.0f), Point(5.0f, 13.0f), Color);
            DrawList->AddTriangleFilled(Point(19.0f, 10.0f), Point(15.0f, 7.0f), Point(15.0f, 13.0f), Color);

            break;
        case EIcon::Rotate:
            DrawList->PathArcTo(Point(10.0f, 10.0f), 7.0f * Scale, -2.25f, 2.55f, 24);
            DrawList->PathStroke(Color, Thickness);
            DrawList->AddTriangleFilled(Point(3.0f, 12.0f), Point(3.0f, 18.0f), Point(8.0f, 14.0f), Color);

            break;
        case EIcon::Scale:
            Line(3.0f, 3.0f, 17.0f, 3.0f);
            Line(17.0f, 3.0f, 17.0f, 17.0f);
            Line(17.0f, 17.0f, 11.0f, 17.0f);
            Line(3.0f, 3.0f, 3.0f, 9.0f);
            DrawList->AddRectFilled(Point(2.0f, 12.0f), Point(8.0f, 18.0f), Color, Scale);

            break;
        case EIcon::World:
            DrawList->AddCircle(Point(10.0f, 10.0f), 8.0f * Scale, Color, 24, Thickness);
            DrawList->AddEllipse(Point(10.0f, 10.0f), ImVec2{3.5f * Scale, 8.0f * Scale}, Color, 0.0f, 24, Thickness);
            Line(2.0f, 10.0f, 18.0f, 10.0f);

            break;
        case EIcon::Local: {
            const Core::TStaticArray<ImVec2, 6> Cube{Point(3.0f, 6.0f), Point(10.0f, 2.0f), Point(17.0f, 6.0f), Point(17.0f, 14.0f), Point(10.0f, 18.0f), Point(3.0f, 14.0f)};

            DrawList->AddPolyline(Cube.data(), static_cast<int>(Cube.size()), Color, Thickness, ImDrawFlags_Closed);
            Line(3.0f, 6.0f, 10.0f, 10.0f);
            Line(10.0f, 10.0f, 17.0f, 6.0f);
            Line(10.0f, 10.0f, 10.0f, 18.0f);

            break;
        }

        case EIcon::Grid:
            for (Index_t Index{}; Index < 4; ++Index) {
                const float Offset{3.0f + static_cast<float>(Index) * 4.5f};

                Line(Offset, 3.0f, Offset, 16.5f);
                Line(3.0f, Offset, 16.5f, Offset);
            }

            break;
        case EIcon::Play:
            DrawList->AddTriangleFilled(Point(6.0f, 3.0f), Point(17.0f, 10.0f), Point(6.0f, 17.0f), Color);

            break;
        case EIcon::Pause:
            DrawList->AddRectFilled(Point(4.0f, 4.0f), Point(8.0f, 16.0f), Color, Scale);
            DrawList->AddRectFilled(Point(12.0f, 4.0f), Point(16.0f, 16.0f), Color, Scale);

            break;
        case EIcon::Simulate:
            DrawList->AddTriangleFilled(Point(6.0f, 2.0f), Point(17.0f, 8.5f), Point(6.0f, 15.0f), Color);
            Line(3.0f, 18.0f, 18.0f, 18.0f);
            Line(3.0f, 18.0f, 5.0f, 14.0f);
            Line(18.0f, 18.0f, 16.0f, 14.0f);

            break;
        case EIcon::More:
            DrawList->AddCircleFilled(Point(10.0f, 4.0f), 1.5f * Scale, Color);
            DrawList->AddCircleFilled(Point(10.0f, 10.0f), 1.5f * Scale, Color);
            DrawList->AddCircleFilled(Point(10.0f, 16.0f), 1.5f * Scale, Color);

            break;
        case EIcon::Cinematics:
            DrawList->AddRect(Point(2.0f, 8.0f), Point(18.0f, 18.0f), Color, Scale, ImDrawFlags_None, Thickness);
            Line(2.0f, 8.0f, 18.0f, 4.0f);
            Line(2.0f, 4.0f, 17.0f, 1.0f);
            Line(2.0f, 4.0f, 2.0f, 8.0f);
            Line(17.0f, 1.0f, 18.0f, 4.0f);
            Line(6.0f, 3.2f, 8.0f, 6.5f);
            Line(11.0f, 2.2f, 13.0f, 5.2f);

            break;
        case EIcon::JumpStart:
            DrawList->AddRectFilled(Point(2.0f, 4.0f), Point(5.0f, 16.0f), Color, Scale);
            DrawList->AddTriangleFilled(Point(17.0f, 4.0f), Point(17.0f, 16.0f), Point(7.0f, 10.0f), Color);

            break;
        case EIcon::JumpEnd:
        case EIcon::Step:
            DrawList->AddTriangleFilled(Point(3.0f, 4.0f), Point(13.0f, 10.0f), Point(3.0f, 16.0f), Color);
            DrawList->AddRectFilled(Point(15.0f, 4.0f), Point(18.0f, 16.0f), Color, Scale);

            break;
        case EIcon::Reset:
            Line(7.5f, 5.0f, 3.0f, 9.5f);
            Line(3.0f, 9.5f, 7.5f, 14.0f);
            DrawList->PathLineTo(Point(3.0f, 9.5f));
            DrawList->PathLineTo(Point(12.0f, 9.5f));
            DrawList->PathBezierQuadraticCurveTo(Point(17.0f, 9.5f), Point(17.0f, 15.5f));
            DrawList->PathStroke(Color, ImDrawFlags_None, Thickness);

            break;
        case EIcon::Stop:
            DrawList->AddRectFilled(Point(4.0f, 4.0f), Point(16.0f, 16.0f), Color, Scale);

            break;
        case EIcon::QuadView:
            DrawList->AddRect(Point(2.0f, 2.0f), Point(18.0f, 18.0f), Color, Scale, ImDrawFlags_None, Thickness);
            Line(10.0f, 2.0f, 10.0f, 18.0f);
            Line(2.0f, 10.0f, 18.0f, 10.0f);

            break;
        case EIcon::Maximize:
            Line(3.0f, 8.0f, 3.0f, 3.0f);
            Line(3.0f, 3.0f, 8.0f, 3.0f);
            Line(12.0f, 3.0f, 17.0f, 3.0f);
            Line(17.0f, 3.0f, 17.0f, 8.0f);
            Line(17.0f, 12.0f, 17.0f, 17.0f);
            Line(17.0f, 17.0f, 12.0f, 17.0f);
            Line(8.0f, 17.0f, 3.0f, 17.0f);
            Line(3.0f, 17.0f, 3.0f, 12.0f);

            break;
        case EIcon::Light:
            DrawList->AddCircle(Point(10.0f, 8.0f), 5.5f * Scale, Color, 24, Thickness);
            Line(7.5f, 15.0f, 12.5f, 15.0f);
            Line(8.5f, 18.0f, 11.5f, 18.0f);

            break;
        case EIcon::Camera: {
            const Core::TStaticArray<ImVec2, 4> Lens{Point(13.0f, 9.0f), Point(18.0f, 6.0f), Point(18.0f, 15.0f), Point(13.0f, 12.0f)};

            DrawList->AddRect(Point(2.0f, 6.0f), Point(13.0f, 15.0f), Color, Scale, ImDrawFlags_None, Thickness);
            DrawList->AddPolyline(Lens.data(), static_cast<int>(Lens.size()), Color, Thickness, ImDrawFlags_Closed);

            break;
        }

        case EIcon::CameraSpeed: {
            const Core::TStaticArray<ImVec2, 4> Lens{Point(11.0f, 5.0f), Point(16.5f, 2.5f), Point(16.5f, 10.5f), Point(11.0f, 8.0f)};

            DrawList->AddRect(Point(2.0f, 2.5f), Point(11.0f, 10.5f), Color, Scale, Thickness);
            DrawList->AddPolyline(Lens.data(), static_cast<int>(Lens.size()), Color, Thickness, ImDrawFlags_Closed);
            Line(2.0f, 15.5f, 3.0f, 15.5f);
            Line(5.5f, 15.5f, 6.5f, 15.5f);
            Line(9.0f, 15.5f, 18.5f, 15.5f);
            Line(15.0f, 12.5f, 18.5f, 15.5f);
            Line(18.5f, 15.5f, 15.0f, 18.5f);

            break;
        }

        case EIcon::Skeleton:
            Line(6.0f, 14.0f, 14.0f, 6.0f);
            DrawList->AddCircle(Point(4.5f, 15.5f), 2.5f * Scale, Color, 12, Thickness);
            DrawList->AddCircle(Point(15.5f, 4.5f), 2.5f * Scale, Color, 12, Thickness);

            break;
        case EIcon::Collision:
            DrawList->AddRect(Point(5.0f, 2.0f), Point(15.0f, 18.0f), Color, 5.0f * Scale, ImDrawFlags_None, Thickness);

            break;
        case EIcon::Sprite: {
            const Core::TStaticArray<ImVec2, 4> Hills{Point(3.0f, 15.0f), Point(8.0f, 10.0f), Point(11.0f, 13.0f), Point(17.0f, 8.0f)};

            DrawList->AddRect(Point(2.0f, 3.0f), Point(18.0f, 17.0f), Color, Scale, ImDrawFlags_None, Thickness);
            DrawList->AddCircleFilled(Point(7.0f, 7.5f), 1.5f * Scale, Color);
            DrawList->AddPolyline(Hills.data(), static_cast<int>(Hills.size()), Color, Thickness, ImDrawFlags_None);

            break;
        }

        case EIcon::ParticleSystem:
            DrawList->AddQuadFilled(Point(8.0f, 4.0f), Point(12.0f, 10.0f), Point(8.0f, 16.0f), Point(4.0f, 10.0f), Color);
            DrawList->AddCircleFilled(Point(15.0f, 5.0f), 2.0f * Scale, Color);
            DrawList->AddCircleFilled(Point(16.5f, 13.0f), 1.5f * Scale, Color);
            DrawList->AddCircleFilled(Point(3.0f, 16.0f), 1.5f * Scale, Color);

            break;
        case EIcon::Text:
            Line(4.0f, 4.0f, 16.0f, 4.0f);
            Line(10.0f, 4.0f, 10.0f, 17.0f);
            Line(7.0f, 17.0f, 13.0f, 17.0f);

            break;
        case EIcon::Fog:
            Line(2.0f, 6.0f, 14.0f, 6.0f);
            Line(6.0f, 10.0f, 18.0f, 10.0f);
            Line(2.0f, 14.0f, 14.0f, 14.0f);

            break;
        case EIcon::SkyDome:
            DrawList->PathLineTo(Point(2.5f, 14.0f));
            DrawList->PathBezierCubicCurveTo(Point(2.5f, 3.0f), Point(17.5f, 3.0f), Point(17.5f, 14.0f));
            DrawList->PathStroke(Color, ImDrawFlags_Closed, Thickness);
            DrawList->PathLineTo(Point(7.0f, 14.0f));
            DrawList->PathBezierQuadraticCurveTo(Point(7.0f, 8.0f), Point(10.0f, 5.75f));
            DrawList->PathStroke(Color, ImDrawFlags_None, Thickness);

            break;
        case EIcon::Sequence:
            DrawList->AddRect(Point(2.0f, 4.0f), Point(18.0f, 16.0f), Color, Scale, ImDrawFlags_None, Thickness);
            Line(7.0f, 4.0f, 7.0f, 16.0f);
            Line(13.0f, 4.0f, 13.0f, 16.0f);

            break;
        case EIcon::Component: {
            const Core::TStaticArray<ImVec2, 4> Diamond{Point(10.0f, 3.0f), Point(17.0f, 10.0f), Point(10.0f, 17.0f), Point(3.0f, 10.0f)};

            DrawList->AddPolyline(Diamond.data(), static_cast<int>(Diamond.size()), Color, Thickness, ImDrawFlags_Closed);

            break;
        }

        case EIcon::Visible:
        case EIcon::Hidden:
            DrawList->PathLineTo(Point(1.5f, 10.0f));
            DrawList->PathBezierQuadraticCurveTo(Point(10.0f, 0.5f), Point(18.5f, 10.0f));
            DrawList->PathBezierQuadraticCurveTo(Point(10.0f, 19.5f), Point(1.5f, 10.0f));
            DrawList->PathStroke(Color, ImDrawFlags_None, Thickness);
            DrawList->AddCircleFilled(Point(10.0f, 10.0f), 2.75f * Scale, Color);

            if (Icon == EIcon::Hidden) {
                Line(3.5f, 16.5f, 16.5f, 3.5f);
            }

            break;
        case EIcon::Save: {
            const Core::TStaticArray<ImVec2, 5> Disk{Point(3.0f, 3.0f), Point(14.0f, 3.0f), Point(17.0f, 6.0f), Point(17.0f, 17.0f), Point(3.0f, 17.0f)};

            DrawList->AddPolyline(Disk.data(), static_cast<int>(Disk.size()), Color, Thickness, ImDrawFlags_Closed);
            DrawList->AddRectFilled(Point(6.0f, 3.0f), Point(13.0f, 7.5f), Color);
            DrawList->AddRect(Point(6.0f, 11.0f), Point(14.0f, 17.0f), Color, 0.0f, ImDrawFlags_None, Thickness);

            break;
        }

        case EIcon::Add: {
            const Core::TStaticArray<ImVec2, 6> Cube{Point(2.0f, 7.0f), Point(8.0f, 4.0f), Point(14.0f, 7.0f), Point(14.0f, 14.0f), Point(8.0f, 17.0f), Point(2.0f, 14.0f)};

            DrawList->AddPolyline(Cube.data(), static_cast<int>(Cube.size()), Color, Thickness, ImDrawFlags_Closed);
            Line(2.0f, 7.0f, 8.0f, 10.0f);
            Line(8.0f, 10.0f, 14.0f, 7.0f);
            Line(8.0f, 10.0f, 8.0f, 17.0f);
            DrawList->AddLine(Point(16.0f, 1.0f), Point(16.0f, 7.0f), IM_COL32(110, 200, 90, 255), Thickness * 1.3f);
            DrawList->AddLine(Point(13.0f, 4.0f), Point(19.0f, 4.0f), IM_COL32(110, 200, 90, 255), Thickness * 1.3f);

            break;
        }

        case EIcon::Settings:
            DrawList->AddCircle(Point(10.0f, 10.0f), 5.5f * Scale, Color, 20, Thickness * 1.4f);
            DrawList->AddCircleFilled(Point(10.0f, 10.0f), 1.8f * Scale, Color);

            for (Index_t Index{}; Index < 8; ++Index) {
                const float Angle{(45_deg * Index).GetBase()};
                const float X{cosf(Angle)};
                const float Y{sinf(Angle)};

                DrawList->AddLine(Point(10.0f + X * 6.0f, 10.0f + Y * 6.0f), Point(10.0f + X * 8.5f, 10.0f + Y * 8.5f), Color, Thickness * 1.6f);
            }

            break;
        case EIcon::Folder: {
            const Core::TStaticArray<ImVec2, 6> Folder{Point(2.0f, 5.0f), Point(8.0f, 5.0f), Point(10.0f, 7.0f), Point(18.0f, 7.0f), Point(18.0f, 16.0f), Point(2.0f, 16.0f)};

            DrawList->AddConvexPolyFilled(Folder.data(), static_cast<int>(Folder.size()), Color);

            break;
        }

        case EIcon::Log:
            DrawList->AddRect(Point(2.0f, 3.0f), Point(18.0f, 17.0f), Color, 2.0f * Scale, ImDrawFlags_None, Thickness);
            Line(5.0f, 7.0f, 8.0f, 10.0f);
            Line(8.0f, 10.0f, 5.0f, 13.0f);
            Line(10.0f, 13.0f, 15.0f, 13.0f);

            break;
        case EIcon::Outliner:
            for (Index_t Index{}; Index < 3; ++Index) {
                const float Y{5.0f + static_cast<float>(Index) * 5.0f};

                DrawList->AddCircleFilled(Point(4.0f, Y), 1.4f * Scale, Color);
                Line(7.5f, Y, 17.0f, Y);
            }

            break;
        case EIcon::Details: {
            const Core::TStaticArray<ImVec2, 5> Pencil{Point(4.0f, 16.0f), Point(4.5f, 12.5f), Point(13.5f, 3.5f), Point(16.5f, 6.5f), Point(7.5f, 15.5f)};

            DrawList->AddPolyline(Pencil.data(), static_cast<int>(Pencil.size()), Color, Thickness, ImDrawFlags_Closed);
            Line(11.5f, 5.5f, 14.5f, 8.5f);

            break;
        }

        case EIcon::Material:
            DrawList->AddCircleFilled(Point(10.0f, 10.0f), 7.5f * Scale, Color, 24);
            DrawList->AddCircleFilled(Point(7.5f, 7.5f), 2.2f * Scale, IM_COL32(255, 255, 255, 150), 12);

            break;
        case EIcon::Select: {
            const Core::TStaticArray<ImVec2, 7> Arrow{Point(5.0f, 2.0f), Point(5.0f, 16.0f), Point(8.5f, 12.5f), Point(11.0f, 18.0f), Point(13.0f, 17.0f), Point(10.5f, 11.5f), Point(15.0f, 11.5f)};

            DrawList->AddPolyline(Arrow.data(), static_cast<int>(Arrow.size()), Color, Thickness, ImDrawFlags_Closed);

            break;
        }

        case EIcon::Snapping:
            DrawList->PathLineTo(Point(3.0f, 3.0f));
            DrawList->PathArcTo(Point(10.0f, 10.0f), 7.0f * Scale, (180_deg).GetBase(), (0_rad).GetBase(), 12);
            DrawList->PathLineTo(Point(17.0f, 3.0f));
            DrawList->PathLineTo(Point(13.0f, 3.0f));
            DrawList->PathArcTo(Point(10.0f, 10.0f), 3.0f * Scale, (0_rad).GetBase(), (180_deg).GetBase(), 8);
            DrawList->PathLineTo(Point(7.0f, 3.0f));
            DrawList->PathStroke(Color, Thickness, ImDrawFlags_Closed);
            Line(3.0f, 7.0f, 7.0f, 7.0f);
            Line(13.0f, 7.0f, 17.0f, 7.0f);

            break;
        case EIcon::SurfaceSnap:
            Line(2.0f, 17.0f, 18.0f, 10.0f);
            Line(2.0f, 19.0f, 18.0f, 19.0f);
            Line(10.0f, 2.0f, 10.0f, 11.0f);
            Line(7.0f, 8.0f, 10.0f, 11.0f);
            Line(10.0f, 11.0f, 13.0f, 8.0f);

            break;
        case EIcon::Performance:
            DrawList->PathArcTo(Point(10.0f, 12.0f), 8.0f * Scale, (180_deg).GetBase(), (1_turn).GetBase(), 16);
            DrawList->PathStroke(Color, Thickness);
            Line(2.0f, 12.0f, 2.0f, 16.0f);
            Line(18.0f, 12.0f, 18.0f, 16.0f);
            Line(2.0f, 16.0f, 18.0f, 16.0f);
            Line(10.0f, 12.0f, 14.0f, 8.0f);
            DrawList->AddCircleFilled(Point(10.0f, 12.0f), 1.4f * Scale, Color);

            break;
        case EIcon::Warning:
            DrawList->AddTriangle(Point(10.0f, 2.0f), Point(18.0f, 17.0f), Point(2.0f, 17.0f), Color, Thickness);
            Line(10.0f, 7.0f, 10.0f, 11.5f);
            DrawList->AddCircleFilled(Point(10.0f, 14.0f), 0.95f * Scale, Color);

            break;
        }
    }

    namespace {
        ImVec4 GetNeutralButtonColor(float Emphasis) {
            const ImGuiStyle& Style{ImGui::GetStyle()};

            return ImLerp(Style.Colors[ImGuiCol_Button], Style.Colors[ImGuiCol_Text], Emphasis);
        }

        bool HoverBackgroundButton(const char* Label, const ImVec2& Size) {
            const bool Pressed{ImGui::InvisibleButton(Label, Size, ImGuiButtonFlags_EnableNav)};

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride)) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(GetNeutralButtonColor(0.12f)), ImGui::GetStyle().FrameRounding);
            }

            return Pressed;
        }
    }

    /**
     * @fn bool Editor::EditorStyle::IconButton(const char* Label, EIcon Icon, bool Selected, bool Enabled, bool Dropdown, const char* Tooltip, float MinimumWidth, EIconSelectionStyle SelectionStyle)
     * @brief 아이콘 버튼을 표시하고 선택 색상, 비활성 처리와 툴팁을 적용합니다.
     */

    /**
     * @brief 선택 및 비활성 상태를 반영하는 아이콘 버튼과 툴팁을 표시합니다.
     */
    bool Editor::EditorStyle::IconButton(const char* Label, EIcon Icon, bool Selected, bool Enabled, bool Dropdown, const char* Tooltip, float MinimumWidth, EIconSelectionStyle SelectionStyle) {
        const float Scale{ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};
        const bool HighlightIcon{SelectionStyle == EIconSelectionStyle::Icon};
        const bool HighlightBackground{Selected && !HighlightIcon};

        ImGui::PushID(Label);
        ImGui::BeginDisabled(!Enabled);

        if (HighlightBackground) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        }

        const float Height{ImGui::GetFrameHeight()};
        const float IconSize{ImGui::GetFontSize() + 2.0f * Scale};
        const float Width{std::max(MinimumWidth, Height + (Dropdown ? 12.0f * Scale : 0.0f))};
        const ImVec2 Size{Width, Height};
        const bool Pressed{HighlightIcon ? HoverBackgroundButton("##Button", Size) : EditorStyle::Button("##Button", Size, HighlightBackground)};
        const ImVec2 Minimum{ImGui::GetItemRectMin()};
        const ImVec2 Origin{Minimum.x + (Height - IconSize) * 0.5f, Minimum.y + (Height - IconSize) * 0.5f};
        const ImVec4 Foreground{Icon == EIcon::Play ? ImVec4{0.30f, 0.85f, 0.40f, 1.0f} : Icon == EIcon::Stop ? ImVec4{0.95f, 0.30f, 0.28f, 1.0f} : ImGui::GetStyleColorVec4(ImGuiCol_Text)};
        const ImU32 Color{ImGui::GetColorU32(Foreground)};

        Editor::EditorStyle::DrawIcon(Icon, Origin, IconSize, Selected && HighlightIcon ? ImGui::GetColorU32(ImGuiCol_CheckMark) : Color);

        if (Dropdown) {
            ImDrawList* DrawList{ImGui::GetWindowDrawList()};
            const float X{Minimum.x + Height + 3.0f * Scale};
            const float Y{Minimum.y + Height * 0.5f};

            DrawList->AddTriangleFilled(ImVec2{X - 3.0f * Scale, Y - 1.5f * Scale}, ImVec2{X + 3.0f * Scale, Y - 1.5f * Scale}, ImVec2{X, Y + 1.5f * Scale}, Color);
        }

        if (HighlightBackground) {
            ImGui::PopStyleColor(2);
        }

        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", Tooltip != nullptr ? Tooltip : Label);
        }

        ImGui::PopID();

        return Pressed;
    }

    float Editor::EditorStyle::GetSplitIconButtonWidth(const char* MenuLabel) {
        const float Height{ImGui::GetFrameHeight()};

        return Height + (MenuLabel != nullptr ? GetLabelButtonWidth(MenuLabel, nullptr, true) : std::floor(Height * 0.55f));
    }

    Editor::EditorStyle::ESplitIconPress Editor::EditorStyle::SplitIconButton(const char* Label, EIcon Icon, const char* ActionTooltip, const char* MenuTooltip, const FSplitIconButtonOptions& Options) {
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const bool HasValueMenu{Options.mMenuLabel != nullptr};
        const float Scale{Style.FontScaleMain * Style.FontScaleDpi};
        const float Height{ImGui::GetFrameHeight()};
        const float FullWidth{GetSplitIconButtonWidth(Options.mMenuLabel)};
        const float MenuWidth{FullWidth - Height};
        const ImVec2 Minimum{ImGui::GetCursorScreenPos()};
        const ImVec2 Maximum{Minimum.x + FullWidth, Minimum.y + Height};
        ImDrawList* DrawList{ImGui::GetWindowDrawList()};
        const auto DrawSegment{[&](bool Menu) {
            const float Width{Menu ? MenuWidth : Height};
            ImGui::BeginDisabled(!Menu && !Options.mActionEnabled);
            const bool Pressed{ImGui::InvisibleButton(Menu ? "##Menu" : "##Action", {Width, Height}, ImGuiButtonFlags_EnableNav)};

            if (ImGui::IsItemVisible()) {
                const ImVec2 Origin{ImGui::GetItemRectMin()};
                const ImVec2 End{ImGui::GetItemRectMax()};
                const ImU32 Foreground{ImGui::GetColorU32(!Menu && Options.mActionChecked ? ImGuiCol_CheckMark : ImGuiCol_Text)};

                if (Menu && HasValueMenu) {
                    const float TextY{Origin.y + (Height - ImGui::GetFontSize()) * 0.5f};
                    const float ArrowX{End.x - Style.FramePadding.x - 3.0f * Scale};
                    const float ArrowY{Origin.y + Height * 0.5f};
                    DrawList->AddText({Origin.x + Style.FramePadding.x, TextY}, Foreground, Options.mMenuLabel);
                    DrawList->AddTriangleFilled({ArrowX - 3.0f * Scale, ArrowY - 1.5f * Scale}, {ArrowX + 3.0f * Scale, ArrowY - 1.5f * Scale}, {ArrowX, ArrowY + 1.5f * Scale}, Foreground);
                } else {
                    const float IconSize{Menu ? ImGui::GetFontSize() : ImGui::GetFontSize() + 2.0f * Scale};
                    DrawIcon(DrawList, Menu ? EIcon::More : Icon, {Origin.x + (Width - IconSize) * 0.5f, Origin.y + (Height - IconSize) * 0.5f}, IconSize, Foreground);
                }
            }

            ImGui::EndDisabled();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", Menu ? MenuTooltip : ActionTooltip);
            }

            return Pressed;
        }};

        ImGui::PushID(Label);
        ImGui::BeginGroup();

        if ((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(Minimum, Maximum)) {
            DrawList->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(GetNeutralButtonColor(0.12f)), Style.FrameRounding);
        }

        const bool FirstPressed{DrawSegment(Options.mMenuFirst)};
        ImGui::SameLine(0.0f, 0.0f);
        const bool SecondPressed{DrawSegment(!Options.mMenuFirst)};
        ImGui::EndGroup();
        ImGui::PopID();

        if (FirstPressed) {
            return Options.mMenuFirst ? ESplitIconPress::Menu : ESplitIconPress::Action;
        }

        return SecondPressed ? (Options.mMenuFirst ? ESplitIconPress::Action : ESplitIconPress::Menu) : ESplitIconPress::None;
    }

    float Editor::EditorStyle::GetLabelButtonWidth(const char* Label, const EIcon* Icon, bool Dropdown) {
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const float Scale{Style.FontScaleMain * Style.FontScaleDpi};
        const float TextWidth{ImGui::CalcTextSize(Label, nullptr, true).x};

        return Style.FramePadding.x * 2.0f + TextWidth + (Icon != nullptr ? ImGui::GetFontSize() + (TextWidth > 0.0f ? Style.ItemInnerSpacing.x : 0.0f) : 0.0f) + (Dropdown ? 12.0f * Scale : 0.0f);
    }

    /**
     * @fn void Editor::EditorStyle::PushViewportToolbar()
     * @brief 뷰포트 툴바에서 사용할 스타일을 ImGui 스택에 추가합니다.
     */

    /**
     * @brief 선택적 아이콘과 드롭다운 표시를 포함한 텍스트 버튼을 그립니다.
     */
    bool Editor::EditorStyle::LabelButton(const char* Label, const EIcon* Icon, bool Dropdown, bool Enabled, const char* Tooltip, bool HoverOnly) {
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const float Scale{Style.FontScaleMain * Style.FontScaleDpi};
        const float Height{ImGui::GetFrameHeight()};
        const float IconSize{ImGui::GetFontSize()};
        const char* End{ImGui::FindRenderedTextEnd(Label)};
        const ImVec2 TextSize{ImGui::CalcTextSize(Label, End)};
        const float Width{GetLabelButtonWidth(Label, Icon, Dropdown)};

        ImGui::PushID(Label);
        ImGui::BeginDisabled(!Enabled);

        const ImVec2 Size{Width, Height};
        const bool Pressed{HoverOnly ? HoverBackgroundButton("##Button", Size) : EditorStyle::Button("##Button", Size)};
        const ImVec2 Minimum{ImGui::GetItemRectMin()};
        const ImU32 Color{ImGui::GetColorU32(ImGuiCol_Text)};
        ImDrawList* DrawList{ImGui::GetWindowDrawList()};
        float X{Minimum.x + Style.FramePadding.x};

        if (Icon != nullptr) {
            Editor::EditorStyle::DrawIcon(*Icon, ImVec2{X, Minimum.y + (Height - IconSize) * 0.5f}, IconSize, Color);

            X += IconSize + Style.ItemInnerSpacing.x;
        }

        DrawList->AddText(ImVec2{X, Minimum.y + (Height - TextSize.y) * 0.5f}, Color, Label, End);

        if (Dropdown) {
            const float ArrowX{Minimum.x + Width - Style.FramePadding.x - 3.0f * Scale};
            const float ArrowY{Minimum.y + Height * 0.5f};

            DrawList->AddTriangleFilled(ImVec2{ArrowX - 3.5f * Scale, ArrowY - 2.0f * Scale}, ImVec2{ArrowX + 3.5f * Scale, ArrowY - 2.0f * Scale}, ImVec2{ArrowX, ArrowY + 2.0f * Scale}, Color);
        }

        ImGui::EndDisabled();

        if (Tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", Tooltip);
        }

        ImGui::PopID();

        return Pressed;
    }

    void Editor::EditorStyle::DrawFieldLabel(const char* Label, float LabelWidth, float FieldWidth) {
        const ImVec2 Origin{ImGui::GetCursorScreenPos()};
        const float Width{LabelWidth >= 0.0f ? LabelWidth : std::max(0.0f, ImGui::GetContentRegionAvail().x * 0.43f)};
        ImGui::AlignTextToFramePadding();
        ImGui::PushClipRect(Origin, {Origin.x + Width, Origin.y + ImGui::GetFrameHeight()}, true);
        ImGui::TextUnformatted(Label);
        if (ImGui::CalcTextSize(Label).x > Width) {
            ImGui::SetItemTooltip("%s", Label);
        }
        ImGui::PopClipRect();
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos({Origin.x + Width + ImGui::GetStyle().ItemSpacing.x, Origin.y});
        ImGui::SetNextItemWidth(FieldWidth);
    }

    /**
     * @brief EditorStyle 구현에 한정되는 내부 형식·상수·도우미 함수입니다.
     */
    namespace {
        /**
         * @brief 에셋 경로를 콘텐츠 브라우저에 표시하도록 요청하는 콜백입니다.
         */
        std::function<void(const std::filesystem::path&)> AssetBrowseHandler{};
    }

    void Editor::EditorStyle::SetAssetBrowseHandler(std::function<void(const std::filesystem::path&)> Handler) {
        AssetBrowseHandler = std::move(Handler);
    }

    void Editor::EditorStyle::BrowseAsset(const std::filesystem::path& AssetPath) {
        if (AssetBrowseHandler && !AssetPath.empty()) {
            AssetBrowseHandler(AssetPath);
        }
    }

    void Editor::EditorStyle::PushViewportToolbar() {
        const float Scale{ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{5.0f * Scale, 5.0f * Scale});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2.0f * Scale, 4.0f * Scale});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * Scale);

        ImVec4 Background{ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg)};

        Background.w = 0.92f;

        ImGui::PushStyleColor(ImGuiCol_Button, Background);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    }

    /**
     * @fn void Editor::EditorStyle::PopViewportToolbar()
     * @brief 뷰포트 툴바의 스타일 스택 변경을 되돌립니다.
     */

    /**
     * @brief PushViewportToolbar()로 적용한 스타일을 복원합니다.
     */
    void Editor::EditorStyle::PopViewportToolbar() {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    /**
     * @fn bool Editor::EditorStyle::IconMenuItem(const char* Label, EIcon Icon, bool Selected, bool Enabled)
     * @brief 메뉴의 기본 아이콘 열에 벡터 아이콘을 배치하여 항목 이름과 체크 표시를 정렬합니다.
     */

    /**
     * @brief 메뉴의 아이콘 열을 확보하고 선택 및 비활성 상태를 반영한 항목을 표시합니다.
     */
    bool Editor::EditorStyle::IconMenuItem(const char* Label, EIcon Icon, bool Selected, bool Enabled) {
        const ImVec2 Position{ImGui::GetCursorScreenPos()};
        const float Size{ImGui::GetFontSize()};

        ImGui::GetCurrentWindow()->DC.MenuColumns.DeclColumns(Size, 0.0f, 0.0f, 0.0f);
        ImGui::BeginDisabled(!Enabled);

        const bool Pressed{ImGui::MenuItem(Label, nullptr, Selected)};
        const ImVec2 Minimum{ImGui::GetItemRectMin()};
        const ImVec2 Maximum{ImGui::GetItemRectMax()};

        Editor::EditorStyle::DrawIcon(Icon, ImVec2{Position.x, (Minimum.y + Maximum.y - Size) * 0.5f}, Size, ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::EndDisabled();

        return Pressed;
    }

}
