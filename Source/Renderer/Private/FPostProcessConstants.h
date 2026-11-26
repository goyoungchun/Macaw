/**
 * @file FPostProcessConstants.h
 * @brief 월드 위치 복원·높이 안개·깊이 시각화·FXAA에 사용하는 GPU 상수입니다.
 * @ingroup Renderer
 */

#pragma once

#include "Core/Math/FMatrix.h"
#include "Core/Math/FVector4.h"

namespace Renderer {

    /**
     * @brief 후처리 픽셀 셰이더와 공유하는 256바이트 상수 버퍼입니다.
     */
    struct alignas(16) FPostProcessConstants {
    public:
        /**
         * @brief 텍스처 UV와 장치 깊이를 월드 위치로 복원하는 행 우선 역행렬입니다.
         */
        FMatrix mInverseViewProjection{};

        /**
         * @brief 정사영에서 안개 광선의 시작 평면을 계산하는 행 우선 뷰 행렬입니다.
         */
        FMatrix mView{};

        /**
         * @brief 카메라 월드 위치 XYZ와 정사영 여부 W를 저장합니다.
         */
        FVector4 mCameraPosition{};

        /**
         * @brief 렌더 타깃의 폭·높이 XY와 각각의 역수 ZW를 저장합니다.
         */
        FVector4 mScreenSize{};

        /**
         * @brief 합성에 사용할 기본 산란 RGBA 색상입니다.
         */
        FVector4 mFogColor{};

        /**
         * @brief 기준 밀도·높이 감쇠·안개 기준 Z·최대 불투명도를 순서대로 저장합니다.
         */
        FVector4 mFogParameters{};

        /**
         * @brief 안개 시작 거리·적분 종료 거리·적용 제외 거리와 예약값을 저장합니다.
         */
        FVector4 mFogDistances{};

        /**
         * @brief X는 후처리 패스 번호, Y는 조리개 변의 수(원형은 0), ZW는 Near·Far 블러 비활성화 여부입니다.
         */
        FVector4 mSettings{};

        /**
         * @brief 장치 깊이 복원에 사용하는 0부터 시작하는 투영 행렬 성분 P22·P32·P23·P33입니다.
         */
        FVector4 mProjectionDepth{};

        /**
         * @brief 초점 평면 거리(m), 무한대의 착란원 반경 K(px), 전경(착란원 상한)·배경 블러의 최대 탐색 반경(px)입니다.
         */
        FVector4 mDepthOfFieldParameters{};
    };

    static_assert(sizeof(Renderer::FPostProcessConstants) == 256);

}
