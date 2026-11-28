/**
 * @file
 * @brief AnimGraph 포즈 및 상태 머신의 TGL 뷰, 연결 규칙과 편집 적용을 구현합니다.
 * @ingroup Editor
 */

#include "FAnimAssetGraphDocuments.h"

#include "Asset/Animation/UAnimGraph.h"
#include "Core/FilePath.h"
#include "Engine/Animation/FAnimGraphNodeRegistry.h"

#include <charconv>
#include <format>
#include <limits>

using namespace Editor;
using namespace TGL;

namespace {

    constexpr FNodeId SpecialNode{1}; ///< 포즈 문서의 Output Pose 및 상태 머신 문서의 Entry 예약 노드입니다.
    constexpr FPinId OutputPin{1}; ///< 일반 포즈 노드와 Getter가 제공하는 출력 핀입니다.
    constexpr std::uintptr_t PosePinBase{2}; ///< 포즈 입력 슬롯을 핀 식별자로 변환할 때 사용하는 시작값입니다.
    constexpr std::uintptr_t ValuePinBase{256}; ///< Float 입력과 포즈 입력을 분리하는 핀 식별자의 시작값입니다.
    constexpr std::uintptr_t GetterBit{std::uintptr_t{1} << 62}; ///< 원본 포즈 노드와 별도로 발급한 Getter 식별자를 구분하는 비트입니다.
    constexpr std::string_view GetterPrefix{"Getter:"}; ///< 변수 Getter 동작 키의 접두어입니다.

    /**
     * @brief 에셋 식별자를 TGL 노드 식별자로 변환합니다.
     * @param Id 원본 에셋 식별자입니다. -1은 예약 노드에 대응합니다.
     * @return 해당하는 TGL 노드 식별자입니다.
     */
    FNodeId NodeId(Int32 Id) {
        return {static_cast<std::uintptr_t>(static_cast<std::int64_t>(Id) + 2)};
    }

    /**
     * @brief 예약값을 제외한 TGL 식별자를 원본 식별자로 변환합니다.
     * @param Id 변환할 TGL 정수 식별자입니다.
     * @return 원본 식별자입니다. 예약값이나 범위를 벗어난 값이면 -1입니다.
     */
    Int32 DecodeId(std::uintptr_t Id) {
        return Id < 2 || Id - 2 > std::numeric_limits<Int32>::max() ? -1 : static_cast<Int32>(Id - 2);
    }

    /**
     * @brief 포즈 입력 슬롯을 문서의 핀 식별자로 변환합니다.
     * @param Index 노드 타입이 정의한 0부터 시작하는 입력 번호입니다.
     * @return 해당하는 포즈 입력 핀입니다.
     */
    FPinId PosePin(Index_t Index) {
        return {PosePinBase + static_cast<std::uintptr_t>(Index)};
    }

    /**
     * @brief 연결 하나만 받을 수 있는 입력 핀을 링크의 식별자로 변환합니다.
     * @param Input 링크의 도착 핀입니다.
     * @return 도착 노드와 핀 번호로 만든 링크 식별자입니다.
     */
    FLinkId InputLink(FPinRef Input) {
        return {(Input.mNode.Get() << 16) | Input.mPin.Get()};
    }

    /**
     * @brief 연결 용도에 맞는 공용 색을 문서 색상 형식으로 변환합니다.
     * @param Role 연결의 용도입니다.
     * @return ImGui에서 사용하는 압축 RGBA 값입니다.
     */
    ImU32 Color(Canvas::EConnectionRole Role) {
        return ImGui::ColorConvertFloat4ToU32(Canvas::ConnectionColor(Role));
    }

    /**
     * @brief 원본이 바뀌었는지에 따라 편집 결과를 만듭니다.
     * @param Value 원본 값이 변경됐는지 나타냅니다.
     * @return 변경이 있으면 Applied, 그대로이면 NoChange입니다.
     */
    FEditResult Changed(bool Value) {
        return {Value ? EEditStatus::Applied : EEditStatus::NoChange};
    }

    /**
     * @brief 사용자가 볼 설명이 포함된 편집 거부 결과를 만듭니다.
     * @param Message 편집을 거부한 이유입니다.
     * @return Rejected 결과와 설명입니다.
     */
    FEditResult Rejected(const char* Message) {
        return {EEditStatus::Rejected, Message};
    }

