/**
 * @file
 * @brief 그래프 조회, 문서 스냅샷, 경로 탐색 및 공용 스키마를 구현합니다.
 * @ingroup TGL
 */

#include "TGL/Graph.h"

#include <unordered_set>

using namespace TGL;

std::vector<FDiagnostic> IGraphSchema::ValidateDocument(const FGraphView&) const {
    return {};
}

FGraphView FGraphSnapshot::ReadView() const {
    return {mNodes, mLinks};
}

void FGraphSnapshot::Replace(std::vector<FNodeView> Nodes, std::vector<FLinkView> Links) {
    mNodes = std::move(Nodes);
    mLinks = std::move(Links);
}

namespace {

    FEditResult Reject(const char* Message) {
        return {EEditStatus::Rejected, Message};
    }

    const FPinView* ResolvePin(const FGraphView& View, const FEndpoint& Endpoint) {
        const auto* Ref{std::get_if<FPinRef>(&Endpoint)};

        return Ref != nullptr ? FindPin(View, *Ref) : nullptr;
    }

    bool IsNodeEndpoint(const FGraphView& View, const FEndpoint& Endpoint, EPinDirection Direction) {
        const auto* Id{std::get_if<FNodeId>(&Endpoint)};
        const FNodeView* Node{Id != nullptr ? FindNode(View, *Id) : nullptr};

        return Node != nullptr && (Direction == EPinDirection::Input ? Node->mTransitionInput : Node->mTransitionOutput);
    }

    std::span<const FPropertyView> GetProperties(const FGraphView& View, const FSelectionItem& Item) {
        if (const auto* Id{std::get_if<FNodeId>(&Item)}) {
            if (const FNodeView* Node{FindNode(View, *Id)}) {
                return Node->mProperties;
            }
        } else if (const FLinkView* Link{FindLink(View, std::get<FLinkId>(Item))}) {
            return Link->mProperties;
        }

        return {};
    }

}

FGraphSchema::FGraphSchema(const IGraphDocument& Document, bool EditTopology)
    : mDocument{Document},
      mEditTopology{EditTopology} {
}

std::vector<FNodeAction> FGraphSchema::ListNodeActions(const FGraphView& View, const std::optional<FEndpoint>& From) const {
    if (!mEditTopology || GetEditUnavailableReason() != nullptr) {
        return {};
    }

    const FPinView* Pin{From ? ResolvePin(View, *From) : nullptr};

    if (From && Pin == nullptr && !IsNodeEndpoint(View, *From, EPinDirection::Input) && !IsNodeEndpoint(View, *From, EPinDirection::Output)) {
        return {};
    }

    return CollectNodeActions(View, From, Pin);
}

FConnectionResult FGraphSchema::QueryConnection(const FGraphView& View, const FConnect& Connection) const {
    if (!mEditTopology) {
        return {EConnection::Reject, "This graph is read-only."};
    }

    if (const char* Reason{GetEditUnavailableReason()}) {
        return {EConnection::Reject, Reason};
    }

    const FPinView* Output{ResolvePin(View, Connection.mFrom)};
    const FPinView* Input{ResolvePin(View, Connection.mTo)};
    const bool ValidOutput{Output != nullptr ? Output->mDirection == EPinDirection::Output : IsNodeEndpoint(View, Connection.mFrom, EPinDirection::Output)};
    const bool ValidInput{Input != nullptr ? Input->mDirection == EPinDirection::Input : IsNodeEndpoint(View, Connection.mTo, EPinDirection::Input)};

    if (!ValidOutput || !ValidInput) {
        return {EConnection::Reject, "Connect an existing output to an existing input."};
    }

    if (Output != nullptr && Input != nullptr && !ArePinTypesCompatible(*Output, *Input)) {
        return {EConnection::Reject, "The input and output must have compatible types."};
    }

    FConnectionResult Result{CheckConnectionRules(View, Connection)};
    const EInputLinkPolicy Policy{GetInputLinkPolicy(Connection.mTo)};

    if (Result.mDecision != EConnection::Allow || Policy == EInputLinkPolicy::Multiple) {
        return Result;
    }

    const auto Existing{std::ranges::find(View.mLinks, Connection.mTo, &FLinkView::mTo)};

    if (Existing == View.mLinks.end()) {
        return Result;
    }

    if (Existing->mFrom != Connection.mFrom) {
        return {EConnection::Replace, "Replace the existing connection."};
    }

    return Policy == EInputLinkPolicy::SingleRejectDuplicate ? FConnectionResult{EConnection::Reject, "These pins are already connected."} : Result;
}

