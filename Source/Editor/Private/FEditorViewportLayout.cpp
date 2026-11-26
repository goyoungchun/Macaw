/**
 * @file FEditorViewportLayout.cpp
 * @brief 뷰포트 분할 프리셋·최대화·활성 입력과 ImGui 설정 저장을 관리합니다.
 * @ingroup Editor
 */

#include "FEditorViewportLayout.h"
#include "EditorStyle.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorViewport.h"
#include "Editor/FPlayInEditorSession.h"
#include "RenderCore/IRendererModule.h"
#include "Core/Types.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Editor;

FEditorViewportLayout::FEditorViewportLayout(FEditorContext* Context, FEditorViewport* Viewport, FPlayInEditorSession* PlaySession)
	: mContext{Context},
	  mViewport{Viewport},
	  mPlaySession{PlaySession} {
}

FEditorViewportLayout::~FEditorViewportLayout() {
    Shutdown();
}

const FEditorViewportLayout::FPresetDefinition& FEditorViewportLayout::GetDefinition(EEditorViewportLayoutPreset Preset) {
    const auto Leaf{[](Index_t Slot) {
        return FLayoutNode{-1, -1, Slot, 0, false};
    }};
    const auto Split{[](bool Columns, Index_t Ratio, Int32 First, Int32 Second) {
        return FLayoutNode{First, Second, 0, Ratio, Columns};
    }};
    static const Core::TStaticArray<FPresetDefinition, 12> Definitions{{{"단일 뷰포트", 1, {Leaf(0)}, {0.5f, 0.5f, 0.5f}},
        {"좌우 2분할", 2, {Split(true, 0, 1, 2), Leaf(0), Leaf(1)}, {0.5f, 0.5f, 0.5f}},
        {"상하 2분할", 2, {Split(false, 1, 1, 2), Leaf(0), Leaf(1)}, {0.5f, 0.5f, 0.5f}},
        {"왼쪽 큰 3분할", 3, {Split(true, 0, 1, 2), Leaf(0), Split(false, 1, 3, 4), Leaf(1), Leaf(2)}, {0.66f, 0.5f, 0.5f}},
        {"오른쪽 큰 3분할", 3, {Split(true, 0, 1, 2), Split(false, 1, 3, 4), Leaf(0), Leaf(1), Leaf(2)}, {0.34f, 0.5f, 0.5f}},
        {"위쪽 큰 3분할", 3, {Split(false, 1, 1, 2), Leaf(0), Split(true, 0, 3, 4), Leaf(1), Leaf(2)}, {0.5f, 0.66f, 0.5f}},
        {"아래쪽 큰 3분할", 3, {Split(false, 1, 1, 2), Split(true, 0, 3, 4), Leaf(0), Leaf(1), Leaf(2)}, {0.5f, 0.34f, 0.5f}},
        {"균등 4분할", 4, {Split(true, 0, 1, 2), Split(false, 1, 3, 4), Split(false, 1, 5, 6), Leaf(0), Leaf(2), Leaf(1), Leaf(3)}, {0.5f, 0.5f, 0.5f}},
        {"왼쪽 큰 4분할", 4, {Split(true, 0, 1, 2), Leaf(0), Split(false, 1, 3, 4), Leaf(1), Split(false, 2, 5, 6), Leaf(2), Leaf(3)}, {0.66f, 0.333333f, 0.5f}},
        {"오른쪽 큰 4분할", 4, {Split(true, 0, 1, 2), Split(false, 1, 3, 4), Leaf(0), Leaf(1), Split(false, 2, 5, 6), Leaf(2), Leaf(3)}, {0.34f, 0.333333f, 0.5f}},
        {"위쪽 큰 4분할", 4, {Split(false, 1, 1, 2), Leaf(0), Split(true, 0, 3, 4), Leaf(1), Split(true, 2, 5, 6), Leaf(2), Leaf(3)}, {0.333333f, 0.66f, 0.5f}},
        {"아래쪽 큰 4분할", 4, {Split(false, 1, 1, 2), Split(true, 0, 3, 4), Leaf(0), Leaf(1), Split(true, 2, 5, 6), Leaf(2), Leaf(3)}, {0.333333f, 0.34f, 0.5f}}}};
    const Index_t Index{static_cast<Index_t>(Preset)};
    return Definitions[Index < Definitions.size() ? Index : 0];
}

