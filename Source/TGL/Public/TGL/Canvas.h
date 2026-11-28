/**
 * @file
 * @brief imgui-node-editor를 감싼 TGL 캔버스, 스타일 및 편집 제스처 API를 정의합니다.
 * @ingroup TGL
 */

#pragma once

#include "TGL/Graph.h"

#include <imgui.h>
#include <string_view>

namespace TGL::Canvas {

    using NodeId = TGL::FNodeId; ///< 캔버스의 노드 식별자입니다.
    using PinId = TGL::FPinId; ///< 캔버스 컨텍스트 안에서 유일한 논리 핀 식별자입니다.
    using LinkId = TGL::FLinkId; ///< 캔버스의 링크 식별자입니다.
    using PinKind = TGL::EPinDirection; ///< 캔버스 핀의 방향입니다.

    /**
     * @brief 캔버스 스타일에서 개별 색상을 지정하는 항목입니다.
     */
    enum EStyleColor {
        Background, ///< 캔버스 배경색입니다.
        Grid, ///< 격자선의 색입니다.
        NodeBackground, ///< 노드 본문 배경색입니다.
        NodeBorder, ///< 노드 기본 테두리 색입니다.
        HoveredNodeBorder, ///< 포인터가 올라간 노드의 테두리 색입니다.
        SelectedNodeBorder, ///< 선택한 노드의 테두리 색입니다.
        HoveredLink, ///< 포인터가 올라간 링크의 색입니다.
        SelectedLink, ///< 선택한 링크의 색입니다.
        ColorCount ///< 스타일에 등록된 색의 개수입니다.
    };

    /**
     * @brief 공용 스타일에서 핀과 링크의 용도에 맞는 색을 선택합니다.
     */
    enum class EConnectionRole {
        Default, ///< 일반 연결입니다.
        Pose, ///< 포즈 값 연결입니다.
        Number, ///< 수치 값 연결입니다.
        Boolean, ///< 논리 값 연결입니다.
        Transition, ///< 상태 사이의 전이입니다.
        Relationship ///< 물리 바디와 제약의 관계 등 항목 사이의 연관입니다.
    };

    inline constexpr float LinkWidth{2.0f}; ///< 공용 링크 굵기입니다.

    /**
     * @brief 하나의 캔버스가 사용하는 여백과 색상 설정입니다.
     */
    struct Style {
        ImVec4 mNodePadding{11.0f, 9.0f, 11.0f, 9.0f}; ///< 노드의 왼쪽·위·오른쪽·아래 여백입니다.
        float mNodeRounding{9.0f}; ///< 노드 모서리 반경입니다.
        float mNodeBorderWidth{1.0f}; ///< 노드 테두리 굵기입니다.
        ImVec4 mNodeAccent{0.43f, 0.63f, 0.96f, 1.0f}; ///< 노드 머리글의 기본 강조 색입니다.
        ImVec4 mActiveColor{0.96f, 0.77f, 0.40f, 1.0f}; ///< 활성 노드나 링크 흐름을 강조하는 색입니다.
        ImVec4 mErrorColor{0.98f, 0.42f, 0.49f, 1.0f}; ///< 편집 거부와 검증 오류를 표시하는 색입니다.
        /**
         * @brief EStyleColor로 지정하는 캔버스 색상입니다.
         */
        ImVec4 mColors[ColorCount]{
            {0.075f, 0.088f, 0.112f, 1.0f},
            {0.31f, 0.37f, 0.46f, 0.18f},
            {0.115f, 0.135f, 0.171f, 1.0f},
            {0.28f, 0.34f, 0.43f, 1.0f},
            {0.52f, 0.66f, 0.85f, 1.0f},
            {0.51f, 0.76f, 1.0f, 1.0f},
            {0.86f, 0.90f, 0.96f, 1.0f},
            {0.50f, 0.81f, 1.0f, 1.0f}
        };
    };

    /**
     * @brief 네이티브 노드 편집기 및 논리 핀·배치·선택 상태를 보관하는 캔버스 컨텍스트입니다.
     */
    struct EditorContext;

    /**
     * @brief 별도 설정 파일을 사용하지 않는 캔버스 컨텍스트를 만듭니다.
     * @return 호출자가 DestroyEditor로 해제할 컨텍스트입니다.
     */
    TGL::Canvas::EditorContext* CreateEditor();
    /**
     * @brief 네이티브 편집기와 캔버스 상태를 해제합니다.
     * @param Editor 해제할 컨텍스트입니다. nullptr도 허용합니다.
     */
    void DestroyEditor(TGL::Canvas::EditorContext* Editor);
    /**
     * @brief 현재 캔버스 함수가 사용하는 컨텍스트를 조회합니다.
     * @return 현재 컨텍스트의 비소유 주소입니다. 지정된 컨텍스트가 없으면 nullptr입니다.
     */
    TGL::Canvas::EditorContext* GetCurrentEditor();
    /**
     * @brief 이어지는 캔버스 함수에 사용할 컨텍스트를 지정합니다.
     * @param Editor 활성화할 컨텍스트입니다. nullptr이면 현재 지정을 해제합니다.
     */
    void SetCurrentEditor(TGL::Canvas::EditorContext* Editor);

