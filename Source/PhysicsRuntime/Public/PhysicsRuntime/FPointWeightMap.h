/**
 * @file FPointWeightMap.h
 * @brief 천 입자마다 값 하나를 저장하는 가중치 맵을 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"

namespace PhysicsRuntime {

    /**
     * @brief 가중치 맵이 조절하는 천 매개변수의 종류입니다.
     * @ingroup PhysicsRuntime
     */
    enum class EWeightMapTargetCommon : Uint8 {
        /**
         * @brief 대상이 없음을 나타냅니다.
         */
        None = 0,

        /**
         * @brief 입자가 스키닝 위치에서 벗어날 수 있는 최대 거리입니다.
         */
        MaxDistance,

        /**
         * @brief 표면 뒤쪽으로 밀려나지 않게 하는 구의 중심 거리입니다.
         */
        BackstopDistance,

        /**
         * @brief 표면 뒤쪽으로 밀려나지 않게 하는 구의 반지름입니다.
         */
        BackstopRadius,

        /**
         * @brief 입자를 스키닝 위치로 당기는 힘의 배율입니다.
         */
        AnimDriveMultiplier
    };

    /**
     * @brief 천 입자마다 실수 하나를 저장하는 가중치 맵입니다.
     * @ingroup PhysicsRuntime
     */
    struct FPointWeightMap {
    public:

        /**
         * @brief 값을 입자 수만큼 0으로 채우고 대상을 비웁니다.
         * @param[in] NumPoints 입자 수입니다.
         */
        void Initialize(Index_t NumPoints);

        /**
         * @brief 다른 맵의 값을 복사하고 대상을 지정해 켭니다.
         * @param[in] Source 값을 복사할 맵입니다.
         * @param[in] Target 이 맵이 조절할 매개변수입니다.
         */
        void Initialize(const FPointWeightMap& Source, EWeightMapTargetCommon Target);

        /**
         * @brief 값의 개수를 반환합니다.
         * @return 입자 수입니다.
         */
        Index_t Num() const;

        /**
         * @brief 입자 하나의 값을 반환합니다.
         * @param[in] Index 입자 번호이며 범위 안이어야 합니다.
         * @return 값의 읽기 전용 참조입니다.
         */
        const float& operator[](Index_t Index) const;

        /**
         * @brief 입자 하나의 값을 반환합니다.
         * @param[in] Index 입자 번호이며 범위 안이어야 합니다.
         * @return 값의 참조입니다.
         */
        float& operator[](Index_t Index);

        /**
         * @brief 입자 하나의 값을 반환합니다.
         * @param[in] Index 입자 번호입니다.
         * @return 범위 밖이면 0입니다.
         */
        float GetValue(Index_t Index) const;

        /**
         * @brief 입자 하나의 값을 설정합니다.
         * @param[in] Index 입자 번호이며 범위 밖이면 무시합니다.
         * @param[in] Value 설정할 값입니다.
         */
        void SetValue(Index_t Index, float Value);

        /**
         * @brief 입자 하나의 값이 기준 이하인지 검사합니다.
         * @param[in] Index 입자 번호입니다.
         * @param[in] Threshold 비교할 기준이며 기본값은 0.1 cm입니다.
         * @return 범위 안이고 값이 기준 이하이면 true입니다.
         */
        bool IsBelowThreshold(Index_t Index, float Threshold = 0.001f) const;

        /**
         * @brief 삼각형의 세 입자 중 하나라도 기준 이하인지 검사합니다.
         * @param[in] Index0 첫 입자 번호입니다.
         * @param[in] Index1 둘째 입자 번호입니다.
         * @param[in] Index2 셋째 입자 번호입니다.
         * @param[in] Threshold 비교할 기준이며 기본값은 0.1 cm입니다.
         * @return 하나라도 기준 이하이면 true입니다.
         */
        bool AreAnyBelowThreshold(Index_t Index0, Index_t Index1, Index_t Index2, float Threshold = 0.001f) const;

        /**
         * @brief 삼각형의 세 입자가 모두 기준 이하인지 검사합니다.
         * @param[in] Index0 첫 입자 번호입니다.
         * @param[in] Index1 둘째 입자 번호입니다.
         * @param[in] Index2 셋째 입자 번호입니다.
         * @param[in] Threshold 비교할 기준이며 기본값은 0.1 cm입니다.
         * @return 모두 기준 이하이면 true입니다.
         */
        bool AreAllBelowThreshold(Index_t Index0, Index_t Index1, Index_t Index2, float Threshold = 0.001f) const;

    public:

        /**
         * @brief 입자 순서대로 저장한 값입니다.
         */
        Core::TArray<float> mValues{};

        /**
         * @brief 사용자가 맵을 구분하는 이름입니다.
         */
        Core::FString mName{};

        /**
         * @brief 이 맵이 조절하는 매개변수입니다.
         */
        EWeightMapTargetCommon mCurrentTarget{EWeightMapTargetCommon::None};

        /**
         * @brief 이 맵을 천 메시에 적용할지 나타냅니다.
         */
        bool mEnabled{};
    };
}
