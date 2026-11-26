/**
 * @file UClothConfigNv.cpp
 * @brief NvCloth 천의 시뮬레이션 설정을 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/UClothConfigNv.h"
#include "Core/Archive.h"

namespace PhysicsRuntime {

    /**
     * @brief 기본 설정을 구성합니다.
     */
    UClothConfigNv::UClothConfigNv() = default;

    /**
     * @brief 설정을 정리합니다.
     */
    UClothConfigNv::~UClothConfigNv() = default;

    /**
     * @brief 천끼리 부딪히는 기능을 쓰는지 검사합니다.
     */
    bool UClothConfigNv::UseSelfCollisions() const {
        return mSelfCollisionRadius > Core::FLength{} && mSelfCollisionStiffness > 0.0f;
    }

    /**
     * @brief 천끼리 부딪힐 입자를 고를 때 쓰는 거리를 구합니다.
     */
    Core::FLength UClothConfigNv::NeedsSelfCollisionIndices() const {
        return UseSelfCollisions() ? mSelfCollisionRadius * mSelfCollisionCullScale : Core::FLength{};
    }

    /**
     * @brief 모든 설정 값을 저장하거나 불러옵니다.
     */
    void UClothConfigNv::SerializeConfig(Core::FArchive& Archive) {
        // 값이 바뀌면 이 번호를 올리고 예전 번호를 읽는 분기를 둡니다. 2부터 기본 바람이 있습니다.
        Uint32 Version{2};

        Archive.Serialize("ClothConfigVersion", Version);

        if (Archive.HasError() || (Version != 1 && Version != 2)) {
            Archive.SetError("Unsupported cloth config version");

            return;
        }

        Uint32 WindMethod{static_cast<Uint32>(mClothingWindMethod)};
        Archive.Serialize("WindMethod", WindMethod);

        if (Archive.IsLoading()) {
            mClothingWindMethod = WindMethod == static_cast<Uint32>(EClothingWindMethodNv::Accurate) ? EClothingWindMethodNv::Accurate : EClothingWindMethodNv::Legacy;
        }

        for (FClothConstraintSetupNv* Setup : {&mVerticalConstraint, &mHorizontalConstraint, &mBendConstraint, &mShearConstraint}) {
            Archive.Serialize("Stiffness", Setup->mStiffness);
            Archive.Serialize("StiffnessMultiplier", Setup->mStiffnessMultiplier);
            Archive.Serialize("StretchLimit", Setup->mStretchLimit);
            Archive.Serialize("CompressionLimit", Setup->mCompressionLimit);
        }

        Archive.Serialize("SelfCollisionRadius", mSelfCollisionRadius);
        Archive.Serialize("SelfCollisionStiffness", mSelfCollisionStiffness);
        Archive.Serialize("SelfCollisionCullScale", mSelfCollisionCullScale);
        Archive.Serialize("Damping", mDamping);
        Archive.Serialize("Friction", mFriction);
        Archive.Serialize("WindDragCoefficient", mWindDragCoefficient);
        Archive.Serialize("WindLiftCoefficient", mWindLiftCoefficient);
        Archive.Serialize("LinearDrag", mLinearDrag);
        Archive.Serialize("AngularDrag", mAngularDrag);
        Archive.Serialize("LinearInertiaScale", mLinearInertiaScale);
        Archive.Serialize("AngularInertiaScale", mAngularInertiaScale);
        Archive.Serialize("CentrifugalInertiaScale", mCentrifugalInertiaScale);
        Archive.Serialize("SolverFrequency", mSolverFrequency);
        Archive.Serialize("StiffnessFrequency", mStiffnessFrequency);
        Archive.Serialize("GravityScale", mGravityScale);
        Archive.Serialize("GravityOverride", mGravityOverride);
        Archive.Serialize("UseGravityOverride", mUseGravityOverride);
        Archive.Serialize("TetherStiffness", mTetherStiffness);
        Archive.Serialize("TetherLimit", mTetherLimit);
        Archive.Serialize("CollisionThickness", mCollisionThickness);
        Archive.Serialize("AnimDriveSpringStiffness", mAnimDriveSpringStiffness);
        Archive.Serialize("AnimDriveDamperStiffness", mAnimDriveDamperStiffness);

        if (Version >= 2) {
            Archive.Serialize("DefaultWindVelocity", mDefaultWindVelocity);
        } else if (Archive.IsLoading()) {
            mDefaultWindVelocity = FVector{};
        }
    }
}
