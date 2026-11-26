/**
 * @file UVehicleMovementComponent4W.cpp
 * @brief 차체에 4륜 구동계를 등록합니다.
 * @ingroup Engine
 */

#include "Engine/Component/UVehicleMovementComponent4W.h"
#include "PhysicsRuntime/FVehicleManager.h"

namespace Engine {

    bool UVehicleMovementComponent4W::CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                                                   const physx::PxVehicleWheelsSimData& Wheels) {
        return Manager.RegisterDrive4W(Body, Wheels);
    }

}
