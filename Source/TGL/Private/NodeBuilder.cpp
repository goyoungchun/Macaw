/**
 * @file NodeBuilder.cpp
 * @brief 노드 하나를 헤더 → 핀 행 → 본문 순서로 그립니다.
 * @ingroup TGL
 */

#include "TGL/NodeBuilder.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace TGL {

    /**
     * @brief 노드 하나를 헤더 → 핀 행 → 본문 순서로 그립니다.
     */
    namespace {

        /**
         * @brief 노드 핀 아이콘 크기(픽셀)입니다.
         */
        constexpr float IconSize{16.0f};

        /**
         * @brief 핀 아이콘과 이름 사이 간격(픽셀)입니다.
         */
        constexpr float IconSpacing{6.0f};

        /**
         * @brief 노드 머리글 내부 여백(픽셀)입니다.
         */
        constexpr float HeaderInset{12.0f};

        /**
         * @brief 노드 속성 열 사이 간격(픽셀)입니다.
         */
        constexpr float PropertySpacing{14.0f};

        /**
         * @brief 노드 제목의 표시 색상입니다.
         */
        constexpr ImVec4 TitleColor{0.94f, 0.96f, 1.0f, 1.0f};

        /**
         * @brief 노드 보조 정보의 표시 색상입니다.
         */
        constexpr ImVec4 SecondaryColor{0.62f, 0.68f, 0.77f, 1.0f};

        /**
         * @brief 노드 값의 표시 색상입니다.
         */
        constexpr ImVec4 ValueColor{0.86f, 0.90f, 0.96f, 1.0f};

        /**
         * @brief 변수에 연결된 속성의 강조 색상입니다.
         */
        constexpr ImVec4 BindingColor{0.50f, 0.81f, 1.0f, 1.0f};

        /**
         * @brief 핀 이름과 아이콘을 함께 표시할 너비를 계산합니다.
         * @param Label UI 항목의 표시 이름입니다.
         * @return 필요한 너비(픽셀)입니다.
         */
        float PinWidth(const char* Label) {
            return ImGui::CalcTextSize(Label).x + (Label[0] != 0 ? IconSpacing : 0.0f) + IconSize;
        }
    }

    void FNodeBuilder::FitHeader(const char* Title, const char* Type) {
        FitText(Title, HeaderInset);
        FitText(Type, HeaderInset);
    }

    void FNodeBuilder::FitText(const char* Text, float Padding) {
        mWidth = std::max(mWidth, ImGui::CalcTextSize(Text).x + Padding);
    }

    void FNodeBuilder::FitPins(const char* Input, const char* Output) {
        const float InputWidth{Input != nullptr ? PinWidth(Input) : 0.0f};
        const float OutputWidth{Output != nullptr ? PinWidth(Output) : 0.0f};
        const float Spacing{Input != nullptr && Output != nullptr ? ImGui::GetStyle().ItemSpacing.x : 0.0f};

        mWidth = std::max(mWidth, InputWidth + Spacing + OutputWidth);
    }

    void FNodeBuilder::FitProperty(const char* Label, const char* Value) {
        mLabelWidth = std::max(mLabelWidth, ImGui::CalcTextSize(Label).x);
        mValueWidth = std::max(mValueWidth, ImGui::CalcTextSize(Value).x);
        mWidth = std::max(mWidth, mLabelWidth + PropertySpacing + mValueWidth);
    }

    void FNodeBuilder::Begin(TGL::Canvas::NodeId Id, float MinWidth, bool Movable) {
        mWidth = std::ceil(std::max(mWidth, MinWidth));
        TGL::Canvas::BeginNode(Id, Movable);
        mStartX = ImGui::GetCursorPosX();
        mStartScreenX = ImGui::GetCursorScreenPos().x;
    }

    void FNodeBuilder::Header(const char* Title, const char* Type, const ImVec4* Accent) {
        const std::string_view TypeName{Type};
        const ImVec4 Color{Accent != nullptr ? *Accent : TGL::Canvas::GetStyle().mNodeAccent};

        ImGui::BeginGroup();
        ImGui::Indent(HeaderInset);
        DrawText(Title, TitleColor);

        if (!TypeName.empty() && TypeName != Title) {
            DrawText(Type, SecondaryColor);
        }

        ImGui::Unindent(HeaderInset);
        ImGui::EndGroup();

        // 헤더 배경은 노드를 끝낸 뒤 이 높이까지 칠합니다. 노드 폭은 헤더 아래 빈 줄로 고정합니다.
        TGL::Canvas::SetNodeHeader(ImGui::GetItemRectMax().y + 7.0f, Color);
        ImGui::Dummy({mWidth, 9.0f});
    }

    void FNodeBuilder::PinIcon(TGL::Canvas::PinKind Kind, bool Connected, const ImVec4& Color) {
        const ImVec2 Cursor{ImGui::GetCursorScreenPos()};
        const float Size{IconSize};
        const float Offset{std::max((ImGui::GetTextLineHeight() - Size) * 0.5f, 0.0f)};
        const ImVec4 Padding{TGL::Canvas::GetStyle().mNodePadding};
        const float X{Kind == TGL::Canvas::PinKind::Input ? mStartScreenX - Padding.x + 1.0f : mStartScreenX + mWidth + Padding.z - 1.0f};
        const float Radius{Size * 0.5f};
        const ImVec2 Anchor{X, Cursor.y + Offset + Radius};

        ImGui::Dummy({Size, std::max(Size, ImGui::GetTextLineHeight())});

        // 핀 영역과 링크가 시작하는 점을 아이콘으로 정해, 링크가 노드 옆 가장자리에서 나가게 합니다.
        TGL::Canvas::PinRect({Anchor.x - Radius, Anchor.y - Radius}, {Anchor.x + Radius, Anchor.y + Radius});

        ImDrawList* DrawList{ImGui::GetWindowDrawList()};
        const ImU32 IconColor{ImGui::GetColorU32(ImVec4{Color.x, Color.y, Color.z, Color.w * (Connected ? 1.0f : 0.58f)})};

        DrawList->AddCircle(Anchor, 5.0f, IconColor, 16, 1.5f);

        if (Connected) {
            DrawList->AddCircleFilled(Anchor, 2.6f, IconColor, 12);
        }
    }

    void FNodeBuilder::InputPin(TGL::Canvas::PinId Id, const char* Label, bool Connected, const ImVec4& Color) {
        ImGui::SetCursorPosX(mStartX);
        TGL::Canvas::BeginPin(Id, TGL::Canvas::PinKind::Input);
        PinIcon(TGL::Canvas::PinKind::Input, Connected, Color);
        if (Label[0] != 0) {
            ImGui::SameLine(0.0f, IconSpacing);
            ImGui::TextColored(Connected ? ValueColor : SecondaryColor, "%s", Label);
        }
        TGL::Canvas::EndPin();
    }

    void FNodeBuilder::OutputPin(TGL::Canvas::PinId Id, const char* Label, bool Connected, const ImVec4& Color, bool SameLine) {
        const float Width{PinWidth(Label)};
        float X{mStartX + mWidth - Width};

        if (SameLine) {
            ImGui::SameLine();
            X = std::max(X, ImGui::GetCursorPosX());
        }

        ImGui::SetCursorPosX(X);
        TGL::Canvas::BeginPin(Id, TGL::Canvas::PinKind::Output);
        if (Label[0] != 0) {
            ImGui::TextColored(Connected ? ValueColor : SecondaryColor, "%s", Label);
            ImGui::SameLine(0.0f, IconSpacing);
        }

        PinIcon(TGL::Canvas::PinKind::Output, Connected, Color);
        TGL::Canvas::EndPin();
    }

    void FNodeBuilder::BeginBody() {
        const ImVec2 Position{ImGui::GetCursorScreenPos()};

        ImGui::GetWindowDrawList()->AddLine({Position.x, Position.y + 2.0f}, {Position.x + mWidth, Position.y + 2.0f}, ImGui::GetColorU32(ImVec4{0.40f, 0.48f, 0.61f, 0.28f}));
        ImGui::Dummy({mWidth, 6.0f});
    }

    void FNodeBuilder::DrawText(const char* Text, const ImVec4& Color) const {
        const ImVec2 Position{ImGui::GetCursorScreenPos()};
        const ImVec2 Size{ImGui::CalcTextSize(Text)};

        ImGui::Dummy(Size);
        ImGui::GetWindowDrawList()->AddText(Position, ImGui::GetColorU32(Color), Text);
    }

    void FNodeBuilder::DrawProperty(const char* Label, const char* Value, bool Bound) const {
        const ImVec2 Position{ImGui::GetCursorScreenPos()};
        const float ValueOffset{(mLabelWidth + PropertySpacing)};
        const float Height{std::max(ImGui::CalcTextSize(Label).y, ImGui::CalcTextSize(Value).y)};
        ImDrawList* DrawList{ImGui::GetWindowDrawList()};

        ImGui::Dummy({mWidth, Height});
        DrawList->AddText(Position, ImGui::GetColorU32(SecondaryColor), Label);
        DrawList->AddText({Position.x + ValueOffset, Position.y}, ImGui::GetColorU32(Bound ? BindingColor : ValueColor), Value);
    }

    void FNodeBuilder::BeginPropertyInput(const char* Label) const {
        ImGui::SetCursorPosX(mStartX);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(SecondaryColor, "%s", Label);
        ImGui::SameLine(0.0f, PropertySpacing);
        const float X{std::max(ImGui::GetCursorPosX(), mStartX + mLabelWidth + PropertySpacing)};
        ImGui::SetCursorPosX(X);
        ImGui::SetNextItemWidth(std::max(40.0f, mWidth - (X - mStartX)));
    }

    void FNodeBuilder::End() {
        TGL::Canvas::EndNode();
    }

    float FNodeBuilder::GetWidth() const {
        return mWidth;
    }

    void FNodeBuilder::DrawValueNode(TGL::Canvas::NodeId Node, TGL::Canvas::PinId Pin, const char* Title, const char* Type, const char* Value, bool Connected, const ImVec4& Color) {
        TGL::FNodeBuilder Builder{};

        Builder.FitHeader(Title, Type);
        Builder.FitPins(nullptr, Value);
        Builder.Begin(Node, 125.0f);
        Builder.Header(Title, Type);
        Builder.OutputPin(Pin, Value, Connected, Color, false);
        Builder.End();
    }

}
