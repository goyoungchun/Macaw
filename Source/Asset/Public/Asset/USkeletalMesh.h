/**
 * @file USkeletalMesh.h
 * @brief 바인드 형상과 GPU 스킨 가중치를 공유하는 스켈레탈 메시 에셋입니다.
 * @ingroup Asset
 */

#pragma once

#include "Core/Types.h"
#include "Asset/FClothingSectionData.h"
#include "Asset/FMeshData.h"
#include "Asset/UStaticMesh.h"
#include "Asset/Import/FMeshImportSkinWeight.h"
#include "CoreUObject/UObject.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"
#include "PhysicsRuntime/UPhysicsAsset.h"
#include "RenderCore/TStructuredBuffer.h"
#include "RenderCore/TVertexBuffer.h"

#include <memory>
#include <optional>
#include <span>

#include "USkeletalMesh.generated.h"

namespace Core {
    class FArchive;
}

namespace Asset {
    class USkeleton;

    /**
     * @brief 불변 바인드 형상과 가중치를 소유하며 현재 포즈는 각 컴포넌트가 관리합니다.
     * @details 스키닝 데이터는 다음 순서로 흐릅니다.
     * 1. USkeleton이 로컬 바인드 포즈를 누적해 역바인드 행렬을 계산합니다.
     * 2. FSkeletalMeshPose가 현재 로컬 포즈를 누적해 전역 포즈를 구하고, 스키닝 행렬(역바인드 * 전역)을 계산합니다.
     * 3. USkeletalMeshComponent가 스키닝 행렬을 씬 프록시에 복사하고, FScene::UploadSkinning이 본 팔레트로 GPU에 올립니다.
     * 4. 정점 셰이더(Skinning.hlsl)가 가중치로 섞은 행렬을 적용해 정점을 현재 포즈로 옮깁니다.
     *
     * 행 벡터 규약의 최종 위치는 v' = Σ 가중치(i) * (v * 역바인드(i) * 전역(i))이며, 바인드 포즈에서는 스키닝 행렬이 항등 행렬이 되어 v' = v입니다.
     */
    UCLASS()
    class USkeletalMesh final : public CoreUObject::UObject {
        GENERATED_BODY()

    public:

        /**
         * @brief 빈 스켈레탈 메시를 생성합니다.
         */
        USkeletalMesh() = default;

        /**
         * @brief 공유 GPU 자원을 해제합니다.
         */
        ~USkeletalMesh() override = default;

    public:

        /**
         * @brief 형상과 가중치를 검증하고 저장합니다.
         * @param MeshData 바인드 형상입니다.
         * @param SkinWeights 정점별 최대 네 본의 영향입니다.
         * @return 모든 데이터가 유효해 교체했으면 true입니다.
         */
        bool SetMeshData(Asset::FMeshData MeshData, Core::TArray<Asset::FMeshImportSkinWeight> SkinWeights);

        /**
         * @brief 바인드 포즈의 CPU 형상을 반환합니다.
         * @return 공유 형상의 읽기 전용 참조입니다.
         */
        const Asset::FMeshData& GetMeshData() const;

        /**
         * @brief 정점별 스킨 가중치를 반환합니다.
         * @return 가중치 목록의 읽기 전용 참조입니다.
         */
        const Core::TArray<Asset::FMeshImportSkinWeight>& GetSkinWeights() const;

        /**
         * @brief 스키닝 가중치가 요구하는 본 행렬 수를 반환합니다.
         * @return GPU 스키닝에 필요한 행렬 개수입니다.
         */
        Uint32 GetSkinMatrixCount() const;

        /**
         * @brief 공통 메시 드로 경로가 사용할 바인드 형상을 반환합니다.
         * @return 에셋 수명 동안 주소가 유지되는 형상입니다.
         */
        Asset::UStaticMesh* GetGeometry();

        /**
         * @brief 장치에 바인드 형상과 불변 가중치 버퍼를 준비합니다.
         * @param Device 사용할 D3D11 장치입니다.
         * @return 모든 버퍼를 준비하면 true입니다.
         */
        bool CreateResources(ID3D11Device* Device);

