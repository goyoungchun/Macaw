#include "FLandscapeResources.h"

#include "Asset/UMaterial.h"
#include "Asset/UTexture2D.h"
#include "Engine/FLandscapeRenderData.h"
#include "RenderCore/EShaderStage.h"

#include <algorithm>

namespace Renderer {

	namespace {
		constexpr UINT HeightmapVertexSlot{13u};
		constexpr UINT VertexConstantSlot{5u};
		constexpr UINT PixelConstantSlot{6u};
		constexpr UINT ClampSamplerSlot{4u};
		constexpr UINT WeightmapStartSlot{20u};
		constexpr UINT LayerAlbedoStartSlot{22u};
		constexpr UINT LayerNormalStartSlot{30u};
		constexpr UINT TotalPixelResourceCount{18u};
	}

	bool FLandscapeResources::Initialize(ID3D11Device* Device) {
		if (Device == nullptr) {
			return false;
		}

		Core::FString Error{};

		if (!mVertexBuffer.Create(Device, Error) || !mPixelBuffer.Create(Device, Error)) {
			return false;
		}

		D3D11_SAMPLER_DESC Desc{};
		Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		Desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.MaxLOD = D3D11_FLOAT32_MAX;

		return SUCCEEDED(Device->CreateSamplerState(&Desc, mClampSampler.GetAddressOf()));
	}

	void FLandscapeResources::Release() {
		mVertexBuffer.Reset();
		mPixelBuffer.Reset();
		mClampSampler.Reset();
	}

	bool FLandscapeResources::BindVertex(ID3D11Device* Device, ID3D11DeviceContext* Context, const Engine::FLandscapeComponentRenderData& Data, const Renderer::FLandscapeDrawInfo& DrawInfo) {
		if (Device == nullptr || Context == nullptr || Data.mShared == nullptr) {
			return false;
		}

		const Engine::FLandscapeSharedRenderData& Shared{*Data.mShared};
		Asset::UTexture2D* Heightmap{Shared.mHeightmapTexture.Get()};

		if (Heightmap == nullptr || (!Heightmap->HasResources() && !Heightmap->CreateResources(Device))) {
			return false;
		}

		const Uint32 MaxLOD{Shared.mMaxLOD};
		const Uint32 ClampedLOD{std::min(DrawInfo.mLOD, MaxLOD)};
		const Uint32 CoarseLOD{std::min(ClampedLOD + 1u, MaxLOD)};
		const Uint32 CurrentQuads{ClampedLOD < Shared.mLODRanges.size() ? Shared.mLODRanges[ClampedLOD].mQuadsPerSide : Shared.mSubsectionSizeQuads};
		const Uint32 CoarseQuads{CoarseLOD < Shared.mLODRanges.size() ? Shared.mLODRanges[CoarseLOD].mQuadsPerSide : std::max<Uint32>(1u, CurrentQuads / 2u)};
		const float SubWorldSize{static_cast<float>(Shared.mSubsectionSizeQuads) * Shared.mQuadSize};

		Renderer::FLandscapeVertexConstants Constants{};
		Constants.mSubsectionParams = {
			static_cast<float>(DrawInfo.mSubX) * SubWorldSize,
			static_cast<float>(DrawInfo.mSubY) * SubWorldSize,
			SubWorldSize,
			Shared.mHeightScale};
		Constants.mHeightmapParams = {
			static_cast<float>(Shared.mTotalResolutionX),
			static_cast<float>(Shared.mTotalResolutionY),
			static_cast<float>(Data.mSectionBaseX + DrawInfo.mSubX * Shared.mSubsectionSizeQuads),
			static_cast<float>(Data.mSectionBaseY + DrawInfo.mSubY * Shared.mSubsectionSizeQuads)};
		Constants.mComponentParams = {
			static_cast<float>(DrawInfo.mSubX),
			static_cast<float>(DrawInfo.mSubY),
			1.0f / static_cast<float>(std::max<Uint32>(1u, Shared.mNumSubsections)),
			static_cast<float>(Shared.mSubsectionSizeQuads)};
		Constants.mLODParams = {
			static_cast<float>(CurrentQuads),
			static_cast<float>(CoarseQuads),
			ClampedLOD < MaxLOD ? DrawInfo.mMorphFactor : 0.0f,
			0.0f};
		Constants.mStitchParams = {DrawInfo.mStitchMask, 0u, 0u, 0u};

		mVertexBuffer.Update(Context, Constants);
		mVertexBuffer.Bind(Context, RenderCore::EShaderStage::Vertex, VertexConstantSlot);

		ID3D11ShaderResourceView* HeightSRV{Heightmap->GetShaderResourceView()};
		Context->VSSetShaderResources(HeightmapVertexSlot, 1u, &HeightSRV);

		return true;
	}

