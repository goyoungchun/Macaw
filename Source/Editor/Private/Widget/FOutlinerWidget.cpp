/**
 * @file FOutlinerWidget.cpp
 * @brief 월드의 액터 목록과 선택 상태를 표시합니다.
 * @ingroup Editor
 */

#include "Editor/Widget/FOutlinerWidget.h"
#include "Engine/ALevelSequenceActor.h"
#include "Editor/FEditorContext.h"
#include "EditorStyle.h"
#include "Engine/AActor.h"
#include "Engine/UWorld.h"
#include "Engine/Component/UCameraComponent.h"
#include "Engine/Component/USceneComponent.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Engine/EPrimitiveType.h"
#include "Widget/ObjectRow.h"

#include <imgui_internal.h>
#include <algorithm>
#include <optional>

namespace Editor {

    /**
     * @brief 이 파일에서 사용하는 내부 보조 기능과 상태를 묶습니다.
     */
    namespace {
        using FActorRef = CoreUObject::TObjectRef<Engine::AActor>;

        struct FOutlinerRow {
            FActorRef mActor{};
            float mIndent{};
            std::optional<ImGuiID> mExpansionId{};
        };

        /**
         * @brief 객체·클래스 이름이 Outliner 필터에 맞는지 검사합니다.
         * @param[in] Filter 객체·클래스 이름을 검사할 필터입니다.
         * @param[in] Object 조회하거나 호출할 엔진·COM 객체입니다.
         * @return 객체·클래스 이름이 필터를 통과하면 true입니다.
         */
        bool MatchesFilter(const ImGuiTextFilter& Filter, const CoreUObject::UObject* Object) {
            return Filter.PassFilter(Object->GetName().c_str()) || Filter.PassFilter(ObjectRow::GetTypeLabel(Object).c_str());
        }

        Core::TArray<FOutlinerRow> BuildActorRows(const FEditorContext& Context, const Core::TArray<FActorRef>& Actors, const ImGuiTextFilter& Filter, const ImGuiStorage& ExpansionState) {
            Core::TMap<const Engine::AActor*, const Engine::AActor*> Parents{};
            Core::TArray<Engine::AActor*> ResolvedActors{};
            ResolvedActors.reserve(Actors.size());

            for (FActorRef Handle : Actors) {
                auto* Actor{CoreUObject::Cast<Engine::AActor>(Context.ResolveObject(Handle))};

                if (Actor == nullptr) {
                    continue;
                }

                Parents.emplace(Actor, Actor->AActor::GetSceneOutlinerParent().Get());
                ResolvedActors.push_back(Actor);
            }

            for (Engine::AActor* Actor : ResolvedActors) {
                const Engine::AActor*& CurrentParent{Parents.at(Actor)};
                const Engine::AActor* Parent{Actor->GetSceneOutlinerParent().Get()};

                if (Parent == CurrentParent || (Parent != nullptr && !Parents.contains(Parent))) {
                    continue;
                }

                const Engine::AActor* Ancestor{Parent};

                for (Index_t Depth{}; Ancestor != nullptr && Ancestor != Actor && Depth < Parents.size(); ++Depth) {
                    const auto AncestorEntry{Parents.find(Ancestor)};
                    Ancestor = AncestorEntry != Parents.end() ? AncestorEntry->second : nullptr;
                }

                if (Ancestor == nullptr) {
                    CurrentParent = Parent;
                }
            }

            Core::TMap<const Engine::AActor*, Core::TArray<Engine::AActor*>> Children{};

            for (Engine::AActor* Actor : ResolvedActors) {
                const Engine::AActor* Parent{Parents.at(Actor)};
                Children[Parents.contains(Parent) ? Parent : nullptr].push_back(Actor);
            }

            Core::TArray<FOutlinerRow> Rows{};
            Rows.reserve(Actors.size());
            const float IndentStep{ImGui::GetTreeNodeToLabelSpacing()};
            const bool CanCollapse{!Filter.IsActive()};
            const ImGuiID ExpansionScope{ImGui::GetID("##OutlinerExpansion")};
            const auto Visit{[&](const auto& VisitChildren, const Core::TArray<Engine::AActor*>& Siblings, float Indent) -> void {
                for (Engine::AActor* Actor : Siblings) {
                    const bool Matches{MatchesFilter(Filter, Actor)};
                    const auto ActorChildren{Children.find(Actor)};
                    std::optional<ImGuiID> ExpansionId{};

                    if (CanCollapse && ActorChildren != Children.end()) {
                        const auto& GuidBytes{Actor->GetGuid().mBytes};
                        ExpansionId = ImHashData(GuidBytes.data(), GuidBytes.size(), ExpansionScope);
                    }

                    if (Matches) {
                        Rows.push_back({CoreUObject::MakeObjectRef(Actor), Indent, ExpansionId});
                    }

                    if (ActorChildren != Children.end() && (!ExpansionId || ExpansionState.GetBool(*ExpansionId, true))) {
                        VisitChildren(VisitChildren, ActorChildren->second, Matches ? Indent + IndentStep : Indent);
                    }
                }
            }};

            Visit(Visit, Children[nullptr], 0.0f);

            return Rows;
        }