    /**
     * @brief 이름이 일치하는 Float 변수를 찾습니다.
     * @param Graph 변수를 소유한 에셋입니다.
     * @param Name 조회할 변수 이름입니다.
     * @return 변수의 비소유 주소입니다. 변수가 없거나 Float 타입이 아니면 nullptr입니다.
     */
    const Asset::FAnimGraphVariable* FindFloatVariable(const Asset::UAnimGraph& Graph, const std::string& Name) {
        const auto& Variables{Graph.GetVariables()};
        const auto Found{std::ranges::find(Variables, Name, &Asset::FAnimGraphVariable::mName)};

        return Found != Variables.end() && Found->mType == Asset::EAnimGraphVariableType::Float ? &*Found : nullptr;
    }

    /**
     * @brief 파라미터를 Float Getter의 입력으로 표시할 수 있는지 검사합니다.
     * @param Parameter 검사할 파라미터 메타데이터입니다.
     * @return Float 값의 변수 연결을 허용하면 true입니다.
     */
    bool IsBindable(const Engine::FAnimGraphParameterInfo& Parameter) {
        return Parameter.mType == Engine::EAnimGraphParameterType::Float && Parameter.mBindable;
    }

    /**
     * @brief Float 입력 핀에 대응하는 파라미터를 조회합니다.
     * @param Node 핀을 소유한 원본 포즈 노드입니다.
     * @param Pin 조회할 문서 핀의 식별자입니다.
     * @return 연결 가능한 Float 파라미터이며 없으면 nullptr입니다.
     */
    const Engine::FAnimGraphParameterInfo* FindValueInput(const Asset::FAnimGraphNode* Node, FPinId Pin) {
        const auto* Type{Node != nullptr ? Engine::FAnimGraphNodeRegistry::Find(Node->mType) : nullptr};
        const std::uintptr_t Index{Pin.Get() >= ValuePinBase ? Pin.Get() - ValuePinBase : std::numeric_limits<std::uintptr_t>::max()};

        return Type != nullptr && Index < Type->mParameters.size() && IsBindable(Type->mParameters[Index]) ? &Type->mParameters[Index] : nullptr;
    }

    /**
     * @brief 원본 파라미터의 속성을 표시하고 Float 바인딩의 핀과 링크를 추가합니다.
     * @param Graph 파라미터가 참조하는 변수와 Getter를 소유한 에셋입니다.
     * @param Node 파라미터 값을 보관하는 원본 노드입니다.
     * @param Parameter 노드 타입이 등록한 속성 메타데이터입니다.
     * @param Index 노드 타입 안의 파라미터 번호입니다.
     * @param[in,out] View 속성과 필요한 입력 핀을 추가할 노드 뷰입니다.
     * @param[in,out] Links 이미 연결된 Float Getter의 링크를 추가할 목록입니다.
     */
    void AddPoseParameter(const Asset::UAnimGraph& Graph, const Asset::FAnimGraphNode& Node, const Engine::FAnimGraphParameterInfo& Parameter, std::size_t Index, FNodeView& View, std::vector<FLinkView>& Links) {
        using enum Engine::EAnimGraphParameterType;
        const std::string* Stored{Node.FindParameter(Parameter.mName)};
        const std::string& Text{Stored != nullptr ? *Stored : Parameter.mDefault};
        FPropertyView Property{Parameter.mName, Parameter.mName};

        if (Parameter.mType == Float) {
            float Number{};
            std::from_chars(Text.data(), Text.data() + Text.size(), Number);
            Property.mValue = static_cast<double>(Number);
            Property.mNumeric = FNumericEditor{Parameter.mMin, Parameter.mMax, 0.01f, Parameter.mMin < Parameter.mMax};

            if (Parameter.mBindable) {
                const std::string Key{Parameter.mName + ".Binding"};
                const std::string* Binding{Node.FindParameter(Key)};
                const auto* Getter{Graph.FindBoundGetter(Node, Parameter.mName)};

                const ImU32 Tint{Color(Canvas::EConnectionRole::Number)};
                const FPinRef Input{View.mId, {ValuePinBase + Index}};
                const std::optional<FPropertyValue> Default{Binding != nullptr ? FPropertyValue{*Binding} : Property.mValue};
                View.mPins.push_back({Input.mPin, EPinDirection::Input, Parameter.mName, "Float", Default, Tint});
                FPropertyView Option{Key, Parameter.mName + " Binding", Binding != nullptr ? *Binding : std::string{}, {""}};
                Option.mShowInNode = false;

                for (const Asset::FAnimGraphVariable& Variable : Graph.GetVariables()) {
                    if (Variable.mType == Asset::EAnimGraphVariableType::Float) {
                        Option.mOptions.push_back(Variable.mName);
                    }
                }

                if (Getter != nullptr) {
                    const FPinRef Output{{GetterBit | NodeId(Getter->mId).Get()}, OutputPin};
                    Links.push_back({InputLink(Input), Output, Input, {}, {}, Tint});
                }

                View.mProperties.push_back(std::move(Option));
                Property.mReadOnly = Binding != nullptr;
                Property.mShowInNode = false;
            }
        } else if (Parameter.mType == Bool) {
            Property.mValue = Text == "true";
        } else {
            Property.mValue = Text;
            Property.mCustomEditor = Parameter.mType == AnimSequence ? "AnimSequence" : Parameter.mType == BlendSpace ? "BlendSpace" : "PhysicsAsset";
            Property.mDisplayValue = Text.empty() ? std::string{"(없음)"} : Core::FilePath::ToUtf8(Core::FilePath::FromUtf8(Text).stem());
        }

        View.mProperties.push_back(std::move(Property));
    }

}

