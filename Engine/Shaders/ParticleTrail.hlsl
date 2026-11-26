/**
 * @file ParticleTrail.hlsl
 * @brief 빔·리본 파티클 띠를 월드 공간 삼각형 목록으로 그립니다.
 */

#pragma pack_matrix(row_major)

/**
 * @brief ParticleSprite.hlsl과 같은 배치의 상수입니다. 띠는 카메라 축과 SubUV를 쓰지 않습니다.
 */
cbuffer ParticleConstants : register(b0) {
    float4x4 ViewProjection;
    float4 CameraRight;
    float4 CameraUp;
    float4 SubImageSize;

    /**
     * @brief 머티리얼 기본 색이며 띠 색에 곱합니다.
     */
    float4 Tint;
};

/**
 * @brief Renderer::FParticleRenderer::FTrailVertex와 같은 48바이트 배치입니다.
 */
struct FParticleTrailVertex {
    float3 mPosition;
    float mU;
    float4 mColor;
    float mV;
    float3 mPadding;
};

StructuredBuffer<FParticleTrailVertex> Vertices : register(t1);

Texture2D TrailTexture : register(t0);

SamplerState TrailSampler : register(s0);

struct FOutput {
    float4 mPosition : SV_POSITION;
    float2 mUV : TEXCOORD0;
    float4 mColor : COLOR0;
};

/**
 * @brief 렌더러가 펼친 정점을 정점 번호로 읽어 투영합니다.
 */
FOutput VertexMain(uint VertexIndex : SV_VertexID) {
    const FParticleTrailVertex Vertex = Vertices[VertexIndex];
    FOutput Output = (FOutput)0;

    Output.mPosition = mul(float4(Vertex.mPosition, 1.0f), ViewProjection);
    Output.mUV = float2(Vertex.mU, Vertex.mV);
    Output.mColor = Vertex.mColor * Tint;

    return Output;
}

/**
 * @brief 띠 텍스처 색에 띠 색을 곱합니다.
 */
float4 PixelMain(FOutput Input) : SV_TARGET {
    const float4 Surface = TrailTexture.Sample(TrailSampler, Input.mUV) * Input.mColor;

    clip(Surface.a - 0.001f);

    return Surface;
}