        /**
         * @brief 액터 표시 상태 버튼을 그리고 클릭 여부를 반환합니다.
         * @param[in] ActorVisible 액터의 현재 표시 상태입니다.
         * @return 표시 전환 버튼을 클릭했으면 true입니다.
         */
        bool DrawVisibilityButton(bool ActorVisible) {
            const float Size{ImGui::GetFontSize()};
            const bool Pressed{ImGui::InvisibleButton("##Visibility", {Size, Size})};
            const ImU32 Color{ImGui::GetColorU32(ActorVisible || ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled)};

            EditorStyle::DrawIcon(ActorVisible ? EditorStyle::EIcon::Visible : EditorStyle::EIcon::Hidden, ImGui::GetItemRectMin(), Size, Color);
            ImGui::SetItemTooltip("%s", ActorVisible ? "숨기기" : "표시");

            return Pressed;
        }
    }

    /**
     * @brief FOutlinerWidget의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Editor::FOutlinerWidget::FOutlinerWidget(Editor::FEditorContext* Context)
		: Editor::FEditorContextWidget{Context, "Outliner"} {
    }

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Editor::FOutlinerWidget::~FOutlinerWidget() = default;

    /**
     * @brief 패널 고유의 ImGui 위젯과 내용을 그립니다.
     */
    void Editor::FOutlinerWidget::DrawContent(float) {
        Editor::FEditorContext* Context{GetEditorContext()};

        ImGui::BeginDisabled(!Context->CanEditWorld());

        if (ImGui::Button("+ 액터")) {
            ImGui::OpenPopup("AddActor");
        }

        if (ImGui::BeginPopup("AddActor")) {
            if (ImGui::MenuItem("Plane")) {
                Context->SpawnPrimitive(Engine::EPrimitiveType::Plane);
            }

            if (ImGui::MenuItem("Cube")) {
                Context->SpawnPrimitive(Engine::EPrimitiveType::Cube);
            }

            if (ImGui::MenuItem("Sphere")) {
                Context->SpawnPrimitive(Engine::EPrimitiveType::Sphere);
            }

            if (ImGui::MenuItem("Camera")) {
                Context->SpawnCamera();
            }

            if (ImGui::MenuItem("Text")) {
                Context->SpawnText();
            }

            if (ImGui::MenuItem("Billboard")) {
                Context->SpawnBillboard();
            }

            if (ImGui::BeginMenu("Editor Icons")) {
                if (ImGui::MenuItem("Spot Light Icon")) {
                    Context->SpawnBillboard("/Engine/Editor/Icon/SpotLight_64x.png");
                }

                if (ImGui::MenuItem("Point Light Icon")) {
                    Context->SpawnBillboard("/Engine/Editor/Icon/PointLight_64x.png");
                }

                if (ImGui::MenuItem("Pawn Icon")) {
                    Context->SpawnBillboard("/Engine/Editor/Icon/S_Pawn.png");
                }

                if (ImGui::MenuItem("Particle System Icon")) {
                    Context->SpawnBillboard("/Engine/Editor/Icon/S_Emitter.png");
                }

                if (ImGui::MenuItem("Sky Atmosphere Icon")) {
                    Context->SpawnBillboard("/Engine/Editor/Icon/S_SkyAtmosphere.png");
                }

                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }

        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##Search", "검색", mSearch.data(), mSearch.size());

        const ImGuiTextFilter Filter{mSearch.data()};
        const auto Actors{Context->GetActorRefs()};
        const Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Selection{Context->GetSelectedActors()};
        ImGuiStorage& ExpansionState{ *ImGui::GetStateStorage() };
        const auto Visible{ BuildActorRows(*Context, Actors, Filter, ExpansionState) };

        const bool CanEdit{Context->CanEditWorld()};
        bool DuplicateRequested{};
        bool DeleteRequested{};
        constexpr ImGuiTableFlags TableFlags{ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY};
        const ImGuiWindow* Window{ImGui::GetCurrentWindow()};
        const float ContentX{ImGui::GetCursorPosX()};
        const float TableWidth{Window->InnerRect.GetWidth()};
        ImRect TableClip{Window->InnerRect};

        TableClip.ClipWith(Window->OuterRectClipped);
        ImGui::SetCursorScreenPos(ImVec2{Window->InnerRect.Min.x, ImGui::GetCursorScreenPos().y});
        ImGui::PushClipRect(TableClip.Min, TableClip.Max, false);

        if (ImGui::BeginTable("Actors", 3, TableFlags, {TableWidth, -ImGui::GetTextLineHeightWithSpacing()})) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##Visibility", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoHide, ImGui::GetFontSize());
            ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("DirectionalLightActor").x);
            ImGui::TableHeadersRow();

            for (const auto& [Handle, Indent, ExpansionId] : Visible) {
                Engine::AActor* Actor{CoreUObject::Cast<Engine::AActor>(Context->ResolveObject(Handle))};

                if (Actor == nullptr) {
                    continue;
                }

                ImGui::PushID(Actor);

                const bool Selected{std::find(Selection.begin(), Selection.end(), Handle) != Selection.end()};

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                // 행 전체를 덮는 라벨보다 먼저 제출해야 버튼이 클릭을 먼저 받습니다.
                ImGui::BeginDisabled(!CanEdit);

                if (DrawVisibilityButton(Actor->IsVisible())) {
                    const bool ActorVisible{!Actor->IsVisible()};

                    Context->SetActorsVisible(Selected ? Selection : Core::TArray<CoreUObject::TObjectRef<Engine::AActor>>{Handle}, ActorVisible);
                }

                ImGui::EndDisabled();

                const float RowAlpha{ImGui::GetStyle().Alpha * (Actor->IsVisible() ? 1.0f : 0.5f)};
                bool Expanded{ !ExpansionId || ExpansionState.GetBool(*ExpansionId, true) };

                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, RowAlpha);
                ObjectRow::DrawLabel(*Context, Actor, Selected, Indent, ExpansionId ? &Expanded : nullptr);
                ImGui::PopStyleVar();

                const bool Toggled{ ExpansionId && ImGui::IsItemToggledOpen() };

                if (Toggled) {
                    ExpansionState.SetBool(*ExpansionId, Expanded);
                }

                if (!Toggled && ImGui::IsItemClicked()) {
                    const auto Anchor{Selection.empty() ? Visible.end() : std::ranges::find(Visible, Selection.back(), &FOutlinerRow::mActor)};

                    if (ImGui::GetIO().KeyCtrl) {
                        Context->ToggleActorSelection(Handle);
                    } else if (ImGui::GetIO().KeyShift && Anchor != Visible.end()) {
                        // 클릭한 액터가 마지막에 오도록 앵커 방향으로 범위를 수집합니다.
                        const auto Clicked{std::ranges::find(Visible, Handle, &FOutlinerRow::mActor)};
                        Core::TArray<CoreUObject::TObjectRef<Engine::AActor>> Range{};
                        const auto Step{Anchor <= Clicked ? 1 : -1};

                        for (auto Row{Anchor}; Row != Clicked; Row += Step) {
                            Range.push_back(Row->mActor);
                        }

                        Range.push_back(Handle);
                        Context->SetSelectedActors(Range);
                    } else {
                        Context->SelectActor(Handle);
                    }
                }

                if (!Toggled && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    Context->FocusActor(Handle);
                    Context->OpenInSequencer(CoreUObject::Cast<Engine::ALevelSequenceActor>(Context->ResolveObject(Handle)));
                }

                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !Selected) {
                    Context->SelectActor(Handle);
                }

