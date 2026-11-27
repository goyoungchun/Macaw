/**
 * @file
 * @brief 그래프 문서를 그리고 도메인 편집 요청을 전달하는 공용 편집기를 정의합니다.
 * @ingroup TGL
 */

#pragma once

#include "TGL/Canvas.h"

#include <array>
#include <functional>
#include <map>
#include <set>

namespace TGL {

    /**
     * @brief 스키마에서 제공한 노드 생성 동작을 검색해 고르는 팔레트입니다.
     */
    class FNodePalette {
    public:
        /**
         * @brief 검색어를 지우고 다음 그리기에서 검색 입력에 포커스를 둡니다.
         */
        void Reset();
        /**
         * @brief 검색어에 맞는 동작을 표시합니다.
         * @param Actions 선택지로 제공할 노드 생성 동작입니다.
         * @return 선택한 동작의 원본 인덱스입니다. 아직 고르지 않았으면 값이 없습니다.
         */
        std::optional<std::size_t> Draw(std::span<const TGL::FNodeAction> Actions);

    private:
        std::array<char, 128> mSearch{}; ///< 노드 이름과 분류를 검색하는 입력 버퍼입니다.
        bool mFocus{true}; ///< 다음 그리기에서 검색 입력에 포커스를 줄지 나타냅니다.
    };

    /**
     * @brief 문서 편집 없이 실행 상태를 캔버스에 표시할 활성 항목입니다.
     */
    struct FGraphOverlay {
        std::span<const TGL::FNodeId> mActiveNodes{}; ///< 테두리를 강조할 노드입니다.
        std::span<const TGL::FLinkId> mActiveLinks{}; ///< 흐름 애니메이션을 표시할 링크입니다.
    };

    /**
     * @brief 이번 프레임에 편집기가 처리한 사용자 입력과 편집 결과입니다.
     */
    struct FGraphEvents {
        std::optional<TGL::FSelectionItem> mActivated{}; ///< 더블클릭으로 활성화한 노드 또는 링크입니다.
        std::optional<TGL::FSelectionItem> mContextItem{}; ///< 컨텍스트 메뉴를 요청한 노드 또는 링크입니다.
        TGL::FPoint mPointerPosition{}; ///< 프레임 시작 시 포인터의 캔버스 좌표입니다.
        std::optional<TGL::FEditResult> mEdit{}; ///< 공용 편집 요청을 처리한 결과입니다.
        bool mSelectionChanged{}; ///< 프레임에서 읽은 선택이 편집기가 보관한 선택과 달랐는지 나타냅니다.
        bool mBackgroundClicked{}; ///< 캔버스 배경이 왼쪽 버튼으로 눌렸는지 나타냅니다.
        bool mVisible{}; ///< 캔버스가 보이고 그리기가 실행되었는지 나타냅니다.
    };

    /**
     * @brief TGL 문서와 스키마로 하나의 그래프 캔버스를 운영합니다.
     * @details 캔버스, 선택, 팔레트 상태를 소유합니다. 문서의 원본 데이터를 바꾸는 요청은 편집 대상에 전달합니다.
     * @ingroup TGL
     */
    class FGraphEditor {
    public:
        using FPropertyEditor = std::function<bool(const TGL::FPropertyView&, TGL::FPropertyValue&)>; ///< 속성과 값의 복사본으로 전용 입력 위젯을 그리고 값이 바뀌면 true를 반환하는 콜백입니다.

    private:
        using FPinKey = std::pair<TGL::FEndpoint, TGL::EPinDirection>; ///< 같은 노드가 전이 입력과 출력을 모두 가질 때 실제 캔버스 핀을 구분하는 키입니다.

