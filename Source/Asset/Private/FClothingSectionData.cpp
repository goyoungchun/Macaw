/**
 * @file FClothingSectionData.cpp
 * @brief 스켈레탈 메시의 섹션 하나가 천 에셋을 따르기 위한 연결 데이터를 구현합니다.
 * @ingroup Asset
 */

#include "Asset/FClothingSectionData.h"
#include "Core/Archive.h"

namespace Asset {

    /**
     * @brief 천 에셋과 상세 단계가 지정되어 있는지 검사합니다.
     */
    bool FClothingSectionData::IsValid() const {
        return mAssetGuid.IsValid() && mAssetLodIndex != InvalidIndex;
    }

    /**
     * @brief 연결 데이터를 저장하거나 불러옵니다.
     */
    void FClothingSectionData::Serialize(Core::FArchive& Archive) {
        constexpr Uint32 MaxSerializedCount{50000000u};

        // 천이 없는 섹션은 빈 식별자를 가집니다. 식별자 읽기는 빈 값을 오류로 보므로 문자열로 직접 읽고 씁니다.
        Core::FString GuidText{mAssetGuid.ToString()};
        Archive.Serialize("AssetGuid", GuidText);

        if (Archive.HasError()) {
            return;
        }

        if (Archive.IsLoading()) {
            if (GuidText == Core::FGuid::Null().ToString()) {
                mAssetGuid = Core::FGuid::Null();
            } else if (!Core::FGuid::Parse(GuidText, mAssetGuid)) {
                Archive.SetError("Invalid clothing section asset guid");

                return;
            }
        }

        Uint64 AssetLodIndex{static_cast<Uint64>(mAssetLodIndex)};
        Uint64 CorrespondClothAssetIndex{static_cast<Uint64>(mCorrespondClothAssetIndex)};
        Archive.Serialize("AssetLodIndex", AssetLodIndex);
        Archive.Serialize("CorrespondClothAssetIndex", CorrespondClothAssetIndex);

        if (Archive.IsLoading()) {
            mAssetLodIndex = static_cast<Index_t>(AssetLodIndex);
            mCorrespondClothAssetIndex = static_cast<Index_t>(CorrespondClothAssetIndex);
        }

        Uint32 NumMappings{Archive.IsLoading() ? 0u : static_cast<Uint32>(mClothMappingData.size())};

        if (Archive.HasError() || !Archive.BeginArray("ClothMappingData", NumMappings) || NumMappings > MaxSerializedCount) {
            Archive.SetError("Invalid cloth mapping count");

            return;
        }

        if (Archive.IsLoading()) {
            mClothMappingData.assign(NumMappings, PhysicsRuntime::FMeshToMeshVertData{});
        }

        for (PhysicsRuntime::FMeshToMeshVertData& Mapping : mClothMappingData) {
            PhysicsRuntime::SerializeMeshToMeshVertData(Archive, Mapping);
        }

        Archive.EndArray();

        Uint32 NumVertexIndices{Archive.IsLoading() ? 0u : static_cast<Uint32>(mSectionVertexIndices.size())};

        if (Archive.HasError() || !Archive.BeginArray("SectionVertexIndices", NumVertexIndices) || NumVertexIndices > MaxSerializedCount) {
            Archive.SetError("Invalid cloth section vertex count");

            return;
        }

        if (Archive.IsLoading()) {
            mSectionVertexIndices.assign(NumVertexIndices, 0u);
        }

        for (Uint32& VertexIndex : mSectionVertexIndices) {
            Archive.Serialize("VertexIndex", VertexIndex);
        }

        Archive.EndArray();
    }
}