    /**
     * @brief 현재 컨텍스트의 캔버스 입력과 그리기를 시작합니다.
     * @param Id 캔버스를 구분하는 ImGui 식별자입니다.
     * @param Size 캔버스 크기입니다. 기본값은 남은 영역을 사용합니다.
     * @param ReadOnly true이면 구조 편집 제스처와 이동 요청 발행을 비활성화합니다.
     * @return 캔버스 영역이 보여 네이티브 편집기를 시작했으면 true입니다.
     */
    bool Begin(const char* Id, ImVec2 Size = {}, bool ReadOnly = false);
    /**
     * @brief 링크 레이블을 그린 뒤 캔버스를 닫고 노드의 이동 결과를 수집합니다.
     */
    void End();
    /**
     * @brief 캔버스 좌표계와 입력 처리를 일시 중단해 일반 ImGui 팝업이나 툴팁을 그릴 수 있게 합니다.
     */
    void Suspend();
    /**
     * @brief Suspend로 중단한 캔버스 좌표계와 입력 처리를 다시 활성화합니다.
     */
    void Resume();
    /**
     * @brief 다음에 제출한 노드 전체가 화면에 들어오도록 뷰 이동을 예약합니다.
     */
    void NavigateToContent();
    /**
     * @brief 캔버스 시작 시 계산한 포인터 좌표를 조회합니다.
     * @return 그래프 캔버스 좌표계의 위치입니다.
     */
    ImVec2 GetPointerPosition();
    /**
     * @brief 현재 캔버스 배율을 조회합니다.
     * @return 캔버스의 확대 배율입니다.
     */
    float GetZoom();

    /**
     * @brief 현재 컨텍스트의 스타일에 접근합니다.
     * @return 컨텍스트가 소유한 스타일의 참조입니다.
     */
    TGL::Canvas::Style& GetStyle();
    /**
     * @brief 용도에 맞는 공용 연결 색을 반환합니다.
     * @param Role 핀이나 링크의 용도입니다.
     * @return 표시할 RGBA 색상입니다.
     */
    ImVec4 ConnectionColor(TGL::Canvas::EConnectionRole Role);
    /**
     * @brief 현재 캔버스 색 하나를 임시로 바꿉니다.
     * @param Index 바꿀 스타일 색상입니다.
     * @param Color 그리기에 사용할 임시 색입니다.
     */
    void PushStyleColor(TGL::Canvas::EStyleColor Index, ImVec4 Color);
    /**
     * @brief 마지막 PushStyleColor 이전의 색상을 복원합니다.
     */
    void PopStyleColor();
    /**
     * @brief 이어서 그릴 노드의 테두리 굵기를 임시로 바꿉니다.
     * @param Value 적용할 굵기입니다.
     */
    void PushNodeBorderWidth(float Value);
    /**
     * @brief 마지막 PushNodeBorderWidth 이전의 굵기를 복원합니다.
     */
    void PopNodeBorderWidth();

    /**
     * @brief 노드와 하위 핀을 제출하기 시작합니다.
     * @param Id 그릴 노드의 식별자입니다.
     * @param Movable true이면 노드 위치를 캔버스에서 이동할 수 있습니다.
     */
    void BeginNode(TGL::Canvas::NodeId Id, bool Movable = true);
    /**
     * @brief 현재 노드에 머리글 배경을 그릴 범위와 색을 지정합니다.
     * @param Bottom 머리글 하단의 Y 좌표입니다.
     * @param Color 머리글에 사용할 강조 색입니다.
     */
    void SetNodeHeader(float Bottom, ImVec4 Color);
    /**
     * @brief 현재 노드와 전이 경로용 핀 제출을 마치고 머리글 배경을 그립니다.
     */
    void EndNode();
    /**
     * @brief 노드에 새 위치를 적용하고 이동 감지 기준을 갱신합니다.
     * @param Id 배치할 노드입니다.
     * @param Position 유한한 캔버스 좌표입니다.
     */
    void SetNodePosition(TGL::Canvas::NodeId Id, ImVec2 Position);
    /**
     * @brief 처음 표시하거나 문서 위치가 바뀐 노드에만 좌표를 적용합니다.
     * @param Id 문서 좌표를 동기화할 노드입니다.
     * @param Position 문서가 제공하는 캔버스 좌표입니다.
     */
    void SyncNodePosition(TGL::Canvas::NodeId Id, ImVec2 Position);
    /**
     * @brief 캔버스에서 노드의 현재 위치를 조회합니다.
     * @param Id 조회할 노드입니다.
     * @return 노드의 캔버스 좌표입니다.
     */
    ImVec2 GetNodePosition(TGL::Canvas::NodeId Id);

