/**
 * @file UVehicleMovementComponent2W.h
 * @brief 앞바퀴로 조향하고 뒷바퀴로 구동하는 2륜 차량입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UVehicleMovementComponent.h"
#include "UVehicleMovementComponent2W.generated.h"

namespace Engine {

    /**
     * @brief 바퀴는 앞(0)·뒤(1) 순서입니다.
     * @details 차체가 쓰러지지 않도록 물리 스텝마다 롤 안정화 토크를 가합니다. 직진·정지 중에는 서 있고, 회전할 때는 안쪽으로 기웁니다.
     */
    UCLASS()
    class UVehicleMovementComponent2W : public Engine::UVehicleMovementComponent {
        GENERATED_BODY()

    protected:
        bool CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                           const physx::PxVehicleWheelsSimData& Wheels) override;
    };

}
