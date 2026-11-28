/**
 * @file
 * @brief 전이 조건의 입력·출력 타입 검사, 편집 적용 및 공용 그래프 표시를 구현합니다.
 * @ingroup Editor
 */

#include "FAnimTransitionGraphEditor.h"

#include "Asset/Animation/UAnimGraph.h"

#include <charconv>
#include <cmath>
#include <limits>

using namespace Editor;
using namespace TGL;

namespace {

    using FRule = Asset::FAnimTransitionGraph; ///< 전이 조건의 원본 그래프 타입입니다.
    using ERuleNode = Asset::EAnimRuleNodeType; ///< 전이 조건 원본에서 정의하는 노드 종류입니다.

    constexpr FNodeId ResultId{1}; ///< 전이 허용 여부를 받는 Bool 결과 노드의 예약 식별자입니다.
    constexpr FPinId OutputPin{3}; ///< 전이 조건 노드의 출력 핀 식별자입니다.
    constexpr std::string_view GetterPrefix{"Getter:"}; ///< 특정 변수를 지정한 Getter 생성 동작의 접두어입니다.
    /**
     * @brief 전이 조건 값 타입에 맞는 핀 색을 구합니다.
     * @param Bool Bool 핀이면 true이며 false이면 Float 핀입니다.
     * @return 압축 RGBA 핀 색상입니다.
     */
    std::uint32_t PinColor(bool Bool) {
        const Canvas::EConnectionRole Role{Bool ? Canvas::EConnectionRole::Boolean : Canvas::EConnectionRole::Number};

        return ImGui::ColorConvertFloat4ToU32(Canvas::ConnectionColor(Role));
    }

    /**
     * @brief 원본 노드 번호에서 캔버스의 노드 식별자를 만듭니다.
     * @param Id 전이 조건 에셋의 원본 노드 번호입니다.
     * @return 결과 노드와 구별되는 문서 노드 식별자입니다.
     */
    FNodeId NodeKey(Int32 Id) {
        return {static_cast<std::uintptr_t>(Id) + 2};
    }

    /**
     * @brief 조건 노드의 문서 식별자를 원본 노드 번호로 변환합니다.
     * @param Id 조회할 문서 노드입니다.
     * @return 원본 노드 번호입니다. 결과 노드 또는 빈 ID는 -1입니다.
     */
    Int32 NodeIndex(FNodeId Id) {
        return Id.Get() >= 2 ? static_cast<Int32>(Id.Get() - 2) : -1;
    }

    /**
     * @brief 전이 조건의 타입 정보를 표시하는 핀을 구성합니다.
     * @param Id 노드 안의 핀 식별자입니다.
     * @param Direction 핀의 입력 또는 출력 방향입니다.
     * @param Name 표시할 핀 이름입니다.
     * @param Bool 타입이 Bool이면 true이며 false이면 Float입니다.
     * @return 공용 그래프에 제출할 핀 뷰입니다.
     */
    FPinView MakePin(FPinId Id, EPinDirection Direction, const char* Name, bool Bool) {
        FPinView Pin{Id, Direction, Name, Bool ? "Bool" : "Float"};
        Pin.mColor = PinColor(Bool);

        return Pin;
    }

    /**
     * @brief 전이 조건의 연결을 문서 링크 목록에 추가합니다.
     * @param[in,out] Links 변환한 연결을 추가할 링크 목록입니다.
     * @param From 출발 노드의 원본 번호입니다.
     * @param To 도착 노드의 문서 식별자입니다.
     * @param Pin 도착 노드의 입력 핀입니다.
     * @param Bool Bool 값 연결이면 true이며 false이면 Float 연결입니다.
     */
    void AddLink(std::vector<FLinkView>& Links, Int32 From, FNodeId To, FPinId Pin, bool Bool) {
        FLinkView Link{};
        Link.mId = {To.Get() * 2 + Pin.Get()};
        Link.mFrom = FPinRef{NodeKey(From), OutputPin};
        Link.mTo = FPinRef{To, Pin};
        Link.mColor = PinColor(Bool);
        Links.push_back(std::move(Link));
    }

    /**
     * @brief 변수 Getter가 Bool 변수를 필요로 하는지 검사합니다.
     * @param Type 조회할 전이 조건 노드 종류입니다.
     * @return Bool Getter이면 true입니다.
     */
    bool WantBool(ERuleNode Type) {
        return Type == ERuleNode::GetBool;
    }

