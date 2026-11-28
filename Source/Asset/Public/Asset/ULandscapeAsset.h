#pragma once

#include "Asset/FLandscapeLayerInfo.h"
#include "Core/FRevision.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FVector.h"
#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "CoreUObject/UObject.h"

#include <cstdint>
#include <filesystem>
#include <memory>

#include "ULandscapeAsset.generated.h"

namespace Core {
	class FArchive;
}

namespace Asset {

	class FAssetManager;
	class UTexture2D;

	// 하이트맵과 레이어 및 웨이트맵 데이터를 보관하는 지형 에셋
	UCLASS()
	class ULandscapeAsset final : public CoreUObject::UObject {
		GENERATED_BODY()

	public:
		static constexpr std::uint16_t MidHeightValue{32768u};
		static constexpr Uint32 MaxActiveLayersPerComponent{8u};
		static constexpr Uint32 LayersPerWeightmap{4u};

	public:
		ULandscapeAsset();
		~ULandscapeAsset() override;

	public:
		// 지정한 규격의 평탄한 지형 데이터 초기화
		bool CreateFlat(Uint32 SubsectionSizeQuads, Uint32 NumSubsections, Uint32 ComponentCountX, Uint32 ComponentCountY, float QuadSize = 1.0f, float HeightScale = 512.0f);

		// 테스트용 프로시저럴 언덕 하이트맵과 자동 레이어 가중치 생성
		bool GenerateProceduralHills(Uint32 Seed, float Amplitude, float Frequency);

		// 외부 파일에서 하이트맵 임포트
		bool ImportHeightmapFromFile(const std::filesystem::path& SourcePath, Core::FString& Error);

		// 텍스처 에셋에서 하이트맵 적용
		bool ApplyHeightmapFromTexture(const Asset::UTexture2D& Texture, Core::FString& Error);

		// 외부 이미지 파일에서 특정 레이어 웨이트맵 임포트
		bool ImportWeightmapFromFile(Index_t LayerIndex, const std::filesystem::path& SourcePath, Core::FString& Error);

		// 텍스처 에셋에서 특정 레이어 웨이트맵 적용
		bool ApplyWeightmapFromTexture(Index_t LayerIndex, const Asset::UTexture2D& Texture, Core::FString& Error);

		// 정규화 없이 텍스처 에셋의 원본 가중치를 특정 레이어에 기록
		bool SampleWeightmapTextureToLayerRaw(Index_t LayerIndex, const Asset::UTexture2D& Texture, Core::FString& Error);

		// 특정 레이어 전체 가중치를 단일 값으로 채우기
		void FillLayerWeights(Index_t LayerIndex, Uint8 Value);

		// 외부 하이트맵 또는 웨이트맵 원본 파일을 에셋 저장용 텍스처로 변환
		static bool ConvertSourceImageToTextureAsset(const std::filesystem::path& SourcePath, bool PreserveHighPrecision, Asset::UTexture2D& OutTexture, Core::FString& Error);

		// 레이어 추가 및 가중치 배열 확보
		Index_t AddLayer(const Asset::FLandscapeLayerInfo& LayerInfo);

		// 레이어 제거
		bool RemoveLayer(Index_t LayerIndex);

		// 레이어 목록의 머티리얼 참조 해석
		bool ResolveLayerMaterials(Asset::FAssetManager& Assets, Core::FString& Error);

		// 가중치 정규화
		void NormalizeWeightBlendedLayers();

		// 정점 높이 설정
		bool SetHeightSample(Uint32 X, Uint32 Y, std::uint16_t HeightValue);

		// 정점 높이 조회
		std::uint16_t GetHeightSample(Uint32 X, Uint32 Y) const;

		// 정점 레이어 가중치 설정
		bool SetLayerWeightSample(Index_t LayerIndex, Uint32 X, Uint32 Y, Uint8 WeightValue);

		// 정점 레이어 가중치 조회
		Uint8 GetLayerWeightSample(Index_t LayerIndex, Uint32 X, Uint32 Y) const;

		// 쿼드 홀 마스크 설정
		bool SetQuadHole(Uint32 QuadX, Uint32 QuadY, bool IsHole);

		// 쿼드 홀 여부 조회
		bool IsQuadHole(Uint32 QuadX, Uint32 QuadY) const;

