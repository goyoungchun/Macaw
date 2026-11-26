/**
 * @file ClothingMeshUtils.h
 * @brief 렌더 메시와 천 메시 사이의 정점 대응을 만드는 함수를 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FTransform.h"
#include "Core/Math/FVector4.h"
#include "PhysicsRuntime/FMeshToMeshVertData.h"

#include <span>

namespace PhysicsRuntime {
    struct FClothPhysicalMeshData;

    /**
     * @brief 천 메시를 다루는 함수 모음입니다.
     * @ingroup PhysicsRuntime
     */
    namespace ClothingMeshUtils {

        /**
         * @brief 위치, 법선, 삼각형 인덱스를 복사하지 않고 가리키는 메시 설명입니다.
         * @ingroup PhysicsRuntime
         */
        struct ClothMeshDesc {
            /**
             * @brief 세 배열이 서로 맞는지 검사합니다.
             * @return 위치와 법선 수가 같고 인덱스가 3의 배수이면 true입니다.
             */
            bool HasValidMesh() const;

            /**
             * @brief 정점 위치입니다.
             */
            std::span<const FVector> mPositions{};

            /**
             * @brief 정점 법선입니다.
             */
            std::span<const FVector> mNormals{};

            /**
             * @brief 삼각형마다 정점 번호 세 개를 나열한 배열입니다.
             */
            std::span<const Uint32> mIndices{};
        };

        /**
         * @brief 천 입자를 현재 자세로 옮긴 뒤 기준 변환의 좌표로 바꿉니다.
         * @details 입자마다 뼈 영향의 스키닝 행렬을 가중치로 섞어 컴포넌트 좌표를 구하고,
         *          크기를 뺀 기준 변환의 역변환을 적용합니다.
         * @param[in] BoneMap 천 에셋의 사용 뼈 번호마다 찾은 스켈레톤 뼈 번호입니다.
         * @param[in] InMesh 바인드 포즈의 천 메시입니다.
         * @param[in] PostTransform 결과 좌표의 기준이 되는 컴포넌트 좌표의 변환입니다.
         * @param[in] BoneMatrices 스켈레톤 뼈 순서의 스키닝 행렬(역바인드 * 컴포넌트 좌표 뼈 변환)입니다.
         * @param[out] OutPositions 입자 순서의 위치이며 기존 내용은 지웁니다.
         * @param[out] OutNormals 입자 순서의 단위 법선이며 기존 내용은 지웁니다.
         */
        void SkinPhysicsMesh(const Core::TArray<Index_t>& BoneMap, const FClothPhysicalMeshData& InMesh, const FTransform& PostTransform, const Core::TArray<FMatrix>& BoneMatrices, Core::TArray<FVector>& OutPositions, Core::TArray<FVector>& OutNormals);

        /**
         * @brief 대상 메시의 정점마다 원본 메시에서 가장 가까운 삼각형 위의 위치를 구합니다.
         * @details 대상 정점의 위치, 법선 끝점, 탄젠트 끝점을 각각 그 삼각형의 무게중심 좌표와 평면 거리로 저장합니다.
         *          원본 메시에 넓이가 없는 삼각형이 있으면 결과를 비웁니다.
         * @param[out] OutSkinningData 대상 정점 순서의 결과이며 실패하면 비어 있습니다.
         * @param[in] TargetMesh 원본 메시에 붙일 메시입니다.
         * @param[in] TargetTangents 대상 정점의 탄젠트이며 nullptr이면 법선에 수직인 축을 고릅니다.
         * @param[in] SourceMesh 대상 정점을 붙일 메시입니다.
         */
        void GenerateMeshToMeshSkinningData(Core::TArray<FMeshToMeshVertData>& OutSkinningData, const ClothMeshDesc& TargetMesh, const Core::TArray<FVector>* TargetTangents, const ClothMeshDesc& SourceMesh);

        /**
         * @brief 점을 삼각형 평면에 투영한 무게중심 좌표와 평면까지의 거리를 구합니다.
         * @param[in] A 삼각형의 첫 꼭짓점입니다.
         * @param[in] B 삼각형의 둘째 꼭짓점입니다.
         * @param[in] C 삼각형의 셋째 꼭짓점입니다.
         * @param[in] Point 표현할 점입니다.
         * @return (A 비율, B 비율, C 비율, 법선 방향 거리)입니다.
         */
        FVector4 GetPointBaryAndDist(const FVector& A, const FVector& B, const FVector& C, const FVector& Point);
    }
}
