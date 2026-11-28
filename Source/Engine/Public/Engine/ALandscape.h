#pragma once

#include "Asset/FLandscapeGridBuilder.h"
#include "Asset/FLandscapeLayerInfo.h"
#include "Core/Math/FVector.h"
#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "CoreUObject/TObjectRef.h"
#include "Engine/AActor.h"
#include "Engine/FLandscapeRenderData.h"

#include <filesystem>
#include <memory>

#include "ALandscape.generated.h"

struct FRay;

namespace Asset {
	class ULandscapeAsset;
	class UMaterial;
	class UStaticMesh;
	class UTexture2D;
}

namespace Engine {

	class ULandscapeComponent;

	// 컴포넌트와 서브섹션 계층을 관리하는 지형 액터
	UCLASS()
	class ALandscape final : public Engine::AActor {
		GENERATED_BODY()

	public:
		ALandscape();
		~ALandscape() override;

	public:
		bool InitializeDefaultComponents() override;

	public:
		// 런타임 공유 그리드와 패킹 텍스처 및 머티리얼 보장
		void EnsureRuntimeResources();

		// 현재 설정과 에셋에 맞춰 컴포넌트 격자와 렌더 및 물리 상태 재구성
		bool RebuildLandscape();

		// 프로시저럴 언덕 지형 생성 후 갱신
		bool GenerateProceduralHills(Uint32 Seed, float Amplitude, float Frequency);

		// 외부 하이트맵 파일 임포트 후 갱신
		bool ImportHeightmapFile(const std::filesystem::path& SourcePath, Core::FString& Error);

		// 텍스처 에셋에서 하이트맵 적용 후 갱신
		bool ApplyHeightmapTextureAsset(const Core::FString& TextureAssetPath, Core::FString& Error);

		// 외부 웨이트맵 파일 임포트 후 갱신
		bool ImportWeightmapFile(Index_t LayerIndex, const std::filesystem::path& SourcePath, Core::FString& Error);

		// 텍스처 에셋에서 특정 레이어 웨이트맵 적용 후 갱신
		bool ApplyWeightmapTextureAsset(Index_t LayerIndex, const Core::FString& TextureAssetPath, Core::FString& Error);

		// 현재 지형 데이터를 에셋 파일로 저장
		bool SaveLandscapeAsset(const std::filesystem::path& TargetPath, Core::FString& Error);

		// 레이어 추가 및 갱신
		Index_t AddLayer(const Asset::FLandscapeLayerInfo& LayerInfo);

		// 레이어 제거 및 갱신
		bool RemoveLayer(Index_t LayerIndex);

		// 월드 위치에서 지형 높이 조회
		bool SampleHeightAtWorld(const FVector& WorldPosition, float& OutWorldZ) const;

		// 월드 위치에서 지형 높이와 법선 조회
		bool SampleHeightAndNormalAtWorld(const FVector& WorldPosition, float& OutWorldZ, FVector& OutWorldNormal, bool SmoothNormal = true) const;

		// 월드 위치에서 레이어 가중치 조회
		bool SampleLayerWeightsAtWorld(const FVector& WorldPosition, Core::TArray<float>& OutWeights) const;

		// 월드 광선으로 지형 교차 검사
		bool RaycastLandscape(const FRay& WorldRay, float& OutDistance, FVector& OutWorldNormal) const;

	public:
		const Core::FString& GetLandscapeAssetPath() const;
		bool SetLandscapeAssetPath(const Core::FString& Path);

		const Core::FString& GetLandscapeMaterialPath() const;
		bool SetLandscapeMaterialPath(const Core::FString& Path);

		const Core::FString& GetHeightmapTexturePath() const;
		bool SetHeightmapTexturePath(const Core::FString& Path);

		Uint32 GetSubsectionSizeQuads() const;
		bool SetSubsectionSizeQuads(Uint32 SizeQuads);

		Uint32 GetNumSubsections() const;
		bool SetNumSubsections(Uint32 NumSubsections);

		Uint32 GetComponentCountX() const;
		bool SetComponentCountX(Uint32 CountX);

		Uint32 GetComponentCountY() const;
		bool SetComponentCountY(Uint32 CountY);

		float GetQuadSize() const;
		bool SetQuadSize(float QuadSize);

		float GetHeightScale() const;
		bool SetHeightScale(float HeightScale);

		float GetLODDistanceFactor() const;
		bool SetLODDistanceFactor(float DistanceFactor);