        /**
         * @brief 형상과 가중치의 GPU 자원을 해제합니다.
         */
        void ReleaseResources();

        /**
         * @brief 본 인덱스와 가중치를 담은 정점 버퍼를 입력 슬롯에 연결합니다.
         * @param Context 바인딩할 장치 컨텍스트입니다.
         * @param Slot 입력 슬롯입니다.
         */
        void BindSkinWeights(ID3D11DeviceContext* Context, Index_t Slot) const;

        /**
         * @brief 입력 슬롯의 스킨 가중치 버퍼 연결을 해제합니다.
         * @param Context 바인딩할 장치 컨텍스트입니다.
         * @param Slot 입력 슬롯입니다.
         */
        static void UnbindSkinWeights(ID3D11DeviceContext* Context, Index_t Slot);

        /**
         * @brief 메시 정점마다 붙어 있는 천 입자 삼각형 정보를 담은 버퍼를 정점 셰이더 슬롯에 연결합니다.
         * @param Context 바인딩할 장치 컨텍스트입니다.
         * @param Slot 셰이더 리소스 슬롯(t 레지스터)입니다.
         */
        void BindClothVertices(ID3D11DeviceContext* Context, Index_t Slot) const;

        /**
         * @brief 정점 셰이더 슬롯의 천 정점 버퍼 연결을 해제합니다.
         * @param Context 바인딩할 장치 컨텍스트입니다.
         * @param Slot 셰이더 리소스 슬롯(t 레지스터)입니다.
         */
        static void UnbindClothVertices(ID3D11DeviceContext* Context, Index_t Slot);

        /**
         * @brief 천 정점 버퍼를 준비했는지 검사합니다.
         * @return 천 섹션이 있고 버퍼를 만들었으면 true입니다.
         */
        bool HasClothVertices() const;

        /**
         * @brief 바인드 형상의 로컬 경계를 반환합니다.
         * @return 공유 경계 상자입니다.
         */
        const FBox& GetBounds() const;

        FBox CalcSkinnedWorldBounds(std::span<const FMatrix> SkinMatrices, const FMatrix& ComponentToWorld) const;

        /**
         * @brief 메시 전체를 하나의 바디로 다룰 때 사용할 충돌 형상을 반환합니다.
         * @return 바인드 포즈 경계를 감싸는 바디 셋업의 비소유 주소이며, 메시 데이터가 없으면 nullptr입니다.
         * @note 본마다 바디를 두는 피직스 에셋을 도입하기 전까지 사용하는 단일 바디 형상입니다.
         */
        const PhysicsRuntime::UBodySetup* GetBodySetup() const;

        /**
         * @brief 정점 수를 반환합니다.
         * @return 바인드 형상의 정점 수입니다.
         */
        Uint32 GetVertexCount() const;

        /**
         * @brief 인덱스 수를 반환합니다.
         * @return 삼각형 인덱스 수입니다.
         */
        Uint32 GetIndexCount() const;

        /**
         * @brief 스키닝에 필요한 형상과 가중치가 있는지 검사합니다.
         * @return 유효한 메시이면 true입니다.
         */
        bool IsValid() const;

        /**
         * @brief 이 메시가 참조하는 물리 에셋 경로를 반환합니다.
         * @return Content 상대 경로이며, 지정하지 않았으면 빈 문자열입니다.
         */
        const Core::FString& GetPhysicsAssetPath() const;

        /**
         * @brief 이 메시가 참조할 물리 에셋 경로를 설정합니다.
         * @param[in] Path Content 상대 경로입니다.
         */
        void SetPhysicsAssetPath(Core::FString Path);

        /**
         * @brief 물리 에셋이 지정되지 않았을 때 쓸 물리 에셋을 뼈와 정점으로 만들어 반환합니다.
         * @param[in] Skeleton 이 메시가 사용하는 스켈레톤입니다.
         * @return 메시가 소유하는 물리 에셋이며, 만들 수 없으면 nullptr입니다.
         * @note 한 번 만들면 같은 스켈레톤에 대해 다시 사용합니다.
         */
        PhysicsRuntime::UPhysicsAsset* GetGeneratedPhysicsAsset(const Asset::USkeleton& Skeleton);

