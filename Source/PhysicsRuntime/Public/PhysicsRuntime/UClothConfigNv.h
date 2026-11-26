/**
 * @file UClothConfigNv.h
 * @brief NvCloth 천의 세기, 감쇠, 관성, 충돌 같은 시뮬레이션 설정을 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FVector.h"
#include "CoreUObject/UObject.h"

#include "UClothConfigNv.generated.h"

namespace Core {
    class FArchive;
}

namespace PhysicsRuntime {

    /**
     * @brief 바람을 천에 적용하는 방식입니다.
     * @ingroup PhysicsRuntime
     */
    enum class EClothingWindMethodNv : Uint8 {
        /**
         * @brief 입자마다 바람 방향 가속도를 직접 더하는 단순한 방식입니다.
         */
        Legacy,

        /**
         * @brief 삼각형 면에 항력과 양력을 계산하는 방식입니다.
         */
        Accurate
    };

    /**
     * @brief 제약 위상 한 종류(세로, 가로, 굽힘, 전단)의 세기 설정입니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothConstraintSetupNv {
        /**
         * @brief 제약을 지키려는 세기이며 0이면 풀지 않고 1이면 한 번에 맞춥니다.
         */
        float mStiffness{1.0f};

        /**
         * @brief 늘어남, 줄어듦 한도 안에서 쓰는 세기 배율입니다.
         */
        float mStiffnessMultiplier{1.0f};

        /**
         * @brief 원래 길이에 대한 늘어날 수 있는 비율이며 1이면 늘어나지 않습니다.
         */
        float mStretchLimit{1.0f};

        /**
         * @brief 원래 길이에 대한 줄어들 수 있는 비율이며 1이면 줄어들지 않습니다.
         */
        float mCompressionLimit{1.0f};
    };

    /**
     * @brief NvCloth 천 하나에 넣는 시뮬레이션 설정입니다.
     * @details 천 에셋이 소유하고, 천을 만들 때 NvCloth에 넣습니다. 길이 단위는 미터입니다.
     * @ingroup PhysicsRuntime
     */
    UCLASS()
    class UClothConfigNv : public CoreUObject::UObject {
        GENERATED_BODY()

    public:

        /**
         * @brief 기본 설정을 구성합니다.
         */
        UClothConfigNv();

        /**
         * @brief 설정을 정리합니다.
         */
        ~UClothConfigNv() override;

    public:

        /**
         * @brief 천끼리 부딪히는 기능을 쓰는지 검사합니다.
         * @return 반지름과 세기가 모두 0보다 크면 true입니다.
         */
        bool UseSelfCollisions() const;

        /**
         * @brief 천끼리 부딪힐 입자를 고를 때 쓰는 거리를 구합니다.
         * @return 천끼리 부딪히지 않으면 0이고, 아니면 반지름에 컬링 배율을 곱한 값(m)입니다.
         */
        Core::FLength NeedsSelfCollisionIndices() const;

        /**
         * @brief 모든 설정 값을 저장하거나 불러옵니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         */
        void SerializeConfig(Core::FArchive& Archive);

    public:

        /**
         * @brief 바람을 천에 적용하는 방식입니다.
         */
        EClothingWindMethodNv mClothingWindMethod{EClothingWindMethodNv::Legacy};

        /**
         * @brief 세로(보통 중력 방향) 제약 세기입니다.
         */
        FClothConstraintSetupNv mVerticalConstraint{};

        /**
         * @brief 가로 제약 세기입니다.
         */
        FClothConstraintSetupNv mHorizontalConstraint{};

        /**
         * @brief 굽힘 제약 세기입니다.
         */
        FClothConstraintSetupNv mBendConstraint{};

        /**
         * @brief 전단 제약 세기입니다.
         */
        FClothConstraintSetupNv mShearConstraint{};

        /**
         * @brief 천끼리 부딪힐 때 입자 사이에 둘 거리(m)이며 0이면 천끼리 부딪히지 않습니다.
         */
        Core::FLength mSelfCollisionRadius{};

        /**
         * @brief 천끼리 부딪힐 때 밀어내는 세기이며 0이면 천끼리 부딪히지 않습니다.
         */
        float mSelfCollisionStiffness{};

        /**
         * @brief 천끼리 부딪힐 입자를 고를 때 반지름에 곱하는 배율이며, 처음부터 이 거리 안에 있는 입자는 빼냅니다.
         */
        float mSelfCollisionCullScale{1.0f};

        /**
         * @brief 축마다 속도를 줄이는 비율(0~1)이며 클수록 빨리 멈춥니다.
         */
        FVector mDamping{0.4f, 0.4f, 0.4f};

        /**
         * @brief 충돌체 표면에서 미끄러지는 속도를 줄이는 마찰 계수입니다.
         */
        float mFriction{0.1f};

        /**
         * @brief 정확한 바람 방식에서 쓰는 항력 계수입니다.
         */
        float mWindDragCoefficient{0.02f / 100.0f};

        /**
         * @brief 정확한 바람 방식에서 쓰는 양력 계수입니다.
         */
        float mWindLiftCoefficient{0.02f / 100.0f};

        /**
         * @brief 이 천에 기본으로 부는 월드 좌표 바람 속도(m/s)입니다.
         * @details 컴포넌트의 천 바람 속도가 0이면 이 값을 씁니다. 메시 뷰어 미리 보기도 이 값을 씁니다.
         */
        FVector mDefaultWindVelocity{};

        /**
         * @brief 기준 뼈가 움직일 때 천이 끌려가는 정도를 축마다 줄이는 비율(0~1)입니다.
         */
        FVector mLinearDrag{0.2f, 0.2f, 0.2f};

        /**
         * @brief 기준 뼈가 회전할 때 천이 끌려가는 정도를 축마다 줄이는 비율(0~1)입니다.
         */
        FVector mAngularDrag{0.2f, 0.2f, 0.2f};

        /**
         * @brief 기준 뼈의 이동 가속도가 천에 전해지는 배율(0~1)입니다.
         */
        FVector mLinearInertiaScale{1.0f, 1.0f, 1.0f};

        /**
         * @brief 기준 뼈의 회전 가속도가 천에 전해지는 배율(0~1)입니다.
         */
        FVector mAngularInertiaScale{1.0f, 1.0f, 1.0f};

        /**
         * @brief 기준 뼈의 회전으로 생기는 원심력이 천에 전해지는 배율(0~1)입니다.
         */
        FVector mCentrifugalInertiaScale{1.0f, 1.0f, 1.0f};

        /**
         * @brief 1초에 제약을 푸는 횟수(Hz)입니다.
         */
        Core::FFrequency mSolverFrequency{Core::FFrequency::FromBase(120.0f)};

        /**
         * @brief 세기와 감쇠 값이 기준으로 삼는 횟수(Hz)입니다.
         */
        Core::FFrequency mStiffnessFrequency{Core::FFrequency::FromBase(100.0f)};

        /**
         * @brief 월드 중력에 곱하는 배율입니다.
         */
        float mGravityScale{1.0f};

        /**
         * @brief 월드 중력 대신 쓸 중력 가속도(m/s²)입니다.
         */
        FVector mGravityOverride{0.0f, 0.0f, 0.0f};

        /**
         * @brief 월드 중력 대신 덮어쓸 중력을 쓰는지 나타냅니다.
         */
        bool mUseGravityOverride{};

        /**
         * @brief 고정 입자에서 멀어지지 않게 묶는 끈의 세기입니다.
         */
        float mTetherStiffness{1.0f};

        /**
         * @brief 끈이 원래 거리에 대해 늘어날 수 있는 비율입니다.
         */
        float mTetherLimit{1.0f};

        /**
         * @brief 천 두께를 흉내 내려고 충돌체 반지름에 더하는 값(m)입니다.
         */
        Core::FLength mCollisionThickness{Core::FLength::FromBase(0.01f)};

        /**
         * @brief 입자를 스키닝 위치 쪽으로 당기는 스프링 세기입니다.
         */
        float mAnimDriveSpringStiffness{1.0f};

        /**
         * @brief 입자 속도를 스키닝 속도에 맞추는 감쇠 세기입니다.
         */
        float mAnimDriveDamperStiffness{1.0f};
    };
}
