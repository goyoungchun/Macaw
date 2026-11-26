/**
 * @file UVehicleMovementComponentTank.cpp
 * @brief 차체에 탱크 구동계를 등록합니다.
 * @ingroup Engine
 */

#include "Engine/Component/UVehicleMovementComponentTank.h"
#include "PhysicsRuntime/FVehicleManager.h"

namespace Engine {

    bool UVehicleMovementComponentTank::CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                                                     const physx::PxVehicleWheelsSimData& Wheels) {
        return Manager.RegisterDriveTank(Body, Wheels);
    }

}
