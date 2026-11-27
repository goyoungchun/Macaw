/**
 * @file
 * @brief 그래프 문서, 연결 규칙, 편집 요청의 공용 계약을 정의합니다.
 * @ingroup TGL
 */

#pragma once

#include <algorithm>
#include <compare>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace TGL {

    /**
     * @brief 노드·핀·링크 식별자를 타입으로 구분합니다.
     * @tparam Tag 식별자 종류를 구분하는 태그 타입입니다.
     */
    template <class Tag>
    struct TId {
        /**
         * @brief 예약값 0을 제외한 식별자인지 검사합니다.
         * @return 값이 0이 아니면 true입니다.
         */
        explicit operator bool() const;
        /**
         * @brief 식별자의 정수 표현을 반환합니다.
         * @return 저장된 식별자 값입니다.
         */
        std::uintptr_t Get() const;
        /**
         * @brief 같은 종류의 식별자를 값으로 비교합니다.
         * @param Other 비교할 식별자입니다.
         * @return 두 값의 순서입니다.
         */
        auto operator<=>(const TId& Other) const = default;

        std::uintptr_t mValue{}; ///< 0은 지정되지 않은 식별자입니다.
    };

    using FNodeId = TGL::TId<struct FNodeTag>; ///< 문서 내 노드 식별자입니다.
    using FPinId = TGL::TId<struct FPinTag>; ///< 노드 내 핀 식별자입니다.
    using FLinkId = TGL::TId<struct FLinkTag>; ///< 문서 내 링크 식별자입니다.

    /**
     * @brief 캔버스 좌표계의 2차원 위치입니다.
     */
    struct FPoint {
        /**
         * @brief 좌표의 X, Y 성분을 순서대로 비교합니다.
         * @param Other 비교할 위치입니다.
         * @return 좌표의 비교 결과입니다.
         */
        auto operator<=>(const FPoint& Other) const = default;

        float mX{}; ///< 캔버스 X 좌표입니다.
        float mY{}; ///< 캔버스 Y 좌표입니다.
    };

    /**
     * @brief 핀의 연결 방향입니다.
     */
    enum class EPinDirection {
        Input, ///< 연결을 받는 입력입니다.
        Output ///< 연결을 제공하는 출력입니다.
    };

    /**
     * @brief 노드 식별자와 노드 내 핀 식별자를 묶어 핀을 지정합니다.
     */
    struct FPinRef {
        /**
         * @brief 소유 노드와 핀의 식별자를 순서대로 비교합니다.
         * @param Other 비교할 핀 참조입니다.
         * @return 두 참조의 순서입니다.
         */
        auto operator<=>(const FPinRef& Other) const = default;

        TGL::FNodeId mNode{}; ///< 핀을 소유하는 노드입니다.
        TGL::FPinId mPin{}; ///< 소유 노드 안에서 핀을 구분하는 값입니다.
    };

    using FEndpoint = std::variant<TGL::FNodeId, TGL::FPinRef>; ///< 상태 전이처럼 노드 전체를 쓰거나 개별 핀을 쓰는 링크의 끝점입니다.
    using FSelectionItem = std::variant<TGL::FNodeId, TGL::FLinkId>; ///< 선택과 편집의 대상이 되는 노드 또는 링크입니다.
    using FPropertyValue = std::variant<bool, std::int64_t, double, std::string>; ///< 공용 속성 위젯과 편집 요청이 사용하는 값 타입입니다.

    /**
     * @brief 실수 속성에 표시할 슬라이더 또는 드래그 입력의 설정입니다.
     */
    struct FNumericEditor {
        double mMin{}; ///< 범위의 하한입니다. 드래그 입력은 하한이 상한보다 작을 때 범위를 적용합니다.
        double mMax{}; ///< 범위의 상한입니다.
        float mStep{}; ///< 드래그 입력에서 포인터 이동에 대한 변경량입니다.
        bool mSlider{}; ///< true이면 슬라이더를 표시합니다.
    };

    /**
     * @brief 노드와 링크의 속성값 및 공용 편집 위젯에 필요한 정보를 제공합니다.
     */
    struct FPropertyView {
        std::string mKey{}; ///< 선택 항목 안에서 속성을 식별하고 편집 요청에 사용하는 키입니다.
        std::string mName{}; ///< 속성의 표시 이름입니다.
        TGL::FPropertyValue mValue{}; ///< 속성의 현재 값입니다.
        std::vector<std::string> mOptions{}; ///< 문자열 속성에 표시할 선택지입니다.
        bool mReadOnly{}; ///< true이면 현재 값을 표시하고 편집 입력을 비활성화합니다.
        bool mShowInNode{true}; ///< true이면 노드 본문에 속성을 표시합니다.
        bool mInlineEditable{}; ///< 편집 대상이 있고 속성이 수정 가능하면 노드 본문에 입력 위젯을 표시합니다.
        std::string mCustomEditor{}; ///< 상세 패널에서 도메인 전용 위젯을 고르는 키입니다.
        std::optional<TGL::FNumericEditor> mNumeric{}; ///< 실수 속성의 슬라이더 또는 드래그 설정입니다.
        std::optional<std::string> mDisplayValue{}; ///< 노드 본문에 표시할 값의 문자열입니다. 편집할 때는 원래 값을 사용합니다.
    };

    /**
     * @brief 핀의 식별자, 연결 타입 및 캔버스 표시 정보를 설명합니다.
     */
    struct FPinView {
        TGL::FPinId mId{}; ///< 소유 노드 안에서 유일한 핀 식별자입니다.
        TGL::EPinDirection mDirection{}; ///< 연결의 입력 또는 출력 방향입니다.
        std::string mName{}; ///< 핀의 표시 이름입니다.
        std::string mType{}; ///< 연결 규칙에서 비교할 도메인 타입 이름입니다.
        std::optional<TGL::FPropertyValue> mDefaultValue{}; ///< 연결이 없는 입력 핀에 표시할 값입니다.
        std::uint32_t mColor{}; ///< ImGui 색상이며 0이면 공용 핀 색을 사용합니다.
    };

    /**
     * @brief 캔버스에 제출할 노드와 해당 노드의 편집 가능 범위를 설명합니다.
     */
    struct FNodeView {
        TGL::FNodeId mId{}; ///< 문서 내에서 유일한 노드 식별자입니다.
        TGL::FPoint mPosition{}; ///< 문서가 제공하는 캔버스 위치입니다.
        std::string mTitle{}; ///< 노드 머리글의 제목입니다.
        std::string mSummary{}; ///< 노드 본문에 표시하는 보조 설명입니다.
        std::string mType{}; ///< 노드 머리글의 종류 표시입니다.
        std::vector<TGL::FPinView> mPins{}; ///< 노드에 속한 입출력 핀입니다.
        std::vector<TGL::FPropertyView> mProperties{}; ///< 노드의 표시 및 편집 속성입니다.
        std::uint32_t mColor{}; ///< ImGui 머리글 강조 색상이며 0이면 공용 색을 사용합니다.
        bool mMovable{true}; ///< 캔버스에서 노드를 이동할 수 있는지 나타냅니다.
        bool mDeletable{true}; ///< 사용자가 노드를 삭제할 수 있는지 나타냅니다.
        bool mTransitionInput{}; ///< 노드 자체를 끝점으로 쓰는 전이 입력을 표시합니다.
        bool mTransitionOutput{}; ///< 노드 자체를 끝점으로 쓰는 전이 출력을 표시합니다.
        std::optional<float> mProgress{}; ///< 노드 본문에 표시할 0부터 1까지의 진행률입니다.
        std::uint32_t mBorderColor{}; ///< ImGui 테두리 색상이며 0이면 기본 스타일이나 활성 강조를 사용합니다.
    };

    /**
     * @brief 출발점에서 도착점으로 향하는 링크 및 표시 속성입니다.
     */
    struct FLinkView {
        TGL::FLinkId mId{}; ///< 문서 내에서 유일한 링크 식별자입니다.
        TGL::FEndpoint mFrom{}; ///< 출발 노드 또는 출력 핀입니다.
        TGL::FEndpoint mTo{}; ///< 도착 노드 또는 입력 핀입니다.
        std::string mLabel{}; ///< 링크 위에 표시할 레이블입니다.
        std::vector<TGL::FPropertyView> mProperties{}; ///< 링크를 선택했을 때 표시할 속성입니다.
        std::uint32_t mColor{}; ///< ImGui 링크 색상이며 0이면 공용 색을 사용합니다.
        bool mArrow{}; ///< 화살표를 가진 전이 경로로 표시할지 여부입니다.
        bool mDeletable{true}; ///< 사용자가 링크를 삭제할 수 있는지 나타냅니다.
        float mWidth{}; ///< 링크 너비입니다. 양수가 아니면 공용 너비를 사용합니다.
        std::string mTooltip{}; ///< 포인터를 올렸을 때 표시할 설명입니다.
    };

    /**
     * @brief 현재 문서의 노드와 링크를 참조하는 비소유 읽기 뷰입니다.
     * @details 원본 문서가 해당 데이터를 교체하거나 파괴하기 전까지 노드와 링크를 조회할 수 있습니다.
     */
    struct FGraphView {
        std::span<const TGL::FNodeView> mNodes{}; ///< 현재 문서의 노드 범위입니다.
        std::span<const TGL::FLinkView> mLinks{}; ///< 현재 문서의 링크 범위입니다.
    };

    /**
     * @brief 현재 스키마가 노드 추가 메뉴에 제공하는 동작입니다.
     */
    struct FNodeAction {
        std::string mKey{}; ///< FCreateNode 요청에 전달되는 동작 키입니다.
        std::string mName{}; ///< 메뉴에 표시하는 동작 이름입니다.
        std::string mCategory{}; ///< 메뉴에서 이름 앞에 표시하는 분류입니다.
    };

    /**
     * @brief 선택한 동작으로 캔버스 위치에 노드를 생성하는 요청입니다.
     */
    struct FCreateNode {
        std::string mAction{}; ///< 스키마가 노드 추가 메뉴에 제공한 동작 키입니다.
        TGL::FPoint mPosition{}; ///< 노드의 초기 캔버스 위치입니다.
        std::optional<TGL::FEndpoint> mConnectFrom{}; ///< 연결을 끌어 생성할 때 대상이 되는 기존 끝점입니다.
        TGL::EPinDirection mConnectDirection{TGL::EPinDirection::Output}; ///< 기존 끝점이 입력인지 출력인지 나타냅니다.
    };

    /**
     * @brief 출력에서 입력으로 방향을 정규화한 연결 요청입니다.
     */
    struct FConnect {
        TGL::FEndpoint mFrom{}; ///< 연결의 출력 끝점입니다.
        TGL::FEndpoint mTo{}; ///< 연결의 입력 끝점입니다.
    };

    /**
     * @brief 노드 하나의 새 캔버스 위치입니다.
     */
    struct FMoveNode {
        TGL::FNodeId mNode{}; ///< 이동할 노드입니다.
        TGL::FPoint mPosition{}; ///< 이동한 뒤의 캔버스 위치입니다.
    };

    /**
     * @brief 드래그로 이동한 노드 위치를 묶어 제출하는 요청입니다.
     */
    struct FMoveNodes {
        std::vector<TGL::FMoveNode> mMoves{}; ///< 같은 편집에서 반영할 위치 변경입니다.
    };

    /**
     * @brief 선택한 노드와 링크를 삭제하는 요청입니다.
     */
    struct FDeleteSelection {
        std::vector<TGL::FSelectionItem> mItems{}; ///< 삭제할 노드와 링크입니다.
    };

    /**
     * @brief 선택한 노드나 링크의 속성 하나를 수정하는 요청입니다.
     */
    struct FSetProperty {
        TGL::FSelectionItem mItem{}; ///< 속성을 소유한 노드 또는 링크입니다.
        std::string mKey{}; ///< 선택 항목의 속성 키입니다.
        TGL::FPropertyValue mValue{}; ///< 적용을 요청한 새 값입니다.
    };

    /**
     * @brief 지정한 노드의 입력 연결을 끊는 요청입니다.
     */
    struct FDisconnectInputs {
        TGL::FNodeId mNode{}; ///< 입력 연결을 끊을 노드입니다.
    };

    using FEdit = std::variant<TGL::FCreateNode, TGL::FConnect, TGL::FMoveNodes, TGL::FDeleteSelection, TGL::FSetProperty, TGL::FDisconnectInputs>; ///< 공용 편집기가 대상 문서로 전달하는 편집 요청입니다.

    /**
     * @brief 편집 요청을 처리한 결과입니다.
     */
    enum class EEditStatus {
        NoChange, ///< 적용할 변경이 없습니다.
        Applied, ///< 원본에 변경을 적용했습니다.
        Rejected ///< 문서의 규칙 또는 현재 상태에 따라 요청을 거부했습니다.
    };

    /**
     * @brief 스키마가 연결 요청에 내린 판정입니다.
     */
    enum class EConnection {
        Allow, ///< 새 연결을 허용합니다.
        Replace, ///< 기존 연결을 교체할 수 있습니다.
        Reject ///< 연결을 거부합니다.
    };

    /**
     * @brief 하나의 입력에 연결을 추가할 때 적용하는 정책입니다.
     */
    enum class EInputLinkPolicy {
        Single, ///< 다른 출력에 연결하면 교체하고 동일한 연결 요청은 무시합니다.
        SingleRejectDuplicate, ///< 다른 출력에 연결하면 교체하고 동일한 연결 요청은 거부합니다.
        Multiple ///< 같은 출발점의 중복 연결을 포함해 여러 링크를 허용합니다.
    };

    /**
     * @brief 연결의 허용 여부와 사용자가 볼 설명입니다.
     */
    struct FConnectionResult {
        TGL::EConnection mDecision{TGL::EConnection::Reject}; ///< 스키마의 연결 판정입니다.
        std::string mMessage{}; ///< 연결 미리보기에 표시할 설명입니다.
    };

    /**
     * @brief 편집을 적용한 결과와 생성된 노드의 식별자입니다.
     */
    struct FEditResult {
        TGL::EEditStatus mStatus{TGL::EEditStatus::NoChange}; ///< 원본에 요청을 반영한 결과입니다.
        std::string mMessage{}; ///< 호스트가 표시할 수 있는 메시지입니다.
        TGL::FNodeId mCreatedNode{}; ///< 성공한 생성 요청의 노드입니다. 생성한 노드가 없으면 값은 0입니다.
    };

    /**
     * @brief 스키마가 노드나 링크에 연결한 검증 메시지입니다.
     */
    struct FDiagnostic {
        TGL::FSelectionItem mItem{}; ///< 검증 메시지를 표시할 노드 또는 링크입니다.
        std::string mMessage{}; ///< 문서 규칙 위반 등의 진단 설명입니다.
    };

    /**
     * @brief 원본 그래프를 캔버스용 읽기 뷰로 제공하는 인터페이스입니다.
     * @ingroup TGL
     */
    class IGraphDocument {
    public:
        /**
         * @brief 문서 구현을 소멸합니다.
         */
        virtual ~IGraphDocument() = default;

    public:
        /**
         * @brief 현재 노드와 링크를 조회합니다.
         * @return 문서가 소유한 데이터의 비소유 뷰입니다. 문서가 해당 데이터를 교체하면 범위와 원소 주소가 무효화될 수 있습니다.
         */
        virtual TGL::FGraphView ReadView() const = 0;
    };

    /**
     * @brief 노드 추가 동작, 연결 규칙 및 문서 진단을 정의하는 인터페이스입니다.
     * @details 공용 편집기는 연결을 미리 판정할 때 스키마를 조회합니다. 실제 원본 변경은 편집 대상이 처리합니다.
     * @ingroup TGL
     */
    class IGraphSchema {
    public:
        /**
         * @brief 스키마 구현을 소멸합니다.
         */
        virtual ~IGraphSchema() = default;

    public:
        /**
         * @brief 현재 위치나 연결 끝점에서 만들 수 있는 노드의 동작을 나열합니다.
         * @param View 현재 문서 뷰입니다.
         * @param From 연결을 끌기 시작한 끝점입니다. 빈 캔버스에서 열었으면 값이 없습니다.
         * @return 노드 추가 메뉴에 표시할 동작 목록입니다.
         */
        virtual std::vector<TGL::FNodeAction> ListNodeActions(const TGL::FGraphView& View, const std::optional<TGL::FEndpoint>& From) const = 0;
        /**
         * @brief 요청한 연결을 허용하거나 교체할 수 있는지 판정합니다.
         * @param View 현재 문서 뷰입니다.
         * @param Connection 출력에서 입력으로 향하는 연결 요청입니다.
         * @return 연결 판정과 필요 시 표시할 설명입니다.
         */
        virtual TGL::FConnectionResult QueryConnection(const TGL::FGraphView& View, const TGL::FConnect& Connection) const = 0;
        /**
         * @brief 문서의 노드와 링크에 표시할 검증 결과를 반환합니다.
         * @param View 검증할 문서 뷰입니다.
         * @return 표시할 진단 목록입니다. 기본 구현은 빈 목록을 반환합니다.
         */
        virtual std::vector<TGL::FDiagnostic> ValidateDocument(const TGL::FGraphView& View) const;
    };

    /**
     * @brief 편집 요청을 도메인 원본에 적용하는 인터페이스입니다.
     * @ingroup TGL
     */
    class IGraphEditTarget {
    public:
        /**
         * @brief 편집 대상 구현을 소멸합니다.
         */
        virtual ~IGraphEditTarget() = default;

    public:
        /**
         * @brief 현재 그래프의 대상과 도메인 규칙을 확인하고 편집을 적용합니다.
         * @param Edit 검증하고 적용할 편집 요청입니다.
         * @return 요청의 적용 결과와 필요 시 설명 또는 새 노드 식별자입니다.
         */
        virtual TGL::FEditResult Apply(const TGL::FEdit& Edit) = 0;
    };

    /**
     * @brief 도메인 데이터에서 만든 노드·링크 배열을 소유하는 자료 스냅샷입니다.
     * @ingroup TGL
     */
    class FGraphSnapshot : public TGL::IGraphDocument {
    public:
        /**
         * @brief 스냅샷과 보관 중인 노드·링크를 소멸합니다.
         */
        ~FGraphSnapshot() override = default;

    public:
        /**
         * @copydoc TGL::IGraphDocument::ReadView
         */
        TGL::FGraphView ReadView() const override;
        /**
         * @brief 노드와 링크의 전체 목록을 교체합니다.
         * @param Nodes 다음 뷰에 사용할 노드 목록입니다.
         * @param Links 다음 뷰에 사용할 링크 목록입니다.
         */
        void Replace(std::vector<TGL::FNodeView> Nodes, std::vector<TGL::FLinkView> Links);

    private:
        std::vector<TGL::FNodeView> mNodes{}; ///< 스냅샷이 소유하는 노드 배열입니다.
        std::vector<TGL::FLinkView> mLinks{}; ///< 스냅샷이 소유하는 링크 배열입니다.
    };

    /**
     * @brief 공통 연결 판정과 편집 요청 검증을 제공하는 그래프 스키마입니다.
     * @details 공용 검사를 통과한 요청은 그래프별 구현에서 원본에 반영합니다.
     * @ingroup TGL
     */
    class FGraphSchema : public TGL::IGraphSchema, public TGL::IGraphEditTarget {
    public:
        /**
         * @param Document 편집과 검증에 사용할 문서이며 스키마보다 오래 살아 있어야 합니다.
         * @param EditTopology 노드·링크 생성, 삭제, 이동을 허용하면 true입니다.
         */
        explicit FGraphSchema(const TGL::IGraphDocument& Document, bool EditTopology = true);
        ~FGraphSchema() override = default;
        FGraphSchema(const TGL::FGraphSchema&) = delete;
        TGL::FGraphSchema& operator=(const TGL::FGraphSchema&) = delete;

    public:
        /** @copydoc TGL::IGraphSchema::ListNodeActions */
        std::vector<TGL::FNodeAction> ListNodeActions(const TGL::FGraphView& View, const std::optional<TGL::FEndpoint>& From) const final;
        /** @copydoc TGL::IGraphSchema::QueryConnection */
        TGL::FConnectionResult QueryConnection(const TGL::FGraphView& View, const TGL::FConnect& Connection) const final;
        /** @copydoc TGL::IGraphEditTarget::Apply */
        TGL::FEditResult Apply(const TGL::FEdit& Edit) final;

    private:
        virtual const char* GetEditUnavailableReason() const;
        virtual std::vector<TGL::FNodeAction> CollectNodeActions(const TGL::FGraphView& View, const std::optional<TGL::FEndpoint>& From, const TGL::FPinView* FromPin) const;
        virtual bool ArePinTypesCompatible(const TGL::FPinView& Output, const TGL::FPinView& Input) const;
        virtual TGL::EInputLinkPolicy GetInputLinkPolicy(const TGL::FEndpoint& Input) const;
        virtual TGL::FConnectionResult CheckConnectionRules(const TGL::FGraphView& View, const TGL::FConnect& Connection) const;

        virtual TGL::FEditResult ApplyValidatedEdit(const TGL::FEdit& Edit);
        virtual TGL::FEditResult ApplyEdit(const TGL::FCreateNode& Edit);
        virtual TGL::FEditResult ApplyEdit(const TGL::FConnect& Edit);
        virtual TGL::FEditResult ApplyEdit(const TGL::FMoveNodes& Edit);
        virtual TGL::FEditResult ApplyEdit(const TGL::FDeleteSelection& Edit);
        virtual TGL::FEditResult ApplyEdit(const TGL::FSetProperty& Edit);
        virtual TGL::FEditResult ApplyEdit(const TGL::FDisconnectInputs& Edit);
        virtual void Refresh();

        std::optional<TGL::FEditResult> CheckEdit(const TGL::FGraphView& View, const TGL::FCreateNode& Edit) const;
        std::optional<TGL::FEditResult> CheckEdit(const TGL::FGraphView& View, const TGL::FConnect& Edit) const;
        std::optional<TGL::FEditResult> CheckEdit(const TGL::FGraphView& View, const TGL::FMoveNodes& Edit) const;
        std::optional<TGL::FEditResult> CheckEdit(const TGL::FGraphView& View, const TGL::FDeleteSelection& Edit) const;
        std::optional<TGL::FEditResult> CheckEdit(const TGL::FGraphView& View, const TGL::FSetProperty& Edit) const;
        std::optional<TGL::FEditResult> CheckEdit(const TGL::FGraphView& View, const TGL::FDisconnectInputs& Edit) const;

    private:
        const TGL::IGraphDocument& mDocument;
        bool mEditTopology{};
    };

    /**
     * @brief 그래프를 읽기 전용으로 표시하는 스키마입니다.
     * @ingroup TGL
     */
    class FReadOnlyGraphSchema final : public TGL::FGraphSchema {
    public:
        /**
         * @param Document 읽을 그래프 문서입니다.
         */
        explicit FReadOnlyGraphSchema(const TGL::IGraphDocument& Document);
        ~FReadOnlyGraphSchema() override = default;
    };

    /**
     * @brief 끝점에 대응하는 노드 식별자를 반환합니다.
     * @param Endpoint 노드 또는 핀으로 지정된 연결 끝점입니다.
     * @return 끝점의 노드 또는 핀이 속한 노드의 식별자입니다.
     */
    TGL::FNodeId GetNodeId(const TGL::FEndpoint& Endpoint);
    TGL::FDeleteSelection GetDeletableInputLinks(const TGL::FGraphView& View, TGL::FNodeId Node);
    /**
     * @brief 문서 뷰에서 식별자가 일치하는 노드를 찾습니다.
     * @param Graph 조회할 문서 뷰입니다.
     * @param Id 찾을 노드의 식별자입니다.
     * @return 노드의 비소유 주소이며 없으면 nullptr입니다.
     */
    const TGL::FNodeView* FindNode(const TGL::FGraphView& Graph, TGL::FNodeId Id);
    /**
     * @brief 소유 노드와 핀 식별자로 핀을 찾습니다.
     * @param Graph 조회할 문서 뷰입니다.
     * @param Ref 찾을 노드와 핀의 식별자입니다.
     * @return 핀의 비소유 주소이며 노드나 핀이 없으면 nullptr입니다.
     */
    const TGL::FPinView* FindPin(const TGL::FGraphView& Graph, const TGL::FPinRef& Ref);
    /**
     * @brief 문서 뷰에서 식별자가 일치하는 링크를 찾습니다.
     * @param Graph 조회할 문서 뷰입니다.
     * @param Id 찾을 링크의 식별자입니다.
     * @return 링크의 비소유 주소이며 없으면 nullptr입니다.
     */
    const TGL::FLinkView* FindLink(const TGL::FGraphView& Graph, TGL::FLinkId Id);
    /**
     * @brief 출발 노드에서 링크 방향을 따라 도착 노드에 접근할 수 있는지 검사합니다.
     * @param Graph 순회할 문서 뷰입니다.
     * @param From 출발 노드입니다.
     * @param To 도착 노드입니다.
     * @return 두 노드가 존재하고 경로가 있으면 true입니다. 같은 노드는 길이가 0인 경로로 간주합니다.
     */
    bool HasPath(const TGL::FGraphView& Graph, TGL::FNodeId From, TGL::FNodeId To);
    /**
     * @brief 링크의 출발 노드를 도착 노드보다 앞에 두도록 정렬합니다.
     * @param Graph 정렬할 방향 그래프의 문서 뷰입니다.
     * @return 위상 순서입니다. 순환, 유효하지 않거나 중복된 노드 ID, 존재하지 않는 링크 끝점 노드가 있으면 값이 없습니다.
     */
    std::optional<std::vector<TGL::FNodeId>> TopologicalOrder(const TGL::FGraphView& Graph);

    /**
     * @brief 표시 정보와 도메인 데이터를 함께 소유하는 그래프 문서입니다.
     * @tparam NodeData 노드에 연결해 보관하는 데이터 타입입니다.
     * @tparam LinkData 링크에 연결해 보관하는 데이터 타입입니다.
     * @ingroup TGL
     */
    template <class NodeData, class LinkData>
    class TGraph final : public TGL::IGraphDocument {
    public:
        /**
         * @brief 빈 그래프를 생성합니다.
         */
        TGraph() = default;
        /**
         * @brief 그래프와 소유한 데이터를 소멸합니다.
         */
        ~TGraph() override = default;

    public:
        /**
         * @copydoc TGL::IGraphDocument::ReadView
         */
        TGL::FGraphView ReadView() const override;

        /**
         * @brief 새 식별자를 배정해 노드와 데이터를 추가합니다.
         * @param Node 노드의 표시 정보와 위치입니다.
         * @param Data 노드에 연결할 데이터입니다.
         * @return 새 노드 식별자이며 위치나 핀 식별자가 유효하지 않거나 ID를 할당할 수 없으면 값은 0입니다.
         */
        TGL::FNodeId AddNode(TGL::FNodeView Node, NodeData Data);
        /**
         * @brief 끝점과 핀 방향을 확인하고 링크와 데이터를 추가합니다.
         * @param Link 새 링크의 끝점과 표시 정보입니다.
         * @param Data 링크에 연결할 데이터입니다.
         * @return 새 링크 식별자이며 끝점에 오류가 있거나 ID를 할당할 수 없으면 값은 0입니다.
         */
        TGL::FLinkId AddLink(TGL::FLinkView Link, LinkData Data);
        /**
         * @brief 노드와 노드를 끝점으로 사용하는 링크를 삭제합니다.
         * @param Id 삭제할 노드의 식별자입니다.
         * @return 존재하는 노드를 삭제했으면 true입니다.
         */
        bool RemoveNode(TGL::FNodeId Id);
        /**
         * @brief 지정한 링크와 연결된 데이터를 삭제합니다.
         * @param Id 삭제할 링크의 식별자입니다.
         * @return 존재하는 링크를 삭제했으면 true입니다.
         */
        bool RemoveLink(TGL::FLinkId Id);

        /**
         * @brief 이동이 허용된 노드의 캔버스 위치를 바꿉니다.
         * @param Id 이동할 노드의 식별자입니다.
         * @param Position 적용할 유한한 좌표입니다.
         * @return 위치가 바뀌었으면 true입니다.
         */
        bool SetPosition(TGL::FNodeId Id, TGL::FPoint Position);
        /**
         * @brief 속성의 쓰기 가능 여부, 값 타입, 선택지 제약을 확인하고 값을 바꿉니다.
         * @param Edit 대상 항목, 키 및 새 값을 포함한 요청입니다.
         * @return 속성 값이 바뀌었으면 true입니다.
         */
        bool SetProperty(const TGL::FSetProperty& Edit);
        /**
         * @brief 노드가 소유한 도메인 데이터를 조회합니다.
         * @param Id 조회할 노드의 식별자입니다.
         * @return 데이터의 비소유 주소이며 노드가 없으면 nullptr입니다.
         */
        const NodeData* FindNodeData(TGL::FNodeId Id) const;
        /**
         * @brief 링크가 소유한 도메인 데이터를 조회합니다.
         * @param Id 조회할 링크의 식별자입니다.
         * @return 데이터의 비소유 주소이며 링크가 없으면 nullptr입니다.
         */
        const LinkData* FindLinkData(TGL::FLinkId Id) const;

    private:
        std::vector<TGL::FNodeView> mNodes{}; ///< 그래프가 소유하는 노드 목록입니다.
        std::vector<TGL::FLinkView> mLinks{}; ///< 그래프가 소유하는 링크 목록입니다.
        std::unordered_map<std::uintptr_t, NodeData> mNodeData{}; ///< 노드 식별자별 도메인 데이터입니다.
        std::unordered_map<std::uintptr_t, LinkData> mLinkData{}; ///< 링크 식별자별 도메인 데이터입니다.
        std::uintptr_t mNextNode{1}; ///< 다음에 발급할 노드 식별자입니다.
        std::uintptr_t mNextLink{1}; ///< 다음에 발급할 링크 식별자입니다.
    };

    template <class Tag>
    TId<Tag>::operator bool() const {
        return mValue != 0;
    }

    template <class Tag>
    std::uintptr_t TId<Tag>::Get() const {
        return mValue;
    }

    template <class NodeData, class LinkData>
    TGL::FGraphView TGraph<NodeData, LinkData>::ReadView() const {
        return {mNodes, mLinks};
    }

    template <class NodeData, class LinkData>
    TGL::FNodeId TGraph<NodeData, LinkData>::AddNode(TGL::FNodeView Node, NodeData Data) {
        if (mNextNode == 0 || !std::isfinite(Node.mPosition.mX) || !std::isfinite(Node.mPosition.mY) || !std::ranges::all_of(Node.mPins, [](const TGL::FPinView& Pin) { return static_cast<bool>(Pin.mId); })) {
            return {};
        }

        for (auto Pin{Node.mPins.begin()}; Pin != Node.mPins.end(); ++Pin) {
            if (std::find_if(Node.mPins.begin(), Pin, [Id = Pin->mId](const TGL::FPinView& Other) { return Other.mId == Id; }) != Pin) {
                return {};
            }
        }

        const TGL::FNodeId Id{mNextNode};
        Node.mId = Id;
        if (mNodes.size() == mNodes.capacity()) {
            mNodes.reserve(std::max<std::size_t>(8, mNodes.size() * 2));
        }
        mNodeData.emplace(Id.Get(), std::move(Data));
        mNodes.push_back(std::move(Node));
        ++mNextNode;

        return Id;
    }

    template <class NodeData, class LinkData>
    TGL::FLinkId TGraph<NodeData, LinkData>::AddLink(TGL::FLinkView Link, LinkData Data) {
        const TGL::FGraphView View{ReadView()};
        const auto Valid{[&View](const TGL::FEndpoint& Endpoint, TGL::EPinDirection Direction) {
            if (const auto* Ref{std::get_if<TGL::FPinRef>(&Endpoint)}) {
                const TGL::FPinView* Pin{TGL::FindPin(View, *Ref)};

                return Pin != nullptr && Pin->mDirection == Direction;
            }

            return TGL::FindNode(View, std::get<TGL::FNodeId>(Endpoint)) != nullptr;
        }};

        if (mNextLink == 0 || !Valid(Link.mFrom, TGL::EPinDirection::Output) || !Valid(Link.mTo, TGL::EPinDirection::Input)) {
            return {};
        }

        const TGL::FLinkId Id{mNextLink};
        Link.mId = Id;
        if (mLinks.size() == mLinks.capacity()) {
            mLinks.reserve(std::max<std::size_t>(8, mLinks.size() * 2));
        }
        mLinkData.emplace(Id.Get(), std::move(Data));
        mLinks.push_back(std::move(Link));
        ++mNextLink;

        return Id;
    }

    template <class NodeData, class LinkData>
    bool TGraph<NodeData, LinkData>::RemoveNode(TGL::FNodeId Id) {
        if (mNodeData.erase(Id.Get()) == 0) {
            return false;
        }

        std::erase_if(mNodes, [Id](const TGL::FNodeView& Node) { return Node.mId == Id; });
        std::erase_if(mLinks, [this, Id](const TGL::FLinkView& Link) {
            if (TGL::GetNodeId(Link.mFrom) != Id && TGL::GetNodeId(Link.mTo) != Id) {
                return false;
            }

            mLinkData.erase(Link.mId.Get());

            return true;
        });

        return true;
    }

    template <class NodeData, class LinkData>
    bool TGraph<NodeData, LinkData>::RemoveLink(TGL::FLinkId Id) {
        if (mLinkData.erase(Id.Get()) == 0) {
            return false;
        }

        std::erase_if(mLinks, [Id](const TGL::FLinkView& Link) { return Link.mId == Id; });

        return true;
    }

    template <class NodeData, class LinkData>
    bool TGraph<NodeData, LinkData>::SetPosition(TGL::FNodeId Id, TGL::FPoint Position) {
        const auto Node{std::ranges::find(mNodes, Id, &TGL::FNodeView::mId)};

        if (Node == mNodes.end() || !Node->mMovable || Node->mPosition == Position || !std::isfinite(Position.mX) || !std::isfinite(Position.mY)) {
            return false;
        }

        Node->mPosition = Position;

        return true;
    }

    template <class NodeData, class LinkData>
    bool TGraph<NodeData, LinkData>::SetProperty(const TGL::FSetProperty& Edit) {
        std::span<TGL::FPropertyView> Properties{};

        if (const auto* Id{std::get_if<TGL::FNodeId>(&Edit.mItem)}) {
            const auto Node{std::ranges::find(mNodes, *Id, &TGL::FNodeView::mId)};

            if (Node != mNodes.end()) {
                Properties = Node->mProperties;
            }
        } else {
            const auto Link{std::ranges::find(mLinks, std::get<TGL::FLinkId>(Edit.mItem), &TGL::FLinkView::mId)};

            if (Link != mLinks.end()) {
                Properties = Link->mProperties;
            }
        }

        const auto Property{std::ranges::find(Properties, Edit.mKey, &TGL::FPropertyView::mKey)};

        if (Property == Properties.end() || Property->mReadOnly || Property->mValue.index() != Edit.mValue.index() || Property->mValue == Edit.mValue) {
            return false;
        }

        if (const auto* Value{std::get_if<double>(&Edit.mValue)}; Value != nullptr && !std::isfinite(*Value)) {
            return false;
        }

        const auto* Option{std::get_if<std::string>(&Edit.mValue)};

        if (!Property->mOptions.empty() && (Option == nullptr || std::ranges::find(Property->mOptions, *Option) == Property->mOptions.end())) {
            return false;
        }

        TGL::FPropertyValue Value{Edit.mValue};
        Property->mValue = std::move(Value);

        return true;
    }

    template <class NodeData, class LinkData>
    const NodeData* TGraph<NodeData, LinkData>::FindNodeData(TGL::FNodeId Id) const {
        const auto Found{mNodeData.find(Id.Get())};

        return Found == mNodeData.end() ? nullptr : &Found->second;
    }

    template <class NodeData, class LinkData>
    const LinkData* TGraph<NodeData, LinkData>::FindLinkData(TGL::FLinkId Id) const {
        const auto Found{mLinkData.find(Id.Get())};

        return Found == mLinkData.end() ? nullptr : &Found->second;
    }

}