const char* FEditorViewportLayout::GetPresetName(EEditorViewportLayoutPreset Preset) {
    return GetDefinition(Preset).mName;
}

bool FEditorViewportLayout::Initialize(RenderCore::IRendererModule* Renderer) {
    if (Renderer == nullptr || mContext == nullptr || mViewport == nullptr || mPlaySession == nullptr || mOrthographicViewports[0] != nullptr) {
        return false;
    }

    const Core::TStaticArray<EEditorViewportType, 3> Types{EEditorViewportType::Top, EEditorViewportType::Front, EEditorViewportType::Right};
    for (Index_t Index{}; Index < mOrthographicViewports.size(); ++Index) {
        mOrthographicViewports[Index] = std::make_unique<FEditorViewport>(mContext, "Orthographic" + std::to_string(Index));
        mOrthographicViewports[Index]->GetClient()->SetViewType(Types[Index]);
        if (!mOrthographicViewports[Index]->Initialize(Renderer)) {
            Shutdown();
            return false;
        }
    }

    if (ImGui::GetCurrentContext() != nullptr) {
        ImGuiSettingsHandler Handler{};
        Handler.TypeName = "MacawViewport";
        Handler.TypeHash = ImHashStr(Handler.TypeName);
        Handler.UserData = this;
        Handler.ReadOpenFn = ReadSettingsOpen;
        Handler.ReadLineFn = ReadSettingsLine;
        Handler.WriteAllFn = WriteSettings;
        ImGui::AddSettingsHandler(&Handler);
    }

    return true;
}

void FEditorViewportLayout::Shutdown() {
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGuiSettingsHandler* Handler{ImGui::FindSettingsHandler("MacawViewport")};
        if (Handler != nullptr && Handler->UserData == this) {
            ImGui::RemoveSettingsHandler("MacawViewport");
        }
    }

    for (auto& Viewport : mOrthographicViewports) {
        Viewport.reset();
    }

    mVisible.fill(false);
    mActiveViewport = 0;
    mSingleViewport = 0;
    mPreset = EEditorViewportLayoutPreset::FourGrid;
    mMaximized = true;
    mRatios = {0.5f, 0.5f, 0.5f};
}

FEditorViewport* FEditorViewportLayout::GetViewport(Index_t Index) const {
    return Index == 0 ? mViewport : Index <= mOrthographicViewports.size() ? mOrthographicViewports[Index - 1].get()
                                                                           : nullptr;
}

FEditorViewport* FEditorViewportLayout::GetActiveViewport() const {
    return mPlaySession->IsActive() && !mPlaySession->IsSimulating() ? mViewport : GetViewport(mActiveViewport);
}

void FEditorViewportLayout::UpdateViewportContexts() {
    FEditorContext* Context{mPlaySession->IsActive() ? mPlaySession->GetInspectionContext() : mContext};
    mViewport->GetClient()->SetEditorContext(mPlaySession->IsSimulating() ? Context : mContext);

    for (const auto& Viewport : mOrthographicViewports) {
        if (Viewport != nullptr) {
            Viewport->GetClient()->SetEditorContext(Context);
        }
    }
}

bool FEditorViewportLayout::CanChangeLayout() const {
    if (mPlaySession->IsActive() && !mPlaySession->IsSimulating()) {
        return false;
    }

    for (Index_t Index{}; Index < mVisible.size(); ++Index) {
        FEditorViewport* Viewport{GetViewport(Index)};
        if (Viewport != nullptr && (Viewport->IsMouseCaptured() || Viewport->GetClient()->GetGizmo().IsDragging())) {
            return false;
        }
    }

    return true;
}

void FEditorViewportLayout::SetPreset(EEditorViewportLayoutPreset Preset) {
    if (!CanChangeLayout() || static_cast<Index_t>(Preset) >= static_cast<Index_t>(EEditorViewportLayoutPreset::Count)) {
        return;
    }

    if (Preset == EEditorViewportLayoutPreset::Single) {
        mActiveViewport = 0;
        mSingleViewport = 0;
        mViewport->GetClient()->SetViewType(EEditorViewportType::Perspective);
        mMaximized = true;
    } else {
        if (mPreset != Preset) {
            mRatios = GetDefinition(Preset).mRatios;
        }

        mPreset = Preset;
        mMaximized = false;
        if (mActiveViewport >= GetDefinition(Preset).mCount) {
            mActiveViewport = 0;
        }
    }

    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::MarkIniSettingsDirty();
    }
}

