#include "Engine/Curves/FFloatCurveKey.h"

#include <algorithm>
#include <unordered_set>

namespace Engine::CurveKeys {

    float EvaluateSegment(const Engine::FFloatCurveKey& Left, const Engine::FFloatCurveKey& Right, float Time) {
        const float Length{Right.mTime - Left.mTime};

        if (Length <= 0.0001f || Left.mInterpMode == Engine::ECurveInterpMode::Constant) {
            return Left.mValue;
        }

        const float T{std::clamp((Time - Left.mTime) / Length, 0.0f, 1.0f)};

        if (Left.mInterpMode == Engine::ECurveInterpMode::Linear) {
            return Left.mValue + (Right.mValue - Left.mValue) * T;
        }

        // Hermite 기저. 접선이 값/초이므로 구간 길이를 곱해 정규화된 T에 맞춥니다.
        const float T2{T * T};
        const float T3{T2 * T};

        return (2.0f * T3 - 3.0f * T2 + 1.0f) * Left.mValue + (T3 - 2.0f * T2 + T) * Left.mLeaveTangent * Length + (-2.0f * T3 + 3.0f * T2) * Right.mValue + (T3 - T2) * Right.mArriveTangent * Length;
    }

    float Evaluate(const Core::TArray<Engine::FFloatCurveKey>& Keys, float Time) {
        if (Keys.empty()) {
            return 0.0f;
        }

        if (Time <= Keys.front().mTime) {
            return Keys.front().mValue;
        }

        if (Time >= Keys.back().mTime) {
            return Keys.back().mValue;
        }

        const auto Right{std::upper_bound(Keys.begin(), Keys.end(), Time, [](float Value, const Engine::FFloatCurveKey& Key) {
            return Value < Key.mTime;
        })};

        return EvaluateSegment(*(Right - 1), *Right, Time);
    }

    void SortKeys(Core::TArray<Engine::FFloatCurveKey>& Keys) {
        std::ranges::stable_sort(Keys, [](const Engine::FFloatCurveKey& A, const Engine::FFloatCurveKey& B) {
            return A.mTime < B.mTime;
        });
    }

    void EnsureKeyIds(Core::TArray<Engine::FFloatCurveKey>& Keys) {
        Int32 MaxId{-1};

        for (const Engine::FFloatCurveKey& Key : Keys) {
            MaxId = std::max(MaxId, Key.mKeyId);
        }

        std::unordered_set<Int32> Seen{};

        for (Engine::FFloatCurveKey& Key : Keys) {
            if (Key.mKeyId < 0 || !Seen.insert(Key.mKeyId).second) {
                Key.mKeyId = ++MaxId;
                Seen.insert(Key.mKeyId);
            }
        }
    }

    Int32 NewKeyId(const Core::TArray<Engine::FFloatCurveKey>& Keys) {
        Int32 MaxId{-1};

        for (const Engine::FFloatCurveKey& Key : Keys) {
            MaxId = std::max(MaxId, Key.mKeyId);
        }

        return MaxId + 1;
    }

    Int32 FindKeyIndex(const Core::TArray<Engine::FFloatCurveKey>& Keys, Int32 KeyId) {
        for (Index_t Index{}; Index < Keys.size(); ++Index) {
            if (Keys[Index].mKeyId == KeyId) {
                return static_cast<Int32>(Index);
            }
        }

        return -1;
    }

    void SetAutoTangent(Core::TArray<Engine::FFloatCurveKey>& Keys, Index_t Index) {
        if (Index >= Keys.size()) {
            return;
        }

        float Slope{};

        if (Index > 0 && Index + 1 < Keys.size()) {
            const Engine::FFloatCurveKey& Previous{Keys[Index - 1]};
            const Engine::FFloatCurveKey& Next{Keys[Index + 1]};
            const float Delta{Next.mTime - Previous.mTime};

            Slope = Delta > 0.0001f ? (Next.mValue - Previous.mValue) / Delta : 0.0f;
        }

        Keys[Index].mArriveTangent = Slope;
        Keys[Index].mLeaveTangent = Slope;
    }

    void SetFlatTangent(Engine::FFloatCurveKey& Key) {
        Key.mArriveTangent = 0.0f;
        Key.mLeaveTangent = 0.0f;
    }

}
