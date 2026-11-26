#include "FMessageBox.h"
#include "EditorStyle.h"
#include <imgui.h>
#include <utility>

using namespace Editor;

void FMessageBox::Show(Core::FString Title, Core::FString Message) {
    if (!IsOpen()) {
        mPrompts.push_back({std::move(Title), std::move(Message)});
        mAcknowledgement = true;
    }
}

void FMessageBox::ConfirmSave(Core::TArray<FSavePrompt> Prompts, std::function<void()> Action) {
    if (IsOpen()) {
        return;
    }

    if (Prompts.empty()) {
        Action();

        return;
    }

    mPrompts = std::move(Prompts);
    mAction = std::move(Action);
}

bool FMessageBox::IsOpen() const {
    return !mPrompts.empty();
}

void FMessageBox::Draw() {
    if (!IsOpen()) {
        return;
    }

    const FSavePrompt& Prompt{mPrompts[mPromptIndex]};
    const Core::FString Popup{Prompt.mTitle + "###EditorMessageBox"};

    if (!ImGui::IsPopupOpen(Popup.c_str())) {
        ImGui::OpenPopup(Popup.c_str());
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});

    if (!ImGui::BeginPopupModal(Popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(Prompt.mMessage.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const EChoice Choice{DrawButtons(static_cast<bool>(Prompt.mSave))};
    if (Choice != EChoice::None) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();

    HandleChoice(Choice);
}

void FMessageBox::Reset() {
    mPrompts.clear();
    mDiscards.clear();
    mAction = {};
    mPromptIndex = {};
    mAcknowledgement = false;
}

FMessageBox::EChoice FMessageBox::DrawButtons(bool CanSave) const {
    const bool Escape{ImGui::IsKeyPressed(ImGuiKey_Escape, false)};

    if (mAcknowledgement) {
        const bool Acknowledged{EditorStyle::Button("OK")};
        ImGui::SetItemDefaultFocus();

        return Acknowledged || Escape ? EChoice::Cancel : EChoice::None;
    }

    ImGui::BeginDisabled(!CanSave);
    const bool Save{EditorStyle::Button("Save")};
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool Discard{EditorStyle::Button("Don't Save")};
    ImGui::SameLine();
    const bool Cancel{EditorStyle::Button("Cancel")};
    ImGui::SetItemDefaultFocus();

    if (Cancel || Escape) {
        return EChoice::Cancel;
    }

    if (CanSave && (Save || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))) {
        return EChoice::Save;
    }

    return Discard ? EChoice::Discard : EChoice::None;
}

void FMessageBox::HandleChoice(EChoice Choice) {
    if (Choice == EChoice::None) {
        return;
    }

    FSavePrompt& Prompt{mPrompts[mPromptIndex]};

    if (Choice == EChoice::Cancel || (Choice == EChoice::Save && !Prompt.mSave())) {
        Reset();

        return;
    }

    if (Choice == EChoice::Discard && Prompt.mDiscard) {
        mDiscards.push_back(std::move(Prompt.mDiscard));
    }

    if (++mPromptIndex < mPrompts.size()) {
        return;
    }

    auto Discards{std::move(mDiscards)};
    auto Action{std::move(mAction)};
    Reset();

    for (const auto& Discard : Discards) {
        if (!Discard()) {
            return;
        }
    }

    Action();
}
