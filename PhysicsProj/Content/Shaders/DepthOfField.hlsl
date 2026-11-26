/**
 * @file DepthOfField.hlsl
 * @brief 전경과 배경을 절반 해상도에서 분리하고 흐리게 한 뒤 원본 장면에 합성합니다.
 */

#include "PostProcess.hlsl"

Texture2D<float4> ForegroundLayer : register(t2);
Texture2D<float4> BackgroundLayer : register(t3);

struct FDepthOfFieldLayers {
    float4 mForeground : SV_TARGET0;
    float4 mBackground : SV_TARGET1;
};

static const float MinLayerCoverage = {0.0001f};
static const int2 NeighborOffsets[8] = {
    int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1),
    int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1)
};

// 외접원 반경 1인 조리개에서 화면 좌표 Y의 가로 반폭을 계산합니다.
static const float2 PentagonShoulder = {0.9510565f, -0.3090170f};
static const float2 PentagonBaseCorner = {0.5877853f, 0.8090170f};

float ApertureHalfWidth(float Y, float Sides) {
    if (Sides == 5.0f) {
        const float UpperWidth = {PentagonShoulder.x * (Y + 1.0f) / (PentagonShoulder.y + 1.0f)};
        const float LowerT = {saturate((Y - PentagonShoulder.y) / (PentagonBaseCorner.y - PentagonShoulder.y))};
        const float LowerWidth = {lerp(PentagonShoulder.x, PentagonBaseCorner.x, LowerT)};

        return min(UpperWidth, LowerWidth);
    }

    if (Sides == 6.0f) {
        return 0.8660254f * saturate(2.0f * (1.0f - abs(Y)));
    }

    return sqrt(saturate(1.0f - Y * Y));
}

float ApertureSampleCoverage(float X, float HalfWidth) {
    return max(min(X + 0.5f, HalfWidth) - max(X - 0.5f, -HalfWidth), 0.0f);
}

float2 GetLayerWeights(float Depth) {
    float2 Weights = {float2(0.0f, 1.0f)};

    if (Depth < 1.0f) {
        Weights = ComputeDepthOfFieldWeights(ReconstructViewDepth(Depth), DepthOfFieldParameters.x, DepthOfFieldParameters.y, DepthOfFieldParameters.z);
    }

    return Weights * (1.0f - Settings.zw);
}

// 이웃 절반 해상도 텍셀의 Near 커버리지는 원본 2×2 픽셀의 평균입니다.
float4 LoadForegroundWeights(int2 FirstPixel, int2 LastPixel) {
    float4 Weights;

    [unroll]
    for (int Index = 0; Index < 4; ++Index) {
        const int2 Offset = {int2(Index % 2, Index / 2)};
        const int2 Pixel = {min(FirstPixel + Offset, LastPixel)};
        Weights[Index] = GetLayerWeights(SceneDepth.Load(int3(Pixel, 0))).x;
    }

    return Weights;
}

float4 DilateForeground(float4 Center, int2 FirstPixel, int2 LastPixel) {
    const int2 LastBlock = {LastPixel & ~1};
    float BestCoverage = {Center.a};
    int2 BestPixel = {FirstPixel};
    float4 BestWeights = {(float4)0};

    // 반해상도 3×3에서 가장 큰 커버리지를 선택합니다. 동률이면 중앙과 가까운 표본을 우선합니다.
    [loop]
    for (int Index = 0; Index < 8 && BestCoverage < 1.0f; ++Index) {
        const int2 Neighbor = {clamp(FirstPixel + NeighborOffsets[Index] * 2, int2(0, 0), LastBlock)};
        const float4 Weights = {LoadForegroundWeights(Neighbor, LastPixel)};
        const float Coverage = {dot(Weights, 0.25f)};

        if (Coverage > BestCoverage) {
            BestCoverage = Coverage;
            BestPixel = Neighbor;
            BestWeights = Weights;
        }
    }

    if (BestCoverage > Center.a) {
        float3 ExpandedColor = {(float3)0};

        [unroll]
        for (int Tap = 0; Tap < 4; ++Tap) {
            const int2 Offset = {int2(Tap % 2, Tap / 2)};
            const int2 Pixel = {min(BestPixel + Offset, LastPixel)};
            ExpandedColor += SceneColor.Load(int3(Pixel, 0)).rgb * BestWeights[Tap];
        }

        // 선택한 표본의 RGB와 알파를 함께 사용하고 작은 블러 반경에서는 확장을 줄입니다.
        const float4 Expanded = {float4(ExpandedColor * 0.25f, BestCoverage)};
        Center = lerp(Center, Expanded, saturate(DepthOfFieldParameters.w * 0.5f));
    }

    return Center;
}