FAnimPoseGraphDocument::FAnimPoseGraphDocument()
    : TGL::FGraphSchema{static_cast<const TGL::FGraphSnapshot&>(*this)} {
}

void FAnimPoseGraphDocument::Bind(Asset::UAnimGraph& Graph) {
    mGraph = &Graph;

    for (const Asset::FAnimGraphNode& Node : Graph.GetNodes()) {
        const auto* Type{Engine::FAnimGraphNodeRegistry::Find(Node.mType)};

        if (Type == nullptr) {
            continue;
        }

        for (const auto& Parameter : Type->mParameters) {
            if (IsBindable(Parameter)) {
                const auto* Binding{Node.FindParameter(Parameter.mName + ".Binding")};

                if (Binding != nullptr && Graph.FindBoundGetter(Node, Parameter.mName) == nullptr) {
                    Graph.AddVariableGetter(*Binding, Node.mX - 245.0f, Node.mY - 170.0f);
                }
            }
        }
    }

    Refresh();
}

void FAnimPoseGraphDocument::Refresh() {
    const Asset::UAnimGraph& Graph{*mGraph};
    std::vector<FNodeView> Nodes{};
    std::vector<FLinkView> Links{};
    const ImU32 PoseColor{Color(Canvas::EConnectionRole::Pose)};
    const ImU32 ValueColor{Color(Canvas::EConnectionRole::Number)};
    FNodeView Output{SpecialNode, {Graph.GetOutputX(), Graph.GetOutputY()}, "Output Pose", {}, "Graph Output"};
    Output.mDeletable = false;
    Output.mPins.push_back({PosePin(0), EPinDirection::Input, "Result", "Pose", {}, PoseColor});
    Nodes.push_back(std::move(Output));

    for (const Asset::FAnimGraphNode& Node : Graph.GetNodes()) {
        const auto* Type{Engine::FAnimGraphNodeRegistry::Find(Node.mType)};
        const std::string& Title{Type != nullptr ? Type->mTitle : Node.mType};
        FNodeView View{NodeId(Node.mId), {Node.mX, Node.mY}, Node.mName.empty() ? Title : Node.mName, {}, Title};
        FPropertyView Name{"Name", "Name", Node.mName};
        Name.mShowInNode = false;
        View.mProperties.push_back(std::move(Name));
        View.mPins.push_back({OutputPin, EPinDirection::Output, "Pose", "Pose", {}, PoseColor});

        if (Type != nullptr) {
            for (std::size_t Index{}; Index < Type->mPoseInputs.size(); ++Index) {
                View.mPins.push_back({PosePin(Index), EPinDirection::Input, Type->mPoseInputs[Index], "Pose", {}, PoseColor});
            }

            for (std::size_t Index{}; Index < Type->mParameters.size(); ++Index) {
                AddPoseParameter(Graph, Node, Type->mParameters[Index], Index, View, Links);
            }
        } else {
            View.mSummary = "알 수 없는 노드 종류: " + Node.mType;
        }

        Nodes.push_back(std::move(View));
    }

    for (const Asset::FAnimGraphVariableGetter& Getter : Graph.GetVariableGetters()) {
        if (const Asset::FAnimGraphVariable* Variable{FindFloatVariable(Graph, Getter.mVariable)}) {
            FNodeView View{{GetterBit | NodeId(Getter.mId).Get()}, {Getter.mX, Getter.mY}, "Get " + Getter.mVariable, {}, "Float"};
            View.mPins.push_back({OutputPin, EPinDirection::Output, std::format("{:.3f}", Variable->mDefault), "Float", {}, ValueColor});
            Nodes.push_back(std::move(View));
        }
    }

    const FGraphView GraphView{Nodes, {}};

    for (const Asset::FAnimGraphLink& Link : Graph.GetLinks()) {
        const FPinRef From{NodeId(Link.mFromNode), OutputPin};
        const FPinRef To{NodeId(Link.mToNode), PosePin(Link.mToPin)};

        if (FindPin(GraphView, From) != nullptr && FindPin(GraphView, To) != nullptr) {
            Links.push_back({InputLink(To), From, To, {}, {}, PoseColor});
        }
    }

    Replace(std::move(Nodes), std::move(Links));
}

