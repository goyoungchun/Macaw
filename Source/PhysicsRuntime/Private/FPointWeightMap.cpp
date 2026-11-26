/**
 * @file FPointWeightMap.cpp
 * @brief 천 입자마다 값 하나를 저장하는 가중치 맵을 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FPointWeightMap.h"

namespace PhysicsRuntime {

    /**
     * @brief 값을 입자 수만큼 0으로 채우고 대상을 비웁니다.
     */
    void FPointWeightMap::Initialize(Index_t NumPoints) {
        mValues.assign(NumPoints, 0.0f);
        mCurrentTarget = EWeightMapTargetCommon::None;
        mEnabled = false;
    }

    /**
     * @brief 다른 맵의 값을 복사하고 대상을 지정해 켭니다.
     */
    void FPointWeightMap::Initialize(const FPointWeightMap& Source, EWeightMapTargetCommon Target) {
        mValues = Source.mValues;
        mCurrentTarget = Target;
        mEnabled = true;
    }

    /**
     * @brief 값의 개수를 반환합니다.
     */
    Index_t FPointWeightMap::Num() const {
        return mValues.size();
    }

    /**
     * @brief 입자 하나의 값을 반환합니다.
     */
    const float& FPointWeightMap::operator[](Index_t Index) const {
        return mValues[Index];
    }

    /**
     * @brief 입자 하나의 값을 반환합니다.
     */
    float& FPointWeightMap::operator[](Index_t Index) {
        return mValues[Index];
    }

    /**
     * @brief 입자 하나의 값을 반환하며 범위 밖이면 0입니다.
     */
    float FPointWeightMap::GetValue(Index_t Index) const {
        return Index < mValues.size() ? mValues[Index] : 0.0f;
    }

    /**
     * @brief 범위 안이면 입자 하나의 값을 설정합니다.
     */
    void FPointWeightMap::SetValue(Index_t Index, float Value) {
        if (Index < mValues.size()) {
            mValues[Index] = Value;
        }
    }

    /**
     * @brief 입자 하나의 값이 기준 이하인지 검사합니다.
     */
    bool FPointWeightMap::IsBelowThreshold(Index_t Index, float Threshold) const {
        return Index < mValues.size() && mValues[Index] <= Threshold;
    }

    /**
     * @brief 삼각형의 세 입자 중 하나라도 기준 이하인지 검사합니다.
     */
    bool FPointWeightMap::AreAnyBelowThreshold(Index_t Index0, Index_t Index1, Index_t Index2, float Threshold) const {
        return IsBelowThreshold(Index0, Threshold) || IsBelowThreshold(Index1, Threshold) || IsBelowThreshold(Index2, Threshold);
    }

    /**
     * @brief 삼각형의 세 입자가 모두 기준 이하인지 검사합니다.
     */
    bool FPointWeightMap::AreAllBelowThreshold(Index_t Index0, Index_t Index1, Index_t Index2, float Threshold) const {
        return IsBelowThreshold(Index0, Threshold) && IsBelowThreshold(Index1, Threshold) && IsBelowThreshold(Index2, Threshold);
    }
}
