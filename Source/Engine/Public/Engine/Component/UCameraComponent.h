/**
 * @file UCameraComponent.h
 * @brief 시야각 설정과 월드 변환으로부터 카메라 뷰 생성에 대한 선언입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/USceneComponent.h"
#include "Core/Literals/QuantityTypes.h"
#include "Engine/FDepthOfFieldSettings.h"
#include "UCameraComponent.generated.h"

struct FVector2;

namespace Engine {

    struct FSceneView;
    struct FMinimalViewInfo;

    /**
     * @brief 시야각 설정과 월드 변환으로부터 카메라 뷰 생성을 담당합니다.
     */
    UCLASS()
    class UCameraComponent final : public Engine::USceneComponent {
        GENERATED_BODY()

    public:
        UCameraComponent();

    public:
        /**
         * @brief 시야각을 직접 지정하는지 반환합니다. false이면 렌즈 초점거리와 센서 높이로 시야각을 계산합니다.
         */
        bool GetUseFieldOfView() const;

        /**
         * @brief 시야각 지정 방식을 바꿉니다. 전환 직전의 화면 구도가 유지되도록 반대쪽 값을 갱신합니다.
         * @return 항상 true입니다.
         */
        bool SetUseFieldOfView(bool UseFieldOfView);

        /**
         * @brief 카메라의 실제 수직 시야각을 반환합니다. 렌즈 모드에서는 초점거리와 센서 높이로 계산합니다.
         * @return 카메라의 시야각입니다.
         */
        Core::FAngle GetFieldOfView() const;

        /**
         * @brief 유효한 범위의 시야각을 검사하여 카메라에 설정합니다. 렌즈 모드에서는 이 시야각이 되도록 초점거리를 바꿉니다.
         * @param[in] FieldOfView 설정할 카메라 시야각이며 도 단위입니다.
         * @return 요청한 처리를 완료하면 true, 실패하면 false입니다.
         */
        bool SetFieldOfView(Core::FAngle FieldOfView);

        /**
         * @brief 렌즈 초점거리를 반환합니다. 시야각 모드에서는 시야각과 센서 높이로 계산합니다.
         */
        Core::FLength GetFocalLength() const;

        /**
         * @brief 렌즈 초점거리를 설정합니다. 시야각 모드에서는 이 초점거리에 해당하는 시야각으로 바꿉니다.
         * @return 0보다 큰 유한한 값을 적용했으면 true입니다.
         */
        bool SetFocalLength(Core::FLength FocalLength);

        /**
         * @brief 카메라 센서(필름백)의 세로 크기를 반환합니다.
         */
        Core::FLength GetSensorHeight() const;

        /**
         * @brief 센서의 세로 크기를 설정합니다. 시야각 모드에서는 시야각, 렌즈 모드에서는 초점거리를 유지합니다.
         * @return 0보다 큰 유한한 값을 적용했으면 true입니다.
         */
        bool SetSensorHeight(Core::FLength SensorHeight);

        /**
         * @brief 카메라의 피사계 심도 설정을 반환합니다.
         */
        const Engine::FDepthOfFieldSettings& GetDepthOfFieldSettings() const;

        /**
         * @brief 모든 피사계 심도 설정을 검증한 뒤 한 번에 적용합니다.
         * @return 유효한 설정을 적용했으면 true입니다.
         */
        bool SetDepthOfFieldSettings(const Engine::FDepthOfFieldSettings& Settings);

        /**
         * @brief 피사계 심도의 초점 거리(m)를 변경합니다.
         * @return 0 이상의 유한한 거리를 적용했으면 true입니다.
         */
        bool SetDepthOfFieldFocalDistance(Core::FLength Distance);

        /**
         * @brief 카메라의 현재 월드 변환, 시야각, 피사계 심도 설정으로 시점 값을 만듭니다.
         * @return 카메라 관리자가 블렌드에 사용할 시점 값입니다.
         */
        Engine::FMinimalViewInfo GetCameraViewInfo() const;

        /**
         * @brief 카메라의 월드 변환과 시야각으로 뷰·투영 행렬을 계산합니다.
         * @param[in] Size 계산할 뷰포트의 가로·세로 픽셀 크기입니다.
         * @param[out] View 계산한 카메라 뷰·투영 상태를 받을 출력입니다.
         * @return 요청한 처리를 완료하면 true, 실패하면 false입니다.
         */
        bool GetCameraView(const FVector2& Size, Engine::FSceneView& View) const;

    private:
        // 불러올 때 프로퍼티 순서대로 Setter가 호출됩니다. 센서 높이를 시야각·초점거리보다 먼저 두어야 저장값이 그대로 복원됩니다.

        /**
         * @brief true이면 시야각을 직접 지정하고, false이면 초점거리와 센서 높이로 시야각을 계산합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Camera", Getter = GetUseFieldOfView, Setter = SetUseFieldOfView)
        bool mUseFieldOfView{true};

        /**
         * @brief 센서(필름백)의 세로 크기입니다. 수직 시야각과 착란원의 화면 크기 환산에 씁니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Camera", Getter = GetSensorHeight, Setter = SetSensorHeight, Meta = (SerializeUnit = "mm"))
        Core::FLength mSensorHeight{};

        /**
         * @brief 시야각 모드에서 쓰는 수직 시야각이며 도 단위입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Camera", Getter = GetFieldOfView, Setter = SetFieldOfView, Meta = (SerializeUnit = "deg"))
        Core::FAngle mFieldOfView{};

        /**
         * @brief 렌즈 모드에서 쓰는 렌즈 초점거리입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Camera", Getter = GetFocalLength, Setter = SetFocalLength, Meta = (SerializeUnit = "mm"))
        Core::FLength mFocalLength{};

        UPROPERTY(Edit, Serialize, Category = "Camera", Name = "DepthOfField", Getter = GetDepthOfFieldSettings, Setter = SetDepthOfFieldSettings)
        Engine::FDepthOfFieldSettings mDepthOfFieldSettings{};
    };

}
