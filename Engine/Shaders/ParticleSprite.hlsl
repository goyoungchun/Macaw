/**
 * @file ParticleSprite.hlsl
 * @brief 스프라이트 파티클을 카메라를 향한 인스턴스 사각형으로 그립니다.
 */

#pragma pack_matrix(row_major)

/**
 * @brief 뷰와 에미터 묶음 하나에 공통인 상수입니다.
 */
cbuffer ParticleConstants : register(b0) {
    float4x4 ViewProjection;

    /**
     * @brief 월드 공간 카메라 오른쪽 방향입니다.
     */
    float4 CameraRight;

    /**
     * @brief 월드 공간 카메라 위쪽 방향입니다.
     */
    float4 CameraUp;

    /**
     * @brief SubUV 가로 칸 수, 세로 칸 수, 1/가로, 1/세로입니다.
     */
    float4 SubImageSize;

    /**
     * @brief 머티리얼 기본 색이며 파티클 색에 곱합니다.
     */
    float4 Tint;
};

/**
 * @brief Engine::FParticleSpriteInstance와 같은 48바이트 배치입니다.
 */
struct FParticleSpriteInstance {
    float3 mPosition;
    float mRotation;
    float2 mSize;
    float mSubImageIndex;
    uint mCounter;
    float4 mColor;
};

StructuredBuffer<FParticleSpriteInstance> Instances : register(t1);

Texture2D SpriteTexture : register(t0);

SamplerState SpriteSampler : register(s0);

struct FOutput {
    float4 mPosition : SV_POSITION;

    /**
     * @brief 현재 SubUV 칸의 텍스처 좌표입니다.
     */
    float2 mUVA : TEXCOORD0;

    /**
     * @brief 다음 SubUV 칸의 텍스처 좌표입니다.
     */
    float2 mUVB : TEXCOORD1;

    /**
     * @brief 두 칸을 섞는 비율입니다.
     */
    float mBlend : TEXCOORD2;

    float4 mColor : COLOR0;
};

/**
 * @brief 칸 번호와 칸 안의 좌표(0~1)로 아틀라스 텍스처 좌표를 구합니다.
 */
float2 CellUV(float Index, float2 Corner) {
    const float2 Cell = float2(fmod(Index, SubImageSize.x), floor(Index / SubImageSize.x));

    return (Cell + Corner) * SubImageSize.zw;
}

/**
 * @brief 정점 번호로 사각형 꼭짓점을 만들고 파티클 회전·크기를 적용합니다.
 */
FOutput VertexMain(uint Vertex : SV_VertexID, uint InstanceIndex : SV_InstanceID) {
    const float2 Corners[6] = {float2(-0.5f, 0.5f), float2(-0.5f, -0.5f), float2(0.5f, 0.5f), float2(0.5f, 0.5f), float2(-0.5f, -0.5f), float2(0.5f, -0.5f)};
    const FParticleSpriteInstance Instance = Instances[InstanceIndex];
    const float2 Offset = Corners[Vertex] * Instance.mSize;
    float Sine;
    float Cosine;

    sincos(Instance.mRotation, Sine, Cosine);

    const float3 Right = CameraRight.xyz * Cosine + CameraUp.xyz * Sine;
    const float3 Up = CameraUp.xyz * Cosine - CameraRight.xyz * Sine;
    const float3 World = Instance.mPosition + Right * Offset.x + Up * Offset.y;
    // 텍스처 V는 아래로 증가하므로 위쪽 꼭짓점이 V = 0입니다.
    const float2 Corner = Corners[Vertex] * float2(1.0f, -1.0f) + 0.5f;
    const float Total = SubImageSize.x * SubImageSize.y;
    const float IndexA = floor(Instance.mSubImageIndex);
    const float IndexB = min(IndexA + 1.0f, Total - 1.0f);
    FOutput Output = (FOutput)0;

    Output.mPosition = mul(float4(World, 1.0f), ViewProjection);
    Output.mUVA = CellUV(IndexA, Corner);
    Output.mUVB = CellUV(IndexB, Corner);
    Output.mBlend = frac(Instance.mSubImageIndex);
    Output.mColor = Instance.mColor * Tint;

    return Output;
}

/**
 * @brief SubUV 칸을 섞은 텍스처 색에 파티클 색을 곱합니다.
 */
float4 PixelMain(FOutput Input) : SV_TARGET {
    float4 Texel = SpriteTexture.Sample(SpriteSampler, Input.mUVA);

    if (Input.mBlend > 0.0f) {
        Texel = lerp(Texel, SpriteTexture.Sample(SpriteSampler, Input.mUVB), Input.mBlend);
    }

    const float4 Surface = Texel * Input.mColor;

    clip(Surface.a - 0.001f);

    return Surface;
}
