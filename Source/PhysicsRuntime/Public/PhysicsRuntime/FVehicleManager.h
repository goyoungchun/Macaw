/**
 * @file FVehicleManager.h
 * @brief 물리 장면의 차량 인스턴스와 서스펜션 쿼리를 관리합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"

#include <memory>
#include <span>

namespace physx {
    class PxScene;
    class PxShape;
    class PxVehicleWheelsSimData;
    struct PxWheelQueryResult;
}

namespace PhysicsRuntime {

    struct FBodyInstance;
    class FPhysicsRuntime;
    class FPhysicsScene;

    /** @brief 장면에 등록된 차체의 차량 구동과 바퀴 결과를 관리합니다. */
    class FVehicleManager {
    public:
        FVehicleManager(physx::PxScene& Scene, FPhysicsRuntime& Runtime);
        ~FVehicleManager();

        /** @brief 장면에 속한 동적 바디에 NoDrive 차량을 한 대 등록합니다. */
        bool RegisterNoDrive(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels);

        /** @brief 전좌·전우·후좌·후우 바퀴로 4륜 구동 차량을 등록합니다. */
        bool RegisterDrive4W(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels);

        /**
         * @brief 좌·우 쌍을 앞에서부터 나열한 4개 이상 짝수 바퀴로 스키드 스티어 탱크를 등록합니다.
         * @details 모든 바퀴가 구동되고 조향하지 않습니다. 조향 입력은 좌우 추력 차이로 바뀌며, 가속 0에서 조향만 주면 제자리에서 회전합니다.
         */
        bool RegisterDriveTank(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels);

        /**
         * @brief 앞바퀴(0)가 조향하고 뒷바퀴(1)가 구동하는 2륜 차량을 등록합니다.
         * @details 매 물리 스텝 차체에 롤 안정화 토크를 가해 직진·정지 중에는 서 있고, 회전할 때는 안쪽으로 기울게 합니다.
         */
        bool RegisterDrive2W(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels);

        /** @brief 엔진이 있는 차량에 다음 물리 스텝부터 사용할 입력입니다. 음수 가속은 후진 요청입니다. NoDrive는 거부합니다. */
        bool SetDriveInput(const FBodyInstance& Body, float Throttle, float Brake, float Steering);

        /** @brief 엔진이 있는 차량의 엔진, 변속기, 입력과 강체 속도를 초기화합니다. 차체 위치는 보존합니다. */
        bool ResetDrive(const FBodyInstance& Body);

        /** @brief 전진 기어 번호를 반환합니다. 후진은 -1, 중립·NoDrive·미등록 상태는 0입니다. */
        Int32 GetDriveGear(const FBodyInstance& Body) const;

        /** @brief 바디가 유지된 상태에서 해당 차량 인스턴스와 제약을 해제합니다. */
        bool UnregisterVehicle(const FBodyInstance& Body);

        /** @brief 다음 스텝부터 바퀴에 적용할 구동 토크(N·m)를 지정합니다. */
        bool SetDriveTorque(const FBodyInstance& Body, Uint32 WheelIndex, Core::FTorque Torque);

        /** @brief 다음 스텝부터 바퀴에 적용할 제동 토크(N·m)를 지정합니다. */
        bool SetBrakeTorque(const FBodyInstance& Body, Uint32 WheelIndex, Core::FTorque Torque);

        /** @brief 다음 스텝부터 바퀴에 적용할 조향각(라디안)을 지정합니다. */
        bool SetSteerAngle(const FBodyInstance& Body, Uint32 WheelIndex, Core::FAngle Angle);

        /** @brief 마지막 업데이트의 바퀴 결과를 빌립니다. 스텝 진행이나 장면 구성 변경 전에 사용합니다. */
        std::span<const physx::PxWheelQueryResult> GetWheelQueryResults(const FBodyInstance& Body) const;

        Index_t GetVehicleCount() const;

        /** @brief 지정한 형상을 서스펜션 레이가 검사할 주행면으로 표시하거나 해제합니다. */
        static void SetDrivableSurface(physx::PxShape& Shape, bool Drivable);

    private:
        friend class FPhysicsScene;

        struct FVehicle;
        struct FWheelControl;
        struct FRaycastBatch;

        FVehicle* FindVehicle(const FBodyInstance& Body);
        const FVehicle* FindVehicle(const FBodyInstance& Body) const;
        FWheelControl* FindWheelControl(const FBodyInstance& Body, Uint32 WheelIndex);
        bool PrepareVehicle(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels, FVehicle& Vehicle);
        bool EnsureRaycastCapacity(Uint32 WheelCount);
        void Step(Core::FTime StepTime);

        // 장면과 런타임은 차량 및 쿼리보다 오래 유지됩니다.
        physx::PxScene& mScene;
        FPhysicsRuntime& mRuntime;
        std::unique_ptr<FRaycastBatch> mRaycastBatch{};
        Core::TArray<FVehicle> mVehicles{};
    };

}
