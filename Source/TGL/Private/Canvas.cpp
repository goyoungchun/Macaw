/**
 * @file
 * @brief 논리 노드와 핀, 전이 경로 및 입력 이벤트를 imgui-node-editor에 연결합니다.
 * @ingroup TGL
 */

#include "TGL/Canvas.h"

#include <imgui_node_editor.h>

#include <cmath>
#include <map>
#include <memory>

using namespace TGL;
using namespace TGL::Canvas;

namespace {

    /**
     * @brief TGL 스타일 색의 순서를 네이티브 편집기 색상 항목에 대응시킵니다.
     */
    constexpr ax::NodeEditor::StyleColor StyleColors[]{
        ax::NodeEditor::StyleColor_Bg,
        ax::NodeEditor::StyleColor_Grid,
        ax::NodeEditor::StyleColor_NodeBg,
        ax::NodeEditor::StyleColor_NodeBorder,
        ax::NodeEditor::StyleColor_HovNodeBorder,
        ax::NodeEditor::StyleColor_SelNodeBorder,
        ax::NodeEditor::StyleColor_HovLinkBorder,
        ax::NodeEditor::StyleColor_SelLinkBorder
    };

    /**
     * @brief 사용자 이동을 추적하기 위한 확정 좌표와 마지막 문서 좌표입니다.
     */
    struct FNodePosition {
        ImVec2 mCommitted{}; ///< 캔버스가 마지막으로 확정한 노드 위치입니다.
        std::optional<ImVec2> mModel{}; ///< 문서에서 마지막으로 읽은 위치입니다.
        bool mMovable{true}; ///< 이 노드의 드래그 이동을 허용하는지 나타냅니다.
    };

    /**
     * @brief 노드에 제출한 논리 핀의 방향과 연결 기준점입니다.
     */
    struct FPin {
        PinId mId{}; ///< 노드에 제출하는 논리 핀 식별자입니다.
        PinKind mKind{}; ///< 핀의 입력 또는 출력 방향입니다.
        std::optional<ImVec2> mCenter{}; ///< 핀 사각형을 제출했을 때 계산한 연결 기준점입니다.
    };

    /**
     * @brief 일반 핀과 별도로 배치하는 전이 링크의 두 연결 지점입니다.
     */
    struct FTransitionRoute {
        PinId mFrom{}; ///< 출발 논리 핀입니다.
        PinId mTo{}; ///< 도착 논리 핀입니다.
        ax::NodeEditor::PinId mNativeFrom{}; ///< 전이에만 사용하는 네이티브 출력 핀입니다.
        ax::NodeEditor::PinId mNativeTo{}; ///< 전이에만 사용하는 네이티브 입력 핀입니다.
        ImVec2 mFromPosition{}; ///< 전이의 시작 좌표입니다.
        ImVec2 mToPosition{}; ///< 전이의 끝 좌표입니다.
        bool mLoop{}; ///< 같은 노드로 돌아오는 전이인지 나타냅니다.
    };

    /**
     * @brief 캔버스를 닫기 전에 그릴 링크 레이블의 좌표와 표시 정보입니다.
     */
    struct FLinkLabel {
        ImVec2 mPosition{}; ///< 레이블의 기준 캔버스 좌표입니다.
        std::string mText{}; ///< 레이블 내용입니다.
        ImU32 mColor{}; ///< 레이블 표시 색입니다.
    };

    thread_local EditorContext* Current{}; ///< 현재 스레드에서 사용하는 활성 캔버스입니다.

    /**
     * @brief 두 위치의 좌표가 같은지 비교합니다.
     * @param Left 첫 번째 위치입니다.
     * @param Right 두 번째 위치입니다.
     * @return X와 Y가 모두 같으면 true입니다.
     */
    bool SamePosition(ImVec2 Left, ImVec2 Right) {
        return Left.x == Right.x && Left.y == Right.y;
    }

    /**
     * @brief TGL 핀 방향을 네이티브 편집기의 방향으로 변환합니다.
     * @param Kind 논리 핀 방향입니다.
     * @return 해당하는 네이티브 핀 방향입니다.
     */
    ax::NodeEditor::PinKind ToNative(PinKind Kind) {
        return Kind == PinKind::Input ? ax::NodeEditor::PinKind::Input : ax::NodeEditor::PinKind::Output;
    }

}

