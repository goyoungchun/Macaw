/**
 * @file
 * @brief AnimGraph 에셋과 상태 머신을 TGL 문서, 스키마, 편집 대상으로 연결합니다.
 * @ingroup Editor
 */

#pragma once

#include "Core/Types.h"
#include "TGL/GraphEditor.h"

namespace Asset {
    class FAnimStateMachineData;
    class UAnimGraph;
}

namespace Editor {

    /**
     * @brief 포즈 노드와 Float Getter를 AnimGraph 에셋 사본에서 읽고 편집합니다.
     * @details 포즈 순환을 거부합니다. 타입이 맞는 입력에 새 출력을 연결하면 기존 연결을 교체합니다.
     * @ingroup Editor
     */
    class FAnimPoseGraphDocument final : public TGL::FGraphSnapshot, public TGL::FGraphSchema {
    public:
        FAnimPoseGraphDocument();

    public:
        /**
         * @brief 편집할 AnimGraph에 연결하고 Getter 노드가 없는 레거시 Float 바인딩을 초기 표시용으로 복원합니다.
         * @param Graph 어댑터를 사용하는 동안 유효해야 하는 편집 사본입니다.
         */
        void Bind(Asset::UAnimGraph& Graph);
        /**
         * @brief Bind에서 연결한 에셋의 노드·링크와 속성을 읽어 문서 뷰를 갱신합니다.
         */
        void Refresh() override;

        /**
         * @brief 변수 드래그나 생성 팔레트에서 사용하는 Getter 동작 키를 만듭니다.
         * @param Name Getter로 읽을 Float 변수 이름입니다.
         * @return FCreateNode 요청에 전달할 동작 키입니다.
         */
        static std::string GetterAction(const std::string& Name);
        /**
         * @brief TGL 노드 식별자를 포즈 그래프의 원본 식별자로 변환합니다.
         * @param Id 포즈 그래프 문서가 제공한 노드 식별자입니다.
         * @return 원본 포즈 노드 ID입니다. Output Pose는 OutputNodeId, Getter 또는 빈 ID는 -2를 반환합니다.
         */
        static Int32 GetPoseNodeId(TGL::FNodeId Id);

    private:
        const char* GetEditUnavailableReason() const override;
        std::vector<TGL::FNodeAction> CollectNodeActions(const TGL::FGraphView& View, const std::optional<TGL::FEndpoint>& From, const TGL::FPinView* FromPin) const override;
        TGL::FConnectionResult CheckConnectionRules(const TGL::FGraphView& View, const TGL::FConnect& Connection) const override;

        /**
         * @brief 요청한 포즈 노드 또는 Float Getter를 생성하고 필요한 경우 기존 끝점에 연결합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FCreateNode& Edit) override;
        /**
         * @brief 포즈 입력이나 Float 입력에 스키마가 허용한 연결을 적용합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FConnect& Edit) override;
        /**
         * @brief 스키마가 승인한 위치를 포즈 노드와 Getter에 반영합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FMoveNodes& Edit) override;
        /**
         * @brief 스키마가 승인한 노드, 링크 또는 변수 바인딩을 삭제합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FDeleteSelection& Edit) override;
        /**
         * @brief 스키마가 승인한 속성값을 포즈 노드에 반영합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FSetProperty& Edit) override;
        /**
         * @brief 노드의 포즈 입력과 Float 바인딩을 모두 끊습니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FDisconnectInputs& Edit) override;

    private:
        Asset::UAnimGraph* mGraph{}; ///< Bind로 연결한 에셋 편집 사본의 비소유 주소입니다.
    };

    /**
     * @brief 상태와 전이를 노드와 방향 링크로 표시하고 상태 머신 사본을 편집합니다.
     * @details 보호된 Entry 노드와 링크로 진입 상태를 나타냅니다. 일반 상태는 자기 전이와 같은 두 상태 사이의 복수 전이를 허용합니다.
     * @ingroup Editor
     */
    class FAnimStateMachineGraphDocument final : public TGL::FGraphSnapshot, public TGL::FGraphSchema {
    public:
        FAnimStateMachineGraphDocument();

    public:
        /**
         * @brief 편집할 상태 머신을 연결하고 상태·전이와 속성을 읽어 문서 뷰를 갱신합니다.
         * @param Machine 어댑터가 읽고 편집할 상태 머신입니다.
         * @param Owner 새 상태의 하위 포즈 그래프에 전달할 변수 정의를 소유한 루트 에셋입니다.
         */
        void Refresh(Asset::FAnimStateMachineData& Machine, const Asset::UAnimGraph& Owner);

        /**
         * @brief TGL 노드에서 원본 상태 식별자를 복원합니다.
         * @param Id 상태 머신 문서의 노드 식별자입니다.
         * @return 원본 상태 ID입니다. Entry 또는 유효하지 않은 ID면 -1입니다.
         */
        static Int32 GetStateId(TGL::FNodeId Id);
        /**
         * @brief TGL 링크에서 원본 전이 식별자를 복원합니다.
         * @param Id 상태 머신 문서의 링크 식별자입니다.
         * @return 원본 전이 ID입니다. Entry 링크 또는 유효하지 않은 ID면 -1입니다.
         */
        static Int32 GetTransitionId(TGL::FLinkId Id);

    private:
        const char* GetEditUnavailableReason() const override;
        std::vector<TGL::FNodeAction> CollectNodeActions(const TGL::FGraphView& View, const std::optional<TGL::FEndpoint>& From, const TGL::FPinView* FromPin) const override;
        TGL::FConnectionResult CheckConnectionRules(const TGL::FGraphView& View, const TGL::FConnect& Connection) const override;
        TGL::EInputLinkPolicy GetInputLinkPolicy(const TGL::FEndpoint& Input) const override;
        void Refresh() override;

        /**
         * @brief 새 상태와 하위 포즈 그래프를 만들고 필요한 경우 기존 상태와 전이로 연결합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FCreateNode& Edit) override;
        /**
         * @brief 출발점에 따라 진입 상태를 바꾸거나 일반 상태 전이를 추가합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FConnect& Edit) override;
        /**
         * @brief 스키마가 승인한 위치를 상태 노드에 반영합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FMoveNodes& Edit) override;
        /**
         * @brief 스키마가 승인한 상태와 전이를 삭제합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FDeleteSelection& Edit) override;
        /**
         * @brief 상태 이름, 진입·초기화 설정 또는 전이 설정을 변경합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FSetProperty& Edit) override;
        /**
         * @brief 지정한 상태로 들어오는 일반 전이를 제거합니다. Entry 링크는 유지합니다.
         * @param Edit 확인하고 적용할 문서 편집 요청입니다.
         * @return 요청의 적용 결과입니다.
         */
        TGL::FEditResult ApplyEdit(const TGL::FDisconnectInputs& Edit) override;

    private:
        Asset::FAnimStateMachineData* mMachine{}; ///< 편집할 상태 머신 사본의 비소유 주소입니다.
        const Asset::UAnimGraph* mOwner{}; ///< 새 상태에 전달할 변수 정의를 가진 루트 그래프의 비소유 주소입니다.
    };

}
