/**
 * @file UWindSourceComponent.h
 * @brief 월드에 바람을 내는 원천 컴포넌트입니다. 방향 바람, 점 바람, 원뿔(선풍기) 바람을 지원합니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "Engine/Component/USceneComponent.h"
#include "Engine/EWindSourceType.h"

#include "UWindSourceComponent.generated.h"

namespace Engine {
    class FPrimitiveDrawInterface;
}

namespace Engine {

    /**
     * @brief 월드의 바람장(FWindField)에 등록되어 위치별 바람 속도에 기여하는 원천입니다.
     * @details 바람이 부는 방향은 컴포넌트의 앞쪽(X축)입니다. 점 바람은 위치에서 바깥으로 불고, 방향과 원뿔 바람은 앞쪽으로 붑니다.
     *          천과 머리카락은 자기 위치에서 월드의 바람장에 질의해 이 값을 받습니다.
     * @ingroup Engine
     */
    UCLASS()
    class UWindSourceComponent final : public Engine::USceneComponent {
        GENERATED_BODY()

    public:

        /**
         * @brief 기본값(방향 바람, 세기 5 m/s)으로 구성합니다.
         */
        UWindSourceComponent();

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~UWindSourceComponent() override;

    public:

        /**
         * @brief 컴포넌트를 월드에 연결하고 월드의 바람장에 원천으로 등록합니다.
         * @param[in] World 연결할 월드입니다.
         */
        void RegisterComponent(Engine::UWorld* World) override;

        /**
         * @brief 월드의 바람장에서 원천을 빼고 월드와의 연결을 해제합니다.
         */
        void UnregisterComponent() override;

        /**
         * @brief 이 원천이 월드 위치에 만드는 바람 속도를 계산합니다.
         * @param[in] Position 바람을 알고 싶은 월드 위치입니다.
         * @return 월드 좌표의 바람 속도(m/s)입니다. 꺼져 있거나 범위 밖이면 영벡터입니다.
         */
        FVector SampleWind(const FVector& Position) const;

        /**
         * @brief 원천의 방향과 범위를 에디터 뷰포트에 선으로 그립니다.
         * @param[in,out] Draw 선을 추가할 표시 목록입니다.
         * @param[in] Highlight 선택된 액터이면 true이며, 주황색으로 그립니다.
         */
        void DrawVisualization(Engine::FPrimitiveDrawInterface& Draw, bool Highlight) const;

    private:

        /**
         * @brief 바람 원천의 종류입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Wind")
        Engine::EWindSourceType mWindType{Engine::EWindSourceType::Directional};

        /**
         * @brief 켜져 있을 때만 바람에 기여합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Wind")
        bool mWindEnabled{true};

        /**
         * @brief 바람 세기(m/s)입니다. 점, 원뿔 바람은 원천 가까이에서의 세기입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Wind")
        float mStrength{5.0f};

        /**
         * @brief 점, 원뿔 바람이 닿는 거리(m)입니다. 방향 바람에는 쓰지 않습니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Wind")
        float mRadius{10.0f};

        /**
         * @brief 거리 감쇠 지수입니다. 0이면 반경 안에서 세기가 일정하고, 클수록 원천 가까이에만 강하게 붑니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Wind")
        float mFalloff{1.0f};

        /**
         * @brief 원뿔 바람의 반각(도)입니다. 앞쪽 축에서 이 각도 안에서만 붑니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Wind")
        float mConeHalfAngle{30.0f};
    };

}