/**
 * @brief 네이티브 편집기와 TGL 스타일, 논리 핀, 배치 및 선택 상태를 보관합니다.
 * @ingroup TGL
 */
struct TGL::Canvas::EditorContext {
    ax::NodeEditor::EditorContext* mNative{}; ///< 소유하는 네이티브 편집기 컨텍스트입니다.
    Style mStyle{}; ///< 현재 캔버스의 그리기 스타일입니다.
    std::vector<std::pair<EStyleColor, ImVec4>> mColors{}; ///< 임시 색상을 적용하기 전 값의 스택입니다.
    std::vector<float> mBorders{}; ///< 임시 테두리 굵기를 적용하기 전 값의 스택입니다.
    std::unordered_map<std::uintptr_t, FNodePosition> mPositions{}; ///< 노드 식별자별 문서 좌표와 확정 좌표입니다.
    std::unordered_map<std::uintptr_t, ax::NodeEditor::PinId> mPins{}; ///< 논리 핀마다 사용하는 기본 네이티브 핀입니다.
    std::unordered_map<std::uintptr_t, PinId> mLogicalPins{}; ///< 네이티브 핀에서 논리 핀으로의 역매핑입니다.
    std::map<std::pair<LinkId, PinKind>, ax::NodeEditor::PinId> mRoutePins{}; ///< 전이 링크와 입력·출력 방향마다 할당한 네이티브 핀입니다.
    std::uintptr_t mNextPin{1}; ///< 다음에 발급할 네이티브 핀 식별자입니다.
    std::map<LinkId, FTransitionRoute> mTransitions{}; ///< 이번 프레임에 그릴 전이의 경로입니다.
    std::unordered_map<std::uintptr_t, ImVec2> mPinPositions{}; ///< 논리 핀의 연결 기준점입니다.
    std::vector<FPin> mNodePins{}; ///< 현재 노드에 제출한 핀 목록입니다.
    std::vector<NodeId> mSubmittedNodes{}; ///< 이번 프레임에 제출한 노드 목록입니다.
    std::vector<FSelectionItem> mSelection{}; ///< 네이티브 편집기에서 조회한 선택 목록입니다.
    std::vector<FMoveNode> mMovedNodes{}; ///< 이번 프레임에 확정되어 문서로 전달할 노드 이동입니다.
    std::vector<FLinkLabel> mLabels{}; ///< 이번 프레임에 그릴 링크 레이블입니다.
    std::unordered_map<std::uintptr_t, double> mFlowPulses{}; ///< 링크별 흐름 효과의 재생 제한 시간입니다.
    NodeId mNode{}; ///< 현재 제출 중인 노드의 식별자입니다.
    FPin mPin{}; ///< 현재 제출 중인 논리 핀입니다.
    ImVec2 mCanvasMin{}; ///< 캔버스 화면 영역의 좌상단입니다.
    ImVec2 mCanvasMax{}; ///< 캔버스 화면 영역의 우하단입니다.
    ImVec2 mPointerPosition{}; ///< 캔버스를 시작할 때 변환한 포인터 좌표입니다.
    ImVec4 mHeaderColor{}; ///< 현재 노드의 머리글 강조 색상입니다.
    float mHeaderBottom{}; ///< 현재 노드의 머리글 하단 위치입니다.
    bool mReadOnly{}; ///< 구조 편집과 문서 이동 요청을 비활성화할지 나타냅니다.
    bool mVisible{}; ///< 이번 프레임에 네이티브 캔버스가 시작되었는지 나타냅니다.
    bool mFit{}; ///< 다음 노드 제출에 맞춰 화면을 조정할지 나타냅니다.
};

namespace {

    /**
     * @brief 네이티브 핀 식별자를 발급하고 역방향 매핑을 저장합니다.
     * @param[in,out] Context 핀 식별자와 역매핑을 관리하는 캔버스입니다.
     * @param Id 네이티브 핀이 가리킬 논리 핀입니다.
     * @return 새로 발급한 네이티브 핀 식별자입니다.
     */
    ax::NodeEditor::PinId AddNativePin(EditorContext& Context, PinId Id) {
        const ax::NodeEditor::PinId Native{Context.mNextPin++};
        Context.mLogicalPins.emplace(Native.Get(), Id);

        return Native;
    }

