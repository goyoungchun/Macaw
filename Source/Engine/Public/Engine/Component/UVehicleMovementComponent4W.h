/**
 * @file UVehicleMovementComponent4W.h
 * @brief 엔진, 자동 변속기, 차동장치가 있는 4륜 구동 차량입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UVehicleMovementComponent.h"
#include "UVehicleMovementComponent4W.generated.h"

namespace Engine {

    /** @brief 바퀴는 전좌·전우·후좌·후우 순서이며 앞바퀴가 조향합니다. */
    UCLASS()
    class UVehicleMovementComponent4W : public Engine::UVehicleMovementComponent {
        GENERATED_BODY()

    protected:
        bool CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                           const physx::PxVehicleWheelsSimData& Wheels) override;
    };

}
