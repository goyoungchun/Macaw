/**
 * @file FClothingSystemRuntimeModuleNv.cpp
 * @brief NvCloth 라이브러리 초기화와 CPU 팩토리의 수명을 관리합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FClothingSystemRuntimeModuleNv.h"
#include "PhysicsRuntime/FClothingSimulationNv.h"
#include "Core/Log/Log.h"

#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>
#include <NvClothExt/ClothMeshQuadifier.h>

namespace PhysicsRuntime {

    /**
     * @brief NvCloth 콜백 구현과 로그 범주입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogCloth, Log, All);

        /**
         * @brief NvCloth 오류와 경고를 엔진 로그로 전달합니다.
         */
        class FNvClothErrorCallback final : public physx::PxErrorCallback {
        public:

            /**
             * @brief 오류 코드에 맞는 로그 수준으로 메시지를 기록합니다.
             * @param[in] Code NvCloth가 보고한 오류 종류입니다.
             * @param[in] Message 오류 내용입니다.
             * @param[in] File 오류가 발생한 소스 파일입니다.
             * @param[in] Line 오류가 발생한 줄 번호입니다.
             */
            void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override {
                if (Code == physx::PxErrorCode::eDEBUG_INFO) {
                    MACAW_LOG(LogCloth, Log, "{} ({}:{})", Message, File, Line);
                } else if (Code == physx::PxErrorCode::eDEBUG_WARNING || Code == physx::PxErrorCode::ePERF_WARNING) {
                    MACAW_LOG(LogCloth, Warning, "{} ({}:{})", Message, File, Line);
                } else {
                    MACAW_LOG(LogCloth, Error, "{} ({}:{})", Message, File, Line);
                }
            }
        };

        /**
         * @brief NvCloth 내부 assert 실패를 엔진 로그로 전달합니다.
         */
        class FNvClothAssertHandler final : public nv::cloth::PxAssertHandler {
        public:

            /**
             * @brief 실패한 조건식과 위치를 오류로 기록합니다.
             * @param[in] Expression 실패한 조건식입니다.
             * @param[in] File assert가 있는 소스 파일입니다.
             * @param[in] Line assert가 있는 줄 번호입니다.
             * @param[in,out] Ignore true로 바꾸면 같은 assert를 다시 보고하지 않습니다.
             */
            void operator()(const char* Expression, const char* File, int Line, bool& Ignore) override {
                MACAW_LOG(LogCloth, Error, "NvCloth assert failed: {} ({}:{})", Expression, File, Line);
                Ignore = true;
            }
        };
    }

    /**
     * @brief 초기화하지 않은 천 시스템을 구성합니다.
     */
    FClothingSystemRuntimeModuleNv::FClothingSystemRuntimeModuleNv() = default;

    /**
     * @brief 생성한 팩토리와 콜백을 모두 해제합니다.
     */
    FClothingSystemRuntimeModuleNv::~FClothingSystemRuntimeModuleNv() {
        ShutdownModule();
    }

    /**
     * @brief NvCloth에 콜백을 등록하고 CPU 팩토리를 만듭니다.
     */
    bool FClothingSystemRuntimeModuleNv::StartupModule() {
        if (mClothFactory != nullptr) {
            return true;
        }

        mErrorCallback = std::make_unique<FNvClothErrorCallback>();
        mAssertHandler = std::make_unique<FNvClothAssertHandler>();

        nv::cloth::InitializeNvCloth(&mAllocator, mErrorCallback.get(), mAssertHandler.get(), nullptr);

        mClothFactory = NvClothCreateFactoryCPU();
        mMeshQuadifier = NvClothCreateMeshQuadifier();

        if (mClothFactory == nullptr || mMeshQuadifier == nullptr) {
            MACAW_LOG(LogCloth, Error, "Cannot create the NvCloth CPU factory or mesh quadifier.");
            ShutdownModule();

            return false;
        }

        MACAW_LOG(LogCloth, Log, "NvCloth CPU factory created.");

        return true;
    }

    /**
     * @brief CPU 팩토리를 해제한 뒤 콜백을 해제합니다.
     */
    void FClothingSystemRuntimeModuleNv::ShutdownModule() {
        delete mMeshQuadifier;
        mMeshQuadifier = nullptr;

        if (mClothFactory != nullptr) {
            NvClothDestroyFactory(mClothFactory);
            mClothFactory = nullptr;
        }

        mAssertHandler.reset();
        mErrorCallback.reset();
    }

    /**
     * @brief CPU 팩토리를 반환합니다.
     */
    nv::cloth::Factory* FClothingSystemRuntimeModuleNv::GetSoftwareFactory() const {
        return mClothFactory;
    }

    /**
     * @brief 삼각형 메시를 사각형 메시로 바꾸는 도구를 반환합니다.
     */
    nv::cloth::ClothMeshQuadifier* FClothingSystemRuntimeModuleNv::GetMeshQuadifier() const {
        return mMeshQuadifier;
    }

    /**
     * @brief 이 모듈의 팩토리로 초기화한 천 시뮬레이션을 만듭니다.
     */
    std::unique_ptr<IClothingSimulation> FClothingSystemRuntimeModuleNv::CreateSimulation() const {
        auto Simulation{std::make_unique<FClothingSimulationNv>()};

        if (!Simulation->Initialize(*this)) {
            return nullptr;
        }

        return Simulation;
    }
}
