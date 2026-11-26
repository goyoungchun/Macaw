#pragma once

#include "Core/Types.h"
#include <functional>

namespace Editor {

    class FMessageBox {
    public:
        struct FSavePrompt {
            Core::FString mTitle{};
            Core::FString mMessage{};
            std::function<bool()> mSave{};
            std::function<bool()> mDiscard{};
        };

    private:
        enum class EChoice {
            None,
            Save,
            Discard,
            Cancel
        };

    public:
        void Show(Core::FString Title, Core::FString Message);
        void ConfirmSave(Core::TArray<FSavePrompt> Prompts, std::function<void()> Action);

        bool IsOpen() const;
        void Draw();
        void Reset();

    private:
        EChoice DrawButtons(bool CanSave) const;
        void HandleChoice(EChoice Choice);

    private:
        Core::TArray<FSavePrompt> mPrompts{};
        Core::TArray<std::function<bool()>> mDiscards{};
        std::function<void()> mAction{};
        Index_t mPromptIndex{};
        bool mAcknowledgement{};
    };

}
