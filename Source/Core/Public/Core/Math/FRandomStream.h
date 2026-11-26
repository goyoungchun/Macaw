/**
 * @file FRandomStream.h
 * @brief 시드로 재현 가능한 선형 합동 난수 스트림입니다.
 * @ingroup Core
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector.h"

/**
 * @brief 시드가 같으면 같은 수열을 만드는 가벼운 난수 생성기입니다.
 * @details 파티클처럼 많이 뽑는 곳에서 쓰며 암호학적 품질은 보장하지 않습니다.
 */
struct FRandomStream {
public:
    /**
     * @brief 주어진 시드로 스트림을 만듭니다.
     * @param Seed 초기 시드입니다.
     */
    explicit FRandomStream(Int32 Seed = 0);

public:
    /**
     * @brief 초기 시드를 바꾸고 수열을 처음부터 시작합니다.
     * @param Seed 새 초기 시드입니다.
     */
    void Initialize(Int32 Seed);

    /**
     * @brief 운영체제 난수로 새 시드를 골라 초기화합니다.
     */
    void GenerateNewSeed();

    /**
     * @brief 현재 초기 시드로 수열을 처음부터 다시 시작합니다.
     */
    void Reset();

    /**
     * @brief [0, 1) 구간의 실수를 뽑습니다.
     * @return 뽑은 값입니다.
     */
    float GetFraction();

    /**
     * @brief [Minimum, Maximum) 구간의 실수를 뽑습니다.
     * @return 뽑은 값입니다.
     */
    float FRandRange(float Minimum, float Maximum);

    /**
     * @brief 성분마다 따로 뽑은 벡터를 반환합니다.
     * @return 각 성분이 Minimum과 Maximum 사이인 벡터입니다.
     */
    FVector FRandRange(const FVector& Minimum, const FVector& Maximum);

    /**
     * @brief [Minimum, Maximum] 구간의 정수를 뽑습니다.
     * @return 뽑은 값입니다.
     */
    Int32 RandRange(Int32 Minimum, Int32 Maximum);

    /**
     * @brief 단위 구 표면에서 균일하게 뽑은 방향입니다.
     * @return 길이가 1인 벡터입니다.
     */
    FVector VRand();

private:
    /**
     * @brief 처음 지정한 시드입니다.
     */
    Int32 mInitialSeed{};

    /**
     * @brief 다음 값을 만들 현재 상태입니다.
     */
    std::uint32_t mSeed{};
};