FDepthOfFieldLayers SeparateDepthOfField(FVertexOutput Input) {
    const int2 FirstPixel = {int2(Input.mPosition.xy) * 2};
    const int2 LastPixel = {int2(ScreenSize.xy) - 1};
    FDepthOfFieldLayers Output = {(FDepthOfFieldLayers)0};

    [unroll]
    for (int Y = 0; Y < 2; ++Y) {
        [unroll]
        for (int X = 0; X < 2; ++X) {
            const int3 Pixel = {int3(min(FirstPixel + int2(X, Y), LastPixel), 0)};
            const float2 Weights = {GetLayerWeights(SceneDepth.Load(Pixel))};
            const float4 Color = {float4(SceneColor.Load(Pixel).rgb, 1.0f)};

            // 색상과 커버리지를 함께 필터링해 레이어 경계에서 색이 섞이거나 어두워지는 현상을 줄입니다.
            Output.mForeground += Color * Weights.x;
            Output.mBackground += Color * Weights.y;
        }
    }

    Output.mForeground *= 0.25f;
    Output.mBackground *= 0.25f;

    [branch]
    if (Settings.z == 0.0f && Output.mForeground.a < 1.0f) {
        Output.mForeground = DilateForeground(Output.mForeground, FirstPixel, LastPixel);
    }

    return Output;
}

FDepthOfFieldLayers BlurDepthOfField(FVertexOutput Input) {
    const float2 HalfSize = {ceil(ScreenSize.xy * 0.5f)};
    const float HalfRadius = {max(0.5f * DepthOfFieldParameters.w, 0.001f)};
    // 큰 반경에서는 표본 간격을 넓혀 커널이 읽는 텍셀 수를 제한합니다.
    const float SampleSpacing = {max(1.0f, HalfRadius / 10.0f)};
    const float Radius = {HalfRadius / SampleSpacing};
    const float ApertureBottom = {Radius * (Settings.y == 5.0f ? PentagonBaseCorner.y : 1.0f)};
    const float2 SampleStepUV = {SampleSpacing / HalfSize};
    const int RowExtent = {(int)ceil(Radius)};
    const bool PairSamples = {SampleSpacing == 1.0f};
    const int ColumnStep = {PairSamples ? 2 : 1};
    FDepthOfFieldLayers Output = {(FDepthOfFieldLayers)0};
    float TotalCoverage = {0.0f};

    [loop]
    for (int Y = -RowExtent; Y <= RowExtent; ++Y) {
        const float Top = {max(Y - 0.5f, -Radius)};
        const float Bottom = {min(Y + 0.5f, ApertureBottom)};
        const float RowCoverage = {Bottom - Top};

        if (RowCoverage <= 0.0f) {
            continue;
        }

        const float HalfWidth = {Radius * ApertureHalfWidth((Top + Bottom) * 0.5f / Radius, Settings.y)};
        const int ColumnExtent = {(int)ceil(HalfWidth - 0.5f)};

        [loop]
        for (int X = -ColumnExtent; X <= ColumnExtent; X += ColumnStep) {
            const float First = {ApertureSampleCoverage(X, HalfWidth)};
            const float Second = {PairSamples ? ApertureSampleCoverage(X + 1, HalfWidth) : 0.0f};
            const float Coverage = {First + Second};
            const float Weight = {RowCoverage * Coverage};

            // 한 텍셀 간격의 두 표본을 선형 필터로 묶어 같은 가중합을 한 번에 읽습니다.
            const float SampleX = {X + Second / max(Coverage, 0.000001f)};
            const float2 UV = {Input.mUV - float2(SampleX, Y) * SampleStepUV};
            Output.mForeground += ForegroundLayer.SampleLevel(LinearClamp, UV, 0.0f) * Weight;
            Output.mBackground += BackgroundLayer.SampleLevel(LinearClamp, UV, 0.0f) * Weight;
            TotalCoverage += Weight;
        }
    }

    Output.mForeground /= TotalCoverage;
    Output.mBackground /= TotalCoverage;

    return Output;
}

