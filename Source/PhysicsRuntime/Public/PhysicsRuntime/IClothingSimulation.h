/**
 * @file IClothingSimulation.h
 * @brief 컴포넌트가 천 시뮬레이션을 다루는 인터페이스를 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "PhysicsRuntime/FClothCollisionData.h"
#include "PhysicsRuntime/FClothSimulData.h"
#include "PhysicsRuntime/FClothingSimulationContextCommon.h"

namespace PhysicsRuntime {
    class UClothingAssetCommon;

    /**
     * @brief 천 에셋으로 천을 만들고 매 프레임 진행해 결과를 돌려주는 시뮬레이션입니다.
     * @details 컴포넌트는 이 인터페이스만 알고 실제 솔버 구현은 알지 않습니다.
     * @ingroup PhysicsRuntime
     */
    class IClothingSimulation {
    public:

        /**
         * @brief 빈 시뮬레이션을 구성합니다.
         */
        IClothingSimulation() = default;

        /**
         * @brief 구현이 만든 천과 솔버를 해제합니다.
         */
        virtual ~IClothingSimulation() = default;

        /**
         * @brief 천과 솔버를 두 객체가 해제하지 않도록 복사를 금지합니다.
         */
        IClothingSimulation(const IClothingSimulation&) = delete;

        /**
         * @brief 천과 솔버를 두 객체가 해제하지 않도록 대입을 금지합니다.
         */
        IClothingSimulation& operator=(const IClothingSimulation&) = delete;

    public:

        /**
         * @brief 천 에셋으로 천을 만들어 시뮬레이션에 넣습니다.
         * @param[in] Asset 천 에셋이며 천보다 오래 살아 있어야 합니다.
         * @param[in] SimDataIndex 결과를 돌려줄 때 쓸 번호입니다.
         * @param[in] Context 현재 포즈와 변환을 담은 입력입니다.
         * @return 천을 만들었으면 true입니다.
         */
        virtual bool CreateActor(const UClothingAssetCommon& Asset, Index_t SimDataIndex, const FClothingSimulationContextCommon& Context) = 0;

        /**
         * @brief 만든 천을 모두 해제합니다.
         */
        virtual void DestroyActors() = 0;

        /**
         * @brief 천을 모두 해제하고 솔버를 해제합니다.
         */
        virtual void Shutdown() = 0;

        /**
         * @brief 입력으로 모든 천을 한 프레임 진행합니다.
         * @param[in] Context 이번 프레임의 포즈, 변환, 시간, 중력입니다.
         */
        virtual void Simulate(const FClothingSimulationContextCommon& Context) = 0;

        /**
         * @brief 천마다 현재 입자 위치와 법선을 복사합니다.
         * @param[out] OutData 결과 번호를 키로 한 결과이며 시뮬레이션하지 않는 천은 넣지 않습니다.
         * @param[in] Context 결과의 기준 변환을 구할 때 쓰는 입력입니다.
         */
        virtual void GetSimulationData(Core::TMap<Index_t, FClothSimulData>& OutData, const FClothingSimulationContextCommon& Context) const = 0;

        /**
         * @brief 시뮬레이션할 천이 있는지 검사합니다.
         * @return 솔버에 들어간 천이 하나라도 있으면 true입니다.
         */
        virtual bool ShouldSimulate() const = 0;

        /**
         * @brief 모든 천에 바깥에서 정한 충돌체를 더합니다.
         * @param[in] Data 컴포넌트 좌표 또는 뼈 좌표의 충돌체입니다.
         */
        virtual void AddExternalCollisions(const FClothCollisionData& Data) = 0;

        /**
         * @brief 모든 천에서 바깥에서 정한 충돌체를 뺍니다.
         */
        virtual void ClearExternalCollisions() = 0;

        /**
         * @brief 천들이 쓰는 충돌체를 모읍니다.
         * @param[out] OutCollisions 모은 충돌체이며 기존 내용은 지웁니다.
         * @param[in] IncludeExternal true이면 물리 에셋과 바깥 충돌체까지, false이면 천 에셋에 넣어 둔 충돌체만 모읍니다.
         */
        virtual void GetCollisions(FClothCollisionData& OutCollisions, bool IncludeExternal = true) const = 0;
    };
}
