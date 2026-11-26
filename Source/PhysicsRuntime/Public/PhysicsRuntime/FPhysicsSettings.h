/**
 * @file FPhysicsSettings.h
 * @brief PhysX 런타임과 물리 장면이 따르는 조정 가능한 정책을 정의합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/Types.h"

namespace PhysicsRuntime {

    /**
     * @brief PhysX 런타임과 물리 장면이 따르는 조정 가능한 정책입니다.
     * @details FPhysicsRuntime이 보관하며 SDK 객체 생성, 월드별 장면 생성, 프레임별 장면 진행에서 읽습니다.
     *          엔진 1유닛을 1m로 보는 단위 규약은 조정 대상이 아니므로 포함하지 않습니다.
     * @ingroup PhysicsRuntime
     */
    struct FPhysicsSettings {
    public:
        /**
         * @brief 허용 오차 기준으로 쓰는 일반적인 물체 속력(m/s)입니다.
         */
        Core::FSpeed mTypicalSpeed{mDefaultTypicalSpeed};

        /**
         * @brief 시뮬레이션 작업을 실행할 CPU 디스패처의 작업 스레드 수입니다.
         */
        Uint32 mWorkerThreadCount{2};

        /**
         * @brief 질량을 지정하지 않은 동적 바디의 질량을 형상 부피로 계산할 때 쓰는 밀도(kg/m³)입니다.
         */
        Core::FDensity mDefaultDensity{mDefaultMassDensity};

        /**
         * @brief PhysX Visual Debugger 연결을 시도할지 여부입니다.
         */
        bool mEnablePvd{true};

        /**
         * @brief PVD 프로그램이 대기하는 호스트 주소입니다.
         */
        Core::FString mPvdHost{"127.0.0.1"};

        /**
         * @brief PVD 프로그램이 대기하는 포트입니다.
         */
        Int32 mPvdPort{5425};

        /**
         * @brief PVD 연결을 기다리는 최대 시간(밀리초)입니다.
         */
        Uint32 mPvdConnectTimeoutMilliseconds{10};

        /**
         * @brief 물리 장면에 적용할 중력 가속도(m/s²)이며 엔진 Up 축(+Z)의 반대 방향이 기본값입니다.
         */
        FVector mGravity{0.0f, 0.0f, mDefaultGravityZ};

        /**
         * @brief 빠르게 움직이는 강체의 터널링을 막는 연속 충돌 검출(CCD)을 장면에서 허용할지 여부입니다.
         * @note 실제 적용에는 강체별 CCD 설정도 필요합니다.
         */
        bool mEnableCCD{true};

        /**
         * @brief 접촉 정보를 프레임 사이에 유지해 안정성을 높이는 지속 접촉 다양체(PCM)를 사용할지 여부입니다.
         */
        bool mEnablePCM{true};

        /**
         * @brief 물리 장면을 한 번에 진행하는 고정 스텝 시간(초)이며 유한한 양수여야 합니다.
         */
        Core::FTime mFixedStep{mDefaultFixedStep};

        /**
         * @brief 한 프레임에 진행할 수 있는 최대 고정 스텝 수이며, 넘는 시간은 버립니다.
         */
        Uint32 mMaxStepsPerFrame{4};

    private:
        static const Core::FSpeed mDefaultTypicalSpeed;
        static const Core::FDensity mDefaultMassDensity;
        static const float mDefaultGravityZ;
        static const Core::FTime mDefaultFixedStep;
    };

}
