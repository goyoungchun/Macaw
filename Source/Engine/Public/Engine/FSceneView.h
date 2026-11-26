/**
 * @file FSceneView.h
 * @brief 카메라 위치와 방향, 뷰·투영 행렬 및 뷰포트 크기입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Literals/AngleFunctions.h"
#include "Core/Math/FVector2.h"
#include "Core/Math/FMatrix.h"
#include "Engine/FDepthOfFieldSettings.h"

struct FRay;

namespace Engine {

    /**
     * @brief 카메라 위치와 방향, 뷰·투영 행렬 및 뷰포트 크기입니다.
     * @ingroup Engine
     */
    struct FSceneView {
    public:
        /**
         * @brief 뷰포트 픽셀 좌표를 월드 공간의 정규화된 광선으로 역투영합니다.
         * @param[in] Position 뷰포트 픽셀 좌표입니다.
         * @param[out] Ray 계산한 월드 광선입니다.
         * @return 역투영 광선을 구했으면 true입니다.
         */
        bool Deproject(const FVector2& Position, FRay& Ray) const;

    public:
        /**
         * @brief 공간상의 위치입니다.
         */
        FVector mLocation{};

        /**
         * @brief 카메라의 전방 방향입니다.
         */
        FVector mForward{1.0f, 0.0f, 0.0f};

        /**
         * @brief 월드에서 뷰 공간으로 바꾸는 뷰 행렬입니다.
         */
        FMatrix mView{};

        /**
         * @brief 원근 투영 행렬입니다.
         */
        FMatrix mProjection{};

        /**
         * @brief 뷰와 투영을 합성한 행렬입니다.
         */
        FMatrix mViewProjection{};

        /**
         * @brief 뷰포트의 가로·세로 픽셀 크기입니다.
         */
        FVector2 mSize{};

        /**
         * @brief 수직 시야각입니다.
         */
        Core::FAngle mFieldOfView{mDefaultFieldOfView};

        /**
         * @brief 정사영 카메라 여부입니다.
         */
        bool mOrthographic{};

        /**
         * @brief 정사영 가로 가시 범위의 월드 길이입니다.
         */
        float mOrthographicWidth{mDefaultOrthographicWidth};

        /**
         * @brief 센서의 세로 크기입니다. 피사계 심도가 수직 시야각으로 렌즈 초점거리를 구할 때 씁니다.
         */
        Core::FLength mSensorHeight{Core::FLength::FromBase(Engine::Lens::DefaultSensorHeightMillimeters * 0.001f)};

        /**
         * @brief 피사계 심도에 쓸 렌즈 초점거리입니다. 0이면 시야각과 센서 높이로 계산합니다.
         * @details 에디터 자유 시점처럼 화면 시야각이 카메라 렌즈와 다를 때 카메라의 렌즈로 흐림을 미리 보도록 지정합니다.
         */
        Core::FLength mLensFocalLength{};

        Engine::FDepthOfFieldSettings mDepthOfFieldSettings{};

    private:
        static const Core::FAngle mDefaultFieldOfView;
        static const float mDefaultOrthographicWidth;
    };

}
