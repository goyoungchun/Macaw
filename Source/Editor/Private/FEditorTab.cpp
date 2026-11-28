/**
 * @file FEditorTab.cpp
 * @brief 위젯 또는 툴킷 호스트를 담는 창의 표시·포커스·닫기와 도킹 속성을 관리합니다.
 * @ingroup Editor
 */

#include "Editor/FEditorTab.h"
#include "EditorStyle.h"
#include "FEditorAnimation.h"
#include "Editor/FEditorToolkitHost.h"
#include "Editor/Widget/FEditorWidget.h"

#include <imgui_internal.h>
#include <utility>
#include <cassert>

using namespace Editor;

FEditorTab::FEditorTab(Core::FString Name, bool Closable, EEditorTabRole Role)
	: mName{std::move(Name)},
	  mClosable{Closable},
	  mRole{Role} {
}


FEditorTab::~FEditorTab() {
    SetContent(nullptr);
}

void FEditorTab::ApplyFocus() {
    if (mRole == EEditorTabRole::TopLevel) {
        ImGuiWindowClass WindowClass{};
        WindowClass.ClassId = EditorStyle::TopLevelTabClassId;
        WindowClass.DockingAllowUnclassed = false;
        ImGui::SetNextWindowClass(&WindowClass);
    } else if (mDockClass != 0) {
        ImGuiWindowClass WindowClass{};
        WindowClass.ClassId = mDockClass;
        WindowClass.DockingAllowUnclassed = false;
        WindowClass.DockNodeFlagsOverrideSet = mAutoHideTabBar ? ImGuiDockNodeFlags_AutoHideTabBar : ImGuiDockNodeFlags_None;
        ImGui::SetNextWindowClass(&WindowClass);
    }

    if (ConsumeFocusRequest()) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowCollapsed(false);
    }
}

bool FEditorTab::Begin() {
    ApplyFocus();
    mOpenBeforeBegin = mVisible;

    const bool OverridePadding{mToolkitHost != nullptr || mContentPadding.has_value()};

    if (OverridePadding) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, mContentPadding.has_value() ? ImVec2{mContentPadding->x, mContentPadding->y} : ImVec2{});
    }

    const ImGuiWindowFlags Flags{mToolkitHost != nullptr ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | (mClosable ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoMove) : ImGuiWindowFlags_None};
    const bool Expanded{EditorStyle::BeginEditorWindow(mName.c_str(), mClosable ? &mVisible : nullptr, Flags | static_cast<ImGuiWindowFlags>(mWindowFlags))};

    if (OverridePadding) {
        ImGui::PopStyleVar();
    }

    if (mIcon.has_value()) {
        EditorStyle::TabIcon(*mIcon, mIconColor);
    }

    const float Opacity{Expanded ? FEditorAnimation::Appear(ImGui::GetCurrentWindow()->ID, 0.10f) : 1.0f};
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Opacity);

    return Expanded;
}

void FEditorTab::End() {
    ImGui::PopStyleVar();
    ImGui::End();

    if (mOpenBeforeBegin && !mVisible) {
        mVisible = true;
        SetVisible(false);
    }
}

void FEditorTab::Draw(float DeltaTime) {
    if (!mVisible) {
        return;
    }

    const bool Expanded{Begin()};

    if (mToolkitHost != nullptr) {
        mToolkitHost->DrawDockSpace(Expanded);
    } else if (FEditorWidget* Content{GetContent()}; Expanded && Content != nullptr) {
        Content->Draw(DeltaTime);
    }

    End();

    if (mToolkitHost != nullptr && Expanded) {
        mToolkitHost->GetTabManager().Draw(DeltaTime);
    }
}

void FEditorTab::Open() {
    mVisible = true;
    mFocusRequested = true;
}

