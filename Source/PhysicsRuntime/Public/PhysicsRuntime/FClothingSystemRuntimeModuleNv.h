/**
 * @file FClothingSystemRuntimeModuleNv.h
 * @brief NvCloth 라이브러리 초기화와 CPU 팩토리의 수명을 관리합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "PhysicsRuntime/IClothingSimulation.h"

#include <PxPhysicsAPI.h>
#include <memory>

namespace nv::cloth {
    class ClothMeshQuadifier;
    class Factory;
    class PxAssertHandler;
}

namespace PhysicsRuntime {

    /**
     * @brief 프로세스 전체에서 공유하는 NvCloth 콜백과 CPU 팩토리를 소유합니다.
     * @details 초기화할 때 할당자, 오류 콜백, assert 처리기를 NvCloth에 등록하고 CPU 팩토리를 만듭니다.
     *          천 시뮬레이션은 이 팩토리로 패브릭, 천, 솔버를 만듭니다.
     * @ingroup PhysicsRuntime
     */
    class FClothingSystemRuntimeModuleNv {
    public:

        /**
         * @brief 초기화하지 않은 천 시스템을 구성합니다.
         */
        FClothingSystemRuntimeModuleNv();

        /**
         * @brief 생성한 팩토리와 콜백을 모두 해제합니다.
         */
        ~FClothingSystemRuntimeModuleNv();

        /**
         * @brief 팩토리를 두 객체가 해제하지 않도록 복사를 금지합니다.
         */
        FClothingSystemRuntimeModuleNv(const FClothingSystemRuntimeModuleNv&) = delete;

        /**
         * @brief 팩토리를 두 객체가 해제하지 않도록 대입을 금지합니다.
         */
        FClothingSystemRuntimeModuleNv& operator=(const FClothingSystemRuntimeModuleNv&) = delete;

    public:

        /**
         * @brief NvCloth에 콜백을 등록하고 CPU 팩토리를 만듭니다.
         * @return 이미 초기화되었거나 팩토리 생성에 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool StartupModule();

        /**
         * @brief CPU 팩토리를 해제한 뒤 콜백을 해제합니다.
         * @note 여러 번 호출해도 안전하며, 이 팩토리로 만든 천과 솔버는 먼저 해제되어야 합니다.
         */
        void ShutdownModule();

        /**
         * @brief CPU 팩토리를 반환합니다.
         * @return 초기화되지 않았으면 nullptr입니다.
         */
        nv::cloth::Factory* GetSoftwareFactory() const;

        /**
         * @brief 삼각형 메시를 사각형 메시로 바꾸는 도구를 반환합니다.
         * @return 초기화되지 않았으면 nullptr입니다.
         */
        nv::cloth::ClothMeshQuadifier* GetMeshQuadifier() const;

        /**
         * @brief 이 모듈의 팩토리로 초기화한 천 시뮬레이션을 만듭니다.
         * @return 새 시뮬레이션이며 모듈이 초기화되지 않았거나 솔버를 만들 수 없으면 nullptr입니다.
         * @note 만든 시뮬레이션은 이 모듈을 종료하기 전에 해제해야 합니다.
         */
        std::unique_ptr<IClothingSimulation> CreateSimulation() const;

    private:

        /**
         * @brief NvCloth가 사용하는 16바이트 정렬 메모리 할당자입니다.
         */
        physx::PxDefaultAllocator mAllocator{};

        /**
         * @brief NvCloth 오류와 경고를 엔진 로그로 전달하는 콜백입니다.
         */
        std::unique_ptr<physx::PxErrorCallback> mErrorCallback{};

        /**
         * @brief NvCloth 내부 assert 실패를 엔진 로그로 전달하는 처리기입니다.
         */
        std::unique_ptr<nv::cloth::PxAssertHandler> mAssertHandler{};

        /**
         * @brief 천, 패브릭, 솔버를 만드는 CPU 팩토리입니다.
         */
        nv::cloth::Factory* mClothFactory{};

        /**
         * @brief 패브릭을 굽기 전에 삼각형 메시를 사각형 메시로 바꾸는 도구입니다.
         */
        nv::cloth::ClothMeshQuadifier* mMeshQuadifier{};
    };
}