std::vector<FNodeAction> FAnimPoseGraphDocument::CollectNodeActions(const FGraphView&, const std::optional<FEndpoint>&, const FPinView* Pin) const {
    std::vector<FNodeAction> Actions{};

    for (const auto& Type : Engine::FAnimGraphNodeRegistry::GetNodeTypes()) {
        if (Pin == nullptr || (Pin->mType == "Pose" && (Pin->mDirection == EPinDirection::Input || !Type.mPoseInputs.empty())) || (Pin->mType == "Float" && Pin->mDirection == EPinDirection::Output && std::ranges::any_of(Type.mParameters, IsBindable))) {
            Actions.push_back({Type.mType, Type.mTitle});
        }
    }

    if (Pin == nullptr || (Pin->mType == "Float" && Pin->mDirection == EPinDirection::Input)) {
        for (const Asset::FAnimGraphVariable& Variable : mGraph->GetVariables()) {
            if (Variable.mType == Asset::EAnimGraphVariableType::Float) {
                Actions.push_back({GetterAction(Variable.mName), "Get " + Variable.mName, "Variables"});
            }
        }
    }

    return Actions;
}

FConnectionResult FAnimPoseGraphDocument::CheckConnectionRules(const FGraphView& View, const FConnect& Connection) const {
    const auto* From{std::get_if<FPinRef>(&Connection.mFrom)};
    const auto* To{std::get_if<FPinRef>(&Connection.mTo)};

    if (From == nullptr || To == nullptr) {
        return {EConnection::Reject, "포즈 그래프의 핀을 연결하세요."};
    }

    if (FindPin(View, *From)->mType == "Pose" && mGraph->WouldCreateCycle(GetPoseNodeId(From->mNode), GetPoseNodeId(To->mNode))) {
        return {EConnection::Reject, "포즈 연결은 순환할 수 없습니다."};
    }

    return {EConnection::Allow};
}

const char* FAnimPoseGraphDocument::GetEditUnavailableReason() const {
    return mGraph == nullptr ? "편집할 포즈 그래프가 없습니다." : nullptr;
}

std::string FAnimPoseGraphDocument::GetterAction(const std::string& Name) {
    return std::string{GetterPrefix} + Name;
}

Int32 FAnimPoseGraphDocument::GetPoseNodeId(FNodeId Id) {
    if ((Id.Get() & GetterBit) != 0 || !Id) {
        return -2;
    }

    return Id == SpecialNode ? Asset::UAnimGraph::OutputNodeId : DecodeId(Id.Get());
}

