#pragma once

#include "Core/Math/FVector.h"

namespace Engine::ParticleCurveMath {

    // 두 점과 각 점의 접선으로 정한 Hermite 곡선 위의 점입니다. 접선을 이웃 점 차이의 절반으로 주면 Catmull-Rom이 됩니다.
    inline FVector CubicInterp(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, float Alpha) {
        const float A2{Alpha * Alpha};
        const float A3{A2 * Alpha};

        return P0 * (2.0f * A3 - 3.0f * A2 + 1.0f) + T0 * (A3 - 2.0f * A2 + Alpha) + P1 * (-2.0f * A3 + 3.0f * A2) + T1 * (A3 - A2);
    }

}
