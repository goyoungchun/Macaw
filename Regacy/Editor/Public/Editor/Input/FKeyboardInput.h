#pragma once

#include <cstdint>
#include <optional>

#include <Windows.h>
#include "Core/Channel/FMessageChannel.h"
#include "Editor/Input/EKeyState.h"
#include "Core/Types.h"

struct FViewportKeyboardNavigationInput {
    float mForwardAxis{0.0f};
    float mRightAxis{0.0f};
    float mUpAxis{0.0f};
    float mDeltaTime{0.0f};
};

class FKeyboardInput {
public:
    FKeyboardInput();

public:
    void Reset();
    void InitializeWorldCommandSender(FMessageChannel::FSender&& Sender);

    void ProcessWindowMessage(UINT Message, WPARAM WParam, LPARAM LParam);

    FViewportKeyboardNavigationInput ConsumeViewportNavigation(float DeltaTime, bool BKeyboardCaptureByUi);

    EKeyState GetKeyState(std::uint8_t VirtualKey) const;
    bool IsHeld(std::uint8_t VirtualKey) const;

private:
    void AdvanceKeyStates();
    void ResetKeyStates();

private:
    static constexpr std::size_t KeyCount{256};
    Core::TStaticArray<EKeyState, KeyCount> mKeyStates{};
    std::optional<FMessageChannel::FSender> mWorldCommandSender{};
};