    /**
     * @brief 논리 핀의 기본 네이티브 핀 식별자를 찾거나 발급합니다.
     * @param[in,out] Context 핀 매핑을 관리하는 캔버스입니다.
     * @param Id 변환할 논리 핀입니다.
     * @return 논리 핀에 대응하는 기본 네이티브 핀입니다.
     */
    ax::NodeEditor::PinId GetNativePin(EditorContext& Context, PinId Id) {
        const auto Found{Context.mPins.find(Id.Get())};

        if (Found != Context.mPins.end()) {
            return Found->second;
        }

        const ax::NodeEditor::PinId Native{AddNativePin(Context, Id)};
        Context.mPins.emplace(Id.Get(), Native);

        return Native;
    }

    /**
     * @brief 네이티브 편집기에서 조회한 핀을 논리 핀으로 되돌립니다.
     * @param Native 네이티브 핀 식별자입니다.
     * @return 대응하는 논리 핀입니다. 매핑이 없으면 값은 0입니다.
     */
    PinId GetLogicalPin(ax::NodeEditor::PinId Native) {
        const auto Found{Current->mLogicalPins.find(Native.Get())};

        return Found == Current->mLogicalPins.end() ? PinId{} : Found->second;
    }

    /**
     * @brief 각 전이 링크에 고유한 네이티브 연결 핀을 배치합니다.
     * @details 같은 논리 핀을 지나는 전이를 서로 다른 높이에 놓습니다. 자기 전이의 입출력 방향을 위로 벌려 루프를 표시합니다.
     * @param[in,out] Context 현재 노드의 핀과 이번 프레임의 전이 경로를 보관하는 캔버스입니다.
     */
    void DrawTransitionPins(EditorContext& Context) {
        if (Context.mTransitions.empty()) {
            return;
        }

        const ImVec2 Cursor{ImGui::GetCursorScreenPos()};

        for (const FPin& Pin : Context.mNodePins) {
            const bool Input{Pin.mKind == PinKind::Input};
            const auto Matches{[&Pin, Input](const auto& Entry) { return (Input ? Entry.second.mTo : Entry.second.mFrom) == Pin.mId; }};
            const auto Count{std::ranges::count_if(Context.mTransitions, Matches)};
            float Lane{-0.5f * static_cast<float>(Count - 1)};
            const float Spacing{std::min(12.0f, 44.0f / static_cast<float>(std::max<decltype(Count)>(Count - 1, 1)))};

            for (auto& [Id, Route] : Context.mTransitions) {
                if ((Input ? Route.mTo : Route.mFrom) != Pin.mId) {
                    continue;
                }

                auto [Slot, Inserted]{Context.mRoutePins.try_emplace({Id, Pin.mKind})};

                if (Inserted) {
                    Slot->second = AddNativePin(Context, Pin.mId);
                } else {
                    Context.mLogicalPins.insert_or_assign(Slot->second.Get(), Pin.mId);
                }

                ax::NodeEditor::PinId& Native{Input ? Route.mNativeTo : Route.mNativeFrom};
                Native = Slot->second;
                ImVec2 Position{*Pin.mCenter};
                Position.y += Lane++ * Spacing;
                (Input ? Route.mToPosition : Route.mFromPosition) = Position;

                ImGui::SetCursorScreenPos(Cursor);
                ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize, Input ? 12.0f : 0.0f);
                ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, Input ? 7.0f : 0.0f);

                if (Route.mLoop) {
                    ax::NodeEditor::PushStyleVar(Input ? ax::NodeEditor::StyleVar_TargetDirection : ax::NodeEditor::StyleVar_SourceDirection, ImVec2{Input ? -1.0f : 1.0f, -1.6f});
                }

                ax::NodeEditor::BeginPin(Native, ToNative(Pin.mKind));
                ax::NodeEditor::PinRect(Position, Position);
                ax::NodeEditor::PinPivotRect(Position, Position);
                ax::NodeEditor::EndPin();
                ax::NodeEditor::PopStyleVar(Route.mLoop ? 3 : 2);
            }
        }
    }

    /**
     * @brief 링크 레이블을 화면 좌표로 바꿔 캔버스 영역 안에 그립니다.
     * @param Context 레이블과 캔버스 경계를 보관하는 컨텍스트입니다.
     */
    void DrawLabels(const EditorContext& Context) {
        if (Context.mLabels.empty()) {
            return;
        }

        ax::NodeEditor::Suspend();
        ImDrawList* Draw{ImGui::GetWindowDrawList()};
        Draw->PushClipRect(Context.mCanvasMin, Context.mCanvasMax, true);

        for (const FLinkLabel& Label : Context.mLabels) {
            const ImVec2 Screen{ax::NodeEditor::CanvasToScreen(Label.mPosition)};
            Draw->AddText({Screen.x + 5.0f, Screen.y - ImGui::GetTextLineHeight()}, Label.mColor, Label.mText.c_str());
        }

        Draw->PopClipRect();
        ax::NodeEditor::Resume();
    }

    /**
     * @brief 사용자가 드래그를 끝냈을 때 변경된 노드 위치를 수집합니다.
     * @details 이동을 막은 노드는 확정 위치로 복원합니다. 읽기 전용 캔버스에서는 위치 변경을 문서 요청으로 발행하지 않습니다.
     * @param[in,out] Context 노드 위치와 확정된 이동 목록을 관리하는 캔버스입니다.
     */
    void CaptureLayout(EditorContext& Context) {
        const bool Dragging{ImGui::IsMouseDown(ImGuiMouseButton_Left)};

        for (NodeId Node : Context.mSubmittedNodes) {
            FNodePosition& State{Context.mPositions.at(Node.Get())};
            const ax::NodeEditor::NodeId Native{Node.Get()};
            const ImVec2 Position{ax::NodeEditor::GetNodePosition(Native)};

            if (SamePosition(State.mCommitted, Position)) {
                continue;
            }

            if (!State.mMovable) {
                ax::NodeEditor::SetNodePosition(Native, State.mCommitted);
            } else if (!Dragging) {
                State.mCommitted = Position;

                if (!Context.mReadOnly) {
                    Context.mMovedNodes.push_back({Node, {Position.x, Position.y}});
                }
            }
        }
    }

    /**
     * @brief 네이티브 선택 항목을 TGL 식별자 배열로 복사합니다.
     * @tparam Id 복사할 대상의 TGL 식별자 타입입니다.
     * @tparam NativeId 조회 함수가 반환하는 네이티브 식별자 타입입니다.
     * @param[out] Items 변환한 식별자를 쓸 배열입니다.
     * @param Capacity 배열의 수용 개수입니다.
     * @param GetSelected 네이티브 선택 항목을 읽는 함수입니다.
     * @return 복사한 항목의 개수입니다.
     */
    template <class Id, class NativeId>
    int CopySelection(Id* Items, int Capacity, int (*GetSelected)(NativeId*, int)) {
        if (Capacity <= 0) {
            return 0;
        }

        std::vector<NativeId> Native{};
        Native.resize(static_cast<std::size_t>(Capacity));
        const int Count{GetSelected(Native.data(), Capacity)};

        for (int Index{}; Index < Count; ++Index) {
            Items[Index] = Id{Native[Index].Get()};
        }

        return Count;
    }

}

