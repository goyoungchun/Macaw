#include "LightingCommon.hlsli"

#if !defined(MACAW_GBUFFER)
#include "PixelLighting.hlsli"
#endif

// 지형 멀티 레이어 블렌딩 상수
cbuffer LandscapePixelConstants : register(b6) {
	// 활성 레이어 수와 웨이트맵 수 및 컴포넌트 쿼드 수
	uint4 LandscapeLayerMeta;

	// 웨이트맵 텍셀 중심 보정용 배율과 오프셋
	float4 LandscapeWeightmapScaleBias;

	// 레이어별 기본 색상
	float4 LandscapeLayerBaseColor[8];

	// 레이어별 정반사 색상
	float4 LandscapeLayerSpecularColor[8];

	// 레이어별 정반사 강도 및 지수와 노멀 강도 및 노멀 맵 유무
	float4 LandscapeLayerSurfaceParams[8];

	// 레이어별 타일링 배율과 웨이트맵 슬롯 및 채널
	float4 LandscapeLayerUVAndChannel[8];
};

// 웨이트맵 클램프 샘플러
SamplerState LandscapeClampSampler : register(s4);

// 컴포넌트 웨이트맵 텍스처
Texture2D<float4> LandscapeWeightmap0 : register(t20);
Texture2D<float4> LandscapeWeightmap1 : register(t21);

// 활성 레이어 알베도 텍스처
Texture2D<float4> LandscapeLayerAlbedo0 : register(t22);
Texture2D<float4> LandscapeLayerAlbedo1 : register(t23);
Texture2D<float4> LandscapeLayerAlbedo2 : register(t24);
Texture2D<float4> LandscapeLayerAlbedo3 : register(t25);
Texture2D<float4> LandscapeLayerAlbedo4 : register(t26);
Texture2D<float4> LandscapeLayerAlbedo5 : register(t27);
Texture2D<float4> LandscapeLayerAlbedo6 : register(t28);
Texture2D<float4> LandscapeLayerAlbedo7 : register(t29);

// 활성 레이어 노멀 텍스처
Texture2D<float4> LandscapeLayerNormal0 : register(t30);
Texture2D<float4> LandscapeLayerNormal1 : register(t31);
Texture2D<float4> LandscapeLayerNormal2 : register(t32);
Texture2D<float4> LandscapeLayerNormal3 : register(t33);
Texture2D<float4> LandscapeLayerNormal4 : register(t34);
Texture2D<float4> LandscapeLayerNormal5 : register(t35);
Texture2D<float4> LandscapeLayerNormal6 : register(t36);
Texture2D<float4> LandscapeLayerNormal7 : register(t37);

struct FLandscapeSurface {
	float4 mColor;
	float3 mNormal;
	float3 mSpecularTint;
	float mSpecularStrength;
	float mShininess;
	float3 mEmissive;
};

float SampleLayerWeight(float4 Weight0, float4 Weight1, uint Slot) {
	const uint MapIndex = {(uint)LandscapeLayerUVAndChannel[Slot].z};
	const uint Channel = {(uint)LandscapeLayerUVAndChannel[Slot].w};
	const float4 Source = {MapIndex == 0u ? Weight0 : Weight1};

	if (Channel == 0u) {
		return Source.r;
	}

	if (Channel == 1u) {
		return Source.g;
	}

	if (Channel == 2u) {
		return Source.b;
	}

	return Source.a;
}

float4 SampleLayerAlbedo(uint Slot, float2 UV) {
	float4 Result = {LandscapeLayerAlbedo0.Sample(MaterialSampler, UV)};

	if (Slot == 1u) {
		Result = LandscapeLayerAlbedo1.Sample(MaterialSampler, UV);
	} else if (Slot == 2u) {
		Result = LandscapeLayerAlbedo2.Sample(MaterialSampler, UV);
	} else if (Slot == 3u) {
		Result = LandscapeLayerAlbedo3.Sample(MaterialSampler, UV);
	} else if (Slot == 4u) {
		Result = LandscapeLayerAlbedo4.Sample(MaterialSampler, UV);
	} else if (Slot == 5u) {
		Result = LandscapeLayerAlbedo5.Sample(MaterialSampler, UV);
	} else if (Slot == 6u) {
		Result = LandscapeLayerAlbedo6.Sample(MaterialSampler, UV);
	} else if (Slot >= 7u) {
		Result = LandscapeLayerAlbedo7.Sample(MaterialSampler, UV);
	}

	return Result;
}

float3 SampleLayerNormal(uint Slot, float2 UV) {
	float3 Result = {LandscapeLayerNormal0.Sample(MaterialSampler, UV).xyz};

	if (Slot == 1u) {
		Result = LandscapeLayerNormal1.Sample(MaterialSampler, UV).xyz;
	} else if (Slot == 2u) {
		Result = LandscapeLayerNormal2.Sample(MaterialSampler, UV).xyz;
	} else if (Slot == 3u) {
		Result = LandscapeLayerNormal3.Sample(MaterialSampler, UV).xyz;
	} else if (Slot == 4u) {
		Result = LandscapeLayerNormal4.Sample(MaterialSampler, UV).xyz;
	} else if (Slot == 5u) {
		Result = LandscapeLayerNormal5.Sample(MaterialSampler, UV).xyz;
	} else if (Slot == 6u) {
		Result = LandscapeLayerNormal6.Sample(MaterialSampler, UV).xyz;
	} else if (Slot >= 7u) {
		Result = LandscapeLayerNormal7.Sample(MaterialSampler, UV).xyz;
	}

	return Result;
}

