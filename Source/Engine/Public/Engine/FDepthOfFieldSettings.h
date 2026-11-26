/**
 * @file FDepthOfFieldSettings.h
 * @brief 카메라별 피사계 심도 설정입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include <cmath>
#include "FDepthOfFieldSettings.generated.h"

namespace Engine {

    UENUM()
    enum class EDepthOfFieldApertureShape : Uint8 {
        Circle,
        Pentagon,
        Hexagon
    };

    /**
     * @brief 얇은 렌즈 모델로 흐림을 계산하는 피사계 심도 설정입니다.
     * @details 렌즈 초점거리와 센서 크기는 카메라가 제공합니다. 선명한 구간은 초점 거리·F-stop·렌즈·센서로 정해집니다.
     */
    USTRUCT()
    struct FDepthOfFieldSettings {
        GENERATED_BODY()

    public:
        UPROPERTY(Edit, Serialize)
        bool mEnabled{false};

        /** @brief 카메라에서 초점이 정확히 맞는 평면까지의 거리(m)입니다. */
        UPROPERTY(Edit, Serialize)
        Core::FLength mFocalDistance{Core::FLength::FromBase(5.0f)};

        /** @brief 조리개 값입니다. 조리개 지름은 렌즈 초점거리 / F-stop입니다. */
        UPROPERTY(Edit, Serialize)
        float mFStop{2.8f};

        /** @brief 전체 해상도에서의 블러 반경 상한(px)입니다. 성능과 근거리 전경 흐림의 한계를 정합니다. */
        UPROPERTY(Edit, Serialize)
        float mMaxBlurRadius{16.0f};

        UPROPERTY(Edit, Serialize)
        Engine::EDepthOfFieldApertureShape mApertureShape{Engine::EDepthOfFieldApertureShape::Circle};

        UPROPERTY(Edit, Serialize)
        bool mNearBlur{true};

        UPROPERTY(Edit, Serialize)
        bool mFarBlur{true};
    };

    /**
     * @brief 얇은 렌즈와 센서의 관계를 계산합니다. 시야각은 수직 시야각이며 센서 높이에 대응합니다.
     */
    namespace Lens {

        /** @brief 센서 높이(36×24mm 풀프레임)의 기본값입니다. */
        inline constexpr float DefaultSensorHeightMillimeters{24.0f};

        /**
         * @brief 수직 시야각과 센서 높이로 렌즈 초점거리를 구합니다.
         * @return 초점거리입니다. 입력이 유효하지 않으면 0입니다.
         */
        inline Core::FLength FocalLengthFromFieldOfView(Core::FAngle FieldOfView, Core::FLength SensorHeight) {
            const float HalfTangent{std::tan(FieldOfView.GetBase() * 0.5f)};

            return HalfTangent > 0.0f && std::isfinite(HalfTangent) ? Core::FLength::FromBase(SensorHeight.GetBase() * 0.5f / HalfTangent) : Core::FLength{};
        }

        /**
         * @brief 렌즈 초점거리와 센서 높이로 수직 시야각을 구합니다.
         * @return 수직 시야각입니다. 입력이 유효하지 않으면 0입니다.
         */
        inline Core::FAngle FieldOfViewFromFocalLength(Core::FLength FocalLength, Core::FLength SensorHeight) {
            return FocalLength.GetBase() > 0.0f ? Core::FAngle::FromBase(2.0f * std::atan(SensorHeight.GetBase() * 0.5f / FocalLength.GetBase())) : Core::FAngle{};
        }

    }

}
