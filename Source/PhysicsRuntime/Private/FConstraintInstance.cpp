/**
 * @file FConstraintInstance.cpp
 * @brief 두 바디를 PhysX 관절로 잇고 움직임 한계를 적용합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FConstraintInstance.h"
#include "Core/Literals/UnitLiterals.h"

#include "Core/Log/Log.h"
#include "Core/Math/FMatrix.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/PhysicsConversion.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace PhysicsRuntime {
    DEFINE_LOG_CATEGORY_STATIC(LogConstraint, Log, All);

    namespace {
        /**
         * @brief 0으로 둘 수 없는 한계 각도의 최솟값이며 단위는 도입니다.
         */
        constexpr Core::FAngle MinimumLimitAngle{0.0001_deg};

        /**
         * @brief 한계 근처에서 미리 반응하기 시작하는 기본 각도 여유이며 단위는 도입니다.
         */
        constexpr Core::FAngle ContactDistance{1_deg};

        /**
         * @brief 관절이 벌어졌을 때 강제로 붙이기 시작하는 거리이며 단위는 m입니다.
         */
        constexpr Core::FLength ProjectionLinearTolerance{0.05_m};

        /**
         * @brief 관절이 꺾였을 때 강제로 맞추기 시작하는 각도이며 단위는 도입니다.
         */
        constexpr Core::FAngle ProjectionAngularTolerance{180_deg};

        /**
         * @brief 값을 최솟값과 최댓값 사이로 자릅니다.
         * @param[in] Value 자를 값입니다.
         * @param[in] Minimum 최솟값입니다.
         * @param[in] Maximum 최댓값입니다.
         * @return 자른 값이며, 최솟값이 최댓값보다 크면 최댓값입니다.
         */
        Core::FAngle Clamp(Core::FAngle Value, Core::FAngle Minimum, Core::FAngle Maximum) {
            return Value < Minimum ? Minimum : Value < Maximum ? Value : Maximum;
        }

        /**
         * @brief 회전 방식을 PhysX 값으로 바꿉니다.
         * @param[in] Motion 바꿀 회전 방식입니다.
         * @return PhysX 관절 축의 움직임 값입니다.
         */
        physx::PxD6Motion::Enum U2PAngularMotion(EAngularConstraintMotion Motion) {
            switch (Motion) {
                case EAngularConstraintMotion::Free:
                    return physx::PxD6Motion::eFREE;
                case EAngularConstraintMotion::Limited:
                    return physx::PxD6Motion::eLIMITED;
                case EAngularConstraintMotion::Locked:
                default:
                    return physx::PxD6Motion::eLOCKED;
            }
        }

        /**
         * @brief 이동 방식을 PhysX 값으로 바꿉니다.
         * @param[in] Motion 바꿀 이동 방식입니다.
         * @return PhysX 관절 축의 움직임 값입니다.
         * @note 한계 거리 값이 없으므로 Limited는 Locked로 다룹니다.
         */
        physx::PxD6Motion::Enum U2PLinearMotion(ELinearConstraintMotion Motion) {
            return Motion == ELinearConstraintMotion::Free ? physx::PxD6Motion::eFREE : physx::PxD6Motion::eLOCKED;
        }
    }

    /**
     * @brief PhysX 관절이 없는 기본 설정으로 구성합니다.
     */
    FConstraintInstance::FConstraintInstance()
		: mSwing1Limit{45_deg},
		  mSwing2Limit{45_deg},
		  mTwistLimit{45_deg} {
    }

    /**
     * @brief PhysX 관절이 남아 있으면 해제합니다.
     */
    FConstraintInstance::~FConstraintInstance() {
        TermConstraint();
    }

    /**
     * @brief 다른 관절의 설정값만 복사합니다.
     */
    void FConstraintInstance::CopyConstraintParamsFrom(const FConstraintInstance& Other) {
        mJointName = Other.mJointName;
        mConstraintBone1 = Other.mConstraintBone1;
        mConstraintBone2 = Other.mConstraintBone2;
        mPos1 = Other.mPos1;
        mPriAxis1 = Other.mPriAxis1;
        mSecAxis1 = Other.mSecAxis1;
        mPos2 = Other.mPos2;
        mPriAxis2 = Other.mPriAxis2;
        mSecAxis2 = Other.mSecAxis2;
        mLinearXMotion = Other.mLinearXMotion;
        mLinearYMotion = Other.mLinearYMotion;
        mLinearZMotion = Other.mLinearZMotion;
        mSwing1Motion = Other.mSwing1Motion;
        mSwing2Motion = Other.mSwing2Motion;
        mTwistMotion = Other.mTwistMotion;
        mSwing1Limit = Other.mSwing1Limit;
        mSwing2Limit = Other.mSwing2Limit;
        mTwistLimit = Other.mTwistLimit;
    }

    /**
     * @brief 두 바디를 PhysX 관절로 잇고 움직임 한계를 적용합니다.
     */
    bool FConstraintInstance::InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2, float Scale) {
        TermConstraint();

        physx::PxRigidActor* Actor1{Body1 != nullptr ? Body1->GetPxRigidActor() : nullptr};
        physx::PxRigidActor* Actor2{Body2 != nullptr ? Body2->GetPxRigidActor() : nullptr};

        if (Actor1 == nullptr && Actor2 == nullptr) {
            return false;
        }

        // 관절 위치는 그 관절 프레임이 속한 바디의 스케일만큼 늘어난 자리에 둡니다.
        // 프레임 위치는 뼈 기준 값이고 바디의 형상은 뼈의 월드 스케일(부모 뼈들의 스케일 포함)로 만들어지므로,
        // 같은 스케일을 써야 한다. 컴포넌트 스케일만 쓰면 뼈에 스케일이 있는 스켈레톤(예: cm 단위를 0.01 루트 스케일로 줄인 FBX)에서
        // 관절 위치가 형상보다 수십~수백 배 멀리 놓여 시뮬레이션이 폭주한다.
        // 바디가 뼈의 스케일을 담고 있지 않은 호출(스케일 1)에서는 호출자가 넘긴 Scale을 그대로 쓴다.
        const auto ScaleOf{[Scale](const FBodyInstance* Body) {
            if (Body == nullptr) {
                return Scale;
            }

            const FVector& BodyScale{Body->GetBodyScale()};
            const float BodyMin{std::min({std::abs(BodyScale.x), std::abs(BodyScale.y), std::abs(BodyScale.z)})};

            return BodyMin > 0.0f && std::isfinite(BodyMin) ? BodyMin : Scale;
        }};
        FTransform Local1{GetRefFrame(EConstraintFrame::Frame1)};
        FTransform Local2{GetRefFrame(EConstraintFrame::Frame2)};

        if (Actor1 != nullptr) {
            Local1.mLocation *= ScaleOf(Body1);
        }

        if (Actor2 != nullptr) {
            Local2.mLocation *= ScaleOf(Body2);
        }

        // 부모 바디를 앞에, 자식 바디를 뒤에 둡니다.
        mConstraintHandle = physx::PxD6JointCreate(PxGetPhysics(), Actor2, PhysicsRuntime::ToPxTransform(Local2), Actor1, PhysicsRuntime::ToPxTransform(Local1));

        if (mConstraintHandle == nullptr) {
            MACAW_LOG(LogConstraint, Warning, "Cannot create a PhysX D6 joint.");

            return false;
        }

        // 관절에서 이 객체를 찾을 수 있도록 주소를 넣습니다.
        mConstraintHandle->userData = this;

        // 이어진 두 바디끼리는 부딪히지 않고, 크게 벌어지면 강제로 붙입니다.
        mConstraintHandle->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, false);
        mConstraintHandle->setConstraintFlag(physx::PxConstraintFlag::ePROJECTION, true);
        mConstraintHandle->setProjectionLinearTolerance(ProjectionLinearTolerance.GetBase());
        mConstraintHandle->setProjectionAngularTolerance(ProjectionAngularTolerance.GetBase());

        mConstraintHandle->setMotion(physx::PxD6Axis::eX, U2PLinearMotion(mLinearXMotion));
        mConstraintHandle->setMotion(physx::PxD6Axis::eY, U2PLinearMotion(mLinearYMotion));
        mConstraintHandle->setMotion(physx::PxD6Axis::eZ, U2PLinearMotion(mLinearZMotion));

        // 두 흔들기 각도는 원뿔 하나로 함께 제한합니다.
        if (mSwing1Motion == EAngularConstraintMotion::Limited || mSwing2Motion == EAngularConstraintMotion::Limited) {
            const Core::FAngle Limit1{Clamp(mSwing1Limit, MinimumLimitAngle, 179.9999_deg)};
            const Core::FAngle Limit2{Clamp(mSwing2Limit, MinimumLimitAngle, 179.9999_deg)};
            const Core::FAngle Contact{Clamp(ContactDistance, 1_deg, std::min(mSwing1Limit, mSwing2Limit) * 0.49f)};

            mConstraintHandle->setSwingLimit(physx::PxJointLimitCone{Limit2.GetBase(), Limit1.GetBase(), Contact.GetBase()});
        }

        mConstraintHandle->setMotion(physx::PxD6Axis::eSWING2, U2PAngularMotion(mSwing1Motion));
        mConstraintHandle->setMotion(physx::PxD6Axis::eSWING1, U2PAngularMotion(mSwing2Motion));

        if (mTwistMotion == EAngularConstraintMotion::Limited) {
            const Core::FAngle TwistLimit{mTwistLimit};
            const Core::FAngle Contact{Clamp(ContactDistance, 1_deg, TwistLimit * 0.95f)};

            mConstraintHandle->setTwistLimit(physx::PxJointAngularLimitPair{-TwistLimit.GetBase(), TwistLimit.GetBase(), Contact.GetBase()});
        }

        mConstraintHandle->setMotion(physx::PxD6Axis::eTWIST, U2PAngularMotion(mTwistMotion));

        return true;
    }

    /**
     * @brief PhysX 관절을 해제합니다.
     */
    void FConstraintInstance::TermConstraint() {
        if (mConstraintHandle == nullptr) {
            return;
        }

        // 해제 뒤 이 객체를 가리키지 않도록 주소를 먼저 지웁니다.
        mConstraintHandle->userData = nullptr;
        mConstraintHandle->release();
        mConstraintHandle = nullptr;
    }

    /**
     * @brief PhysX 관절이 만들어져 있는지 검사합니다.
     */
    bool FConstraintInstance::IsValidConstraintInstance() const {
        return mConstraintHandle != nullptr;
    }

    /**
     * @brief 한쪽 바디 기준의 관절 위치와 방향을 구합니다.
     */
    FTransform FConstraintInstance::GetRefFrame(EConstraintFrame Frame) const {
        const bool UseFrame1{Frame == EConstraintFrame::Frame1};
        const FVector& PriAxis{UseFrame1 ? mPriAxis1 : mPriAxis2};
        const FVector& SecAxis{UseFrame1 ? mSecAxis1 : mSecAxis2};
        const FVector& Position{UseFrame1 ? mPos1 : mPos2};
        const FVector Axes[]{PriAxis, SecAxis, PriAxis.Cross(SecAxis), Position};

        // 주 축, 보조 축, 두 축의 외적, 위치를 행으로 쌓아 변환을 만듭니다.
        FMatrix Matrix{FMatrix::Identity()};

        for (Index_t Row{}; Row < 4; ++Row) {
            Matrix(Row, 0) = Axes[Row].x;
            Matrix(Row, 1) = Axes[Row].y;
            Matrix(Row, 2) = Axes[Row].z;
        }

        FTransform Result{};

        if (!Matrix.TryGetTransform(Result) || std::abs(Matrix.Determinant() - 1.0f) > 0.01f) {
            MACAW_LOG(LogConstraint, Warning, "The constraint frame axes are not orthonormal.");
        }

        Result.mScale = FVector{1.0f, 1.0f, 1.0f};

        return Result;
    }
}
