/**
 * @file
 * @brief 그래프 뷰를 공용 캔버스에 표시하고 편집 제스처를 도메인 요청으로 전달합니다.
 * @ingroup TGL
 */

#include "TGL/GraphEditor.h"
#include "TGL/NodeBuilder.h"

#include <cctype>
#include <cstdio>

using namespace TGL;

namespace {

    /**
     * @brief 현재 프레임에 그릴 핀의 레이블, 색상 및 연결 상태입니다.
     */
    struct FPinPresentation {
        FEndpoint mEndpoint{}; ///< 핀에 대응하는 문서 끝점입니다.
        std::string mLabel{}; ///< 이름과 필요 시 기본값을 포함한 표시 문자열입니다.
        ImVec4 mColor{}; ///< 핀 아이콘 색입니다.
        bool mConnected{}; ///< 현재 뷰에서 사용 중인 핀인지 나타냅니다.
    };

    /**
     * @brief 압축 색상이나 공용 기본 색상에서 표시용 색을 구합니다.
     * @param Color 압축된 색이며 0이면 기본값을 사용합니다.
     * @return RGBA 색입니다.
     */
    ImVec4 PinColor(ImU32 Color) {
        return Color != 0 ? ImGui::ColorConvertU32ToFloat4(Color) : Canvas::ConnectionColor(Canvas::EConnectionRole::Default);
    }

    /**
     * @brief 팔레트 표시 문자열에 검색어가 있는지 비교합니다.
     * @param Text 검색 대상 문자열입니다.
     * @param Search 찾을 문자열입니다.
     * @return 검색어가 비었거나 대상에 포함되면 true입니다.
     */
    bool Matches(std::string_view Text, std::string_view Search) {
        const auto Equal{[](unsigned char Left, unsigned char Right) {
            return std::tolower(Left) == std::tolower(Right);
        }};

        return Search.empty() || std::search(Text.begin(), Text.end(), Search.begin(), Search.end(), Equal) != Text.end();
    }