EditorContext* TGL::Canvas::CreateEditor() {
    ax::NodeEditor::Config Config{};
    Config.SettingsFile = nullptr;
    auto Editor{std::make_unique<EditorContext>()};
    Editor->mNative = ax::NodeEditor::CreateEditor(&Config);

    return Editor.release();
}

void TGL::Canvas::DestroyEditor(EditorContext* Editor) {
    if (Editor == nullptr) {
        return;
    }

    if (Current == Editor) {
        SetCurrentEditor(nullptr);
    }

    ax::NodeEditor::DestroyEditor(Editor->mNative);
    delete Editor;
}

EditorContext* TGL::Canvas::GetCurrentEditor() {
    return Current;
}

void TGL::Canvas::SetCurrentEditor(EditorContext* Editor) {
    Current = Editor;
    ax::NodeEditor::SetCurrentEditor(Editor != nullptr ? Editor->mNative : nullptr);
}

bool TGL::Canvas::Begin(const char* Id, ImVec2 Size, bool ReadOnly) {
    EditorContext& Context{*Current};
    Context.mReadOnly = ReadOnly;
    Context.mMovedNodes.clear();
    Context.mSubmittedNodes.clear();
    Context.mTransitions.clear();
    Context.mPinPositions.clear();
    Context.mLabels.clear();
    std::erase_if(Context.mFlowPulses, [Time = ImGui::GetTime()](const auto& Entry) { return Entry.second <= Time; });

    const ImVec2 Available{ImGui::GetContentRegionAvail()};
    const ImVec2 Extent{Size.x > 0.0f ? Size.x : std::max(Available.x + Size.x, 1.0f), Size.y > 0.0f ? Size.y : std::max(Available.y + Size.y, 1.0f)};
    Context.mCanvasMin = ImGui::GetCursorScreenPos();
    Context.mCanvasMax = {Context.mCanvasMin.x + Extent.x, Context.mCanvasMin.y + Extent.y};
    Context.mVisible = ImGui::IsRectVisible(Context.mCanvasMin, Context.mCanvasMax);

    if (!Context.mVisible) {
        return false;
    }

    ax::NodeEditor::Style& NativeStyle{ax::NodeEditor::GetStyle()};
    NativeStyle.NodePadding = Context.mStyle.mNodePadding;
    NativeStyle.NodeRounding = Context.mStyle.mNodeRounding;
    NativeStyle.NodeBorderWidth = Context.mStyle.mNodeBorderWidth;
    NativeStyle.LinkStrength = 96.0f;
    NativeStyle.Colors[ax::NodeEditor::StyleColor_Flow] = Context.mStyle.mActiveColor;
    NativeStyle.Colors[ax::NodeEditor::StyleColor_FlowMarker] = Context.mStyle.mActiveColor;
    NativeStyle.SnapLinkToPinDir = 1.0f;

    for (std::size_t Index{}; Index < std::size(StyleColors); ++Index) {
        NativeStyle.Colors[StyleColors[Index]] = Context.mStyle.mColors[Index];
    }

    const ImVec2 Mouse{ImGui::GetMousePos()};
    ax::NodeEditor::EnableShortcuts(!ReadOnly);
    ax::NodeEditor::Begin(Id, Size);
    Context.mPointerPosition = ax::NodeEditor::ScreenToCanvas(Mouse);

    return true;
}

