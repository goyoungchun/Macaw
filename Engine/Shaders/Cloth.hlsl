/**
 * @file Cloth.hlsl
 * @brief 베이스 패스와 그림자 패스가 공유하는 천 섹션 정점 이동입니다.
 */
#ifndef MacawClothIncluded
#define MacawClothIncluded

#ifdef MacawCloth
/**
 * @brief 정점 하나가 붙어 있는 천 입자 삼각형 정보이며 USkeletalMesh의 FClothVertexGpu와 같아야 합니다.
 */
struct FClothVertex {
    /**
     * @brief 위치의 무게중심 좌표 세 개와 삼각형 면에서 법선 방향으로 떨어진 거리입니다.
     */
    float4 mPositionBaryCoordsAndDist;
    /**
     * @brief 위치에서 법선 방향으로 간 점의 무게중심 좌표와 거리입니다.
     */
    float4 mNormalBaryCoordsAndDist;
    /**
     * @brief 위치에서 접선 방향으로 간 점의 무게중심 좌표와 거리입니다.
     */
    float4 mTangentBaryCoordsAndDist;
    /**
     * @brief 앞 세 개는 입자 번호이고 마지막이 0xFFFF이면 스키닝만 쓰는 정점입니다.
     */
    uint4 mSourceMeshVertIndices;
    /**
     * @brief 여러 삼각형의 영향을 섞을 때 쓰는 가중치입니다.
     */
    float mWeight;
    /**
     * @brief 16바이트 정렬을 맞추는 빈 칸입니다.
     */
    float3 mPadding;
};

/**
 * @brief 천 입자 하나의 기준 뼈 좌표 위치와 법선이며 FClothParticleGpu와 같아야 합니다.
 */
struct FClothParticle {
    /**
     * @brief 기준 뼈 좌표의 입자 위치입니다.
     */
    float3 mPosition;
    /**
     * @brief 기준 뼈 좌표의 입자 법선입니다.
     */
    float3 mNormal;
};

/**
 * @brief 메시 정점 순서의 천 정점 정보입니다.
 */
StructuredBuffer<FClothVertex> ClothVertices : register(t11);

/**
 * @brief 이번 프레임 천 시뮬레이션의 입자 위치와 법선입니다.
 */
StructuredBuffer<FClothParticle> ClothParticles : register(t12);

/**
 * @brief 입자 좌표를 컴포넌트 좌표로 바꾸는 행렬과 섞는 비율이며 FClothConstants와 같아야 합니다.
 */
cbuffer ClothConstants : register(b5) {
    /**
     * @brief 기준 뼈 좌표를 컴포넌트 좌표로 바꾸는 행렬입니다.
     */
    row_major float4x4 ClothLocalToComponent;
    /**
     * @brief 스키닝 위치와 시뮬레이션 위치를 섞는 비율입니다.
     */
    float ClothBlendWeight;
    /**
     * @brief 16바이트 정렬을 맞추는 빈 칸입니다.
     */
    float3 ClothPadding;
};

/**
 * @brief 입자 삼각형을 따르지 않고 스키닝만 쓰는 정점의 표시 값입니다.
 */
static const uint ClothFixedVertexIndex = 0xFFFF;

/**
 * @brief 정점이 천 입자 삼각형을 따라 움직이는지 검사합니다.
 * @param Vertex 검사할 천 정점 정보입니다.
 * @return 입자 삼각형을 따르면 true입니다.
 */
bool IsSimulatedVertex(FClothVertex Vertex) {
    return Vertex.mSourceMeshVertIndices.w < ClothFixedVertexIndex;
}

/**
 * @brief 입자 위치를 컴포넌트 좌표로 읽습니다.
 * @param Index 입자 번호입니다.
 * @return 컴포넌트 좌표의 입자 위치입니다.
 */
float3 GetClothSimulPosition(uint Index) {
    return mul(float4(ClothParticles[Index].mPosition, 1.0f), ClothLocalToComponent).xyz;
}

/**
 * @brief 입자 법선을 컴포넌트 좌표로 읽습니다.
 * @param Index 입자 번호입니다.
 * @return 컴포넌트 좌표의 입자 법선입니다.
 */
float3 GetClothSimulNormal(uint Index) {
    return mul(float4(ClothParticles[Index].mNormal, 0.0f), ClothLocalToComponent).xyz;
}