        /**
         * @brief 천 에셋을 메시에 추가합니다.
         * @param[in] ClothingAsset 메시가 소유할 천 에셋입니다.
         * @return 천 에셋 목록에서의 번호이며 에셋이 비어 있으면 InvalidIndex입니다.
         */
        Index_t AddClothingAsset(std::unique_ptr<PhysicsRuntime::UClothingAssetCommon> ClothingAsset);

        /**
         * @brief 메시가 소유한 천 에셋 목록을 반환합니다.
         * @return 천 에셋 목록의 읽기 전용 참조입니다.
         */
        const Core::TArray<std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>>& GetMeshClothingAssets() const;

        /**
         * @brief 식별자로 천 에셋을 찾습니다.
         * @param[in] AssetGuid 찾을 천 에셋의 식별자입니다.
         * @return 천 에셋의 비소유 주소이며 없으면 nullptr입니다.
         */
        PhysicsRuntime::UClothingAssetCommon* GetClothingAsset(const Core::FGuid& AssetGuid) const;

        /**
         * @brief 섹션의 천 연결 데이터를 반환합니다.
         * @param[in] SubMeshIndex 섹션 번호입니다.
         * @return 연결 데이터의 주소이며 섹션이 없으면 nullptr입니다.
         */
        const Asset::FClothingSectionData* GetSectionClothingData(Index_t SubMeshIndex) const;

        /**
         * @brief 섹션의 천 연결 데이터를 저장합니다.
         * @param[in] SubMeshIndex 섹션 번호입니다.
         * @param[in] ClothingData 저장할 연결 데이터입니다.
         * @return 섹션이 있어 저장했으면 true입니다.
         */
        bool SetSectionClothingData(Index_t SubMeshIndex, Asset::FClothingSectionData ClothingData);

        /**
         * @brief 섹션에 연결된 천 에셋을 모읍니다.
         * @param[out] OutClothingAssets 연결된 천 에셋의 비소유 주소이며 기존 내용은 지우고 같은 에셋은 한 번만 넣습니다.
         */
        void GetClothingAssetsInUse(Core::TArray<const PhysicsRuntime::UClothingAssetCommon*>& OutClothingAssets) const;

        /**
         * @brief 천 에셋을 메시에서 빼고, 그 에셋을 따르던 섹션의 연결을 지웁니다.
         * @details 뒤쪽 천 에셋의 번호가 하나씩 당겨지므로 다른 섹션의 에셋 번호도 맞춥니다.
         *          이 에셋으로 천을 만든 컴포넌트는 부르기 전에 천을 해제해야 합니다.
         * @param[in] AssetGuid 뺄 천 에셋의 식별자입니다.
         * @return 에셋을 찾아 뺐으면 true입니다.
         */
        bool RemoveClothingAsset(const Core::FGuid& AssetGuid);

        /**
         * @brief 천 에셋과 섹션 연결 데이터를 저장하거나 불러옵니다.
         * @details 불러올 때는 기존 천 데이터를 지우고 파일의 것으로 바꿉니다. 물리 에셋은 경로만 들어 있습니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         */
        void SerializeClothingData(Core::FArchive& Archive);

    private:

        /**
         * @brief BLENDINDICES(uint4)와 BLENDWEIGHT(float4) 입력 하나의 GPU 배치입니다.
         */
        struct FSkinWeightVertex {

            /**
             * @brief 정점에 영향을 주는 최대 네 개의 본 인덱스입니다.
             */
            Core::TStaticArray<Uint32, 4> mBoneIndices{};

            /**
             * @brief 본 인덱스 순서에 대응하는 정점 스킨 가중치입니다.
             */
            Core::TStaticArray<float, 4> mWeights{};
        };

        /**
         * @brief 스킨 가중치를 담는 불변 정점 버퍼입니다.
         */
        using FSkinWeightBuffer = RenderCore::TVertexBuffer<FSkinWeightVertex, RenderCore::EBufferUpdate::Immutable>;

