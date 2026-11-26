/**
 * @file UVehicleMovementComponentTank.h
 * @brief 좌우 궤도 추력 차이로 선회하는 스키드 스티어 탱크입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UVehicleMovementComponent.h"
#include "UVehicleMovementComponentTank.generated.h"

namespace Engine {

    /**
     * @brief 바퀴는 좌·우 쌍을 앞에서부터 나열하며(0 전좌, 1 전우, 2 둘째 좌, ...) 4개 이상 짝수여야 합니다.
     * @details 모든 바퀴가 구동되고 조향하지 않습니다. `SetDriveInput()`의 조향은 좌우 추력 차이로 바뀌어,
     *          전진 중 양수면 +Y(우측)로 선회하고, 후진 중에는 자동차처럼 반대로 돕니다.
     *          가속 0에서 조향만 주면 1단 기어로 제자리 회전합니다.
     */
    UCLASS()
    class UVehicleMovementComponentTank : public Engine::UVehicleMovementComponent {
        GENERATED_BODY()

    protected:
        bool CreateVehicle(PhysicsRuntime::FVehicleManager& Manager, const PhysicsRuntime::FBodyInstance& Body,
                           const physx::PxVehicleWheelsSimData& Wheels) override;
    };

}
