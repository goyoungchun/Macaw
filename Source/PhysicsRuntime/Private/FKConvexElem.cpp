/**
 * @file FKConvexElem.cpp
 * @brief 바디 셋업에 포함되는 볼록 다면체 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/FKConvexElem.h"

#include <algorithm>

namespace PhysicsRuntime {

    /**
     * @brief 컴포넌트 스케일을 반영한 볼록 형상을 반환합니다.
     */
    PhysicsRuntime::FKConvexElem PhysicsRuntime::FKConvexElem::GetScaled(const FVector& Scale) const {
        PhysicsRuntime::FKConvexElem Scaled{*this};

        for (FVector& Vertex : Scaled.mVertices) {
            Vertex = Vertex * Scale;
        }

        return Scaled;
    }

    /**
     * @brief 볼록 다면체를 만들 수 있는 정점과 인덱스를 가졌는지 검사합니다.
     */
    bool PhysicsRuntime::FKConvexElem::IsValid() const {
        const Uint32 VertexCount{static_cast<Uint32>(mVertices.size())};

        return VertexCount >= 4 && mIndices.size() % 3 == 0 && std::all_of(mIndices.begin(), mIndices.end(), [VertexCount](Uint32 Index) {
            return Index < VertexCount;
        });
    }

}
