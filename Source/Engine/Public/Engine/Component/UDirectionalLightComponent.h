/**
 * @file UDirectionalLightComponent.h
 * @brief 방향광과 캐스케이드 그림자 설정을 관리합니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/ULightComponent.h"
#include "Core/Literals/AngleFunctions.h"
#include "UDirectionalLightComponent.generated.h"

namespace Engine {

    /**
     * @brief 방향광과 캐스케이드 그림자 설정을 관리합니다.
     * @ingroup Engine
     */
    UCLASS()
    class UDirectionalLightComponent final : public Engine::ULightComponent {
        GENERATED_BODY()

    public:
        /**
         * @brief UDirectionalLightComponent의 기본 상태를 초기화합니다.
         */
        UDirectionalLightComponent();

        /**
         * @brief UDirectionalLightComponent 객체의 수명을 끝내고 소유 자원을 정리합니다.
         */
        ~UDirectionalLightComponent() override;

    public:
        /**
         * @brief 방향광 그림자의 캐스케이드 개수를 반환합니다.
         * @return 방향광 그림자의 캐스케이드 개수입니다.
         */
        Uint32 GetCascadeCount() const;

        /**
         * @brief 방향광 그림자의 캐스케이드 개수를 검증하고 변경합니다.
         * @param[in] Count 1 이상 4 이하인 캐스케이드 개수입니다.
         * @return 입력값을 수락하고 상태를 갱신하면 true이며 잘못된 값이면 false입니다.
         */
        bool SetCascadeCount(Uint32 Count);

        /**
         * @brief 방향광 그림자의 최대 표시 거리를 반환합니다.
         * @return 방향광 그림자의 최대 표시 거리입니다.
         */
        Core::FLength GetShadowDistance() const;

        /**
         * @brief 방향광 그림자의 최대 표시 거리를 검증하고 변경합니다.
         * @param[in] Distance 1 이상 100000 이하인 유한한 그림자 표시 거리입니다.
         * @return 입력값을 수락하고 상태를 갱신하면 true이며 잘못된 값이면 false입니다.
         */
        bool SetShadowDistance(Core::FLength Distance);

        /**
         * @brief 각 캐스케이드 구간에서 전환에 사용하는 비율을 반환합니다.
         * @return 각 캐스케이드 구간에서 전환에 사용하는 비율입니다.
         */
        float GetCascadeTransitionFraction() const;

        /**
         * @brief 각 캐스케이드 구간에서 전환에 사용하는 비율을 검증하고 변경합니다.
         * @param[in] Fraction 0 이상 0.5 이하인 유한한 캐스케이드 전환 비율입니다.
         * @return 입력값을 수락하고 상태를 갱신하면 true이며 잘못된 값이면 false입니다.
         */
        bool SetCascadeTransitionFraction(float Fraction);

        Core::FAngle GetSourceAngle() const;
        bool SetSourceAngle(Core::FAngle Angle);

        /**
         * @brief PCSS 반그림자 계산에 쓰는 광원 각반지름을 반환합니다.
         * @return 방향광 광원의 각반지름(도)입니다.
         */
        Core::FAngle GetShadowSourceAngle() const;

        /**
         * @brief PCSS 반그림자 계산에 쓰는 광원 각반지름을 검증하고 변경합니다.
         * @param[in] Angle 0 이상 15 이하인 유한한 각반지름(도)입니다.
         * @return 입력값을 수락하고 상태를 갱신하면 true이며 잘못된 값이면 false입니다.
         */
        bool SetShadowSourceAngle(Core::FAngle Angle);

        bool IsAtmosphereSunLight() const;
        bool SetAtmosphereSunLight(bool Enabled);

        /**
         * @brief 현재 컴포넌트의 조명·그림자 설정으로 렌더 프록시를 생성합니다.
         * @return 렌더 장면에 복사할 광원 상태와 아이콘의 비소유 주소입니다.
         */
        Engine::FLightSceneProxy CreateLightProxy() const override;
        void Serialize(Core::FArchive& Archive) override;

    private:
        /**
         * @brief 방향광 그림자의 캐스케이드 개수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Light", Getter = GetCascadeCount, Setter = SetCascadeCount)
        Uint32 mCascadeCount{4};

        /**
         * @brief 방향광 그림자의 최대 표시 거리입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Light", Getter = GetShadowDistance, Setter = SetShadowDistance)
        Core::FLength mShadowDistance{};

        /**
         * @brief 각 캐스케이드 구간에서 전환에 사용하는 비율입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Light", Getter = GetCascadeTransitionFraction, Setter = SetCascadeTransitionFraction)
        float mCascadeTransitionFraction{0.25f};

        /**
         * @brief 방향광 광원의 각지름(도)입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Light", Getter = GetSourceAngle, Setter = SetSourceAngle, Meta = (SerializeUnit = "deg"))
        Core::FAngle mSourceAngle{};

        UPROPERTY(Edit, Serialize, Category = "Atmosphere", Getter = IsAtmosphereSunLight, Setter = SetAtmosphereSunLight)
        bool mAtmosphereSunLight{};
    };

}
