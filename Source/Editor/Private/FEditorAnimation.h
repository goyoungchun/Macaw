/**
 * @file FEditorAnimation.h
 * @brief ImGui 식별자와 채널별로 에디터 UI의 값·색상·등장 애니메이션을 관리합니다.
 * @ingroup Editor
 */

#pragma once

#include <imgui.h>

namespace Editor {
    /**
     * @brief ImGui 식별자와 채널별로 에디터 UI의 값·색상·등장 애니메이션을 관리합니다.
     */
    class FEditorAnimation {
    public:
        /**
         * @brief 보간 라이브러리의 새 프레임을 시작하고 오래 사용하지 않은 채널을 정리합니다.
         */
        static void BeginFrame();

        /**
         * @brief 보간 라이브러리와 채널별 마지막 사용 프레임 저장소를 정리합니다.
         */
        static void Shutdown();

        /**
         * @brief 채널의 실수 값을 목표까지 보간하고 오랫동안 표시하지 않은 채널은 초기값에서 시작합니다.
         * @param[in] Owner 애니메이션 채널을 소유하는 위젯의 ImGui 식별자입니다.
         * @param[in] Channel 소유자 안에서 보간 상태를 구분할 채널 이름입니다.
         * @param[in] Target 애니메이션의 목표 값 또는 목표 색상입니다.
         * @param[in] Duration 목표 값까지 보간할 초 단위 시간이며 음수는 0으로 처리합니다.
         * @param[in] Initial 새로 나타난 애니메이션 채널의 시작 값입니다.
         * @return 현재 프레임의 보간 값입니다.
         */
        static float Float(ImGuiID Owner, const char* Channel, float Target, float Duration, float Initial = 0.0f);

        /**
         * @brief 채널의 색상을 sRGB 공간에서 목표까지 보간합니다.
         * @param[in] Owner 애니메이션 채널을 소유하는 위젯의 ImGui 식별자입니다.
         * @param[in] Channel 소유자 안에서 보간 상태를 구분할 채널 이름입니다.
         * @param[in] Target 애니메이션의 목표 값 또는 목표 색상입니다.
         * @param[in] Duration 목표 값까지 보간할 초 단위 시간이며 음수는 0으로 처리합니다.
         * @param[in] Initial 새로 나타난 애니메이션 채널의 시작 값입니다.
         * @return 현재 프레임의 보간 색상입니다.
         */
        static ImVec4 Color(ImGuiID Owner, const char* Channel, const ImVec4& Target, float Duration, const ImVec4& Initial);

        /**
         * @brief 위젯이 다시 나타날 때 불투명도를 거의 0에서 1까지 보간합니다.
         * @param[in] Owner 애니메이션 채널을 소유하는 위젯의 ImGui 식별자입니다.
         * @param[in] Duration 목표 값까지 보간할 초 단위 시간이며 음수는 0으로 처리합니다.
         * @return 현재 프레임의 등장 불투명도입니다.
         */
        static float Appear(ImGuiID Owner, float Duration = mDefaultAppearDuration);

        /**
         * @brief 실수 채널의 보간 상태를 지정한 값으로 즉시 초기화합니다.
         * @param[in] Owner 애니메이션 채널을 소유하는 위젯의 ImGui 식별자입니다.
         * @param[in] Channel 소유자 안에서 보간 상태를 구분할 채널 이름입니다.
         * @param[in] Value 실수 채널에 즉시 적용할 값입니다.
         */
        static void ResetFloat(ImGuiID Owner, const char* Channel, float Value);

    private:
        /**
         * @brief 채널의 마지막 사용 프레임을 갱신하고 새로 나타난 채널인지 판별합니다.
         * @param[in] Owner 애니메이션 채널을 소유하는 위젯의 ImGui 식별자입니다.
         * @param[in] Channel 소유자 안에서 보간 상태를 구분할 채널 이름입니다.
         * @return 직전 프레임에 사용하지 않은 채널이면 true입니다.
         */
        static bool Touch(ImGuiID Owner, const char* Channel);

    private:
        static const float mDefaultAppearDuration;
    };
}