FEditResult FGraphSchema::Apply(const FEdit& Edit) {
    if (const char* Reason{GetEditUnavailableReason()}) {
        return Reject(Reason);
    }

    if (!mEditTopology && !std::holds_alternative<FSetProperty>(Edit)) {
        return Reject("This graph is read-only.");
    }

    const FGraphView View{mDocument.ReadView()};
    const auto Checked{std::visit([this, &View](const auto& Request) { return CheckEdit(View, Request); }, Edit)};

    if (Checked) {
        return *Checked;
    }

    FEditResult Result{ApplyValidatedEdit(Edit)};

    if (Result.mStatus == EEditStatus::Applied) {
        Refresh();
    }

    return Result;
}

const char* FGraphSchema::GetEditUnavailableReason() const {
    return nullptr;
}

std::vector<FNodeAction> FGraphSchema::CollectNodeActions(const FGraphView&, const std::optional<FEndpoint>&, const FPinView*) const {
    return {};
}

bool FGraphSchema::ArePinTypesCompatible(const FPinView& Output, const FPinView& Input) const {
    return Output.mType == Input.mType;
}

EInputLinkPolicy FGraphSchema::GetInputLinkPolicy(const FEndpoint&) const {
    return EInputLinkPolicy::Single;
}

FConnectionResult FGraphSchema::CheckConnectionRules(const FGraphView&, const FConnect&) const {
    return {EConnection::Allow};
}

FEditResult FGraphSchema::ApplyValidatedEdit(const FEdit& Edit) {
    return std::visit([this](const auto& Request) { return ApplyEdit(Request); }, Edit);
}

FEditResult FGraphSchema::ApplyEdit(const FCreateNode&) {
    return Reject("Node creation is unavailable.");
}

FEditResult FGraphSchema::ApplyEdit(const FConnect&) {
    return Reject("Connections cannot be edited.");
}

FEditResult FGraphSchema::ApplyEdit(const FMoveNodes&) {
    return Reject("Nodes cannot be moved.");
}

FEditResult FGraphSchema::ApplyEdit(const FDeleteSelection&) {
    return Reject("Items cannot be deleted.");
}

FEditResult FGraphSchema::ApplyEdit(const FSetProperty&) {
    return Reject("Properties cannot be edited.");
}

FEditResult FGraphSchema::ApplyEdit(const FDisconnectInputs& Edit) {
    FDeleteSelection Inputs{GetDeletableInputLinks(mDocument.ReadView(), Edit.mNode)};

    return Inputs.mItems.empty() ? FEditResult{} : ApplyValidatedEdit(Inputs);
}

void FGraphSchema::Refresh() {
}

std::optional<FEditResult> FGraphSchema::CheckEdit(const FGraphView& View, const FCreateNode& Edit) const {
    if (!std::isfinite(Edit.mPosition.mX) || !std::isfinite(Edit.mPosition.mY)) {
        return Reject("The node position is invalid.");
    }

    if (Edit.mConnectFrom && std::holds_alternative<FNodeId>(*Edit.mConnectFrom) && !IsNodeEndpoint(View, *Edit.mConnectFrom, Edit.mConnectDirection)) {
        return Reject("The source endpoint is invalid.");
    }

    const auto Actions{ListNodeActions(View, Edit.mConnectFrom)};

    if (std::ranges::find(Actions, Edit.mAction, &FNodeAction::mKey) == Actions.end()) {
        return Reject("This node cannot be created here.");
    }

    return std::nullopt;
}

