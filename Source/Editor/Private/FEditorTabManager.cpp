/**
 * @file FEditorTabManager.cpp
 * @brief 탭과 위젯의 등록·지연 생성·표시 및 기본 도킹 배치를 관리합니다.
 * @ingroup Editor
 */

#include "Editor/FEditorTabManager.h"
#include "Editor/Widget/FEditorWidget.h"
#include "Editor/FEditorTab.h"
#include "Core/Types.h"

#include <imgui_internal.h>
#include <algorithm>
#include <utility>

using namespace Editor;

FEditorTabManager::FEditorTabManager(FEditorTab* Owner)
	: mOwner{Owner} {
}

FEditorTabManager::~FEditorTabManager() = default;

bool FEditorTabManager::RegisterWidget(Core::FString Id, Core::FString Label, FEditorWidget& Widget, EEditorDockRegion Region, EEditorTabRole Role, bool Closable, Core::FString WindowName) {
    if (Widget.IsAttached() || std::any_of(mEntries.begin(), mEntries.end(), [&Id](const FEntry& Entry) {
            return Entry.mId == Id;
        })) {
        return false;
    }

    FEntry Entry{};
    Entry.mId = std::move(Id);
    Entry.mLabel = std::move(Label);
    Entry.mWindowName = WindowName.empty() ? Entry.mLabel : std::move(WindowName);
    Entry.mClosable = Closable;
    Entry.mOwnedTab = std::make_unique<FEditorTab>(Entry.mWindowName, Closable, Role);
    Entry.mOwnedTab->SetContent(&Widget);
    Entry.mTab = Entry.mOwnedTab.get();
    Entry.mTab->SetDockClass(mDockClass);

    if (Role == EEditorTabRole::Drawer) {
        Entry.mTab->SetVisible(false);
    }
    Entry.mRegion = Region;
    Entry.mRole = Role;
    Entry.mDefaultVisible = Entry.mTab->IsVisible();
    mEntries.push_back(std::move(Entry));

    return true;
}

bool FEditorTabManager::RegisterWidget(Core::FString Id, Core::FString Label, std::unique_ptr<FEditorWidget> Widget, EEditorDockRegion Region, bool Closable, Core::FString WindowName) {
    if (Widget == nullptr || !RegisterWidget(std::move(Id), std::move(Label), *Widget, Region, EEditorTabRole::Panel, Closable, std::move(WindowName))) {
        return false;
    }

    mEntries.back().mOwnedWidget = std::move(Widget);

    return true;
}

bool FEditorTabManager::RegisterTabSpawner(Core::FString Id, Core::FString Label, std::function<std::unique_ptr<FEditorWidget>()> Spawner, EEditorTabRole Role, bool Closable, Core::FString WindowName) {
    if (!Spawner || std::any_of(mEntries.begin(), mEntries.end(), [&Id](const FEntry& Entry) {
            return Entry.mId == Id;
        })) {
        return false;
    }

    FEntry Entry{};
    Entry.mId = std::move(Id);
    Entry.mLabel = std::move(Label);
    Entry.mSpawner = std::move(Spawner);
    Entry.mWindowName = WindowName.empty() ? Entry.mLabel : std::move(WindowName);
    Entry.mClosable = Closable;
    Entry.mRole = Role;
    mEntries.push_back(std::move(Entry));

    return true;
}

FEditorWidget* FEditorTabManager::SpawnTab(const Core::FString& Id) {
    const auto Entry{std::find_if(mEntries.begin(), mEntries.end(), [&Id](const FEntry& Item) {
        return Item.mId == Id;
    })};

    if (Entry == mEntries.end()) {
        return nullptr;
    }

    if (Entry->mTab == nullptr && Entry->mSpawner) {
        Entry->mOwnedWidget = Entry->mSpawner();
        Entry->mOwnedTab = Entry->mOwnedWidget != nullptr ? std::make_unique<FEditorTab>(Entry->mWindowName, Entry->mClosable, Entry->mRole) : nullptr;
        Entry->mTab = Entry->mOwnedTab.get();

        if (Entry->mTab != nullptr) {
            Entry->mTab->SetContent(Entry->mOwnedWidget.get());
            Entry->mTab->SetDockClass(mDockClass);

            if (Entry->mRole == EEditorTabRole::Drawer) {
                Entry->mTab->SetVisible(false);
            }
        }

        Entry->mDefaultVisible = Entry->mTab != nullptr && Entry->mTab->IsVisible();
    }

    return Entry->mTab != nullptr ? Entry->mTab->GetContent() : nullptr;
}

FEditorTab* FEditorTabManager::TryInvokeTab(const Core::FString& Id) {
    SpawnTab(Id);
    FEditorTab* Tab{FindTab(Id)};

    if (Tab != nullptr) {
        if (mOwner != nullptr && Tab->GetRole() != EEditorTabRole::Drawer) {
            mOwner->Open();
        }

        Tab->Open();
    }

    return Tab;
}