    public:
        /**
         * @brief 독립적인 노드 에디터 컨텍스트를 생성합니다.
         */
        FGraphEditor();
        /**
         * @brief 편집기가 소유한 캔버스 컨텍스트를 해제합니다.
         */
        ~FGraphEditor();
        /**
         * @brief 캔버스의 단독 소유권을 위해 복사 생성을 막습니다.
         */
        FGraphEditor(const TGL::FGraphEditor&) = delete;
        /**
         * @brief 캔버스의 단독 소유권을 위해 복사 할당을 막습니다.
         * @return 복사 할당의 대상입니다. 이 연산은 호출할 수 없습니다.
         */
        TGL::FGraphEditor& operator=(const TGL::FGraphEditor&) = delete;

    public:
        /**
         * @brief 현재 문서 뷰를 그리고 캔버스 입력을 처리합니다.
         * @details 문서 객체가 바뀌면 캔버스와 선택 상태를 초기화합니다. 확정한 편집 요청은 편집 대상에 바로 전달합니다.
         * @param Id 캔버스의 ImGui 식별자입니다.
         * @param Document 현재 노드와 링크를 제공하는 문서입니다.
         * @param Schema 생성 동작, 연결 판정 및 진단을 제공하는 스키마입니다.
         * @param Target 변경을 적용하는 편집 대상입니다. nullptr이면 읽기 전용으로 표시합니다.
         * @param Size 캔버스 크기입니다. 기본값은 남은 영역을 사용합니다.
         * @param Overlay 활성 노드와 링크 표시입니다.
         * @param EditTopology false이면 인라인 속성 입력을 남겨 두고 구조와 문서 위치 편집을 비활성화합니다.
         * @return 이번 프레임에 발생한 선택, 활성화, 편집 이벤트입니다.
         */
        TGL::FGraphEvents Draw(const char* Id, const TGL::IGraphDocument& Document, const TGL::IGraphSchema& Schema, TGL::IGraphEditTarget* Target = nullptr, ImVec2 Size = {}, const TGL::FGraphOverlay& Overlay = {}, bool EditTopology = true);
        /**
         * @brief 하나의 선택 항목에 대한 속성과 진단을 표시합니다.
         * @param Document 현재 캔버스가 표시하는 문서입니다.
         * @param Target 속성 변경을 적용하는 대상입니다. nullptr이면 속성을 읽기 전용으로 표시합니다.
         * @param CustomEditor 전용 편집 키가 있는 상세 속성을 그리는 선택적 콜백입니다.
         * @return 속성 편집의 적용 결과입니다. 바뀐 값이 없으면 NoChange입니다.
         */
        TGL::FEditResult DrawDetails(const TGL::IGraphDocument& Document, TGL::IGraphEditTarget* Target = nullptr, const FPropertyEditor& CustomEditor = {});

        /**
         * @brief 캔버스나 호스트가 만든 편집 요청을 제출합니다.
         * @details 요청으로 노드가 생성되면 새 노드를 선택합니다.
         * @param Target 요청을 검증하고 적용할 대상입니다.
         * @param Edit 원본에 적용할 편집 요청입니다.
         * @return 편집 대상이 반환한 결과입니다.
         */
        TGL::FEditResult SubmitEdit(TGL::IGraphEditTarget& Target, const TGL::FEdit& Edit);

        /**
         * @brief 다음에 노드가 표시되는 프레임에서 전체 그래프가 보이도록 화면을 맞춥니다.
         */
        void Fit();
        /**
         * @brief 다음 그리기에 문서의 좌표를 다시 적용하고 전체 그래프가 보이도록 맞춥니다.
         */
        void ResetLayout();
        /**
         * @brief 편집기가 보관한 현재 선택을 조회합니다.
         * @return 다음 선택 변경까지 유효한 비소유 범위입니다.
         */
        std::span<const TGL::FSelectionItem> GetSelection() const;
        /**
         * @brief 호스트의 선택을 보관하고 다음 그리기에서 캔버스에 반영합니다.
         * @param Selection 선택할 노드 및 링크입니다. 빈 범위를 주면 선택을 지웁니다.
         */
        void SetSelection(std::span<const TGL::FSelectionItem> Selection);