        /**
         * @brief 정점 하나가 붙어 있는 천 입자 삼각형 정보의 GPU 배치이며 Cloth.hlsl의 FClothVertex와 같아야 합니다.
         */
        struct FClothVertexGpu {

            /**
             * @brief 위치의 무게중심 좌표 세 개와 삼각형 면에서 법선 방향으로 떨어진 거리입니다.
             */
            Core::TStaticArray<float, 4> mPositionBaryCoordsAndDist{};

            /**
             * @brief 위치에서 법선 방향으로 간 점의 무게중심 좌표와 거리입니다.
             */
            Core::TStaticArray<float, 4> mNormalBaryCoordsAndDist{};

            /**
             * @brief 위치에서 접선 방향으로 간 점의 무게중심 좌표와 거리입니다.
             */
            Core::TStaticArray<float, 4> mTangentBaryCoordsAndDist{};

            /**
             * @brief 앞 세 개는 입자 번호이고 마지막이 0xFFFF이면 스키닝만 쓰는 정점입니다.
             */
            Core::TStaticArray<Uint32, 4> mSourceMeshVertIndices{0, 0, 0, 0xFFFF};

            /**
             * @brief 여러 삼각형의 영향을 섞을 때 쓰는 가중치입니다.
             */
            float mWeight{};

            /**
             * @brief 16바이트 정렬을 맞추는 빈 칸입니다.
             */
            Core::TStaticArray<float, 3> mPadding{};
        };

        /**
         * @brief 메시 정점 순서의 천 정점 정보를 담는 구조화 버퍼입니다.
         */
        using FClothVertexBuffer = RenderCore::TStructuredBuffer<FClothVertexGpu>;

        /**
         * @brief 천 섹션의 연결 데이터로 메시 정점 순서의 천 정점 정보를 만들어 올립니다.
         * @param Device 사용할 D3D11 장치입니다.
         * @return 천 섹션이 없거나 버퍼를 준비했으면 true입니다.
         */
        bool CreateClothVertexBuffer(ID3D11Device* Device);

    private:

        /**
         * @brief 정적 메시와 공유하는 버퍼 구현이며 위치는 바인드 포즈를 유지합니다.
         */
        Asset::UStaticMesh mGeometry{};

        /**
         * @brief 정점별 정규화한 본 영향입니다.
         */
        Core::TArray<Asset::FMeshImportSkinWeight> mSkinWeights{};

        /**
         * @brief GPU 스키닝에서 참조할 본 행렬 수입니다.
         */
        Uint32 mSkinMatrixCount{};

        Core::TArray<std::optional<FBox>> mSkinInfluenceBounds{};
        std::optional<FBox> mUnweightedBounds{};

        /**
         * @brief BLENDINDICES와 BLENDWEIGHT 입력의 공유 불변 버퍼입니다.
         */
        FSkinWeightBuffer mSkinWeightBuffer{};

        /**
         * @brief 메시 정점마다 붙어 있는 천 입자 삼각형 정보이며 천 섹션 연결이 바뀌면 다시 만듭니다.
         */
        FClothVertexBuffer mClothVertexBuffer{};

        /**
         * @brief 이 메시가 참조하는 물리 에셋 경로입니다.
         */
        Core::FString mPhysicsAssetPath{};

        /**
         * @brief 물리 에셋이 지정되지 않았을 때 만들어 둔 물리 에셋입니다.
         */
        std::unique_ptr<PhysicsRuntime::UPhysicsAsset> mGeneratedPhysicsAsset{};

        /**
         * @brief 만들어 둔 물리 에셋의 기준이 된 스켈레톤입니다.
         */
        const Asset::USkeleton* mGeneratedPhysicsAssetSkeleton{};

        /**
         * @brief 섹션을 천으로 시뮬레이션할 때 쓰는 천 에셋입니다.
         */
        Core::TArray<std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>> mMeshClothingAssets{};

        /**
         * @brief 섹션 순서대로의 천 연결 데이터이며 천이 없는 섹션은 비어 있습니다.
         */
        Core::TArray<Asset::FClothingSectionData> mSectionClothingData{};
    };

}