    /**
     * @brief 속성 타입에 맞는 공용 ImGui 입력을 표시합니다.
     * @param Property 속성의 표시 정보와 편집 설정입니다.
     * @param[in,out] Value 입력에 의해 바뀌는 값의 복사본입니다.
     * @return 입력으로 값이 바뀌었으면 true입니다.
     */
    bool EditProperty(const FPropertyView& Property, FPropertyValue& Value) {
        if (!Property.mOptions.empty()) {
            auto* Choice{std::get_if<std::string>(&Value)};

            if (Choice == nullptr || !ImGui::BeginCombo("##Value", Choice->empty() ? "(값)" : Choice->c_str())) {
                return false;
            }

            bool Changed{};

            for (const std::string& Option : Property.mOptions) {
                if (ImGui::Selectable(Option.empty() ? "(값)" : Option.c_str(), Option == *Choice) && Option != *Choice) {
                    *Choice = Option;
                    Changed = true;
                }
            }

            ImGui::EndCombo();

            return Changed;
        }

        if (auto* Bool{std::get_if<bool>(&Value)}) {
            return ImGui::Checkbox("##Value", Bool);
        }

        if (auto* Integer{std::get_if<std::int64_t>(&Value)}) {
            return ImGui::InputScalar("##Value", ImGuiDataType_S64, Integer);
        }

        if (auto* Real{std::get_if<double>(&Value)}) {
            if (Property.mNumeric) {
                const FNumericEditor& Numeric{*Property.mNumeric};

                if (Numeric.mSlider) {
                    return ImGui::SliderScalar("##Value", ImGuiDataType_Double, Real, &Numeric.mMin, &Numeric.mMax, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                }

                const bool Bounded{Numeric.mMin < Numeric.mMax};

                return ImGui::DragScalar("##Value", ImGuiDataType_Double, Real, Numeric.mStep, Bounded ? &Numeric.mMin : nullptr, Bounded ? &Numeric.mMax : nullptr, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            }

            return ImGui::InputDouble("##Value", Real);
        }

        std::string& Text{std::get<std::string>(Value)};
        std::vector<char> Buffer{};
        Buffer.resize(Text.size() + 256);
        std::ranges::copy(Text, Buffer.begin());

        if (!ImGui::InputText("##Value", Buffer.data(), Buffer.size())) {
            return false;
        }

        Text = Buffer.data();

        return true;
    }

    /**
     * @brief 속성값을 노드나 핀에서 읽을 수 있는 문자열로 만듭니다.
     * @param Value 표시할 속성값입니다.
     * @return 값의 표시 문자열입니다.
     */
    std::string DisplayValue(const FPropertyValue& Value) {
        if (const auto* Bool{std::get_if<bool>(&Value)}) {
            return *Bool ? "True" : "False";
        }

        if (const auto* Integer{std::get_if<std::int64_t>(&Value)}) {
            return std::to_string(*Integer);
        }

        if (const auto* Real{std::get_if<double>(&Value)}) {
            char Text[32]{};

            std::snprintf(Text, sizeof(Text), "%.4g", *Real);

            return Text;
        }

        return std::get<std::string>(Value);
    }

}

void FNodePalette::Reset() {
    mSearch.fill('\0');
    mFocus = true;
}

std::optional<std::size_t> FNodePalette::Draw(std::span<const FNodeAction> Actions) {
    if (mFocus) {
        ImGui::SetKeyboardFocusHere();
        mFocus = false;
    }

    ImGui::SetNextItemWidth(std::max(220.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##Search", "Search nodes", mSearch.data(), mSearch.size());

    const std::string_view Search{mSearch.data()};

    for (std::size_t Index{}; Index < Actions.size(); ++Index) {
        const FNodeAction& Action{Actions[Index]};

        if (Matches(Action.mName, Search) || Matches(Action.mCategory, Search)) {
            const std::string Label{Action.mCategory.empty() ? Action.mName : Action.mCategory + " / " + Action.mName};

            ImGui::PushID(Action.mKey.c_str());
            const bool Picked{ImGui::Selectable(Label.c_str())};
            ImGui::PopID();

            if (Picked) {
                return Index;
            }
        }
    }

    return std::nullopt;
}

FGraphEditor::FGraphEditor()
    : mContext{Canvas::CreateEditor()} {
}

FGraphEditor::~FGraphEditor() {
    Canvas::DestroyEditor(mContext);
}

FGraphEvents FGraphEditor::Draw(const char* Id, const IGraphDocument& Document, const IGraphSchema& Schema, IGraphEditTarget* Target, ImVec2 Size, const FGraphOverlay& Overlay, bool EditTopology) {
    if (mDocument != &Document) {
        if (mDocument != nullptr) {
            mPins.clear();
            mEndpoints.clear();
            mNextPin = 1;
            mSelection.clear();
            mPendingSelection.reset();
            Canvas::DestroyEditor(mContext);
            mContext = Canvas::CreateEditor();
        }

        mDocument = &Document;
        Fit();
    }

    Canvas::EditorContext* Previous{Canvas::GetCurrentEditor()};
    Canvas::SetCurrentEditor(mContext);
    const FGraphView View{Document.ReadView()};
    mDiagnostics = Schema.ValidateDocument(View);
    FGraphEvents Result{};

    const bool CanEditTopology{Target != nullptr && EditTopology};

    if (!Canvas::Begin(Id, Size, !CanEditTopology)) {
        Canvas::SetCurrentEditor(Previous);

        return Result;
    }

    Result.mVisible = true;
    std::optional<FEdit> Edit{};

    std::set<FPinKey> Connected{};

    for (const FLinkView& Link : View.mLinks) {
        Connected.emplace(Link.mFrom, EPinDirection::Output);
        Connected.emplace(Link.mTo, EPinDirection::Input);

        if (Link.mArrow) {
            Canvas::DeclareTransition(Link.mId, GetPin(Link.mFrom, EPinDirection::Output), GetPin(Link.mTo, EPinDirection::Input), GetNodeId(Link.mFrom) == GetNodeId(Link.mTo));
        }
    }

    for (const FNodeView& Node : View.mNodes) {
        if (mResetLayout) {
            Canvas::SetNodePosition(Node.mId, {Node.mPosition.mX, Node.mPosition.mY});
        }

        Canvas::SyncNodePosition(Node.mId, {Node.mPosition.mX, Node.mPosition.mY});
        DrawNode(Connected, Node, Overlay, Target != nullptr ? &Edit : nullptr);
    }

    mResetLayout = false;

    for (const FLinkView& Link : View.mLinks) {
        Canvas::Link(Link.mId, GetPin(Link.mFrom, EPinDirection::Output), GetPin(Link.mTo, EPinDirection::Input), PinColor(Link.mColor), Link.mWidth > 0.0f ? Link.mWidth : Canvas::LinkWidth, Link.mLabel);

        if (std::ranges::find(Overlay.mActiveLinks, Link.mId) != Overlay.mActiveLinks.end()) {
            Canvas::Flow(Link.mId);
        }
    }

    if (mPendingSelection) {
        Canvas::ClearSelection();

        for (const FSelectionItem& Item : *mPendingSelection) {
            if (const auto* Node{std::get_if<FNodeId>(&Item)}) {
                Canvas::SelectNode(*Node, true);
            } else {
                Canvas::SelectLink(std::get<FLinkId>(Item), true);
            }
        }

        mPendingSelection.reset();
    }

    if (CanEditTopology) {
        if (auto TopologyEdit{DrawEditControls(View, Schema)}) {
            Edit = std::move(TopologyEdit);
        }
    }

    if (const FLinkView* Hovered{FindLink(View, Canvas::GetHoveredLink())}; Hovered != nullptr && !Hovered->mTooltip.empty()) {
        Canvas::Suspend();
        ImGui::SetTooltip("%s", Hovered->mTooltip.c_str());
        Canvas::Resume();
    }

    Result.mBackgroundClicked = Canvas::IsBackgroundClicked();

    if (const FNodeId Node{Canvas::GetDoubleClickedNode()}) {
        Result.mActivated = Node;
    } else if (const FLinkId Link{Canvas::GetDoubleClickedLink()}) {
        Result.mActivated = Link;
    }

    const ImVec2 Pointer{Canvas::GetPointerPosition()};
    Result.mPointerPosition = {Pointer.x, Pointer.y};
    FNodeId ContextNode{};
    FLinkId ContextLink{};

    if (CanEditTopology && Canvas::ShowNodeContextMenu(&ContextNode)) {
        Canvas::SelectNode(ContextNode);
        Result.mContextItem = ContextNode;
    } else if (CanEditTopology && Canvas::ShowLinkContextMenu(&ContextLink)) {
        Canvas::SelectLink(ContextLink);
        Result.mContextItem = ContextLink;
    }

    Canvas::End();
    const auto Selection{Canvas::GetSelection()};

    if (!std::ranges::equal(Selection, mSelection)) {
        mSelection.assign(Selection.begin(), Selection.end());
        Result.mSelectionChanged = true;
    }

    const auto Moves{Canvas::GetMovedNodes()};

    if (!Edit && CanEditTopology && !Moves.empty()) {
        Edit = FMoveNodes{{Moves.begin(), Moves.end()}};
    }

    if (Edit) {
        Result.mEdit = SubmitEdit(*Target, *Edit);
    }

    if (!Moves.empty() && (!Result.mEdit || Result.mEdit->mStatus != EEditStatus::Applied || !std::holds_alternative<FMoveNodes>(*Edit))) {
        const FGraphView CurrentView{Document.ReadView()};

        for (const FMoveNode& Move : Moves) {
            if (const FNodeView* Node{FindNode(CurrentView, Move.mNode)}) {
                Canvas::SetNodePosition(Node->mId, {Node->mPosition.mX, Node->mPosition.mY});
            }
        }
    }

    Canvas::SetCurrentEditor(Previous);

    return Result;
}

FEditResult FGraphEditor::DrawDetails(const IGraphDocument& Document, IGraphEditTarget* Target, const FPropertyEditor& CustomEditor) {
    if (mDocument != &Document || mSelection.size() != 1) {
        return {};
    }

    const FGraphView View{Document.ReadView()};
    const FSelectionItem& Item{mSelection.front()};
    std::span<const FPropertyView> Properties{};

    if (const auto* Id{std::get_if<FNodeId>(&Item)}) {
        if (const FNodeView* Node{FindNode(View, *Id)}) {
            Properties = Node->mProperties;
        }
    } else if (const FLinkView* Link{FindLink(View, std::get<FLinkId>(Item))}) {
        Properties = Link->mProperties;
    }

    for (const FDiagnostic& Diagnostic : mDiagnostics) {
        if (Diagnostic.mItem == Item) {
            ImGui::TextColored({1.0f, 0.57f, 0.4f, 1.0f}, "%s", Diagnostic.mMessage.c_str());
        }
    }

    std::optional<FSetProperty> Change{};
    ImGui::PushID(std::holds_alternative<FNodeId>(Item) ? "NodeDetails" : "LinkDetails");
    ImGui::PushID(std::visit([](const auto& Id) { return reinterpret_cast<const void*>(Id.Get()); }, Item));

    for (const FPropertyView& Property : Properties) {
        ImGui::PushID(Property.mKey.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Property.mName.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::BeginDisabled(Target == nullptr || Property.mReadOnly);

        FPropertyValue Value{Property.mValue};

        const bool Changed{!Property.mCustomEditor.empty() && CustomEditor ? CustomEditor(Property, Value) : EditProperty(Property, Value)};

        if (Target != nullptr && !Property.mReadOnly && Changed) {
            Change = FSetProperty{Item, Property.mKey, std::move(Value)};
        }

        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::PopID();
    ImGui::PopID();

    return Change ? SubmitEdit(*Target, *Change) : FEditResult{};
}

FEditResult FGraphEditor::SubmitEdit(IGraphEditTarget& Target, const FEdit& Edit) {
    FEditResult Result{Target.Apply(Edit)};

    if (Result.mStatus == EEditStatus::Applied && Result.mCreatedNode) {
        const FSelectionItem Created[]{Result.mCreatedNode};
        SetSelection(Created);
    }

    return Result;
}

void FGraphEditor::Fit() {
    Canvas::EditorContext* Previous{Canvas::GetCurrentEditor()};

    Canvas::SetCurrentEditor(mContext);
    Canvas::NavigateToContent();
    Canvas::SetCurrentEditor(Previous);
}

void FGraphEditor::ResetLayout() {
    mResetLayout = true;
    Fit();
}

std::span<const FSelectionItem> FGraphEditor::GetSelection() const {
    return mSelection;
}

void FGraphEditor::SetSelection(std::span<const FSelectionItem> Selection) {
    std::vector<FSelectionItem> Items{Selection.begin(), Selection.end()};
    mSelection = Items;
    mPendingSelection = std::move(Items);
}

FPinId FGraphEditor::GetPin(const FEndpoint& Endpoint, EPinDirection Direction) {
    const auto [Found, Inserted]{mPins.try_emplace(FPinKey{Endpoint, Direction}, FPinId{mNextPin})};

    if (Inserted) {
        mEndpoints.emplace(mNextPin++, Found->first);
    }

    return Found->second;
}

void FGraphEditor::DrawNode(const std::set<FPinKey>& ConnectedPins, const FNodeView& Node, const FGraphOverlay& Overlay, std::optional<FEdit>* InlineEdit) {
    const auto IsConnected{[&ConnectedPins](const FEndpoint& Endpoint, EPinDirection Direction) {
        return ConnectedPins.contains(FPinKey{Endpoint, Direction});
    }};
    std::vector<FPinPresentation> Inputs{};
    std::vector<FPinPresentation> Outputs{};

    for (const FPinView& Pin : Node.mPins) {
        const FEndpoint Endpoint{FPinRef{Node.mId, Pin.mId}};
        const bool Input{Pin.mDirection == EPinDirection::Input};
        const bool Connected{IsConnected(Endpoint, Pin.mDirection)};
        std::string Label{Pin.mName};

        if (Input && !Connected && Pin.mDefaultValue) {
            Label += "  " + DisplayValue(*Pin.mDefaultValue);
        }

        (Input ? Inputs : Outputs).push_back({Endpoint, std::move(Label), PinColor(Pin.mColor), Connected});
    }

    const ImVec4 TransitionColor{Canvas::ConnectionColor(Canvas::EConnectionRole::Transition)};

    if (Node.mTransitionInput) {
        Inputs.push_back({Node.mId, "In", TransitionColor, IsConnected(Node.mId, EPinDirection::Input)});
    }

    if (Node.mTransitionOutput) {
        Outputs.push_back({Node.mId, "Out", TransitionColor, IsConnected(Node.mId, EPinDirection::Output)});
    }

    FNodeBuilder Builder{};
    Builder.FitHeader(Node.mTitle.c_str(), Node.mType.c_str());
    const std::size_t Rows{std::max(Inputs.size(), Outputs.size())};

    for (std::size_t Row{}; Row < Rows; ++Row) {
        const char* Input{Row < Inputs.size() ? Inputs[Row].mLabel.c_str() : nullptr};
        const char* Output{Row < Outputs.size() ? Outputs[Row].mLabel.c_str() : nullptr};
        Builder.FitPins(Input, Output);
    }

    std::vector<std::string> Values{};
    Values.reserve(Node.mProperties.size());

    for (const FPropertyView& Property : Node.mProperties) {
        const std::string& Value{Values.emplace_back(Property.mDisplayValue.value_or(DisplayValue(Property.mValue)))};

        if (Property.mShowInNode) {
            Builder.FitProperty(Property.mName.c_str(), Property.mInlineEditable ? "000.0000      " : Value.c_str());
        }
    }

    if (!Node.mSummary.empty()) {
        Builder.FitText(Node.mSummary.c_str());
    }

    std::vector<const std::string*> Messages{};

    for (const FDiagnostic& Diagnostic : mDiagnostics) {
        if (Diagnostic.mItem == FSelectionItem{Node.mId}) {
            Messages.push_back(&Diagnostic.mMessage);
            Builder.FitText(Diagnostic.mMessage.c_str());
        }
    }

    const Canvas::Style& Style{Canvas::GetStyle()};
    const bool Active{std::ranges::find(Overlay.mActiveNodes, Node.mId) != Overlay.mActiveNodes.end()};

    const bool Highlight{Node.mBorderColor != 0 || Active};

    if (Highlight) {
        Canvas::PushStyleColor(Canvas::NodeBorder, Node.mBorderColor != 0 ? PinColor(Node.mBorderColor) : Style.mActiveColor);
    }

    const ImVec4 Accent{ImGui::ColorConvertU32ToFloat4(Node.mColor)};
    Builder.Begin(Node.mId, 160.0f, Node.mMovable);
    Builder.Header(Node.mTitle.c_str(), Node.mType.c_str(), Node.mColor != 0 ? &Accent : nullptr);

    for (std::size_t Row{}; Row < Rows; ++Row) {
        if (Row < Inputs.size()) {
            const FPinPresentation& Pin{Inputs[Row]};
            Builder.InputPin(GetPin(Pin.mEndpoint, EPinDirection::Input), Pin.mLabel.c_str(), Pin.mConnected, Pin.mColor);
        }

        if (Row < Outputs.size()) {
            const FPinPresentation& Pin{Outputs[Row]};
            Builder.OutputPin(GetPin(Pin.mEndpoint, EPinDirection::Output), Pin.mLabel.c_str(), Pin.mConnected, Pin.mColor, Row < Inputs.size());
        }
    }

    const bool HasProperties{std::ranges::any_of(Node.mProperties, &FPropertyView::mShowInNode)};

    if (HasProperties || !Node.mSummary.empty() || !Messages.empty() || Node.mProgress) {
        Builder.BeginBody();

        for (std::size_t Index{}; Index < Values.size(); ++Index) {
            const FPropertyView& Property{Node.mProperties[Index]};

            if (!Property.mShowInNode) {
                continue;
            }

            if (InlineEdit != nullptr && Property.mInlineEditable && !Property.mReadOnly) {
                FPropertyValue Value{Property.mValue};
                ImGui::PushID(Property.mKey.c_str());
                Builder.BeginPropertyInput(Property.mName.c_str());

                if (EditProperty(Property, Value)) {
                    *InlineEdit = FSetProperty{Node.mId, Property.mKey, std::move(Value)};
                }

                ImGui::PopID();
            } else {
                Builder.DrawProperty(Property.mName.c_str(), Values[Index].c_str());
            }
        }

        if (!Node.mSummary.empty()) {
            Builder.DrawText(Node.mSummary.c_str(), ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        if (Node.mProgress) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Style.mActiveColor);
            ImGui::ProgressBar(*Node.mProgress, {Builder.GetWidth(), 4.0f}, "");
            ImGui::PopStyleColor();
        }

        for (const std::string* Message : Messages) {
            Builder.DrawText(Message->c_str(), Style.mErrorColor);
        }
    }

    Builder.End();

    if (Highlight) {
        Canvas::PopStyleColor();
    }
}

void FGraphEditor::OpenPalette(ImVec2 Position, std::optional<FPinKey> Source) {
    mPalettePosition = {Position.x, Position.y};
    mPaletteSource = std::move(Source);
    mPalette.Reset();
    ImGui::OpenPopup("TGLNodePalette");
}

std::optional<FEdit> FGraphEditor::DrawEditControls(const FGraphView& View, const IGraphSchema& Schema) {
    std::optional<FEdit> Edit{};
    std::optional<FPinKey> SpawnFrom{};
    std::string Hint{};
    const ImVec2 Mouse{Canvas::GetPointerPosition()};

    if (Canvas::BeginCreate()) {
        FPinId From{};
        FPinId To{};

        if (Canvas::QueryNewLink(&From, &To) && From && To) {
            const FPinKey& Start{mEndpoints.at(From.Get())};
            const FPinKey& End{mEndpoints.at(To.Get())};
            const FConnect Connection{Start.second == EPinDirection::Output ? FConnect{Start.first, End.first} : FConnect{End.first, Start.first}};
            const FConnectionResult Verdict{Start.second == End.second ? FConnectionResult{EConnection::Reject, "Connect an output to an input."} : Schema.QueryConnection(View, Connection)};
            Hint = Verdict.mMessage;

            if (Verdict.mDecision == EConnection::Reject) {
                Canvas::RejectNewItem(Canvas::GetStyle().mErrorColor);
            } else if (Canvas::AcceptNewItem(Canvas::ConnectionColor(Canvas::EConnectionRole::Default))) {
                Edit = Connection;
            }
        } else if (Canvas::QueryNewNode(&From) && Canvas::AcceptNewItem(Canvas::ConnectionColor(Canvas::EConnectionRole::Default))) {
            SpawnFrom = mEndpoints.at(From.Get());
        }
    }

    Canvas::EndCreate();
    Canvas::Suspend();

    if (!Hint.empty()) {
        ImGui::SetTooltip("%s", Hint.c_str());
    }

    if (SpawnFrom || Canvas::ShowBackgroundContextMenu()) {
        OpenPalette(Mouse, std::move(SpawnFrom));
    }

    if (ImGui::BeginPopup("TGLNodePalette")) {
        const std::optional<FEndpoint> Source{mPaletteSource ? std::optional<FEndpoint>{mPaletteSource->first} : std::nullopt};
        const auto Actions{Schema.ListNodeActions(View, Source)};

        if (const auto Selected{mPalette.Draw(Actions)}) {
            Edit = FCreateNode{Actions[*Selected].mKey, mPalettePosition, Source, mPaletteSource ? mPaletteSource->second : EPinDirection::Output};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    Canvas::Resume();

    if (Canvas::BeginDelete()) {
        FDeleteSelection Delete{};
        FNodeId Node{};
        FLinkId Link{};

        while (Canvas::QueryDeletedNode(&Node)) {
            const FNodeView* Value{FindNode(View, Node)};

            if (!Edit && Value != nullptr && Value->mDeletable) {
                if (Canvas::AcceptDeletedItem(false)) {
                    Delete.mItems.emplace_back(Node);
                }
            } else {
                Canvas::RejectDeletedItem();
            }
        }

        while (Canvas::QueryDeletedLink(&Link)) {
            const FLinkView* Value{FindLink(View, Link)};

            if (!Edit && Value != nullptr && Value->mDeletable) {
                if (Canvas::AcceptDeletedItem(false)) {
                    Delete.mItems.emplace_back(Link);
                }
            } else {
                Canvas::RejectDeletedItem();
            }
        }

        if (!Delete.mItems.empty()) {
            Edit = std::move(Delete);
        }
    }

    Canvas::EndDelete();

    return Edit;
}
