/**
 * @file FSkyResources.cpp
 * @brief 등장방형 파노라마를 전체 화면 삼각형으로 그리는 기본 하늘 패스의 자원입니다.
 * @ingroup Renderer
 */

#include "Core/Types.h"
#include "FSkyResources.h"
#include "FScene.h"
#include "FPassTarget.h"
#include "Core/Base/ErrorHandler.h"
#include "Core/Literals/AngleFunctions.h"
#include "Core/Math/FMatrix.h"
#include "Engine/FSceneView.h"

#include "Asset/FAssetManager.h"
#include "Asset/UTexture2D.h"

namespace Renderer {

    /**
     * @brief 기본 설정으로 FSkyResources 객체를 초기화합니다.
     */
    Renderer::FSkyResources::FSkyResources() = default;

    /**
     * @brief 객체 수명을 끝내고 소유한 자원을 정리합니다.
     */
    Renderer::FSkyResources::~FSkyResources() = default;

    /**
     * @brief 상수 버퍼·샘플러·깊이·래스터라이저 상태를 생성하고 파노라마를 불러옵니다.
     */
    bool Renderer::FSkyResources::Initialize(ID3D11Device* Device, const std::filesystem::path& PreviewFallbackPath, const std::filesystem::path& PreviewPath) {
        D3D11_SAMPLER_DESC Sampler{};
        Sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        Sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        Sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.MaxLOD = D3D11_FLOAT32_MAX;

        D3D11_DEPTH_STENCIL_DESC Depth{};
        Depth.DepthEnable = true;
        Depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        Depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

        D3D11_RASTERIZER_DESC Rasterizer{};
        Rasterizer.FillMode = D3D11_FILL_SOLID;
        Rasterizer.CullMode = D3D11_CULL_NONE;

        Core::FString Error{};

        if (!mConstants.Create(Device, Error) || !mAtmosphereConstants.Create(Device, Error) || FAILED(Device->CreateSamplerState(&Sampler, mSampler.GetAddressOf())) || FAILED(Device->CreateDepthStencilState(&Depth, mDepthState.GetAddressOf())) || FAILED(Device->CreateRasterizerState(&Rasterizer, mRasterizer.GetAddressOf()))) {
            return false;
        }

        if (!LoadPanorama(Device, PreviewPath, mPreviewPanorama) && !LoadPanorama(Device, PreviewFallbackPath, mPreviewPanorama)) {
            Core::ErrorHandler::Report("Sky", "Cannot load the preview panorama: " + PreviewPath.string(), Core::ErrorHandler::EErrorLevel::Warning);
        }

        return true;
    }

