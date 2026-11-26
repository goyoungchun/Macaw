/**
 * @file FConsoleWidget.cpp
 * @brief 콘솔 로그와 명령 입력을 표시하는 에디터 패널입니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Editor/Widget/FConsoleWidget.h"
#include "Editor/FEditorContext.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <limits>
#include <string_view>

namespace Editor {

    /**
     * @brief 이 파일에서 사용하는 내부 보조 기능과 상태를 묶습니다.
     */
    namespace {
        /**
         * @brief 현재 테마의 콘솔 강조 색상을 반환합니다.
         * @return 현재 테마의 콘솔 강조 색상입니다.
         */
        ImVec4 GetConsoleAccent() {
            const ImVec4& Link{ImGui::GetStyleColorVec4(ImGuiCol_TextLink)};
            const ImVec4& Text{ImGui::GetStyleColorVec4(ImGuiCol_Text)};

            return {0.6f * Link.x + 0.4f * Text.x, 0.6f * Link.y + 0.4f * Text.y, 0.6f * Link.z + 0.4f * Text.z, Text.w};
        }
    }

    /**
     * @brief 편집 로그 출력과 필터, 텍스트 선택을 관리합니다.
     */
    class Editor::FConsoleWidget::FOutput {
    private:
        struct FLine {
            std::size_t mBegin{};
            std::size_t mEnd{};
            bool mIsCommand{};
        };

        struct FMatch {
            std::size_t mBegin{};
            std::size_t mEnd{};
        };

    public:
        /**
         * @brief 로그와 선택적 필터를 예약된 UI 높이에 맞춰 그립니다.
         * @param[in] Context 로그 원본인 편집 컨텍스트입니다.
         * @param[in] ReservedHeight 입력·후보 목록에 남겨 둘 높이입니다.
         * @param[in] ShowFilter 콘솔 필터 입력란의 표시 여부입니다.
         */
        void Draw(const Editor::FEditorContext& Context, float ReservedHeight, bool ShowFilter);

    private:
        /**
         * @brief 편집 컨텍스트의 로그 변경분을 콘솔 출력에 반영합니다.
         * @param[in] Context 로그 원본인 편집 컨텍스트입니다.
         */
        bool SyncLogs(const Editor::FEditorContext& Context, float Width);
        void AppendMessage(std::string_view Message);
        void UpdateSelection(const ImVec2& Origin, float LineHeight, bool Dragging);
        std::size_t GetTextOffset(const ImVec2& Position, const ImVec2& Origin, float LineHeight) const;
        void DrawLines(const ImVec2& Origin, float LineHeight) const;

    private:
        Core::TStaticArray<char, 128> mFilterInput{};
        Core::FString mFilter{};
        Core::FString mText{};
        Core::TArray<FLine> mLines{};
        Core::TArray<FMatch> mMatches{};
        std::size_t mSelectionAnchor{};
        std::size_t mSelectionEnd{};
        ImFont* mFont{};
        float mFontSize{};
        float mWrapWidth{};

        /**
         * @brief 콘솔 로그 원본 컨텍스트의 비소유 주소입니다.
         */
        const Editor::FEditorContext* mLogContext{};

        /**
         * @brief 마지막으로 반영한 로그 리비전입니다.
         */
        Uint64 mLastLogRevision{};

        /**
         * @brief 콘솔 출력에 반영한 원본 로그 항목 수입니다.
         */
        std::size_t mLogCount{};
    };

    /**
     * @brief 편집 로그와 필터를 예약된 UI 높이에 맞춰 그립니다.
     */
    void Editor::FConsoleWidget::FOutput::Draw(const Editor::FEditorContext& Context, float ReservedHeight, bool ShowFilter) {
        if (ShowFilter) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##LogFilter", "Filter output", mFilterInput.data(), mFilterInput.size());
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        if (ImGui::BeginChild("##LogOutput", {0.0f, -ReservedHeight}, ImGuiChildFlags_Borders)) {
            const ImVec2 Available{ImGui::GetContentRegionAvail()};
            const float Width{std::max(1.0f, Available.x)};
            const bool WasAtBottom{ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f};
            const bool OutputChanged{SyncLogs(Context, Width)};
            const float LineHeight{ImGui::GetTextLineHeightWithSpacing()};
            const float TextHeight{static_cast<float>(mLines.size()) * LineHeight};
            const float SelectionHeight{std::max(TextHeight, Available.y - ImGui::GetStyle().ItemSpacing.y)};
            const ImVec2 Origin{ImGui::GetCursorScreenPos()};
            ImGui::InvisibleButton("##LogSelection", {Width, std::max(1.0f, SelectionHeight)});

            const bool Dragging{ImGui::IsItemActive()};
            UpdateSelection(Origin, LineHeight, Dragging);
            DrawLines(Origin, LineHeight);

            if (OutputChanged && WasAtBottom && !Dragging && mSelectionAnchor == mSelectionEnd) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    /**
     * @brief 편집 컨텍스트의 로그 변경분을 콘솔 출력에 반영합니다.
     */
    bool Editor::FConsoleWidget::FOutput::SyncLogs(const Editor::FEditorContext& Context, float Width) {
        const Uint64 Revision{Context.GetLogRevision()};
        const std::string_view Filter{mFilterInput.data()};
        const bool FilterChanged{Filter != mFilter};
        const bool LayoutChanged{mFont != ImGui::GetFont() || mFontSize != ImGui::GetFontSize() || mWrapWidth != Width};

        if (mLogContext == &Context && Revision == mLastLogRevision && !FilterChanged && !LayoutChanged) {
            return false;
        }

        const Core::TArray<Core::FString>& Logs{Context.GetLogs()};
        const bool LogsReplaced{mLogContext != &Context || Logs.size() < mLogCount || Revision - mLastLogRevision != Logs.size() - mLogCount};
        const std::size_t PreviousLineCount{mLines.size()};

        if (LogsReplaced || FilterChanged) {
            mSelectionAnchor = 0;
            mSelectionEnd = 0;

            if (ImGui::GetActiveID() == ImGui::GetID("##LogSelection")) {
                ImGui::ClearActiveID();
            }
        }

        if (LogsReplaced || FilterChanged || LayoutChanged) {
            mText.clear();
            mLines.clear();
            mMatches.clear();
            mLogCount = 0;
            mFilter = Filter;
            mFont = ImGui::GetFont();
            mFontSize = ImGui::GetFontSize();
            mWrapWidth = Width;
        }

        for (; mLogCount < Logs.size(); ++mLogCount) {
            const std::string_view Message{Logs[mLogCount]};
            if (Message.empty() || Message.find(Filter) != std::string_view::npos) {
                AppendMessage(Message);
            }
        }

        mLogContext = &Context;
        mLastLogRevision = Revision;

        return LogsReplaced || FilterChanged || LayoutChanged || PreviousLineCount != mLines.size();
    }

    void Editor::FConsoleWidget::FOutput::AppendMessage(std::string_view Message) {
        if (!mLines.empty()) {
            mText.push_back('\n');
        }

        const std::size_t MessageOffset{mText.size()};
        mText += Message;

        if (!mFilter.empty()) {
            for (std::size_t Match{Message.find(mFilter)}; Match != std::string_view::npos; Match = Message.find(mFilter, Match + mFilter.size())) {
                mMatches.push_back({MessageOffset + Match, MessageOffset + Match + mFilter.size()});
            }
        }

        const bool IsCommand{Message.starts_with("> ")};
        const char* const Data{Message.data()};
        for (std::size_t Begin{}; Begin <= Message.size();) {
            const std::size_t End{std::min(Message.find('\n', Begin), Message.size())};
            const char* LineBegin{Data + Begin};
            const char* const LineEnd{Data + End};

            do {
                const char* const WrapEnd{ImFontCalcWordWrapPositionEx(mFont, mFontSize, LineBegin, LineEnd, mWrapWidth, ImDrawTextFlags_WrapKeepBlanks)};
                mLines.push_back({MessageOffset + static_cast<std::size_t>(LineBegin - Data), MessageOffset + static_cast<std::size_t>(WrapEnd - Data), IsCommand});
                LineBegin = WrapEnd;
            } while (LineBegin < LineEnd);

            Begin = End + 1;
        }
    }

    void Editor::FConsoleWidget::FOutput::UpdateSelection(const ImVec2& Origin, float LineHeight, bool Dragging) {
        if (ImGui::IsItemHovered() || Dragging) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        }

        const ImVec2 Mouse{ImGui::GetMousePos()};
        const bool Released{ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)};
        if ((Dragging || Released) && ImGui::IsMousePosValid(&Mouse)) {
            mSelectionEnd = GetTextOffset(Mouse, Origin, LineHeight);

            if (ImGui::IsItemActivated()) {
                mSelectionAnchor = mSelectionEnd;
            }

            const ImDrawList* DrawList{ImGui::GetWindowDrawList()};
            const float Top{DrawList->GetClipRectMin().y};
            const float Bottom{DrawList->GetClipRectMax().y};
            const float Distance{Mouse.y - std::clamp(Mouse.y, Top, Bottom)};
            if (Dragging && Distance != 0.0f) {
                ImGui::SetScrollY(ImGui::GetScrollY() + std::clamp(Distance * 8.0f, -900.0f, 900.0f) * ImGui::GetIO().DeltaTime);
            }
        }

        if (!ImGui::IsWindowFocused()) {
            return;
        }

        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
            mSelectionAnchor = 0;
            mSelectionEnd = mText.size();
        }

        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C) && mSelectionAnchor != mSelectionEnd) {
            const auto [Begin, End]{std::minmax(mSelectionAnchor, mSelectionEnd)};
            const Core::FString SelectedText{mText.substr(Begin, End - Begin)};
            ImGui::SetClipboardText(SelectedText.c_str());
        }
    }

    std::size_t Editor::FConsoleWidget::FOutput::GetTextOffset(const ImVec2& Position, const ImVec2& Origin, float LineHeight) const {
        if (mLines.empty() || Position.y < Origin.y) {
            return 0;
        }

        const float LineIndex{(Position.y - Origin.y) / LineHeight};
        if (LineIndex >= static_cast<float>(mLines.size())) {
            return mText.size();
        }

        const FLine& Line{mLines[static_cast<std::size_t>(LineIndex)]};
        const char* const Begin{mText.data() + Line.mBegin};
        const char* const End{mText.data() + Line.mEnd};
        const char* Character{Begin};
        const float X{std::max(0.0f, Position.x - Origin.x)};
        const float Before{mFont->CalcTextSizeA(mFontSize, X, 0.0f, Begin, End, &Character).x};

        if (Character != End) {
            const char* const Next{Character + ImTextCountUtf8BytesFromChar(Character, End)};
            const float Advance{mFont->CalcTextSizeA(mFontSize, std::numeric_limits<float>::max(), 0.0f, Character, Next).x};
            if (X - Before >= Advance * 0.5f) {
                Character = Next;
            }
        }

        return static_cast<std::size_t>(Character - mText.data());
    }

    void Editor::FConsoleWidget::FOutput::DrawLines(const ImVec2& Origin, float LineHeight) const {
        ImDrawList* DrawList{ImGui::GetWindowDrawList()};
        const std::size_t FirstLine{static_cast<std::size_t>(std::max(0.0f, (DrawList->GetClipRectMin().y - Origin.y) / LineHeight))};
        const float LastLine{(DrawList->GetClipRectMax().y - Origin.y) / LineHeight};
        const auto [SelectionBegin, SelectionEnd]{std::minmax(mSelectionAnchor, mSelectionEnd)};
        const ImU32 TextColor{ImGui::GetColorU32(ImGuiCol_Text)};
        const ImU32 AccentColor{ImGui::GetColorU32(GetConsoleAccent())};
        const ImU32 SelectionColor{ImGui::GetColorU32(ImGuiCol_TextSelectedBg)};
        const char* const Data{mText.data()};

        for (std::size_t Index{FirstLine}; Index < mLines.size() && static_cast<float>(Index) < LastLine; ++Index) {
            const FLine& Line{mLines[Index]};
            const float Y{Origin.y + static_cast<float>(Index) * LineHeight};
            const auto GetX{[&](std::size_t Offset) {
                return Origin.x + mFont->CalcTextSizeA(mFontSize, std::numeric_limits<float>::max(), 0.0f, Data + Line.mBegin, Data + Offset).x;
            }};

            if (SelectionBegin < SelectionEnd && SelectionEnd > Line.mBegin && SelectionBegin <= Line.mEnd) {
                const float Left{GetX(std::max(Line.mBegin, SelectionBegin))};
                const bool IncludesNewline{SelectionEnd > Line.mEnd && Line.mEnd < mText.size() && mText[Line.mEnd] == '\n'};
                const float Right{GetX(std::min(Line.mEnd, SelectionEnd)) + (IncludesNewline ? mFontSize * 0.35f : 0.0f)};
                DrawList->AddRectFilled({Left, Y}, {Right, Y + LineHeight}, SelectionColor);
            }

            if (Line.mIsCommand) {
                DrawList->AddText({Origin.x, Y}, AccentColor, Data + Line.mBegin, Data + Line.mEnd);
                continue;
            }

            std::size_t Offset{Line.mBegin};
            for (auto Match{std::ranges::upper_bound(mMatches, Line.mBegin, {}, &FMatch::mEnd)}; Match != mMatches.end() && Match->mBegin < Line.mEnd; ++Match) {
                const std::size_t Begin{std::max(Line.mBegin, Match->mBegin)};
                const std::size_t End{std::min(Line.mEnd, Match->mEnd)};
                DrawList->AddText({GetX(Offset), Y}, TextColor, Data + Offset, Data + Begin);
                DrawList->AddText({GetX(Begin), Y}, AccentColor, Data + Begin, Data + End);
                Offset = End;
            }

            DrawList->AddText({GetX(Offset), Y}, TextColor, Data + Offset, Data + Line.mEnd);
        }
    }

    /**
     * @brief FConsoleWidget의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Editor::FConsoleWidget::FConsoleWidget(Editor::FEditorContext* Context)
		: Editor::FEditorContextWidget{Context, "Console"},
		  mOutput{std::make_unique<Editor::FConsoleWidget::FOutput>()},
		  mCommandInput{Context} {
    }

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Editor::FConsoleWidget::~FConsoleWidget() = default;

    /**
     * @brief 콘솔 패널에 메시지를 추가하고 스크롤 갱신을 요청합니다.
     */
    void Editor::FConsoleWidget::AddLog(const Core::FString& Message) {
        GetEditorContext()->AddLog(Message);
    }

    /**
     * @brief 패널 고유의 ImGui 위젯과 내용을 그립니다.
     */
    void Editor::FConsoleWidget::DrawContent(float) {
        Editor::FEditorContext* Context{GetEditorContext()};
        Context->SyncGlobalLogs();

        const float InputHeight{ImGui::GetFrameHeightWithSpacing()};
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const ImVec2 Available{ImGui::GetContentRegionAvail()};
        const bool ShowFilter{Available.y >= 4.0f * InputHeight && Available.x >= 10.0f * ImGui::GetFontSize()};
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, std::max(Style.ChildRounding, Style.FrameRounding));
        mOutput->Draw(*Context, InputHeight, ShowFilter);
        ImGui::PopStyleVar();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(GetConsoleAccent(), ">");
        ImGui::SameLine(0.0f, Style.ItemInnerSpacing.x);
        mCommandInput.Draw("OutputCommand", ImGui::GetContentRegionAvail().x, true);
    }

}
