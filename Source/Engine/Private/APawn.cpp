/**
 * @file APawn.cpp
 * @brief Pawn 이동 입력의 누적과 소비를 처리합니다.
 * @ingroup Engine
 */

#include "Engine/APawn.h"

namespace Engine {

    void APawn::AddMovementInput(const FVector& WorldDirection, float ScaleValue) {
        const FVector Sum{mPendingMovementInput + WorldDirection * ScaleValue};

        if (Sum.IsFinite()) {
            mPendingMovementInput = Sum;
        }
    }

    FVector APawn::GetPendingMovementInputVector() const {
        return mPendingMovementInput;
    }

    FVector APawn::GetLastMovementInputVector() const {
        return mLastMovementInput;
    }

    FVector APawn::ConsumeMovementInputVector() {
        mLastMovementInput = mPendingMovementInput;
        mPendingMovementInput = {};

        return mLastMovementInput;
    }

    void APawn::EndPlay() {
        mPendingMovementInput = {};
        mLastMovementInput = {};

        Engine::AActor::EndPlay();
    }

}
