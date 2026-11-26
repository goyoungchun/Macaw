/**
 * @file APawn.h
 * @brief 조작 입력을 누적하고 소비하는 액터입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Engine/AActor.h"
#include "APawn.generated.h"

namespace Engine {

    /** @brief 이동 컴포넌트와 공유하는 런타임 입력을 보관합니다. */
    UCLASS()
    class APawn : public Engine::AActor {
        GENERATED_BODY()

    public:
        /** @brief 월드 방향에 배율을 곱해 대기 입력에 더합니다. 합계는 유한한 경우에만 저장합니다. */
        void AddMovementInput(const FVector& WorldDirection, float ScaleValue = 1.0f);

        /** @brief 아직 소비하지 않은 입력의 합을 반환합니다. */
        FVector GetPendingMovementInputVector() const;

        /** @brief 마지막 소비 시 반환한 입력을 조회합니다. */
        FVector GetLastMovementInputVector() const;

        /** @brief 대기 입력을 반환하고 마지막 입력으로 저장한 뒤 대기 입력을 비웁니다. */
        FVector ConsumeMovementInputVector();

        void EndPlay() override;

    private:
        FVector mPendingMovementInput{};
        FVector mLastMovementInput{};
    };

}
