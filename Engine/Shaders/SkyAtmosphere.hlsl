#include "Sky.hlsl"

cbuffer SkyAtmosphereConstants : register(b1) {
    float3 CameraPosition;
    float GroundRadius;
    float3 SunDirection;
    float AtmosphereHeight;
    float3 SunColor;
    float MieAnisotropy;
    float RayleighScaleHeight;
    float MieScaleHeight;
    float RayleighScatteringScale;
    float MieScatteringScale;
    float OzoneAbsorptionScale;
    float SunDiskChordSquared;
};

static const float Pi = {3.14159265f};
static const float GroundClearance = {0.001f};
static const uint ViewSampleCount = {24};
static const uint SunSampleCount = {8};

float2 IntersectSphere(float3 Position, float3 Direction, float Radius) {
    float Closest = {-dot(Position, Direction)};
    float RadialDistance = {length(Position)};
    float Discriminant = {Closest * Closest + (Radius - RadialDistance) * (Radius + RadialDistance)};
    float HalfDistance = {sqrt(max(Discriminant, 0.0f))};

    return Discriminant >= 0.0f ? float2(Closest - HalfDistance, Closest + HalfDistance) : float2(-1.0f, -1.0f);
}

float3 GetDensities(float3 Position) {
    float Altitude = {max(length(Position) - GroundRadius, 0.0f)};
    float Rayleigh = {exp(-Altitude / RayleighScaleHeight)};
    float Mie = {exp(-Altitude / MieScaleHeight)};
    float Ozone = {saturate(1.0f - abs(Altitude - 25.0f) / 15.0f)};

    return float3(Rayleigh, Mie, Ozone);
}

float3 GetOpticalDepth(float3 DensityIntegral) {
    static const float3 RayleighExtinction = {0.005802f, 0.013558f, 0.0331f};
    static const float MieExtinction = {0.008396f};
    static const float3 OzoneExtinction = {0.000650f, 0.001881f, 0.000085f};

    return RayleighExtinction * (RayleighScatteringScale * DensityIntegral.x) + MieExtinction * (MieScatteringScale * DensityIntegral.y) + OzoneExtinction * (OzoneAbsorptionScale * DensityIntegral.z);
}

float3 GetSunTransmittance(float3 Position) {
    float2 GroundHit = {IntersectSphere(Position, SunDirection, GroundRadius)};
    float3 Transmittance = {0.0f, 0.0f, 0.0f};

    if (GroundHit.x < 0.0f) {
        float Distance = {max(IntersectSphere(Position, SunDirection, GroundRadius + AtmosphereHeight).y, 0.0f)};
        float3 DensityIntegral = {0.0f, 0.0f, 0.0f};
        float PreviousDistance = {0.0f};

        [loop]
        for (uint Index = {1}; Index <= SunSampleCount; ++Index) {
            float Fraction = {(float)Index / SunSampleCount};
            float End = {Distance * Fraction * Fraction};
            float Middle = {0.5f * (PreviousDistance + End)};
            DensityIntegral += GetDensities(Position + SunDirection * Middle) * (End - PreviousDistance);
            PreviousDistance = End;
        }

        Transmittance = exp(-GetOpticalDepth(DensityIntegral));
    }

    return Transmittance;
}