		// 정수 높이를 로컬 높이로 변환
		float HeightSampleToLocalZ(std::uint16_t HeightValue) const;

		// 로컬 높이를 정수 높이로 변환
		std::uint16_t LocalZToHeightSample(float LocalZ) const;

		// 로컬 평면 좌표에서 삼각형 보간 높이 조회
		bool SampleLocalHeight(float LocalX, float LocalY, float& OutLocalZ) const;

		// 로컬 평면 좌표에서 높이 및 법선 조회
		bool SampleLocalHeightAndNormal(float LocalX, float LocalY, float& OutLocalZ, FVector& OutLocalNormal, bool SmoothNormal) const;

		// 로컬 평면 좌표에서 각 레이어 가중치 조회
		bool SampleLocalLayerWeights(float LocalX, float LocalY, Core::TArray<float>& OutWeights) const;

		// 지정한 쿼드에서 지배적인 레이어 인덱스 반환
		Index_t GetDominantLayerAtQuad(Uint32 QuadX, Uint32 QuadY) const;

		// 서브섹션의 로컬 경계 상자 계산
		FBox ComputeSubsectionLocalBounds(Uint32 ComponentX, Uint32 ComponentY, Uint32 SubX, Uint32 SubY) const;

		// 컴포넌트의 로컬 경계 상자 계산
		FBox ComputeComponentLocalBounds(Uint32 ComponentX, Uint32 ComponentY) const;

		// 전체 지형의 로컬 경계 상자 계산
		FBox ComputeTotalLocalBounds() const;

		// 높이와 법선을 패킹한 GPU 하이트맵 텍스처 생성
		std::unique_ptr<Asset::UTexture2D> BuildPackedHeightmapTexture() const;

		// 컴포넌트 영역의 활성 레이어 웨이트맵 텍스처 생성
		bool BuildComponentWeightmapTextures(Uint32 ComponentX, Uint32 ComponentY, Core::TArray<std::unique_ptr<Asset::UTexture2D>>& OutTextures, Core::TArray<Index_t>& OutLayerIndices) const;

		// 바이너리 아카이브 직렬화
		bool SerializeBinary(Core::FArchive& Archive);

	public:
		bool IsValid() const;
		Uint32 GetSubsectionSizeQuads() const;
		Uint32 GetNumSubsections() const;
		Uint32 GetComponentSizeQuads() const;
		Uint32 GetComponentResolution() const;
		Uint32 GetComponentCountX() const;
		Uint32 GetComponentCountY() const;
		Uint32 GetComponentCount() const;
		Uint32 GetTotalQuadsX() const;
		Uint32 GetTotalQuadsY() const;
		Uint32 GetTotalResolutionX() const;
		Uint32 GetTotalResolutionY() const;
		Uint32 GetMaxLOD() const;
		float GetQuadSize() const;
		bool SetQuadSize(float QuadSize);
		float GetHeightScale() const;
		bool SetHeightScale(float HeightScale);
		Core::FRevision GetDataRevision() const;
		void MarkDataDirty();

		const Core::TArray<std::uint16_t>& GetHeights() const;
		const Core::TArray<Asset::FLandscapeLayerInfo>& GetLayers() const;
		Core::TArray<Asset::FLandscapeLayerInfo>& GetMutableLayers();
		const Core::TArray<Core::TArray<Uint8>>& GetLayerWeights() const;
		const Core::TArray<Uint8>& GetHoles() const;

	private:
		FVector ComputeVertexLocalNormal(Uint32 X, Uint32 Y) const;
		void SyncWeightArraySizes();

	private:
		Uint32 mSubsectionSizeQuads{63u};
		Uint32 mNumSubsections{1u};
		Uint32 mComponentCountX{2u};
		Uint32 mComponentCountY{2u};
		float mQuadSize{1.0f};
		float mHeightScale{512.0f};
		Core::FRevision mDataRevision{Core::FRevision::Next()};

		Core::TArray<std::uint16_t> mHeights{};
		Core::TArray<Asset::FLandscapeLayerInfo> mLayers{};
		Core::TArray<Core::TArray<Uint8>> mLayerWeights{};
		Core::TArray<Uint8> mHoles{};
	};

}
