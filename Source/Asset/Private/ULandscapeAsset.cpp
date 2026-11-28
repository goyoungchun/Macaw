#include "Asset/ULandscapeAsset.h"
#include "Asset/FAssetManager.h"
#include "Asset/FLandscapeGridBuilder.h"
#include "Asset/UMaterial.h"
#include "Asset/UTexture2D.h"
#include "Core/Archive.h"
#include "Core/FilePath.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <DirectXTex.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace Asset {

	namespace {

		constexpr Uint32 LandscapeBinaryVersion{1u};
		constexpr Uint32 MaxLandscapeDimension{4097u};
		constexpr Uint32 MaxLandscapeLayers{64u};

		// 정수 좌표 기반 해시 값 생성
		float HashGrid(Int32 X, Int32 Y, Uint32 Seed) {
			Uint32 Value{static_cast<Uint32>(X) * 374761393u + static_cast<Uint32>(Y) * 668265263u + Seed * 1442695041u};
			Value = (Value ^ (Value >> 13u)) * 1274126177u;
			Value ^= (Value >> 16u);

			return static_cast<float>(Value & 0x00FFFFFFu) / 16777215.0f;
		}

		// 부드러운 보간 곡선
		float SmoothStep(float T) {
			return T * T * (3.0f - 2.0f * T);
		}

		// 단옥타브 값 노이즈
		float ValueNoise(float X, float Y, Uint32 Seed) {
			const Int32 X0{static_cast<Int32>(std::floor(X))};
			const Int32 Y0{static_cast<Int32>(std::floor(Y))};
			const float FX{SmoothStep(X - static_cast<float>(X0))};
			const float FY{SmoothStep(Y - static_cast<float>(Y0))};

			const float V00{HashGrid(X0, Y0, Seed)};
			const float V10{HashGrid(X0 + 1, Y0, Seed)};
			const float V01{HashGrid(X0, Y0 + 1, Seed)};
			const float V11{HashGrid(X0 + 1, Y0 + 1, Seed)};

			const float Bottom{V00 + (V10 - V00) * FX};
			const float Top{V01 + (V11 - V01) * FX};

			return Bottom + (Top - Bottom) * FY;
		}

		// 다옥타브 프랙탈 노이즈
		float FractalNoise(float X, float Y, Uint32 Seed) {
			float Total{};
			float Weight{0.5f};
			float Scale{1.0f};
			float Normalization{};

			for (Uint32 Octave{}; Octave < 5u; ++Octave) {
				Total += ValueNoise(X * Scale, Y * Scale, Seed + Octave * 101u) * Weight;
				Normalization += Weight;
				Weight *= 0.5f;
				Scale *= 2.0f;
			}

			return Normalization > 0.0f ? (Total / Normalization) * 2.0f - 1.0f : 0.0f;
		}

		// 정규화 이미지를 그레이스케일 부동소수점 배열로 디코딩
		bool DecodeImageToGrayscale(const std::filesystem::path& SourcePath, Core::TArray<float>& OutPixels, Uint32& OutWidth, Uint32& OutHeight, Core::FString& Error) {
			OutPixels.clear();
			OutWidth = 0u;
			OutHeight = 0u;

			std::error_code Code{};
			const Uint64 FileSize{std::filesystem::file_size(SourcePath, Code)};
			std::ifstream Input{SourcePath, std::ios::binary};

			if (Code || FileSize == 0u || !Input) {
				Error = "Cannot open source image file";

				return false;
			}

			Core::TArray<Uint8> FileBytes(static_cast<Index_t>(FileSize));

			if (!Input.read(reinterpret_cast<char*>(FileBytes.data()), static_cast<std::streamsize>(FileBytes.size()))) {
				Error = "Cannot read source image file";

				return false;
			}

			const std::wstring Extension{Core::FilePath::ToLowerExtension(SourcePath)};
			DirectX::ScratchImage Image{};
			HRESULT Result{E_FAIL};

			if (Extension == L".dds") {
				Result = DirectX::LoadFromDDSMemory(FileBytes.data(), FileBytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, Image);
			} else if (Extension == L".tga") {
				Result = DirectX::LoadFromTGAMemory(FileBytes.data(), FileBytes.size(), DirectX::TGA_FLAGS_NONE, nullptr, Image);
			} else if (Extension == L".hdr") {
				Result = DirectX::LoadFromHDRMemory(FileBytes.data(), FileBytes.size(), nullptr, Image);
			} else {
				const HRESULT ComResult{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};

				if (FAILED(ComResult) && ComResult != RPC_E_CHANGED_MODE) {
					Error = "Cannot initialize image decoder";

					return false;
				}

				Result = DirectX::LoadFromWICMemory(FileBytes.data(), FileBytes.size(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, Image);

				if (SUCCEEDED(ComResult)) {
					CoUninitialize();
				}
			}

			if (FAILED(Result)) {
				Error = "Unsupported or corrupted image file";

				return false;
			}

			const DirectX::Image* BaseImage{Image.GetImage(0u, 0u, 0u)};

			if (BaseImage == nullptr || BaseImage->width == 0u || BaseImage->height == 0u) {
				Error = "Decoded image is empty";

				return false;
			}

			DirectX::ScratchImage FloatImage{};

			if (FAILED(DirectX::Convert(*BaseImage, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_FORCE_NON_WIC, DirectX::TEX_THRESHOLD_DEFAULT, FloatImage))) {
				Error = "Cannot convert image format";

				return false;
			}

			const DirectX::Image* Converted{FloatImage.GetImage(0u, 0u, 0u)};

			if (Converted == nullptr) {
				Error = "Converted image is empty";

				return false;
			}

			OutWidth = static_cast<Uint32>(Converted->width);
			OutHeight = static_cast<Uint32>(Converted->height);
			OutPixels.resize(static_cast<Index_t>(OutWidth) * OutHeight);

			for (Uint32 Y{}; Y < OutHeight; ++Y) {
				const float* Row{reinterpret_cast<const float*>(Converted->pixels + static_cast<Index_t>(Y) * Converted->rowPitch)};

				for (Uint32 X{}; X < OutWidth; ++X) {
					OutPixels[static_cast<Index_t>(Y) * OutWidth + X] = std::clamp(Row[X * 4u], 0.0f, 1.0f);
				}
			}

			return true;
		}

		// 원시 정수 하이트맵 파일을 정규화 부동소수점 배열로 디코딩
		bool DecodeRawHeightmapToGrayscale(const std::filesystem::path& SourcePath, Uint32 ExpectedWidth, Uint32 ExpectedHeight, Core::TArray<float>& OutPixels, Uint32& OutWidth, Uint32& OutHeight, Core::FString& Error) {
			OutPixels.clear();
			OutWidth = 0u;
			OutHeight = 0u;

			std::error_code Code{};
			const Uint64 FileSize{std::filesystem::file_size(SourcePath, Code)};
			std::ifstream Input{SourcePath, std::ios::binary};

			if (Code || FileSize < 2u || (FileSize % 2u) != 0u || !Input) {
				Error = "Cannot open raw heightmap file";

				return false;
			}

			const Uint64 SampleCount{FileSize / 2u};

			if (ExpectedWidth > 0u && ExpectedHeight > 0u && SampleCount == static_cast<Uint64>(ExpectedWidth) * ExpectedHeight) {
				OutWidth = ExpectedWidth;
				OutHeight = ExpectedHeight;
			} else {
				const Uint32 SquareSide{static_cast<Uint32>(std::round(std::sqrt(static_cast<double>(SampleCount))))};

				if (static_cast<Uint64>(SquareSide) * SquareSide != SampleCount || SquareSide < 2u) {
					Error = "Raw heightmap dimensions do not match landscape resolution or square size";

					return false;
				}

				OutWidth = SquareSide;
				OutHeight = SquareSide;
			}

			Core::TArray<std::uint16_t> RawSamples(static_cast<Index_t>(SampleCount));

			if (!Input.read(reinterpret_cast<char*>(RawSamples.data()), static_cast<std::streamsize>(FileSize))) {
				Error = "Failed to read raw heightmap samples";

				return false;
			}

			OutPixels.resize(RawSamples.size());

			for (Index_t Index{}; Index < RawSamples.size(); ++Index) {
				OutPixels[Index] = static_cast<float>(RawSamples[Index]) / 65535.0f;
			}

			return true;
		}

		// 텍스처 에셋의 픽셀 또는 압축 데이터를 그레이스케일 배열로 추출
		bool DecodeTextureToGrayscale(const Asset::UTexture2D& Texture, Core::TArray<float>& OutPixels, Uint32& OutWidth, Uint32& OutHeight, Core::FString& Error) {
			OutPixels.clear();
			OutWidth = 0u;
			OutHeight = 0u;

			if (!Texture.IsValid() || Texture.GetSizeX() == 0u || Texture.GetSizeY() == 0u) {
				Error = "Invalid source texture asset";

				return false;
			}

			const Core::TArray<Uint8>& DDSData{Texture.GetDDSData()};

			if (!DDSData.empty()) {
				DirectX::ScratchImage Image{};

				if (FAILED(DirectX::LoadFromDDSMemory(DDSData.data(), DDSData.size(), DirectX::DDS_FLAGS_NONE, nullptr, Image))) {
					Error = "Cannot decode DDS texture asset";

					return false;
				}

				if (DirectX::IsCompressed(Image.GetMetadata().format)) {
					DirectX::ScratchImage Decompressed{};
					const DirectX::Image* CompressedBase{Image.GetImage(0u, 0u, 0u)};

					if (CompressedBase == nullptr || FAILED(DirectX::Decompress(*CompressedBase, DXGI_FORMAT_UNKNOWN, Decompressed))) {
						Error = "Cannot decompress texture asset";

						return false;
					}

					Image = std::move(Decompressed);
				}

				const DirectX::Image* BaseImage{Image.GetImage(0u, 0u, 0u)};
				DirectX::ScratchImage FloatImage{};

				if (BaseImage == nullptr || FAILED(DirectX::Convert(*BaseImage, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_FORCE_NON_WIC, DirectX::TEX_THRESHOLD_DEFAULT, FloatImage))) {
					Error = "Cannot convert texture asset format";

					return false;
				}

				const DirectX::Image* Converted{FloatImage.GetImage(0u, 0u, 0u)};

				if (Converted == nullptr) {
					Error = "Converted texture asset is empty";

					return false;
				}

				OutWidth = static_cast<Uint32>(Converted->width);
				OutHeight = static_cast<Uint32>(Converted->height);
				OutPixels.resize(static_cast<Index_t>(OutWidth) * OutHeight);

				for (Uint32 Y{}; Y < OutHeight; ++Y) {
					const float* Row{reinterpret_cast<const float*>(Converted->pixels + static_cast<Index_t>(Y) * Converted->rowPitch)};

					for (Uint32 X{}; X < OutWidth; ++X) {
						OutPixels[static_cast<Index_t>(Y) * OutWidth + X] = std::clamp(Row[X * 4u], 0.0f, 1.0f);
					}
				}

				return true;
			}

			const Core::TArray<Uint8>& Pixels{Texture.GetPixels()};

			if (Pixels.empty()) {
				Error = "Texture asset has no CPU pixel data";

				return false;
			}

			OutWidth = Texture.GetSizeX();
			OutHeight = Texture.GetSizeY();
			const Index_t PixelCount{static_cast<Index_t>(OutWidth) * OutHeight};
			OutPixels.resize(PixelCount);

			const Asset::ETextureFormat Format{Texture.GetFormat()};

			if (Format == Asset::ETextureFormat::RGBA8) {
				if (Pixels.size() < PixelCount * 4u) {
					Error = "Unexpected pixel buffer size";

					return false;
				}

				for (Index_t Index{}; Index < PixelCount; ++Index) {
					OutPixels[Index] = static_cast<float>(Pixels[Index * 4u]) / 255.0f;
				}

				return true;
			}

			if (Format == Asset::ETextureFormat::BGRA8) {
				if (Pixels.size() < PixelCount * 4u) {
					Error = "Unexpected pixel buffer size";

					return false;
				}

				for (Index_t Index{}; Index < PixelCount; ++Index) {
					OutPixels[Index] = static_cast<float>(Pixels[Index * 4u + 2u]) / 255.0f;
				}

				return true;
			}

			if (Format == Asset::ETextureFormat::R32Float) {
				if (Pixels.size() < PixelCount * 4u) {
					Error = "Unexpected pixel buffer size";

					return false;
				}

				for (Index_t Index{}; Index < PixelCount; ++Index) {
					float Sample{};
					std::memcpy(&Sample, Pixels.data() + Index * 4u, sizeof(float));
					OutPixels[Index] = std::clamp(Sample, 0.0f, 1.0f);
				}

				return true;
			}

			if (Format == Asset::ETextureFormat::RGBA32Float) {
				if (Pixels.size() < PixelCount * 16u) {
					Error = "Unexpected pixel buffer size";

					return false;
				}

				for (Index_t Index{}; Index < PixelCount; ++Index) {
					float Sample{};
					std::memcpy(&Sample, Pixels.data() + Index * 16u, sizeof(float));
					OutPixels[Index] = std::clamp(Sample, 0.0f, 1.0f);
				}

				return true;
			}

			if (Format == Asset::ETextureFormat::RGBA16Float) {
				if (Pixels.size() < PixelCount * 8u) {
					Error = "Unexpected pixel buffer size";

					return false;
				}

				DirectX::Image RawImage{};
				RawImage.width = OutWidth;
				RawImage.height = OutHeight;
				RawImage.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				RawImage.rowPitch = static_cast<std::size_t>(OutWidth) * 8u;
				RawImage.slicePitch = RawImage.rowPitch * OutHeight;
				RawImage.pixels = const_cast<uint8_t*>(Pixels.data());

				DirectX::ScratchImage FloatImage{};

				if (FAILED(DirectX::Convert(RawImage, DXGI_FORMAT_R32G32B32A32_FLOAT, DirectX::TEX_FILTER_FORCE_NON_WIC, DirectX::TEX_THRESHOLD_DEFAULT, FloatImage))) {
					Error = "Cannot convert half float texture asset";

					return false;
				}

				const DirectX::Image* Converted{FloatImage.GetImage(0u, 0u, 0u)};

				if (Converted == nullptr) {
					Error = "Converted texture asset is empty";

					return false;
				}

				for (Uint32 Y{}; Y < OutHeight; ++Y) {
					const float* Row{reinterpret_cast<const float*>(Converted->pixels + static_cast<Index_t>(Y) * Converted->rowPitch)};

					for (Uint32 X{}; X < OutWidth; ++X) {
						OutPixels[static_cast<Index_t>(Y) * OutWidth + X] = std::clamp(Row[X * 4u], 0.0f, 1.0f);
					}
				}

				return true;
			}

			Error = "Unsupported texture format for landscape map";

			return false;
		}

		// 정규화 좌표로 이중선형 샘플링
		float SampleBilinearFloat(const Core::TArray<float>& Source, Uint32 SourceWidth, Uint32 SourceHeight, float U, float V) {
			const float FX{std::clamp(U, 0.0f, 1.0f) * static_cast<float>(SourceWidth - 1u)};
			const float FY{std::clamp(V, 0.0f, 1.0f) * static_cast<float>(SourceHeight - 1u)};
			const Uint32 X0{static_cast<Uint32>(std::floor(FX))};
			const Uint32 Y0{static_cast<Uint32>(std::floor(FY))};
			const Uint32 X1{std::min(X0 + 1u, SourceWidth - 1u)};
			const Uint32 Y1{std::min(Y0 + 1u, SourceHeight - 1u)};
			const float TX{FX - static_cast<float>(X0)};
			const float TY{FY - static_cast<float>(Y0)};

			const float C00{Source[static_cast<Index_t>(Y0) * SourceWidth + X0]};
			const float C10{Source[static_cast<Index_t>(Y0) * SourceWidth + X1]};
			const float C01{Source[static_cast<Index_t>(Y1) * SourceWidth + X0]};
			const float C11{Source[static_cast<Index_t>(Y1) * SourceWidth + X1]};

			const float Bottom{C00 + (C10 - C00) * TX};
			const float Top{C01 + (C11 - C01) * TX};

			return Bottom + (Top - Bottom) * TY;
		}

	}

	ULandscapeAsset::ULandscapeAsset() {
		CreateFlat(63u, 1u, 2u, 2u, 1.0f, 512.0f);
	}

	ULandscapeAsset::~ULandscapeAsset() = default;

	bool ULandscapeAsset::CreateFlat(Uint32 SubsectionSizeQuads, Uint32 NumSubsections, Uint32 ComponentCountX, Uint32 ComponentCountY, float QuadSize, float HeightScale) {
		if (!Asset::FLandscapeGridBuilder::IsValidSubsectionSizeQuads(SubsectionSizeQuads)) {
			return false;
		}

		if (NumSubsections != 1u && NumSubsections != 2u) {
			return false;
		}

		if (ComponentCountX == 0u || ComponentCountY == 0u || !std::isfinite(QuadSize) || QuadSize <= 0.0f || !std::isfinite(HeightScale) || HeightScale <= 0.0f) {
			return false;
		}

		const Uint32 ComponentSizeQuads{SubsectionSizeQuads * NumSubsections};
		const Uint32 TotalResX{ComponentCountX * ComponentSizeQuads + 1u};
		const Uint32 TotalResY{ComponentCountY * ComponentSizeQuads + 1u};

		if (TotalResX > MaxLandscapeDimension || TotalResY > MaxLandscapeDimension) {
			return false;
		}

		mSubsectionSizeQuads = SubsectionSizeQuads;
		mNumSubsections = NumSubsections;
		mComponentCountX = ComponentCountX;
		mComponentCountY = ComponentCountY;
		mQuadSize = QuadSize;
		mHeightScale = HeightScale;

		const Index_t VertexCount{static_cast<Index_t>(TotalResX) * TotalResY};
		mHeights.assign(VertexCount, MidHeightValue);
		mHoles.clear();

		SyncWeightArraySizes();
		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::GenerateProceduralHills(Uint32 Seed, float Amplitude, float Frequency) {
		if (!IsValid() || !std::isfinite(Amplitude) || Amplitude < 0.0f || !std::isfinite(Frequency) || Frequency <= 0.0f) {
			return false;
		}

		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};

		for (Uint32 Y{}; Y < ResY; ++Y) {
			for (Uint32 X{}; X < ResX; ++X) {
				const float WorldX{static_cast<float>(X) * mQuadSize};
				const float WorldY{static_cast<float>(Y) * mQuadSize};
				const float LocalZ{FractalNoise(WorldX * Frequency, WorldY * Frequency, Seed) * Amplitude};
				mHeights[static_cast<Index_t>(Y) * ResX + X] = LocalZToHeightSample(LocalZ);
			}
		}

		// 레이어가 여러 개 등록되어 있으면 높이와 경사 기반으로 초기 가중치 분배
		if (mLayers.size() >= 2u) {
			SyncWeightArraySizes();

			for (Uint32 Y{}; Y < ResY; ++Y) {
				for (Uint32 X{}; X < ResX; ++X) {
					const Index_t VertexIndex{static_cast<Index_t>(Y) * ResX + X};
					const float LocalZ{HeightSampleToLocalZ(mHeights[VertexIndex])};
					const FVector Normal{ComputeVertexLocalNormal(X, Y)};
					const float Slope{1.0f - std::clamp(Normal.z, 0.0f, 1.0f)};
					const float NormalizedHeight{Amplitude > 0.0001f ? std::clamp((LocalZ / Amplitude) * 0.5f + 0.5f, 0.0f, 1.0f) : 0.5f};

					for (Index_t LayerIndex{}; LayerIndex < mLayerWeights.size(); ++LayerIndex) {
						mLayerWeights[LayerIndex][VertexIndex] = 0u;
					}

					const float RockWeight{std::clamp((Slope - 0.12f) * 4.0f, 0.0f, 1.0f)};
					const float HighWeight{mLayers.size() >= 3u ? std::clamp((NormalizedHeight - 0.65f) * 3.0f, 0.0f, 1.0f) * (1.0f - RockWeight) : 0.0f};
					const float BaseWeight{std::clamp(1.0f - RockWeight - HighWeight, 0.0f, 1.0f)};

					mLayerWeights[0u][VertexIndex] = static_cast<Uint8>(std::round(BaseWeight * 255.0f));
					mLayerWeights[1u][VertexIndex] = static_cast<Uint8>(std::round(RockWeight * 255.0f));

					if (mLayers.size() >= 3u) {
						mLayerWeights[2u][VertexIndex] = static_cast<Uint8>(std::round(HighWeight * 255.0f));
					}
				}
			}

			NormalizeWeightBlendedLayers();
		}

		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::ImportHeightmapFromFile(const std::filesystem::path& SourcePath, Core::FString& Error) {
		Error.clear();

		if (!IsValid() || SourcePath.empty()) {
			Error = "Invalid landscape state or heightmap path";

			return false;
		}

		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};
		const std::wstring Extension{Core::FilePath::ToLowerExtension(SourcePath)};

		if (Extension == L".r16" || Extension == L".raw") {
			std::error_code Code{};
			const Uint64 FileSize{std::filesystem::file_size(SourcePath, Code)};
			std::ifstream Input{SourcePath, std::ios::binary};

			if (Code || FileSize < 2u || (FileSize % 2u) != 0u || !Input) {
				Error = "Cannot open raw heightmap file";

				return false;
			}

			const Uint64 SampleCount{FileSize / 2u};
			Uint32 SourceWidth{};
			Uint32 SourceHeight{};

			if (SampleCount == static_cast<Uint64>(ResX) * ResY) {
				SourceWidth = ResX;
				SourceHeight = ResY;
			} else {
				const Uint32 SquareSide{static_cast<Uint32>(std::round(std::sqrt(static_cast<double>(SampleCount))))};

				if (static_cast<Uint64>(SquareSide) * SquareSide != SampleCount || SquareSide < 2u) {
					Error = "Raw heightmap dimensions do not match landscape resolution or square size";

					return false;
				}

				SourceWidth = SquareSide;
				SourceHeight = SquareSide;
			}

			Core::TArray<std::uint16_t> RawSamples(static_cast<Index_t>(SampleCount));

			if (!Input.read(reinterpret_cast<char*>(RawSamples.data()), static_cast<std::streamsize>(FileSize))) {
				Error = "Failed to read raw heightmap samples";

				return false;
			}

			if (SourceWidth == ResX && SourceHeight == ResY) {
				mHeights = std::move(RawSamples);
			} else {
				Core::TArray<float> Normalized(RawSamples.size());

				for (Index_t Index{}; Index < RawSamples.size(); ++Index) {
					Normalized[Index] = static_cast<float>(RawSamples[Index]) / 65535.0f;
				}

				for (Uint32 Y{}; Y < ResY; ++Y) {
					const float V{static_cast<float>(Y) / static_cast<float>(ResY - 1u)};

					for (Uint32 X{}; X < ResX; ++X) {
						const float U{static_cast<float>(X) / static_cast<float>(ResX - 1u)};
						const float Sample{SampleBilinearFloat(Normalized, SourceWidth, SourceHeight, U, V)};
						mHeights[static_cast<Index_t>(Y) * ResX + X] = static_cast<std::uint16_t>(std::round(std::clamp(Sample, 0.0f, 1.0f) * 65535.0f));
					}
				}
			}

			MarkDataDirty();

			return true;
		}

		Core::TArray<float> Decoded{};
		Uint32 SourceWidth{};
		Uint32 SourceHeight{};

		if (!DecodeImageToGrayscale(SourcePath, Decoded, SourceWidth, SourceHeight, Error)) {
			return false;
		}

		for (Uint32 Y{}; Y < ResY; ++Y) {
			const float V{static_cast<float>(Y) / static_cast<float>(ResY - 1u)};

			for (Uint32 X{}; X < ResX; ++X) {
				const float U{static_cast<float>(X) / static_cast<float>(ResX - 1u)};
				const float Sample{SampleBilinearFloat(Decoded, SourceWidth, SourceHeight, U, V)};
				mHeights[static_cast<Index_t>(Y) * ResX + X] = static_cast<std::uint16_t>(std::round(std::clamp(Sample, 0.0f, 1.0f) * 65535.0f));
			}
		}

		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::ApplyHeightmapFromTexture(const Asset::UTexture2D& Texture, Core::FString& Error) {
		Error.clear();

		if (!IsValid()) {
			Error = "Invalid landscape state";

			return false;
		}

		Core::TArray<float> Decoded{};
		Uint32 SourceWidth{};
		Uint32 SourceHeight{};

		if (!DecodeTextureToGrayscale(Texture, Decoded, SourceWidth, SourceHeight, Error)) {
			return false;
		}

		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};

		for (Uint32 Y{}; Y < ResY; ++Y) {
			const float V{static_cast<float>(Y) / static_cast<float>(ResY - 1u)};

			for (Uint32 X{}; X < ResX; ++X) {
				const float U{static_cast<float>(X) / static_cast<float>(ResX - 1u)};
				const float Sample{SampleBilinearFloat(Decoded, SourceWidth, SourceHeight, U, V)};
				mHeights[static_cast<Index_t>(Y) * ResX + X] = static_cast<std::uint16_t>(std::round(std::clamp(Sample, 0.0f, 1.0f) * 65535.0f));
			}
		}

		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::ImportWeightmapFromFile(Index_t LayerIndex, const std::filesystem::path& SourcePath, Core::FString& Error) {
		Error.clear();

		if (!IsValid() || LayerIndex >= mLayers.size() || SourcePath.empty()) {
			Error = "Invalid layer index or weightmap path";

			return false;
		}

		Core::TArray<float> Decoded{};
		Uint32 SourceWidth{};
		Uint32 SourceHeight{};

		if (!DecodeImageToGrayscale(SourcePath, Decoded, SourceWidth, SourceHeight, Error)) {
			return false;
		}

		SyncWeightArraySizes();

		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};
		Core::TArray<Uint8>& TargetWeights{mLayerWeights[LayerIndex]};

		for (Uint32 Y{}; Y < ResY; ++Y) {
			const float V{static_cast<float>(Y) / static_cast<float>(ResY - 1u)};

			for (Uint32 X{}; X < ResX; ++X) {
				const float U{static_cast<float>(X) / static_cast<float>(ResX - 1u)};
				const float Sample{SampleBilinearFloat(Decoded, SourceWidth, SourceHeight, U, V)};
				TargetWeights[static_cast<Index_t>(Y) * ResX + X] = static_cast<Uint8>(std::round(std::clamp(Sample, 0.0f, 1.0f) * 255.0f));
			}
		}

		NormalizeWeightBlendedLayers();
		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::ApplyWeightmapFromTexture(Index_t LayerIndex, const Asset::UTexture2D& Texture, Core::FString& Error) {
		if (!SampleWeightmapTextureToLayerRaw(LayerIndex, Texture, Error)) {
			return false;
		}

		NormalizeWeightBlendedLayers();
		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::SampleWeightmapTextureToLayerRaw(Index_t LayerIndex, const Asset::UTexture2D& Texture, Core::FString& Error) {
		Error.clear();

		if (!IsValid() || LayerIndex >= mLayers.size()) {
			Error = "Invalid layer index";

			return false;
		}

		Core::TArray<float> Decoded{};
		Uint32 SourceWidth{};
		Uint32 SourceHeight{};

		if (!DecodeTextureToGrayscale(Texture, Decoded, SourceWidth, SourceHeight, Error)) {
			return false;
		}

		SyncWeightArraySizes();

		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};
		Core::TArray<Uint8>& TargetWeights{mLayerWeights[LayerIndex]};

		for (Uint32 Y{}; Y < ResY; ++Y) {
			const float V{static_cast<float>(Y) / static_cast<float>(ResY - 1u)};

			for (Uint32 X{}; X < ResX; ++X) {
				const float U{static_cast<float>(X) / static_cast<float>(ResX - 1u)};
				const float Sample{SampleBilinearFloat(Decoded, SourceWidth, SourceHeight, U, V)};
				TargetWeights[static_cast<Index_t>(Y) * ResX + X] = static_cast<Uint8>(std::round(std::clamp(Sample, 0.0f, 1.0f) * 255.0f));
			}
		}

		return true;
	}

	void ULandscapeAsset::FillLayerWeights(Index_t LayerIndex, Uint8 Value) {
		SyncWeightArraySizes();

		if (LayerIndex >= mLayerWeights.size()) {
			return;
		}

		std::fill(mLayerWeights[LayerIndex].begin(), mLayerWeights[LayerIndex].end(), Value);
	}

	bool ULandscapeAsset::ConvertSourceImageToTextureAsset(const std::filesystem::path& SourcePath, bool PreserveHighPrecision, Asset::UTexture2D& OutTexture, Core::FString& Error) {
		Error.clear();

		if (SourcePath.empty()) {
			Error = "Source image path is empty";

			return false;
		}

		Core::TArray<float> Decoded{};
		Uint32 SourceWidth{};
		Uint32 SourceHeight{};
		const std::wstring Extension{Core::FilePath::ToLowerExtension(SourcePath)};

		if (Extension == L".r16" || Extension == L".raw") {
			if (!DecodeRawHeightmapToGrayscale(SourcePath, 0u, 0u, Decoded, SourceWidth, SourceHeight, Error)) {
				return false;
			}
		} else {
			if (!DecodeImageToGrayscale(SourcePath, Decoded, SourceWidth, SourceHeight, Error)) {
				return false;
			}
		}

		const Index_t PixelCount{static_cast<Index_t>(SourceWidth) * SourceHeight};

		if (PreserveHighPrecision) {
			Core::TArray<Uint8> FloatBytes(PixelCount * 16u);

			for (Index_t Index{}; Index < PixelCount; ++Index) {
				const float Value{std::clamp(Decoded[Index], 0.0f, 1.0f)};
				const float Pixel[4]{Value, Value, Value, 1.0f};
				std::memcpy(FloatBytes.data() + Index * 16u, Pixel, sizeof(Pixel));
			}

			if (!OutTexture.Configure(SourceWidth, SourceHeight, Asset::ETextureFormat::RGBA32Float, Asset::ETextureUsage::ShaderResource, false) ||
				!OutTexture.SetPixels(SourceWidth, SourceHeight, Asset::ETextureFormat::RGBA32Float, std::move(FloatBytes))) {
				Error = "Cannot build float texture asset from source image";

				return false;
			}
		} else {
			Core::TArray<Uint8> BytePixels(PixelCount * 4u);

			for (Index_t Index{}; Index < PixelCount; ++Index) {
				const Uint8 Byte{static_cast<Uint8>(std::round(std::clamp(Decoded[Index], 0.0f, 1.0f) * 255.0f))};
				BytePixels[Index * 4u + 0u] = Byte;
				BytePixels[Index * 4u + 1u] = Byte;
				BytePixels[Index * 4u + 2u] = Byte;
				BytePixels[Index * 4u + 3u] = 255u;
			}

			if (!OutTexture.Configure(SourceWidth, SourceHeight, Asset::ETextureFormat::RGBA8, Asset::ETextureUsage::ShaderResource, false) ||
				!OutTexture.SetPixels(SourceWidth, SourceHeight, Asset::ETextureFormat::RGBA8, std::move(BytePixels))) {
				Error = "Cannot build texture asset from source image";

				return false;
			}
		}

		return true;
	}

	Index_t ULandscapeAsset::AddLayer(const Asset::FLandscapeLayerInfo& LayerInfo) {
		if (mLayers.size() >= MaxLandscapeLayers) {
			return InvalidIndex;
		}

		const Index_t NewIndex{mLayers.size()};
		mLayers.push_back(LayerInfo);

		const Index_t VertexCount{static_cast<Index_t>(GetTotalResolutionX()) * GetTotalResolutionY()};
		const Uint8 InitialWeight{NewIndex == 0u ? 255u : 0u};
		mLayerWeights.emplace_back(VertexCount, InitialWeight);

		MarkDataDirty();

		return NewIndex;
	}

	bool ULandscapeAsset::RemoveLayer(Index_t LayerIndex) {
		if (LayerIndex >= mLayers.size()) {
			return false;
		}

		mLayers.erase(mLayers.begin() + static_cast<std::ptrdiff_t>(LayerIndex));

		if (LayerIndex < mLayerWeights.size()) {
			mLayerWeights.erase(mLayerWeights.begin() + static_cast<std::ptrdiff_t>(LayerIndex));
		}

		NormalizeWeightBlendedLayers();
		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::ResolveLayerMaterials(Asset::FAssetManager& Assets, Core::FString& Error) {
		Error.clear();
		bool AllResolved{true};

		for (Asset::FLandscapeLayerInfo& Layer : mLayers) {
			if (Layer.mMaterialPath.empty()) {
				Layer.mMaterial = {};
				continue;
			}

			Core::FString MaterialError{};
			Asset::UMaterial* Loaded{Assets.LoadMaterial(Core::FilePath::FromUtf8(Layer.mMaterialPath), MaterialError)};

			if (Loaded == nullptr) {
				if (Error.empty()) {
					Error = MaterialError;
				}
				Layer.mMaterial = {};
				AllResolved = false;
			} else {
				Layer.mMaterial = Loaded;
			}
		}

		return AllResolved;
	}

	void ULandscapeAsset::NormalizeWeightBlendedLayers() {
		SyncWeightArraySizes();

		if (mLayers.empty()) {
			return;
		}

		const Index_t VertexCount{static_cast<Index_t>(GetTotalResolutionX()) * GetTotalResolutionY()};

		for (Index_t VertexIndex{}; VertexIndex < VertexCount; ++VertexIndex) {
			Uint32 Sum{};
			Index_t FirstBlended{InvalidIndex};

			for (Index_t LayerIndex{}; LayerIndex < mLayers.size(); ++LayerIndex) {
				if (!mLayers[LayerIndex].mWeightBlended) {
					continue;
				}

				if (FirstBlended == InvalidIndex) {
					FirstBlended = LayerIndex;
				}

				Sum += mLayerWeights[LayerIndex][VertexIndex];
			}

			if (FirstBlended == InvalidIndex) {
				continue;
			}

			if (Sum == 0u) {
				mLayerWeights[FirstBlended][VertexIndex] = 255u;
				continue;
			}

			if (Sum == 255u) {
				continue;
			}

			Uint32 Remaining{255u};
			Index_t LastBlended{FirstBlended};

			for (Index_t LayerIndex{}; LayerIndex < mLayers.size(); ++LayerIndex) {
				if (!mLayers[LayerIndex].mWeightBlended) {
					continue;
				}

				LastBlended = LayerIndex;
				const Uint32 Scaled{static_cast<Uint32>(std::round((static_cast<float>(mLayerWeights[LayerIndex][VertexIndex]) * 255.0f) / static_cast<float>(Sum)))};
				const Uint32 Clamped{std::min(Scaled, Remaining)};
				mLayerWeights[LayerIndex][VertexIndex] = static_cast<Uint8>(Clamped);
				Remaining -= Clamped;
			}

			if (Remaining > 0u) {
				const Uint32 Adjusted{std::min<Uint32>(255u, static_cast<Uint32>(mLayerWeights[LastBlended][VertexIndex]) + Remaining)};
				mLayerWeights[LastBlended][VertexIndex] = static_cast<Uint8>(Adjusted);
			}
		}
	}

	bool ULandscapeAsset::SetHeightSample(Uint32 X, Uint32 Y, std::uint16_t HeightValue) {
		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};

		if (!IsValid() || X >= ResX || Y >= ResY) {
			return false;
		}

		mHeights[static_cast<Index_t>(Y) * ResX + X] = HeightValue;
		MarkDataDirty();

		return true;
	}

	std::uint16_t ULandscapeAsset::GetHeightSample(Uint32 X, Uint32 Y) const {
		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};

		if (!IsValid() || ResX == 0u || ResY == 0u) {
			return MidHeightValue;
		}

		const Uint32 ClampedX{std::min(X, ResX - 1u)};
		const Uint32 ClampedY{std::min(Y, ResY - 1u)};

		return mHeights[static_cast<Index_t>(ClampedY) * ResX + ClampedX];
	}

	bool ULandscapeAsset::SetLayerWeightSample(Index_t LayerIndex, Uint32 X, Uint32 Y, Uint8 WeightValue) {
		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};

		if (!IsValid() || LayerIndex >= mLayers.size() || X >= ResX || Y >= ResY) {
			return false;
		}

		SyncWeightArraySizes();
		mLayerWeights[LayerIndex][static_cast<Index_t>(Y) * ResX + X] = WeightValue;
		MarkDataDirty();

		return true;
	}

	Uint8 ULandscapeAsset::GetLayerWeightSample(Index_t LayerIndex, Uint32 X, Uint32 Y) const {
		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};

		if (!IsValid() || LayerIndex >= mLayerWeights.size() || mLayerWeights[LayerIndex].size() != static_cast<Index_t>(ResX) * ResY) {
			return 0u;
		}

		const Uint32 ClampedX{std::min(X, ResX - 1u)};
		const Uint32 ClampedY{std::min(Y, ResY - 1u)};

		return mLayerWeights[LayerIndex][static_cast<Index_t>(ClampedY) * ResX + ClampedX];
	}

	bool ULandscapeAsset::SetQuadHole(Uint32 QuadX, Uint32 QuadY, bool IsHole) {
		const Uint32 TotalQuadsX{GetTotalQuadsX()};
		const Uint32 TotalQuadsY{GetTotalQuadsY()};

		if (!IsValid() || QuadX >= TotalQuadsX || QuadY >= TotalQuadsY) {
			return false;
		}

		if (mHoles.empty()) {
			if (!IsHole) {
				return true;
			}

			mHoles.assign(static_cast<Index_t>(TotalQuadsX) * TotalQuadsY, 0u);
		}

		mHoles[static_cast<Index_t>(QuadY) * TotalQuadsX + QuadX] = IsHole ? 1u : 0u;
		MarkDataDirty();

		return true;
	}

	bool ULandscapeAsset::IsQuadHole(Uint32 QuadX, Uint32 QuadY) const {
		const Uint32 TotalQuadsX{GetTotalQuadsX()};
		const Uint32 TotalQuadsY{GetTotalQuadsY()};

		if (mHoles.empty() || QuadX >= TotalQuadsX || QuadY >= TotalQuadsY) {
			return false;
		}

		return mHoles[static_cast<Index_t>(QuadY) * TotalQuadsX + QuadX] != 0u;
	}

	float ULandscapeAsset::HeightSampleToLocalZ(std::uint16_t HeightValue) const {
		return (static_cast<float>(HeightValue) / 65535.0f - 0.5f) * mHeightScale;
	}

	std::uint16_t ULandscapeAsset::LocalZToHeightSample(float LocalZ) const {
		const float Normalized{std::clamp(LocalZ / mHeightScale + 0.5f, 0.0f, 1.0f)};

		return static_cast<std::uint16_t>(std::round(Normalized * 65535.0f));
	}

	bool ULandscapeAsset::SampleLocalHeight(float LocalX, float LocalY, float& OutLocalZ) const {
		FVector UnusedNormal{};

		return SampleLocalHeightAndNormal(LocalX, LocalY, OutLocalZ, UnusedNormal, false);
	}

	bool ULandscapeAsset::SampleLocalHeightAndNormal(float LocalX, float LocalY, float& OutLocalZ, FVector& OutLocalNormal, bool SmoothNormal) const {
		if (!IsValid() || !std::isfinite(LocalX) || !std::isfinite(LocalY)) {
			return false;
		}

		const Uint32 TotalQuadsX{GetTotalQuadsX()};
		const Uint32 TotalQuadsY{GetTotalQuadsY()};
		const float GridX{LocalX / mQuadSize};
		const float GridY{LocalY / mQuadSize};

		if (GridX < 0.0f || GridY < 0.0f || GridX > static_cast<float>(TotalQuadsX) || GridY > static_cast<float>(TotalQuadsY)) {
			return false;
		}

		const Uint32 CellX{std::min(static_cast<Uint32>(std::floor(GridX)), TotalQuadsX - 1u)};
		const Uint32 CellY{std::min(static_cast<Uint32>(std::floor(GridY)), TotalQuadsY - 1u)};

		if (IsQuadHole(CellX, CellY)) {
			return false;
		}

		const float FX{GridX - static_cast<float>(CellX)};
		const float FY{GridY - static_cast<float>(CellY)};

		const float Z00{HeightSampleToLocalZ(GetHeightSample(CellX, CellY))};
		const float Z10{HeightSampleToLocalZ(GetHeightSample(CellX + 1u, CellY))};
		const float Z01{HeightSampleToLocalZ(GetHeightSample(CellX, CellY + 1u))};
		const float Z11{HeightSampleToLocalZ(GetHeightSample(CellX + 1u, CellY + 1u))};

		// 공통 대각선 분할 규약과 일치하는 삼각형 보간
		if (FX >= FY) {
			OutLocalZ = Z00 + (Z10 - Z00) * FX + (Z11 - Z10) * FY;

			if (!SmoothNormal) {
				const FVector EdgeA{mQuadSize, 0.0f, Z10 - Z00};
				const FVector EdgeB{mQuadSize, mQuadSize, Z11 - Z00};
				OutLocalNormal = EdgeA.Cross(EdgeB).Normalized();
			}
		} else {
			OutLocalZ = Z00 + (Z11 - Z01) * FX + (Z01 - Z00) * FY;

			if (!SmoothNormal) {
				const FVector EdgeA{mQuadSize, mQuadSize, Z11 - Z00};
				const FVector EdgeB{0.0f, mQuadSize, Z01 - Z00};
				OutLocalNormal = EdgeA.Cross(EdgeB).Normalized();
			}
		}

		if (SmoothNormal) {
			const FVector N00{ComputeVertexLocalNormal(CellX, CellY)};
			const FVector N10{ComputeVertexLocalNormal(CellX + 1u, CellY)};
			const FVector N01{ComputeVertexLocalNormal(CellX, CellY + 1u)};
			const FVector N11{ComputeVertexLocalNormal(CellX + 1u, CellY + 1u)};

			const FVector Bottom{N00.Lerp(N10, FX)};
			const FVector Top{N01.Lerp(N11, FX)};
			OutLocalNormal = Bottom.Lerp(Top, FY).Normalized();
		}

		return true;
	}

	bool ULandscapeAsset::SampleLocalLayerWeights(float LocalX, float LocalY, Core::TArray<float>& OutWeights) const {
		OutWeights.clear();

		if (!IsValid() || !std::isfinite(LocalX) || !std::isfinite(LocalY)) {
			return false;
		}

		const Uint32 TotalQuadsX{GetTotalQuadsX()};
		const Uint32 TotalQuadsY{GetTotalQuadsY()};
		const float GridX{LocalX / mQuadSize};
		const float GridY{LocalY / mQuadSize};

		if (GridX < 0.0f || GridY < 0.0f || GridX > static_cast<float>(TotalQuadsX) || GridY > static_cast<float>(TotalQuadsY)) {
			return false;
		}

		const Uint32 CellX{std::min(static_cast<Uint32>(std::floor(GridX)), TotalQuadsX - 1u)};
		const Uint32 CellY{std::min(static_cast<Uint32>(std::floor(GridY)), TotalQuadsY - 1u)};
		const float FX{GridX - static_cast<float>(CellX)};
		const float FY{GridY - static_cast<float>(CellY)};

		OutWeights.resize(mLayers.size(), 0.0f);

		for (Index_t LayerIndex{}; LayerIndex < mLayers.size(); ++LayerIndex) {
			const float W00{static_cast<float>(GetLayerWeightSample(LayerIndex, CellX, CellY)) / 255.0f};
			const float W10{static_cast<float>(GetLayerWeightSample(LayerIndex, CellX + 1u, CellY)) / 255.0f};
			const float W01{static_cast<float>(GetLayerWeightSample(LayerIndex, CellX, CellY + 1u)) / 255.0f};
			const float W11{static_cast<float>(GetLayerWeightSample(LayerIndex, CellX + 1u, CellY + 1u)) / 255.0f};
			const float Bottom{W00 + (W10 - W00) * FX};
			const float Top{W01 + (W11 - W01) * FX};
			OutWeights[LayerIndex] = Bottom + (Top - Bottom) * FY;
		}

		return true;
	}

	Index_t ULandscapeAsset::GetDominantLayerAtQuad(Uint32 QuadX, Uint32 QuadY) const {
		if (!IsValid() || mLayers.empty()) {
			return InvalidIndex;
		}

		const Uint32 CellX{std::min(QuadX, GetTotalQuadsX() - 1u)};
		const Uint32 CellY{std::min(QuadY, GetTotalQuadsY() - 1u)};
		Index_t BestLayer{};
		Uint32 BestSum{};

		for (Index_t LayerIndex{}; LayerIndex < mLayers.size(); ++LayerIndex) {
			const Uint32 Sum{
				static_cast<Uint32>(GetLayerWeightSample(LayerIndex, CellX, CellY)) +
				static_cast<Uint32>(GetLayerWeightSample(LayerIndex, CellX + 1u, CellY)) +
				static_cast<Uint32>(GetLayerWeightSample(LayerIndex, CellX, CellY + 1u)) +
				static_cast<Uint32>(GetLayerWeightSample(LayerIndex, CellX + 1u, CellY + 1u))};

			if (Sum > BestSum) {
				BestSum = Sum;
				BestLayer = LayerIndex;
			}
		}

		return BestLayer;
	}

	FBox ULandscapeAsset::ComputeSubsectionLocalBounds(Uint32 ComponentX, Uint32 ComponentY, Uint32 SubX, Uint32 SubY) const {
		if (!IsValid() || ComponentX >= mComponentCountX || ComponentY >= mComponentCountY || SubX >= mNumSubsections || SubY >= mNumSubsections) {
			return {};
		}

		const Uint32 ComponentSizeQuads{GetComponentSizeQuads()};
		const Uint32 StartX{ComponentX * ComponentSizeQuads + SubX * mSubsectionSizeQuads};
		const Uint32 StartY{ComponentY * ComponentSizeQuads + SubY * mSubsectionSizeQuads};
		const Uint32 EndX{StartX + mSubsectionSizeQuads};
		const Uint32 EndY{StartY + mSubsectionSizeQuads};

		float MinZ{std::numeric_limits<float>::max()};
		float MaxZ{-std::numeric_limits<float>::max()};

		for (Uint32 Y{StartY}; Y <= EndY; ++Y) {
			for (Uint32 X{StartX}; X <= EndX; ++X) {
				const float Z{HeightSampleToLocalZ(GetHeightSample(X, Y))};
				MinZ = std::min(MinZ, Z);
				MaxZ = std::max(MaxZ, Z);
			}
		}

		constexpr float BoundsPadding{0.05f};
		const FVector Minimum{static_cast<float>(StartX) * mQuadSize, static_cast<float>(StartY) * mQuadSize, MinZ - BoundsPadding};
		const FVector Maximum{static_cast<float>(EndX) * mQuadSize, static_cast<float>(EndY) * mQuadSize, MaxZ + BoundsPadding};

		return FBox{Minimum, Maximum};
	}

	FBox ULandscapeAsset::ComputeComponentLocalBounds(Uint32 ComponentX, Uint32 ComponentY) const {
		if (!IsValid() || ComponentX >= mComponentCountX || ComponentY >= mComponentCountY) {
			return {};
		}

		FBox Combined{ComputeSubsectionLocalBounds(ComponentX, ComponentY, 0u, 0u)};

		for (Uint32 SubY{}; SubY < mNumSubsections; ++SubY) {
			for (Uint32 SubX{}; SubX < mNumSubsections; ++SubX) {
				const FBox SubBounds{ComputeSubsectionLocalBounds(ComponentX, ComponentY, SubX, SubY)};
				Combined.mMinimum = Combined.mMinimum.Min(SubBounds.mMinimum);
				Combined.mMaximum = Combined.mMaximum.Max(SubBounds.mMaximum);
			}
		}

		return Combined;
	}

	FBox ULandscapeAsset::ComputeTotalLocalBounds() const {
		if (!IsValid()) {
			return {};
		}

		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};
		float MinZ{std::numeric_limits<float>::max()};
		float MaxZ{-std::numeric_limits<float>::max()};

		for (std::uint16_t HeightValue : mHeights) {
			const float Z{HeightSampleToLocalZ(HeightValue)};
			MinZ = std::min(MinZ, Z);
			MaxZ = std::max(MaxZ, Z);
		}

		constexpr float BoundsPadding{0.05f};

		return FBox{
			{0.0f, 0.0f, MinZ - BoundsPadding},
			{static_cast<float>(ResX - 1u) * mQuadSize, static_cast<float>(ResY - 1u) * mQuadSize, MaxZ + BoundsPadding}};
	}

	std::unique_ptr<Asset::UTexture2D> ULandscapeAsset::BuildPackedHeightmapTexture() const {
		if (!IsValid()) {
			return nullptr;
		}

		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};
		Core::TArray<Uint8> Pixels(static_cast<Index_t>(ResX) * ResY * 4u);

		// 높이는 상위와 하위 바이트로 나누고 법선은 XY 성분을 정규화하여 패킹
		for (Uint32 Y{}; Y < ResY; ++Y) {
			for (Uint32 X{}; X < ResX; ++X) {
				const Index_t Index{static_cast<Index_t>(Y) * ResX + X};
				const std::uint16_t HeightValue{mHeights[Index]};
				const FVector Normal{ComputeVertexLocalNormal(X, Y)};

				Pixels[Index * 4u + 0u] = static_cast<Uint8>((HeightValue >> 8u) & 0xFFu);
				Pixels[Index * 4u + 1u] = static_cast<Uint8>(HeightValue & 0xFFu);
				Pixels[Index * 4u + 2u] = static_cast<Uint8>(std::round((std::clamp(Normal.x, -1.0f, 1.0f) * 0.5f + 0.5f) * 255.0f));
				Pixels[Index * 4u + 3u] = static_cast<Uint8>(std::round((std::clamp(Normal.y, -1.0f, 1.0f) * 0.5f + 0.5f) * 255.0f));
			}
		}

		std::unique_ptr<Asset::UTexture2D> Texture{CoreUObject::NewObject<Asset::UTexture2D>()};

		if (Texture == nullptr || !Texture->Configure(ResX, ResY, Asset::ETextureFormat::RGBA8, Asset::ETextureUsage::ShaderResource, false) || !Texture->SetPixels(ResX, ResY, Asset::ETextureFormat::RGBA8, std::move(Pixels))) {
			return nullptr;
		}

		Texture->SetName("LandscapeHeightmap");

		return Texture;
	}

	bool ULandscapeAsset::BuildComponentWeightmapTextures(Uint32 ComponentX, Uint32 ComponentY, Core::TArray<std::unique_ptr<Asset::UTexture2D>>& OutTextures, Core::TArray<Index_t>& OutLayerIndices) const {
		OutTextures.clear();
		OutLayerIndices.clear();

		if (!IsValid() || ComponentX >= mComponentCountX || ComponentY >= mComponentCountY || mLayers.empty()) {
			return true;
		}

		const Uint32 ComponentSizeQuads{GetComponentSizeQuads()};
		const Uint32 CompRes{ComponentSizeQuads + 1u};
		const Uint32 BaseX{ComponentX * ComponentSizeQuads};
		const Uint32 BaseY{ComponentY * ComponentSizeQuads};

		struct FLayerUsage {
			Index_t mLayerIndex{};
			Uint64 mWeightSum{};
		};

		Core::TArray<FLayerUsage> Usages{};
		Usages.reserve(mLayers.size());

		for (Index_t LayerIndex{}; LayerIndex < mLayers.size(); ++LayerIndex) {
			Uint64 WeightSum{};

			for (Uint32 LocalY{}; LocalY < CompRes; ++LocalY) {
				for (Uint32 LocalX{}; LocalX < CompRes; ++LocalX) {
					WeightSum += GetLayerWeightSample(LayerIndex, BaseX + LocalX, BaseY + LocalY);
				}
			}

			if (WeightSum > 0u) {
				Usages.push_back({LayerIndex, WeightSum});
			}
		}

		if (Usages.empty()) {
			Usages.push_back({0u, 1u});
		}

		if (Usages.size() > MaxActiveLayersPerComponent) {
			std::sort(Usages.begin(), Usages.end(), [](const FLayerUsage& A, const FLayerUsage& B) {
				return A.mWeightSum > B.mWeightSum;
			});
			Usages.resize(MaxActiveLayersPerComponent);
		}

		for (const FLayerUsage& Usage : Usages) {
			OutLayerIndices.push_back(Usage.mLayerIndex);
		}

		const Uint32 TextureCount{static_cast<Uint32>((OutLayerIndices.size() + LayersPerWeightmap - 1u) / LayersPerWeightmap)};
		OutTextures.reserve(TextureCount);

		for (Uint32 TextureIndex{}; TextureIndex < TextureCount; ++TextureIndex) {
			Core::TArray<Uint8> Pixels(static_cast<Index_t>(CompRes) * CompRes * 4u, 0u);

			for (Uint32 Channel{}; Channel < LayersPerWeightmap; ++Channel) {
				const Index_t SlotIndex{static_cast<Index_t>(TextureIndex) * LayersPerWeightmap + Channel};

				if (SlotIndex >= OutLayerIndices.size()) {
					break;
				}

				const Index_t LayerIndex{OutLayerIndices[SlotIndex]};

				for (Uint32 LocalY{}; LocalY < CompRes; ++LocalY) {
					for (Uint32 LocalX{}; LocalX < CompRes; ++LocalX) {
						const Index_t PixelIndex{static_cast<Index_t>(LocalY) * CompRes + LocalX};
						Pixels[PixelIndex * 4u + Channel] = GetLayerWeightSample(LayerIndex, BaseX + LocalX, BaseY + LocalY);
					}
				}
			}

			std::unique_ptr<Asset::UTexture2D> Weightmap{CoreUObject::NewObject<Asset::UTexture2D>()};

			if (Weightmap == nullptr || !Weightmap->Configure(CompRes, CompRes, Asset::ETextureFormat::RGBA8, Asset::ETextureUsage::ShaderResource, false) || !Weightmap->SetPixels(CompRes, CompRes, Asset::ETextureFormat::RGBA8, std::move(Pixels))) {
				OutTextures.clear();
				OutLayerIndices.clear();

				return false;
			}

			Weightmap->SetName("LandscapeWeightmap");
			OutTextures.push_back(std::move(Weightmap));
		}

		return true;
	}

	bool ULandscapeAsset::SerializeBinary(Core::FArchive& Archive) {
		Uint32 Version{LandscapeBinaryVersion};
		Archive.Serialize("Version", Version);

		if (Archive.IsLoading() && Version != LandscapeBinaryVersion) {
			Archive.SetError("Unsupported landscape asset version");

			return false;
		}

		Archive.Serialize("SubsectionSizeQuads", mSubsectionSizeQuads);
		Archive.Serialize("NumSubsections", mNumSubsections);
		Archive.Serialize("ComponentCountX", mComponentCountX);
		Archive.Serialize("ComponentCountY", mComponentCountY);
		Archive.Serialize("QuadSize", mQuadSize);
		Archive.Serialize("HeightScale", mHeightScale);

		if (Archive.HasError()) {
			return false;
		}

		if (Archive.IsLoading()) {
			if (!Asset::FLandscapeGridBuilder::IsValidSubsectionSizeQuads(mSubsectionSizeQuads) || (mNumSubsections != 1u && mNumSubsections != 2u) || mComponentCountX == 0u || mComponentCountY == 0u || !std::isfinite(mQuadSize) || mQuadSize <= 0.0f || !std::isfinite(mHeightScale) || mHeightScale <= 0.0f) {
				Archive.SetError("Invalid landscape configuration");

				return false;
			}

			const Uint32 TotalResX{GetTotalResolutionX()};
			const Uint32 TotalResY{GetTotalResolutionY()};

			if (TotalResX > MaxLandscapeDimension || TotalResY > MaxLandscapeDimension) {
				Archive.SetError("Landscape resolution exceeds limit");

				return false;
			}
		}

		const Uint32 ExpectedVertexCount{GetTotalResolutionX() * GetTotalResolutionY()};
		Uint32 HeightCount{Archive.IsLoading() ? 0u : static_cast<Uint32>(mHeights.size())};
		Archive.Serialize("HeightCount", HeightCount);

		if (Archive.HasError() || HeightCount != ExpectedVertexCount) {
			Archive.SetError("Landscape height sample count mismatch");

			return false;
		}

		if (Archive.IsLoading()) {
			mHeights.resize(HeightCount);
		}

		// 두 개씩 묶어 정수 하나로 직렬화
		const Uint32 PackedHeightWords{(HeightCount + 1u) / 2u};

		for (Uint32 WordIndex{}; WordIndex < PackedHeightWords; ++WordIndex) {
			const Index_t FirstIndex{static_cast<Index_t>(WordIndex) * 2u};
			const Index_t SecondIndex{FirstIndex + 1u};
			Uint32 Packed{};

			if (!Archive.IsLoading()) {
				const Uint32 Low{mHeights[FirstIndex]};
				const Uint32 High{SecondIndex < HeightCount ? static_cast<Uint32>(mHeights[SecondIndex]) : 0u};
				Packed = Low | (High << 16u);
			}

			Archive.Serialize("HeightPair", Packed);

			if (Archive.HasError()) {
				return false;
			}

			if (Archive.IsLoading()) {
				mHeights[FirstIndex] = static_cast<std::uint16_t>(Packed & 0xFFFFu);

				if (SecondIndex < HeightCount) {
					mHeights[SecondIndex] = static_cast<std::uint16_t>((Packed >> 16u) & 0xFFFFu);
				}
			}
		}

		Uint32 LayerCount{Archive.IsLoading() ? 0u : static_cast<Uint32>(mLayers.size())};
		Archive.Serialize("LayerCount", LayerCount);

		if (Archive.HasError() || LayerCount > MaxLandscapeLayers) {
			Archive.SetError("Invalid landscape layer count");

			return false;
		}

		if (Archive.IsLoading()) {
			mLayers.resize(LayerCount);
			mLayerWeights.resize(LayerCount);
		}

		for (Uint32 LayerIndex{}; LayerIndex < LayerCount; ++LayerIndex) {
			Asset::FLandscapeLayerInfo& Layer{mLayers[LayerIndex]};
			Archive.Serialize("LayerName", Layer.mLayerName);
			Archive.Serialize("MaterialPath", Layer.mMaterialPath);
			Archive.Serialize("WeightmapTexturePath", Layer.mWeightmapTexturePath);
			Archive.Serialize("UVScale", Layer.mUVScale);
			Archive.Serialize("WeightBlended", Layer.mWeightBlended);
			Archive.Serialize("StaticFriction", Layer.mStaticFriction);
			Archive.Serialize("DynamicFriction", Layer.mDynamicFriction);
			Archive.Serialize("Restitution", Layer.mRestitution);

			if (Archive.HasError()) {
				return false;
			}

			Core::TArray<Uint8>& Weights{mLayerWeights[LayerIndex]};

			if (Archive.IsLoading()) {
				Weights.resize(ExpectedVertexCount);
			} else if (Weights.size() != ExpectedVertexCount) {
				Weights.assign(ExpectedVertexCount, 0u);
			}

			const Uint32 PackedWeightWords{(ExpectedVertexCount + 3u) / 4u};

			for (Uint32 WordIndex{}; WordIndex < PackedWeightWords; ++WordIndex) {
				const Index_t Base{static_cast<Index_t>(WordIndex) * 4u};
				Uint32 Packed{};

				if (!Archive.IsLoading()) {
					for (Index_t Offset{}; Offset < 4u && Base + Offset < ExpectedVertexCount; ++Offset) {
						Packed |= static_cast<Uint32>(Weights[Base + Offset]) << (Offset * 8u);
					}
				}

				Archive.Serialize("WeightQuad", Packed);

				if (Archive.HasError()) {
					return false;
				}

				if (Archive.IsLoading()) {
					for (Index_t Offset{}; Offset < 4u && Base + Offset < ExpectedVertexCount; ++Offset) {
						Weights[Base + Offset] = static_cast<Uint8>((Packed >> (Offset * 8u)) & 0xFFu);
					}
				}
			}
		}

		Uint32 HoleCount{Archive.IsLoading() ? 0u : static_cast<Uint32>(mHoles.size())};
		Archive.Serialize("HoleCount", HoleCount);

		const Uint32 ExpectedQuadCount{GetTotalQuadsX() * GetTotalQuadsY()};

		if (Archive.HasError() || (HoleCount != 0u && HoleCount != ExpectedQuadCount)) {
			Archive.SetError("Invalid landscape hole mask count");

			return false;
		}

		if (Archive.IsLoading()) {
			mHoles.resize(HoleCount);
		}

		if (HoleCount > 0u) {
			const Uint32 PackedHoleWords{(HoleCount + 3u) / 4u};

			for (Uint32 WordIndex{}; WordIndex < PackedHoleWords; ++WordIndex) {
				const Index_t Base{static_cast<Index_t>(WordIndex) * 4u};
				Uint32 Packed{};

				if (!Archive.IsLoading()) {
					for (Index_t Offset{}; Offset < 4u && Base + Offset < HoleCount; ++Offset) {
						Packed |= static_cast<Uint32>(mHoles[Base + Offset]) << (Offset * 8u);
					}
				}

				Archive.Serialize("HoleQuad", Packed);

				if (Archive.HasError()) {
					return false;
				}

				if (Archive.IsLoading()) {
					for (Index_t Offset{}; Offset < 4u && Base + Offset < HoleCount; ++Offset) {
						mHoles[Base + Offset] = static_cast<Uint8>((Packed >> (Offset * 8u)) & 0xFFu);
					}
				}
			}
		}

		if (Archive.IsLoading()) {
			MarkDataDirty();
		}

		return !Archive.HasError();
	}

	bool ULandscapeAsset::IsValid() const {
		if (!Asset::FLandscapeGridBuilder::IsValidSubsectionSizeQuads(mSubsectionSizeQuads)) {
			return false;
		}

		if ((mNumSubsections != 1u && mNumSubsections != 2u) || mComponentCountX == 0u || mComponentCountY == 0u) {
			return false;
		}

		const Index_t ExpectedVertices{static_cast<Index_t>(GetTotalResolutionX()) * GetTotalResolutionY()};

		return mHeights.size() == ExpectedVertices && mQuadSize > 0.0f && mHeightScale > 0.0f;
	}

	Uint32 ULandscapeAsset::GetSubsectionSizeQuads() const {
		return mSubsectionSizeQuads;
	}

	Uint32 ULandscapeAsset::GetNumSubsections() const {
		return mNumSubsections;
	}

	Uint32 ULandscapeAsset::GetComponentSizeQuads() const {
		return mSubsectionSizeQuads * mNumSubsections;
	}

	Uint32 ULandscapeAsset::GetComponentResolution() const {
		return GetComponentSizeQuads() + 1u;
	}

	Uint32 ULandscapeAsset::GetComponentCountX() const {
		return mComponentCountX;
	}

	Uint32 ULandscapeAsset::GetComponentCountY() const {
		return mComponentCountY;
	}

	Uint32 ULandscapeAsset::GetComponentCount() const {
		return mComponentCountX * mComponentCountY;
	}

	Uint32 ULandscapeAsset::GetTotalQuadsX() const {
		return mComponentCountX * GetComponentSizeQuads();
	}

	Uint32 ULandscapeAsset::GetTotalQuadsY() const {
		return mComponentCountY * GetComponentSizeQuads();
	}

	Uint32 ULandscapeAsset::GetTotalResolutionX() const {
		return GetTotalQuadsX() + 1u;
	}

	Uint32 ULandscapeAsset::GetTotalResolutionY() const {
		return GetTotalQuadsY() + 1u;
	}

	Uint32 ULandscapeAsset::GetMaxLOD() const {
		return Asset::FLandscapeGridBuilder::ComputeMaxLOD(mSubsectionSizeQuads);
	}

	float ULandscapeAsset::GetQuadSize() const {
		return mQuadSize;
	}

	bool ULandscapeAsset::SetQuadSize(float QuadSize) {
		if (!std::isfinite(QuadSize) || QuadSize <= 0.0f) {
			return false;
		}

		mQuadSize = QuadSize;
		MarkDataDirty();

		return true;
	}

	float ULandscapeAsset::GetHeightScale() const {
		return mHeightScale;
	}

	bool ULandscapeAsset::SetHeightScale(float HeightScale) {
		if (!std::isfinite(HeightScale) || HeightScale <= 0.0f) {
			return false;
		}

		mHeightScale = HeightScale;
		MarkDataDirty();

		return true;
	}

	Core::FRevision ULandscapeAsset::GetDataRevision() const {
		return mDataRevision;
	}

	void ULandscapeAsset::MarkDataDirty() {
		mDataRevision = Core::FRevision::Next();
	}

	const Core::TArray<std::uint16_t>& ULandscapeAsset::GetHeights() const {
		return mHeights;
	}

	const Core::TArray<Asset::FLandscapeLayerInfo>& ULandscapeAsset::GetLayers() const {
		return mLayers;
	}

	Core::TArray<Asset::FLandscapeLayerInfo>& ULandscapeAsset::GetMutableLayers() {
		return mLayers;
	}

	const Core::TArray<Core::TArray<Uint8>>& ULandscapeAsset::GetLayerWeights() const {
		return mLayerWeights;
	}

	const Core::TArray<Uint8>& ULandscapeAsset::GetHoles() const {
		return mHoles;
	}

	FVector ULandscapeAsset::ComputeVertexLocalNormal(Uint32 X, Uint32 Y) const {
		const Uint32 ResX{GetTotalResolutionX()};
		const Uint32 ResY{GetTotalResolutionY()};
		const Uint32 LeftX{X > 0u ? X - 1u : 0u};
		const Uint32 RightX{std::min(X + 1u, ResX - 1u)};
		const Uint32 DownY{Y > 0u ? Y - 1u : 0u};
		const Uint32 UpY{std::min(Y + 1u, ResY - 1u)};

		const float SpanX{static_cast<float>(RightX - LeftX) * mQuadSize};
		const float SpanY{static_cast<float>(UpY - DownY) * mQuadSize};
		const float DzDx{SpanX > 0.0f ? (HeightSampleToLocalZ(GetHeightSample(RightX, Y)) - HeightSampleToLocalZ(GetHeightSample(LeftX, Y))) / SpanX : 0.0f};
		const float DzDy{SpanY > 0.0f ? (HeightSampleToLocalZ(GetHeightSample(X, UpY)) - HeightSampleToLocalZ(GetHeightSample(X, DownY))) / SpanY : 0.0f};

		return FVector{-DzDx, -DzDy, 1.0f}.Normalized();
	}

	void ULandscapeAsset::SyncWeightArraySizes() {
		const Index_t VertexCount{static_cast<Index_t>(GetTotalResolutionX()) * GetTotalResolutionY()};
		mLayerWeights.resize(mLayers.size());

		for (Index_t LayerIndex{}; LayerIndex < mLayerWeights.size(); ++LayerIndex) {
			if (mLayerWeights[LayerIndex].size() != VertexCount) {
				mLayerWeights[LayerIndex].assign(VertexCount, LayerIndex == 0u ? 255u : 0u);
			}
		}
	}

}
