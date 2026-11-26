/**
 * @file UBodySetup.cpp
 * @brief 하나의 바디를 이루는 충돌 형상을 보관하고 PhysX 형상으로 만듭니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/PhysicsConversion.h"
#include "PhysicsRuntime/UPhysicalMaterial.h"
#include "Core/Log/Log.h"

#include <PxPhysicsAPI.h>
#include <memory>
#include <utility>

namespace PhysicsRuntime {

    /**
     * @brief 충돌 형상을 PhysX 형상으로 바꿀 때 쓰는 보조 함수와 로그 범주입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogPhysics, Log, All);

        /**
         * @brief 검증한 지오메트리와 로컬 자세로 강체 전용 형상을 만들어 붙입니다.
         * @param[in,out] Actor 형상을 붙일 PhysX 강체입니다.
         * @param[in] Geometry 스케일을 반영한 지오메트리입니다.
         * @param[in] LocalPose 바디 로컬 공간에서의 형상 자세입니다.
         * @param[in] Material 형상에 지정할 재질입니다.
         * @return 형상을 붙였으면 true, 지오메트리·자세가 유효하지 않거나 생성에 실패하면 false입니다.
         */
        bool AttachShape(physx::PxRigidActor& Actor, const physx::PxGeometry& Geometry, const physx::PxTransform& LocalPose, const physx::PxMaterial& Material) {
            if (!LocalPose.isValid()) {
                return false;
            }

            physx::PxShape* Shape{physx::PxRigidActorExt::createExclusiveShape(Actor, Geometry, Material)};

            if (Shape == nullptr) {
                return false;
            }

            Shape->setLocalPose(LocalPose);

            return true;
        }
    }

    /**
     * @brief 형상이 없는 바디 셋업을 구성합니다.
     */
    PhysicsRuntime::UBodySetup::UBodySetup() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    PhysicsRuntime::UBodySetup::~UBodySetup() = default;

    /**
     * @brief 바디를 이루는 충돌 형상 묶음을 반환합니다.
     */
    PhysicsRuntime::FKAggregateGeom& PhysicsRuntime::UBodySetup::GetAggGeom() {
        return mAggGeom;
    }

    /**
     * @brief 바디를 이루는 충돌 형상 묶음을 반환합니다.
     */
    const PhysicsRuntime::FKAggregateGeom& PhysicsRuntime::UBodySetup::GetAggGeom() const {
        return mAggGeom;
    }

    /**
     * @brief 바디에 지정한 물리 재질을 반환합니다.
     */
    const PhysicsRuntime::UPhysicalMaterial* PhysicsRuntime::UBodySetup::GetPhysicalMaterial() const {
        return mPhysicalMaterial.get();
    }

    /**
     * @brief 바디를 생성할 때 적용할 물리 재질의 소유권을 받습니다.
     */
    bool PhysicsRuntime::UBodySetup::SetPhysicalMaterial(std::unique_ptr<PhysicsRuntime::UPhysicalMaterial> Material) {
        if (Material != nullptr && !Material->SetOuter(this)) {
            return false;
        }

        mPhysicalMaterial = std::move(Material);

        return true;
    }

    /**
     * @brief 충돌 형상마다 PhysX 형상을 만들어 강체에 붙입니다.
     * @details 형상 크기와 중심은 각 형상의 GetScaled 규칙으로 스케일을 반영합니다.
     */
    Index_t PhysicsRuntime::UBodySetup::AddShapesToRigidActor(physx::PxRigidActor& Actor, const FVector& Scale, const PhysicsRuntime::FPhysicsRuntime& Runtime, const physx::PxMaterial* MaterialOverride) const {
        std::unique_ptr<physx::PxMaterial, PhysicsRuntime::FPhysXRelease> CustomMaterial{};
        const physx::PxMaterial* Material{Runtime.GetDefaultMaterial()};

        if (MaterialOverride != nullptr) {
            // 컴포넌트가 지정한 재질이 바디 셋업의 재질보다 우선한다.
            Material = MaterialOverride;
        } else if (mPhysicalMaterial != nullptr) {
            CustomMaterial.reset(Runtime.CreateMaterial(*mPhysicalMaterial));
            Material = CustomMaterial.get();
        }

        if (Material == nullptr) {
            MACAW_LOG(LogPhysics, Warning, "Cannot resolve a PhysX material for body setup '{}'.", GetName());

            return 0;
        }

        Index_t Attached{};

        for (Index_t Index{}; Index < mAggGeom.mSphereElems.size(); ++Index) {
            const PhysicsRuntime::FKSphereElem Element{mAggGeom.mSphereElems[Index].GetScaled(Scale)};
            const physx::PxSphereGeometry Geometry{Element.mRadius.GetBase()};
            const physx::PxTransform LocalPose{PhysicsRuntime::ToPxVec3(Element.mCenter)};

            if (Geometry.isValid() && AttachShape(Actor, Geometry, LocalPose, *Material)) {
                ++Attached;
            } else {
                MACAW_LOG(LogPhysics, Warning, "Skipped invalid sphere element {} of body setup '{}'.", Index, GetName());
            }
        }

        for (Index_t Index{}; Index < mAggGeom.mBoxElems.size(); ++Index) {
            const PhysicsRuntime::FKBoxElem Element{mAggGeom.mBoxElems[Index].GetScaled(Scale)};
            const physx::PxBoxGeometry Geometry{PhysicsRuntime::ToPxVec3(Element.mHalfExtent)};
            const physx::PxTransform LocalPose{PhysicsRuntime::ToPxVec3(Element.mCenter), PhysicsRuntime::ToPxQuat(Element.mRotation.Quaternion())};

            if (Geometry.isValid() && AttachShape(Actor, Geometry, LocalPose, *Material)) {
                ++Attached;
            } else {
                MACAW_LOG(LogPhysics, Warning, "Skipped invalid box element {} of body setup '{}'.", Index, GetName());
            }
        }

        for (Index_t Index{}; Index < mAggGeom.mSphylElems.size(); ++Index) {
            const PhysicsRuntime::FKSphylElem Element{mAggGeom.mSphylElems[Index].GetScaled(Scale)};
            const physx::PxCapsuleGeometry Geometry{Element.mRadius.GetBase(), Element.mLength.GetBase() * 0.5f};
            const physx::PxTransform LocalPose{PhysicsRuntime::ToPxVec3(Element.mCenter), PhysicsRuntime::ToPxQuat(Element.mRotation.Quaternion()) * PhysicsRuntime::GetSphylBasis()};

            if (Geometry.isValid() && AttachShape(Actor, Geometry, LocalPose, *Material)) {
                ++Attached;
            } else {
                MACAW_LOG(LogPhysics, Warning, "Skipped invalid capsule element {} of body setup '{}'.", Index, GetName());
            }
        }

        for (Index_t Index{}; Index < mAggGeom.mConvexElems.size(); ++Index) {
            // 형상이 메시 참조를 하나 더 가지므로 쿠킹한 참조는 붙인 뒤 놓습니다. 바디가 해제되면 메시도 함께 해제됩니다.
            const std::unique_ptr<physx::PxConvexMesh, PhysicsRuntime::FPhysXRelease> Mesh{Runtime.CreateConvexMesh(mAggGeom.mConvexElems[Index])};
            const physx::PxConvexMeshGeometry Geometry{Mesh.get(), physx::PxMeshScale{PhysicsRuntime::ToPxVec3(Scale)}};

            if (Mesh != nullptr && Geometry.isValid() && AttachShape(Actor, Geometry, physx::PxTransform{physx::PxIdentity}, *Material)) {
                ++Attached;
            } else {
                MACAW_LOG(LogPhysics, Warning, "Skipped invalid convex element {} of body setup '{}'.", Index, GetName());
            }
        }

        return Attached;
    }

}
