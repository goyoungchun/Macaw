/**
 * @file FAnimGraphEditor.cpp
 * @brief 언리얼 AnimGraph 편집기처럼 노드를 배치하고 포즈 핀을 연결해 그래프 에셋을 만듭니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "FAnimGraphEditor.h"
#include "Editor/FEditorToolkitHost.h"
#include "Asset/FAssetManager.h"
#include "Asset/Animation/UAnimGraph.h"
#include "Core/FilePath.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Editor/FAssetThumbnailCache.h"
#include "Editor/FEditorAssetCatalog.h"

#include "EditorStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <utility>

namespace Editor {

    /**
     * @brief 언리얼 AnimGraph 편집기처럼 노드를 배치하고 포즈 핀을 연결해 그래프 에셋을 만듭니다.
     */
    namespace {

        /**
         * @brief 표시 이름과 무관하게 창 상태를 유지하는 ImGui 창 ID입니다.
         */

        /**
         * @brief 그래프 변수의 ImGui 드래그 페이로드 식별자입니다.
         */
        constexpr char VariablePayload[]{"MACAW_ANIM_GRAPH_VARIABLE"};

        /**
         * @brief 정규화한 경로가 지정 루트 내부에 속하는지 검사합니다.
         * @param Path 처리할 에셋 또는 파일 경로입니다.
         * @param Root 해당 상태 또는 그래프의 비소유 루트 포즈 노드입니다.
         * @return 경로가 루트 또는 그 하위에 있으면 true입니다.
         */
        bool IsUnder(const std::filesystem::path& Path, const std::filesystem::path& Root) {
            const auto Relative{Path.lexically_normal().lexically_relative(Root.lexically_normal())};

            return !Relative.empty() && *Relative.begin() != L"..";
        }

    }

    FAnimGraphEditor::FAnimGraphEditor(Asset::FAssetManager* Assets, Editor::FEditorAssetCatalog* Catalog, Editor::FAssetThumbnailCache* Thumbnails)
        : mAssets{Assets},
          mCatalog{Catalog},
          mThumbnails{Thumbnails} {
    }

    FAnimGraphEditor::~FAnimGraphEditor() {
        ClearGraphContexts();
    }

    bool FAnimGraphEditor::Open(const std::filesystem::path& Path, Core::FString& Error) {
        Asset::UAnimGraph* Source{mAssets != nullptr ? mAssets->LoadAnimGraph(Path, Error) : nullptr};

        if (Source == nullptr) {
            return false;
        }

        mNavigation.clear();
        ClearGraphContexts();
        mRootEditing = CoreUObject::NewObject<Asset::UAnimGraph>();
        mEditing = mRootEditing.get();
        mEditing->CopyFrom(*Source);
        mEditing->SetName(Source->GetName());

        mPath = Path;
        mDirty = false;
        mOpen = true;
        mNewVariable.fill('\0');
        mError.clear();

        // 다른 그래프의 노드가 남지 않도록 노드 에디터 컨텍스트를 새로 만듭니다.
        mView = {};
        mView.mGraph = mEditing;
        ActivateView(mView);
        ImGui::SetWindowFocus(mWindowId.c_str());

        return true;
    }

    void FAnimGraphEditor::CloseRetired(const std::filesystem::path& Path) {
        if (!mOpen || mAssets == nullptr) {
            return;
        }

        const std::filesystem::path Root{mAssets->GetContentRoot()};
        const std::filesystem::path Target{Path.is_absolute() ? Path : Root / Path};

        if (Root / mPath == Target || IsUnder(Root / mPath, Target)) {
            Close();
        }
    }

    void FAnimGraphEditor::SetWindowId(Core::FString Id) {
        mWindowId = std::move(Id);
    }

    const Core::FString& FAnimGraphEditor::GetWindowId() const {
        return mWindowId;
    }

    bool FAnimGraphEditor::IsOpen() const {
        return mOpen;
    }

    bool FAnimGraphEditor::IsFocused() const {
        if (mHostTab != nullptr) {
            return mHostTab->IsFocused();
        }

        ImGuiWindow* Window{mOpen ? ImGui::FindWindowByName(mWindowId.c_str()) : nullptr};
        ImGuiWindow* Focused{ImGui::GetCurrentContext()->NavWindow};

        return Window != nullptr && Focused != nullptr && ImGui::IsWindowChildOf(Focused, Window, true, false);
    }

    void FAnimGraphEditor::Close() {
        mOpen = false;
        mEditing = nullptr;
        mNavigation.clear();
        ClearGraphContexts();
        mRootEditing.reset();
    }

    bool FAnimGraphEditor::Save() {
        if (mEditing == nullptr || mAssets == nullptr) {
            return false;
        }

        if (!mAssets->SaveAnimGraph(mPath, *mRootEditing, mError)) {
            return false;
        }

        mDirty = false;
        mError.clear();

        return true;
    }

    void FAnimGraphEditor::InitializeToolkit(FEditorTab& Tab) {
        mHostTab = &Tab;
        Tab.SetIcon(EEditorIcon::Component, IM_COL32(200, 100, 100, 255));
        FEditorToolkitHost& Host{Tab.GetToolkitHost()};
        Host.AddWidget("Variables", "Variables", [this](float) {
            if (mEditing != nullptr) {
                DrawVariables(mSaveRequested);
            }
        }, EEditorDockRegion::Left);
        Host.AddWidget("Graph", "Graph", [this](float) {
            if (mEditing == nullptr) {
                return;
            }

            if (!mError.empty()) {
                ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", mError.c_str());
            }

            DrawNavigation();

            if (mView.mMode == EGraphView::Machine) {
                DrawStateMachine();
            } else if (mView.mMode == EGraphView::Rule) {
                DrawTransitionRule();
            } else {
                DrawGraph();
            }
        });
    }

    void FAnimGraphEditor::UpdateToolkit(float, bool) {
        mSaveRequested = false;

        if (mRootEditing != nullptr) {
            mHostTab->SetName(EditorStyle::TabIconPadding + mRootEditing->GetName() + (mDirty ? "*" : "") + mWindowId);
        }
    }

    void FAnimGraphEditor::HandleCommands(bool CanSave) {
        mSaveRequested = CanSave && IsFocused() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S);
    }

    void FAnimGraphEditor::FinishToolkit() {
        if (mSaveRequested && mDirty) {
            Save();
        }
    }

    void FAnimGraphEditor::DrawVariables(bool SaveRequested) {
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const float TextHeight{ImGui::GetTextLineHeight()};
        const float AddWidth{ImGui::CalcTextSize("+ 추가").x + 2.0f * Style.FramePadding.x};

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, TextHeight * 0.3f);
        ImGui::TextUnformatted("Variables");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu", mRootEditing->GetVariables().size());

        ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - AddWidth - Style.ItemSpacing.x));
        const bool AddFromName{ImGui::InputTextWithHint("##NewVariable", "새 변수 이름", mNewVariable.data(), mNewVariable.size(), ImGuiInputTextFlags_EnterReturnsTrue)};
        ImGui::SameLine();

        bool Added{};

        if (ImGui::Button("+ 추가", {AddWidth, 0.0f}) || AddFromName) {
            Added = mRootEditing->AddVariable({mNewVariable.data(), 0.0f, Asset::EAnimGraphVariableSource::Manual});

            if (Added) {
                mNewVariable.fill('\0');
                mDirty = true;
                mError.clear();
            } else {
                mError = "변수 이름은 비어 있지 않고 다른 변수와 겹치지 않아야 합니다.";
            }
        }

        ImGui::Spacing();

        Index_t Remove{InvalidIndex};

        if (ImGui::BeginChild("VariableList", {}, ImGuiChildFlags_NavFlattened)) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{TextHeight * 0.45f, TextHeight * 0.35f});
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, TextHeight * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.11f, 0.14f, 0.18f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_Border, {0.29f, 0.35f, 0.43f, 0.55f});

            for (Index_t Index{}; Index < mRootEditing->GetVariables().size(); ++Index) {
                if (DrawVariable(Index, SaveRequested)) {
                    Remove = Index;
                }
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            if (Added) {
                ImGui::SetScrollHereY(1.0f);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (Remove < mRootEditing->GetVariables().size()) {
            mRootEditing->RemoveVariable(Remove);
            mDirty = true;
        }
    }

    bool FAnimGraphEditor::DrawVariable(Index_t Index, bool SaveRequested) {
        ImGui::PushID(static_cast<int>(Index));
        const bool Visible{ImGui::BeginChild("Variable", {}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)};
        bool Remove{};

        if (Visible) {
            const ImGuiStyle& Style{ImGui::GetStyle()};
            const float Width{std::max(1.0f, ImGui::GetContentRegionAvail().x)};
            const float FrameHeight{ImGui::GetFrameHeight()};
            const float GetWidth{ImGui::CalcTextSize("Get").x + 2.0f * Style.FramePadding.x};
            const float NameWidth{std::max(1.0f, Width - FrameHeight - GetWidth - 2.0f * Style.ItemSpacing.x)};
            Asset::FAnimGraphVariable Variable{mRootEditing->GetVariables()[Index]};
            Core::TStaticArray<char, 64> Name{};
            bool Changed{};

            std::copy_n(Variable.mName.begin(), std::min(Variable.mName.size(), Name.size() - 1), Name.begin());
            ImGui::SetNextItemWidth(NameWidth);

            // 이름은 입력을 마칠 때 바꿉니다. 글자마다 바꾸면 중간 이름이 다른 변수와 겹칠 수 있습니다.
            const bool Rename{ImGui::InputText("##Name", Name.data(), Name.size(), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()};

            if ((Rename || (SaveRequested && ImGui::IsItemActive())) && Variable.mName != Name.data()) {
                Variable.mName = Name.data();
                Changed = true;
            }

            if (ImGui::CalcTextSize(Variable.mName.c_str()).x + 2.0f * Style.FramePadding.x > NameWidth) {
                ImGui::SetItemTooltip("%s", Variable.mName.c_str());
            }

            ImGui::SameLine();
            ImGui::Button("Get", {GetWidth, FrameHeight});

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(VariablePayload, Variable.mName.c_str(), Variable.mName.size() + 1);
                ImGui::Text("Get %s", Variable.mName.c_str());
                ImGui::EndDragDropSource();
            } else {
                ImGui::SetItemTooltip("캔버스로 끌어 Get %s 추가", Variable.mName.c_str());
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.55f, 0.22f, 0.25f, 0.85f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.68f, 0.25f, 0.28f, 1.0f});
            Remove = ImGui::Button("×##Remove", {FrameHeight, FrameHeight});
            ImGui::PopStyleColor(3);
            ImGui::SetItemTooltip("변수 삭제");

            const char* Sources[]{"Manual", "Owner Speed"};
            const float MinSourceWidth{ImGui::CalcTextSize(Sources[1]).x + FrameHeight + 2.0f * Style.FramePadding.x};
            const float MinValueWidth{ImGui::CalcTextSize("-00.000").x + 2.0f * Style.FramePadding.x};
            const float FieldWidth{Width - Style.ItemSpacing.x};
            const bool SideBySide{FieldWidth >= MinSourceWidth + MinValueWidth};
            const float SourceWidth{SideBySide ? std::clamp(FieldWidth * 0.6f, MinSourceWidth, FieldWidth - MinValueWidth) : Width};
            int Source{static_cast<int>(Variable.mSource)};

            ImGui::TextDisabled("Source");

            if (SideBySide) {
                ImGui::SameLine(0.0f, SourceWidth + Style.ItemSpacing.x - ImGui::GetItemRectSize().x);
                ImGui::TextDisabled("Default");
            }

            ImGui::SetNextItemWidth(SourceWidth);

            if (ImGui::Combo("##Source", &Source, Sources, 2)) {
                Variable.mSource = static_cast<Asset::EAnimGraphVariableSource>(Source);
                Changed = true;
            }

            if (SideBySide) {
                ImGui::SameLine();
            } else {
                ImGui::TextDisabled("Default");
            }

            ImGui::SetNextItemWidth(-1.0f);

            if (Variable.mType == Asset::EAnimGraphVariableType::Bool) {
                Changed |= ImGui::Checkbox("##Default", &Variable.mBoolDefault);
                Variable.mSource = Asset::EAnimGraphVariableSource::Manual;
            } else {
                Changed |= ImGui::DragFloat("##Default", &Variable.mDefault, 0.01f, 0.0f, 0.0f, "%.3f");
            }

            int Type{static_cast<int>(Variable.mType)};
            const char* Types[]{"Float", "Bool"};

            if (ImGui::Combo("Type", &Type, Types, 2)) {
                Variable.mType = static_cast<Asset::EAnimGraphVariableType>(Type);
                Changed = true;
            }

            if (Changed) {
                if (mRootEditing->SetVariable(Index, Variable)) {
                    mDirty = true;
                    mError.clear();
                } else {
                    mError = "변수 이름은 비어 있지 않고 다른 변수와 겹치지 않아야 합니다.";
                }
            }
        }

        ImGui::EndChild();
        ImGui::PopID();

        return Remove;
    }

    void FAnimGraphEditor::TrackGraphEdit(const TGL::FEditResult& Edit) {
        if (Edit.mStatus == TGL::EEditStatus::Applied) {
            mDirty = true;
            mError.clear();
        } else if (!Edit.mMessage.empty()) {
            mError = Edit.mMessage;
        }
    }

    void FAnimGraphEditor::DrawGraph() {
        auto& Document{mPoseDocuments[mView.mKey]};
        auto& Editor{mGraphEditors[mView.mKey]};

        if (!Document) {
            Document = std::make_unique<FAnimPoseGraphDocument>();
            Editor = std::make_unique<TGL::FGraphEditor>();
            Document->Bind(*mEditing);
        } else {
            Document->Refresh();
        }

        const auto Apply{[this, &Document, &Editor](const TGL::FEdit& Edit) {
            TrackGraphEdit(Editor->SubmitEdit(*Document, Edit));
        }};

        const ImVec2 Origin{ImGui::GetCursorScreenPos()};
        const ImVec2 Size{ImGui::GetContentRegionAvail()};
        const TGL::FGraphEvents Events{Editor->Draw("AnimGraphCanvas", *Document, *Document, Document.get(), Size)};

        if (Events.mEdit) {
            TrackGraphEdit(*Events.mEdit);
        }

        if (!Events.mVisible) {
            return;
        }

        if (Events.mContextItem && std::holds_alternative<TGL::FNodeId>(*Events.mContextItem)) {
            ImGui::OpenPopup("AnimGraphNodeMenu");
        }

        TGL::FNodeId OpenMachine{};
        ImGui::SetNextWindowSizeConstraints({360.0f, 0.0f}, {500.0f, ImGui::GetIO().DisplaySize.y});

        if (ImGui::BeginPopup("AnimGraphNodeMenu")) {
            const auto Selection{Editor->GetSelection()};
            const auto* Id{Selection.size() == 1 ? std::get_if<TGL::FNodeId>(&Selection.front()) : nullptr};

            if (Id != nullptr) {
                const TGL::FNodeId NodeId{*Id};
                const Int32 PoseId{FAnimPoseGraphDocument::GetPoseNodeId(NodeId)};
                const TGL::FNodeView* Node{TGL::FindNode(Document->ReadView(), NodeId)};
                const bool CanDelete{Node != nullptr && Node->mDeletable};

                if (Node != nullptr) {
                    ImGui::SeparatorText(Node->mTitle.c_str());
                }

                TrackGraphEdit(Editor->DrawDetails(*Document, Document.get(), [this](const TGL::FPropertyView& Property, TGL::FPropertyValue& Value) {
                    const Asset::EAssetType Type{Property.mCustomEditor == "AnimSequence" ? Asset::EAssetType::AnimSequence : Property.mCustomEditor == "BlendSpace" ? Asset::EAssetType::BlendSpace : Asset::EAssetType::PhysicsAsset};

                    return mCatalog != nullptr && mThumbnails != nullptr && mPicker.Draw(*mCatalog, *mThumbnails, Property.mName.c_str(), std::get<std::string>(Value), Type, {}, "None", "Inherited", false);
                }));

                if (mEditing->FindStateMachine(PoseId) != nullptr && ImGui::MenuItem("Open State Machine")) {
                    OpenMachine = NodeId;
                }

                if (ImGui::MenuItem("삭제", "Delete", false, CanDelete)) {
                    Apply(TGL::FDeleteSelection{{NodeId}});
                }

                if (ImGui::MenuItem("입력 연결 끊기", nullptr, false, PoseId != -2)) {
                    Apply(TGL::FDisconnectInputs{NodeId});
                }
            }

            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTargetCustom(ImRect{Origin, {Origin.x + Size.x, Origin.y + Size.y}}, ImGui::GetID("AnimGraphVariableDrop"))) {
            if (const ImGuiPayload* Payload{ImGui::AcceptDragDropPayload(VariablePayload)}; Payload != nullptr && Payload->Data != nullptr && Payload->DataSize > 1 && static_cast<const char*>(Payload->Data)[Payload->DataSize - 1] == '\0') {
                const Core::FString Name{static_cast<const char*>(Payload->Data), static_cast<std::size_t>(Payload->DataSize - 1)};
                Apply(TGL::FCreateNode{FAnimPoseGraphDocument::GetterAction(Name), Events.mPointerPosition});
            }

            ImGui::EndDragDropTarget();
        }

        if (Events.mActivated) {
            if (const auto* Node{std::get_if<TGL::FNodeId>(&*Events.mActivated)}) {
                OpenMachine = *Node;
            }
        }

        if (OpenMachine) {
            OpenStateMachine(FAnimPoseGraphDocument::GetPoseNodeId(OpenMachine));
        }
    }

}