FEditResult FAnimPoseGraphDocument::ApplyEdit(const FCreateNode& Edit) {
    const FGraphView View{ReadView()};
    const bool IsGetter{Edit.mAction.starts_with(GetterPrefix)};
    const auto* Type{IsGetter ? nullptr : Engine::FAnimGraphNodeRegistry::Find(Edit.mAction)};
    const Int32 Id{IsGetter ? mGraph->AddVariableGetter(Edit.mAction.substr(GetterPrefix.size()), Edit.mPosition.mX, Edit.mPosition.mY) : mGraph->AddNode(Type->mType, Type->mTitle, Edit.mPosition.mX, Edit.mPosition.mY)};

    if (Id < 0) {
        return Rejected("노드 수가 한도에 도달했습니다.");
    }

    const FNodeId Created{NodeId(Id).Get() | (IsGetter ? GetterBit : 0)};

    if (Edit.mConnectFrom) {
        const FPinRef From{std::get<FPinRef>(*Edit.mConnectFrom)};
        const FPinView* Pin{FindPin(View, From)};

        if (Pin->mType == "Pose") {
            if (Pin->mDirection == EPinDirection::Input) {
                mGraph->Connect(Id, GetPoseNodeId(From.mNode), From.mPin.Get() - PosePinBase);
            } else {
                mGraph->Connect(GetPoseNodeId(From.mNode), Id, 0);
            }
        } else if (IsGetter) {
            auto* Node{mGraph->FindNode(GetPoseNodeId(From.mNode))};
            const auto* Parameter{FindValueInput(Node, From.mPin)};
            Node->SetParameter(Parameter->mName + ".Binding", Edit.mAction.substr(GetterPrefix.size()), Id);
        } else {
            const auto* Getter{mGraph->FindVariableGetter(DecodeId(From.mNode.Get() & ~GetterBit))};
            const auto Parameter{std::ranges::find_if(Type->mParameters, IsBindable)};
            mGraph->FindNode(Id)->SetParameter(Parameter->mName + ".Binding", Getter->mVariable, Getter->mId);
        }
    }

    return {EEditStatus::Applied, {}, Created};
}

FEditResult FAnimPoseGraphDocument::ApplyEdit(const FConnect& Edit) {
    const FPinRef& From{std::get<FPinRef>(Edit.mFrom)};
    const FPinRef& To{std::get<FPinRef>(Edit.mTo)};
    const Int32 Target{GetPoseNodeId(To.mNode)};

    if (To.mPin.Get() < ValuePinBase) {
        return Changed(mGraph->Connect(GetPoseNodeId(From.mNode), Target, To.mPin.Get() - PosePinBase));
    }

    auto* Node{mGraph->FindNode(Target)};
    const auto* Parameter{FindValueInput(Node, To.mPin)};
    const auto* Getter{mGraph->FindVariableGetter(DecodeId(From.mNode.Get() & ~GetterBit))};

    if (Parameter == nullptr || Getter == nullptr) {
        return Rejected("변수 연결을 찾을 수 없습니다.");
    }

    Node->SetParameter(Parameter->mName + ".Binding", Getter->mVariable, Getter->mId);

    return Changed(true);
}

FEditResult FAnimPoseGraphDocument::ApplyEdit(const FMoveNodes& Edit) {
    const FGraphView View{ReadView()};

    bool Moved{};

    for (const FMoveNode& Move : Edit.mMoves) {
        if (FindNode(View, Move.mNode)->mPosition != Move.mPosition) {
            if ((Move.mNode.Get() & GetterBit) != 0) {
                mGraph->SetVariableGetterPosition(DecodeId(Move.mNode.Get() & ~GetterBit), Move.mPosition.mX, Move.mPosition.mY);
            } else {
                mGraph->SetNodePosition(GetPoseNodeId(Move.mNode), Move.mPosition.mX, Move.mPosition.mY);
            }

            Moved = true;
        }
    }

    return Changed(Moved);
}

FEditResult FAnimPoseGraphDocument::ApplyEdit(const FDeleteSelection& Edit) {
    const FGraphView View{ReadView()};

    for (const FSelectionItem& Item : Edit.mItems) {
        if (const auto* Id{std::get_if<FLinkId>(&Item)}) {
            const FPinRef To{std::get<FPinRef>(FindLink(View, *Id)->mTo)};
            const Int32 Target{GetPoseNodeId(To.mNode)};

            if (To.mPin.Get() < ValuePinBase) {
                mGraph->Disconnect(Target, To.mPin.Get() - PosePinBase);
            } else if (auto* Node{mGraph->FindNode(Target)}) {
                Node->SetParameter(FindValueInput(Node, To.mPin)->mName + ".Binding", {});
            }
        }
    }

    for (const FSelectionItem& Item : Edit.mItems) {
        if (const auto* Id{std::get_if<FNodeId>(&Item)}) {
            if ((Id->Get() & GetterBit) != 0) {
                mGraph->RemoveVariableGetter(DecodeId(Id->Get() & ~GetterBit));
            } else {
                mGraph->RemoveNode(GetPoseNodeId(*Id));
            }
        }
    }

    return Changed(!Edit.mItems.empty());
}