    /**
     * @brief 전이 조건의 입력 핀 또는 Bool 결과에 출력을 연결합니다.
     * @param[in,out] Graph 연결을 적용할 후보 그래프입니다.
     * @param From 조건 노드의 출력 핀입니다.
     * @param To 다른 노드의 입력 또는 결과 핀입니다.
     * @return 유효한 새 연결을 반영했으면 true입니다.
     */
    bool Connect(FRule& Graph, const FPinRef& From, const FPinRef& To) {
        if (From.mPin != OutputPin) {
            return false;
        }

        const Int32 Source{NodeIndex(From.mNode)};

        if (Source < 0) {
            return false;
        }

        if (To.mNode == ResultId) {
            return To.mPin == FPinId{1} && Source != Graph.GetResult() && Graph.SetResult(Source);
        }

        const auto* Node{Graph.FindNode(NodeIndex(To.mNode))};
        const Index_t Pin{static_cast<Index_t>(To.mPin.Get()) - 1};

        return Node != nullptr && (Pin == 0 ? Node->mInputA : Node->mInputB) != Source && Graph.Connect(Source, Node->mId, Pin);
    }

}

FAnimTransitionGraphEditor::FAnimTransitionGraphEditor()
    : TGL::FGraphSchema{static_cast<const TGL::FGraphSnapshot&>(*this)} {
}

FGraphEvents FAnimTransitionGraphEditor::Draw(Asset::FAnimTransitionGraph& Graph, const Asset::UAnimGraph& Owner, ImVec2 Size) {
    mGraph = &Graph;
    mOwner = &Owner;
    Refresh();

    return mEditor.Draw("TransitionRuleCanvas", *this, *this, this, Size);
}

FEditResult FAnimTransitionGraphEditor::DrawDetails() {
    if (mEditor.GetSelection().empty() || mEditor.GetSelection().front() == FSelectionItem{ResultId}) {
        ImGui::TextWrapped("Connect a Bool output to Can Enter Transition. An unconnected result is false.");
    } else if (const auto* Id{std::get_if<FNodeId>(&mEditor.GetSelection().front())}) {
        if (const auto* Node{mGraph->FindNode(NodeIndex(*Id))}) {
            ImGui::SeparatorText(FRule::GetTitle(Node->mType));

            if (Node->mType == ERuleNode::TimeRemaining || Node->mType == ERuleNode::Finished) {
                ImGui::TextWrapped("Leave empty when the source state contains a single animation player. Time Remaining uses seconds at the current play rate.");
            }
        }
    }

    return mEditor.DrawDetails(*this, this);
}

std::span<const FSelectionItem> FAnimTransitionGraphEditor::GetSelection() const {
    return mEditor.GetSelection();
}

FEditResult FAnimTransitionGraphEditor::AddVariable(const std::string& Name, FPoint Position) {
    return mEditor.SubmitEdit(*this, FCreateNode{std::string{GetterPrefix} + Name, Position});
}

FEditResult FAnimTransitionGraphEditor::Delete(FSelectionItem Item) {
    return mEditor.SubmitEdit(*this, FDeleteSelection{{Item}});
}

std::vector<FNodeAction> FAnimTransitionGraphEditor::CollectNodeActions(const FGraphView&, const std::optional<FEndpoint>&, const FPinView* Pin) const {
    std::vector<FNodeAction> Actions{};

    for (std::size_t Index{}; Index < static_cast<std::size_t>(ERuleNode::Count); ++Index) {
        const auto Type{static_cast<ERuleNode>(Index)};
        const bool Bool{Pin != nullptr && Pin->mType == "Bool"};

        if (Pin != nullptr && (Pin->mDirection == EPinDirection::Input ? FRule::IsBool(Type) != Bool : FRule::GetInputCount(Type) == 0 || FRule::IsBoolInput(Type) != Bool)) {
            continue;
        }

        Actions.push_back({std::to_string(Index), FRule::GetTitle(Type)});
    }

    for (const Asset::FAnimGraphVariable& Variable : mOwner->GetVariables()) {
        const bool Bool{Variable.mType == Asset::EAnimGraphVariableType::Bool};

        if (Pin == nullptr || (Pin->mDirection == EPinDirection::Input && (Pin->mType == "Bool") == Bool)) {
            Actions.push_back({std::string{GetterPrefix} + Variable.mName, "Get " + Variable.mName, "Variables"});
        }
    }

    return Actions;
}

FConnectionResult FAnimTransitionGraphEditor::CheckConnectionRules(const FGraphView& View, const FConnect& Connection) const {
    const auto* From{std::get_if<FPinRef>(&Connection.mFrom)};
    const auto* To{std::get_if<FPinRef>(&Connection.mTo)};

    if (From == nullptr || To == nullptr) {
        return {EConnection::Reject, "Select pins to create a connection."};
    }
    if (HasPath(View, To->mNode, From->mNode)) {
        return {EConnection::Reject, "This connection would create a cycle."};
    }

    return {EConnection::Allow};
}

EInputLinkPolicy FAnimTransitionGraphEditor::GetInputLinkPolicy(const FEndpoint&) const {
    return EInputLinkPolicy::SingleRejectDuplicate;
}