void TGL::Canvas::End() {
    EditorContext& Context{*Current};

    if (!Context.mVisible) {
        return;
    }

    if (Context.mReadOnly) {
        if (ax::NodeEditor::BeginCreate()) {
            ax::NodeEditor::PinId From{};
            ax::NodeEditor::PinId To{};

            if (ax::NodeEditor::QueryNewLink(&From, &To) || ax::NodeEditor::QueryNewNode(&From)) {
                ax::NodeEditor::RejectNewItem();
            }
        }

        ax::NodeEditor::EndCreate();
    }

    if (Context.mFit && !Context.mSubmittedNodes.empty()) {
        ax::NodeEditor::NavigateToContent(0.0f);
        Context.mFit = false;
    }

    DrawLabels(Context);
    ax::NodeEditor::End();
    CaptureLayout(Context);
}

void TGL::Canvas::Suspend() {
    ax::NodeEditor::Suspend();
}

void TGL::Canvas::Resume() {
    ax::NodeEditor::Resume();
}

void TGL::Canvas::NavigateToContent() {
    Current->mFit = true;
}

ImVec2 TGL::Canvas::GetPointerPosition() {
    return Current->mPointerPosition;
}

float TGL::Canvas::GetZoom() {
    return ax::NodeEditor::GetCurrentZoom();
}

Style& TGL::Canvas::GetStyle() {
    return Current->mStyle;
}

ImVec4 TGL::Canvas::ConnectionColor(EConnectionRole Role) {
    switch (Role) {
        case EConnectionRole::Pose:
            return {0.76f, 0.85f, 0.98f, 1.0f};
        case EConnectionRole::Number:
            return {0.47f, 0.87f, 0.66f, 1.0f};
        case EConnectionRole::Boolean:
            return {0.96f, 0.55f, 0.57f, 1.0f};
        case EConnectionRole::Transition:
            return {0.64f, 0.73f, 1.0f, 1.0f};
        case EConnectionRole::Relationship:
            return {0.48f, 0.81f, 0.88f, 1.0f};
        case EConnectionRole::Default:
            return {0.66f, 0.75f, 0.88f, 1.0f};
    }

    return {};
}

void TGL::Canvas::PushStyleColor(EStyleColor Index, ImVec4 Color) {
    Current->mColors.emplace_back(Index, Current->mStyle.mColors[Index]);
    Current->mStyle.mColors[Index] = Color;
    ax::NodeEditor::PushStyleColor(StyleColors[Index], Color);
}

