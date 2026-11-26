/**
 * @file FEditorAnimation.cpp
 * @brief ImGui 식별자와 채널별로 에디터 UI의 값·색상·등장 애니메이션을 관리합니다.
 * @ingroup Editor
 */

#include "FEditorAnimation.h"
#include "Core/Literals/UnitLiterals.h"

#include <im_anim.h>
#include <imgui_internal.h>
#include <algorithm>

using namespace Editor;
using namespace Core::Literals;

constinit const float Editor::FEditorAnimation::mDefaultAppearDuration{(0.12_s).GetBase()};

/**
 * @brief FEditorAnimation 구현에 한정되는 내부 형식·상수·도우미 함수입니다.
 */
namespace {
    /**
     * @brief 애니메이션 채널별 마지막 사용 프레임을 보관하는 ImGui 저장소입니다.
     */
    ImGuiStorage AnimationFrames{};
}

void FEditorAnimation::BeginFrame() {
    iam_update_begin_frame();
    iam_gc(240);

    const int Frame{ImGui::GetFrameCount()};

    if (Frame % 120 == 0) {
        for (int Index{AnimationFrames.Data.Size - 1}; Index >= 0; --Index) {
            if (AnimationFrames.Data[Index].val_i < Frame - 240) {
                AnimationFrames.Data.erase(AnimationFrames.Data.begin() + Index);
            }
        }
    }
}

void FEditorAnimation::Shutdown() {
    iam_update_begin_frame();
    iam_gc(0);
    AnimationFrames.Clear();
}

bool FEditorAnimation::Touch(ImGuiID Owner, const char* Channel) {
    const ImGuiID Key{ImHashStr(Channel, 0, Owner)};
    const int Frame{ImGui::GetFrameCount()};
    const int Previous{AnimationFrames.GetInt(Key, -2)};
    AnimationFrames.SetInt(Key, Frame);
    return Previous < Frame - 1;
}

float FEditorAnimation::Float(ImGuiID Owner, const char* Channel, float Target, float Duration, float Initial) {
    const ImGuiID ChannelId{ImHashStr(Channel)};
    const iam_ease_desc Ease{iam_ease_preset(iam_ease_out_cubic)};
    const float DeltaTime{ImGui::GetIO().DeltaTime};

    if (Touch(Owner, Channel)) {
        iam_tween_float(Owner, ChannelId, Initial, 0.0f, Ease, iam_policy_cut, DeltaTime);
    }

    return iam_tween_float(Owner, ChannelId, Target, std::max(0.0f, Duration), Ease, iam_policy_crossfade, DeltaTime);
}

ImVec4 FEditorAnimation::Color(ImGuiID Owner, const char* Channel, const ImVec4& Target, float Duration, const ImVec4& Initial) {
    const ImGuiID ChannelId{ImHashStr(Channel)};
    const iam_ease_desc Ease{iam_ease_preset(iam_ease_out_cubic)};
    const float DeltaTime{ImGui::GetIO().DeltaTime};

    if (Touch(Owner, Channel)) {
        const bool LazyInitialization{iam_is_lazy_init_enabled()};
        iam_set_lazy_init(false);
        iam_tween_color(Owner, ChannelId, Initial, 0.0f, Ease, iam_policy_cut, iam_col_srgb, DeltaTime);
        iam_set_lazy_init(LazyInitialization);
    }

    return iam_tween_color(Owner, ChannelId, Target, std::max(0.0f, Duration), Ease, iam_policy_crossfade, iam_col_srgb, DeltaTime);
}

float FEditorAnimation::Appear(ImGuiID Owner, float Duration) {
    return Float(Owner, "Appearance", 1.0f, Duration, 1.0f / 255.0f);
}

void FEditorAnimation::ResetFloat(ImGuiID Owner, const char* Channel, float Value) {
    Touch(Owner, Channel);
    iam_tween_float(Owner, ImHashStr(Channel), Value, 0.0f, iam_ease_preset(iam_ease_out_cubic), iam_policy_cut, ImGui::GetIO().DeltaTime);
}