const char* FAnimTransitionGraphEditor::GetEditUnavailableReason() const {
    return mGraph == nullptr ? "No transition rule is open." : nullptr;
}

FEditResult FAnimTransitionGraphEditor::ApplyValidatedEdit(const FEdit& Edit) {
    FRule Candidate{*mGraph};
    FNodeId Created{};
    const bool Changed{std::visit([this, &Candidate, &Created](const auto& Request) {
        if constexpr (std::is_same_v<std::decay_t<decltype(Request)>, FCreateNode>) {
            Created = Create(Candidate, Request);

            return static_cast<bool>(Created);
        } else {
            return ApplyTo(Candidate, Request);
        }
    }, Edit)};

    if (!Changed || !Commit(std::move(Candidate))) {
        return {EEditStatus::Rejected, "The transition rule could not be edited."};
    }

    return {EEditStatus::Applied, {}, Created};
}

void FAnimTransitionGraphEditor::Refresh() {
    std::vector<FNodeView> Nodes{};
    std::vector<FLinkView> Links{};

    FNodeView Result{};
    Result.mId = ResultId;
    Result.mPosition = {450.0f, 0.0f};
    Result.mTitle = "Result";
    Result.mType = "Can Enter Transition";
    Result.mMovable = false;
    Result.mDeletable = false;
    Result.mPins.push_back(MakePin({1}, EPinDirection::Input, "Can Enter", true));
    Nodes.push_back(std::move(Result));

    if (mGraph->GetResult() >= 0) {
        AddLink(Links, mGraph->GetResult(), ResultId, FPinId{1}, true);
    }

    for (const Asset::FAnimRuleNode& Node : mGraph->GetNodes()) {
        FNodeView View{};
        View.mId = NodeKey(Node.mId);
        View.mPosition = {Node.mX, Node.mY};
        View.mTitle = FRule::GetTitle(Node.mType);
        const bool BoolOutput{FRule::IsBool(Node.mType)};
        const bool BoolInput{FRule::IsBoolInput(Node.mType)};

        for (Int32 Index{}; Index < FRule::GetInputCount(Node.mType); ++Index) {
            const FPinId Pin{static_cast<std::uintptr_t>(Index) + 1};
            View.mPins.push_back(MakePin(Pin, EPinDirection::Input, Index == 0 ? "A" : "B", BoolInput));
            const Int32 Input{Index == 0 ? Node.mInputA : Node.mInputB};

            if (Input >= 0) {
                AddLink(Links, Input, View.mId, Pin, BoolInput);
            }
        }

        View.mPins.push_back(MakePin(OutputPin, EPinDirection::Output, BoolOutput ? "Bool" : "Float", BoolOutput));

        if (Node.mType == ERuleNode::Float) {
            View.mProperties.push_back({"Value", "Value", static_cast<double>(Node.mValue)});
        } else if (Node.mType == ERuleNode::Bool) {
            View.mProperties.push_back({"Value", "Value", Node.mBoolValue});
        } else if (Node.mType == ERuleNode::GetFloat || Node.mType == ERuleNode::GetBool) {
            FPropertyView Variable{"Variable", "Variable", Node.mName};

            for (const Asset::FAnimGraphVariable& Entry : mOwner->GetVariables()) {
                if (WantBool(Node.mType) == (Entry.mType == Asset::EAnimGraphVariableType::Bool)) {
                    Variable.mOptions.push_back(Entry.mName);
                }
            }

            Variable.mReadOnly = Variable.mOptions.empty();
            View.mProperties.push_back(std::move(Variable));
        } else if (Node.mType == ERuleNode::TimeRemaining || Node.mType == ERuleNode::Finished) {
            View.mProperties.push_back({"Player", "Player Name", Node.mName});
        }

        Nodes.push_back(std::move(View));
    }

    Replace(std::move(Nodes), std::move(Links));
}

bool FAnimTransitionGraphEditor::Commit(Asset::FAnimTransitionGraph Candidate) {
    if (!Candidate.Validate()) {
        return false;
    }

    *mGraph = std::move(Candidate);

    return true;
}

