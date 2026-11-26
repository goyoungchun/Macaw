/**
 * @file UBodySetup.h
 * @brief 하나의 바디를 이루는 충돌 형상을 보관하고 PhysX 형상으로 만듭니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FVector.h"
#include "PhysicsRuntime/UBodySetupCore.h"
#include "PhysicsRuntime/FKAggregateGeom.h"

#include <cmath>
#include <memory>

#include "UBodySetup.generated.h"

namespace physx {
    class PxRigidActor;
    class PxMaterial;
}

namespace PhysicsRuntime {
    class FPhysicsRuntime;
    class UPhysicalMaterial;
}

namespace PhysicsRuntime {

    /**
     * @brief 하나의 바디를 이루는 충돌 형상을 보관하고 PhysX 형상으로 만듭니다.
     * @details 스태틱 메시는 바디 셋업 하나를, 피직스 에셋은 본마다 바디 셋업 하나를 가집니다.
     *          형상 크기는 스케일을 적용하기 전 값이며, 바디를 만들 때 컴포넌트 스케일을 형상 크기에 반영합니다.
     * @ingroup PhysicsRuntime
     */
    UCLASS()
    class UBodySetup : public PhysicsRuntime::UBodySetupCore {
        GENERATED_BODY()

    public:

        /**
         * @brief 형상이 없는 바디 셋업을 구성합니다.
         */
        UBodySetup();

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~UBodySetup() override;

    public:

        /**
         * @brief 바디를 이루는 충돌 형상 묶음을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        PhysicsRuntime::FKAggregateGeom& GetAggGeom();

        /**
         * @brief 바디를 이루는 충돌 형상 묶음을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const PhysicsRuntime::FKAggregateGeom& GetAggGeom() const;

        /**
         * @brief 바디에 지정한 물리 재질을 반환합니다.
         * @return 재질의 비소유 주소이며, 기본 재질을 사용하면 nullptr입니다.
         */
        const PhysicsRuntime::UPhysicalMaterial* GetPhysicalMaterial() const;

        /**
         * @brief 바디를 생성할 때 적용할 물리 재질의 소유권을 받습니다.
         * @param[in] Material 바디가 소유할 재질이며, nullptr이면 기본 재질을 사용합니다.
         * @return 재질 설정에 성공하면 true입니다.
         */
        bool SetPhysicalMaterial(std::unique_ptr<PhysicsRuntime::UPhysicalMaterial> Material);

        /**
         * @brief 물리 시뮬레이션이 켜졌을 때 이 바디가 시뮬레이션(Dynamic)되는지 반환합니다.
         * @return true이면 Dynamic, false이면 시뮬레이션 중에도 Kinematic으로 남습니다.
         * @note 피직스 에셋의 바디에만 쓰며, 스태틱 메시의 바디 셋업은 이 값을 사용하지 않습니다.
         */
        bool ShouldSimulatePhysics() const {
            return mSimulatePhysics;
        }

        /**
         * @brief 물리 시뮬레이션이 켜졌을 때 이 바디가 시뮬레이션(Dynamic)될지 설정합니다.
         * @param[in] Simulate true이면 Dynamic, false이면 Kinematic입니다.
         */
        void SetSimulatePhysics(bool Simulate) {
            mSimulatePhysics = Simulate;
        }

        /**
         * @brief 이 바디의 모양이 다른 모양과 접촉(충돌)을 만드는지 반환합니다.
         * @return false이면 이 바디는 같은 에셋의 바디와도 월드의 다른 것과도 충돌하지 않습니다. 중력과 관절의 영향은 그대로 받습니다.
         * @note 피직스 에셋의 바디에만 쓰며, 뷰포트 선택(레이캐스트)에는 영향을 주지 않습니다.
         */
        bool IsCollisionEnabled() const {
            return mCollisionEnabled;
        }

        /**
         * @brief 이 바디의 모양이 다른 모양과 접촉(충돌)을 만들지 정합니다.
         * @param[in] Enabled false이면 어떤 모양과도 충돌하지 않습니다.
         */
        void SetCollisionEnabled(bool Enabled) {
            mCollisionEnabled = Enabled;
        }

        /**
         * @brief 이 바디의 선형 감쇠 계수를 반환합니다. 클수록 이동 속도가 빨리 줄어듭니다.
         * @return 감쇠 계수(1/s)입니다.
         * @note 피직스 에셋의 바디에만 쓰며, 시뮬레이션할 때 바디 인스턴스에 복사됩니다.
         */
        Core::FFrequency GetLinearDamping() const {
            return mLinearDamping;
        }

        /**
         * @brief 이 바디의 선형 감쇠 계수를 설정합니다.
         * @param[in] Damping 0 이상의 유한한 값입니다. 유효하지 않으면 무시합니다.
         */
        void SetLinearDamping(Core::FFrequency Damping) {
            if (Damping.IsFinite() && Damping >= Core::FFrequency{}) {
                mLinearDamping = Damping;
            }
        }

        /**
         * @brief 이 바디의 회전 감쇠 계수를 반환합니다. 클수록 회전 속도가 빨리 줄어들어 떨림이 줄어듭니다.
         * @return 감쇠 계수(1/s)입니다.
         */
        Core::FFrequency GetAngularDamping() const {
            return mAngularDamping;
        }

        /**
         * @brief 이 바디의 회전 감쇠 계수를 설정합니다.
         * @param[in] Damping 0 이상의 유한한 값입니다. 유효하지 않으면 무시합니다.
         */
        void SetAngularDamping(Core::FFrequency Damping) {
            if (Damping.IsFinite() && Damping >= Core::FFrequency{}) {
                mAngularDamping = Damping;
            }
        }

        /**
         * @brief 이 바디의 질량을 반환합니다.
         * @return 질량(kg)이며, 0이면 충돌 형상의 부피와 기본 밀도로 계산합니다.
         */
        Core::FMass GetMassInKg() const {
            return mMassInKg;
        }

        /**
         * @brief 이 바디의 질량을 설정합니다.
         * @param[in] Mass 0 이상의 유한한 값입니다. 0이면 밀도로 계산합니다. 유효하지 않으면 무시합니다.
         */
        void SetMassInKg(Core::FMass Mass) {
            if (Mass.IsFinite() && Mass >= Core::FMass{}) {
                mMassInKg = Mass;
            }
        }

        /**
         * @brief 이 바디의 관성 배율을 반환합니다.
         * @return 질량 중심 기준 관성 모멘트에 곱하는 값이며 1이면 형상 그대로입니다. 클수록 회전이 둔해집니다.
         */
        float GetInertiaScale() const {
            return mInertiaScale;
        }

        /**
         * @brief 이 바디의 관성 배율을 설정합니다.
         * @param[in] Scale 0보다 큰 유한한 값입니다. 유효하지 않으면 무시합니다.
         */
        void SetInertiaScale(float Scale) {
            if (std::isfinite(Scale) && Scale > 0.0f) {
                mInertiaScale = Scale;
            }
        }

        /**
         * @brief 이 바디가 바람에 반응하는 정도를 반환합니다.
         * @return 바람 속도와 바디 속도의 차이에 곱하는 계수(1/s)이며, 0이면 바람의 영향을 받지 않습니다.
         * @note 독립 시뮬레이션(Rigid Body 노드)의 Dynamic 바디에만 쓰며, 질량과 무관하게 가속도로 적용합니다.
         */
        float GetWindInfluence() const {
            return mWindInfluence;
        }

        /**
         * @brief 이 바디가 바람에 반응하는 정도를 설정합니다.
         * @param[in] Influence 0 이상의 유한한 값입니다. 유효하지 않으면 무시합니다.
         */
        void SetWindInfluence(float Influence) {
            if (std::isfinite(Influence) && Influence >= 0.0f) {
                mWindInfluence = Influence;
            }
        }

        /**
         * @brief 충돌 형상마다 PhysX 형상을 만들어 강체에 붙입니다.
         * @param[in,out] Actor 형상을 붙일 PhysX 강체입니다.
         * @param[in] Scale 형상 크기와 중심에 반영할 컴포넌트 스케일입니다.
         * @param[in] Runtime 형상의 재질 생성과 볼록 메시 쿠킹을 제공하는 PhysX 런타임입니다.
         * @param[in] MaterialOverride nullptr이 아니면 바디 셋업의 재질 대신 모든 형상에 이 재질을 씁니다. 형상이 재질을 참조하므로 호출이 끝난 뒤 호출자가 해제해도 됩니다.
         * @return 강체에 붙인 형상의 개수입니다.
         * @note 스케일을 반영한 크기가 유효하지 않은 형상은 건너뜁니다. 회전한 상자·캡슐에 비균등 스케일을 주면 근사합니다.
         *       볼록 형상은 호출할 때마다 쿠킹하며, 쿠킹한 메시는 형상만 참조하므로 바디가 해제될 때 함께 해제됩니다.
         */
        Index_t AddShapesToRigidActor(physx::PxRigidActor& Actor, const FVector& Scale, const PhysicsRuntime::FPhysicsRuntime& Runtime, const physx::PxMaterial* MaterialOverride = nullptr) const;

    private:

        /**
         * @brief 바디를 이루는 충돌 형상 묶음입니다.
         */
        UPROPERTY(Serialize)
        PhysicsRuntime::FKAggregateGeom mAggGeom{};

        /**
         * @brief 바디의 모든 충돌 형상에 적용할 물리 재질입니다.
         */
        std::unique_ptr<PhysicsRuntime::UPhysicalMaterial> mPhysicalMaterial{};

        /**
         * @brief 피직스 에셋에서 시뮬레이션이 켜졌을 때 이 바디를 Dynamic으로 시뮬레이션할지 나타냅니다.
         * @details 리플렉션 직렬화 대상이 아니므로 스태틱 메시 에셋의 저장 형식에 영향을 주지 않습니다. 피직스 에셋 파일이 따로 저장합니다.
         */
        bool mSimulatePhysics{true};

        /**
         * @brief 피직스 에셋에서 이 바디의 모양이 충돌하는지 나타냅니다. mSimulatePhysics처럼 리플렉션 직렬화 대상이 아니며 물리 에셋 파일이 따로 저장합니다.
         */
        bool mCollisionEnabled{true};

        /**
         * @brief 선형 감쇠 계수(1/s)입니다. 기본값은 0이 아니어서 작은 진동이 쌓이지 않습니다. 피직스 에셋 파일이 따로 저장합니다.
         */
        Core::FFrequency mLinearDamping{Core::FFrequency::FromBase(0.1f)};

        /**
         * @brief 회전 감쇠 계수(1/s)입니다. 머리카락처럼 가는 형상의 회전 떨림을 줄이도록 기본값을 둡니다.
         */
        Core::FFrequency mAngularDamping{Core::FFrequency::FromBase(0.5f)};

        /**
         * @brief 질량(kg)입니다. 0이면 형상 부피와 기본 밀도로 계산합니다.
         */
        Core::FMass mMassInKg{};

        /**
         * @brief 관성 모멘트에 곱하는 배율입니다.
         */
        float mInertiaScale{1.0f};

        /**
         * @brief 바람에 반응하는 정도(1/s)입니다. 0이면 바람을 받지 않습니다. 피직스 에셋 파일이 따로 저장합니다.
         */
        float mWindInfluence{0.0f};
    };

}