void FEditorTab::SetVisible(bool Visible) {
    if (Visible == mVisible || (!Visible && mCanCloseHandler && !mCanCloseHandler())) {
        return;
    }

    mVisible = Visible;

    if (FEditorWidget* Content{GetContent()}; !mVisible && Content != nullptr) {
        Content->NotifyHidden();
    }

    if (!mVisible && mClosedHandler) {
        mClosedHandler();
    }
}

bool FEditorTab::IsVisible() const {
    return mVisible;
}

bool FEditorTab::IsFocused() const {
    const ImGuiWindow* Window{ImGui::FindWindowByName(mName.c_str())};
    const ImGuiWindow* Focused{ImGui::GetCurrentContext()->NavWindow};

    return Window != nullptr && Focused != nullptr && (Window->RootWindow == Focused->RootWindow || (mToolkitHost != nullptr && mToolkitHost->IsFocused()));
}

void FEditorTab::PrepareDockedFocus() const {
    if (!mFocusRequested) {
        return;
    }

    ImGuiWindow* Window{ImGui::FindWindowByName(mName.c_str())};
    ImGuiTabBar* TabBar{Window != nullptr && Window->DockNode != nullptr ? Window->DockNode->TabBar : nullptr};

    if (ImGuiTabItem* Tab{TabBar != nullptr ? ImGui::TabBarFindTabByID(TabBar, Window->TabId) : nullptr}; Tab != nullptr) {
        ImGui::FocusWindow(Window);
        ImGui::TabBarQueueFocus(TabBar, Tab);
    }
}

const Core::FString& FEditorTab::GetName() const {
    return mName;
}

void FEditorTab::SetName(Core::FString Name) {
    mName = std::move(Name);
}

void FEditorTab::SetIcon(EEditorIcon Icon, Uint32 Color) {
    const std::size_t Separator{mName.find("###")};
    const Core::FString Label{mName.substr(0, Separator)};
    const Core::FString Identifier{Separator == Core::FString::npos ? mName : mName.substr(Separator + 3)};

    mName = (mIcon.has_value() ? "" : EditorStyle::TabIconPadding) + Label + "###" + Identifier;
    mIcon = Icon;
    mIconColor = Color;
}

void FEditorTab::SetRole(EEditorTabRole Role) {
    mRole = Role;
}

EEditorTabRole FEditorTab::GetRole() const {
    return mRole;
}

void FEditorTab::SetContent(FEditorWidget* Widget) {
    if (GetContent() == Widget) {
        return;
    }

    assert(Widget == nullptr || !Widget->IsAttached());

    if (Widget != nullptr && !Widget->AttachTo(this)) {
        return;
    }

    if (FEditorWidget* Previous{GetContent()}) {
        Previous->DetachFrom(this);
    }

    mContent = Widget;
    mContentLifetime = Widget != nullptr ? Widget->GetLifetime() : std::weak_ptr<void>{};
}

FEditorWidget* FEditorTab::GetContent() const {
    return mContentLifetime.expired() ? nullptr : mContent;
}

FEditorToolkitHost& FEditorTab::GetToolkitHost() {
    assert(mRole == EEditorTabRole::TopLevel);

    if (mToolkitHost == nullptr) {
        mToolkitHost = std::make_unique<FEditorToolkitHost>(*this);
    }

    return *mToolkitHost;
}

void FEditorTab::SetDockClass(Uint32 ClassId) {
    mDockClass = ClassId;
}

void FEditorTab::SetWindowFlags(Uint32 Flags) {
    mWindowFlags = Flags;
}

void FEditorTab::SetContentPadding(const FVector2& Padding) {
    mContentPadding = Padding;
}

void FEditorTab::SetAutoHideTabBar(bool AutoHide) {
    mAutoHideTabBar = AutoHide;
}

bool FEditorTab::ConsumeFocusRequest() {
    return std::exchange(mFocusRequested, false);
}

void FEditorTab::SetClosedHandler(std::function<void()> Handler) {
    mClosedHandler = std::move(Handler);
}

void FEditorTab::SetCanCloseHandler(std::function<bool()> Handler) {
    mCanCloseHandler = std::move(Handler);
}