void TGL::Canvas::PopStyleColor() {
    const auto [Index, Color]{Current->mColors.back()};
    Current->mStyle.mColors[Index] = Color;
    Current->mColors.pop_back();
    ax::NodeEditor::PopStyleColor();
}

void TGL::Canvas::PushNodeBorderWidth(float Value) {
    Current->mBorders.push_back(Current->mStyle.mNodeBorderWidth);
    Current->mStyle.mNodeBorderWidth = Value;
    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_NodeBorderWidth, Value);
}

void TGL::Canvas::PopNodeBorderWidth() {
    Current->mStyle.mNodeBorderWidth = Current->mBorders.back();
    Current->mBorders.pop_back();
    ax::NodeEditor::PopStyleVar();
}

void TGL::Canvas::BeginNode(NodeId Id, bool Movable) {
    EditorContext& Context{*Current};
    const ax::NodeEditor::NodeId Native{Id.Get()};
    ax::NodeEditor::BeginNode(Native);
    ImGui::PushID(Native.AsPointer());
    auto [Position, Inserted]{Context.mPositions.try_emplace(Id.Get())};

    if (Inserted) {
        Position->second.mCommitted = ax::NodeEditor::GetNodePosition(Native);
    }

    Position->second.mMovable = Movable;
    Context.mNode = Id;
    Context.mNodePins.clear();
    Context.mHeaderBottom = 0.0f;
}

void TGL::Canvas::SetNodeHeader(float Bottom, ImVec4 Color) {
    Current->mHeaderBottom = Bottom;
    Current->mHeaderColor = Color;
}

void TGL::Canvas::EndNode() {
    EditorContext& Context{*Current};
    const ax::NodeEditor::NodeId Native{Context.mNode.Get()};
    DrawTransitionPins(Context);
    ImGui::PopID();
    ax::NodeEditor::EndNode();
    Context.mSubmittedNodes.push_back(Context.mNode);

    if (Context.mHeaderBottom == 0.0f) {
        return;
    }

    ImDrawList* Draw{ax::NodeEditor::GetNodeBackgroundDrawList(Native)};
    const ImVec2 Position{ax::NodeEditor::GetNodePosition(Native)};
    const ImVec2 Size{ax::NodeEditor::GetNodeSize(Native)};

    if (Draw == nullptr || Size.x <= 2.0f || Context.mHeaderBottom <= Position.y + 1.0f) {
        return;
    }

    const ImVec4 Accent{Context.mHeaderColor};
    const ImVec2 Bottom{Position.x + Size.x - 1.0f, std::min(Context.mHeaderBottom, Position.y + Size.y)};
    Draw->AddRectFilled({Position.x + 1.0f, Position.y + 1.0f}, Bottom, ImGui::GetColorU32(ImVec4{Accent.x, Accent.y, Accent.z, 0.20f}), std::max(0.0f, Context.mStyle.mNodeRounding - 1.0f), ImDrawFlags_RoundCornersTop);
    Draw->AddLine({Position.x + 1.0f, Bottom.y}, Bottom, ImGui::GetColorU32(ImVec4{Accent.x, Accent.y, Accent.z, 0.36f}));
    const float AccentX{Position.x + Context.mStyle.mNodePadding.x};

    if (Bottom.y > Position.y + 16.0f) {
        Draw->AddRectFilled({AccentX, Position.y + 8.0f}, {AccentX + 2.5f, Bottom.y - 7.0f}, ImGui::GetColorU32(Accent), 1.25f);
    }
}

void TGL::Canvas::SetNodePosition(NodeId Id, ImVec2 Position) {
    if (!std::isfinite(Position.x) || !std::isfinite(Position.y)) {
        return;
    }

    const ax::NodeEditor::NodeId Native{Id.Get()};
    ax::NodeEditor::SetNodePosition(Native, Position);
    Current->mPositions[Id.Get()].mCommitted = ax::NodeEditor::GetNodePosition(Native);
}

void TGL::Canvas::SyncNodePosition(NodeId Id, ImVec2 Position) {
    FNodePosition& State{Current->mPositions[Id.Get()]};

    if (!State.mModel || !SamePosition(*State.mModel, Position)) {
        SetNodePosition(Id, Position);
        State.mModel = Position;
    }
}

ImVec2 TGL::Canvas::GetNodePosition(NodeId Id) {
    const auto Found{Current->mPositions.find(Id.Get())};

    return Found == Current->mPositions.end() ? ImVec2{} : Found->second.mCommitted;
}