EEditorViewportLayoutPreset FEditorViewportLayout::GetPreset() const {
    return mMaximized ? EEditorViewportLayoutPreset::Single : mPreset;
}

bool FEditorViewportLayout::IsMaximized() const {
    return mMaximized;
}

void FEditorViewportLayout::ToggleMaximize(Index_t Index) {
    if (!CanChangeLayout() || GetViewport(Index) == nullptr) {
        return;
    }

    mActiveViewport = Index;
    mSingleViewport = Index;
    if (mMaximized) {
        RestoreViewports();
    } else {
        mMaximized = true;
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::MarkIniSettingsDirty();
        }
    }
}

void FEditorViewportLayout::RestoreViewports() {
    if (CanChangeLayout()) {
        mMaximized = false;
        if (mActiveViewport >= GetDefinition(mPreset).mCount) {
            mActiveViewport = 0;
        }

        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::MarkIniSettingsDirty();
        }
    }
}

void FEditorViewportLayout::DrawPreviewNode(const FPresetDefinition& Definition, Index_t NodeIndex, const FVector4& Bounds, Uint32 Color) const {
    const FLayoutNode& Node{Definition.mNodes[NodeIndex]};
    if (Node.mFirst < 0) {
        ImGui::GetWindowDrawList()->AddRect({Bounds.x, Bounds.y}, {Bounds.x + Bounds.z, Bounds.y + Bounds.w}, Color, 1.0f, 0, 1.5f);
        return;
    }

    const float Ratio{Definition.mRatios[Node.mRatio]};
    if (Node.mColumns) {
        const float First{(Bounds.z - 2.0f) * Ratio};
        DrawPreviewNode(Definition, Node.mFirst, {Bounds.x, Bounds.y, First, Bounds.w}, Color);
        DrawPreviewNode(Definition, Node.mSecond, {Bounds.x + First + 2.0f, Bounds.y, Bounds.z - First - 2.0f, Bounds.w}, Color);
    } else {
        const float First{(Bounds.w - 2.0f) * Ratio};
        DrawPreviewNode(Definition, Node.mFirst, {Bounds.x, Bounds.y, Bounds.z, First}, Color);
        DrawPreviewNode(Definition, Node.mSecond, {Bounds.x, Bounds.y + First + 2.0f, Bounds.z, Bounds.w - First - 2.0f}, Color);
    }
}