FEditResult FAnimPoseGraphDocument::ApplyEdit(const FSetProperty& Edit) {
    const auto* Id{std::get_if<FNodeId>(&Edit.mItem)};
    const auto* View{Id != nullptr ? FindNode(ReadView(), *Id) : nullptr};
    auto* Node{Id != nullptr ? mGraph->FindNode(GetPoseNodeId(*Id)) : nullptr};

    if (View == nullptr || Node == nullptr) {
        return Rejected("편집할 노드가 없습니다.");
    }

    const auto Property{std::ranges::find(View->mProperties, Edit.mKey, &FPropertyView::mKey)};

    if (const auto* Value{std::get_if<std::string>(&Edit.mValue)}) {
        if (Edit.mKey == "Name") {
            Node->mName = *Value;
        } else {
            if (Edit.mKey.ends_with(".Binding") && !Value->empty() && std::ranges::find(mGraph->GetVariableGetters(), *Value, &Asset::FAnimGraphVariableGetter::mVariable) == mGraph->GetVariableGetters().end()) {
                mGraph->AddVariableGetter(*Value, Node->mX - 245.0f, Node->mY - 170.0f);
            }

            Node->SetParameter(Edit.mKey, *Value);
        }
    } else if (const auto* Number{std::get_if<double>(&Edit.mValue)}) {
        const FNumericEditor& Numeric{*Property->mNumeric};
        const double Clamped{Numeric.mSlider ? std::clamp(*Number, Numeric.mMin, Numeric.mMax) : *Number};
        const float Float{static_cast<float>(Clamped)};

        if (!std::isfinite(Float)) {
            return Rejected("숫자가 유효하지 않습니다.");
        }

        Node->SetParameter(Edit.mKey, std::format("{}", Float));
    } else {
        Node->SetParameter(Edit.mKey, std::get<bool>(Edit.mValue) ? "true" : "false");
    }

    return Changed(true);
}

FEditResult FAnimPoseGraphDocument::ApplyEdit(const FDisconnectInputs& Edit) {
    if ((Edit.mNode.Get() & GetterBit) != 0) {
        return Rejected("입력 연결을 끊을 수 없는 노드입니다.");
    }

    const Int32 Id{GetPoseNodeId(Edit.mNode)};
    std::vector<Index_t> Pins{};

    for (const auto& Link : mGraph->GetLinks()) {
        if (Link.mToNode == Id) {
            Pins.push_back(Link.mToPin);
        }
    }

    for (Index_t Pin : Pins) {
        mGraph->Disconnect(Id, Pin);
    }

    auto* Node{mGraph->FindNode(Id)};
    const bool Cleared{Node != nullptr && std::erase_if(Node->mParameters, [](const Asset::FAnimGraphParameter& Parameter) { return Parameter.mName.ends_with(".Binding"); }) > 0};

    return Changed(!Pins.empty() || Cleared);
}

FAnimStateMachineGraphDocument::FAnimStateMachineGraphDocument()
    : TGL::FGraphSchema{static_cast<const TGL::FGraphSnapshot&>(*this)} {
}

void FAnimStateMachineGraphDocument::Refresh(Asset::FAnimStateMachineData& Machine, const Asset::UAnimGraph& Owner) {
    mMachine = &Machine;
    mOwner = &Owner;
    Refresh();
}

