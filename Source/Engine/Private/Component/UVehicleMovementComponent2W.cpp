/**
 * @file UVehicleMovementComponent2W.cpp
 * @brief 차체에 2륜 구동계를 등록합니다.
 * @ingroup Engine
 */

#include "Engine/Component/UVehicleMovementComponent2W.h"
#include "PhysicsRuntime/FVehicleManager.h"

namespace Engine {

    bool UVehicleMovementComponent2W::CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                                                   const physx::PxVehicleWheelsSimData& Wheels) {
        return Manager.RegisterDrive2W(Body, Wheels);
    }

}