void FEditorViewportLayout::DrawPresetButton(EEditorViewportLayoutPreset Preset) {
    const float Scale{ImGui::GetFontSize() / 16.0f};
    const bool Selected{GetPreset() == Preset};
    ImGui::PushID(static_cast<int>(Preset));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(Selected ? ImGuiCol_HeaderActive : ImGuiCol_PopupBg));
    const bool Clicked{EditorStyle::Button("##Preset", {46.0f * Scale, 42.0f * Scale})};
    ImGui::PopStyleColor();
    const ImVec2 Minimum{ImGui::GetItemRectMin()};
    DrawPreviewNode(GetDefinition(Preset), 0, {Minimum.x + 9.0f * Scale, Minimum.y + 8.0f * Scale, 28.0f * Scale, 26.0f * Scale}, ImGui::GetColorU32(Selected ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::SetItemTooltip("%s", GetPresetName(Preset));
    if (Clicked) {
        SetPreset(Preset);
        ImGui::CloseCurrentPopup();
    }

    ImGui::PopID();
}

void FEditorViewportLayout::DrawLayoutControls(Index_t Index) {
    const float Scale{ImGui::GetFontSize() / 16.0f};
    const EditorStyle::EIcon Icon{mMaximized ? EditorStyle::EIcon::QuadView : EditorStyle::EIcon::Maximize};
    const char* ActionTooltip{mMaximized ? "분할 뷰포트 복구" : "원근 뷰포트로 복귀"};
    const auto Pressed{EditorStyle::SplitIconButton("ViewportLayout", Icon, ActionTooltip, "뷰포트 레이아웃 프리셋", {.mMenuFirst{true}})};

    if (Pressed == EditorStyle::ESplitIconPress::Action) {
        if (mMaximized) {
            RestoreViewports();
        } else {
            SetPreset(EEditorViewportLayoutPreset::Single);
        }
    }

    if (Pressed == EditorStyle::ESplitIconPress::Menu) {
        mActiveViewport = Index;
        ImGui::OpenPopup("ViewportLayoutPresets");
    }

    const ImVec2 Minimum{ImGui::GetItemRectMin()};
    const ImVec2 Maximum{ImGui::GetItemRectMax()};
    ImGui::SetNextWindowPos({Minimum.x, Maximum.y + 4.0f * Scale}, ImGuiCond_Appearing);
    if (EditorStyle::BeginPopup("ViewportLayoutPresets")) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0f * Scale, 8.0f * Scale});
        Index_t PreviousCount{};
        for (Index_t Value{}; Value < static_cast<Index_t>(EEditorViewportLayoutPreset::Count); ++Value) {
            const auto Preset{static_cast<EEditorViewportLayoutPreset>(Value)};
            const Index_t Count{GetDefinition(Preset).mCount};
            if (Count != PreviousCount) {
                if (PreviousCount != 0) {
                    ImGui::Spacing();
                }
                const Core::FString Label{Count == 1 ? "단일 패널" : std::to_string(Count) + " 패널"};
                ImGui::SeparatorText(Label.c_str());
                PreviousCount = Count;
            } else {
                ImGui::SameLine();
            }
            DrawPresetButton(Preset);
        }

        ImGui::Separator();
        if (ImGui::MenuItem("현재 뷰포트 최대화", nullptr, mMaximized, !mMaximized)) {
            ToggleMaximize(Index);
        }

        if (ImGui::MenuItem("모든 뷰포트 복구", nullptr, !mMaximized, mMaximized)) {
            RestoreViewports();
        }

        ImGui::PopStyleVar();
        EditorStyle::EndPopup();
    }
}

void FEditorViewportLayout::DrawViewport(Index_t Index, const Core::FInput& Input, float DeltaTime, FEditorViewport*& CapturedViewport) {
    FEditorViewport* Viewport{GetViewport(Index)};
    if (Viewport == nullptr) {
        return;
    }

    FEditorViewportClient* Client{Viewport->GetClient()};
    const bool AllowInput{CapturedViewport == nullptr || CapturedViewport == Viewport};
    const auto SetViewType{[this, Index, Client](EEditorViewportType Type) {
        if (CanChangeLayout()) {
            Client->SetViewType(Type);
            mActiveViewport = Index;
            ImGui::MarkIniSettingsDirty();
        }
    }};
    const auto DrawLayout{[this, Index] {
        ImGui::BeginDisabled(!CanChangeLayout());
        DrawLayoutControls(Index);
        ImGui::EndDisabled();
    }};

    Viewport->DrawContents(Input, DeltaTime, AllowInput, SetViewType, DrawLayout);
    mVisible[Index] = true;
    if (Viewport->IsMouseCaptured()) {
        CapturedViewport = Viewport;
        mActiveViewport = Index;
    } else if (AllowInput && (!mMaximized || Index == mSingleViewport) && ImGui::IsWindowFocused()) {
        mActiveViewport = Index;
    }
}

