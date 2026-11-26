#include "FParticleRenderer.h"
#include "RendererStats.h"
#include "Asset/UMaterial.h"
#include "Engine/FSceneView.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Renderer {

    namespace {

        constexpr Uint32 DefaultTextureSize{64};

        /**
         * @brief 되돌아가는 꼭짓점에서 폭을 이 배율까지 줄입니다.
         */
        constexpr float MinimumPinch{0.1f};

        // 90°보다 더 꺾인 점에서 폭 배율을 줄여, 되돌아가는 꼭짓점에서 폭이 진행 방향으로 누워 띠가 접히는 것을 막습니다.
        float ComputePinch(std::span<const Engine::FParticleTrailPoint> Points, Index_t Index) {
            if (Index == 0 || Index + 1 >= Points.size()) {
                return 1.0f;
            }

            const FVector In{Points[Index].mPosition - Points[Index - 1].mPosition};
            const FVector Out{Points[Index + 1].mPosition - Points[Index].mPosition};

            if (In.Length() < 0.0001f || Out.Length() < 0.0001f) {
                return 1.0f;
            }

            const float Cosine{In.Normalized().Dot(Out.Normalized())};

            return Cosine >= 0.0f ? 1.0f : MinimumPinch + (1.0f - MinimumPinch) * (Cosine + 1.0f);
        }

    }

    Renderer::FParticleRenderer::FParticleRenderer() = default;

    Renderer::FParticleRenderer::~FParticleRenderer() = default;

    bool Renderer::FParticleRenderer::Initialize(ID3D11Device* Device) {
        D3D11_SAMPLER_DESC Sampler{};
        Sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        Sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.MaxLOD = D3D11_FLOAT32_MAX;

        Core::FString Error{};

        // 띠는 길이 방향으로 텍스처를 반복하므로 U만 감쌉니다.
        D3D11_SAMPLER_DESC TrailSampler{Sampler};
        TrailSampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;

        if (!mConstants.Create(Device, Error) || FAILED(Device->CreateSamplerState(&Sampler, mSampler.GetAddressOf())) || FAILED(Device->CreateSamplerState(&TrailSampler, mTrailSampler.GetAddressOf()))) {
            return false;
        }

        // 머티리얼이 없어도 보이도록 스프라이트는 가장자리로 갈수록 투명해지는 흰 원, 띠는 폭 방향으로만 투명해지는 흰 띠를 씁니다.
        const auto MakeTexture{[Device](bool Trail) -> std::unique_ptr<Asset::UTexture2D> {
            Core::TArray<Uint8> Pixels(static_cast<Index_t>(DefaultTextureSize) * DefaultTextureSize * 4);

            for (Uint32 Y{}; Y < DefaultTextureSize; ++Y) {
                for (Uint32 X{}; X < DefaultTextureSize; ++X) {
                    const float U{Trail ? 0.0f : (static_cast<float>(X) + 0.5f) / static_cast<float>(DefaultTextureSize) * 2.0f - 1.0f};
                    const float V{(static_cast<float>(Y) + 0.5f) / static_cast<float>(DefaultTextureSize) * 2.0f - 1.0f};
                    const float Falloff{std::clamp(1.0f - std::sqrt(U * U + V * V), 0.0f, 1.0f)};
                    const Index_t Offset{(static_cast<Index_t>(Y) * DefaultTextureSize + X) * 4};

                    Pixels[Offset] = 255;
                    Pixels[Offset + 1] = 255;
                    Pixels[Offset + 2] = 255;
                    Pixels[Offset + 3] = static_cast<Uint8>(std::lround(255.0f * Falloff * Falloff));
                }
            }

            auto Texture{CoreUObject::NewObject<Asset::UTexture2D>()};

            if (!Texture->SetPixels(DefaultTextureSize, DefaultTextureSize, Asset::ETextureFormat::RGBA8, std::move(Pixels)) || !Texture->CreateResources(Device)) {
                return nullptr;
            }

            return Texture;
        }};

        mDefaultTexture = MakeTexture(false);
        mDefaultTrailTexture = MakeTexture(true);

        return mDefaultTexture != nullptr && mDefaultTrailTexture != nullptr;
    }

    bool Renderer::FParticleRenderer::CreateShaders(ID3D11Device* Device, std::span<const Uint8> VertexCode, std::span<const Uint8> PixelCode) {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> Vertex{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> Pixel{};

        if (FAILED(Device->CreateVertexShader(VertexCode.data(), VertexCode.size(), nullptr, Vertex.GetAddressOf())) || FAILED(Device->CreatePixelShader(PixelCode.data(), PixelCode.size(), nullptr, Pixel.GetAddressOf()))) {
            return false;
        }

        mVertexShader = std::move(Vertex);
        mPixelShader = std::move(Pixel);

        return true;
    }

    bool Renderer::FParticleRenderer::HasShaders() const {
        return mVertexShader != nullptr && mPixelShader != nullptr;
    }

    bool Renderer::FParticleRenderer::CreateTrailShaders(ID3D11Device* Device, std::span<const Uint8> VertexCode, std::span<const Uint8> PixelCode) {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> Vertex{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> Pixel{};

        if (FAILED(Device->CreateVertexShader(VertexCode.data(), VertexCode.size(), nullptr, Vertex.GetAddressOf())) || FAILED(Device->CreatePixelShader(PixelCode.data(), PixelCode.size(), nullptr, Pixel.GetAddressOf()))) {
            return false;
        }

        mTrailVertexShader = std::move(Vertex);
        mTrailPixelShader = std::move(Pixel);

        return true;
    }

    bool Renderer::FParticleRenderer::DrawTrail(ID3D11DeviceContext* Context, const Engine::FParticleTrailBatch& Batch, const Engine::FSceneView& View) {
        if (mTrailVertexShader == nullptr || mTrailPixelShader == nullptr || Batch.mStripEnds.empty()) {
            return false;
        }

        // 띠 단위로 정렬합니다. 반투명이라 먼 띠부터 쌓습니다.
        const bool Sorted{Batch.mSortMode == Engine::EParticleSortMode::ViewProjDepth || Batch.mSortMode == Engine::EParticleSortMode::DistanceToView};

        mSortKeys.resize(Batch.mStripEnds.size());

        for (Index_t Strip{}; Strip < Batch.mStripEnds.size(); ++Strip) {
            const Uint32 Begin{Strip > 0 ? Batch.mStripEnds[Strip - 1] : 0u};
            float Key{};

            if (Sorted && Begin < Batch.mPoints.size()) {
                const FVector Delta{Batch.mPoints[Begin].mPosition - View.mLocation};

                Key = Batch.mSortMode == Engine::EParticleSortMode::ViewProjDepth ? Delta.Dot(View.mForward) : Delta.LengthSquared();
            }

            mSortKeys[Strip] = {Key, static_cast<Uint32>(Strip)};
        }

        if (Sorted) {
            std::ranges::stable_sort(mSortKeys, [](const auto& A, const auto& B) {
                return A.first > B.first;
            });
        }

        mTrailScratch.clear();

        for (const auto& Entry : mSortKeys) {
            const Uint32 Strip{Entry.second};
            const Uint32 Begin{Strip > 0 ? Batch.mStripEnds[Strip - 1] : 0u};
            const Uint32 End{Batch.mStripEnds[Strip]};

            if (End > Begin + 1 && End <= Batch.mPoints.size()) {
                AppendStrip(std::span<const Engine::FParticleTrailPoint>{Batch.mPoints.data() + Begin, End - Begin}, std::max<Uint32>(Batch.mSheets, 1), View);
            }
        }

        if (mTrailScratch.empty() || !BindCommon(Context, Batch.mMaterial.Get(), View, {1.0f, 1.0f, 1.0f, 1.0f}, true)) {
            return false;
        }

        Core::FString Error{};

        if (!mTrailVertices.Upload(Context, std::span<const FTrailVertex>{mTrailScratch}, Error)) {
            return false;
        }

        Context->VSSetShader(mTrailVertexShader.Get(), nullptr, 0);
        Context->PSSetShader(mTrailPixelShader.Get(), nullptr, 0);
        mTrailVertices.Bind(Context, RenderCore::EShaderStage::Vertex, 1);
        Context->Draw(static_cast<UINT>(mTrailScratch.size()), 0);
        Renderer::RendererStats::Triangles.Add(static_cast<double>(mTrailScratch.size() / 3));

        ID3D11ShaderResourceView* Empty{};

        Context->PSSetShaderResources(0, 1, &Empty);
        RenderCore::TStructuredBuffer<FTrailVertex, RenderCore::EBufferUpdate::Dynamic>::Unbind(Context, RenderCore::EShaderStage::Vertex, 1);

        return true;
    }

    bool Renderer::FParticleRenderer::Draw(ID3D11DeviceContext* Context, const Engine::FParticleSpriteBatch& Batch, const Engine::FSceneView& View) {
        if (!HasShaders() || Batch.mInstances.empty()) {
            return false;
        }

        const float Horizontal{static_cast<float>(std::max<Uint32>(Batch.mSubImagesHorizontal, 1))};
        const float Vertical{static_cast<float>(std::max<Uint32>(Batch.mSubImagesVertical, 1))};

        if (!BindCommon(Context, Batch.mMaterial.Get(), View, {Horizontal, Vertical, 1.0f / Horizontal, 1.0f / Vertical}, false)) {
            return false;
        }

        SortInstances(Batch, View);

        Core::FString Error{};

        if (!mInstances.Upload(Context, std::span<const Engine::FParticleSpriteInstance>{mScratch}, Error)) {
            return false;
        }

        Context->VSSetShader(mVertexShader.Get(), nullptr, 0);
        Context->PSSetShader(mPixelShader.Get(), nullptr, 0);
        mInstances.Bind(Context, RenderCore::EShaderStage::Vertex, 1);
        Context->DrawInstanced(6, static_cast<UINT>(mScratch.size()), 0, 0);
        Renderer::RendererStats::Triangles.Add(2.0 * static_cast<double>(mScratch.size()));

        ID3D11ShaderResourceView* Empty{};

        Context->PSSetShaderResources(0, 1, &Empty);
        RenderCore::TStructuredBuffer<Engine::FParticleSpriteInstance, RenderCore::EBufferUpdate::Dynamic>::Unbind(Context, RenderCore::EShaderStage::Vertex, 1);

        return true;
    }

    std::size_t Renderer::FParticleRenderer::GetResourceBytes() const {
        return sizeof(FConstants) + mInstances.GetResourceBytes() + mTrailVertices.GetResourceBytes() + (mDefaultTexture != nullptr ? mDefaultTexture->GetResourceBytes() : 0) + (mDefaultTrailTexture != nullptr ? mDefaultTrailTexture->GetResourceBytes() : 0);
    }

    bool Renderer::FParticleRenderer::BindCommon(ID3D11DeviceContext* Context, Asset::UMaterial* Material, const Engine::FSceneView& View, const FVector4& SubImageSize, bool Trail) {
        Microsoft::WRL::ComPtr<ID3D11Device> Device{};

        Context->GetDevice(Device.GetAddressOf());

        Asset::UTexture2D* Texture{Trail ? mDefaultTrailTexture.get() : mDefaultTexture.get()};
        FConstants Constants{};

        if (Material != nullptr && Material->CreateResources(Device.Get(), Context)) {
            Constants.mTint = Material->GetBaseColor();

            if (Material->GetBaseColorTexture() != nullptr) {
                Texture = Material->GetBaseColorTexture();
            }
        }

        if (Texture == nullptr || (!Texture->HasResources() && !Texture->CreateResources(Device.Get()))) {
            return false;
        }

        Constants.mViewProjection = View.mViewProjection;
        Constants.mCameraRight = {View.mView(0, 0), View.mView(1, 0), View.mView(2, 0), 0.0f};
        Constants.mCameraUp = {View.mView(0, 1), View.mView(1, 1), View.mView(2, 1), 0.0f};
        Constants.mSubImageSize = SubImageSize;
        mConstants.Update(Context, Constants);

        ID3D11ShaderResourceView* Resource{Texture->GetShaderResourceView()};
        ID3D11SamplerState* Sampler{Trail ? mTrailSampler.Get() : mSampler.Get()};

        Context->IASetInputLayout(nullptr);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mConstants.Bind(Context, RenderCore::EShaderStage::Vertex | RenderCore::EShaderStage::Pixel, 0);
        Context->PSSetShaderResources(0, 1, &Resource);
        Context->PSSetSamplers(0, 1, &Sampler);

        return true;
    }

    void Renderer::FParticleRenderer::AppendStrip(std::span<const Engine::FParticleTrailPoint> Points, Uint32 Sheets, const Engine::FSceneView& View) {
        const Index_t Count{Points.size()};
        const FVector CameraUp{View.mView(0, 1), View.mView(1, 1), View.mView(2, 1)};

        const auto MakeVertex{[](const FVector& Position, const Engine::FParticleTrailPoint& Point, float V) {
            FTrailVertex Vertex{};

            Vertex.mPosition[0] = Position.x;
            Vertex.mPosition[1] = Position.y;
            Vertex.mPosition[2] = Position.z;
            Vertex.mU = Point.mU;
            Vertex.mV = V;
            Vertex.mColor[0] = Point.mColor.x;
            Vertex.mColor[1] = Point.mColor.y;
            Vertex.mColor[2] = Point.mColor.z;
            Vertex.mColor[3] = Point.mColor.w;

            return Vertex;
        }};

        Core::TArray<FTrailVertex> Bottom(Count);
        Core::TArray<FTrailVertex> Top(Count);

        for (Uint32 Sheet{}; Sheet < Sheets; ++Sheet) {
            // 판은 180° 돌리면 같은 모양이라 π를 판 수로 나눕니다.
            const float Angle{std::numbers::pi_v<float> / static_cast<float>(Sheets) * static_cast<float>(Sheet)};
            const float Cosine{std::cos(Angle)};
            const float Sine{std::sin(Angle)};

            for (Index_t Index{}; Index < Count; ++Index) {
                const Engine::FParticleTrailPoint& Point{Points[Index]};
                // 휘어진 경로는 점마다 진행 방향이 달라 앞뒤 점으로 접선을 구합니다.
                FVector Tangent{Points[std::min(Index + 1, Count - 1)].mPosition - Points[Index > 0 ? Index - 1 : 0].mPosition};

                Tangent = Tangent.Length() < 0.0001f ? FVector{1.0f, 0.0f, 0.0f} : Tangent.Normalized();

                // 접선과 시선 둘 다에 수직이면 판이 카메라를 향합니다.
                FVector Right{Tangent.Cross(Point.mPosition - View.mLocation)};

                Right = Right.Length() < 0.0001f ? CameraUp : Right.Normalized();

                const FVector Up{Tangent.Cross(Right)};
                const FVector Offset{(Right * Cosine + Up * Sine) * (Point.mHalfWidth * ComputePinch(Points, Index))};

                Bottom[Index] = MakeVertex(Point.mPosition - Offset, Point, 1.0f);
                Top[Index] = MakeVertex(Point.mPosition + Offset, Point, 0.0f);
            }

            for (Index_t Index{}; Index + 1 < Count; ++Index) {
                mTrailScratch.push_back(Bottom[Index]);
                mTrailScratch.push_back(Top[Index]);
                mTrailScratch.push_back(Bottom[Index + 1]);
                mTrailScratch.push_back(Bottom[Index + 1]);
                mTrailScratch.push_back(Top[Index]);
                mTrailScratch.push_back(Top[Index + 1]);
            }
        }
    }

    void Renderer::FParticleRenderer::SortInstances(const Engine::FParticleSpriteBatch& Batch, const Engine::FSceneView& View) {
        const Engine::EParticleSortMode Mode{Batch.mSortMode};

        if (Mode == Engine::EParticleSortMode::None) {
            mScratch.assign(Batch.mInstances.begin(), Batch.mInstances.end());

            return;
        }

        mSortKeys.resize(Batch.mInstances.size());

        for (Index_t Index{}; Index < Batch.mInstances.size(); ++Index) {
            const Engine::FParticleSpriteInstance& Instance{Batch.mInstances[Index]};
            const FVector Delta{FVector{Instance.mPosition[0], Instance.mPosition[1], Instance.mPosition[2]} - View.mLocation};
            float Key{};

            switch (Mode) {
            case Engine::EParticleSortMode::ViewProjDepth:
                Key = Delta.Dot(View.mForward);
                break;
            case Engine::EParticleSortMode::DistanceToView:
                Key = Delta.LengthSquared();
                break;
            default:
                Key = static_cast<float>(Instance.mCounter);
                break;
            }

            mSortKeys[Index] = {Key, static_cast<Uint32>(Index)};
        }

        // 거리와 깊이는 먼 것부터 그려야 알파 합성이 맞습니다. 나이순은 순번으로 정합니다.
        if (Mode == Engine::EParticleSortMode::AgeOldestFirst) {
            std::ranges::sort(mSortKeys, [](const auto& A, const auto& B) {
                return A.first < B.first;
            });
        } else {
            std::ranges::sort(mSortKeys, [](const auto& A, const auto& B) {
                return A.first > B.first;
            });
        }

        mScratch.resize(mSortKeys.size());

        for (Index_t Index{}; Index < mSortKeys.size(); ++Index) {
            mScratch[Index] = Batch.mInstances[mSortKeys[Index].second];
        }
    }

}
