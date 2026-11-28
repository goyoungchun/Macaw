/**
 * @file
 * @brief 상태 전이 조건과 TGL 공용 편집기를 연결합니다.
 * @ingroup Editor
 */

#pragma once

#include "TGL/GraphEditor.h"

namespace Asset {
    class FAnimTransitionGraph;
    class UAnimGraph;
}

namespace Editor {

    /**
     * @brief 상태 머신 전이의 조건 노드와 Bool 결과를 읽고 편집합니다.
     * @details 입력과 출력의 값 타입을 비교하고 순환을 거부합니다. 편집 요청은 임시 사본에 먼저 적용한 뒤 검증에 성공한 결과를 원본 전이 조건에 반영합니다.
     * @ingroup Editor
     */
    class FAnimTransitionGraphEditor final : public TGL::FGraphSnapshot, public TGL::FGraphSchema {
    public:
        FAnimTransitionGraphEditor();

    public:
        /**
         * @brief 전이 조건에서 새 문서 뷰를 만들고 공용 캔버스로 표시합니다.
         * @param[in,out] Graph 현재 편집할 전이 조건입니다.
         * @param Owner Getter에서 조회할 변수 정의를 소유한 루트 AnimGraph입니다.
         * @param Size 사용할 캔버스 크기입니다.
         * @return 이번 프레임의 그래프 입력과 편집 결과입니다.
         */
        TGL::FGraphEvents Draw(Asset::FAnimTransitionGraph& Graph, const Asset::UAnimGraph& Owner, ImVec2 Size);
        /**
         * @brief 현재 선택의 전용 안내와 공용 속성 편집 위젯을 표시합니다.
         * @return 속성 편집 결과입니다. 변경이 없으면 NoChange입니다.
         */
        TGL::FEditResult DrawDetails();
        /**
         * @brief 전이 조건 그래프에서 현재 선택한 항목을 조회합니다.
         * @return 선택이 바뀌기 전까지 사용할 수 있는 비소유 범위입니다.
         */
        std::span<const TGL::FSelectionItem> GetSelection() const;

        /**
         * @brief 공용 노드 생성 요청으로 변수에 맞는 Bool 또는 Float Getter를 추가합니다.
         * @param Name 루트 AnimGraph의 변수 이름입니다.
         * @param Position Getter를 놓을 캔버스 좌표입니다.
         * @return 생성 요청의 결과입니다.
         */
        TGL::FEditResult AddVariable(const std::string& Name, TGL::FPoint Position);
        /**
         * @brief 공용 삭제 요청으로 선택한 항목을 지웁니다.
         * @param Item 삭제할 노드 또는 링크입니다.
         * @return 삭제 요청의 결과입니다.
         */
        TGL::FEditResult Delete(TGL::FSelectionItem Item);

    private:
        const char* GetEditUnavailableReason() const override;
        std::vector<TGL::FNodeAction> CollectNodeActions(const TGL::FGraphView& View, const std::optional<TGL::FEndpoint>& From, const TGL::FPinView* FromPin) const override;
        TGL::FConnectionResult CheckConnectionRules(const TGL::FGraphView& View, const TGL::FConnect& Connection) const override;
        TGL::EInputLinkPolicy GetInputLinkPolicy(const TGL::FEndpoint& Input) const override;
        TGL::FEditResult ApplyValidatedEdit(const TGL::FEdit& Edit) override;

        /**
         * @brief 전이 조건과 루트 변수 정의에서 현재 노드·링크 뷰를 만듭니다.
         */
        void Refresh() override;
        /**
         * @brief 후보 전이 조건을 검증하고 원본을 갱신합니다.
         * @param Candidate 편집 요청을 적용한 후보 전이 조건입니다.
         * @return 검증에 성공해 후보를 반영했으면 true입니다.
         */
        bool Commit(Asset::FAnimTransitionGraph Candidate);

        /**
         * @brief 후보 전이 조건에 노드를 만들고 필요한 경우 시작 핀과 연결합니다.
         * @param[in,out] Graph 편집할 후보 전이 조건입니다.
         * @param Edit 생성 동작과 위치를 포함한 요청입니다.
         * @return 새 TGL 노드 식별자입니다. 생성이나 연결에 실패하면 값은 0입니다.
         */
        TGL::FNodeId Create(Asset::FAnimTransitionGraph& Graph, const TGL::FCreateNode& Edit) const;
        /**
         * @brief 스키마가 허용한 연결을 후보 전이 조건에 적용합니다.
         * @param[in,out] Graph 변경할 후보 전이 조건입니다.
         * @param Edit 후보에 적용할 요청입니다.
         * @return 후보 데이터가 바뀌었으면 true입니다.
         */
        bool ApplyTo(Asset::FAnimTransitionGraph& Graph, const TGL::FConnect& Edit) const;
        /**
         * @brief 후보에서 선택한 노드나 링크를 지웁니다.
         * @param[in,out] Graph 변경할 후보 전이 조건입니다.
         * @param Edit 후보에 적용할 요청입니다.
         * @return 후보 데이터가 바뀌었으면 true입니다.
         */
        bool ApplyTo(Asset::FAnimTransitionGraph& Graph, const TGL::FDeleteSelection& Edit) const;
        /**
         * @brief 후보에 유효한 노드 위치 변경을 적용합니다.
         * @param[in,out] Graph 변경할 후보 전이 조건입니다.
         * @param Edit 후보에 적용할 요청입니다.
         * @return 후보 데이터가 바뀌었으면 true입니다.
         */
        bool ApplyTo(Asset::FAnimTransitionGraph& Graph, const TGL::FMoveNodes& Edit) const;
        /**
         * @brief 상수, 변수 이름 또는 기준 플레이어 이름을 후보에 적용합니다.
         * @param[in,out] Graph 변경할 후보 전이 조건입니다.
         * @param Edit 후보에 적용할 요청입니다.
         * @return 후보 데이터가 바뀌었으면 true입니다.
         */
        bool ApplyTo(Asset::FAnimTransitionGraph& Graph, const TGL::FSetProperty& Edit) const;
        /**
         * @brief 지정한 노드 또는 결과 핀으로 들어오는 연결을 후보에서 지웁니다.
         * @param[in,out] Graph 변경할 후보 전이 조건입니다.
         * @param Edit 후보에 적용할 요청입니다.
         * @return 후보 데이터가 바뀌었으면 true입니다.
         */
        bool ApplyTo(Asset::FAnimTransitionGraph& Graph, const TGL::FDisconnectInputs& Edit) const;

    private:
        Asset::FAnimTransitionGraph* mGraph{}; ///< 현재 편집하는 전이 조건의 비소유 주소입니다.
        const Asset::UAnimGraph* mOwner{}; ///< 변수 정의가 있는 루트 그래프의 비소유 주소입니다.
        TGL::FGraphEditor mEditor{}; ///< 조건 그래프의 캔버스와 선택을 소유하는 공용 편집기입니다.
    };

}
