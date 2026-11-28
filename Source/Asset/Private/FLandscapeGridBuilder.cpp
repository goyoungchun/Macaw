#include "Asset/FLandscapeGridBuilder.h"
#include "Asset/FMeshData.h"
#include "Asset/FSubMesh.h"
#include "Asset/UStaticMesh.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector2.h"
#include "Core/Math/FVector4.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <utility>

namespace Asset {

	bool FLandscapeGridBuilder::IsValidSubsectionSizeQuads(Uint32 SubsectionSizeQuads) {
		if (SubsectionSizeQuads < 7u || SubsectionSizeQuads > 255u) {
			return false;
		}

		const Uint32 VertexCount{SubsectionSizeQuads + 1u};

		return (VertexCount & (VertexCount - 1u)) == 0u;
	}

	Uint32 FLandscapeGridBuilder::ComputeMaxLOD(Uint32 SubsectionSizeQuads) {
		if (!IsValidSubsectionSizeQuads(SubsectionSizeQuads)) {
			return 0u;
		}

		Uint32 VertexCount{SubsectionSizeQuads + 1u};
		Uint32 MaxLOD{};

		while (VertexCount > 2u) {
			VertexCount >>= 1u;
			++MaxLOD;
		}

		return MaxLOD;
	}

	bool FLandscapeGridBuilder::BuildSharedSubsectionGrid(Uint32 SubsectionSizeQuads, Asset::FLandscapeSharedGrid& OutGrid) {
		OutGrid = {};

		if (!IsValidSubsectionSizeQuads(SubsectionSizeQuads)) {
			return false;
		}

		const Uint32 MaxLOD{ComputeMaxLOD(SubsectionSizeQuads)};
		Asset::FMeshData MeshData{};
		Core::TArray<FVector>& Positions{MeshData.GetPositions()};
		Core::TArray<FVector>& Normals{MeshData.GetNormals()};
		Core::TArray<FVector4>& Tangents{MeshData.GetTangents()};
		Core::TArray<FVector4>& Colors{MeshData.GetColors()};
		Core::TArray<Uint32>& Indices{MeshData.GetIndices()};

		MeshData.GetUVChannels().resize(1u);
		Core::TArray<FVector2>& UVs{MeshData.GetUVChannels().front()};

		OutGrid.mLODRanges.reserve(static_cast<Index_t>(MaxLOD) + 1u);

		// 각 LOD별 정규화 격자 정점과 공통 대각선 인덱스를 순서대로 패킹
		for (Uint32 LOD{}; LOD <= MaxLOD; ++LOD) {
			const Uint32 VerticesPerSide{(SubsectionSizeQuads + 1u) >> LOD};
			const Uint32 QuadsPerSide{VerticesPerSide - 1u};
			const float InvQuads{1.0f / static_cast<float>(QuadsPerSide)};
			const Uint32 BaseVertex{static_cast<Uint32>(Positions.size())};
			const Uint32 FirstIndex{static_cast<Uint32>(Indices.size())};

			for (Uint32 Y{}; Y < VerticesPerSide; ++Y) {
				const float V{static_cast<float>(Y) * InvQuads};

				for (Uint32 X{}; X < VerticesPerSide; ++X) {
					const float U{static_cast<float>(X) * InvQuads};

					Positions.emplace_back(U, V, 0.0f);
					Normals.emplace_back(0.0f, 0.0f, 1.0f);
					Tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
					Colors.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
					UVs.emplace_back(U, V);
				}
			}

			for (Uint32 Y{}; Y < QuadsPerSide; ++Y) {
				for (Uint32 X{}; X < QuadsPerSide; ++X) {
					const Uint32 V00{BaseVertex + Y * VerticesPerSide + X};
					const Uint32 V10{V00 + 1u};
					const Uint32 V01{V00 + VerticesPerSide};
					const Uint32 V11{V01 + 1u};

					Indices.push_back(V00);
					Indices.push_back(V10);
					Indices.push_back(V11);

					Indices.push_back(V00);
					Indices.push_back(V11);
					Indices.push_back(V01);
				}
			}

			const Uint32 IndexCount{static_cast<Uint32>(Indices.size()) - FirstIndex};
			OutGrid.mLODRanges.push_back({FirstIndex, IndexCount, QuadsPerSide, VerticesPerSide});
		}

		MeshData.GetSubMeshes().emplace_back(0u, static_cast<Uint32>(Indices.size()), 0u);
		MeshData.SetBounds({{0.0f, 0.0f, -0.01f}, {1.0f, 1.0f, 0.01f}});

		std::unique_ptr<Asset::UStaticMesh> Mesh{CoreUObject::NewObject<Asset::UStaticMesh>()};

		if (Mesh == nullptr || !Mesh->SetMeshData(std::move(MeshData))) {
			return false;
		}

		Mesh->SetName("LandscapeSharedGrid");
		OutGrid.mMesh = std::move(Mesh);
		OutGrid.mSubsectionSizeQuads = SubsectionSizeQuads;

		return true;
	}

}
