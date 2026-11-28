/**
 * @file FAnimGraphStateMachineEditor.cpp
 * @brief 상태 머신과 전이 조건의 노드 편집 UI를 구현합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "FAnimGraphEditor.h"
#include "Asset/Animation/UAnimGraph.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace Editor {

    void FAnimGraphEditor::ClearGraphContexts() {
        mGraphEditors.clear();
        mPoseDocuments.clear();
        mMachineDocuments.clear();
        mRuleEditors.clear();
    }

    void FAnimGraphEditor::ActivateView(const FGraphView& View) {
        mView = View;
        mEditing = View.mGraph;
    }

    void FAnimGraphEditor::Navigate(Asset::UAnimGraph* Graph, EGraphView Mode, Int32 Machine, Int32 Transition, const Core::FString& Label) {
        mNavigation.push_back(mView);
        FGraphView View{};
        View.mGraph = Graph;
        View.mMode = Mode;
        View.mMachine = Machine;
        View.mTransition = Transition;
        View.mLabel = Label;
        View.mKey = std::format("{}/{}:{}:{}", mView.mKey, static_cast<int>(Mode), Machine, Transition);
        ActivateView(View);
    }

    void FAnimGraphEditor::DrawNavigation() {
        Int32 Destination{-1};
        const float Height{ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + 4.0f};

        if (ImGui::BeginChild("GraphNavigation", {0.0f, Height}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
            for (Index_t Index{}; Index <= mNavigation.size(); ++Index) {
                const bool Current{Index == mNavigation.size()};
                const FGraphView& View{Current ? mView : mNavigation[Index]};
                const Core::FString Label{GetViewLabel(View, Index > 0 ? &mNavigation[Index - 1] : nullptr)};

                if (Index > 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(">");
                    ImGui::SameLine();
                }

                ImGui::PushID(static_cast<int>(Index));
                ImGui::BeginDisabled(Current);

                if (ImGui::Button(Label.c_str())) {
                    Destination = static_cast<Int32>(Index);
                }

                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }

        ImGui::EndChild();

        if (Destination >= 0) {
            NavigateTo(static_cast<std::size_t>(Destination));
        }
    }

    Core::FString FAnimGraphEditor::GetViewLabel(const FGraphView& View, const FGraphView* Parent) const {
        if (View.mGraph != nullptr && View.mMode == EGraphView::Machine) {
            if (const auto* Node{View.mGraph->FindNode(View.mMachine)}) {
                return Node->mName.empty() ? Core::FString{"State Machine"} : Node->mName;
            }
        } else if (View.mGraph != nullptr && View.mMode == EGraphView::Rule) {
            const auto* Machine{View.mGraph->FindStateMachine(View.mMachine)};
            const auto* Transition{Machine != nullptr ? Machine->FindTransition(View.mTransition) : nullptr};
            const auto* From{Transition != nullptr ? Machine->FindState(Transition->mFromState) : nullptr};
            const auto* To{Transition != nullptr ? Machine->FindState(Transition->mToState) : nullptr};

            if (From != nullptr && To != nullptr) {
                return std::format("{} -> {}", From->mName, To->mName);
            }
        } else if (Parent != nullptr && Parent->mGraph != nullptr && Parent->mMode == EGraphView::Machine) {
            const auto* Machine{Parent->mGraph->FindStateMachine(Parent->mMachine)};

            if (const auto* State{Machine != nullptr ? Machine->FindState(View.mMachine) : nullptr}) {
                return State->mName;
            }
        }

        return View.mLabel;
    }

    bool FAnimGraphEditor::OpenStateMachine(Int32 NodeId) {
        if (mEditing == nullptr || mView.mMode != EGraphView::Pose || mEditing->FindStateMachine(NodeId) == nullptr) {
            return false;
        }

        Navigate(mEditing, EGraphView::Machine, NodeId, -1, mEditing->FindNode(NodeId)->mName);

        return true;
    }

    bool FAnimGraphEditor::OpenState(Int32 StateId) {
        auto* Machine{mEditing != nullptr && mView.mMode == EGraphView::Machine ? mEditing->FindStateMachine(mView.mMachine) : nullptr};
        auto* State{Machine != nullptr ? Machine->FindState(StateId) : nullptr};

        if (State == nullptr || State->mGraph == nullptr) {
            return false;
        }

        Navigate(State->mGraph.get(), EGraphView::Pose, StateId, -1, State->mName);

        return true;
    }

    bool FAnimGraphEditor::OpenTransitionRule(Int32 TransitionId) {
        auto* Machine{mEditing != nullptr && mView.mMode == EGraphView::Machine ? mEditing->FindStateMachine(mView.mMachine) : nullptr};

        if (Machine == nullptr || Machine->FindTransition(TransitionId) == nullptr) {
            return false;
        }

        Navigate(mEditing, EGraphView::Rule, mView.mMachine, TransitionId, "Transition Rule");

        return true;
    }

    bool FAnimGraphEditor::NavigateBack() {
        return !mNavigation.empty() && NavigateTo(mNavigation.size() - 1);
    }

    bool FAnimGraphEditor::NavigateTo(Index_t Depth) {
        if (Depth >= mNavigation.size()) {
            return false;
        }

        const FGraphView View{mNavigation[Depth]};
        mNavigation.resize(Depth);
        ActivateView(View);

        return true;
    }

    void FAnimGraphEditor::DrawStateMachine() {
        auto* Machine{mEditing->FindStateMachine(mView.mMachine)};

        if (Machine == nullptr) {
            return;
        }

        auto& Document{mMachineDocuments[mView.mKey]};
        auto& Editor{mGraphEditors[mView.mKey]};

        if (!Document) {
            Document = std::make_unique<FAnimStateMachineGraphDocument>();
            Editor = std::make_unique<TGL::FGraphEditor>();
        }

        Document->Refresh(*Machine, *mRootEditing);
        const auto Apply{[this, &Document, &Editor](const TGL::FEdit& Edit) {
            TrackGraphEdit(Editor->SubmitEdit(*Document, Edit));
        }};

        if (ImGui::Button("+ State")) {
            Apply(TGL::FCreateNode{"State", {static_cast<float>(Machine->GetStates().size()) * 220.0f, 0.0f}});
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Double-click a state or transition to open it");
        const TGL::FGraphEvents Events{Editor->Draw("StateMachineCanvas", *Document, *Document, Document.get())};

        if (Events.mEdit) {
            TrackGraphEdit(*Events.mEdit);
        }

        if (Events.mContextItem) {
            ImGui::OpenPopup("StateMachineGraphMenu");
        }

        std::optional<TGL::FSelectionItem> Open{Events.mActivated};
        ImGui::SetNextWindowSizeConstraints({320.0f, 0.0f}, {500.0f, ImGui::GetIO().DisplaySize.y});

        if (ImGui::BeginPopup("StateMachineGraphMenu")) {
            const auto Selection{Editor->GetSelection()};

            if (Selection.size() == 1) {
                const TGL::FSelectionItem Item{Selection.front()};
                const TGL::FGraphView View{Document->ReadView()};
                const auto* Id{std::get_if<TGL::FNodeId>(&Item)};
                const auto* Node{Id != nullptr ? TGL::FindNode(View, *Id) : nullptr};
                const auto* Link{Id == nullptr ? TGL::FindLink(View, std::get<TGL::FLinkId>(Item)) : nullptr};
                const bool IsState{Node != nullptr};
                const bool CanDelete{(IsState && Node->mDeletable) || (Link != nullptr && Link->mDeletable)};

                if (Node != nullptr) {
                    ImGui::SeparatorText(Node->mTitle.c_str());
                } else if (Link != nullptr && !Link->mTooltip.empty()) {
                    ImGui::TextUnformatted(Link->mTooltip.c_str());
                }

                TrackGraphEdit(Editor->DrawDetails(*Document, Document.get()));

                if (CanDelete) {
                    ImGui::Separator();

                    if (ImGui::MenuItem(IsState ? "Open State Graph" : "Open Transition Rule")) {
                        Open = Item;
                    }

                    if (ImGui::MenuItem(IsState ? "Delete State" : "Delete Transition", "Delete")) {
                        Apply(TGL::FDeleteSelection{{Item}});
                    }
                }
            }

            ImGui::EndPopup();
        }

        if (Open) {
            if (const auto* Id{std::get_if<TGL::FNodeId>(&*Open)}) {
                OpenState(FAnimStateMachineGraphDocument::GetStateId(*Id));
            } else {
                OpenTransitionRule(FAnimStateMachineGraphDocument::GetTransitionId(std::get<TGL::FLinkId>(*Open)));
            }
        }
    }

    void FAnimGraphEditor::DrawTransitionRule() {
        auto* Machine{mEditing->FindStateMachine(mView.mMachine)};
        auto* Transition{Machine != nullptr ? Machine->FindTransition(mView.mTransition) : nullptr};

        if (Transition == nullptr) {
            return;
        }

        auto& RuleEditor{mRuleEditors[mView.mKey]};

        if (RuleEditor == nullptr) {
            RuleEditor = std::make_unique<Editor::FAnimTransitionGraphEditor>();
        }

        const ImVec2 Origin{ImGui::GetCursorScreenPos()};
        const ImVec2 Size{ImGui::GetContentRegionAvail()};
        const TGL::FGraphEvents Events{RuleEditor->Draw(Transition->mRule, *mRootEditing, Size)};

        if (Events.mEdit) {
            TrackGraphEdit(*Events.mEdit);
        }

        if (!Events.mVisible) {
            return;
        }

        if (Events.mContextItem && std::holds_alternative<TGL::FNodeId>(*Events.mContextItem)) {
            ImGui::OpenPopup("AnimRuleNodeMenu");
        }

        ImGui::SetNextWindowSizeConstraints({360.0f, 0.0f}, {500.0f, ImGui::GetIO().DisplaySize.y});

        if (ImGui::BeginPopup("AnimRuleNodeMenu")) {
            DrawRuleSelection();
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTargetCustom(ImRect{Origin, {Origin.x + Size.x, Origin.y + Size.y}}, ImGui::GetID("RuleVariableDrop"))) {
            if (const ImGuiPayload* Payload{ImGui::AcceptDragDropPayload("MACAW_ANIM_GRAPH_VARIABLE")}; Payload != nullptr && Payload->Data != nullptr && Payload->DataSize > 1 && static_cast<const char*>(Payload->Data)[Payload->DataSize - 1] == '\0') {
                const Core::FString Name{static_cast<const char*>(Payload->Data)};
                TrackGraphEdit(RuleEditor->AddVariable(Name, Events.mPointerPosition));
            }

            ImGui::EndDragDropTarget();
        }
    }

    void FAnimGraphEditor::DrawRuleSelection() {
        const auto Found{mRuleEditors.find(mView.mKey)};

        if (Found == mRuleEditors.end()) {
            return;
        }

        Editor::FAnimTransitionGraphEditor& RuleEditor{*Found->second};
        TrackGraphEdit(RuleEditor.DrawDetails());
        ImGui::Separator();

        const auto Selection{RuleEditor.GetSelection()};
        const auto* Id{Selection.size() == 1 ? std::get_if<TGL::FNodeId>(&Selection.front()) : nullptr};
        const TGL::FNodeView* Node{Id != nullptr ? TGL::FindNode(RuleEditor.ReadView(), *Id) : nullptr};

        if (ImGui::MenuItem("Delete Node", "Delete", false, Node != nullptr && Node->mDeletable)) {
            TrackGraphEdit(RuleEditor.Delete(*Id));
        }
    }

}