	bool FLandscapeResources::BindPixel(ID3D11Device* Device, ID3D11DeviceContext* Context, const Engine::FLandscapeComponentRenderData& Data, Asset::UTexture2D* DefaultWhiteTexture) {
		if (Device == nullptr || Context == nullptr || Data.mShared == nullptr) {
			return false;
		}

		const Engine::FLandscapeSharedRenderData& Shared{*Data.mShared};
		ID3D11ShaderResourceView* DefaultSRV{DefaultWhiteTexture != nullptr ? DefaultWhiteTexture->GetShaderResourceView() : nullptr};

		Core::TStaticArray<ID3D11ShaderResourceView*, 2> WeightSRVs{};
		Uint32 ValidWeightmaps{};

		for (Index_t Index{}; Index < Data.mWeightmapTextures.size() && Index < WeightSRVs.size(); ++Index) {
			Asset::UTexture2D* WeightTex{Data.mWeightmapTextures[Index].Get()};

			if (WeightTex != nullptr && (WeightTex->HasResources() || WeightTex->CreateResources(Device))) {
				WeightSRVs[Index] = WeightTex->GetShaderResourceView();
				++ValidWeightmaps;
			}
		}

		const Uint32 LayerCount{static_cast<Uint32>(std::min<Index_t>(Data.mLayerAllocations.size(), Renderer::FLandscapePixelConstants::MaxLayers))};
		const float CompQuads{static_cast<float>(Shared.mComponentSizeQuads)};
		const float CompRes{CompQuads + 1.0f};

		Renderer::FLandscapePixelConstants Constants{};
		Constants.mLayerMeta = {LayerCount, ValidWeightmaps, Shared.mComponentSizeQuads, 0u};
		Constants.mWeightmapScaleBias = {CompQuads / CompRes, CompQuads / CompRes, 0.5f / CompRes, 0.5f / CompRes};

		Core::TStaticArray<ID3D11ShaderResourceView*, Renderer::FLandscapePixelConstants::MaxLayers> AlbedoSRVs{};
		Core::TStaticArray<ID3D11ShaderResourceView*, Renderer::FLandscapePixelConstants::MaxLayers> NormalSRVs{};
		AlbedoSRVs.fill(DefaultSRV);

		for (Uint32 Slot{}; Slot < LayerCount; ++Slot) {
			const Engine::FLandscapeLayerAllocation& Allocation{Data.mLayerAllocations[Slot]};
			Asset::UMaterial* LayerMat{Allocation.mMaterial.Get()};

			if (LayerMat != nullptr) {
				Constants.mLayerBaseColor[Slot] = LayerMat->GetBaseColor();
				Constants.mLayerSpecularColor[Slot] = LayerMat->GetSpecularColor();

				Asset::UTexture2D* AlbedoTex{LayerMat->GetBaseColorTexture()};

				if (AlbedoTex != nullptr && (AlbedoTex->HasResources() || AlbedoTex->CreateResources(Device))) {
					AlbedoSRVs[Slot] = AlbedoTex->GetShaderResourceView();
				}

				Asset::UTexture2D* NormalTex{LayerMat->GetNormalMapTexture()};
				const bool HasNormal{NormalTex != nullptr && (NormalTex->HasResources() || NormalTex->CreateResources(Device))};

				if (HasNormal) {
					NormalSRVs[Slot] = NormalTex->GetShaderResourceView();
				}

				Constants.mLayerSurfaceParams[Slot] = {
					LayerMat->GetSpecularStrength(),
					LayerMat->GetShininess(),
					LayerMat->GetNormalStrength(),
					HasNormal ? 1.0f : 0.0f};
			} else {
				Constants.mLayerBaseColor[Slot] = {0.42f, 0.55f, 0.34f, 1.0f};
				Constants.mLayerSpecularColor[Slot] = {1.0f, 1.0f, 1.0f, 1.0f};
				Constants.mLayerSurfaceParams[Slot] = {0.15f, 16.0f, 1.0f, 0.0f};
			}

			Constants.mLayerUVAndChannel[Slot] = {
				Allocation.mUVScale.x,
				Allocation.mUVScale.y,
				static_cast<float>(Allocation.mWeightmapIndex),
				static_cast<float>(Allocation.mWeightmapChannel)};
		}

		mPixelBuffer.Update(Context, Constants);
		mPixelBuffer.Bind(Context, RenderCore::EShaderStage::Pixel, PixelConstantSlot);

		ID3D11SamplerState* ClampSampler{mClampSampler.Get()};
		Context->PSSetSamplers(ClampSamplerSlot, 1u, &ClampSampler);
		Context->PSSetShaderResources(WeightmapStartSlot, static_cast<UINT>(WeightSRVs.size()), WeightSRVs.data());
		Context->PSSetShaderResources(LayerAlbedoStartSlot, static_cast<UINT>(AlbedoSRVs.size()), AlbedoSRVs.data());
		Context->PSSetShaderResources(LayerNormalStartSlot, static_cast<UINT>(NormalSRVs.size()), NormalSRVs.data());

		return true;
	}

	void FLandscapeResources::Unbind(ID3D11DeviceContext* Context) const {
		if (Context == nullptr) {
			return;
		}

		ID3D11ShaderResourceView* EmptyHeight{};
		Context->VSSetShaderResources(HeightmapVertexSlot, 1u, &EmptyHeight);
		RenderCore::TConstantBuffer<Renderer::FLandscapeVertexConstants>::Unbind(Context, RenderCore::EShaderStage::Vertex, VertexConstantSlot);
		RenderCore::TConstantBuffer<Renderer::FLandscapePixelConstants>::Unbind(Context, RenderCore::EShaderStage::Pixel, PixelConstantSlot);

		Core::TStaticArray<ID3D11ShaderResourceView*, TotalPixelResourceCount> EmptyPixelResources{};
		Context->PSSetShaderResources(WeightmapStartSlot, static_cast<UINT>(EmptyPixelResources.size()), EmptyPixelResources.data());
	}

}
