/**
 * @file USimpleVehicleMovementComponent.h
 * @brief 바퀴별 구동과 정적 메시 표시를 연결합니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UVehicleMovementComponent.h"
#include "USimpleVehicleMovementComponent.generated.h"

namespace Engine {

    /** @brief NoDrive 차량에 바퀴별 입력을 전달합니다. 조작값은 다음 입력 또는 바디 재등록까지 유지됩니다. */
    UCLASS()
    class USimpleVehicleMovementComponent : public Engine::UVehicleMovementComponent {
        GENERATED_BODY()

    public:
        /** @brief 등록된 바퀴의 구동 토크(N·m)를 지정합니다. 음수는 역방향 토크입니다. */
        bool SetDriveTorque(Core::FTorque Torque, Uint32 WheelIndex);

        /** @brief 등록된 바퀴의 제동 토크(N·m)를 0 이상으로 지정합니다. */
        bool SetBrakeTorque(Core::FTorque Torque, Uint32 WheelIndex);

        /** @brief 등록된 바퀴의 조향각을 지정합니다. */
        bool SetSteerAngle(Core::FAngle Angle, Uint32 WheelIndex);

    protected:
        bool CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                           const physx::PxVehicleWheelsSimData& Wheels) override;
    };

}