std::optional<FEditResult> FGraphSchema::CheckEdit(const FGraphView& View, const FConnect& Edit) const {
    FConnectionResult Verdict{QueryConnection(View, Edit)};

    if (Verdict.mDecision == EConnection::Reject) {
        return FEditResult{EEditStatus::Rejected, std::move(Verdict.mMessage)};
    }

    if (Verdict.mDecision == EConnection::Allow && GetInputLinkPolicy(Edit.mTo) == EInputLinkPolicy::Single && std::ranges::any_of(View.mLinks, [&Edit](const FLinkView& Link) { return Link.mTo == Edit.mTo && Link.mFrom == Edit.mFrom; })) {
        return FEditResult{};
    }

    return std::nullopt;
}

std::optional<FEditResult> FGraphSchema::CheckEdit(const FGraphView& View, const FMoveNodes& Edit) const {
    for (const FMoveNode& Move : Edit.mMoves) {
        const FNodeView* Node{FindNode(View, Move.mNode)};

        if (Node == nullptr || !Node->mMovable || !std::isfinite(Move.mPosition.mX) || !std::isfinite(Move.mPosition.mY)) {
            return Reject("The node cannot be moved to this position.");
        }
    }

    return Edit.mMoves.empty() ? std::optional<FEditResult>{FEditResult{}} : std::nullopt;
}

std::optional<FEditResult> FGraphSchema::CheckEdit(const FGraphView& View, const FDeleteSelection& Edit) const {
    for (const FSelectionItem& Item : Edit.mItems) {
        if (const auto* NodeId{std::get_if<FNodeId>(&Item)}) {
            const FNodeView* Node{FindNode(View, *NodeId)};

            if (Node == nullptr || !Node->mDeletable) {
                return Reject("This node cannot be deleted.");
            }
        } else {
            const FLinkView* Link{FindLink(View, std::get<FLinkId>(Item))};

            if (Link == nullptr || !Link->mDeletable) {
                return Reject("This link cannot be deleted.");
            }
        }
    }

    return Edit.mItems.empty() ? std::optional<FEditResult>{FEditResult{}} : std::nullopt;
}

std::optional<FEditResult> FGraphSchema::CheckEdit(const FGraphView& View, const FSetProperty& Edit) const {
    const auto Properties{GetProperties(View, Edit.mItem)};
    const auto Property{std::ranges::find(Properties, Edit.mKey, &FPropertyView::mKey)};

    if (Property == Properties.end() || Property->mValue.index() != Edit.mValue.index()) {
        return Reject("The property or value type is invalid.");
    }

    if (Property->mValue == Edit.mValue) {
        return FEditResult{};
    }

    if (Property->mReadOnly) {
        return Reject("This property is read-only.");
    }

    if (const auto* Value{std::get_if<double>(&Edit.mValue)}; Value != nullptr && !std::isfinite(*Value)) {
        return Reject("The value must be finite.");
    }

    const auto* Option{std::get_if<std::string>(&Edit.mValue)};

    if (!Property->mOptions.empty() && (Option == nullptr || std::ranges::find(Property->mOptions, *Option) == Property->mOptions.end())) {
        return Reject("This option is unavailable.");
    }

    return std::nullopt;
}

std::optional<FEditResult> FGraphSchema::CheckEdit(const FGraphView& View, const FDisconnectInputs& Edit) const {
    if (FindNode(View, Edit.mNode) == nullptr) {
        return Reject("The node does not exist.");
    }

    return std::nullopt;
}

FReadOnlyGraphSchema::FReadOnlyGraphSchema(const IGraphDocument& Document)
    : TGL::FGraphSchema{Document, false} {
}

FDeleteSelection TGL::GetDeletableInputLinks(const FGraphView& View, FNodeId Node) {
    FDeleteSelection Inputs{};

    for (const FLinkView& Link : View.mLinks) {
        if (Link.mDeletable && GetNodeId(Link.mTo) == Node) {
            Inputs.mItems.emplace_back(Link.mId);
        }
    }

    return Inputs;
}

