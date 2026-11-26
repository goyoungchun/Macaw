float PreviewFloorGrid(float2 UV, float NormalZ) {
    const float2 MajorUV = {UV * 12.0f};
    const float2 MajorDistance = {abs(frac(MajorUV + 0.5f) - 0.5f)};
    const float2 MinorDistance = {abs(frac(MajorUV * 4.0f + 0.5f) - 0.5f)};
    const float2 PixelWidth = {max(fwidth(MajorUV), 0.0001f)};

    const float2 MajorLines = {1.0f - smoothstep(0.0f, 0.014f + PixelWidth, MajorDistance)};
    const float2 MinorLines = {1.0f - smoothstep(0.0f, 0.018f + PixelWidth * 4.0f, MinorDistance)};
    const float2 CrossArms = {(1.0f - smoothstep(0.07f, 0.09f + PixelWidth, MajorDistance)) * MajorLines.yx};

    const float Footprint = {max(PixelWidth.x, PixelWidth.y)};
    const float MajorFade = {1.0f - smoothstep(0.10f, 0.35f, Footprint)};
    const float MinorFade = {1.0f - smoothstep(0.025f, 0.09f, Footprint)};
    const float Major = {max(MajorLines.x, MajorLines.y) * MajorFade};
    const float Minor = {max(MinorLines.x, MinorLines.y) * MinorFade};
    const float Cross = {max(CrossArms.x, CrossArms.y) * MajorFade};

    const float GridColor = {1.0f - max(max(Major * 0.15f, Minor * 0.075f), Cross * 0.33f)};

    return lerp(0.8f, GridColor, saturate(NormalZ));
}

#define MACAW_PREVIEW_FLOOR
#include "UberLit.hlsli"