void FEditorViewportLayout::DrawLayoutNode(const FPresetDefinition& Definition, Index_t NodeIndex, const FVector4& Bounds, const Core::FInput& Input, float DeltaTime, FEditorViewport*& CapturedViewport) {
    const FLayoutNode& Node{Definition.mNodes[NodeIndex]};
    if (Node.mFirst < 0) {
        const Index_t SingleViewport{mPlaySession->IsActive() && !mPlaySession->IsSimulating() ? 0 : mSingleViewport};
        const Index_t Slot{Definition.mCount == 1 ? SingleViewport : Node.mSlot};
        ImGui::SetCursorScreenPos({Bounds.x, Bounds.y});
        ImGui::PushID(static_cast<int>(Slot));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
        const bool Visible{ImGui::BeginChild("ViewportPane", {std::max(1.0f, Bounds.z), std::max(1.0f, Bounds.w)}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs)};
        ImGui::PopStyleVar();
        if (Visible) {
            DrawViewport(Slot, Input, DeltaTime, CapturedViewport);
        }

        ImGui::EndChild();
        ImGui::PopID();
        return;
    }

    const float Scale{ImGui::GetFontSize() / 16.0f};
    const float Splitter{6.0f * Scale};
    const float Total{std::max(2.0f, (Node.mColumns ? Bounds.z : Bounds.w) - Splitter)};
    const float Minimum{std::min((Node.mColumns ? 100.0f : 75.0f) * Scale, Total * 0.5f)};
    float First{std::clamp(Total * mRatios[Node.mRatio], Minimum, Total - Minimum)};
    float Second{Total - First};
    const ImVec2 Position{Node.mColumns ? Bounds.x + First : Bounds.x, Node.mColumns ? Bounds.y : Bounds.y + First};
    const ImVec2 End{Node.mColumns ? Position.x + Splitter : Bounds.x + Bounds.z, Node.mColumns ? Bounds.y + Bounds.w : Position.y + Splitter};
    ImGui::SetCursorScreenPos(Position);
    ImGui::PushID(static_cast<int>(NodeIndex));
    ImGui::BeginDisabled(CapturedViewport != nullptr || !CanChangeLayout());
    if (ImGui::SplitterBehavior(ImRect{Position, End}, ImGui::GetID("ViewportSplitter"), Node.mColumns ? ImGuiAxis_X : ImGuiAxis_Y, &First, &Second, Minimum, Minimum)) {
        mRatios[Node.mRatio] = First / Total;
        ImGui::MarkIniSettingsDirty();
    }

    ImGui::EndDisabled();
    ImGui::PopID();
    if (Node.mColumns) {
        DrawLayoutNode(Definition, Node.mFirst, {Bounds.x, Bounds.y, First, Bounds.w}, Input, DeltaTime, CapturedViewport);
        DrawLayoutNode(Definition, Node.mSecond, {Bounds.x + First + Splitter, Bounds.y, Second, Bounds.w}, Input, DeltaTime, CapturedViewport);
    } else {
        DrawLayoutNode(Definition, Node.mFirst, {Bounds.x, Bounds.y, Bounds.z, First}, Input, DeltaTime, CapturedViewport);
        DrawLayoutNode(Definition, Node.mSecond, {Bounds.x, Bounds.y + First + Splitter, Bounds.z, Second}, Input, DeltaTime, CapturedViewport);
    }
}

void FEditorViewportLayout::BeginFrame() {
    mVisible.fill(false);
}

void FEditorViewportLayout::DrawContents(const Core::FInput& Input, float DeltaTime) {
    UpdateViewportContexts();
    FEditorViewport* CapturedViewport{};

    for (Index_t Index{}; Index < mVisible.size(); ++Index) {
        if (FEditorViewport* Viewport{GetViewport(Index)}; Viewport != nullptr && Viewport->IsMouseCaptured()) {
            CapturedViewport = Viewport;
            break;
        }
    }

    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const ImVec2 Available{ImGui::GetContentRegionAvail()};
    const FPresetDefinition& Definition{GetDefinition(GetPreset())};

    if (Available.x > 1.0f && Available.y > 1.0f) {
        DrawLayoutNode(Definition, 0, {Origin.x, Origin.y, Available.x, Available.y}, Input, DeltaTime, CapturedViewport);
    }

    ImGui::SetCursorScreenPos(Origin);
    ImGui::Dummy(Available);
}

void FEditorViewportLayout::Draw(float DeltaTime) {
    UpdateViewportContexts();
    for (Index_t Index{}; Index < mVisible.size(); ++Index) {
        if (mVisible[Index]) {
            GetViewport(Index)->Draw();
        } else {
            GetViewport(Index)->ReleaseInput();
        }
    }

    const FEditorContext* Inspection{mPlaySession->IsActive() ? mPlaySession->GetInspectionContext() : mContext};
    for (Index_t Index{}; Index < mVisible.size(); ++Index) {
        if (mVisible[Index]) {
            GetViewport(Index)->DrawStatistics(DeltaTime, Inspection->GetWorld(), mContext->GetRenderer()->GetStats());
        }
    }
}

float FEditorViewportLayout::GetHorizontalRatio() const {
    return mRatios[0];
}

float FEditorViewportLayout::GetVerticalRatio() const {
    return mRatios[1];
}