		const Core::TArray<Asset::FLandscapeLayerInfo>& GetLayers() const;
		bool SetLayers(const Core::TArray<Asset::FLandscapeLayerInfo>& Layers);

		// 정보 표시용 계산 속성
		Uint32 GetComponentResolution() const;
		Uint32 GetComponentCount() const;
		Uint32 GetTotalResolutionX() const;
		Uint32 GetTotalResolutionY() const;
		Uint32 GetMaxLOD() const;

		// 내부 컴포넌트가 참조하는 공유 리소스 접근자
		Asset::ULandscapeAsset* GetActiveLandscapeAsset() const;
		Asset::UStaticMesh* GetSharedGridMesh() const;
		Asset::UMaterial* GetResolvedLandscapeMaterial() const;
		std::shared_ptr<const Engine::FLandscapeSharedRenderData> GetSharedRenderData() const;
		Core::TArray<Engine::ULandscapeComponent*> GetLandscapeComponents() const;

	private:
		void SyncParametersFromAsset(const Asset::ULandscapeAsset& Asset);
		void SyncLayersToAsset(Asset::ULandscapeAsset& Asset);
		void ResolveMaterials();
		void RebuildSharedRenderResources();
		void RebuildComponentGrid();
		void RefreshExistingComponents();

	private:
		UPROPERTY(Edit, Serialize, Category = "Landscape", Name = "LandscapeAsset", Meta = (AssetType = "Landscape", EmptyLabel = "Procedural / Embedded"), Getter = GetLandscapeAssetPath, Setter = SetLandscapeAssetPath)
		Core::FString mLandscapeAssetPath{};

		UPROPERTY(Edit, Serialize, Category = "Landscape", Name = "LandscapeMaterial", Meta = (AssetType = "Material", EmptyLabel = "Default Landscape Material"), Getter = GetLandscapeMaterialPath, Setter = SetLandscapeMaterialPath)
		Core::FString mLandscapeMaterialPath{};

		UPROPERTY(Edit, Serialize, Category = "Landscape", Name = "HeightmapTexture", Meta = (AssetType = "Texture", EmptyLabel = "None (Procedural)"), Getter = GetHeightmapTexturePath, Setter = SetHeightmapTexturePath)
		Core::FString mHeightmapTexturePath{};

		UPROPERTY(Edit, Serialize, Category = "Topology", Getter = GetSubsectionSizeQuads, Setter = SetSubsectionSizeQuads)
		Uint32 mSubsectionSizeQuads{63u};

		UPROPERTY(Edit, Serialize, Category = "Topology", Getter = GetNumSubsections, Setter = SetNumSubsections)
		Uint32 mNumSubsections{1u};

		UPROPERTY(Edit, Serialize, Category = "Topology", Getter = GetComponentCountX, Setter = SetComponentCountX)
		Uint32 mComponentCountX{2u};

		UPROPERTY(Edit, Serialize, Category = "Topology", Getter = GetComponentCountY, Setter = SetComponentCountY)
		Uint32 mComponentCountY{2u};

		UPROPERTY(Edit, Serialize, Category = "Topology", Getter = GetQuadSize, Setter = SetQuadSize)
		float mQuadSize{1.0f};

		UPROPERTY(Edit, Serialize, Category = "Topology", Getter = GetHeightScale, Setter = SetHeightScale)
		float mHeightScale{128.0f};

		UPROPERTY(Edit, Serialize, Category = "LOD", Getter = GetLODDistanceFactor, Setter = SetLODDistanceFactor)
		float mLODDistanceFactor{1.0f};

		UPROPERTY(Edit, Serialize, Category = "Layers", Getter = GetLayers, Setter = SetLayers)
		Core::TArray<Asset::FLandscapeLayerInfo> mLayers{};

		// 런타임 자산과 공유 GPU 리소스
		std::unique_ptr<Asset::ULandscapeAsset> mOwnedAsset{};
		CoreUObject::TObjectRef<Asset::ULandscapeAsset> mExternalAsset{};
		CoreUObject::TObjectRef<Asset::UMaterial> mLandscapeMaterial{};
		std::unique_ptr<Asset::UMaterial> mDefaultLandscapeMaterial{};
		Asset::FLandscapeSharedGrid mSharedGrid{};
		std::unique_ptr<Asset::UTexture2D> mPackedHeightmap{};
		std::shared_ptr<Engine::FLandscapeSharedRenderData> mSharedRenderData{};
		Core::FRevision mBuiltDataRevision{};
	};

}
