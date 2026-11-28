/**
 * @file FEditorWorkspace.cpp
 * @brief 최상위 탭과 레벨 편집기 내부 패널의 도킹 배치·포커스·배치 버전을 관리합니다.
 * @ingroup Editor
 */

#include "FEditorWorkspace.h"
#include "Editor/FImGuiUIScale.h"
#include "Editor/FEditorTabManager.h"
#include "Editor/FEditorTab.h"
#include "Editor/FEditorToolkitHost.h"
#include "Core/FilePath.h"
#include "EditorStyle.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorViewport.h"
#include <imgui_internal.h>
#include <algorithm>
#include <utility>

using namespace Editor;

FEditorWorkspace::FEditorWorkspace(FEditorContext* Context, FEditorTab& LevelTab, FImGuiUIScale& UIScale)
	: mContext{Context},
	  mLevelTab{LevelTab},
	  mTabs{LevelTab.GetToolkitHost().GetTabManager()},
	  mUIScale{UIScale} {
    mLevelTab.GetToolkitHost().SetDockSpaceName("LevelDockSpace");
    mLevelTab.GetToolkitHost().SetLayoutBuilder([this](Uint32 DockSpace) {
        BuildLevelLayout(DockSpace);
    });
}

FEditorWorkspace::~FEditorWorkspace() = default;

void FEditorWorkspace::SetToolbar(std::function<void()> Handler) {
    mLevelTab.GetToolkitHost().SetToolbar([this, Handler = std::move(Handler)]() {
        if (Handler) {
            Handler();
        }

        UpdateLeftDock();
    });
}

void FEditorWorkspace::SetLayoutChanged(std::function<void()> Handler) {
    mLayoutChanged = std::move(Handler);
}

Uint32 FEditorWorkspace::GetLayoutVersion() const {
    return mLayoutVersion;
}

void FEditorWorkspace::SetLayoutVersion(Uint32 Version) {
    mLayoutVersion = Version;
}

void FEditorWorkspace::BuildLayout() {
    constexpr Uint32 CurrentLayout{5};
    const float Scale{mUIScale.GetScale()};
    const bool Upgrade{mLayoutVersion < CurrentLayout};

    // 루트 도킹 영역에는 최상위 탭이 붙고, 레벨 패널은 레벨 에디터 안쪽 도킹 영역에 붙습니다.
    ImGuiWindowClass TopLevelTabs{};

    TopLevelTabs.ClassId = EditorStyle::TopLevelTabClassId;
    TopLevelTabs.DockingAllowUnclassed = false;

    // 언리얼의 탭처럼 닫기·메뉴 버튼이 없는 넉넉한 높이의 탭 표시줄을 사용합니다.
    constexpr ImGuiDockNodeFlags TabBarFlags{ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton};

    EditorStyle::PushTopLevelTabStyle();

    const ImGuiID Root{ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), TabBarFlags, &TopLevelTabs)};
    ImGuiDockNode* RootNode{ImGui::DockBuilderGetNode(Root)};

    if (RootNode != nullptr && ImGui::DockNodeBeginAmendTabBar(RootNode)) {
        ImGui::SetNextItemWidth(std::max(1.0f, EditorStyle::BrandAreaWidth * Scale - ImGui::GetStyle().FramePadding.x - ImGui::GetStyle().WindowBorderSize - ImGui::GetStyle().ItemInnerSpacing.x));
        ImGui::TabItemButton("##BrandSpace", ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoTooltip | ImGuiTabItemFlags_NoReorder);
        ImGui::DockNodeEndAmendTabBar();
    }

    EditorStyle::PopTopLevelTabStyle();

    mRootDock = Root;

    if (Upgrade) {
        ImGui::DockBuilderRemoveNodeChildNodes(Root);
    }

    mLevelTab.SetName(EditorStyle::TabIconPadding + (mContext->GetScenePath().empty() ? Core::FString{"Untitled"} : Core::FilePath::ToUtf8(mContext->GetScenePath().stem())) + (mContext->IsSceneDirty() ? "*" : "") + "###LevelEditor");
    ImGui::SetNextWindowDockID(Root, ImGuiCond_Once);

    if (Upgrade) {
        mLevelTab.GetToolkitHost().RequestLayoutReset();
        mTabs.RestoreDefaults();
        mLayoutVersion = CurrentLayout;

        if (mLayoutChanged) {
            mLayoutChanged();
        }
    }
}

void FEditorWorkspace::BuildLevelLayout(Uint32 DockSpace) {
    mLevelDock = DockSpace;
    const float Scale{mUIScale.GetScale()};
    const ImVec2 Available{ImGui::GetContentRegionAvail()};
    const float RightWidth{std::min(400.0f * Scale, Available.x * 0.35f)};
    const float LeftWidth{std::min(260.0f * Scale, (Available.x - RightWidth) * 0.3f)};
    const float BottomHeight{std::min(300.0f * Scale, Available.y * 0.4f)};
    ImGuiID Center{DockSpace};
    const ImGuiID Right{ImGui::DockBuilderSplitNode(Center, ImGuiDir_Right, RightWidth / std::max(1.0f, Available.x), nullptr, &Center)};
    mLeftDock = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Left, LeftWidth / std::max(1.0f, Available.x - RightWidth), nullptr, &Center);
    mBottomDock = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Down, BottomHeight / std::max(1.0f, Available.y), nullptr, &Center);
    ImGuiID Outliner{};
    const ImGuiID Details{ImGui::DockBuilderSplitNode(Right, ImGuiDir_Down, 0.62f, nullptr, &Outliner)};
    mTabs.ApplyLayout(mLeftDock, Outliner, Details, mBottomDock, Center);
}