FEditorTab* FEditorTabManager::FindTab(const Core::FString& Id) const {
    const auto Entry{std::find_if(mEntries.begin(), mEntries.end(), [&Id](const FEntry& Item) {
        return Item.mId == Id;
    })};

    return Entry != mEntries.end() ? Entry->mTab : nullptr;
}

bool FEditorTabManager::IsVisible(const Core::FString& Id) const {
    const FEditorTab* Tab{FindTab(Id)};

    return Tab != nullptr && Tab->IsVisible();
}

void FEditorTabManager::SetVisible(const Core::FString& Id, bool Visible) {
    SpawnTab(Id);
    FEditorTab* Tab{FindTab(Id)};

    if (Tab != nullptr) {
        Tab->SetVisible(Visible);
    }
}

void FEditorTabManager::Draw(float DeltaTime) {
    const std::size_t Count{mEntries.size()};

    for (std::size_t Index{}; Index < Count; ++Index) {
        if (mEntries[Index].mTab != nullptr && mEntries[Index].mTab->GetRole() == EEditorTabRole::Drawer) {
            continue;
        }

        FEditorTab* Tab{mEntries[Index].mTab};
        const std::function<void(float)> Draw{mEntries[Index].mDraw};

        if (Draw) {
            Draw(DeltaTime);
        } else if (Tab != nullptr) {
            Tab->Draw(DeltaTime);
        }
    }
}

void FEditorTabManager::DrawMenu() {
    for (FEntry& Entry : mEntries) {
        if (ImGui::MenuItem(Entry.mLabel.c_str(), nullptr, IsVisible(Entry.mId))) {
            if (IsVisible(Entry.mId)) {
                SetVisible(Entry.mId, false);
            } else {
                TryInvokeTab(Entry.mId);
            }
        }
    }
}

void FEditorTabManager::Clear() {
    mEntries.clear();
}

bool FEditorTabManager::RegisterTab(Core::FString Id, Core::FString Label, FEditorTab& Tab, std::function<void(float)> Draw) {
    if (!Draw || std::any_of(mEntries.begin(), mEntries.end(), [&Id](const FEntry& Entry) {
            return Entry.mId == Id;
        })) {
        return false;
    }

    FEntry Entry{};
    Entry.mId = std::move(Id);
    Entry.mLabel = std::move(Label);
    Entry.mTab = &Tab;
    Entry.mDraw = std::move(Draw);
    Entry.mDefaultVisible = Tab.IsVisible();
    mEntries.push_back(std::move(Entry));

    return true;
}

void FEditorTabManager::UnregisterTab(const Core::FString& Id) {
    std::erase_if(mEntries, [&Id](const FEntry& Entry) {
        return Entry.mId == Id;
    });
}

void FEditorTabManager::SetDockClass(Uint32 ClassId) {
    mDockClass = ClassId;

    for (FEntry& Entry : mEntries) {
        if (Entry.mTab != nullptr) {
            Entry.mTab->SetDockClass(ClassId);
        }
    }
}

bool FEditorTabManager::HasRegion(EEditorDockRegion Region) const {
    return std::any_of(mEntries.begin(), mEntries.end(), [Region](const FEntry& Entry) {
        return Entry.mRegion == Region && Entry.mRole != EEditorTabRole::Drawer;
    });
}

bool FEditorTabManager::IsFocused() const {
    return std::any_of(mEntries.begin(), mEntries.end(), [](const FEntry& Entry) {
        return Entry.mTab != nullptr && Entry.mTab->IsFocused();
    });
}

void FEditorTabManager::ApplyLayout(Uint32 Left, Uint32 Outliner, Uint32 Details, Uint32 Bottom, Uint32 Center) {
    const Core::TStaticArray<ImGuiID, 5> Regions{Center, Left, Outliner, Details, Bottom};

    for (const FEntry& Entry : mEntries) {
        if (Entry.mTab != nullptr && Entry.mTab->GetRole() != EEditorTabRole::Drawer) {
            const ImGuiID Region{Regions[static_cast<Index_t>(Entry.mRegion)]};
            ImGui::DockBuilderDockWindow(Entry.mTab->GetName().c_str(), Region);
        }
    }
}

void FEditorTabManager::RestoreDefaults() {
    for (FEntry& Entry : mEntries) {
        if (Entry.mTab != nullptr) {
            Entry.mTab->SetVisible(Entry.mDefaultVisible);
        }
    }
}

void FEditorTabManager::SetDefaultVisible(const Core::FString& Id, bool Visible) {
    for (FEntry& Entry : mEntries) {
        if (Entry.mId == Id) {
            Entry.mDefaultVisible = Visible;

            if (Entry.mTab != nullptr) {
                Entry.mTab->SetVisible(Visible);
            }

            return;
        }
    }
}