    /**
     * @brief 현재 노드에 논리 핀의 제출을 시작합니다.
     * @param Id 컨텍스트 안에서 핀을 구분하는 식별자입니다.
     * @param Kind 입력 또는 출력 방향입니다.
     */
    void BeginPin(TGL::Canvas::PinId Id, TGL::Canvas::PinKind Kind);
    /**
     * @brief 현재 핀의 연결과 입력 판정에 사용되는 사각형을 지정합니다.
     * @param Min 사각형의 좌상단 좌표입니다.
     * @param Max 사각형의 우하단 좌표입니다.
     */
    void PinRect(ImVec2 Min, ImVec2 Max);
    /**
     * @brief 현재 핀 제출을 마치고 위치를 기록합니다.
     */
    void EndPin();

    /**
     * @brief 노드를 그리기 전에 전이 링크를 예약해 병렬 전이용 경로를 구성합니다.
     * @param Id 전이 링크의 식별자입니다.
     * @param From 출발 논리 핀입니다.
     * @param To 도착 논리 핀입니다.
     * @param Loop 같은 노드 안에서 시작하고 끝나는 전이면 true입니다.
     */
    void DeclareTransition(TGL::Canvas::LinkId Id, TGL::Canvas::PinId From, TGL::Canvas::PinId To, bool Loop = false);
    /**
     * @brief 제출한 핀 사이의 링크를 그리고 레이블을 예약합니다.
     * @param Id 링크의 식별자입니다.
     * @param From 출력 논리 핀입니다.
     * @param To 입력 논리 핀입니다.
     * @param Color 링크의 표시 색입니다.
     * @param Width 링크의 굵기입니다.
     * @param Label 링크 중간에 표시할 선택적 문자열입니다.
     */
    void Link(TGL::Canvas::LinkId Id, TGL::Canvas::PinId From, TGL::Canvas::PinId To, ImVec4 Color, float Width = TGL::Canvas::LinkWidth, std::string_view Label = {});
    /**
     * @brief 최근 펄스가 끝난 활성 링크에 흐름 애니메이션을 띄웁니다.
     * @param Id 흐름을 표시할 링크입니다.
     */
    void Flow(TGL::Canvas::LinkId Id);
    /**
     * @brief 포인터 아래에 있는 링크를 조회합니다.
     * @return 링크 식별자입니다. 링크가 없으면 값은 0입니다.
     */
    TGL::Canvas::LinkId GetHoveredLink();

    /**
     * @brief 새 링크 또는 노드 생성 제스처를 조회하기 시작합니다.
     * @param Color 제스처 미리보기의 색입니다.
     * @param Width 제스처 미리보기의 굵기입니다.
     * @return 생성 제스처를 처리할 수 있으면 true입니다.
     */
    bool BeginCreate(ImVec4 Color = {0.62f, 0.76f, 0.96f, 1.0f}, float Width = TGL::Canvas::LinkWidth);
    /**
     * @brief 포인터가 연결하려는 핀을 조회합니다.
     * @param[out] From 드래그를 시작한 논리 핀입니다.
     * @param[out] To 제스처가 가리키는 논리 핀입니다.
     * @return 링크 제스처가 진행 중이면 true입니다.
     */
    bool QueryNewLink(TGL::Canvas::PinId* From, TGL::Canvas::PinId* To);
    /**
     * @brief 연결 드래그의 빈 영역에서 노드 생성을 요청했는지 조회합니다.
     * @param[out] From 연결을 시작한 논리 핀입니다.
     * @return 유효한 논리 핀으로 노드 생성을 요청했으면 true입니다.
     */
    bool QueryNewNode(TGL::Canvas::PinId* From);
    /**
     * @brief 생성 제스처를 허용하고 확정 시점을 조회합니다.
     * @param Color 허용된 제스처의 표시 색입니다.
     * @param Width 허용된 제스처의 표시 굵기입니다.
     * @return 사용자가 제스처를 확정했으면 true입니다.
     */
    bool AcceptNewItem(ImVec4 Color, float Width = TGL::Canvas::LinkWidth);
    /**
     * @brief 현재 생성 제스처를 거부합니다.
     * @param Color 거부된 제스처의 표시 색입니다.
     * @param Width 거부된 제스처의 표시 굵기입니다.
     */
    void RejectNewItem(ImVec4 Color, float Width = TGL::Canvas::LinkWidth);

    /**
     * @brief 생성 제스처 조회를 마칩니다.
     */
    void EndCreate();

