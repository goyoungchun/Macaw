/**
 * @file FFloatCurveKey.h
 * @brief 시간에 따른 실수 커브의 키와 평가 함수입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "FFloatCurveKey.generated.h"

namespace Engine {

    /**
     * @brief 이 키에서 다음 키까지 값을 잇는 방식입니다.
     */
    UENUM()
    enum class ECurveInterpMode : Uint8 {
        Constant,
        Linear,
        Cubic
    };

    /**
     * @brief 커브의 한 점입니다.
     * @details 접선은 값/초 단위라 구간 길이가 바뀌어도 기울기 의미가 유지됩니다.
     */
    USTRUCT()
    struct FFloatCurveKey {
        GENERATED_BODY()

    public:
        /**
         * @brief 정렬·이동 뒤에도 같은 키를 가리키는 식별자이며 배열 인덱스가 아닙니다.
         */
        UPROPERTY(Serialize)
        Int32 mKeyId{-1};

        UPROPERTY(Edit, Serialize)
        float mTime{};

        UPROPERTY(Edit, Serialize)
        float mValue{};

        /**
         * @brief 이 키에서 다음 키까지의 보간 방식입니다.
         */
        UPROPERTY(Edit, Serialize)
        Engine::ECurveInterpMode mInterpMode{Engine::ECurveInterpMode::Linear};

        UPROPERTY(Edit, Serialize)
        float mArriveTangent{};

        UPROPERTY(Edit, Serialize)
        float mLeaveTangent{};
    };

    /**
     * @brief 시간순으로 정렬된 키 배열을 다루는 보조 함수입니다.
     */
    namespace CurveKeys {

        /**
         * @brief 두 키 사이 구간을 왼쪽 키의 보간 방식으로 평가합니다.
         */
        float EvaluateSegment(const Engine::FFloatCurveKey& Left, const Engine::FFloatCurveKey& Right, float Time);

        /**
         * @brief 정렬된 키 배열을 평가하며 범위 밖은 끝 키 값으로 고정합니다.
         * @return 키가 없으면 0입니다.
         */
        float Evaluate(const Core::TArray<Engine::FFloatCurveKey>& Keys, float Time);

        /**
         * @brief 같은 시간의 순서를 유지하며 시간순으로 정렬합니다.
         */
        void SortKeys(Core::TArray<Engine::FFloatCurveKey>& Keys);

        /**
         * @brief 비었거나 겹친 식별자에 새 식별자를 줍니다.
         */
        void EnsureKeyIds(Core::TArray<Engine::FFloatCurveKey>& Keys);

        /**
         * @brief 아직 쓰지 않은 새 식별자를 반환합니다.
         */
        Int32 NewKeyId(const Core::TArray<Engine::FFloatCurveKey>& Keys);

        /**
         * @brief 식별자로 키 인덱스를 찾습니다.
         * @return 없으면 -1입니다.
         */
        Int32 FindKeyIndex(const Core::TArray<Engine::FFloatCurveKey>& Keys, Int32 KeyId);

        /**
         * @brief 양옆 키를 잇는 기울기로 접선을 맞춥니다. 끝 키는 평평하게 둡니다.
         */
        void SetAutoTangent(Core::TArray<Engine::FFloatCurveKey>& Keys, Index_t Index);

        /**
         * @brief 접선을 0으로 만듭니다.
         */
        void SetFlatTangent(Engine::FFloatCurveKey& Key);
    }

}
