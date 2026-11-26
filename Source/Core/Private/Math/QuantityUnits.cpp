#include "Core/Literals/QuantityUnits.h"

#include <charconv>
#include <cmath>
#include <limits>

using namespace Core::QuantityUnits;

namespace Core {

    namespace {

        constexpr const Core::FQuantityUnit* KnownUnits[]{
            &Scalar, &Millimeter, &Centimeter, &Meter, &Kilometer, &SquareMeter, &CubicMeter, &PerMeter,
            &Gram, &Kilogram, &Tonne, &KilogramPerCubicMeter, &Millisecond, &Second, &Minute, &Hour,
            &MeterPerSecond, &KilometerPerHour, &MeterPerSecondSquared, &Newton, &Kilonewton, &Joule,
            &Kilojoule, &Watt, &Kilowatt, &Hertz, &PerSecond, &Radian, &Degree, &Turn, &RadianPerSecond, &DegreePerSecond,
            &RevolutionPerMinute, &RadianPerSecondSquared, &DegreePerSecondSquared
        };

        std::string_view Trim(std::string_view Text) {
            const auto Start{Text.find_first_not_of(" \t\n\r")};

            if (Start == std::string_view::npos) {
                return {};
            }

            return Text.substr(Start, Text.find_last_not_of(" \t\n\r") - Start + 1);
        }

        std::optional<float> ToFiniteFloat(long double Value) {
            if (!std::isfinite(Value) || std::abs(Value) > std::numeric_limits<float>::max()) {
                return std::nullopt;
            }

            return static_cast<float>(Value);
        }

    }

    Core::FQuantityUnit GetBaseQuantityUnit(Core::FDimension Dimension) {
        for (const Core::FQuantityUnit* Unit : KnownUnits) {
            if (Unit->mDimension == Dimension && Unit->mScale == 1.0L) {
                return *Unit;
            }
        }

        return {{}, Dimension, 1.0L};
    }

    const Core::FQuantityUnit* FindQuantityUnit(std::string_view Symbol, Core::FDimension Dimension) {
        for (const Core::FQuantityUnit* Unit : KnownUnits) {
            if (Unit->mDimension == Dimension && (Unit->mSymbol == Symbol || (!Unit->mAlias.empty() && Unit->mAlias == Symbol))) {
                return Unit;
            }
        }

        return nullptr;
    }

    std::optional<float> ToQuantityBase(long double Value, const Core::FQuantityUnit& Unit) {
        return ToFiniteFloat(Value * Unit.mScale);
    }

    std::optional<float> FromQuantityBase(float Value, const Core::FQuantityUnit& Unit) {
        return ToFiniteFloat(static_cast<long double>(Value) / Unit.mScale);
    }

    std::optional<float> ParseQuantity(std::string_view Text, const Core::FQuantityUnit& DefaultUnit) {
        Text = Trim(Text);

        if (Text.starts_with('+')) {
            Text.remove_prefix(1);

            if (Text.starts_with('-')) {
                return std::nullopt;
            }
        }

        if (Text.empty()) {
            return std::nullopt;
        }

        long double Number{};
        const auto Result{std::from_chars(Text.data(), Text.data() + Text.size(), Number)};

        if (Result.ec != std::errc{}) {
            return std::nullopt;
        }

        const std::string_view Symbol{Trim(Text.substr(static_cast<std::size_t>(Result.ptr - Text.data())))};
        const Core::FQuantityUnit* Unit{Symbol.empty() ? &DefaultUnit : Core::FindQuantityUnit(Symbol, DefaultUnit.mDimension)};

        return Unit != nullptr ? Core::ToQuantityBase(Number, *Unit) : std::nullopt;
    }

}