void TGL::Canvas::BeginPin(PinId Id, PinKind Kind) {
    Current->mPin = {Id, Kind};
    ax::NodeEditor::BeginPin(GetNativePin(*Current, Id), ToNative(Kind));
}

void TGL::Canvas::PinRect(ImVec2 Min, ImVec2 Max) {
    const ImVec2 Center{(Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f};
    Current->mPin.mCenter = Center;
    ax::NodeEditor::PinRect(Min, Max);
    ax::NodeEditor::PinPivotRect(Center, Center);
}

void TGL::Canvas::EndPin() {
    EditorContext& Context{*Current};
    ax::NodeEditor::EndPin();

    if (!Context.mPin.mCenter) {
        const ImVec2 Min{ImGui::GetItemRectMin()};
        const ImVec2 Max{ImGui::GetItemRectMax()};
        Context.mPin.mCenter = ImVec2{(Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f};
    }

    Context.mPinPositions.insert_or_assign(Context.mPin.mId.Get(), *Context.mPin.mCenter);
    Context.mNodePins.push_back(Context.mPin);
}

void TGL::Canvas::DeclareTransition(LinkId Id, PinId From, PinId To, bool Loop) {
    FTransitionRoute Route{};
    Route.mFrom = From;
    Route.mTo = To;
    Route.mLoop = Loop;
    Current->mTransitions.emplace(Id, Route);
}

void TGL::Canvas::Link(LinkId Id, PinId From, PinId To, ImVec4 Color, float Width, std::string_view Label) {
    EditorContext& Context{*Current};
    const auto Route{Context.mTransitions.find(Id)};
    const bool Routed{Route != Context.mTransitions.end() && Route->second.mNativeFrom && Route->second.mNativeTo};
    const ax::NodeEditor::PinId NativeFrom{Routed ? Route->second.mNativeFrom : GetNativePin(Context, From)};
    const ax::NodeEditor::PinId NativeTo{Routed ? Route->second.mNativeTo : GetNativePin(Context, To)};

    if (!ax::NodeEditor::Link(ax::NodeEditor::LinkId{Id.Get()}, NativeFrom, NativeTo, Color, Width) || Label.empty()) {
        return;
    }

    const ImVec2 Start{Routed ? Route->second.mFromPosition : Context.mPinPositions.at(From.Get())};
    const ImVec2 EndPosition{Routed ? Route->second.mToPosition : Context.mPinPositions.at(To.Get())};
    Context.mLabels.push_back({{(Start.x + EndPosition.x) * 0.5f, (Start.y + EndPosition.y) * 0.5f}, std::string{Label}, ImGui::GetColorU32(Color)});
}

void TGL::Canvas::Flow(LinkId Id) {
    const double Interval{std::max(0.05, static_cast<double>(ax::NodeEditor::GetStyle().FlowDuration) * 0.75)};
    const auto Pulse{Current->mFlowPulses.try_emplace(Id.Get(), ImGui::GetTime() + Interval)};

    if (Pulse.second) {
        ax::NodeEditor::Flow(ax::NodeEditor::LinkId{Id.Get()});
    }
}

LinkId TGL::Canvas::GetHoveredLink() {
    return {ax::NodeEditor::GetHoveredLink().Get()};
}

bool TGL::Canvas::BeginCreate(ImVec4 Color, float Width) {
    return ax::NodeEditor::BeginCreate(Color, Width);
}

bool TGL::Canvas::QueryNewLink(PinId* From, PinId* To) {
    ax::NodeEditor::PinId Start{};
    ax::NodeEditor::PinId EndPin{};

    if (!ax::NodeEditor::QueryNewLink(&Start, &EndPin)) {
        return false;
    }

    *From = GetLogicalPin(Start);
    *To = GetLogicalPin(EndPin);

    return true;
}

bool TGL::Canvas::QueryNewNode(PinId* From) {
    ax::NodeEditor::PinId Start{};

    if (!ax::NodeEditor::QueryNewNode(&Start)) {
        return false;
    }

    *From = GetLogicalPin(Start);

    return static_cast<bool>(*From);
}

bool TGL::Canvas::AcceptNewItem(ImVec4 Color, float Width) {
    return ax::NodeEditor::AcceptNewItem(Color, Width);
}

void TGL::Canvas::RejectNewItem(ImVec4 Color, float Width) {
    ax::NodeEditor::RejectNewItem(Color, Width);
}

void TGL::Canvas::EndCreate() {
    ax::NodeEditor::EndCreate();
}

bool TGL::Canvas::BeginDelete() {
    return ax::NodeEditor::BeginDelete();
}

bool TGL::Canvas::QueryDeletedNode(NodeId* Id) {
    ax::NodeEditor::NodeId Native{};

    if (!ax::NodeEditor::QueryDeletedNode(&Native)) {
        return false;
    }

    *Id = NodeId{Native.Get()};

    return true;
}

bool TGL::Canvas::QueryDeletedLink(LinkId* Id) {
    ax::NodeEditor::LinkId Native{};

    if (!ax::NodeEditor::QueryDeletedLink(&Native)) {
        return false;
    }

    *Id = LinkId{Native.Get()};

    return true;
}

bool TGL::Canvas::AcceptDeletedItem(bool DeleteDependencies) {
    return ax::NodeEditor::AcceptDeletedItem(DeleteDependencies);
}

void TGL::Canvas::RejectDeletedItem() {
    ax::NodeEditor::RejectDeletedItem();
}

void TGL::Canvas::EndDelete() {
    ax::NodeEditor::EndDelete();
}

bool TGL::Canvas::IsBackgroundClicked() {
    return ax::NodeEditor::IsBackgroundClicked() && ax::NodeEditor::GetBackgroundClickButtonIndex() == ImGuiMouseButton_Left;
}

bool TGL::Canvas::ShowBackgroundContextMenu() {
    return ax::NodeEditor::ShowBackgroundContextMenu();
}

bool TGL::Canvas::ShowNodeContextMenu(NodeId* Id) {
    ax::NodeEditor::NodeId Native{};

    if (!ax::NodeEditor::ShowNodeContextMenu(&Native)) {
        return false;
    }

    *Id = NodeId{Native.Get()};

    return true;
}

bool TGL::Canvas::ShowLinkContextMenu(LinkId* Id) {
    ax::NodeEditor::LinkId Native{};

    if (!ax::NodeEditor::ShowLinkContextMenu(&Native)) {
        return false;
    }

    *Id = LinkId{Native.Get()};

    return true;
}

NodeId TGL::Canvas::GetDoubleClickedNode() {
    return {ax::NodeEditor::GetDoubleClickedNode().Get()};
}

LinkId TGL::Canvas::GetDoubleClickedLink() {
    return {ax::NodeEditor::GetDoubleClickedLink().Get()};
}

void TGL::Canvas::ClearSelection() {
    ax::NodeEditor::ClearSelection();
}

void TGL::Canvas::SelectNode(NodeId Id, bool Append) {
    ax::NodeEditor::SelectNode(ax::NodeEditor::NodeId{Id.Get()}, Append);
}

void TGL::Canvas::SelectLink(LinkId Id, bool Append) {
    ax::NodeEditor::SelectLink(ax::NodeEditor::LinkId{Id.Get()}, Append);
}

int TGL::Canvas::GetSelectedNodes(NodeId* Items, int Capacity) {
    return CopySelection(Items, Capacity, ax::NodeEditor::GetSelectedNodes);
}

int TGL::Canvas::GetSelectedLinks(LinkId* Items, int Capacity) {
    return CopySelection(Items, Capacity, ax::NodeEditor::GetSelectedLinks);
}

std::span<const FSelectionItem> TGL::Canvas::GetSelection() {
    const int Count{ax::NodeEditor::GetSelectedObjectCount()};
    auto& Items{Current->mSelection};
    Items.clear();

    if (Count > 0) {
        std::vector<NodeId> Nodes{};
        std::vector<LinkId> Links{};
        Nodes.resize(static_cast<std::size_t>(Count));
        Links.resize(static_cast<std::size_t>(Count));
        const int NodeCount{GetSelectedNodes(Nodes.data(), Count)};
        const int LinkCount{GetSelectedLinks(Links.data(), Count)};

        for (int Index{}; Index < NodeCount; ++Index) {
            Items.emplace_back(Nodes[Index]);
        }

        for (int Index{}; Index < LinkCount; ++Index) {
            Items.emplace_back(Links[Index]);
        }
    }

    return Items;
}

std::span<const FMoveNode> TGL::Canvas::GetMovedNodes() {
    return Current->mMovedNodes;
}
