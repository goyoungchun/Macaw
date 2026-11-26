#include "Core/Math/FRandomStream.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <random>

FRandomStream::FRandomStream(Int32 Seed)
    : mInitialSeed{Seed}
    , mSeed{static_cast<std::uint32_t>(Seed)} {
}

void FRandomStream::Initialize(Int32 Seed) {
    mInitialSeed = Seed;
    mSeed = static_cast<std::uint32_t>(Seed);
}

void FRandomStream::GenerateNewSeed() {
    Initialize(static_cast<Int32>(std::random_device{}()));
}

void FRandomStream::Reset() {
    mSeed = static_cast<std::uint32_t>(mInitialSeed);
}

float FRandomStream::GetFraction() {
    mSeed = mSeed * 196314165u + 907633515u;

    // 지수부를 1로 고정한 [1, 2) 실수에서 1을 빼 [0, 1)을 만듭니다.
    return std::bit_cast<float>(0x3F800000u | (mSeed >> 9)) - 1.0f;
}

float FRandomStream::FRandRange(float Minimum, float Maximum) {
    return Minimum + (Maximum - Minimum) * GetFraction();
}

FVector FRandomStream::FRandRange(const FVector& Minimum, const FVector& Maximum) {
    const float X{FRandRange(Minimum.x, Maximum.x)};
    const float Y{FRandRange(Minimum.y, Maximum.y)};
    const float Z{FRandRange(Minimum.z, Maximum.z)};

    return {X, Y, Z};
}

Int32 FRandomStream::RandRange(Int32 Minimum, Int32 Maximum) {
    if (Maximum <= Minimum) {
        return Minimum;
    }

    const Int32 Value{Minimum + static_cast<Int32>(GetFraction() * static_cast<float>(Maximum - Minimum + 1))};

    return Value > Maximum ? Maximum : Value;
}

FVector FRandomStream::VRand() {
    const float Z{FRandRange(-1.0f, 1.0f)};
    const float Angle{FRandRange(0.0f, 2.0f * std::numbers::pi_v<float>)};
    const float Radius{std::sqrt(std::max(1.0f - Z * Z, 0.0f))};

    return {Radius * std::cos(Angle), Radius * std::sin(Angle), Z};
}
