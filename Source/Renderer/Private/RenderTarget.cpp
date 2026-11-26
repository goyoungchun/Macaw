/**
 * @file RenderTarget.cpp
 * @brief Direct3D 11 오프스크린 렌더 타깃을 구현합니다.
 * @ingroup Renderer
 */

#include "RenderTarget.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Core/Types.h"

#include <algorithm>

namespace Renderer {

    /**
     * @brief FDXRenderTarget의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Renderer::FDXRenderTarget::FDXRenderTarget(bool ForwardShading)
        : mForwardShading(ForwardShading) {
    }

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Renderer::FDXRenderTarget::~FDXRenderTarget() = default;

    /**
     * @brief 새 크기에 맞추어 렌더 타깃 리소스를 생성하거나 교체합니다.
     */
    bool Renderer::FDXRenderTarget::Resize(ID3D11Device* Device, Uint32 Width, Uint32 Height) {
        if (Device == nullptr || Width == 0 || Height == 0 || Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
            return false;
        }

        const bool DeferredResourcesReady{mForwardShading || (mReadOnlyDepthView != nullptr && mStencilResource != nullptr && std::all_of(mGBuffer.begin(), mGBuffer.end(), [](const std::unique_ptr<Asset::UTexture2D>& Attachment) {
            return Attachment != nullptr && Attachment->HasResources();
        }))};

        if (Width == mWidth && Height == mHeight && mColor != nullptr && mDepth != nullptr && mSceneColor != nullptr && mPostProcessColor != nullptr && mColor->HasResources() && mDepth->HasResources() && mSceneColor->HasResources() && mPostProcessColor->HasResources() && DeferredResourcesReady) {
            Microsoft::WRL::ComPtr<ID3D11Device> ResourceDevice{};

            mColor->GetTexture()->GetDevice(ResourceDevice.GetAddressOf());

            if (ResourceDevice.Get() == Device) {
                return true;
            }
        }

        auto Color{CoreUObject::NewObject<Asset::UTexture2D>()};
        auto Depth{CoreUObject::NewObject<Asset::UTexture2D>()};
        auto SceneColor{CoreUObject::NewObject<Asset::UTexture2D>()};
        auto PostProcessColor{CoreUObject::NewObject<Asset::UTexture2D>()};

        if (!Color->Configure(Width, Height, Asset::ETextureFormat::RGBA8, Asset::ETextureUsage::ShaderResource | Asset::ETextureUsage::RenderTarget) || !Depth->Configure(Width, Height, Asset::ETextureFormat::D24S8, Asset::ETextureUsage::DepthStencil | Asset::ETextureUsage::ShaderResource) || !SceneColor->Configure(Width, Height, Asset::ETextureFormat::RGBA8, Asset::ETextureUsage::ShaderResource | Asset::ETextureUsage::RenderTarget) || !PostProcessColor->Configure(Width, Height, Asset::ETextureFormat::RGBA8, Asset::ETextureUsage::ShaderResource | Asset::ETextureUsage::RenderTarget) || !Color->CreateResources(Device) || !Depth->CreateResources(Device) || !SceneColor->CreateResources(Device) || !PostProcessColor->CreateResources(Device)) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> ReadOnlyDepthView{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> StencilResource{};
        Core::TStaticArray<std::unique_ptr<Asset::UTexture2D>, Renderer::GBuffer::AttachmentCount> GBuffer{};

        if (!mForwardShading) {
            D3D11_DEPTH_STENCIL_VIEW_DESC ReadOnlyDescription{};
            Depth->GetDepthStencilView()->GetDesc(&ReadOnlyDescription);
            ReadOnlyDescription.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;

            D3D11_SHADER_RESOURCE_VIEW_DESC StencilDescription{};
            StencilDescription.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
            StencilDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            StencilDescription.Texture2D.MipLevels = 1;

            if (FAILED(Device->CreateDepthStencilView(Depth->GetTexture(), &ReadOnlyDescription, ReadOnlyDepthView.GetAddressOf())) || FAILED(Device->CreateShaderResourceView(Depth->GetTexture(), &StencilDescription, StencilResource.GetAddressOf()))) {
                return false;
            }

            for (auto& Attachment : GBuffer) {
                Attachment = CoreUObject::NewObject<Asset::UTexture2D>();

                if (!Attachment->Configure(Width, Height, Asset::ETextureFormat::RGBA16Float, Asset::ETextureUsage::ShaderResource | Asset::ETextureUsage::RenderTarget) || !Attachment->CreateResources(Device)) {
                    return false;
                }
            }
        }

        mColor = std::move(Color);
        mDepth = std::move(Depth);
        mReadOnlyDepthView = std::move(ReadOnlyDepthView);
        mStencilResource = std::move(StencilResource);
        mGBuffer = std::move(GBuffer);
        mDecalNormalSource.reset();
        mDecalNormalHistory.reset();
        mSceneColor = std::move(SceneColor);
        mPostProcessColor = std::move(PostProcessColor);
        mDepthOfFieldTargets.reset();

        mWidth = Width;
        mHeight = Height;

        return true;
    }

    /**
     * @brief 가로 픽셀 수를 반환합니다.
     */
    Uint32 Renderer::FDXRenderTarget::GetWidth() const {
        return mWidth;
    }

    /**
     * @brief 세로 픽셀 수를 반환합니다.
     */
    Uint32 Renderer::FDXRenderTarget::GetHeight() const {
        return mHeight;
    }

    /**
     * @brief UI에서 샘플링할 셰이더 리소스 뷰의 비소유 주소를 반환합니다.
     */
    void* Renderer::FDXRenderTarget::GetNativeTexture() const {
        return mColor != nullptr ? mColor->GetShaderResourceView() : nullptr;
    }

    /**
     * @brief 컬러 출력용 Direct3D 11 렌더 타깃 뷰의 비소유 주소를 반환합니다.
     */
    ID3D11RenderTargetView* Renderer::FDXRenderTarget::GetRenderTargetView() const {
        return mColor != nullptr ? mColor->GetRenderTargetView() : nullptr;
    }

    /**
     * @brief 깊이 스텐실 뷰를 반환합니다.
     */
    ID3D11DepthStencilView* Renderer::FDXRenderTarget::GetDepthView() const {
        return mDepth != nullptr ? mDepth->GetDepthStencilView() : nullptr;
    }

    ID3D11DepthStencilView* Renderer::FDXRenderTarget::GetReadOnlyDepthView() const {
        return mReadOnlyDepthView.Get();
    }

    ID3D11ShaderResourceView* Renderer::FDXRenderTarget::GetStencilResource() const {
        return mStencilResource.Get();
    }

    const Core::TStaticArray<std::unique_ptr<Asset::UTexture2D>, Renderer::GBuffer::AttachmentCount>& Renderer::FDXRenderTarget::GetGBuffer() const {
        return mGBuffer;
    }

    Asset::UTexture2D* Renderer::FDXRenderTarget::GetDecalNormalSource(ID3D11Device* Device, bool Accumulated) {
        auto& NormalSource{Accumulated ? mDecalNormalHistory : mDecalNormalSource};

        if (NormalSource == nullptr) {
            auto Source{CoreUObject::NewObject<Asset::UTexture2D>()};

            if (!Source->Configure(mWidth, mHeight, mGBuffer[Renderer::GBuffer::NormalAttachment]->GetFormat(), Asset::ETextureUsage::ShaderResource) || !Source->CreateResources(Device)) {
                return nullptr;
            }

            NormalSource = std::move(Source);
        }

        return NormalSource.get();
    }

    /**
     * @brief 현재 보유한 GPU 리소스의 추정 바이트 수를 반환합니다.
     */
    std::size_t Renderer::FDXRenderTarget::GetResourceBytes() const {
        std::size_t Bytes{(mColor != nullptr ? mColor->GetResourceBytes() : 0) + (mDepth != nullptr ? mDepth->GetResourceBytes() : 0) + (mSceneColor != nullptr ? mSceneColor->GetResourceBytes() : 0) + (mPostProcessColor != nullptr ? mPostProcessColor->GetResourceBytes() : 0)};

        for (const auto& Attachment : mGBuffer) {
            Bytes += Attachment != nullptr ? Attachment->GetResourceBytes() : 0;
        }

        Bytes += mDecalNormalSource != nullptr ? mDecalNormalSource->GetResourceBytes() : 0;
        Bytes += mDecalNormalHistory != nullptr ? mDecalNormalHistory->GetResourceBytes() : 0;

        if (mDepthOfFieldTargets != nullptr) {
            for (Index_t Index{}; Index < mDepthOfFieldTargets->mSeparated.size(); ++Index) {
                Bytes += mDepthOfFieldTargets->mSeparated[Index]->GetResourceBytes() + mDepthOfFieldTargets->mBlurred[Index]->GetResourceBytes();
            }

            Bytes += mDepthOfFieldTargets->mCircleOfConfusion->GetResourceBytes();
        }

        return Bytes;
    }

    /**
     * @brief 장면 드로와 교대 후처리 입력으로 사용하는 컬러 텍스처를 조회합니다.
     */
    Asset::UTexture2D* Renderer::FDXRenderTarget::GetSceneColor() const {
        return mSceneColor.get();
    }

    /**
     * @brief 다중 안개 합성에 사용하는 교대 컬러 텍스처를 조회합니다.
     */
    Asset::UTexture2D* Renderer::FDXRenderTarget::GetPostProcessColor() const {
        return mPostProcessColor.get();
    }

    /**
     * @brief 에디터와 UI에 표시하는 최종 컬러 텍스처를 조회합니다.
     */
    Asset::UTexture2D* Renderer::FDXRenderTarget::GetOutputColor() const {
        return mColor.get();
    }

    /**
     * @brief 깊이 검사와 후처리 샘플링에 공유하는 깊이 텍스처를 조회합니다.
     */
    Asset::UTexture2D* Renderer::FDXRenderTarget::GetSceneDepth() const {
        return mDepth.get();
    }

    const Renderer::FDXRenderTarget::FDepthOfFieldTargets* Renderer::FDXRenderTarget::PrepareDepthOfFieldTargets() {
        if (mDepthOfFieldTargets != nullptr || mColor == nullptr) {
            return mDepthOfFieldTargets.get();
        }

        Microsoft::WRL::ComPtr<ID3D11Device> Device{};
        mColor->GetTexture()->GetDevice(Device.GetAddressOf());

        const Uint32 Width{(mWidth + 1) / 2};
        const Uint32 Height{(mHeight + 1) / 2};
        const Asset::ETextureUsage Usage{Asset::ETextureUsage::ShaderResource | Asset::ETextureUsage::RenderTarget};
        auto Targets{std::make_unique<FDepthOfFieldTargets>()};

        for (auto* Layers : {&Targets->mSeparated, &Targets->mBlurred}) {
            for (auto& Layer : *Layers) {
                Layer = CoreUObject::NewObject<Asset::UTexture2D>();

                if (!Layer->Configure(Width, Height, Asset::ETextureFormat::RGBA16Float, Usage) || !Layer->CreateResources(Device.Get())) {
                    return nullptr;
                }
            }
        }

        Targets->mCircleOfConfusion = CoreUObject::NewObject<Asset::UTexture2D>();

        if (!Targets->mCircleOfConfusion->Configure(Width, Height, Asset::ETextureFormat::RGBA16Float, Usage) || !Targets->mCircleOfConfusion->CreateResources(Device.Get())) {
            return nullptr;
        }

        mDepthOfFieldTargets = std::move(Targets);

        return mDepthOfFieldTargets.get();
    }

}
