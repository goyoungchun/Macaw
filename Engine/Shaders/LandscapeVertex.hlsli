#ifndef MacawLandscapeVertexIncluded
#define MacawLandscapeVertexIncluded

#ifdef MacawLandscape

// 공유 하이트맵 텍스처
Texture2D<float4> LandscapeHeightmap : register(t13);

// 서브섹션 변위 및 스티칭 상수
cbuffer LandscapeVertexConstants : register(b5) {
	// 서브섹션 원점과 월드 크기 및 높이 배율
	float4 LandscapeSubsectionParams;

	// 전체 하이트맵 해상도와 전역 서브섹션 시작 좌표
	float4 LandscapeHeightmapParams;

	// 컴포넌트 내부 서브섹션 인덱스와 역수 및 섹션 쿼드 수
	float4 LandscapeComponentParams;

	// 현재 쿼드 수와 상위 쿼드 수 및 모핑 비율
	float4 LandscapeLODParams;

	// 인접 방향 스티칭 비트마스크
	uint4 LandscapeStitchParams;
};

// 정수 텍셀 좌표에서 높이 샘플 디코딩
float DecodeLandscapeHeightSample(int2 Texel, int2 MaxTexel) {
	const int2 Clamped = {clamp(Texel, int2(0, 0), MaxTexel)};
	const float4 Sample = {LandscapeHeightmap.Load(int3(Clamped, 0))};
	const float HeightNorm = {(Sample.r * 255.0f * 256.0f + Sample.g * 255.0f) / 65535.0f};

	return (HeightNorm - 0.5f) * LandscapeSubsectionParams.w;
}

// 정수 텍셀 좌표에서 법선 샘플 디코딩
float3 DecodeLandscapeNormalSample(int2 Texel, int2 MaxTexel) {
	const int2 Clamped = {clamp(Texel, int2(0, 0), MaxTexel)};
	const float4 Sample = {LandscapeHeightmap.Load(int3(Clamped, 0))};
	const float NX = {Sample.b * 2.0f - 1.0f};
	const float NY = {Sample.a * 2.0f - 1.0f};
	const float NZ = {sqrt(max(0.0f, 1.0f - NX * NX - NY * NY))};

	return normalize(float3(NX, NY, max(NZ, 0.0001f)));
}

// 경계 스냅과 내부 지오모핑을 적용한 서브섹션 좌표 계산
float2 ComputeStitchedSubUV(float2 SubUV) {
	const float CoarseQuads = {max(LandscapeLODParams.y, 1.0f)};
	const float2 Snapped = {round(SubUV * CoarseQuads) / CoarseQuads};
	const float EdgeEps = {0.0001f};
	const uint Mask = {LandscapeStitchParams.x};

	float2 Result = {SubUV};
	const bool OnMinX = {SubUV.x <= EdgeEps};
	const bool OnMaxX = {SubUV.x >= 1.0f - EdgeEps};
	const bool OnMinY = {SubUV.y <= EdgeEps};
	const bool OnMaxY = {SubUV.y >= 1.0f - EdgeEps};

	if ((Mask & 1u) != 0u && OnMinX) {
		Result.y = Snapped.y;
	}

	if ((Mask & 2u) != 0u && OnMaxX) {
		Result.y = Snapped.y;
	}

	if ((Mask & 4u) != 0u && OnMinY) {
		Result.x = Snapped.x;
	}

	if ((Mask & 8u) != 0u && OnMaxY) {
		Result.x = Snapped.x;
	}

	if (!OnMinX && !OnMaxX && !OnMinY && !OnMaxY) {
		Result = lerp(SubUV, Snapped, saturate(LandscapeLODParams.z));
	}

	return Result;
}

// 공통 대각선 규약으로 높이와 법선 샘플링
void SampleLandscapeHeightAndNormal(float2 SubUV, out float OutLocalZ, out float3 OutLocalNormal) {
	const int2 MaxTexel = {max(int2(LandscapeHeightmapParams.xy) - int2(1, 1), int2(0, 0))};
	const float2 GlobalCoord = {LandscapeHeightmapParams.zw + SubUV * LandscapeComponentParams.w};
	const int2 Cell = {int2(floor(GlobalCoord))};
	const float2 Frac = {GlobalCoord - float2(Cell)};

	const float Z00 = {DecodeLandscapeHeightSample(Cell, MaxTexel)};
	const float Z10 = {DecodeLandscapeHeightSample(Cell + int2(1, 0), MaxTexel)};
	const float Z01 = {DecodeLandscapeHeightSample(Cell + int2(0, 1), MaxTexel)};
	const float Z11 = {DecodeLandscapeHeightSample(Cell + int2(1, 1), MaxTexel)};

	if (Frac.x >= Frac.y) {
		OutLocalZ = Z00 + (Z10 - Z00) * Frac.x + (Z11 - Z10) * Frac.y;
	} else {
		OutLocalZ = Z00 + (Z11 - Z01) * Frac.x + (Z01 - Z00) * Frac.y;
	}

	const float3 N00 = {DecodeLandscapeNormalSample(Cell, MaxTexel)};
	const float3 N10 = {DecodeLandscapeNormalSample(Cell + int2(1, 0), MaxTexel)};
	const float3 N01 = {DecodeLandscapeNormalSample(Cell + int2(0, 1), MaxTexel)};
	const float3 N11 = {DecodeLandscapeNormalSample(Cell + int2(1, 1), MaxTexel)};

	const float3 Bottom = {lerp(N00, N10, Frac.x)};
	const float3 Top = {lerp(N01, N11, Frac.x)};
	OutLocalNormal = normalize(lerp(Bottom, Top, Frac.y));
}

// 그림자 깊이 패스용 정점 위치 변위
float3 DisplaceLandscapePosition(float3 InputPosition) {
	const float2 SubUV = {ComputeStitchedSubUV(InputPosition.xy)};
	float LocalZ = {0.0f};
	float3 UnusedNormal = {float3(0.0f, 0.0f, 1.0f)};
	SampleLandscapeHeightAndNormal(SubUV, LocalZ, UnusedNormal);

	return float3(
		LandscapeSubsectionParams.x + SubUV.x * LandscapeSubsectionParams.z,
		LandscapeSubsectionParams.y + SubUV.y * LandscapeSubsectionParams.z,
		LocalZ);
}

// 베이스 패스용 정점 위치와 법선 및 접선 변위
void DisplaceLandscapeVertex(inout float3 Position, inout float3 Normal, inout float4 Tangent, inout float2 UV) {
	const float2 SubUV = {ComputeStitchedSubUV(Position.xy)};
	float LocalZ = {0.0f};
	float3 LocalNormal = {float3(0.0f, 0.0f, 1.0f)};
	SampleLandscapeHeightAndNormal(SubUV, LocalZ, LocalNormal);

	Position = float3(
		LandscapeSubsectionParams.x + SubUV.x * LandscapeSubsectionParams.z,
		LandscapeSubsectionParams.y + SubUV.y * LandscapeSubsectionParams.z,
		LocalZ);

	Normal = LocalNormal;

	const float3 RawTangent = {float3(1.0f, 0.0f, 0.0f) - LocalNormal * LocalNormal.x};
	const float TangentLenSq = {max(dot(RawTangent, RawTangent), 0.000001f)};
	Tangent = float4(RawTangent * rsqrt(TangentLenSq), 1.0f);

	// 컴포넌트 내부 웨이트맵 샘플링용 좌표
	UV = (LandscapeComponentParams.xy + SubUV) * LandscapeComponentParams.z;
}

#endif
#endif