void FEditorWorkspace::BeginTopLevelTab() {
    ImGuiWindowClass TopLevelTabs{};

    TopLevelTabs.ClassId = EditorStyle::TopLevelTabClassId;
    TopLevelTabs.DockingAllowUnclassed = false;

    ImGui::SetNextWindowClass(&TopLevelTabs);
    ImGui::SetNextWindowDockID(mRootDock, ImGuiCond_Appearing);
}

void FEditorWorkspace::EndTopLevelTab() {
    // 에디터가 닫혀 있어 창을 만들지 않았다면 남은 예약이 다음 창에 적용되지 않도록 지웁니다.
    ImGui::GetCurrentContext()->NextWindowData.ClearFlags();
}

void FEditorWorkspace::FocusContentBrowser() {
    if (const FEditorTab* Tab{mTabs.FindTab("Content")}; Tab != nullptr && Tab->GetRole() == EEditorTabRole::Panel) {
        FocusLevelEditor();
    }

    mTabs.TryInvokeTab("Content");
}

void FEditorWorkspace::FocusLevelEditor() {
    mLevelTab.Open();
}

bool FEditorWorkspace::IsLevelEditorFocused() const {
    return mLevelTab.IsFocused();
}

void FEditorWorkspace::UpdateLeftDock() {
    Core::TArray<const FEditorTab*> Tabs{mTabs.GetVisibleTabs(EEditorDockRegion::Left)};
    const ImGuiID LevelDock{mLevelTab.GetToolkitHost().GetDockSpaceId()};
    ImGuiDockNode* LevelNode{ImGui::DockBuilderGetNode(LevelDock)};

    if (Tabs.empty() || LevelNode == nullptr) {
        return;
    }

    const auto IsPanelDock{[LevelNode](ImGuiDockNode* Node) {
        return Node != nullptr && Node->IsLeafNode() && !Node->IsCentralNode() && ImGui::DockNodeGetRootNode(Node) == LevelNode;
    }};
    ImGuiDockNode* Left{ImGui::DockBuilderGetNode(mLeftDock)};
    Left = IsPanelDock(Left) ? Left : nullptr;

    std::erase_if(Tabs, [&IsPanelDock, &Left](const FEditorTab* Tab) {
        Tab->PrepareDockedFocus();

        const char* Name{Tab->GetName().c_str()};
        const ImGuiWindow* Window{ImGui::FindWindowByName(Name)};
        const ImGuiWindowSettings* Settings{Window == nullptr ? ImGui::FindWindowSettingsByID(ImHashStr(Name)) : nullptr};
        const ImGuiID DockId{Window != nullptr ? Window->DockId : Settings != nullptr ? Settings->DockId : 0};
        ImGuiDockNode* Node{ImGui::DockBuilderGetNode(DockId)};

        if (IsPanelDock(Node)) {
            if (Left == nullptr) {
                Left = Node;
            }

            return true;
        }

        return Window != nullptr && Window->WasActive && DockId == 0;
    });

    mLeftDock = Left != nullptr ? Left->ID : 0;

    if (Tabs.empty()) {
        return;
    }

    if (Left == nullptr) {
        const ImGuiDockNode* Center{ImGui::DockBuilderGetCentralNode(LevelDock)};
        if (Center == nullptr) {
            return;
        }

        const float Width{std::min(260.0f * std::max(1.0f, mUIScale.GetScale()), Center->Size.x * 0.3f)};
        mLeftDock = ImGui::DockBuilderSplitNode(Center->ID, ImGuiDir_Left, Width / std::max(1.0f, Center->Size.x), nullptr, nullptr);
        if (mLeftDock == 0) {
            return;
        }
    }

    for (const FEditorTab* Tab : Tabs) {
        ImGui::DockBuilderDockWindow(Tab->GetName().c_str(), mLeftDock);
    }

    ImGui::DockBuilderFinish(LevelDock);
    ImGui::MarkIniSettingsDirty();
}

bool FEditorWorkspace::DockTab(const Core::FString& Id) {
    mLevelDock = mLevelTab.GetToolkitHost().GetDockSpaceId();
    FEditorTab* Tab{mTabs.FindTab(Id)};

    if (Tab == nullptr || ImGui::DockBuilderGetNode(mLevelDock) == nullptr) {
        return false;
    }

    if (ImGui::DockBuilderGetNode(mBottomDock) == nullptr) {
        for (const char* Candidate : {"Content", "Console"}) {
            const FEditorTab* Other{mTabs.FindTab(Candidate)};
            const ImGuiWindow* Window{Other != nullptr ? ImGui::FindWindowByName(Other->GetName().c_str()) : nullptr};

            if (Window != nullptr && Window->DockId != 0 && Other->GetRole() == EEditorTabRole::Panel) {
                mBottomDock = Window->DockId;
                break;
            }
        }
    }

    if (ImGui::DockBuilderGetNode(mBottomDock) == nullptr) {
        ImGuiDockNode* Center{ImGui::DockBuilderGetCentralNode(mLevelDock)};

        if (Center == nullptr) {
            return false;
        }

        mBottomDock = ImGui::DockBuilderSplitNode(Center->ID, ImGuiDir_Down, 0.35f, nullptr, nullptr);
    }

    ImGui::DockBuilderDockWindow(Tab->GetName().c_str(), mBottomDock);
    ImGui::DockBuilderFinish(mLevelDock);
    ImGui::MarkIniSettingsDirty();
    FocusLevelEditor();

    return true;
}
