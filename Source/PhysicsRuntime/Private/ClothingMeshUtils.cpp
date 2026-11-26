/**
 * @file ClothingMeshUtils.cpp
 * @brief 렌더 메시와 천 메시 사이의 정점 대응을 만드는 함수를 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/ClothingMeshUtils.h"
#include "PhysicsRuntime/FClothPhysicalMeshData.h"
#include "Core/Log/Log.h"

#include <cmath>
#include <limits>

namespace PhysicsRuntime {

    /**
     * @brief 정점 대응 로그 범주와 내부 계산 함수입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogClothingMeshUtils, Log, All);

        /**
         * @brief 넓이가 없는 삼각형으로 판단하는 외적 제곱 길이(m⁴)의 기준입니다.
         * @details 센티미터 좌표에서 쓰던 1e-8을 미터 좌표로 바꾼 값입니다.
         */
        constexpr float DegenerateCrossSquaredThreshold{1.e-16f};

        /**
         * @brief 삼각형 위에서 점과 가장 가까운 점을 구합니다.
         * @param[in] Point 기준 점입니다.
         * @param[in] A 삼각형의 첫 꼭짓점입니다.
         * @param[in] B 삼각형의 둘째 꼭짓점입니다.
         * @param[in] C 삼각형의 셋째 꼭짓점입니다.
         * @return 삼각형 안이나 변 위의 가장 가까운 점입니다.
         */
        FVector ClosestPointOnTriangleToPoint(const FVector& Point, const FVector& A, const FVector& B, const FVector& C) {
            const FVector AB{B - A};
            const FVector AC{C - A};
            const FVector AP{Point - A};
            const float D1{AB.Dot(AP)};
            const float D2{AC.Dot(AP)};

            // 점이 A 꼭짓점 바깥 영역에 있습니다.
            if (D1 <= 0.0f && D2 <= 0.0f) {
                return A;
            }

            const FVector BP{Point - B};
            const float D3{AB.Dot(BP)};
            const float D4{AC.Dot(BP)};

            // 점이 B 꼭짓점 바깥 영역에 있습니다.
            if (D3 >= 0.0f && D4 <= D3) {
                return B;
            }

            const float VC{D1 * D4 - D3 * D2};

            // 점이 AB 변 바깥 영역에 있습니다.
            if (VC <= 0.0f && D1 >= 0.0f && D3 <= 0.0f) {
                return A + AB * (D1 / (D1 - D3));
            }

            const FVector CP{Point - C};
            const float D5{AB.Dot(CP)};
            const float D6{AC.Dot(CP)};

            // 점이 C 꼭짓점 바깥 영역에 있습니다.
            if (D6 >= 0.0f && D5 <= D6) {
                return C;
            }

            const float VB{D5 * D2 - D1 * D6};

            // 점이 AC 변 바깥 영역에 있습니다.
            if (VB <= 0.0f && D2 >= 0.0f && D6 <= 0.0f) {
                return A + AC * (D2 / (D2 - D6));
            }

            const float VA{D3 * D6 - D5 * D4};

            // 점이 BC 변 바깥 영역에 있습니다.
            if (VA <= 0.0f && D4 - D3 >= 0.0f && D5 - D6 >= 0.0f) {
                return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
            }

            // 점이 삼각형 안쪽 영역에 있습니다.
            const float Denominator{1.0f / (VA + VB + VC)};

            return A + AB * (VB * Denominator) + AC * (VC * Denominator);
        }

        /**
         * @brief 점과 가장 가까운 삼각형을 찾습니다.
         * @param[in] Mesh 찾을 메시입니다.
         * @param[in] Position 기준 점입니다.
         * @return 그 삼각형의 첫 인덱스 위치이며 삼각형이 없으면 InvalidIndex입니다.
         */
        Index_t GetBestTriangleBaseIndex(const ClothingMeshUtils::ClothMeshDesc& Mesh, const FVector& Position) {
            float MinimumDistanceSquared{std::numeric_limits<float>::max()};
            Index_t ClosestBaseIndex{InvalidIndex};

            for (Index_t TriangleBaseIndex{}; TriangleBaseIndex + 2 < Mesh.mIndices.size(); TriangleBaseIndex += 3) {
                const FVector& A{Mesh.mPositions[Mesh.mIndices[TriangleBaseIndex]]};
                const FVector& B{Mesh.mPositions[Mesh.mIndices[TriangleBaseIndex + 1]]};
                const FVector& C{Mesh.mPositions[Mesh.mIndices[TriangleBaseIndex + 2]]};
                const float DistanceSquared{(ClosestPointOnTriangleToPoint(Position, A, B, C) - Position).LengthSquared()};

                if (DistanceSquared < MinimumDistanceSquared) {
                    MinimumDistanceSquared = DistanceSquared;
                    ClosestBaseIndex = TriangleBaseIndex;
                }
            }

            return ClosestBaseIndex;
        }

        /**
         * @brief 삼각형 평면 위의 점을 무게중심 좌표로 바꿉니다.
         * @param[in] Point 삼각형 평면 위의 점입니다.
         * @param[in] A 삼각형의 첫 꼭짓점입니다.
         * @param[in] B 삼각형의 둘째 꼭짓점입니다.
         * @param[in] C 삼각형의 셋째 꼭짓점입니다.
         * @return (A 비율, B 비율, C 비율)이며 합은 1입니다.
         */
        FVector ComputeBaryCentric2D(const FVector& Point, const FVector& A, const FVector& B, const FVector& C) {
            const FVector TriangleNormal{(B - A).Cross(C - A)};
            const FVector Normal{TriangleNormal.Normalized()};
            const float InverseAreaABC{1.0f / Normal.Dot(TriangleNormal)};

            // 점과 맞은편 변이 이루는 삼각형의 넓이 비율이 그 꼭짓점의 비율입니다.
            const float AreaPBC{Normal.Dot((B - Point).Cross(C - Point))};
            const float AreaPCA{Normal.Dot((C - Point).Cross(A - Point))};
            const float BaryA{AreaPBC * InverseAreaABC};
            const float BaryB{AreaPCA * InverseAreaABC};

            return {BaryA, BaryB, 1.0f - BaryA - BaryB};
        }

        /**
         * @brief 법선에 수직인 축 하나를 고릅니다.
         * @param[in] Normal 단위 법선입니다.
         * @return 법선에 수직인 단위 벡터입니다.
         */
        FVector FindBestAxisVector(const FVector& Normal) {
            const bool ZIsLargest{std::abs(Normal.z) > std::abs(Normal.x) && std::abs(Normal.z) > std::abs(Normal.y)};
            const FVector Axis{ZIsLargest ? FVector{1.0f, 0.0f, 0.0f} : FVector{0.0f, 0.0f, 1.0f}};

            return (Axis - Normal * Axis.Dot(Normal)).Normalized();
        }
    }

    /**
     * @brief 천 입자를 현재 자세로 옮긴 뒤 기준 변환의 좌표로 바꿉니다.
     */
    void ClothingMeshUtils::SkinPhysicsMesh(const Core::TArray<Index_t>& BoneMap, const FClothPhysicalMeshData& InMesh, const FTransform& PostTransform, const Core::TArray<FMatrix>& BoneMatrices, Core::TArray<FVector>& OutPositions, Core::TArray<FVector>& OutNormals) {
        const Index_t NumVerts{InMesh.mVertices.size()};

        OutPositions.assign(NumVerts, FVector{0.0f, 0.0f, 0.0f});
        OutNormals.assign(NumVerts, FVector{0.0f, 0.0f, 0.0f});

        for (Index_t VertIndex{}; VertIndex < NumVerts; ++VertIndex) {
            const FClothVertBoneData& BoneData{InMesh.mBoneData[VertIndex]};
            const FVector& RefParticle{InMesh.mVertices[VertIndex]};
            const FVector& RefNormal{InMesh.mNormals[VertIndex]};
            FVector& OutPosition{OutPositions[VertIndex]};
            FVector& OutNormal{OutNormals[VertIndex]};

            float TotalWeight{};

            // 뼈마다 바인드 포즈 위치를 현재 자세로 옮긴 결과를 가중치만큼 더합니다.
            for (Index_t InfluenceIndex{}; InfluenceIndex < BoneData.mNumInfluences; ++InfluenceIndex) {
                const Uint32 UsedBoneIndex{BoneData.mBoneIndices[InfluenceIndex]};

                if (UsedBoneIndex >= BoneMap.size() || BoneMap[UsedBoneIndex] >= BoneMatrices.size()) {
                    continue;
                }

                const FMatrix& BoneMatrix{BoneMatrices[BoneMap[UsedBoneIndex]]};
                const float Weight{BoneData.mBoneWeights[InfluenceIndex]};

                OutPosition += BoneMatrix.TransformPosition(RefParticle) * Weight;
                OutNormal += BoneMatrix.TransformVector(RefNormal) * Weight;
                TotalWeight += Weight;
            }

            // 뼈 영향이 없는 입자는 메시 스키닝과 같이 바인드 포즈 위치에 둡니다.
            if (TotalWeight == 0.0f) {
                OutPosition = RefParticle;
                OutNormal = RefNormal;
            }

            // 크기는 스키닝 행렬에 이미 들어 있으므로 기준 변환의 위치와 회전만 되돌립니다.
            OutPosition = PostTransform.InverseTransformPositionNoScale(OutPosition);
            OutNormal = PostTransform.InverseTransformVectorNoScale(OutNormal);

            if (OutNormal.LengthSquared() > 1.e-8f) {
                OutNormal = OutNormal.Normalized();
            }
        }
    }

    /**
     * @brief 위치와 법선 수가 같고 인덱스가 3의 배수인지 검사합니다.
     */
    bool ClothingMeshUtils::ClothMeshDesc::HasValidMesh() const {
        return mPositions.size() == mNormals.size() && mIndices.size() % 3 == 0;
    }

    /**
     * @brief 대상 메시의 정점마다 원본 메시에서 가장 가까운 삼각형 위의 위치를 구합니다.
     */
    void ClothingMeshUtils::GenerateMeshToMeshSkinningData(Core::TArray<FMeshToMeshVertData>& OutSkinningData, const ClothMeshDesc& TargetMesh, const Core::TArray<FVector>* TargetTangents, const ClothMeshDesc& SourceMesh) {
        OutSkinningData.clear();

        if (!TargetMesh.HasValidMesh()) {
            MACAW_LOG(LogClothingMeshUtils, Warning, "Failed to generate mesh to mesh skinning data. Invalid target mesh.");

            return;
        }

        if (!SourceMesh.HasValidMesh() || SourceMesh.mIndices.empty()) {
            MACAW_LOG(LogClothingMeshUtils, Warning, "Failed to generate mesh to mesh skinning data. Invalid source mesh.");

            return;
        }

        const Index_t NumTargetVerts{TargetMesh.mPositions.size()};

        if (TargetTangents != nullptr && TargetTangents->size() != NumTargetVerts) {
            MACAW_LOG(LogClothingMeshUtils, Warning, "Cannot generate mesh to mesh skinning data. The target mesh is missing tangents.");

            return;
        }

        OutSkinningData.reserve(NumTargetVerts);

        // 법선과 접선은 그 방향으로 이만큼 간 점을 천 삼각형에 붙여 두었다가 다시 구합니다.
        // 점이 삼각형 크기보다 훨씬 멀면 무게중심 좌표가 커져 천이 휠 때 법선이 크게 흔들리므로, 천 삼각형보다 작은 1cm(0.01m)를 씁니다.
        constexpr float DirectionOffset{0.01f};

        for (Index_t VertIndex{}; VertIndex < NumTargetVerts; ++VertIndex) {
            const FVector& VertPosition{TargetMesh.mPositions[VertIndex]};
            const FVector& VertNormal{TargetMesh.mNormals[VertIndex]};
            const FVector VertTangent{TargetTangents != nullptr ? (*TargetTangents)[VertIndex] : FindBestAxisVector(VertNormal)};
            const Index_t ClosestTriangleBaseIndex{GetBestTriangleBaseIndex(SourceMesh, VertPosition)};

            const Uint32 IndexA{SourceMesh.mIndices[ClosestTriangleBaseIndex]};
            const Uint32 IndexB{SourceMesh.mIndices[ClosestTriangleBaseIndex + 1]};
            const Uint32 IndexC{SourceMesh.mIndices[ClosestTriangleBaseIndex + 2]};
            const FVector& A{SourceMesh.mPositions[IndexA]};
            const FVector& B{SourceMesh.mPositions[IndexB]};
            const FVector& C{SourceMesh.mPositions[IndexC]};

            // 넓이가 없는 삼각형은 무게중심 좌표를 구할 수 없으므로 전체를 실패로 처리합니다.
            if ((B - A).Cross(C - A).LengthSquared() < DegenerateCrossSquaredThreshold) {
                OutSkinningData.clear();
                MACAW_LOG(LogClothingMeshUtils, Warning, "Failed to generate skinning data. Triangle {} has coincident vertices.", ClosestTriangleBaseIndex / 3);

                return;
            }

            FMeshToMeshVertData& SkinningData{OutSkinningData.emplace_back()};
            SkinningData.mPositionBaryCoordsAndDist = GetPointBaryAndDist(A, B, C, VertPosition);
            SkinningData.mNormalBaryCoordsAndDist = GetPointBaryAndDist(A, B, C, VertPosition + VertNormal * DirectionOffset);
            SkinningData.mTangentBaryCoordsAndDist = GetPointBaryAndDist(A, B, C, VertPosition + VertTangent * DirectionOffset);
            SkinningData.mSourceMeshVertIndices = {IndexA, IndexB, IndexC, 0};
            SkinningData.mWeight = 1.0f;
        }
    }

    /**
     * @brief 점을 삼각형 평면에 투영한 무게중심 좌표와 평면까지의 거리를 구합니다.
     */
    FVector4 ClothingMeshUtils::GetPointBaryAndDist(const FVector& A, const FVector& B, const FVector& C, const FVector& Point) {
        const FVector PlaneNormal{(B - A).Cross(C - A).Normalized()};
        const float Distance{(Point - A).Dot(PlaneNormal)};
        const FVector PointOnPlane{Point - PlaneNormal * Distance};

        return {ComputeBaryCentric2D(PointOnPlane, A, B, C), Distance};
    }
}
