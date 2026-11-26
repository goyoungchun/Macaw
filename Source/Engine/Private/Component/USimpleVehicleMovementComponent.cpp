/**
 * @file USimpleVehicleMovementComponent.cpp
 * @brief 바퀴별 제어와 물리 자세를 차량에 적용합니다.
 * @ingroup Engine
 */

#include "Engine/Component/USimpleVehicleMovementComponent.h"
#include "PhysicsRuntime/FVehicleManager.h"

namespace Engine {

    bool USimpleVehicleMovementComponent::SetDriveTorque(Core::FTorque Torque, Uint32 WheelIndex) {
        const auto* Body{GetRegisteredBody()};
        auto* Manager{GetVehicleManager()};

        return Body != nullptr && Manager != nullptr && Manager->SetDriveTorque(*Body, WheelIndex, Torque);
    }

    bool USimpleVehicleMovementComponent::SetBrakeTorque(Core::FTorque Torque, Uint32 WheelIndex) {
        const auto* Body{GetRegisteredBody()};
        auto* Manager{GetVehicleManager()};

        return Body != nullptr && Manager != nullptr && Manager->SetBrakeTorque(*Body, WheelIndex, Torque);
    }

    bool USimpleVehicleMovementComponent::SetSteerAngle(Core::FAngle Angle, Uint32 WheelIndex) {
        const auto* Body{GetRegisteredBody()};
        auto* Manager{GetVehicleManager()};
        return Body != nullptr && Manager != nullptr && Manager->SetSteerAngle(*Body, WheelIndex, Angle);
    }

    bool USimpleVehicleMovementComponent::CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                                                       const physx::PxVehicleWheelsSimData& Wheels) {
        return Manager.RegisterNoDrive(Body, Wheels);
    }

}
