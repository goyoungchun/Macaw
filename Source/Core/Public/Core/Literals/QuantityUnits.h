#pragma once

#include "Core/Literals/FDimension.h"

#include <numbers>
#include <optional>
#include <string_view>

namespace Core {

    struct FQuantityUnit {
    public:
        std::string_view mSymbol{};
        Core::FDimension mDimension{};
        long double mScale{1.0L};
        std::string_view mAlias{};
    };

    namespace QuantityUnits {

        inline constexpr long double RadiansPerDegree{std::numbers::pi_v<long double> / 180.0L};
        inline constexpr long double RadiansPerTurn{2.0L * std::numbers::pi_v<long double>};

        inline constexpr Core::FQuantityUnit Scalar{"", Core::Dim::None, 1.0L};
        inline constexpr Core::FQuantityUnit Millimeter{"mm", Core::Dim::Length, 0.001L};
        inline constexpr Core::FQuantityUnit Centimeter{"cm", Core::Dim::Length, 0.01L};
        inline constexpr Core::FQuantityUnit Meter{"m", Core::Dim::Length, 1.0L};
        inline constexpr Core::FQuantityUnit Kilometer{"km", Core::Dim::Length, 1000.0L};
        inline constexpr Core::FQuantityUnit SquareMeter{"m2", Core::Dim::Area, 1.0L};
        inline constexpr Core::FQuantityUnit CubicMeter{"m3", Core::Dim::Volume, 1.0L};
        inline constexpr Core::FQuantityUnit PerMeter{"1/m", Core::Dim::None - Core::Dim::Length, 1.0L};
        inline constexpr Core::FQuantityUnit Gram{"g", Core::Dim::Mass, 0.001L};
        inline constexpr Core::FQuantityUnit Kilogram{"kg", Core::Dim::Mass, 1.0L};
        inline constexpr Core::FQuantityUnit Tonne{"t", Core::Dim::Mass, 1000.0L};
        inline constexpr Core::FQuantityUnit KilogramPerCubicMeter{"kg/m3", Core::Dim::Mass - Core::Dim::Volume, 1.0L};
        inline constexpr Core::FQuantityUnit Millisecond{"ms", Core::Dim::Time, 0.001L};
        inline constexpr Core::FQuantityUnit Second{"s", Core::Dim::Time, 1.0L};
        inline constexpr Core::FQuantityUnit Minute{"min", Core::Dim::Time, 60.0L};
        inline constexpr Core::FQuantityUnit Hour{"h", Core::Dim::Time, 3600.0L};
        inline constexpr Core::FQuantityUnit MeterPerSecond{"m/s", Core::Dim::Speed, 1.0L, "mps"};
        inline constexpr Core::FQuantityUnit KilometerPerHour{"km/h", Core::Dim::Speed, 1000.0L / 3600.0L, "kmh"};
        inline constexpr Core::FQuantityUnit MeterPerSecondSquared{"m/s2", Core::Dim::Acceleration, 1.0L, "mps2"};
        inline constexpr Core::FQuantityUnit Newton{"N", Core::Dim::Force, 1.0L};
        inline constexpr Core::FQuantityUnit Kilonewton{"kN", Core::Dim::Force, 1000.0L};
        inline constexpr Core::FQuantityUnit Joule{"J", Core::Dim::Energy, 1.0L};
        inline constexpr Core::FQuantityUnit Kilojoule{"kJ", Core::Dim::Energy, 1000.0L};
        inline constexpr Core::FQuantityUnit Watt{"W", Core::Dim::Power, 1.0L};
        inline constexpr Core::FQuantityUnit Kilowatt{"kW", Core::Dim::Power, 1000.0L};
        inline constexpr Core::FQuantityUnit Hertz{"Hz", Core::Dim::Frequency, 1.0L};
        inline constexpr Core::FQuantityUnit PerSecond{"1/s", Core::Dim::Frequency, 1.0L};
        inline constexpr Core::FQuantityUnit Radian{"rad", Core::Dim::Angle, 1.0L};
        inline constexpr Core::FQuantityUnit Degree{"deg", Core::Dim::Angle, RadiansPerDegree, "\xC2\xB0"};
        inline constexpr Core::FQuantityUnit Turn{"turn", Core::Dim::Angle, RadiansPerTurn};
        inline constexpr Core::FQuantityUnit RadianPerSecond{"rad/s", Core::Dim::AngularSpeed, 1.0L, "radps"};
        inline constexpr Core::FQuantityUnit DegreePerSecond{"deg/s", Core::Dim::AngularSpeed, RadiansPerDegree, "degps"};
        inline constexpr Core::FQuantityUnit RevolutionPerMinute{"rpm", Core::Dim::AngularSpeed, RadiansPerTurn / 60.0L};
        inline constexpr Core::FQuantityUnit RadianPerSecondSquared{"rad/s2", Core::Dim::AngularAcceleration, 1.0L, "radps2"};
        inline constexpr Core::FQuantityUnit DegreePerSecondSquared{"deg/s2", Core::Dim::AngularAcceleration, RadiansPerDegree, "degps2"};

    }

    Core::FQuantityUnit GetBaseQuantityUnit(Core::FDimension Dimension);
    const Core::FQuantityUnit* FindQuantityUnit(std::string_view Symbol, Core::FDimension Dimension);

    std::optional<float> ToQuantityBase(long double Value, const Core::FQuantityUnit& Unit);
    std::optional<float> FromQuantityBase(float Value, const Core::FQuantityUnit& Unit);
    std::optional<float> ParseQuantity(std::string_view Text, const Core::FQuantityUnit& DefaultUnit);

}
