#include "Engine/Particles/FParticleDistribution.h"
#include "Core/Math/FRandomStream.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Engine {

    namespace {

        /**
         * @brief 한 칸에 들어가는 실수의 최대 개수입니다(벡터 최솟값 3 + 최댓값 3).
         */
        constexpr Index_t MaxValuesPerEntry{6};

        /**
         * @brief 커브를 굽는 칸 수입니다. 많을수록 원본에 가깝습니다.
         */
        constexpr Int32 CurveBakeSampleCount{32};

        template <typename FSample>
        void BuildLookupTable(Engine::FDistributionLookupTable& Table, Engine::EDistributionLookupOperation Operation, Int32 ValuesPerEntry, Int32 EntryCount, float MinimumTime, float MaximumTime, FSample&& Sample) {
            const bool Random{Operation == Engine::EDistributionLookupOperation::Random};

            Table.mOperation = Operation;
            Table.mEntryCount = static_cast<Uint8>(EntryCount);
            Table.mEntryStride = static_cast<Uint8>((Random ? 2 : 1) * ValuesPerEntry);
            Table.mSubEntryStride = static_cast<Uint8>(Random ? ValuesPerEntry : 0);

            const float TimeGap{EntryCount > 1 ? (MaximumTime - MinimumTime) / static_cast<float>(EntryCount - 1) : 0.0f};

            Table.mTimeScale = TimeGap > 0.0f ? 1.0f / TimeGap : 0.0f;
            Table.mTimeBias = MinimumTime;
            Table.mValues.clear();
            Table.mValues.reserve(static_cast<Index_t>(EntryCount) * Table.mEntryStride);

            for (Int32 Index{}; Index < EntryCount; ++Index) {
                float Values[MaxValuesPerEntry]{};

                Sample(MinimumTime + static_cast<float>(Index) * TimeGap, Values);
                Table.mValues.insert(Table.mValues.end(), Values, Values + Table.mEntryStride);
            }
        }

        float Lerp(float A, float B, float Alpha) {
            return A + (B - A) * Alpha;
        }

    }

    void FDistributionLookupTable::Empty() {
        *this = {};
    }

    bool FDistributionLookupTable::IsEmpty() const {
        return mValues.empty() || mEntryCount == 0;
    }

    void FDistributionLookupTable::GetEntry(float Time, const float*& First, const float*& Second, float& Alpha) const {
        Time = std::max((Time - mTimeBias) * mTimeScale, 0.0f);
        Alpha = Time - std::trunc(Time);

        const Index_t Index{static_cast<Index_t>(Time)};
        const Index_t Last{static_cast<Index_t>(mEntryCount) - 1};

        First = &mValues[std::min(Index, Last) * mEntryStride];
        Second = &mValues[std::min(Index + 1, Last) * mEntryStride];
    }

    void FFloatDistribution::Bake() {
        Engine::CurveKeys::SortKeys(mCurveKeys);

        // 키가 0개나 1개인 커브는 상수와 같습니다.
        Engine::EDistributionMode Mode{mMode};
        float Constant{mConstant};

        if (Mode == Engine::EDistributionMode::Curve && mCurveKeys.size() <= 1) {
            Mode = Engine::EDistributionMode::Constant;
            Constant = mCurveKeys.empty() ? 0.0f : mCurveKeys.front().mValue;
        }

        switch (Mode) {
        case Engine::EDistributionMode::Constant:
            BuildLookupTable(mLookupTable, Engine::EDistributionLookupOperation::None, 1, 1, 0.0f, 0.0f, [Constant](float, float* Out) {
                Out[0] = Constant;
            });
            break;
        case Engine::EDistributionMode::Uniform:
            BuildLookupTable(mLookupTable, Engine::EDistributionLookupOperation::Random, 1, 1, 0.0f, 0.0f, [this](float, float* Out) {
                Out[0] = mMinValue;
                Out[1] = mMaxValue;
            });
            break;
        case Engine::EDistributionMode::Curve:
            BuildLookupTable(mLookupTable, Engine::EDistributionLookupOperation::None, 1, CurveBakeSampleCount, mCurveKeys.front().mTime, mCurveKeys.back().mTime, [this](float Time, float* Out) {
                Out[0] = Engine::CurveKeys::Evaluate(mCurveKeys, Time);
            });
            break;
        }
    }

    float FFloatDistribution::GetValue(float Time, FRandomStream& Random) const {
        if (mLookupTable.IsEmpty()) {
            return 0.0f;
        }

        const float* First{};
        const float* Second{};
        float Alpha{};

        mLookupTable.GetEntry(Time, First, Second, Alpha);

        const float Value{Lerp(First[0], Second[0], Alpha)};

        if (mLookupTable.mOperation != Engine::EDistributionLookupOperation::Random) {
            return Value;
        }

        const Index_t MaximumOffset{mLookupTable.mSubEntryStride};
        const float Maximum{Lerp(First[MaximumOffset], Second[MaximumOffset], Alpha)};

        return Value + (Maximum - Value) * Random.GetFraction();
    }

    void FVectorDistribution::Bake() {
        for (Core::TArray<Engine::FFloatCurveKey>* Keys : {&mCurveKeysX, &mCurveKeysY, &mCurveKeysZ}) {
            Engine::CurveKeys::SortKeys(*Keys);
        }

        switch (mMode) {
        case Engine::EDistributionMode::Constant:
            BuildLookupTable(mLookupTable, Engine::EDistributionLookupOperation::None, 3, 1, 0.0f, 0.0f, [this](float, float* Out) {
                Out[0] = mConstant.x;
                Out[1] = mConstant.y;
                Out[2] = mConstant.z;
            });
            break;
        case Engine::EDistributionMode::Uniform:
            BuildLookupTable(mLookupTable, Engine::EDistributionLookupOperation::Random, 3, 1, 0.0f, 0.0f, [this](float, float* Out) {
                Out[0] = mMinValue.x;
                Out[1] = mMinValue.y;
                Out[2] = mMinValue.z;
                Out[3] = mMaxValue.x;
                Out[4] = mMaxValue.y;
                Out[5] = mMaxValue.z;
            });
            break;
        case Engine::EDistributionMode::Curve: {
            float MinimumTime{FLT_MAX};
            float MaximumTime{-FLT_MAX};

            for (const Core::TArray<Engine::FFloatCurveKey>* Keys : {&mCurveKeysX, &mCurveKeysY, &mCurveKeysZ}) {
                if (!Keys->empty()) {
                    MinimumTime = std::min(MinimumTime, Keys->front().mTime);
                    MaximumTime = std::max(MaximumTime, Keys->back().mTime);
                }
            }

            if (MinimumTime > MaximumTime) {
                MinimumTime = 0.0f;
                MaximumTime = 0.0f;
            }

            const Int32 EntryCount{MaximumTime > MinimumTime ? CurveBakeSampleCount : 1};

            BuildLookupTable(mLookupTable, Engine::EDistributionLookupOperation::None, 3, EntryCount, MinimumTime, MaximumTime, [this](float Time, float* Out) {
                Out[0] = Engine::CurveKeys::Evaluate(mCurveKeysX, Time);
                Out[1] = Engine::CurveKeys::Evaluate(mCurveKeysY, Time);
                Out[2] = Engine::CurveKeys::Evaluate(mCurveKeysZ, Time);
            });
            break;
        }
        }
    }

    FVector FVectorDistribution::GetValue(float Time, FRandomStream& Random) const {
        if (mLookupTable.IsEmpty()) {
            return {};
        }

        const float* First{};
        const float* Second{};
        float Alpha{};

        mLookupTable.GetEntry(Time, First, Second, Alpha);

        const FVector Value{Lerp(First[0], Second[0], Alpha), Lerp(First[1], Second[1], Alpha), Lerp(First[2], Second[2], Alpha)};

        if (mLookupTable.mOperation != Engine::EDistributionLookupOperation::Random) {
            return Value;
        }

        const Index_t Offset{mLookupTable.mSubEntryStride};
        const FVector Maximum{Lerp(First[Offset], Second[Offset], Alpha), Lerp(First[Offset + 1], Second[Offset + 1], Alpha), Lerp(First[Offset + 2], Second[Offset + 2], Alpha)};
        const float RandomX{Random.GetFraction()};
        const float RandomY{mLockAxes ? RandomX : Random.GetFraction()};
        const float RandomZ{mLockAxes ? RandomX : Random.GetFraction()};

        return Value + (Maximum - Value) * FVector{RandomX, RandomY, RandomZ};
    }

}