bool FEditorViewportLayout::SetSplitRatios(float Horizontal, float Vertical) {
    if (!std::isfinite(Horizontal) || !std::isfinite(Vertical) || Horizontal < 0.01f || Horizontal > 0.99f || Vertical < 0.01f || Vertical > 0.99f) {
        return false;
    }

    if (mRatios[0] != Horizontal || mRatios[1] != Vertical) {
        mRatios[0] = Horizontal;
        mRatios[1] = Vertical;
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::MarkIniSettingsDirty();
        }
    }

    return true;
}

void* FEditorViewportLayout::ReadSettingsOpen(ImGuiContext*, ImGuiSettingsHandler* Handler, const char* Name) {
    return std::strcmp(Name, "Layout") == 0 ? Handler->UserData : nullptr;
}

void FEditorViewportLayout::ReadSettingsLine(ImGuiContext*, ImGuiSettingsHandler*, void* Entry, const char* Line) {
    auto* Layout{static_cast<FEditorViewportLayout*>(Entry)};
    float Ratio{};
    int Value{};
    unsigned int Index{};

    if (sscanf_s(Line, "HorizontalRatio=%f", &Ratio) == 1 && std::isfinite(Ratio) && Ratio >= 0.01f && Ratio <= 0.99f) {
        Layout->mRatios[0] = Ratio;
    } else if (sscanf_s(Line, "VerticalRatio=%f", &Ratio) == 1 && std::isfinite(Ratio) && Ratio >= 0.01f && Ratio <= 0.99f) {
        Layout->mRatios[1] = Ratio;
    } else if (sscanf_s(Line, "ThirdRatio=%f", &Ratio) == 1 && std::isfinite(Ratio) && Ratio >= 0.01f && Ratio <= 0.99f) {
        Layout->mRatios[2] = Ratio;
    } else if (sscanf_s(Line, "Preset=%d", &Value) == 1 && Value > 0 && Value < static_cast<int>(EEditorViewportLayoutPreset::Count)) {
        Layout->mPreset = static_cast<EEditorViewportLayoutPreset>(Value);
    } else if (sscanf_s(Line, "Maximized=%d", &Value) == 1 && (Value == 0 || Value == 1)) {
        Layout->mMaximized = Value != 0;
    } else if (sscanf_s(Line, "QuadView=%d", &Value) == 1 && (Value == 0 || Value == 1)) {
        Layout->mPreset = EEditorViewportLayoutPreset::FourGrid;
        Layout->mMaximized = Value == 0;
    } else if (sscanf_s(Line, "SingleViewport=%d", &Value) == 1 && Value >= 0 && Value < 4) {
        Layout->mSingleViewport = static_cast<Index_t>(Value);
        Layout->mActiveViewport = Layout->mMaximized ? Layout->mSingleViewport : 0;
    } else if (sscanf_s(Line, "Viewport%uType=%d", &Index, &Value) == 2 && Index < 4 && Value >= static_cast<int>(EEditorViewportType::Perspective) && Value <= static_cast<int>(EEditorViewportType::Right)) {
        if (FEditorViewport* Viewport{Layout->GetViewport(Index)}; Viewport != nullptr) {
            Viewport->GetClient()->SetViewType(static_cast<EEditorViewportType>(Value));
        }
    }
}

void FEditorViewportLayout::WriteSettings(ImGuiContext*, ImGuiSettingsHandler* Handler, ImGuiTextBuffer* Buffer) {
    const auto* Layout{static_cast<const FEditorViewportLayout*>(Handler->UserData)};
    Buffer->appendf("[MacawViewport][Layout]\nPreset=%d\nMaximized=%d\nHorizontalRatio=%.9g\nVerticalRatio=%.9g\nThirdRatio=%.9g\nSingleViewport=%zu\n", static_cast<int>(Layout->mPreset), Layout->mMaximized ? 1 : 0, Layout->mRatios[0], Layout->mRatios[1], Layout->mRatios[2], Layout->mSingleViewport);

    for (Index_t Index{}; Index < 4; ++Index) {
        if (FEditorViewport* Viewport{Layout->GetViewport(Index)}; Viewport != nullptr) {
            Buffer->appendf("Viewport%zuType=%d\n", Index, static_cast<int>(Viewport->GetClient()->GetViewType()));
        }
    }

    Buffer->append("\n");
}
