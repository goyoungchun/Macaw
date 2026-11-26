/**
 * @file FPhysicsRuntime.cpp
 * @brief 프로세스 전체에서 공유하는 PhysX SDK 객체의 수명을 관리합니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/FKConvexElem.h"
#include "PhysicsRuntime/PhysicsConversion.h"
#include "PhysicsRuntime/UPhysicalMaterial.h"
#include "Core/Literals.h"
#include "Core/Log/Log.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <vehicle/PxVehicleSDK.h>

using namespace Core::Literals;

namespace PhysicsRuntime {

    constinit const Core::FSpeed PhysicsRuntime::FPhysicsSettings::mDefaultTypicalSpeed{9.81_mps};
    constinit const Core::FDensity PhysicsRuntime::FPhysicsSettings::mDefaultMassDensity{1000_kg / (1_m * 1_m * 1_m)};
    constinit const float PhysicsRuntime::FPhysicsSettings::mDefaultGravityZ{-(9.81_mps2).GetBase()};
    constinit const Core::FTime PhysicsRuntime::FPhysicsSettings::mDefaultFixedStep{1.0f / 60_Hz};

    /**
     * @brief PhysX 런타임의 단위 규약, 볼록 메시 쿠킹 보조 함수와 로그 범주입니다.
     */
    namespace {
        /**
         * @brief 엔진 1유닛에 대응하는 실제 길이입니다.
         */
        constexpr Core::FLength UnitLength{1_m};

        /**
         * @brief PhysX 볼록 메시가 가질 수 있는 최대 꼭짓점 수입니다.
         */
        constexpr physx::PxU16 MaxConvexVertices{255};

        DEFINE_LOG_CATEGORY_STATIC(LogPhysics, Log, All);

        /**
         * @brief 점 집합의 볼록 외곽을 계산해 PhysX 볼록 메시로 쿠킹합니다.
         * @param[in] Cooking 쿠킹에 사용할 객체입니다.
         * @param[in] Physics 쿠킹한 메시를 등록할 SDK 객체입니다.
         * @param[in] Points 외곽을 계산할 점들입니다.
         * @return 새 볼록 메시의 소유 주소이며, 외곽을 만들 수 없으면 nullptr입니다.
         */
        physx::PxConvexMesh* CookConvexMesh(physx::PxCooking& Cooking, physx::PxPhysics& Physics, const Core::TArray<physx::PxVec3>& Points) {
            physx::PxConvexMeshDesc Desc{};
            Desc.points.count = static_cast<physx::PxU32>(Points.size());
            Desc.points.stride = sizeof(physx::PxVec3);
            Desc.points.data = Points.data();
            Desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;
            Desc.vertexLimit = MaxConvexVertices;

            if (!Desc.isValid()) {
                return nullptr;
            }

            return Cooking.createConvexMesh(Desc, Physics.getPhysicsInsertionCallback());
        }
    }

    /**
     * @brief 초기화하지 않은 런타임을 구성합니다.
     */
    PhysicsRuntime::FPhysicsRuntime::FPhysicsRuntime() = default;

    /**
     * @brief 생성한 PhysX 객체를 모두 해제합니다.
     */
    PhysicsRuntime::FPhysicsRuntime::~FPhysicsRuntime() {
        Shutdown();
    }

    /**
     * @brief PhysX SDK 객체를 생성하고 PVD 연결을 시도합니다.
     */
    bool PhysicsRuntime::FPhysicsRuntime::Initialize() {
        if (IsInitialized()) {
            return true;
        }

        mTolerancesScale.length = UnitLength / 1_m;
        mTolerancesScale.speed = mSettings.mTypicalSpeed.GetBase();

        mFoundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION, mAllocator, mErrorCallback));

        if (mFoundation == nullptr) {
            MACAW_LOG(LogPhysics, Error, "Cannot create the PhysX foundation.");

            return false;
        }

        if (mSettings.mEnablePvd) {
            ConnectPvd();
        }

        mPhysics.reset(PxCreatePhysics(PX_PHYSICS_VERSION, *mFoundation, mTolerancesScale, mPvd != nullptr, mPvd.get()));

        if (mPhysics == nullptr) {
            MACAW_LOG(LogPhysics, Error, "Cannot create the PhysX SDK instance.");
            Shutdown();

            return false;
        }

        mExtensionsInitialized = PxInitExtensions(*mPhysics, mPvd.get());

        if (!mExtensionsInitialized) {
            MACAW_LOG(LogPhysics, Error, "Cannot initialize the PhysX extensions.");
            Shutdown();

            return false;
        }

        // 차량 SDK는 기존 PxPhysics의 1유닛 = 1m 허용 오차 기준을 사용합니다.
        mVehicleSdkInitialized = physx::PxInitVehicleSDK(*mPhysics);

        if (!mVehicleSdkInitialized) {
            MACAW_LOG(LogPhysics, Error, "Cannot initialize the PhysX Vehicle SDK.");
            Shutdown();

            return false;
        }

        physx::PxVehicleSetBasisVectors(physx::PxVec3{0.0f, 0.0f, 1.0f}, physx::PxVec3{1.0f, 0.0f, 0.0f});

        mCooking.reset(PxCreateCooking(PX_PHYSICS_VERSION, *mFoundation, physx::PxCookingParams{mTolerancesScale}));

        if (mCooking == nullptr) {
            MACAW_LOG(LogPhysics, Error, "Cannot create the PhysX cooking interface.");
            Shutdown();

            return false;
        }

        mDefaultMaterial.reset(CreateMaterial(*CoreUObject::NewObject<PhysicsRuntime::UPhysicalMaterial>()));

        if (mDefaultMaterial == nullptr) {
            MACAW_LOG(LogPhysics, Error, "Cannot create the default PhysX material.");
            Shutdown();

            return false;
        }

        mDispatcher.reset(physx::PxDefaultCpuDispatcherCreate(mSettings.mWorkerThreadCount));

        if (mDispatcher == nullptr) {
            MACAW_LOG(LogPhysics, Error, "Cannot create the PhysX CPU dispatcher.");
            Shutdown();

            return false;
        }

        MACAW_LOG(LogPhysics, Log, "PhysX initialized (length scale {}, speed scale {}, {} worker threads, PVD connected {}).", mTolerancesScale.length, mTolerancesScale.speed, mSettings.mWorkerThreadCount, IsPvdConnected());

        return true;
    }

    /**
     * @brief 생성한 PhysX 객체를 의존 관계의 역순으로 해제합니다.
     */
    void PhysicsRuntime::FPhysicsRuntime::Shutdown() {
        mDispatcher.reset();
        mDefaultMaterial.reset();
        mCooking.reset();

        if (mVehicleSdkInitialized) {
            physx::PxCloseVehicleSDK();
            mVehicleSdkInitialized = false;
        }

        if (mExtensionsInitialized) {
            PxCloseExtensions();
            mExtensionsInitialized = false;
        }

        mPhysics.reset();
        mPvd.reset();
        mPvdTransport.reset();
        mFoundation.reset();
    }

    /**
     * @brief 필수 PhysX 객체가 모두 준비되었는지 반환합니다.
     */
    bool PhysicsRuntime::FPhysicsRuntime::IsInitialized() const {
        return mDispatcher != nullptr;
    }

    /**
     * @brief PVD에 연결되었는지 반환합니다.
     */
    bool PhysicsRuntime::FPhysicsRuntime::IsPvdConnected() const {
        return mPvd != nullptr && mPvd->isConnected();
    }

    /**
     * @brief SDK 객체와 쿠킹에 사용한 허용 오차 기준을 반환합니다.
     */
    const physx::PxTolerancesScale& PhysicsRuntime::FPhysicsRuntime::GetTolerancesScale() const {
        return mTolerancesScale;
    }

    /**
     * @brief 런타임과 물리 장면이 따르는 정책을 반환합니다.
     */
    const PhysicsRuntime::FPhysicsSettings& PhysicsRuntime::FPhysicsRuntime::GetSettings() const {
        return mSettings;
    }

    /**
     * @brief PhysX SDK 객체의 비소유 주소를 반환합니다.
     */
    physx::PxPhysics* PhysicsRuntime::FPhysicsRuntime::GetPhysics() const {
        return mPhysics.get();
    }

    /**
     * @brief 충돌 메시 쿠킹 객체의 비소유 주소를 반환합니다.
     */
    physx::PxCooking* PhysicsRuntime::FPhysicsRuntime::GetCooking() const {
        return mCooking.get();
    }

    /**
     * @brief 별도 재질이 없는 형상에 사용할 기본 재질의 비소유 주소를 반환합니다.
     */
    physx::PxMaterial* PhysicsRuntime::FPhysicsRuntime::GetDefaultMaterial() const {
        return mDefaultMaterial.get();
    }

    /**
     * @brief 물리 재질의 계수로 PhysX 재질을 생성합니다.
     */
    physx::PxMaterial* PhysicsRuntime::FPhysicsRuntime::CreateMaterial(const PhysicsRuntime::UPhysicalMaterial& Material) const {
        if (mPhysics == nullptr) {
            return nullptr;
        }

        return mPhysics->createMaterial(Material.GetStaticFriction(), Material.GetDynamicFriction(), Material.GetRestitution());
    }

    /**
     * @brief 물리 장면이 공유하는 CPU 디스패처의 비소유 주소를 반환합니다.
     */
    physx::PxDefaultCpuDispatcher* PhysicsRuntime::FPhysicsRuntime::GetDispatcher() const {
        return mDispatcher.get();
    }

    /**
     * @brief PVD 객체의 비소유 주소를 반환합니다.
     */
    physx::PxPvd* PhysicsRuntime::FPhysicsRuntime::GetPvd() const {
        return mPvd.get();
    }

    /**
     * @brief 점 집합을 감싸는 볼록 외곽을 계산해 볼록 충돌 형상으로 만듭니다.
     */
    bool PhysicsRuntime::FPhysicsRuntime::ComputeConvexHull(std::span<const FVector> Points, PhysicsRuntime::FKConvexElem& Element) const {
        if (!IsInitialized() || Points.size() < 4) {
            return false;
        }

        Core::TArray<physx::PxVec3> Input{};
        Input.reserve(Points.size());

        for (const FVector& Point : Points) {
            Input.push_back(PhysicsRuntime::ToPxVec3(Point));
        }

        // 외곽 꼭짓점과 면만 읽고 메시는 바로 해제합니다. 바디를 만들 때 꼭짓점으로 다시 쿠킹합니다.
        const std::unique_ptr<physx::PxConvexMesh, PhysicsRuntime::FPhysXRelease> Mesh{CookConvexMesh(*mCooking, *mPhysics, Input)};

        if (Mesh == nullptr) {
            return false;
        }

        PhysicsRuntime::FKConvexElem Hull{};
        const physx::PxVec3* Vertices{Mesh->getVertices()};
        const physx::PxU8* IndexBuffer{Mesh->getIndexBuffer()};

        for (physx::PxU32 Index{}; Index < Mesh->getNbVertices(); ++Index) {
            Hull.mVertices.push_back(PhysicsRuntime::ToFVector(Vertices[Index]));
        }

        // 외곽 면은 볼록 다각형이므로 첫 꼭짓점을 기준으로 부채꼴 삼각형으로 나눕니다.
        for (physx::PxU32 Polygon{}; Polygon < Mesh->getNbPolygons(); ++Polygon) {
            physx::PxHullPolygon Data{};

            if (!Mesh->getPolygonData(Polygon, Data)) {
                continue;
            }

            for (physx::PxU16 Corner{1}; Corner + 1 < Data.mNbVerts; ++Corner) {
                Hull.mIndices.push_back(IndexBuffer[Data.mIndexBase]);
                Hull.mIndices.push_back(IndexBuffer[Data.mIndexBase + Corner]);
                Hull.mIndices.push_back(IndexBuffer[Data.mIndexBase + Corner + 1]);
            }
        }

        if (!Hull.IsValid()) {
            return false;
        }

        Element = std::move(Hull);

        return true;
    }

    /**
     * @brief 볼록 충돌 형상의 꼭짓점으로 PhysX 볼록 메시를 쿠킹합니다.
     */
    physx::PxConvexMesh* PhysicsRuntime::FPhysicsRuntime::CreateConvexMesh(const PhysicsRuntime::FKConvexElem& Element) const {
        if (!IsInitialized() || !Element.IsValid()) {
            return nullptr;
        }

        Core::TArray<physx::PxVec3> Points{};
        Points.reserve(Element.mVertices.size());

        for (const FVector& Vertex : Element.mVertices) {
            Points.push_back(PhysicsRuntime::ToPxVec3(Vertex));
        }

        physx::PxConvexMesh* Mesh{CookConvexMesh(*mCooking, *mPhysics, Points)};

        if (Mesh == nullptr) {
            MACAW_LOG(LogPhysics, Warning, "Cannot cook a convex mesh from {} hull vertices.", Points.size());
        }

        return Mesh;
    }

    /**
     * @brief PVD 객체를 만들고 로컬 PVD 프로그램에 연결을 시도합니다.
     */
    void PhysicsRuntime::FPhysicsRuntime::ConnectPvd() {
        mPvd.reset(physx::PxCreatePvd(*mFoundation));

        if (mPvd == nullptr) {
            MACAW_LOG(LogPhysics, Warning, "Cannot create the PhysX Visual Debugger instance.");

            return;
        }

        mPvdTransport.reset(physx::PxDefaultPvdSocketTransportCreate(mSettings.mPvdHost.c_str(), mSettings.mPvdPort, mSettings.mPvdConnectTimeoutMilliseconds));

        if (mPvdTransport == nullptr) {
            MACAW_LOG(LogPhysics, Warning, "Cannot create the PhysX Visual Debugger transport.");

            return;
        }

        if (mPvd->connect(*mPvdTransport, physx::PxPvdInstrumentationFlag::eALL)) {
            MACAW_LOG(LogPhysics, Log, "Connected to the PhysX Visual Debugger at {}:{}.", mSettings.mPvdHost, mSettings.mPvdPort);
        } else {
            MACAW_LOG(LogPhysics, Log, "PhysX Visual Debugger is not running at {}:{}; continuing without it.", mSettings.mPvdHost, mSettings.mPvdPort);
        }
    }

}
