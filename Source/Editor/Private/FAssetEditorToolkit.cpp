/**
 * @file FAssetEditorToolkit.cpp
 * @brief 에셋 경로와 최상위 탭을 보관하고 편집기의 갱신·명령·변경 통지를 연결합니다.
 * @ingroup Editor
 */

#include "FAssetEditorToolkit.h"
#include "Editor/FEditorToolkitHost.h"
#include <utility>

using namespace Editor;

FAssetEditorToolkit::FAssetEditorToolkit(std::filesystem::path Path, Asset::EAssetType Type, Core::FString WindowName)
	: mPath{std::move(Path)},
	  mType{Type},
	  mTab{std::move(WindowName), true, EEditorTabRole::TopLevel} {
    mTab.GetToolkitHost().SetFrameHandler([this]() {
        HandleCommands();
    });
}

FAssetEditorToolkit::~FAssetEditorToolkit() = default;

void FAssetEditorToolkit::Tick(float) {
}

void FAssetEditorToolkit::Draw(float DeltaTime) {
    if (mTab.IsVisible()) {
        UpdateEditor(DeltaTime);
    }

    mTab.Draw(DeltaTime);
}

void FAssetEditorToolkit::HandleCommands() {
}

void FAssetEditorToolkit::CloseRetired(const std::filesystem::path&) {
}

std::optional<FMessageBox::FSavePrompt> FAssetEditorToolkit::GetClosePrompt() {
    return {};
}

const std::filesystem::path& FAssetEditorToolkit::GetAssetPath() const {
    return mPath;
}

Asset::EAssetType FAssetEditorToolkit::GetAssetType() const {
    return mType;
}

FEditorTab& FAssetEditorToolkit::GetTab() {
    return mTab;
}

FEditorToolkitHost& FAssetEditorToolkit::GetToolkitHost() {
    return mTab.GetToolkitHost();
}

void FAssetEditorToolkit::SetChangedHandler(std::function<void()> Handler) {
    mChanged = std::move(Handler);
}

void FAssetEditorToolkit::NotifyChanged() {
    if (mChanged) {
        mChanged();
    }
}
