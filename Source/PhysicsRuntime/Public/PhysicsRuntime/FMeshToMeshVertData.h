/**
 * @file FMeshToMeshVertData.h
 * @brief 한 메시의 정점을 다른 메시의 삼각형 위에 붙이는 데이터를 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"

namespace Core {
    class FArchive;
}

namespace PhysicsRuntime {

    /**
     * @brief 대상 정점 하나를 원본 메시의 삼각형 하나로 표현한 값입니다.
     * @details 각 좌표는 (무게중심 좌표 3개, 삼각형 평면에서의 거리)입니다.
     *          원본 삼각형이 움직이면 같은 비율과 거리로 대상 정점의 위치, 법선, 탄젠트를 다시 구합니다.
     * @ingroup PhysicsRuntime
     */
    struct FMeshToMeshVertData {
        /**
         * @brief 원본 삼각형을 따르지 않고 스키닝만 쓴다는 표시 값입니다.
         */
        static constexpr Uint32 NoSimulationFlag{0xFFFF};

        /**
         * @brief 정점 위치의 무게중심 좌표와 평면 거리입니다.
         */
        FVector4 mPositionBaryCoordsAndDist{};

        /**
         * @brief 정점 위치에서 법선 방향으로 1만큼 간 점의 무게중심 좌표와 평면 거리입니다.
         */
        FVector4 mNormalBaryCoordsAndDist{};

        /**
         * @brief 정점 위치에서 탄젠트 방향으로 1만큼 간 점의 무게중심 좌표와 평면 거리입니다.
         */
        FVector4 mTangentBaryCoordsAndDist{};

        /**
         * @brief 앞 세 개는 원본 삼각형의 입자 번호이고, 마지막은 NoSimulationFlag이면 스키닝만 쓴다는 표시입니다.
         */
        Core::TStaticArray<Uint32, 4> mSourceMeshVertIndices{};

        /**
         * @brief 여러 삼각형의 영향을 섞을 때 쓰는 가중치입니다.
         */
        float mWeight{};
    };

    /**
     * @brief 정점 하나의 삼각형 대응을 저장하거나 불러옵니다.
     * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
     * @param[in,out] Data 저장하거나 채울 대응입니다.
     */
    void SerializeMeshToMeshVertData(Core::FArchive& Archive, FMeshToMeshVertData& Data);
}