                if (ImGui::BeginPopupContextItem("ActorMenu")) {
                    DuplicateRequested = ImGui::MenuItem("Duplicate", nullptr, false, CanEdit) || DuplicateRequested;
                    DeleteRequested = ImGui::MenuItem("Delete", "Del", false, CanEdit) || DeleteRequested;

                    Engine::USceneComponent* Root{Actor->GetRootComponent()};

                    if (Root != nullptr && Root->GetAttachParent() != nullptr && ImGui::MenuItem("Detach from Parent", nullptr, false, Context->CanAttachComponent(Root, nullptr))) {
                        Context->AttachComponent(Root, nullptr);
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Use as PIE Camera", nullptr, false, CanEdit && Context->GetSelectedCamera() != nullptr)) {
                        Context->UseSelectedCamera();
                    }

                    ImGui::EndPopup();
                }

                ObjectRow::DrawAttachmentDragDrop(*Context, Actor);

                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, RowAlpha);
                ObjectRow::DrawType(Actor);
                ImGui::PopStyleVar();
                ImGui::PopID();
            }

            if (Visible.empty() && Filter.IsActive()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("검색 결과가 없습니다.");
            }

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
                Context->ClearSelection();
            }

            ImGui::EndTable();
        }

        ImGui::PopClipRect();
        ImGui::SetCursorPosX(ContentX);

        const bool DeletePressed{CanEdit && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_Delete, false)};

        // 순회 중인 핸들 목록이 바뀌지 않도록 테이블을 닫은 뒤 적용합니다.
        if (DuplicateRequested) {
            Context->DuplicateSelectedActors();
        }

        if (DeleteRequested) {
            Context->DestroySelectedActor();
        } else if (DeletePressed) {
            Context->DestroySelection();
        }

        const Engine::UCameraComponent* Camera{Context->GetWorld() != nullptr ? Context->GetWorld()->GetActiveCamera() : nullptr};

        ImGui::TextDisabled("%zu actors (%zu selected) | PIE Camera: %s", Actors.size(), Selection.size(), Camera != nullptr ? Camera->GetOwner()->GetName().c_str() : "None");
    }

}
