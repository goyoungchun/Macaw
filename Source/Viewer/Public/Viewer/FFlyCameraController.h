/**
 * @file FFlyCameraController.h
 * @brief 뷰어 카메라를 우클릭 + WASD/QE로 자유롭게 날아다니게 하는 입력 처리를 정의합니다.
 * @ingroup Viewer
 */

#pragma once

#include "Viewer/FViewerCamera.h"
#include "Core/Math/FVector2.h"

namespace Viewer {

    /**
     * @brief 비행 카메라 한 프레임의 입력입니다. UI 라이브러리나 입력 시스템과 무관하게 호출자가 채웁니다.
     * @ingroup Viewer
     */
    struct FFlyInput {
    public:

        /**
         * @brief 이전 프레임 대비 마우스 이동량(픽셀)입니다. 눈을 고정한 채 시선을 돌리는 데 씁니다.
         */
        FVector2 mMouseDelta{};

        /**
         * @brief 앞으로(W) 이동 키가 눌렸는지 나타냅니다.
         */
        bool mForward{};

        /**
         * @brief 뒤로(S) 이동 키가 눌렸는지 나타냅니다.
         */
        bool mBackward{};

        /**
         * @brief 오른쪽(D) 이동 키가 눌렸는지 나타냅니다.
         */
        bool mRight{};

        /**
         * @brief 왼쪽(A) 이동 키가 눌렸는지 나타냅니다.
         */
        bool mLeft{};

        /**
         * @brief 위로(E) 이동 키가 눌렸는지 나타냅니다.
         */
        bool mUp{};

        /**
         * @brief 아래로(Q) 이동 키가 눌렸는지 나타냅니다.
         */
        bool mDown{};

        /**
         * @brief 빠르게 이동(Shift)하는지 나타냅니다.
         */
        bool mFast{};

        /**
         * @brief 마우스 휠 입력량이며 이동 속도를 조절하는 데 씁니다.
         */
        float mWheel{};

        /**
         * @brief 이전 프레임 이후 경과한 시간(초)입니다.
         */
        float mDeltaTime{};
    };

    /**
     * @brief 오른쪽 버튼을 누른 동안 시선 회전과 WASD/QE 이동, 휠 속도 조절을 처리하는 비행 카메라 컨트롤러입니다.
     * @details 이동 속도는 카메라의 기준 반지름(대상의 크기)에 비례하므로 작은 대상과 큰 대상에서 모두 알맞게 움직입니다.
     *          입력 장치는 호출자가 FFlyInput으로 넘기며, 호출자가 오른쪽 버튼이 눌린 프레임에서만 Update를 부르면 됩니다.
     * @ingroup Viewer
     */
    class FFlyCameraController {
    public:

        /**
         * @brief 입력을 받아 카메라의 시선을 돌리고 위치를 옮기며, 휠이 있으면 이동 속도를 조절합니다.
         * @param[in,out] Camera 조작할 카메라입니다.
         * @param[in] Input 이번 프레임의 비행 입력입니다.
         */
        void Update(Viewer::FViewerCamera& Camera, const Viewer::FFlyInput& Input);

        /**
         * @brief 현재 이동 속도 배율을 반환합니다.
         * @return 카메라 기준 반지름/초 단위의 속도 배율입니다.
         */
        float GetSpeed() const;

    private:

        /**
         * @brief 이동 속도 배율이며 카메라 기준 반지름/초를 곱해 실제 속도를 구합니다.
         */
        float mSpeed{1.0f};
    };

}
