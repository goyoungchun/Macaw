/**
 * @file FParticleRenderer.h
 * @brief 스프라이트 파티클 묶음을 카메라를 향한 인스턴스 사각형으로 그립니다.
 * @ingroup Renderer
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FVector4.h"
#include "Asset/UTexture2D.h"
#include "Engine/Particles/FParticleRenderData.h"
#include "RenderCore/TConstantBuffer.h"
#include "RenderCore/TStructuredBuffer.h"

#include <memory>
#include <span>

namespace Engine {
    struct FSceneView;
}

namespace Renderer {

    /**
     * @brief 에미터 하나의 스프라이트·띠 묶음을 한 번의 드로로 그립니다.
     * @details 정렬과 띠 방향은 뷰마다 여기서 정합니다. 블렌드와 깊이 상태는 호출한 패스가 설정합니다.
     *          프로젝트에 ParticleSprite.hlsl·ParticleTrail.hlsl이 없으면 그 종류는 그리지 않습니다.
     */
    class FParticleRenderer {
    private:
        /**
         * @brief ParticleSprite.hlsl의 ParticleConstants와 배치가 같아야 합니다.
         */
        struct FConstants {
        public:
            FMatrix mViewProjection{};

            FVector4 mCameraRight{};

            FVector4 mCameraUp{};

            /**
             * @brief 가로 칸 수, 세로 칸 수, 1/가로, 1/세로입니다.
             */
            FVector4 mSubImageSize{1.0f, 1.0f, 1.0f, 1.0f};

            /**
             * @brief 머티리얼 기본 색이며 파티클 색에 곱합니다.
             */
            FVector4 mTint{1.0f, 1.0f, 1.0f, 1.0f};
        };

        /**
         * @brief ParticleTrail.hlsl의 FParticleTrailVertex와 배치가 같아야 합니다.
         */
        struct FTrailVertex {
        public:
            float mPosition[3]{};

            float mU{};

            float mColor[4]{};

            float mV{};

            float mPadding[3]{};
        };

        static_assert(sizeof(FTrailVertex) == 48);

    public:
        FParticleRenderer();

        ~FParticleRenderer();

    public:
        /**
         * @brief 상수 버퍼, 샘플러와 기본 원형 텍스처를 만듭니다.
         */
        bool Initialize(ID3D11Device* Device);

        bool CreateShaders(ID3D11Device* Device, std::span<const Uint8> VertexCode, std::span<const Uint8> PixelCode);

        /**
         * @brief 셰이더가 준비됐으면 true입니다.
         */
        bool HasShaders() const;

        bool CreateTrailShaders(ID3D11Device* Device, std::span<const Uint8> VertexCode, std::span<const Uint8> PixelCode);

        /**
         * @brief 띠 묶음을 카메라를 향한 삼각형 목록으로 펼쳐 그립니다.
         * @return 그렸으면 true입니다.
         */
        bool DrawTrail(ID3D11DeviceContext* Context, const Engine::FParticleTrailBatch& Batch, const Engine::FSceneView& View);

        /**
         * @brief 묶음을 뷰 기준으로 정렬해 그립니다.
         * @return 그렸으면 true입니다.
         */
        bool Draw(ID3D11DeviceContext* Context, const Engine::FParticleSpriteBatch& Batch, const Engine::FSceneView& View);

        std::size_t GetResourceBytes() const;

    private:
        /**
         * @brief 정렬 방식에 따라 그릴 순서로 인스턴스를 mScratch에 담습니다.
         */
        void SortInstances(const Engine::FParticleSpriteBatch& Batch, const Engine::FSceneView& View);

        /**
         * @brief 머티리얼 텍스처와 색조, 뷰 상수를 정해 공통 상태를 바인딩합니다.
         * @return 텍스처를 준비하지 못하면 false입니다.
         */
        bool BindCommon(ID3D11DeviceContext* Context, Asset::UMaterial* Material, const Engine::FSceneView& View, const FVector4& SubImageSize, bool Trail);

        /**
         * @brief 점 목록 하나를 판 수만큼 카메라를 향한 띠로 펼쳐 mTrailScratch에 붙입니다.
         */
        void AppendStrip(std::span<const Engine::FParticleTrailPoint> Points, Uint32 Sheets, const Engine::FSceneView& View);

    private:
        RenderCore::TConstantBuffer<FConstants> mConstants{};

        RenderCore::TStructuredBuffer<Engine::FParticleSpriteInstance, RenderCore::EBufferUpdate::Dynamic> mInstances{};

        Microsoft::WRL::ComPtr<ID3D11VertexShader> mVertexShader{};

        Microsoft::WRL::ComPtr<ID3D11PixelShader> mPixelShader{};

        Microsoft::WRL::ComPtr<ID3D11SamplerState> mSampler{};

        /**
         * @brief 머티리얼이 없을 때 쓰는 가장자리가 부드러운 흰 원입니다.
         */
        std::unique_ptr<Asset::UTexture2D> mDefaultTexture{};

        /**
         * @brief 머티리얼이 없는 띠에 쓰는, 폭 방향으로만 투명해지는 흰 띠입니다.
         */
        std::unique_ptr<Asset::UTexture2D> mDefaultTrailTexture{};

        /**
         * @brief U를 감싸는 띠 전용 샘플러입니다.
         */
        Microsoft::WRL::ComPtr<ID3D11SamplerState> mTrailSampler{};

        Core::TArray<Engine::FParticleSpriteInstance> mScratch{};

        Core::TArray<std::pair<float, Uint32>> mSortKeys{};

        RenderCore::TStructuredBuffer<FTrailVertex, RenderCore::EBufferUpdate::Dynamic> mTrailVertices{};

        Microsoft::WRL::ComPtr<ID3D11VertexShader> mTrailVertexShader{};

        Microsoft::WRL::ComPtr<ID3D11PixelShader> mTrailPixelShader{};

        Core::TArray<FTrailVertex> mTrailScratch{};
    };

}