float3 ApplyLayerNormalMap(float3 BaseNormal, float4 Tangent, float3 Sampled, float Strength) {
	const float3 N = {SafeNormal(BaseNormal)};
	const float3 T = {SafeNormal(Tangent.xyz - N * dot(N, Tangent.xyz))};
	const float3 B = {cross(N, T) * Tangent.w};
	float3 Map = {Sampled * 2.0f - 1.0f};
	Map.xy *= Strength;

	return SafeNormal(T * Map.x + B * Map.y + N * Map.z);
}

float3 GetLandscapeLayerDebugColor(uint Slot) {
	// 레이어 슬롯별 구분 색상 반환
	if (Slot == 0u) {
		return float3(0.18f, 0.82f, 0.28f);
	}

	if (Slot == 1u) {
		return float3(0.22f, 0.48f, 0.96f);
	}

	if (Slot == 2u) {
		return float3(0.96f, 0.76f, 0.16f);
	}

	if (Slot == 3u) {
		return float3(0.94f, 0.22f, 0.28f);
	}

	if (Slot == 4u) {
		return float3(0.16f, 0.86f, 0.88f);
	}

	if (Slot == 5u) {
		return float3(0.86f, 0.26f, 0.90f);
	}

	if (Slot == 6u) {
		return float3(0.96f, 0.48f, 0.14f);
	}

	return float3(0.92f, 0.92f, 0.92f);
}