FNodeId TGL::GetNodeId(const FEndpoint& Endpoint) {
    if (const auto* Pin{std::get_if<FPinRef>(&Endpoint)}) {
        return Pin->mNode;
    }

    return std::get<FNodeId>(Endpoint);
}

const FNodeView* TGL::FindNode(const FGraphView& Graph, FNodeId Id) {
    const auto Found{std::ranges::find(Graph.mNodes, Id, &FNodeView::mId)};

    return Found == Graph.mNodes.end() ? nullptr : &*Found;
}

const FPinView* TGL::FindPin(const FGraphView& Graph, const FPinRef& Ref) {
    const FNodeView* Node{FindNode(Graph, Ref.mNode)};

    if (Node == nullptr) {
        return nullptr;
    }

    const auto Found{std::ranges::find(Node->mPins, Ref.mPin, &FPinView::mId)};

    return Found == Node->mPins.end() ? nullptr : &*Found;
}

const FLinkView* TGL::FindLink(const FGraphView& Graph, FLinkId Id) {
    const auto Found{std::ranges::find(Graph.mLinks, Id, &FLinkView::mId)};

    return Found == Graph.mLinks.end() ? nullptr : &*Found;
}

bool TGL::HasPath(const FGraphView& Graph, FNodeId From, FNodeId To) {
    if (FindNode(Graph, From) == nullptr || FindNode(Graph, To) == nullptr) {
        return false;
    }

    std::unordered_map<std::uintptr_t, std::vector<FNodeId>> Outgoing{};

    for (const FLinkView& Link : Graph.mLinks) {
        Outgoing[GetNodeId(Link.mFrom).Get()].push_back(GetNodeId(Link.mTo));
    }

    std::vector<FNodeId> Pending{From};
    std::unordered_set<std::uintptr_t> Visited{};

    while (!Pending.empty()) {
        const FNodeId Node{Pending.back()};
        Pending.pop_back();

        if (Node == To) {
            return true;
        }

        if (!Visited.insert(Node.Get()).second) {
            continue;
        }

        const auto Links{Outgoing.find(Node.Get())};

        if (Links != Outgoing.end()) {
            Pending.insert(Pending.end(), Links->second.begin(), Links->second.end());
        }
    }

    return false;
}

std::optional<std::vector<FNodeId>> TGL::TopologicalOrder(const FGraphView& Graph) {
    std::unordered_map<std::uintptr_t, std::size_t> Incoming{};
    std::unordered_multimap<std::uintptr_t, FNodeId> Outgoing{};
    std::vector<FNodeId> Order{};

    for (const FNodeView& Node : Graph.mNodes) {
        if (!Node.mId || !Incoming.emplace(Node.mId.Get(), 0).second) {
            return std::nullopt;
        }
    }

    for (const FLinkView& Link : Graph.mLinks) {
        const FNodeId From{GetNodeId(Link.mFrom)};
        const FNodeId To{GetNodeId(Link.mTo)};
        const auto Found{Incoming.find(To.Get())};

        if (!Incoming.contains(From.Get()) || Found == Incoming.end()) {
            return std::nullopt;
        }

        ++Found->second;
        Outgoing.emplace(From.Get(), To);
    }

    for (const FNodeView& Node : Graph.mNodes) {
        if (Incoming.at(Node.mId.Get()) == 0) {
            Order.push_back(Node.mId);
        }
    }

    for (std::size_t Index{}; Index < Order.size(); ++Index) {
        const auto Range{Outgoing.equal_range(Order[Index].Get())};

        for (auto Link{Range.first}; Link != Range.second; ++Link) {
            if (--Incoming.at(Link->second.Get()) == 0) {
                Order.push_back(Link->second);
            }
        }
    }

    if (Order.size() != Graph.mNodes.size()) {
        return std::nullopt;
    }

    return Order;
}