void FAnimStateMachineGraphDocument::Refresh() {
    const Asset::FAnimStateMachineData& Machine{*mMachine};
    std::vector<FNodeView> Nodes{};
    std::vector<FLinkView> Links{};
    const ImU32 Tint{Color(Canvas::EConnectionRole::Transition)};
    FNodeView Entry{SpecialNode, {-180.0f, 0.0f}, "Entry", {}, "State Machine"};
    Entry.mMovable = false;
    Entry.mDeletable = false;
    Entry.mTransitionOutput = true;
    Nodes.push_back(std::move(Entry));

    for (const auto& State : Machine.GetStates()) {
        const bool IsEntry{State.mId == Machine.GetEntryState()};
        FNodeView Node{NodeId(State.mId), {State.mX, State.mY}, State.mName, {}, "State"};
        Node.mTransitionInput = true;
        Node.mTransitionOutput = true;
        Node.mColor = Tint;
        Node.mProperties.push_back({"Name", "Name", State.mName, {}, false, false});
        Node.mProperties.push_back({"ResetOnEntry", "Reset on Entry", State.mResetOnEntry});
        Node.mProperties.push_back({"Entry", "Entry State", IsEntry, {}, IsEntry, false});
        Nodes.push_back(std::move(Node));

        if (IsEntry) {
            Links.push_back({{1}, SpecialNode, NodeId(State.mId), {}, {}, Tint, true, false});
        }
    }

    for (const auto& Transition : Machine.GetTransitions()) {
        const auto* From{Machine.FindState(Transition.mFromState)};
        const auto* To{Machine.FindState(Transition.mToState)};

        if (From == nullptr || To == nullptr) {
            continue;
        }

        FLinkView Link{{NodeId(Transition.mId).Get()}, NodeId(From->mId), NodeId(To->mId), std::format("{:.2f} s", Transition.mDuration.GetBase()), {}, Tint, true};
        FPropertyView Duration{"Duration", "Blend Duration", static_cast<double>(Transition.mDuration.GetBase())};
        Duration.mNumeric = FNumericEditor{0.0, 10.0, 0.01f};
        Link.mProperties.push_back(std::move(Duration));
        Link.mProperties.push_back({"Priority", "Priority", static_cast<std::int64_t>(Transition.mPriority)});
        Link.mTooltip = std::format("{} -> {}\nPriority {} (lower values are checked first)", From->mName, To->mName, Transition.mPriority);
        Links.push_back(std::move(Link));
    }

    Replace(std::move(Nodes), std::move(Links));
}

std::vector<FNodeAction> FAnimStateMachineGraphDocument::CollectNodeActions(const FGraphView&, const std::optional<FEndpoint>&, const FPinView*) const {
    return {{"State", "Add State"}};
}

FConnectionResult FAnimStateMachineGraphDocument::CheckConnectionRules(const FGraphView&, const FConnect& Connection) const {
    const auto* From{std::get_if<FNodeId>(&Connection.mFrom)};
    const auto* To{std::get_if<FNodeId>(&Connection.mTo)};

    if (From == nullptr || To == nullptr) {
        return {EConnection::Reject, "Select states to create a transition."};
    }

    if (*From == SpecialNode && mMachine->GetEntryState() >= 0 && mMachine->GetEntryState() != GetStateId(*To)) {
        return {EConnection::Replace, "진입 상태를 변경합니다."};
    }

    return {EConnection::Allow};
}

EInputLinkPolicy FAnimStateMachineGraphDocument::GetInputLinkPolicy(const FEndpoint&) const {
    return EInputLinkPolicy::Multiple;
}

const char* FAnimStateMachineGraphDocument::GetEditUnavailableReason() const {
    return mMachine == nullptr ? "편집할 상태 머신이 없습니다." : nullptr;
}

Int32 FAnimStateMachineGraphDocument::GetStateId(FNodeId Id) {
    return DecodeId(Id.Get());
}

Int32 FAnimStateMachineGraphDocument::GetTransitionId(FLinkId Id) {
    return DecodeId(Id.Get());
}

FEditResult FAnimStateMachineGraphDocument::ApplyEdit(const FCreateNode& Edit) {
    if (Edit.mConnectFrom && GetNodeId(*Edit.mConnectFrom) != SpecialNode && mMachine->GetTransitions().size() >= Asset::UAnimGraph::MaxNodes) {
        return Rejected("전이 수가 한도에 도달했습니다.");
    }

    std::size_t Number{mMachine->GetStates().size()};
    Core::FString Name{};

    do {
        Name = std::format("State {}", Number++);
    } while (std::ranges::any_of(mMachine->GetStates(), [&Name](const Asset::FAnimStateData& State) { return State.mName == Name; }));

    const Int32 Id{mMachine->AddState(Name, Edit.mPosition.mX, Edit.mPosition.mY, *mOwner)};

    if (Id < 0) {
        return Rejected("상태 수가 한도에 도달했습니다.");
    }

    if (Edit.mConnectFrom) {
        const FNodeId From{std::get<FNodeId>(*Edit.mConnectFrom)};

        if (From == SpecialNode) {
            mMachine->SetEntryState(Id);
        } else if ((Edit.mConnectDirection == EPinDirection::Output ? mMachine->AddTransition(GetStateId(From), Id) : mMachine->AddTransition(Id, GetStateId(From))) < 0) {
            mMachine->RemoveState(Id);

            return Rejected("전이를 추가할 수 없습니다.");
        }
    }

    return {EEditStatus::Applied, {}, NodeId(Id)};
}