FLandscapeSurface EvaluateLandscapeSurface(FPixelInput Input, bool IsFrontFace) {
	FLandscapeSurface Surface = {(FLandscapeSurface)0};
	float3 BaseNormal = {SafeNormal(Input.mNormal)};

	if (!IsFrontFace) {
		BaseNormal = -BaseNormal;
	}

	const uint LayerCount = {min(LandscapeLayerMeta.x, 8u)};

	if (LayerCount == 0u) {
#if defined(LANDSCAPE_WEIGHTMAP)
		Surface.mColor = float4(0.15f, 0.15f, 0.15f, 1.0f);
		Surface.mNormal = BaseNormal;

		return Surface;
#else
		const float2 FallbackUV = {Input.mWorldPosition.xy * 0.25f};
		Surface.mColor = BaseColor * Input.mColor * BaseColorTexture.Sample(MaterialSampler, FallbackUV);
		Surface.mNormal = BaseNormal;
		Surface.mSpecularTint = SpecularColor.rgb;
		Surface.mSpecularStrength = SurfaceParameters.x;
		Surface.mShininess = SurfaceParameters.y;
		Surface.mEmissive = EmissiveColor.rgb;

		return Surface;
#endif
	}

	const float2 WeightUV = {Input.mUV * LandscapeWeightmapScaleBias.xy + LandscapeWeightmapScaleBias.zw};
	const float4 Weight0 = {LandscapeLayerMeta.y > 0u ? LandscapeWeightmap0.Sample(LandscapeClampSampler, WeightUV) : float4(1.0f, 0.0f, 0.0f, 0.0f)};
	const float4 Weight1 = {LandscapeLayerMeta.y > 1u ? LandscapeWeightmap1.Sample(LandscapeClampSampler, WeightUV) : float4(0.0f, 0.0f, 0.0f, 0.0f)};

	float Weights[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	float WeightSum = {0.0f};

	[unroll]
	for (uint Index = {0u}; Index < 8u; ++Index) {
		const float RawWeight = {Index < LayerCount ? SampleLayerWeight(Weight0, Weight1, Index) : 0.0f};
		Weights[Index] = RawWeight;
		WeightSum += RawWeight;
	}

	if (WeightSum <= 0.0001f) {
		Weights[0] = 1.0f;
		WeightSum = 1.0f;
	}

	const float InvWeightSum = {1.0f / WeightSum};

#if defined(LANDSCAPE_WEIGHTMAP)
	// 활성 레이어 가중치를 색상으로 합성하고 지형 음영 보정 적용
	float3 DebugColor = {float3(0.0f, 0.0f, 0.0f)};

	[unroll]
	for (uint Slot = {0u}; Slot < 8u; ++Slot) {
		if (Slot >= LayerCount) {
			break;
		}

		const float Weight = {Weights[Slot] * InvWeightSum};
		DebugColor += GetLandscapeLayerDebugColor(Slot) * Weight;
	}

	const float3 ShadeDir = {normalize(float3(0.4f, 0.5f, 0.8f))};
	const float HillShade = {0.78f + 0.22f * saturate(dot(BaseNormal, ShadeDir))};
	Surface.mColor = float4(DebugColor * HillShade, 1.0f);
	Surface.mNormal = BaseNormal;

	return Surface;
#else
	float3 BlendedColor = {float3(0.0f, 0.0f, 0.0f)};
	float3 BlendedNormal = {float3(0.0f, 0.0f, 0.0f)};
	float3 BlendedSpecular = {float3(0.0f, 0.0f, 0.0f)};
	float BlendedSpecStrength = {0.0f};
	float BlendedShininess = {0.0f};

	[unroll]
	for (uint Slot = {0u}; Slot < 8u; ++Slot) {
		if (Slot >= LayerCount) {
			break;
		}

		const float Weight = {Weights[Slot] * InvWeightSum};

		if (Weight <= 0.001f) {
			continue;
		}

		const float2 LayerUV = {Input.mWorldPosition.xy * LandscapeLayerUVAndChannel[Slot].xy};
		const float4 AlbedoSample = {SampleLayerAlbedo(Slot, LayerUV)};
		const float4 LayerColor = {LandscapeLayerBaseColor[Slot] * AlbedoSample};
		BlendedColor += LayerColor.rgb * Weight;

		float3 LayerNormal = {BaseNormal};

		if (LandscapeLayerSurfaceParams[Slot].w > 0.5f) {
			LayerNormal = ApplyLayerNormalMap(BaseNormal, Input.mTangent, SampleLayerNormal(Slot, LayerUV), LandscapeLayerSurfaceParams[Slot].z);
		}

		BlendedNormal += LayerNormal * Weight;
		BlendedSpecular += LandscapeLayerSpecularColor[Slot].rgb * Weight;
		BlendedSpecStrength += LandscapeLayerSurfaceParams[Slot].x * Weight;
		BlendedShininess += LandscapeLayerSurfaceParams[Slot].y * Weight;
	}

	Surface.mColor = float4(BlendedColor, 1.0f) * Input.mColor;
	Surface.mNormal = SafeNormal(BlendedNormal);
	Surface.mSpecularTint = BlendedSpecular;
	Surface.mSpecularStrength = BlendedSpecStrength;
	Surface.mShininess = max(BlendedShininess, 1.0f);
	Surface.mEmissive = EmissiveColor.rgb;

	return Surface;
#endif
}

#if defined(MACAW_GBUFFER)
struct FGBufferOutput {
	float4 mColor : SV_TARGET0;
	float4 mNormal : SV_TARGET1;
	float4 mSpecular : SV_TARGET2;
	float4 mEmission : SV_TARGET3;
	float4 mAmbient : SV_TARGET4;
	float4 mReflection : SV_TARGET5;
};

FGBufferOutput PixelMain(FPixelInput Input, bool IsFrontFace : SV_IsFrontFace) {
	const FLandscapeSurface Surface = {EvaluateLandscapeSurface(Input, IsFrontFace)};
	FGBufferOutput Output = {(FGBufferOutput)0};
	Output.mColor = Surface.mColor;
	Output.mNormal = float4(Surface.mNormal, Surface.mShininess);

#if defined(MACAW_UNLIT) || defined(LANDSCAPE_WEIGHTMAP)
	Output.mNormal.w = -1.0f;
#elif defined(LIGHTING_MODEL_GOURAUD)
	Output.mAmbient.rgb = Input.mDiffuse;
	Output.mEmission.rgb = Surface.mEmissive + Input.mSpecular * Surface.mSpecularTint;
#elif !defined(WORLD_NORMAL)
	Output.mSpecular.rgb = Surface.mSpecularTint * Surface.mSpecularStrength;
	Output.mEmission.rgb = Surface.mEmissive;
	Output.mAmbient.rgb = AmbientColor.rgb;

#if defined(MACAW_RECEIVE_SHADOWS)
	Output.mSpecular.w = 1.0f;
#endif
#endif

	return Output;
}
#else
float4 PixelMain(FPixelInput Input, bool IsFrontFace : SV_IsFrontFace) : SV_TARGET {
	const FLandscapeSurface Surface = {EvaluateLandscapeSurface(Input, IsFrontFace)};

#if defined(WORLD_NORMAL)
	return float4(Surface.mNormal * 0.5f + 0.5f, 1.0f);
#elif defined(MACAW_UNLIT) || defined(LANDSCAPE_WEIGHTMAP)
	return Surface.mColor;
#else
#if defined(LIGHTING_MODEL_GOURAUD)
	const FLighting Total = {Input.mDiffuse, Input.mSpecular, Input.mAmbient};
#else
	const FLighting Total = {AccumulatePixelLighting(Input.mPosition.xy, Input.mWorldPosition, Surface.mNormal, AmbientColor.rgb, float2(Surface.mSpecularStrength, Surface.mShininess), true)};
#endif

	const float3 Color = {Surface.mColor.rgb * Total.mDiffuse + Total.mSpecular * Surface.mSpecularTint + Surface.mEmissive};

	return float4(Color, Surface.mColor.a);
#endif
}
#endif