float3 IntegrateSky(float3 Position, float3 Direction, float Start, float End, float SunCosine, out float3 ViewTransmittance) {
    static const float3 RayleighScattering = {0.005802f, 0.013558f, 0.0331f};
    static const float MieScattering = {0.003996f};

    float RayleighPhase = {3.0f / (16.0f * Pi) * (1.0f + SunCosine * SunCosine)};
    float MieAnisotropySquared = {MieAnisotropy * MieAnisotropy};
    float MieDenominator = {max(1.0f + MieAnisotropySquared - 2.0f * MieAnisotropy * SunCosine, 0.000001f)};
    float MiePhase = {3.0f / (8.0f * Pi) * (1.0f - MieAnisotropySquared) * (1.0f + SunCosine * SunCosine) / ((2.0f + MieAnisotropySquared) * MieDenominator * sqrt(MieDenominator))};
    float3 DensityIntegral = {0.0f, 0.0f, 0.0f};
    float3 ScatteredLight = {0.0f, 0.0f, 0.0f};
    float PreviousDistance = {Start};
    bool NearerGroundAtEnd = {dot(Position + Direction * (0.5f * (Start + End)), Direction) < 0.0f};

    [loop]
    for (uint Index = {1}; Index <= ViewSampleCount; ++Index) {
        float Fraction = {(float)Index / ViewSampleCount};
        float SampleEnd = {lerp(Start, End, NearerGroundAtEnd ? Fraction * (2.0f - Fraction) : Fraction * Fraction)};
        float3 SamplePosition = {Position + Direction * (0.5f * (PreviousDistance + SampleEnd))};
        float3 Densities = {GetDensities(SamplePosition)};
        float3 DensityStep = {Densities * (SampleEnd - PreviousDistance)};
        float3 Transmittance = {exp(-GetOpticalDepth(DensityIntegral + 0.5f * DensityStep))};
        float3 Scattering = {RayleighScattering * (RayleighScatteringScale * RayleighPhase * DensityStep.x) + MieScattering * (MieScatteringScale * MiePhase * DensityStep.y)};
        ScatteredLight += Transmittance * GetSunTransmittance(SamplePosition) * Scattering;
        DensityIntegral += DensityStep;
        PreviousDistance = SampleEnd;
    }

    ViewTransmittance = exp(-GetOpticalDepth(DensityIntegral));

    return ScatteredLight;
}

float4 PixelMainAtmosphere(FVertexOutput Input) : SV_TARGET {
    float3 Direction = {GetSkyDirection(Input.mDeviceCoordinate)};
    float SunCosine = {clamp(dot(Direction, SunDirection), -1.0f, 1.0f)};
    float3 SunOffset = {Direction - SunDirection};
    float DiskDistanceSquared = {dot(SunOffset, SunOffset)};
    float DiskEdgeWidth = {max(0.5f * fwidth(DiskDistanceSquared), 0.000000001f)};
    float DiskCoverage = {SunDiskChordSquared > 0.0f ? 1.0f - smoothstep(SunDiskChordSquared - DiskEdgeWidth, SunDiskChordSquared + DiskEdgeWidth, DiskDistanceSquared) : 0.0f};

    if (all(SunColor <= 0.0f)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 Position = {CameraPosition};
    float CameraRadius = {length(Position)};

    if (CameraRadius < GroundRadius + GroundClearance) {
        float3 Up = {CameraRadius > 0.0f ? Position / CameraRadius : float3(0.0f, 0.0f, 1.0f)};
        Position = Up * (GroundRadius + GroundClearance);
    }

    float2 AtmosphereHit = {IntersectSphere(Position, Direction, GroundRadius + AtmosphereHeight)};
    float Start = {max(AtmosphereHit.x, 0.0f)};
    float End = {AtmosphereHit.y};
    float GroundEntry = {IntersectSphere(Position, Direction, GroundRadius).x};
    bool HitsGround = {GroundEntry >= Start && GroundEntry <= End};

    if (HitsGround) {
        End = GroundEntry;
    }

    float3 Transmittance = {1.0f, 1.0f, 1.0f};
    float3 Luminance = {0.0f, 0.0f, 0.0f};

    if (End > Start) {
        Luminance = IntegrateSky(Position, Direction, Start, End, SunCosine, Transmittance);
    }

    if (HitsGround) {
        static const float GroundAlbedo = {0.1f};
        float3 GroundNormal = {normalize(Position + Direction * GroundEntry)};
        float SunElevation = {saturate(dot(GroundNormal, SunDirection))};

        if (SunElevation > 0.0f) {
            float3 GroundSample = {GroundNormal * (GroundRadius + GroundClearance)};
            Luminance += Transmittance * GetSunTransmittance(GroundSample) * (GroundAlbedo * SunElevation / Pi);
        }
    } else {
        static const float SunDiskLuminance = {4.0f};
        Luminance += Transmittance * (SunDiskLuminance * DiskCoverage);
    }

    static const float SkyExposure = {10.0f};
    float3 DisplayColor = {pow(saturate(1.0f - exp(-SkyExposure * SunColor * Luminance)), 1.0f / 2.2f)};

    return float4(DisplayColor, 1.0f);
}