FEditResult FAnimStateMachineGraphDocument::ApplyEdit(const FConnect& Edit) {
    const FNodeId From{std::get<FNodeId>(Edit.mFrom)};
    const Int32 To{GetStateId(std::get<FNodeId>(Edit.mTo))};

    if (From == SpecialNode) {
        return Changed(mMachine->GetEntryState() != To && mMachine->SetEntryState(To));
    }

    return mMachine->AddTransition(GetStateId(From), To) >= 0 ? Changed(true) : Rejected("전이 수가 한도에 도달했습니다.");
}

FEditResult FAnimStateMachineGraphDocument::ApplyEdit(const FMoveNodes& Edit) {
    bool Moved{};

    for (const FMoveNode& Move : Edit.mMoves) {
        if (auto* State{mMachine->FindState(GetStateId(Move.mNode))}; State != nullptr && FPoint{State->mX, State->mY} != Move.mPosition) {
            State->mX = Move.mPosition.mX;
            State->mY = Move.mPosition.mY;
            Moved = true;
        }
    }

    return Changed(Moved);
}

FEditResult FAnimStateMachineGraphDocument::ApplyEdit(const FDeleteSelection& Edit) {
    for (const FSelectionItem& Item : Edit.mItems) {
        if (const auto* Id{std::get_if<FNodeId>(&Item)}) {
            mMachine->RemoveState(GetStateId(*Id));
        } else {
            mMachine->RemoveTransition(GetTransitionId(std::get<FLinkId>(Item)));
        }
    }

    return Changed(!Edit.mItems.empty());
}

FEditResult FAnimStateMachineGraphDocument::ApplyEdit(const FSetProperty& Edit) {
    if (const auto* Id{std::get_if<FNodeId>(&Edit.mItem)}) {
        auto* State{mMachine->FindState(GetStateId(*Id))};

        if (State != nullptr) {
            const auto* Name{std::get_if<std::string>(&Edit.mValue)};
            const auto* Flag{std::get_if<bool>(&Edit.mValue)};

            if (Edit.mKey == "Name" && Name != nullptr && !Name->empty() && std::ranges::none_of(mMachine->GetStates(), [State, Name](const Asset::FAnimStateData& Other) { return Other.mId != State->mId && Other.mName == *Name; })) {
                State->mName = *Name;

                return Changed(true);
            }

            if (Edit.mKey == "ResetOnEntry" && Flag != nullptr) {
                return Changed(std::exchange(State->mResetOnEntry, *Flag) != *Flag);
            }

            if (Edit.mKey == "Entry" && Flag != nullptr && *Flag) {
                return Changed(mMachine->GetEntryState() != State->mId && mMachine->SetEntryState(State->mId));
            }
        }
    } else if (auto* Transition{mMachine->FindTransition(GetTransitionId(std::get<FLinkId>(Edit.mItem)))}) {
        const auto* Duration{std::get_if<double>(&Edit.mValue)};
        const auto* Priority{std::get_if<std::int64_t>(&Edit.mValue)};

        if (Edit.mKey == "Duration" && Duration != nullptr) {
            const auto Time{Core::FTime::FromBase(static_cast<float>(std::clamp(*Duration, 0.0, 10.0)))};

            return Changed(std::exchange(Transition->mDuration, Time).GetBase() != Time.GetBase());
        }

        if (Edit.mKey == "Priority" && Priority != nullptr && std::in_range<Int32>(*Priority)) {
            return Changed(std::exchange(Transition->mPriority, static_cast<Int32>(*Priority)) != *Priority);
        }
    }

    return Rejected("이름이나 속성 값이 유효하지 않습니다.");
}

FEditResult FAnimStateMachineGraphDocument::ApplyEdit(const FDisconnectInputs& Edit) {
    if (mMachine->FindState(GetStateId(Edit.mNode)) == nullptr) {
        return Rejected("입력 전이를 끊을 수 없는 노드입니다.");
    }

    return ApplyEdit(GetDeletableInputLinks(ReadView(), Edit.mNode));
}