    private:
        /**
         * @brief 문서 끝점과 방향에 대응하는 캔버스 핀을 조회하거나 발급합니다.
         * @param Endpoint 문서가 지정한 노드 또는 핀 끝점입니다.
         * @param Direction 캔버스 핀의 입력 또는 출력 방향입니다.
         * @return 편집기 컨텍스트 안의 핀 식별자입니다.
         */
        TGL::FPinId GetPin(const TGL::FEndpoint& Endpoint, TGL::EPinDirection Direction);
        /**
         * @brief 노드의 머리글, 핀, 속성 및 진단을 그립니다.
         * @param Connected 현재 문서에서 연결에 사용된 끝점입니다.
         * @param Node 그릴 노드의 표시 정보입니다.
         * @param Overlay 활성 상태를 강조하는 정보입니다.
         * @param[out] InlineEdit 인라인 속성 입력으로 만든 요청입니다. nullptr이면 표시만 합니다.
         */
        void DrawNode(const std::set<FPinKey>& Connected, const TGL::FNodeView& Node, const TGL::FGraphOverlay& Overlay, std::optional<TGL::FEdit>* InlineEdit);
        /**
         * @brief 노드를 생성할 위치와 시작 끝점을 저장하고 팔레트를 엽니다.
         * @param Position 노드를 놓을 캔버스 좌표입니다.
         * @param Source 연결을 끌기 시작한 핀입니다. 빈 캔버스에서 열었으면 값이 없습니다.
         */
        void OpenPalette(ImVec2 Position, std::optional<FPinKey> Source);
        /**
         * @brief 노드 생성, 연결, 삭제 제스처를 편집 요청으로 바꿉니다.
         * @param View 입력 시점의 문서 뷰입니다.
         * @param Schema 생성 동작 및 연결 판정을 제공하는 스키마입니다.
         * @return 확정한 편집 요청입니다. 진행 중인 제스처만 있으면 값이 없습니다.
         */
        std::optional<TGL::FEdit> DrawEditControls(const TGL::FGraphView& View, const TGL::IGraphSchema& Schema);

    private:
        TGL::Canvas::EditorContext* mContext{}; ///< 편집기가 소유하는 캔버스 컨텍스트입니다.
        const TGL::IGraphDocument* mDocument{}; ///< 마지막으로 표시한 문서의 비소유 식별 주소입니다.
        std::map<FPinKey, TGL::FPinId> mPins{}; ///< 문서 끝점과 방향을 캔버스 핀으로 변환합니다.
        std::unordered_map<std::uintptr_t, FPinKey> mEndpoints{}; ///< 캔버스에서 들어온 핀을 문서 끝점과 방향으로 복원합니다.
        std::uintptr_t mNextPin{1}; ///< 다음에 발급할 캔버스 핀 식별자입니다.
        std::vector<TGL::FSelectionItem> mSelection{}; ///< 호스트가 조회하는 현재 선택입니다.
        std::optional<std::vector<TGL::FSelectionItem>> mPendingSelection{}; ///< 노드와 링크를 제출한 뒤 캔버스에 적용할 선택입니다.
        std::vector<TGL::FDiagnostic> mDiagnostics{}; ///< 가장 최근에 표시한 문서 뷰의 진단입니다.
        TGL::FNodePalette mPalette{}; ///< 스키마가 제공한 노드 생성 동작을 표시하는 팔레트입니다.
        std::optional<FPinKey> mPaletteSource{}; ///< 팔레트를 열 때 연결을 끌기 시작한 끝점입니다.
        TGL::FPoint mPalettePosition{}; ///< 팔레트에서 추가한 노드를 놓을 캔버스 위치입니다.
        bool mResetLayout{}; ///< 다음에 문서 좌표를 강제로 다시 적용할지 나타냅니다.
    };

}
