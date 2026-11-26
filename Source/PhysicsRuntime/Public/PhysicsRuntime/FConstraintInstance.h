/**
 * @file FConstraintInstance.h
 * @brief 두 뼈를 잇는 관절의 위치, 축과 움직임 한계를 정의합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Math/FTransform.h"
#include "Core/Literals/AngleFunctions.h"
#include "Core/Math/FVector.h"
#include "Core/Types.h"

namespace physx {
    class PxD6Joint;
}

namespace PhysicsRuntime {
    struct FBodyInstance;

    /**
     * @brief 관절이 두 바디 중 어느 쪽 기준인지 나타냅니다.
     * @ingroup PhysicsRuntime
     */
    enum class EConstraintFrame : Uint8 {
        /**
         * @brief 첫 번째 바디(자식) 기준입니다.
         */
        Frame1,

        /**
         * @brief 두 번째 바디(부모) 기준입니다.
         */
        Frame2
    };

    /**
     * @brief 관절의 회전 축 하나가 움직일 수 있는 방식입니다.
     * @ingroup PhysicsRuntime
     */
    enum class EAngularConstraintMotion : Uint8 {
        /**
         * @brief 제한 없이 돌아갑니다.
         */
        Free,

        /**
         * @brief 한계 각도 안에서만 돌아갑니다.
         */
        Limited,

        /**
         * @brief 돌아가지 않습니다.
         */
        Locked
    };

    /**
     * @brief 관절의 이동 축 하나가 움직일 수 있는 방식입니다.
     * @ingroup PhysicsRuntime
     */
    enum class ELinearConstraintMotion : Uint8 {
        /**
         * @brief 제한 없이 움직입니다.
         */
        Free,

        /**
         * @brief 한계 거리 안에서만 움직입니다.
         */
        Limited,

        /**
         * @brief 움직이지 않습니다.
         */
        Locked
    };

    /**
     * @brief 두 뼈를 잇는 관절의 위치, 축과 움직임 한계입니다.
     * @ingroup PhysicsRuntime
     * @note Bone1은 자식 뼈, Bone2는 부모 뼈이며, 위치와 축은 각 뼈의 로컬 공간 기준입니다.
     * @note 관절의 userData가 이 객체를 가리키므로 복사와 이동을 금지합니다.
     */
    struct FConstraintInstance {
    public:
        /**
         * @brief PhysX 관절이 없는 기본 설정으로 구성합니다.
         */
        FConstraintInstance();

        /**
         * @brief PhysX 관절이 남아 있으면 해제합니다.
         */
        ~FConstraintInstance();

        /**
         * @brief 관절 하나를 두 객체가 해제하지 않도록 복사를 금지합니다.
         */
        FConstraintInstance(const FConstraintInstance&) = delete;

        /**
         * @brief 관절 하나를 두 객체가 해제하지 않도록 대입을 금지합니다.
         */
        FConstraintInstance& operator=(const FConstraintInstance&) = delete;

    public:
        /**
         * @brief 다른 관절의 설정값만 복사합니다.
         * @param[in] Other 설정을 가져올 관절입니다.
         * @note PhysX 관절은 복사하지 않습니다.
         */
        void CopyConstraintParamsFrom(const FConstraintInstance& Other);

        /**
         * @brief 두 바디를 PhysX 관절로 잇고 움직임 한계를 적용합니다.
         * @param[in] Body1 첫 번째 바디(자식)입니다.
         * @param[in] Body2 두 번째 바디(부모)입니다.
         * @param[in] Scale 관절 위치에 곱할 컴포넌트 스케일입니다.
         * @return 관절을 만들었으면 true입니다.
         * @note 이미 관절이 있으면 해제하고 다시 만듭니다.
         */
        bool InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2, float Scale);

        /**
         * @brief PhysX 관절을 해제합니다. 여러 번 호출해도 안전합니다.
         */
        void TermConstraint();

        /**
         * @brief PhysX 관절이 만들어져 있는지 검사합니다.
         * @return 관절이 있으면 true입니다.
         */
        bool IsValidConstraintInstance() const;

        /**
         * @brief 한쪽 바디 기준의 관절 위치와 방향을 구합니다.
         * @param[in] Frame 기준으로 삼을 바디입니다.
         * @return 주 축을 X, 보조 축을 Y로 삼은 관절 변환입니다.
         */
        FTransform GetRefFrame(EConstraintFrame Frame) const;

    public:
        /**
         * @brief 관절의 이름입니다.
         */
        Core::FString mJointName{};

        /**
         * @brief 관절이 잇는 첫 번째 뼈(자식)의 이름입니다.
         */
        Core::FString mConstraintBone1{};

        /**
         * @brief 관절이 잇는 두 번째 뼈(부모)의 이름입니다.
         */
        Core::FString mConstraintBone2{};

        /**
         * @brief 첫 번째 뼈 기준 관절 위치이며 단위는 m입니다.
         */
        FVector mPos1{};

        /**
         * @brief 첫 번째 뼈 기준 관절의 주 축(비틀기 축)입니다.
         */
        FVector mPriAxis1{1.0f, 0.0f, 0.0f};

        /**
         * @brief 첫 번째 뼈 기준 관절의 보조 축입니다.
         */
        FVector mSecAxis1{0.0f, 1.0f, 0.0f};

        /**
         * @brief 두 번째 뼈 기준 관절 위치이며 단위는 m입니다.
         */
        FVector mPos2{};

        /**
         * @brief 두 번째 뼈 기준 관절의 주 축(비틀기 축)입니다.
         */
        FVector mPriAxis2{1.0f, 0.0f, 0.0f};

        /**
         * @brief 두 번째 뼈 기준 관절의 보조 축입니다.
         */
        FVector mSecAxis2{0.0f, 1.0f, 0.0f};

        /**
         * @brief X축 이동 방식입니다.
         */
        ELinearConstraintMotion mLinearXMotion{ELinearConstraintMotion::Locked};

        /**
         * @brief Y축 이동 방식입니다.
         */
        ELinearConstraintMotion mLinearYMotion{ELinearConstraintMotion::Locked};

        /**
         * @brief Z축 이동 방식입니다.
         */
        ELinearConstraintMotion mLinearZMotion{ELinearConstraintMotion::Locked};

        /**
         * @brief 첫 번째 흔들기 축의 회전 방식입니다.
         */
        EAngularConstraintMotion mSwing1Motion{EAngularConstraintMotion::Free};

        /**
         * @brief 두 번째 흔들기 축의 회전 방식입니다.
         */
        EAngularConstraintMotion mSwing2Motion{EAngularConstraintMotion::Free};

        /**
         * @brief 비틀기 축의 회전 방식입니다.
         */
        EAngularConstraintMotion mTwistMotion{EAngularConstraintMotion::Free};

        /**
         * @brief 첫 번째 흔들기 축의 한계 각도이며 단위는 도입니다.
         */
        Core::FAngle mSwing1Limit{};

        /**
         * @brief 두 번째 흔들기 축의 한계 각도이며 단위는 도입니다.
         */
        Core::FAngle mSwing2Limit{};

        /**
         * @brief 비틀기 축의 한계 각도이며 단위는 도입니다.
         */
        Core::FAngle mTwistLimit{};

    private:
        /**
         * @brief 소유한 PhysX 관절입니다.
         */
        physx::PxD6Joint* mConstraintHandle{};
    };
}
