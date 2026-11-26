/**
 * @file FPhysicsRuntime.h
 * @brief 프로세스 전체에서 공유하는 PhysX SDK 객체의 수명을 관리합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "PhysicsRuntime/FPhysXRelease.h"
#include "PhysicsRuntime/FPhysicsSettings.h"

#include <PxPhysicsAPI.h>
#include <memory>
#include <span>

namespace PhysicsRuntime {

    struct FKConvexElem;
    class UPhysicalMaterial;

    /**
     * @brief 프로세스 전체에서 공유하는 PhysX SDK 객체의 수명을 관리합니다.
     * @details Foundation, PVD 연결, Physics, Extensions, Vehicle SDK, Cooking, 기본 재질과 CPU 디스패처를 만들고 역순으로 해제합니다.
     *          엔진 1유닛을 1m로 보고 허용 오차 기준을 정하며, 월드별 물리 장면은 이 객체가 만든 SDK 객체를 빌려 사용합니다.
     * @ingroup PhysicsRuntime
     */
    class FPhysicsRuntime {
    public:

        /**
         * @brief 초기화하지 않은 런타임을 구성합니다.
         */
        FPhysicsRuntime();

        /**
         * @brief 생성한 PhysX 객체를 모두 해제합니다.
         */
        ~FPhysicsRuntime();

    public:

        /**
         * @brief PhysX SDK 객체를 생성하고 PVD 연결을 시도합니다.
         * @details 기본 재질은 UPhysicalMaterial의 기본값을 사용합니다. PVD 연결 실패는 디버깅 기능만 비활성화하므로 초기화를 중단하지 않습니다.
         * @return 이미 초기화되었거나 필수 객체 생성에 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool Initialize();

        /**
         * @brief 생성한 PhysX 객체를 의존 관계의 역순으로 해제합니다.
         * @note 여러 번 호출해도 안전하며, 이 객체에서 빌린 장면과 액터는 먼저 해제되어야 합니다.
         */
        void Shutdown();

        /**
         * @brief 필수 PhysX 객체가 모두 준비되었는지 반환합니다.
         * @return 초기화가 완료되었으면 true입니다.
         */
        bool IsInitialized() const;

        /**
         * @brief PVD에 연결되었는지 반환합니다.
         * @return PVD 소켓 연결이 유지되고 있으면 true입니다.
         */
        bool IsPvdConnected() const;

        /**
         * @brief SDK 객체와 쿠킹에 사용한 허용 오차 기준을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const physx::PxTolerancesScale& GetTolerancesScale() const;

        /**
         * @brief 런타임과 물리 장면이 따르는 정책을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const PhysicsRuntime::FPhysicsSettings& GetSettings() const;

        /**
         * @brief PhysX SDK 객체의 비소유 주소를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 초기화 전이면 nullptr입니다.
         */
        physx::PxPhysics* GetPhysics() const;

        /**
         * @brief 충돌 메시 쿠킹 객체의 비소유 주소를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 초기화 전이면 nullptr입니다.
         */
        physx::PxCooking* GetCooking() const;

        /**
         * @brief 별도 재질이 없는 형상에 사용할 기본 재질의 비소유 주소를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 초기화 전이면 nullptr입니다.
         */
        physx::PxMaterial* GetDefaultMaterial() const;

        /**
         * @brief 물리 재질의 계수로 PhysX 재질을 생성합니다.
         * @param[in] Material 마찰과 반발 계수를 제공하는 물리 재질입니다.
         * @return 새 PhysX 재질의 소유 주소이며, SDK가 준비되지 않았거나 생성에 실패하면 nullptr입니다.
         * @note 호출자가 release()로 자기 참조를 놓아야 합니다. 생성된 형상은 재질에 대한 자체 참조를 유지합니다.
         */
        physx::PxMaterial* CreateMaterial(const PhysicsRuntime::UPhysicalMaterial& Material) const;

        /**
         * @brief 물리 장면이 공유하는 CPU 디스패처의 비소유 주소를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 초기화 전이면 nullptr입니다.
         */
        physx::PxDefaultCpuDispatcher* GetDispatcher() const;

        /**
         * @brief PVD 객체의 비소유 주소를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, PVD를 만들지 못했으면 nullptr입니다.
         */
        physx::PxPvd* GetPvd() const;

        /**
         * @brief 점 집합을 감싸는 볼록 외곽을 계산해 볼록 충돌 형상으로 만듭니다.
         * @param[in] Points 외곽을 계산할 바디 로컬 공간의 점들입니다.
         * @param[out] Element 계산한 외곽 꼭짓점과 면을 나눈 삼각형 인덱스를 받을 형상입니다.
         * @return 외곽을 계산했으면 true이며, 초기화 전이거나 점이 4개 미만이거나 쿠킹에 실패하면 false입니다.
         * @note 외곽 꼭짓점은 PhysX 제한에 따라 최대 255개입니다. 점이 한 평면에 놓이면 PhysX가 법선 방향으로 얇게(두께 약 0.1) 늘린 외곽을 만듭니다.
         */
        bool ComputeConvexHull(std::span<const FVector> Points, PhysicsRuntime::FKConvexElem& Element) const;

        /**
         * @brief 볼록 충돌 형상의 꼭짓점으로 PhysX 볼록 메시를 쿠킹합니다.
         * @param[in] Element 쿠킹할 볼록 충돌 형상입니다.
         * @return 새 볼록 메시의 소유 주소이며, 쿠킹에 실패하면 nullptr입니다.
         * @note 형상에 메시를 붙인 뒤 호출자가 release()로 자기 참조를 놓아야 합니다. 형상이 남은 참조를 유지합니다.
         */
        physx::PxConvexMesh* CreateConvexMesh(const PhysicsRuntime::FKConvexElem& Element) const;

    private:

        /**
         * @brief PVD 객체를 만들고 로컬 PVD 프로그램에 연결을 시도합니다.
         * @note PVD를 사용할 수 없어도 Physics 생성에 필요한 상태는 유지합니다.
         */
        void ConnectPvd();

    private:

        /**
         * @brief PhysX가 사용하는 기본 메모리 할당자입니다.
         */
        physx::PxDefaultAllocator mAllocator{};

        /**
         * @brief PhysX 오류를 표준 출력으로 전달하는 기본 오류 콜백입니다.
         */
        physx::PxDefaultErrorCallback mErrorCallback{};

        /**
         * @brief 엔진 1유닛을 1m로 보는 허용 오차 기준입니다.
         */
        physx::PxTolerancesScale mTolerancesScale{};

        /**
         * @brief 런타임과 모든 물리 장면이 공유하는 정책입니다.
         */
        PhysicsRuntime::FPhysicsSettings mSettings{};

        /**
         * @brief 다른 모든 PhysX 객체가 의존하는 Foundation입니다.
         */
        std::unique_ptr<physx::PxFoundation, PhysicsRuntime::FPhysXRelease> mFoundation{};

        /**
         * @brief PVD가 사용하는 소켓 전송 객체입니다.
         * @note PVD 객체보다 나중에 해제되도록 먼저 선언합니다.
         */
        std::unique_ptr<physx::PxPvdTransport, PhysicsRuntime::FPhysXRelease> mPvdTransport{};

        /**
         * @brief PhysX Visual Debugger 연결 객체입니다.
         */
        std::unique_ptr<physx::PxPvd, PhysicsRuntime::FPhysXRelease> mPvd{};

        /**
         * @brief 강체·형상·재질을 생성하는 PhysX SDK 객체입니다.
         */
        std::unique_ptr<physx::PxPhysics, PhysicsRuntime::FPhysXRelease> mPhysics{};

        /**
         * @brief Convex·Triangle 메시를 쿠킹하는 객체입니다.
         */
        std::unique_ptr<physx::PxCooking, PhysicsRuntime::FPhysXRelease> mCooking{};

        /**
         * @brief 별도 재질이 없는 형상에 사용할 기본 재질입니다.
         */
        std::unique_ptr<physx::PxMaterial, PhysicsRuntime::FPhysXRelease> mDefaultMaterial{};

        /**
         * @brief 물리 장면의 시뮬레이션 작업을 실행하는 CPU 디스패처입니다.
         */
        std::unique_ptr<physx::PxDefaultCpuDispatcher, PhysicsRuntime::FPhysXRelease> mDispatcher{};

        /**
         * @brief PxInitExtensions 호출에 성공했는지 여부입니다.
         */
        bool mExtensionsInitialized{};

        /** @brief PxInitVehicleSDK 호출에 성공했는지 여부입니다. */
        bool mVehicleSdkInitialized{};
    };

}