FNodeId FAnimTransitionGraphEditor::Create(Asset::FAnimTransitionGraph& Graph, const FCreateNode& Edit) const {
    ERuleNode Kind{};
    std::string VariableName{};

    if (Edit.mAction.starts_with(GetterPrefix)) {
        VariableName = Edit.mAction.substr(GetterPrefix.size());
        const auto& Variables{mOwner->GetVariables()};
        const auto Variable{std::ranges::find(Variables, VariableName, &Asset::FAnimGraphVariable::mName)};
        Kind = Variable->mType == Asset::EAnimGraphVariableType::Bool ? ERuleNode::GetBool : ERuleNode::GetFloat;
    } else {
        std::size_t Type{};
        std::from_chars(Edit.mAction.data(), Edit.mAction.data() + Edit.mAction.size(), Type);
        Kind = static_cast<ERuleNode>(Type);
    }

    const Int32 Id{Graph.AddNode(Kind, Edit.mPosition.mX, Edit.mPosition.mY)};
    auto* Node{Graph.FindNode(Id)};

    if (Node == nullptr) {
        return {};
    }

    Node->mName = std::move(VariableName);

    if (const auto* From{Edit.mConnectFrom ? std::get_if<FPinRef>(&*Edit.mConnectFrom) : nullptr}) {
        const bool Connected{From->mPin == OutputPin ? Connect(Graph, *From, {NodeKey(Id), {1}}) : Connect(Graph, {NodeKey(Id), OutputPin}, *From)};

        if (!Connected) {
            return {};
        }
    }

    return NodeKey(Id);
}

bool FAnimTransitionGraphEditor::ApplyTo(Asset::FAnimTransitionGraph& Graph, const FConnect& Edit) const {
    return Connect(Graph, std::get<FPinRef>(Edit.mFrom), std::get<FPinRef>(Edit.mTo));
}

bool FAnimTransitionGraphEditor::ApplyTo(Asset::FAnimTransitionGraph& Graph, const FDeleteSelection& Edit) const {
    bool Changed{};

    for (const FSelectionItem& Item : Edit.mItems) {
        if (const auto* Node{std::get_if<FNodeId>(&Item)}) {
            Changed |= Graph.RemoveNode(NodeIndex(*Node));
        } else if (const FLinkView* Link{FindLink(ReadView(), std::get<FLinkId>(Item))}) {
            const FPinRef& Pin{std::get<FPinRef>(Link->mTo)};
            Changed |= Pin.mNode == ResultId ? Graph.GetResult() >= 0 && Graph.SetResult(-1) : Graph.Disconnect(NodeIndex(Pin.mNode), static_cast<Index_t>(Pin.mPin.Get()) - 1);
        }
    }

    return Changed;
}

bool FAnimTransitionGraphEditor::ApplyTo(Asset::FAnimTransitionGraph& Graph, const FMoveNodes& Edit) const {
    bool Changed{};

    for (const FMoveNode& Item : Edit.mMoves) {
        auto* Node{Graph.FindNode(NodeIndex(Item.mNode))};

        if (Node == nullptr) {
            return false;
        }

        Changed |= Node->mX != Item.mPosition.mX || Node->mY != Item.mPosition.mY;
        Node->mX = Item.mPosition.mX;
        Node->mY = Item.mPosition.mY;
    }

    return Changed;
}

bool FAnimTransitionGraphEditor::ApplyTo(Asset::FAnimTransitionGraph& Graph, const FDisconnectInputs& Edit) const {
    return ApplyTo(Graph, GetDeletableInputLinks(ReadView(), Edit.mNode));
}

bool FAnimTransitionGraphEditor::ApplyTo(Asset::FAnimTransitionGraph& Graph, const FSetProperty& Edit) const {
    const auto* Id{std::get_if<FNodeId>(&Edit.mItem)};
    auto* Node{Id != nullptr ? Graph.FindNode(NodeIndex(*Id)) : nullptr};

    if (Node == nullptr) {
        return false;
    }

    if (Edit.mKey == "Value" && Node->mType == ERuleNode::Bool) {
        const auto* Value{std::get_if<bool>(&Edit.mValue)};

        if (Value != nullptr) {
            Node->mBoolValue = *Value;

            return true;
        }
    }

    if (Edit.mKey == "Value" && Node->mType == ERuleNode::Float) {
        const auto* Value{std::get_if<double>(&Edit.mValue)};

        if (Value != nullptr && std::isfinite(*Value) && std::abs(*Value) <= std::numeric_limits<float>::max()) {
            Node->mValue = static_cast<float>(*Value);

            return true;
        }
    }

    const auto* Text{std::get_if<std::string>(&Edit.mValue)};

    if (Text == nullptr || Text->size() > 255) {
        return false;
    }

    if (Edit.mKey == "Player" && (Node->mType == ERuleNode::TimeRemaining || Node->mType == ERuleNode::Finished)) {
        Node->mName = *Text;

        return true;
    }

    if (Edit.mKey == "Variable" && (Node->mType == ERuleNode::GetBool || Node->mType == ERuleNode::GetFloat)) {
        const auto& Variables{mOwner->GetVariables()};
        const auto Found{std::ranges::find(Variables, *Text, &Asset::FAnimGraphVariable::mName)};

        if (Found != Variables.end() && WantBool(Node->mType) == (Found->mType == Asset::EAnimGraphVariableType::Bool)) {
            Node->mName = *Text;

            return true;
        }
    }

    return false;
}