    /**
     * @brief 정점·픽셀 바이트 코드로 하늘 셰이더를 생성하고 둘 다 성공한 경우에만 교체합니다.
     */
    bool Renderer::FSkyResources::CreateShaders(ID3D11Device* Device, std::span<const Uint8> VertexCode, std::span<const Uint8> PixelCode) {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> Vertex{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> Pixel{};

        if (FAILED(Device->CreateVertexShader(VertexCode.data(), VertexCode.size(), nullptr, Vertex.GetAddressOf())) || FAILED(Device->CreatePixelShader(PixelCode.data(), PixelCode.size(), nullptr, Pixel.GetAddressOf()))) {
            return false;
        }

        mVertexShader = std::move(Vertex);
        mPixelShader = std::move(Pixel);

        return true;
    }

    bool Renderer::FSkyResources::CreateAtmosphereShader(ID3D11Device* Device, std::span<const Uint8> PixelCode) {
        Microsoft::WRL::ComPtr<ID3D11PixelShader> Pixel{};

        if (FAILED(Device->CreatePixelShader(PixelCode.data(), PixelCode.size(), nullptr, Pixel.GetAddressOf()))) {
            return false;
        }

        mAtmospherePixelShader = std::move(Pixel);

        return true;
    }

    /**
     * @brief 불투명 패스의 깊이가 비어 있는 픽셀에만 하늘을 그립니다.
     */
    Uint32 Renderer::FSkyResources::Render(ID3D11Device* Device, ID3D11DeviceContext* Context, const Renderer::FPassTarget& Target, const Engine::FSceneView& View, const Renderer::FScene& Scene, bool Preview) {
        const Engine::FSkyDomeSceneProxy* SkyDome{Preview ? nullptr : Scene.GetSkyDome()};
        Asset::UTexture2D* SkyTexture{SkyDome != nullptr ? SkyDome->mPanorama.Get() : nullptr};
        ID3D11ShaderResourceView* Panorama{Preview ? mPreviewPanorama.Get() : nullptr};

        if (SkyTexture != nullptr && SkyTexture->CreateResources(Device)) {
            Panorama = SkyTexture->GetShaderResourceView();
        }

        const Engine::FSkyAtmosphereSceneProxy* Atmosphere{!Preview && Panorama == nullptr ? Scene.GetSkyAtmosphere() : nullptr};
        ID3D11PixelShader* PixelShader{Atmosphere != nullptr ? mAtmospherePixelShader.Get() : mPixelShader.Get()};

        if (mVertexShader == nullptr || PixelShader == nullptr || (Atmosphere == nullptr && Panorama == nullptr)) {
            return 0;
        }

        // 파노라마는 방향만 필요하므로 이동을 빼서 카메라가 멀리 있어도 정밀도를 유지합니다.
        FMatrix ViewRotation{View.mView};

        ViewRotation(3, 0) = 0.0f;
        ViewRotation(3, 1) = 0.0f;
        ViewRotation(3, 2) = 0.0f;

        FSkyConstants Constants{};

        if (SkyDome != nullptr && Panorama != nullptr) {
            ViewRotation = FMatrix::Rotation(SkyDome->mRotation) * ViewRotation;
            Constants.mPanoramaParameters.x = SkyTexture->IsSRGB() ? 1.0f : 0.0f;
        }

        if (!(ViewRotation * View.mProjection).TryInverse(Constants.mInverseViewRotationProjection)) {
            return 0;
        }

        mConstants.Update(Context, Constants);
        Context->OMSetRenderTargets(1, &Target.mColor, Target.mDepth);
        Context->OMSetDepthStencilState(mDepthState.Get(), 0);
        Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        Context->RSSetViewports(1, &Target.mViewport);
        Context->RSSetState(mRasterizer.Get());
        Context->IASetInputLayout(nullptr);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->VSSetShader(mVertexShader.Get(), nullptr, 0);
        Context->PSSetShader(PixelShader, nullptr, 0);
        mConstants.Bind(Context, RenderCore::EShaderStage::Pixel, 0);

        if (Atmosphere != nullptr) {
            BindAtmosphere(Context, View, *Atmosphere, Scene.GetAtmosphereSunLight());
            Context->Draw(3, 0);
        } else {
            ID3D11SamplerState* Sampler{mSampler.Get()};
            ID3D11ShaderResourceView* Empty{};

            Context->PSSetSamplers(0, 1, &Sampler);
            Context->PSSetShaderResources(0, 1, &Panorama);
            Context->Draw(3, 0);
            Context->PSSetShaderResources(0, 1, &Empty);
        }

        return 1;
    }

    void Renderer::FSkyResources::BindAtmosphere(ID3D11DeviceContext* Context, const Engine::FSceneView& View, const Engine::FSkyAtmosphereSceneProxy& Atmosphere, const Engine::FLightSceneProxy* Sun) {
        constexpr float WorldUnitsPerKilometer{static_cast<float>(Core::QuantityUnits::Kilometer.mScale)};
        const float GroundRadius{Atmosphere.mGroundRadius.GetBase() / WorldUnitsPerKilometer};
        FVector CameraPosition{(View.mLocation - Atmosphere.mGroundPosition) / WorldUnitsPerKilometer};
        CameraPosition.z += GroundRadius;

        FSkyAtmosphereConstants Constants{};
        Constants.mCameraPositionAndGroundRadius = {CameraPosition, GroundRadius};
        Constants.mSunDirectionAndAtmosphereHeight = {Sun != nullptr ? -Sun->mDirection : FVector{0.0f, 0.0f, 1.0f}, Atmosphere.mAtmosphereHeight.GetBase() / WorldUnitsPerKilometer};
        Constants.mSunColorAndMieAnisotropy = {Sun != nullptr ? FVector{Sun->mColor.x, Sun->mColor.y, Sun->mColor.z} * Sun->mIntensity : FVector{}, Atmosphere.mMieAnisotropy};
        Constants.mDensityScaleHeightsAndScatteringScales = {Atmosphere.mRayleighScaleHeight.GetBase() / WorldUnitsPerKilometer, Atmosphere.mMieScaleHeight.GetBase() / WorldUnitsPerKilometer, Atmosphere.mRayleighScatteringScale, Atmosphere.mMieScatteringScale};
        const float SunDiskChord{Sun != nullptr ? 2.0f * Core::Math::Sin(Sun->mShadowSourceAngle * 0.5f) : 0.0f};
        Constants.mOzoneAbsorptionScaleAndSunDisk = {Atmosphere.mOzoneAbsorptionScale, SunDiskChord * SunDiskChord, 0.0f, 0.0f};

        mAtmosphereConstants.Update(Context, Constants);
        mAtmosphereConstants.Bind(Context, RenderCore::EShaderStage::Pixel, 1);
    }

    /**
     * @brief 하늘 텍스처를 표시 공간의 값으로 샘플링하도록 GPU 자원을 생성합니다.
     */
    bool Renderer::FSkyResources::LoadPanorama(ID3D11Device* Device, const std::filesystem::path& Path, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& Panorama) {
        Asset::FAssetManager Assets{Path.parent_path()};
        Core::FString Error{};
        auto* Texture{Assets.LoadTexture(Path, Error)};
        if (Texture == nullptr || !Texture->SetSRGB(false) || !Texture->CreateResources(Device)) {
            return false;
        }

        Panorama = Texture->GetShaderResourceView();

        return Panorama != nullptr;
    }

}
