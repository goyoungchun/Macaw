#pragma once

#include "FLandscapeConstants.h"
#include "RenderCore/TConstantBuffer.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace Asset {
	class UTexture2D;
}

namespace Engine {
	struct FLandscapeComponentRenderData;
}

namespace Renderer {

	// 지형 렌더링용 상수 버퍼와 샘플러 및 슬롯 바인딩 관리자
	class FLandscapeResources {
	public:
		FLandscapeResources() = default;
		~FLandscapeResources() = default;

	public:
		bool Initialize(ID3D11Device* Device);
		void Release();

		// 정점 셰이더 상수와 하이트맵 텍스처 바인딩
		bool BindVertex(ID3D11Device* Device, ID3D11DeviceContext* Context, const Engine::FLandscapeComponentRenderData& Data, const Renderer::FLandscapeDrawInfo& DrawInfo);

		// 픽셀 셰이더 멀티 레이어 상수와 웨이트맵 및 레이어 텍스처 바인딩
		bool BindPixel(ID3D11Device* Device, ID3D11DeviceContext* Context, const Engine::FLandscapeComponentRenderData& Data, Asset::UTexture2D* DefaultWhiteTexture);

		// 지형 전용 슬롯 정리
		void Unbind(ID3D11DeviceContext* Context) const;

	private:
		RenderCore::TConstantBuffer<Renderer::FLandscapeVertexConstants> mVertexBuffer{};
		RenderCore::TConstantBuffer<Renderer::FLandscapePixelConstants> mPixelBuffer{};
		Microsoft::WRL::ComPtr<ID3D11SamplerState> mClampSampler{};
	};

}
