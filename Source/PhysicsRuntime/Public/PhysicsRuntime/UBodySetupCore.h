/**
 * @file UBodySetupCore.h
 * @brief 바디 셋업이 연결될 본을 식별하는 기반 클래스입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/ReflectionMacros.h"
#include "CoreUObject/UObject.h"
#include "UBodySetupCore.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 바디 셋업이 연결될 본을 식별하는 기반 클래스입니다.
     * @details 스태틱 메시처럼 바디가 하나인 경우 본 이름을 비워 두며, 피직스 에셋에서는 바디가 따라갈 본을 지정합니다.
     * @ingroup PhysicsRuntime
     */
    UCLASS()
    class UBodySetupCore : public CoreUObject::UObject {
        GENERATED_BODY()

    public:

        /**
         * @brief 본에 연결되지 않은 바디 셋업을 구성합니다.
         */
        UBodySetupCore();

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~UBodySetupCore() override;

    public:

        /**
         * @brief 바디가 연결될 본 이름을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조이며, 본에 연결되지 않았으면 빈 문자열입니다.
         */
        const Core::FString& GetBoneName() const;

        /**
         * @brief 바디가 연결될 본 이름을 변경합니다.
         * @param[in] BoneName 연결할 본 이름이며, 빈 문자열이면 본 연결을 해제합니다.
         */
        void SetBoneName(const Core::FString& BoneName);

    private:

        /**
         * @brief 바디가 연결될 본 이름입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Getter = GetBoneName, Setter = SetBoneName)
        Core::FString mBoneName{};
    };

}
