/**
 * @file FParticleDistribution.h
 * @brief 파티클 모듈이 시간에 따라 값을 고르는 실수·벡터 분포입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "Engine/Curves/FFloatCurveKey.h"
#include "FParticleDistribution.generated.h"

struct FRandomStream;

namespace Engine {

    /**
     * @brief 분포가 값을 만드는 방식입니다.
     */
    UENUM()
    enum class EDistributionMode : Uint8 {
        Constant,
        Uniform,
        Curve
    };

    /**
     * @brief 구운 표의 한 칸을 해석하는 방식입니다.
     */
    enum class EDistributionLookupOperation : Uint8 {
        /**
         * @brief 아직 굽지 않았습니다.
         */
        Uninitialized,
        /**
         * @brief 칸의 값을 그대로 씁니다.
         */
        None,
        /**
         * @brief 칸 안의 최솟값과 최댓값 사이에서 고릅니다.
         */
        Random
    };

    /**
     * @brief 분포를 일정 시간 간격으로 샘플링한 표입니다.
     * @details 한 칸은 값 묶음 하나(None) 또는 최솟값·최댓값 묶음 둘(Random)로 이뤄집니다.
     */
    struct FDistributionLookupTable {
    public:
        /**
         * @brief 표를 비웁니다.
         */
        void Empty();

        /**
         * @brief 구운 칸이 없으면 true입니다.
         */
        bool IsEmpty() const;

        /**
         * @brief 시간 앞뒤의 두 칸과 그 사이 보간 비율을 찾습니다.
         */
        void GetEntry(float Time, const float*& First, const float*& Second, float& Alpha) const;

    public:
        float mTimeScale{};

        float mTimeBias{};

        Core::TArray<float> mValues{};

        Engine::EDistributionLookupOperation mOperation{Engine::EDistributionLookupOperation::Uninitialized};

        Uint8 mEntryCount{};

        /**
         * @brief 한 칸이 차지하는 실수 개수입니다.
         */
        Uint8 mEntryStride{};

        /**
         * @brief 칸 안에서 최댓값 묶음이 시작하는 위치이며 None이면 0입니다.
         */
        Uint8 mSubEntryStride{};
    };

    /**
     * @brief 상수·균등 난수·커브 중 하나로 실수를 만드는 분포입니다.
     * @details 값을 쓰기 전에 Bake()로 표를 만들어야 합니다.
     */
    USTRUCT()
    struct FFloatDistribution {
        GENERATED_BODY()

    public:
        /**
         * @brief 설정한 값으로 표를 다시 굽습니다.
         */
        void Bake();

        /**
         * @brief 시간에 맞는 값을 반환합니다. 굽지 않았으면 0입니다.
         */
        float GetValue(float Time, FRandomStream& Random) const;

    public:
        UPROPERTY(Edit, Serialize)
        Engine::EDistributionMode mMode{Engine::EDistributionMode::Constant};

        UPROPERTY(Edit, Serialize)
        float mConstant{};

        UPROPERTY(Edit, Serialize)
        float mMinValue{};

        UPROPERTY(Edit, Serialize)
        float mMaxValue{};

        UPROPERTY(Serialize)
        Core::TArray<Engine::FFloatCurveKey> mCurveKeys{};

        Engine::FDistributionLookupTable mLookupTable{};
    };

    /**
     * @brief 상수·균등 난수·축별 커브 중 하나로 벡터를 만드는 분포입니다.
     */
    USTRUCT()
    struct FVectorDistribution {
        GENERATED_BODY()

    public:
        void Bake();

        /**
         * @brief 시간에 맞는 값을 반환합니다. 굽지 않았으면 0 벡터입니다.
         */
        FVector GetValue(float Time, FRandomStream& Random) const;

    public:
        UPROPERTY(Edit, Serialize)
        Engine::EDistributionMode mMode{Engine::EDistributionMode::Constant};

        UPROPERTY(Edit, Serialize)
        FVector mConstant{};

        UPROPERTY(Edit, Serialize)
        FVector mMinValue{};

        UPROPERTY(Edit, Serialize)
        FVector mMaxValue{};

        /**
         * @brief Uniform에서 세 축에 같은 난수를 써 비율을 유지합니다.
         */
        UPROPERTY(Edit, Serialize)
        bool mLockAxes{};

        UPROPERTY(Serialize)
        Core::TArray<Engine::FFloatCurveKey> mCurveKeysX{};

        UPROPERTY(Serialize)
        Core::TArray<Engine::FFloatCurveKey> mCurveKeysY{};

        UPROPERTY(Serialize)
        Core::TArray<Engine::FFloatCurveKey> mCurveKeysZ{};

        Engine::FDistributionLookupTable mLookupTable{};
    };

}