/**
 * @brief 입자 삼각형 위의 무게중심 좌표와 면에서 떨어진 거리로 점 하나의 위치를 구합니다.
 * @param Vertex 입자 번호를 가진 천 정점 정보입니다.
 * @param BaryCoordsAndDist 무게중심 좌표 세 개와 거리입니다.
 * @return 컴포넌트 좌표의 위치입니다.
 */
float3 InterpolateClothPoint(FClothVertex Vertex, float4 BaryCoordsAndDist) {
    const float3 A = {GetClothSimulPosition(Vertex.mSourceMeshVertIndices.x)};
    const float3 B = {GetClothSimulPosition(Vertex.mSourceMeshVertIndices.y)};
    const float3 C = {GetClothSimulPosition(Vertex.mSourceMeshVertIndices.z)};
    const float3 NA = {GetClothSimulNormal(Vertex.mSourceMeshVertIndices.x)};
    const float3 NB = {GetClothSimulNormal(Vertex.mSourceMeshVertIndices.y)};
    const float3 NC = {GetClothSimulNormal(Vertex.mSourceMeshVertIndices.z)};

    // 각 꼭짓점을 법선 방향으로 거리만큼 띄운 뒤 무게중심 좌표로 섞습니다.
    return BaryCoordsAndDist.x * (A + NA * BaryCoordsAndDist.w)
         + BaryCoordsAndDist.y * (B + NB * BaryCoordsAndDist.w)
         + BaryCoordsAndDist.z * (C + NC * BaryCoordsAndDist.w);
}

/**
 * @brief 정점이 붙어 있는 입자 삼각형으로 시뮬레이션 위치를 구합니다.
 * @param Vertex 천 정점 정보입니다.
 * @return 컴포넌트 좌표의 시뮬레이션 위치입니다.
 */
float3 ClothingPosition(FClothVertex Vertex) {
    // 위치는 세 번째 무게중심 좌표를 나머지 둘로 다시 구해 합이 정확히 1이 되게 합니다.
    const float4 BaryCoordsAndDist = {Vertex.mPositionBaryCoordsAndDist.x, Vertex.mPositionBaryCoordsAndDist.y, 1.0f - Vertex.mPositionBaryCoordsAndDist.x - Vertex.mPositionBaryCoordsAndDist.y, Vertex.mPositionBaryCoordsAndDist.w};

    return InterpolateClothPoint(Vertex, BaryCoordsAndDist);
}

/**
 * @brief 스키닝한 위치만 천 시뮬레이션 위치와 섞습니다.
 * @param VertexId 메시 정점 번호입니다.
 * @param Position 스키닝한 컴포넌트 좌표 위치입니다.
 * @return 섞은 컴포넌트 좌표 위치입니다.
 */
float3 BlendClothPosition(uint VertexId, float3 Position) {
    const FClothVertex Vertex = ClothVertices[VertexId];

    if (!IsSimulatedVertex(Vertex)) {
        return Position;
    }

    return lerp(Position, ClothingPosition(Vertex), ClothBlendWeight);
}

/**
 * @brief 스키닝한 위치, 법선, 접선을 천 시뮬레이션 결과와 섞습니다.
 * @param VertexId 메시 정점 번호입니다.
 * @param Position 입출력 컴포넌트 좌표 위치입니다.
 * @param Normal 입출력 컴포넌트 좌표 법선입니다.
 * @param Tangent 입출력 컴포넌트 좌표 접선과 부호입니다.
 */
void BlendClothVertex(uint VertexId, inout float3 Position, inout float3 Normal, inout float4 Tangent) {
    const FClothVertex Vertex = ClothVertices[VertexId];

    if (!IsSimulatedVertex(Vertex)) {
        return;
    }

    // 법선과 접선은 그 방향으로 간 점을 같은 입자 삼각형으로 옮긴 뒤 위치와의 차로 다시 구합니다.
    const float3 SimulatedPosition = {ClothingPosition(Vertex)};
    const float3 NormalPosition = {InterpolateClothPoint(Vertex, Vertex.mNormalBaryCoordsAndDist)};
    const float3 TangentPosition = {InterpolateClothPoint(Vertex, Vertex.mTangentBaryCoordsAndDist)};
    const float3 SimulatedNormal = {normalize(NormalPosition - SimulatedPosition)};
    const float3 SimulatedTangent = {normalize(TangentPosition - SimulatedPosition)};

    Position = lerp(Position, SimulatedPosition, ClothBlendWeight);
    Normal = lerp(Normal, SimulatedNormal, ClothBlendWeight);
    Tangent.xyz = lerp(Tangent.xyz, SimulatedTangent, ClothBlendWeight);
}
#endif
#endif