    /**
     * @brief 삭제 후보를 조회하기 시작합니다.
     * @return 삭제 후보를 조회할 수 있으면 true입니다.
     */
    bool BeginDelete();
    /**
     * @brief 다음 삭제 대상 노드를 조회합니다.
     * @param[out] Id 삭제 후보 노드의 식별자입니다.
     * @return 노드 후보를 찾았으면 true입니다.
     */
    bool QueryDeletedNode(TGL::Canvas::NodeId* Id);
    /**
     * @brief 다음 삭제 대상 링크를 조회합니다.
     * @param[out] Id 삭제 후보 링크의 식별자입니다.
     * @return 링크 후보를 찾았으면 true입니다.
     */
    bool QueryDeletedLink(TGL::Canvas::LinkId* Id);
    /**
     * @brief 현재 삭제 후보를 승인합니다.
     * @param DeleteDependencies 네이티브 편집기에서 종속 항목도 삭제 후보에 포함할지 나타냅니다.
     * @return 현재 삭제 후보가 승인되었으면 true입니다.
     */
    bool AcceptDeletedItem(bool DeleteDependencies = true);
    /**
     * @brief 현재 삭제 후보를 거부합니다.
     */
    void RejectDeletedItem();
    /**
     * @brief 삭제 후보 조회를 마칩니다.
     */
    void EndDelete();

    /**
     * @brief 캔버스 배경의 왼쪽 클릭을 조회합니다.
     * @return 배경을 왼쪽 버튼으로 눌렀으면 true입니다.
     */
    bool IsBackgroundClicked();
    /**
     * @brief 캔버스 배경에서 컨텍스트 메뉴 요청이 발생했는지 조회합니다.
     * @return 배경 메뉴를 열어야 하면 true입니다.
     */
    bool ShowBackgroundContextMenu();
    /**
     * @brief 노드의 컨텍스트 메뉴 요청을 조회합니다.
     * @param[out] Id 메뉴의 대상 노드입니다.
     * @return 요청을 감지했으면 true입니다.
     */
    bool ShowNodeContextMenu(TGL::Canvas::NodeId* Id);
    /**
     * @brief 링크의 컨텍스트 메뉴 요청을 조회합니다.
     * @param[out] Id 메뉴의 대상 링크입니다.
     * @return 요청을 감지했으면 true입니다.
     */
    bool ShowLinkContextMenu(TGL::Canvas::LinkId* Id);
    /**
     * @brief 더블클릭한 노드를 조회합니다.
     * @return 노드 식별자입니다. 해당 노드가 없으면 값은 0입니다.
     */
    TGL::Canvas::NodeId GetDoubleClickedNode();
    /**
     * @brief 더블클릭한 링크를 조회합니다.
     * @return 링크 식별자입니다. 해당 링크가 없으면 값은 0입니다.
     */
    TGL::Canvas::LinkId GetDoubleClickedLink();

    /**
     * @brief 현재 컨텍스트의 노드와 링크 선택을 모두 해제합니다.
     */
    void ClearSelection();
    /**
     * @brief 지정한 캔버스 노드를 선택합니다.
     * @param Id 선택할 노드입니다.
     * @param Append 기존 선택에 추가하면 true입니다.
     */
    void SelectNode(TGL::Canvas::NodeId Id, bool Append = false);
    /**
     * @brief 지정한 캔버스 링크를 선택합니다.
     * @param Id 선택할 링크입니다.
     * @param Append 기존 선택에 추가하면 true입니다.
     */
    void SelectLink(TGL::Canvas::LinkId Id, bool Append = false);
    /**
     * @brief 선택된 노드 식별자를 호출자의 배열에 복사합니다.
     * @param[out] Items 선택한 노드 식별자를 담을 배열입니다.
     * @param Capacity 배열이 담을 수 있는 항목의 최대 개수입니다.
     * @return 배열에 복사한 노드 수입니다.
     */
    int GetSelectedNodes(TGL::Canvas::NodeId* Items, int Capacity);
    /**
     * @brief 선택된 링크 식별자를 호출자의 배열에 복사합니다.
     * @param[out] Items 선택한 링크 식별자를 담을 배열입니다.
     * @param Capacity 배열이 담을 수 있는 항목의 최대 개수입니다.
     * @return 배열에 복사한 링크 수입니다.
     */
    int GetSelectedLinks(TGL::Canvas::LinkId* Items, int Capacity);
    /**
     * @brief 현재 캔버스의 노드와 링크 선택을 하나의 범위로 조회합니다.
     * @return 컨텍스트가 보유하는 선택 목록의 비소유 범위입니다.
     */
    std::span<const TGL::FSelectionItem> GetSelection();
    /**
     * @brief End에서 확정된 노드 이동을 조회합니다.
     * @return 다음 Begin까지 사용할 수 있는 이동 목록입니다.
     */
    std::span<const TGL::FMoveNode> GetMovedNodes();

}