float3 CompositeBackground(float3 Color, float BackgroundWeight, float2 LayerUV) {
    // 전체 해상도 깊이로 배경을 합성해 초점 물체의 경계를 보존합니다.
    [branch]
    if (BackgroundWeight > 0.0f) {
        const float4 Background = {BackgroundLayer.SampleLevel(LinearClamp, LayerUV, 0.0f)};

        if (Background.a > MinLayerCoverage) {
            Color = lerp(Color, Background.rgb / Background.a, BackgroundWeight);
        }
    }

    return Color;
}

float3 ReconstructBehindForeground(int2 Pixel, float Depth, float2 HalfSize, float3 Fallback) {
    const int2 LastPixel = {int2(ScreenSize.xy) - 1};
    const float Radius = {DepthOfFieldParameters.w};
    float4 Behind = {(float4)0};

    [loop]
    for (int Ring = 0; Ring < 2; ++Ring) {
        const float Distance = {Ring == 0 ? max(1.0f, Radius * 0.4f) : Radius + 2.0f};
        const float Weight = {Ring == 0 ? 2.0f : 1.0f};
        const int AxialOffset = {(int)round(Distance)};
        const int DiagonalOffset = {(int)round(Distance * 0.70710678f)};

        [loop]
        for (int Index = 0; Index < 8; ++Index) {
            const int2 Offset = {NeighborOffsets[Index] * (Index < 4 ? AxialOffset : DiagonalOffset)};
            const int2 Neighbor = {clamp(Pixel + Offset, int2(0, 0), LastPixel)};
            const int3 SamplePixel = {int3(Neighbor, 0)};
            const float SampleDepth = {SceneDepth.Load(SamplePixel)};
            const float2 Weights = {GetLayerWeights(SampleDepth)};

            if (SampleDepth > Depth && Weights.x <= MinLayerCoverage) {
                const float2 LayerUV = {(float2(Neighbor) + 0.5f) * 0.5f / HalfSize};
                const float3 Color = {CompositeBackground(SceneColor.Load(SamplePixel).rgb, Weights.y, LayerUV)};
                Behind += float4(Color, 1.0f) * Weight;
            }
        }
    }

    return Behind.a > 0.0f ? Behind.rgb / Behind.a : Fallback;
}

float4 CompositeDepthOfField(FVertexOutput Input) : SV_TARGET {
    const int2 Pixel = {int2(Input.mPosition.xy)};
    const float4 Original = {SceneColor.Load(int3(Pixel, 0))};
    const float Depth = {SceneDepth.Load(int3(Pixel, 0))};
    const float2 Weights = {GetLayerWeights(Depth)};
    const float2 HalfSize = {ceil(ScreenSize.xy * 0.5f)};
    const float2 LayerUV = {Input.mPosition.xy * 0.5f / HalfSize};
    float3 Color = {CompositeBackground(Original.rgb, Weights.y, LayerUV)};

    const float4 Foreground = {ForegroundLayer.SampleLevel(LinearClamp, LayerUV, 0.0f)};
    const float Coverage = {saturate(Foreground.a)};
    const float Uncovered = {1.0f - Coverage};
    const float MissingCoverage = {Weights.x - Coverage};

    [branch]
    if (Foreground.a > 0.0f && MissingCoverage > MinLayerCoverage) {
        // 블러로 비워진 영역은 뒤쪽 표본으로 채워 원본 Near 윤곽이 다시 섞이지 않게 합니다.
        const float3 NearColor = {Foreground.rgb / Foreground.a};
        const float3 Behind = {ReconstructBehindForeground(Pixel, Depth, HalfSize, NearColor)};
        Color = lerp(Color, Behind, saturate(MissingCoverage / Uncovered));
    }

    Color = Foreground.rgb + Color * Uncovered;

    // 반경이 1px보다 작으면 절반 해상도 필터의 영향을 원본으로 부드럽게 줄입니다.
    return float4(lerp(Original.rgb, Color, saturate(DepthOfFieldParameters.w)), Original.a);
}
