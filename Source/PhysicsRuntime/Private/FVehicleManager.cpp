/**
 * @file FVehicleManager.cpp
 * @brief 차량 입력을 적용하고 서스펜션 쿼리와 차량 업데이트를 실행합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FVehicleManager.h"
#include "Core/Literals/UnitLiterals.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/PhysicsStats.h"

#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleDriveNW.h>
#include <vehicle/PxVehicleDriveTank.h>
#include <vehicle/PxVehicleNoDrive.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleUtilControl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace PhysicsRuntime {

    namespace {
        using namespace Core::Literals;

        // PhysX의 클러치 강도는 kg·m²/s로 전달합니다.
        using FClutchStrength = Core::TQuantity<Core::Dim::MomentOfInertia - Core::Dim::Time, float>;

        constexpr physx::PxU32 DrivableSurfaceFlag{1u << 31};
        constexpr physx::PxU32 StandardTireType{0};

        physx::PxQueryHitType::Enum FilterSuspensionRay(physx::PxFilterData, physx::PxFilterData ShapeFilter, const void*, physx::PxU32, physx::PxHitFlags&) {
            return (ShapeFilter.word3 & DrivableSurfaceFlag) != 0 ? physx::PxQueryHitType::eBLOCK : physx::PxQueryHitType::eNONE;
        }

        struct FFreeVehicle {
            void operator()(physx::PxVehicleWheels* Vehicle) const {
                switch (Vehicle->getVehicleType()) {
                case physx::PxVehicleTypes::eDRIVE4W:
                    static_cast<physx::PxVehicleDrive4W*>(Vehicle)->free();
                    break;
                case physx::PxVehicleTypes::eDRIVETANK:
                    static_cast<physx::PxVehicleDriveTank*>(Vehicle)->free();
                    break;
                case physx::PxVehicleTypes::eDRIVENW:
                    static_cast<physx::PxVehicleDriveNW*>(Vehicle)->free();
                    break;
                default:
                    static_cast<physx::PxVehicleNoDrive*>(Vehicle)->free();
                    break;
                }
            }
        };

        /** @brief 엔진이 있는 차량이면 공통 구동 상태를 반환합니다. NoDrive는 nullptr입니다. */
        physx::PxVehicleDrive* AsDrive(physx::PxVehicleWheels& Vehicle) {
            return Vehicle.getVehicleType() != physx::PxVehicleTypes::eNODRIVE ? static_cast<physx::PxVehicleDrive*>(&Vehicle) : nullptr;
        }

        const physx::PxVehicleDrive* AsDrive(const physx::PxVehicleWheels& Vehicle) {
            return AsDrive(const_cast<physx::PxVehicleWheels&>(Vehicle));
        }

        void SetDriveToRestState(physx::PxVehicleDrive& Drive) {
            switch (Drive.getVehicleType()) {
            case physx::PxVehicleTypes::eDRIVE4W:
                static_cast<physx::PxVehicleDrive4W&>(Drive).setToRestState();
                break;
            case physx::PxVehicleTypes::eDRIVETANK:
                static_cast<physx::PxVehicleDriveTank&>(Drive).setToRestState();
                break;
            case physx::PxVehicleTypes::eDRIVENW:
                static_cast<physx::PxVehicleDriveNW&>(Drive).setToRestState();
                break;
            default:
                break;
            }
        }

        struct FDriveInput {
            float mThrottle{};
            float mBrake{};
            float mSteering{};
        };

        struct FPedals {
            float mAccel{};
            float mBrake{};
        };

        /** @brief 엔진·기어·자동 변속·클러치를 설정합니다. 차종별 차이는 최대 토크로 지정합니다. */
        void FillDrivetrain(physx::PxVehicleDriveSimData& Data, Core::FTorque PeakTorque) {
            physx::PxVehicleEngineData Engine{};
            Engine.mPeakTorque = PeakTorque.GetBase();
            Engine.mMaxOmega = (540_radps).GetBase();
            Engine.mTorqueCurve.clear();
            Engine.mTorqueCurve.addPair(0.0f, 0.82f);
            Engine.mTorqueCurve.addPair(0.35f, 1.0f);
            Engine.mTorqueCurve.addPair(0.72f, 0.86f);
            Engine.mTorqueCurve.addPair(1.0f, 0.45f);

            physx::PxVehicleGearsData Gears{};
            Gears.mRatios[physx::PxVehicleGearsData::eREVERSE] = -3.4f;
            Gears.mRatios[physx::PxVehicleGearsData::eFIRST] = 3.6f;
            Gears.mRatios[physx::PxVehicleGearsData::eSECOND] = 2.3f;
            Gears.mSwitchTime = (0.24_s).GetBase();

            physx::PxVehicleAutoBoxData AutoBox{};
            AutoBox.setLatency((0.65_s).GetBase());
            for (auto Gear{static_cast<physx::PxU32>(physx::PxVehicleGearsData::eFIRST)}; Gear < Gears.mNbRatios; ++Gear) {
                AutoBox.mUpRatios[Gear] = 0.58f;
                AutoBox.mDownRatios[Gear] = 0.27f;
            }

            physx::PxVehicleClutchData Clutch{};
            Clutch.mStrength = FClutchStrength::FromBase(10.0f).GetBase();

            Data.setEngineData(Engine);
            Data.setGearsData(Gears);
            Data.setAutoBoxData(AutoBox);
            Data.setClutchData(Clutch);
        }

        void StartInFirstGear(physx::PxVehicleDrive& Drive) {
            Drive.mDriveDynData.forceGearChange(physx::PxVehicleGearsData::eFIRST);
            Drive.mDriveDynData.setUseAutoGears(true);
        }

        /** @brief 진행 방향을 바꿀 때는 먼저 멈추고, 전진 기어에 돌아오면 자동 변속을 이어갑니다. */
        FPedals ResolveDriveDirection(physx::PxVehicleDrive& Drive, const FDriveInput& Input) {
            auto& Dynamics{Drive.mDriveDynData};
            FPedals Pedals{std::abs(Input.mThrottle), Input.mBrake};

            if (Pedals.mAccel > 0.0f && Pedals.mBrake == 0.0f) {
                const bool Reverse{Input.mThrottle < 0.0f};
                const bool InReverse{Dynamics.getCurrentGear() == physx::PxVehicleGearsData::eREVERSE};
                if (Reverse != InReverse) {
                    constexpr auto DirectionChangeSpeed{0.5_mps};
                    const auto ForwardSpeed{Core::FSpeed::FromBase(Drive.computeForwardSpeed())};
                    if (Core::Math::Abs(ForwardSpeed) > DirectionChangeSpeed) {
                        Pedals.mBrake = std::max(Pedals.mBrake, Pedals.mAccel);
                        Pedals.mAccel = 0.0f;
                    } else {
                        Dynamics.forceGearChange(Reverse ? physx::PxVehicleGearsData::eREVERSE : physx::PxVehicleGearsData::eFIRST);
                        Dynamics.setUseAutoGears(!Reverse);
                    }
                }
            }

            return Pedals;
        }

        void ResetWheelResults(std::span<physx::PxWheelQueryResult> Results, const physx::PxVehicleWheelsSimData& Wheels,
                               const physx::PxRigidDynamic& Actor) {
            // 첫 물리 스텝 전에는 차체 로컬 기준 자세를 사용합니다.
            const auto CenterOfMass{Actor.getCMassLocalPose()};
            for (physx::PxU32 Wheel{}; Wheel < Results.size(); ++Wheel) {
                auto& Result{Results[Wheel]};
                Result = {};
                Result.localPose.p = CenterOfMass.transform(Wheels.getWheelCentreOffset(Wheel));
            }
        }

        /** @brief 조향 차량(4W, 2W)이 공유하는 페달·조향 스무딩입니다. */
        const physx::PxVehiclePadSmoothingData& GetSteeredSmoothing() {
            constexpr auto AccelRise{4.0f / 1_s};
            constexpr auto AccelFall{8.0f / 1_s};
            constexpr auto BrakeRise{8.0f / 1_s};
            constexpr auto BrakeFall{10.0f / 1_s};
            constexpr auto SteerRise{3.0f / 1_s};
            constexpr auto SteerFall{5.0f / 1_s};
            static constexpr physx::PxVehiclePadSmoothingData Smoothing{
                {AccelRise.GetBase(), BrakeRise.GetBase(), BrakeRise.GetBase(), SteerRise.GetBase(), SteerRise.GetBase()},
                {AccelFall.GetBase(), BrakeFall.GetBase(), BrakeFall.GetBase(), SteerFall.GetBase(), SteerFall.GetBase()}
            };
            return Smoothing;
        }

        /** @brief 속도가 높을수록 허용 조향 비율을 줄입니다. */
        const physx::PxFixedSizeLookupTable<8>& GetSteerBySpeed() {
            static const auto SteerBySpeed{[] {
                physx::PxFixedSizeLookupTable<8> Table{};
                Table.addPair((0_mps).GetBase(), 1.0f);
                Table.addPair((7_mps).GetBase(), 0.70f);
                Table.addPair((15_mps).GetBase(), 0.40f);
                Table.addPair((25_mps).GetBase(), 0.22f);
                Table.addPair((36_mps).GetBase(), 0.12f);
                return Table;
            }()};
            return SteerBySpeed;
        }

        /** @brief 4W와 NW의 원시 입력은 같은 구조입니다. */
        void FillSteeredRawInput(physx::PxVehicleDrive& Drive, const FDriveInput& Input, physx::PxVehicleDrive4WRawInputData& Raw) {
            const auto Pedals{ResolveDriveDirection(Drive, Input)};
            Raw.setAnalogAccel(Pedals.mBrake > 0.0f ? 0.0f : Pedals.mAccel);
            Raw.setAnalogBrake(Pedals.mBrake);
            Raw.setAnalogSteer(Input.mSteering);
        }

        void ApplyDrive4WInput(physx::PxVehicleDrive4W& Drive, const FDriveInput& Input, Core::FTime StepTime, bool InAir) {
            physx::PxVehicleDrive4WRawInputData Raw{};
            FillSteeredRawInput(Drive, Input, Raw);
            physx::PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(GetSteeredSmoothing(), GetSteerBySpeed(), Raw, StepTime.GetBase(), InAir, Drive);
        }

        void ApplyDriveNWInput(physx::PxVehicleDriveNW& Drive, const FDriveInput& Input, Core::FTime StepTime, bool InAir) {
            physx::PxVehicleDriveNWRawInputData Raw{};
            FillSteeredRawInput(Drive, Input, Raw);
            physx::PxVehicleDriveNWSmoothAnalogRawInputsAndSetAnalogInputs(GetSteeredSmoothing(), GetSteerBySpeed(), Raw, StepTime.GetBase(), InAir, Drive);
        }

        /**
         * @brief 2륜 차체가 쓰러지지 않도록 전방축 기준 롤을 PD 제어합니다.
         * @details 목표 기울기는 원심력 평형각 atan(v·ω_yaw / g)을 줄여 쓰는 게임식 값이라, 회전하는 쪽으로 살짝 기울고 직진·정지 중에는 수직을 유지합니다.
         *          요 각속도는 차체 흔들림에 민감하므로 목표를 저역 통과로 다듬어 기울기와 요 각속도가 서로 키우는 진동을 막습니다.
         *          질량·관성에 무관하게 튜닝하도록 각가속도로 가합니다.
         */
        void ApplyRollStabilizer(physx::PxRigidDynamic& Actor, const physx::PxVec3& Gravity, Core::FTime StepTime, float& SmoothedTargetRoll) {
            constexpr float RollStiffness{150.0f}; // 1/s²
            constexpr float RollDamping{22.0f};    // 1/s, 임계 감쇠(2√k ≈ 24.5)보다 약간 작게
            constexpr float LeanScale{0.5f};       // 물리 평형각 대비 비율
            constexpr auto MaxLean{20_deg};
            constexpr auto TargetSmoothingTime{0.25_s};

            const float GravityMagnitude{Gravity.magnitude()};
            const physx::PxVec3 WorldUp{-Gravity / GravityMagnitude};
            const auto Rotation{Actor.getGlobalPose().q};
            const auto Forward{Rotation.rotate({1.0f, 0.0f, 0.0f})};
            const auto Side{Rotation.rotate({0.0f, 1.0f, 0.0f})};
            const auto Up{Rotation.rotate({0.0f, 0.0f, 1.0f})};
            const auto AngularVelocity{Actor.getAngularVelocity()};

            // 전방축 기준 오른손 회전각입니다. 양수면 +Y 쪽이 올라가 -Y 쪽으로 기웁니다.
            const float Roll{std::atan2(Side.dot(WorldUp), Up.dot(WorldUp))};
            const float ForwardSpeed{Actor.getLinearVelocity().dot(Forward)};
            const float YawRate{AngularVelocity.dot(WorldUp)};
            const float RawTargetRoll{-std::clamp(LeanScale * std::atan(ForwardSpeed * YawRate / GravityMagnitude), -MaxLean.GetBase(), MaxLean.GetBase())};
            const float Blend{1.0f - std::exp(-(StepTime / TargetSmoothingTime).GetBase())};
            SmoothedTargetRoll += (RawTargetRoll - SmoothedTargetRoll) * Blend;

            const float RollAcceleration{RollStiffness * (SmoothedTargetRoll - Roll) - RollDamping * AngularVelocity.dot(Forward)};
            Actor.addTorque(Forward * RollAcceleration, physx::PxForceMode::eACCELERATION);
        }

        void ApplyDriveTankInput(physx::PxVehicleDriveTank& Drive, const FDriveInput& Input, Core::FTime StepTime) {
            constexpr auto AccelRise{4.0f / 1_s};
            constexpr auto AccelFall{8.0f / 1_s};
            constexpr auto BrakeRise{8.0f / 1_s};
            constexpr auto BrakeFall{10.0f / 1_s};
            constexpr auto ThrustRise{4.0f / 1_s};
            constexpr auto ThrustFall{6.0f / 1_s};
            static constexpr physx::PxVehiclePadSmoothingData Smoothing{
                {AccelRise.GetBase(), BrakeRise.GetBase(), BrakeRise.GetBase(), ThrustRise.GetBase(), ThrustRise.GetBase()},
                {AccelFall.GetBase(), BrakeFall.GetBase(), BrakeFall.GetBase(), ThrustFall.GetBase(), ThrustFall.GetBase()}
            };

            // 가속 0에서 조향만 줘도 기어 판단은 가속 부호로 하고, 엔진 출력은 좌우 중 큰 추력으로 냅니다.
            const auto Pedals{ResolveDriveDirection(Drive, Input)};
            const bool Braking{Pedals.mBrake > 0.0f};
            auto& Dynamics{Drive.mDriveDynData};
            const bool InReverse{Dynamics.getCurrentGear() == physx::PxVehicleGearsData::eREVERSE};

            // 제자리 회전은 1단에 고정해 최대 토크를 냅니다. 주행 중 고단 기어에 머물면 회전력이 부족합니다.
            const bool Pivot{Input.mThrottle == 0.0f && Input.mSteering != 0.0f && !Braking};
            if (Pivot && !InReverse) {
                if (Dynamics.getCurrentGear() != physx::PxVehicleGearsData::eFIRST) {
                    Dynamics.forceGearChange(physx::PxVehicleGearsData::eFIRST);
                }
                Dynamics.setUseAutoGears(false);
            } else if (Input.mThrottle > 0.0f && !InReverse) {
                Dynamics.setUseAutoGears(true);
            }

            // 후진 중에는 자동차처럼 조향을 뒤집어, 오른쪽 조향이면 차체 뒤쪽이 오른쪽으로 갑니다.
            const float Steering{Input.mThrottle < 0.0f ? -Input.mSteering : Input.mSteering};
            const float Left{std::clamp(Input.mThrottle + Steering, -1.0f, 1.0f)};
            const float Right{std::clamp(Input.mThrottle - Steering, -1.0f, 1.0f)};

            // 후진 기어에서는 양의 추력이 바퀴를 뒤로 돌리므로 원하는 회전 방향을 유지하도록 부호를 뒤집습니다.
            const float Direction{InReverse ? -1.0f : 1.0f};

            physx::PxVehicleDriveTankRawInputData Raw{physx::PxVehicleDriveTankControlModel::eSPECIAL};
            Raw.setAnalogAccel(Braking ? 0.0f : std::max(std::abs(Left), std::abs(Right)));
            Raw.setAnalogLeftThrust(Braking ? 0.0f : Left * Direction);
            Raw.setAnalogRightThrust(Braking ? 0.0f : Right * Direction);
            Raw.setAnalogLeftBrake(Pedals.mBrake);
            Raw.setAnalogRightBrake(Pedals.mBrake);
            physx::PxVehicleDriveTankSmoothAnalogRawInputsAndSetAnalogInputs(Smoothing, Raw, StepTime.GetBase(), Drive);
        }
    }

    struct FVehicleManager::FWheelControl {
        Core::FTorque mDriveTorque{};
        Core::FTorque mBrakeTorque{};
        Core::FAngle mSteerAngle{};
    };

    struct FVehicleManager::FVehicle {
        std::unique_ptr<physx::PxVehicleWheels, FFreeVehicle> mDrive{};
        Core::TArray<FWheelControl> mWheelControls{};
        Core::TArray<physx::PxWheelQueryResult> mWheelResults{};
        FDriveInput mDriveInput{};
        bool mBalanced{};
        float mTargetRoll{};
    };

    struct FVehicleManager::FRaycastBatch {
        Core::TArray<physx::PxRaycastQueryResult> mRaycastResults{};
        Core::TArray<physx::PxRaycastHit> mRaycastHits{};
        Core::TArray<physx::PxVehicleWheels*> mStepVehicles{};
        Core::TArray<physx::PxVehicleWheelQueryResult> mStepWheelResults{};
        std::unique_ptr<physx::PxVehicleDrivableSurfaceToTireFrictionPairs, FPhysXRelease> mFriction{};
        std::unique_ptr<physx::PxBatchQuery, FPhysXRelease> mQuery{};
    };

    FVehicleManager::FVehicleManager(physx::PxScene& Scene, FPhysicsRuntime& Runtime)
        : mScene{Scene}, mRuntime{Runtime} {
    }

    FVehicleManager::~FVehicleManager() = default;

    bool FVehicleManager::PrepareVehicle(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels, FVehicle& Vehicle) {
        auto* Actor{Body.GetPxRigidDynamic()};
        const physx::PxU32 WheelCount{Wheels.getNbWheels()};

        if (Actor == nullptr || Actor->getScene() != &mScene || Actor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
            WheelCount == 0 || WheelCount > PX_MAX_NB_WHEELS || !GetWheelQueryResults(Body).empty()) {
            return false;
        }

        const auto ShapeCount{static_cast<physx::PxI32>(Actor->getNbShapes())};
        for (physx::PxU32 Wheel{}; Wheel < WheelCount; ++Wheel) {
            if (Wheels.getWheelShapeMapping(Wheel) >= ShapeCount || Wheels.getTireData(Wheel).mType != StandardTireType) {
                return false;
            }
        }

        Index_t TotalWheels{WheelCount};
        for (const FVehicle& Entry : mVehicles) {
            TotalWheels += Entry.mWheelResults.size();
        }

        if (TotalWheels > std::numeric_limits<Uint32>::max() || !EnsureRaycastCapacity(static_cast<Uint32>(TotalWheels))) {
            return false;
        }

        Vehicle.mWheelResults.resize(WheelCount);
        ResetWheelResults(Vehicle.mWheelResults, Wheels, *Actor);

        return true;
    }

    bool FVehicleManager::RegisterNoDrive(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels) {
        FVehicle Vehicle{};
        if (!PrepareVehicle(Body, Wheels, Vehicle)) {
            return false;
        }

        auto* Actor{Body.GetPxRigidDynamic()};
        Vehicle.mWheelControls.resize(Wheels.getNbWheels());
        Vehicle.mDrive.reset(physx::PxVehicleNoDrive::create(mRuntime.GetPhysics(), Actor, Wheels));
        if (Vehicle.mDrive == nullptr || Vehicle.mDrive->getRigidDynamicActor() != Actor) {
            return false;
        }

        mVehicles.push_back(std::move(Vehicle));

        return true;
    }

    bool FVehicleManager::RegisterDrive4W(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels) {
        using physx::PxVehicleDrive4WWheelOrder;
        constexpr physx::PxU32 WheelCount{4};
        FVehicle Vehicle{};
        if (Wheels.getNbWheels() != WheelCount || !PrepareVehicle(Body, Wheels, Vehicle)) {
            return false;
        }

        const auto& FrontLeft{Vehicle.mWheelResults[PxVehicleDrive4WWheelOrder::eFRONT_LEFT].localPose.p};
        const auto& FrontRight{Vehicle.mWheelResults[PxVehicleDrive4WWheelOrder::eFRONT_RIGHT].localPose.p};
        const auto& RearLeft{Vehicle.mWheelResults[PxVehicleDrive4WWheelOrder::eREAR_LEFT].localPose.p};
        const auto& RearRight{Vehicle.mWheelResults[PxVehicleDrive4WWheelOrder::eREAR_RIGHT].localPose.p};
        if (!(FrontLeft.x > RearLeft.x && FrontRight.x > RearRight.x && FrontRight.y > FrontLeft.y && RearRight.y > RearLeft.y)) {
            return false;
        }

        const auto LeftWheelbase{Core::FLength::FromBase(FrontLeft.x - RearLeft.x)};
        const auto RightWheelbase{Core::FLength::FromBase(FrontRight.x - RearRight.x)};
        const auto Wheelbase{LeftWheelbase * 0.5f + RightWheelbase * 0.5f};
        const auto FrontWidth{Core::FLength::FromBase(FrontRight.y - FrontLeft.y)};
        const auto RearWidth{Core::FLength::FromBase(RearRight.y - RearLeft.y)};
        if (!std::isfinite(Wheelbase.GetBase()) || !std::isfinite(FrontWidth.GetBase()) || !std::isfinite(RearWidth.GetBase())) {
            return false;
        }

        physx::PxVehicleAckermannGeometryData Geometry{};
        Geometry.mAccuracy = 1.0f;
        Geometry.mAxleSeparation = Wheelbase.GetBase();
        Geometry.mFrontWidth = FrontWidth.GetBase();
        Geometry.mRearWidth = RearWidth.GetBase();

        auto FreeWheels{[](physx::PxVehicleWheelsSimData* Data) { Data->free(); }};
        std::unique_ptr<physx::PxVehicleWheelsSimData, decltype(FreeWheels)> DrivenWheels{physx::PxVehicleWheelsSimData::allocate(WheelCount)};
        if (DrivenWheels == nullptr) {
            return false;
        }

        *DrivenWheels = Wheels;
        constexpr auto MaxSteerAngle{32_deg};
        for (const auto Wheel : {PxVehicleDrive4WWheelOrder::eFRONT_LEFT, PxVehicleDrive4WWheelOrder::eFRONT_RIGHT}) {
            auto Data{DrivenWheels->getWheelData(Wheel)};
            Data.mMaxSteer = MaxSteerAngle.GetBase();
            DrivenWheels->setWheelData(Wheel, Data);
        }

        physx::PxVehicleDifferential4WData Differential{};
        Differential.mType = physx::PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;

        physx::PxVehicleDriveSimData4W DriveData{};
        FillDrivetrain(DriveData, 420_Nm);
        DriveData.setDiffData(Differential);
        DriveData.setAckermannGeometryData(Geometry);

        auto* Actor{Body.GetPxRigidDynamic()};
        auto* Drive{physx::PxVehicleDrive4W::create(mRuntime.GetPhysics(), Actor, *DrivenWheels, DriveData, 0)};
        Vehicle.mDrive.reset(Drive);
        if (Drive == nullptr || Drive->getRigidDynamicActor() != Actor) {
            return false;
        }

        StartInFirstGear(*Drive);
        mVehicles.push_back(std::move(Vehicle));

        return true;
    }

    bool FVehicleManager::RegisterDriveTank(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels) {
        const physx::PxU32 WheelCount{Wheels.getNbWheels()};
        FVehicle Vehicle{};
        if (WheelCount < 4 || WheelCount % 2 != 0 || !PrepareVehicle(Body, Wheels, Vehicle)) {
            return false;
        }

        // 쌍마다 좌측이 우측보다 y가 작고, 쌍은 앞에서 뒤로 x가 줄어들어야 합니다.
        for (physx::PxU32 Pair{}; Pair < WheelCount / 2; ++Pair) {
            const auto& Left{Vehicle.mWheelResults[Pair * 2].localPose.p};
            const auto& Right{Vehicle.mWheelResults[Pair * 2 + 1].localPose.p};
            if (!(Right.y > Left.y)) {
                return false;
            }

            if (Pair > 0) {
                const auto& FrontLeft{Vehicle.mWheelResults[Pair * 2 - 2].localPose.p};
                const auto& FrontRight{Vehicle.mWheelResults[Pair * 2 - 1].localPose.p};
                if (!(FrontLeft.x > Left.x && FrontRight.x > Right.x)) {
                    return false;
                }
            }
        }

        auto FreeWheels{[](physx::PxVehicleWheelsSimData* Data) { Data->free(); }};
        std::unique_ptr<physx::PxVehicleWheelsSimData, decltype(FreeWheels)> TrackWheels{physx::PxVehicleWheelsSimData::allocate(WheelCount)};
        if (TrackWheels == nullptr) {
            return false;
        }

        *TrackWheels = Wheels;
        for (physx::PxU32 Wheel{}; Wheel < WheelCount; ++Wheel) {
            auto Data{TrackWheels->getWheelData(Wheel)};
            Data.mMaxSteer = 0.0f;
            TrackWheels->setWheelData(Wheel, Data);

            // 바퀴가 많으면 측면 그립 합이 커져 제자리 회전을 막으므로 횡방향 강성을 낮춥니다.
            constexpr float TrackLateralStiffnessScale{0.4f};
            auto Tire{TrackWheels->getTireData(Wheel)};
            Tire.mLatStiffY *= TrackLateralStiffnessScale;
            TrackWheels->setTireData(Wheel, Tire);
        }

        physx::PxVehicleDriveSimData DriveData{};
        FillDrivetrain(DriveData, 1000_Nm);

        auto* Actor{Body.GetPxRigidDynamic()};
        auto* Drive{physx::PxVehicleDriveTank::create(mRuntime.GetPhysics(), Actor, *TrackWheels, DriveData, WheelCount)};
        Vehicle.mDrive.reset(Drive);
        if (Drive == nullptr || Drive->getRigidDynamicActor() != Actor) {
            return false;
        }

        // 좌우 추력이 반대 방향일 수 있어야 제자리 회전이 가능합니다.
        Drive->setDriveModel(physx::PxVehicleDriveTankControlModel::eSPECIAL);
        StartInFirstGear(*Drive);
        mVehicles.push_back(std::move(Vehicle));

        return true;
    }

    bool FVehicleManager::RegisterDrive2W(const FBodyInstance& Body, const physx::PxVehicleWheelsSimData& Wheels) {
        constexpr physx::PxU32 WheelCount{2};
        constexpr physx::PxU32 FrontWheel{0};
        constexpr physx::PxU32 RearWheel{1};
        FVehicle Vehicle{};
        if (Wheels.getNbWheels() != WheelCount || !PrepareVehicle(Body, Wheels, Vehicle) ||
            !(Vehicle.mWheelResults[FrontWheel].localPose.p.x > Vehicle.mWheelResults[RearWheel].localPose.p.x)) {
            return false;
        }

        auto FreeWheels{[](physx::PxVehicleWheelsSimData* Data) { Data->free(); }};
        std::unique_ptr<physx::PxVehicleWheelsSimData, decltype(FreeWheels)> BikeWheels{physx::PxVehicleWheelsSimData::allocate(WheelCount)};
        if (BikeWheels == nullptr) {
            return false;
        }

        // NW는 조향 입력을 모든 바퀴의 최대 조향각에 곱하므로 뒷바퀴는 0으로 둡니다.
        *BikeWheels = Wheels;
        constexpr auto MaxSteerAngle{30_deg};
        for (const auto [Wheel, MaxSteer] : {std::pair{FrontWheel, MaxSteerAngle.GetBase()}, std::pair{RearWheel, 0.0f}}) {
            auto Data{BikeWheels->getWheelData(Wheel)};
            Data.mMaxSteer = MaxSteer;
            BikeWheels->setWheelData(Wheel, Data);

            // 폭이 좁은 차체는 롤 관성이 작아, 질량 중심 아래에 작용하는 타이어 횡력이 바깥쪽으로 쓰러뜨립니다.
            // 타이어 힘을 질량 중심 높이에 가해 롤·피치 모멘트를 없애고, 기울기는 롤 안정화가 전담하게 합니다.
            auto TireOffset{BikeWheels->getTireForceAppPointOffset(Wheel)};
            TireOffset.z = 0.0f;
            BikeWheels->setTireForceAppPointOffset(Wheel, TireOffset);
        }

        physx::PxVehicleDifferentialNWData Differential{};
        Differential.setDrivenWheel(RearWheel, true);

        physx::PxVehicleDriveSimDataNW DriveData{};
        FillDrivetrain(DriveData, 110_Nm);
        DriveData.setDiffData(Differential);

        auto* Actor{Body.GetPxRigidDynamic()};
        auto* Drive{physx::PxVehicleDriveNW::create(mRuntime.GetPhysics(), Actor, *BikeWheels, DriveData, WheelCount)};
        Vehicle.mDrive.reset(Drive);
        if (Drive == nullptr || Drive->getRigidDynamicActor() != Actor) {
            return false;
        }

        StartInFirstGear(*Drive);
        Vehicle.mBalanced = true;
        mVehicles.push_back(std::move(Vehicle));

        return true;
    }

    bool FVehicleManager::SetDriveInput(const FBodyInstance& Body, float Throttle, float Brake, float Steering) {
        auto* Vehicle{FindVehicle(Body)};
        if (Vehicle == nullptr || AsDrive(*Vehicle->mDrive) == nullptr ||
            !std::isfinite(Throttle) || !std::isfinite(Brake) || !std::isfinite(Steering)) {
            return false;
        }

        Vehicle->mDriveInput = {std::clamp(Throttle, -1.0f, 1.0f), std::clamp(Brake, 0.0f, 1.0f), std::clamp(Steering, -1.0f, 1.0f)};
        return true;
    }

    bool FVehicleManager::ResetDrive(const FBodyInstance& Body) {
        auto* Vehicle{FindVehicle(Body)};
        auto* Drive{Vehicle != nullptr ? AsDrive(*Vehicle->mDrive) : nullptr};
        if (Drive == nullptr) {
            return false;
        }

        SetDriveToRestState(*Drive);
        StartInFirstGear(*Drive);
        Vehicle->mDriveInput = {};
        Vehicle->mTargetRoll = 0.0f;
        ResetWheelResults(Vehicle->mWheelResults, Drive->mWheelsSimData, *Drive->getRigidDynamicActor());
        return true;
    }

    Int32 FVehicleManager::GetDriveGear(const FBodyInstance& Body) const {
        const auto* Vehicle{FindVehicle(Body)};
        const auto* Drive{Vehicle != nullptr ? AsDrive(*Vehicle->mDrive) : nullptr};
        if (Drive == nullptr) {
            return 0;
        }

        return static_cast<Int32>(Drive->mDriveDynData.getCurrentGear()) - physx::PxVehicleGearsData::eNEUTRAL;
    }

    bool FVehicleManager::UnregisterVehicle(const FBodyInstance& Body) {
        const auto* Actor{Body.GetPxRigidDynamic()};

        if (Actor == nullptr) {
            return false;
        }

        return std::erase_if(mVehicles, [Actor](const FVehicle& Vehicle) {
            return Vehicle.mDrive->getRigidDynamicActor() == Actor;
        }) != 0;
    }

    FVehicleManager::FVehicle* FVehicleManager::FindVehicle(const FBodyInstance& Body) {
        return const_cast<FVehicle*>(std::as_const(*this).FindVehicle(Body));
    }

    const FVehicleManager::FVehicle* FVehicleManager::FindVehicle(const FBodyInstance& Body) const {
        const auto* Actor{Body.GetPxRigidDynamic()};
        const auto Vehicle{std::ranges::find_if(mVehicles, [Actor](const FVehicle& Entry) {
            return Entry.mDrive->getRigidDynamicActor() == Actor;
        })};

        return Vehicle != mVehicles.end() ? &*Vehicle : nullptr;
    }

    FVehicleManager::FWheelControl* FVehicleManager::FindWheelControl(const FBodyInstance& Body, Uint32 WheelIndex) {
        auto* Vehicle{FindVehicle(Body)};
        return Vehicle != nullptr && WheelIndex < Vehicle->mWheelControls.size() ? &Vehicle->mWheelControls[WheelIndex] : nullptr;
    }

    bool FVehicleManager::SetDriveTorque(const FBodyInstance& Body, Uint32 WheelIndex, Core::FTorque Torque) {
        auto* Control{FindWheelControl(Body, WheelIndex)};

        if (Control == nullptr || !std::isfinite(Torque.GetBase())) {
            return false;
        }

        Control->mDriveTorque = Torque;

        return true;
    }

    bool FVehicleManager::SetBrakeTorque(const FBodyInstance& Body, Uint32 WheelIndex, Core::FTorque Torque) {
        auto* Control{FindWheelControl(Body, WheelIndex)};

        if (Control == nullptr || !std::isfinite(Torque.GetBase()) || Torque < 0_Nm) {
            return false;
        }

        Control->mBrakeTorque = Torque;

        return true;
    }

    bool FVehicleManager::SetSteerAngle(const FBodyInstance& Body, Uint32 WheelIndex, Core::FAngle Angle) {
        auto* Control{FindWheelControl(Body, WheelIndex)};

        if (Control == nullptr || !std::isfinite(Angle.GetBase())) {
            return false;
        }

        Control->mSteerAngle = Angle;

        return true;
    }

    std::span<const physx::PxWheelQueryResult> FVehicleManager::GetWheelQueryResults(const FBodyInstance& Body) const {
        const auto* Vehicle{FindVehicle(Body)};
        return Vehicle != nullptr ? std::span<const physx::PxWheelQueryResult>{Vehicle->mWheelResults} : std::span<const physx::PxWheelQueryResult>{};
    }

    Index_t FVehicleManager::GetVehicleCount() const {
        return mVehicles.size();
    }

    void FVehicleManager::SetDrivableSurface(physx::PxShape& Shape, bool Drivable) {
        auto Filter{Shape.getQueryFilterData()};
        Filter.word3 = Drivable ? (Filter.word3 | DrivableSurfaceFlag) : (Filter.word3 & ~DrivableSurfaceFlag);
        Shape.setQueryFilterData(Filter);
    }

    bool FVehicleManager::EnsureRaycastCapacity(Uint32 WheelCount) {
        if (mRaycastBatch != nullptr && mRaycastBatch->mRaycastResults.size() >= WheelCount) {
            return true;
        }

        auto Batch{std::make_unique<FRaycastBatch>()};
        Batch->mRaycastResults.resize(WheelCount);
        Batch->mRaycastHits.resize(WheelCount);
        Batch->mFriction.reset(physx::PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1));

        if (Batch->mFriction == nullptr) {
            return false;
        }

        const physx::PxMaterial* Materials[]{mRuntime.GetDefaultMaterial()};
        const physx::PxVehicleDrivableSurfaceType Surfaces[]{{0}};
        Batch->mFriction->setup(1, 1, Materials, Surfaces);

        physx::PxBatchQueryDesc Desc{WheelCount, 0, 0};
        Desc.preFilterShader = FilterSuspensionRay;
        Desc.queryMemory.userRaycastResultBuffer = Batch->mRaycastResults.data();
        Desc.queryMemory.userRaycastTouchBuffer = Batch->mRaycastHits.data();
        Desc.queryMemory.raycastTouchBufferSize = WheelCount;
        Batch->mQuery.reset(mScene.createBatchQuery(Desc));

        if (Batch->mQuery == nullptr) {
            return false;
        }

        mRaycastBatch = std::move(Batch);

        return true;
    }

    void FVehicleManager::Step(Core::FTime StepTime) {
        if (mVehicles.empty() || mScene.getGravity().isZero()) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::VehicleUpdate};
        FRaycastBatch& Batch{*mRaycastBatch};
        for (FVehicle& Vehicle : mVehicles) {
            const physx::PxVehicleWheelQueryResult Results{Vehicle.mWheelResults.data(), static_cast<physx::PxU32>(Vehicle.mWheelResults.size())};
            switch (Vehicle.mDrive->getVehicleType()) {
            case physx::PxVehicleTypes::eDRIVE4W: {
                auto& Drive{static_cast<physx::PxVehicleDrive4W&>(*Vehicle.mDrive)};
                ApplyDrive4WInput(Drive, Vehicle.mDriveInput, StepTime, physx::PxVehicleIsInAir(Results));
                break;
            }
            case physx::PxVehicleTypes::eDRIVETANK:
                ApplyDriveTankInput(static_cast<physx::PxVehicleDriveTank&>(*Vehicle.mDrive), Vehicle.mDriveInput, StepTime);
                break;
            case physx::PxVehicleTypes::eDRIVENW: {
                auto& Drive{static_cast<physx::PxVehicleDriveNW&>(*Vehicle.mDrive)};
                ApplyDriveNWInput(Drive, Vehicle.mDriveInput, StepTime, physx::PxVehicleIsInAir(Results));
                break;
            }
            default: {
                auto& Drive{static_cast<physx::PxVehicleNoDrive&>(*Vehicle.mDrive)};
                for (physx::PxU32 Wheel{}; Wheel < Results.nbWheelQueryResults; ++Wheel) {
                    const auto& Control{Vehicle.mWheelControls[Wheel]};
                    Drive.setDriveTorque(Wheel, Control.mDriveTorque.GetBase());
                    Drive.setBrakeTorque(Wheel, Control.mBrakeTorque.GetBase());
                    Drive.setSteerAngle(Wheel, Control.mSteerAngle.GetBase());
                }
                break;
            }
            }

            if (Vehicle.mBalanced) {
                ApplyRollStabilizer(*Vehicle.mDrive->getRigidDynamicActor(), mScene.getGravity(), StepTime, Vehicle.mTargetRoll);
            }

            Batch.mStepVehicles.push_back(Vehicle.mDrive.get());
            Batch.mStepWheelResults.push_back(Results);
        }

        const auto VehicleCount{static_cast<physx::PxU32>(Batch.mStepVehicles.size())};
        const auto RaycastCount{static_cast<physx::PxU32>(Batch.mRaycastResults.size())};
        physx::PxVehicleSuspensionRaycasts(Batch.mQuery.get(), VehicleCount, Batch.mStepVehicles.data(), RaycastCount, Batch.mRaycastResults.data());
        physx::PxVehicleUpdates(StepTime.GetBase(), mScene.getGravity(), *Batch.mFriction, VehicleCount, Batch.mStepVehicles.data(), Batch.mStepWheelResults.data());

        Batch.mStepVehicles.clear();
        Batch.mStepWheelResults.clear();
    }

}
