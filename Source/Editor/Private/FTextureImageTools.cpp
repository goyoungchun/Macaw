#include "FTextureImageTools.h"
#include "../ThirdParty/ImGuiImageEditor/addons/imguiyesaddons/imguiimageeditor.h"
#include "../ThirdParty/ImGuiTexInspect/backends/tex_inspect_directx11.h"
#include "../ThirdParty/ImGuiTexInspect/imgui_tex_inspect.h"
#include <imgui_impl_dx11.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace Editor;
using Microsoft::WRL::ComPtr;

namespace {
    struct FImageTexture {
        ComPtr<ID3D11ShaderResourceView> View{};
        ComPtr<ID3D11SamplerState> Sampler{};
    };

    ID3D11Device* Device{};
    ID3D11DeviceContext* Context{};
    bool InspectorReady{};
    std::unordered_map<ImTextureID, FImageTexture> Textures{};
    std::vector<ImTextureID> RetiredIds{};
    std::vector<ComPtr<ID3D11ShaderResourceView>> RetiredViews{};
    std::vector<ImGuiTexInspect::Context*> RetiredInspectors{};

    void ReleaseTexture(ImTextureID& Texture) {
        if (Texture != ImTextureID_Invalid) {
            RetiredIds.push_back(Texture);
            Texture = ImTextureID_Invalid;
        }
    }

    void ApplySampler(const ImDrawList*, const ImDrawCmd* Command) {
        const ImTextureID Id{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(Command->UserCallbackData))};
        const auto Entry{Textures.find(Id)};
        const auto* State{static_cast<const ImGui_ImplDX11_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState)};
        if (State != nullptr && Entry != Textures.end()) {
            ID3D11SamplerState* Sampler{Entry->second.Sampler.Get()};
            State->DeviceContext->PSSetSamplers(0, 1, &Sampler);
        }
    }

    void UploadTexture(ImTextureID& Id, int Width, int Height, int Channels, const unsigned char* Pixels, bool Mips, bool WrapX, bool WrapY, bool MinNearest, bool MagNearest) {
        if (Device == nullptr || Pixels == nullptr || Width <= 0 || Height <= 0 || Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || (Channels != 3 && Channels != 4)) {
            return;
        }

        std::vector<unsigned char> Expanded{};
        if (Channels == 3) {
            const std::size_t Count{static_cast<std::size_t>(Width) * Height};
            Expanded.resize(Count * 4);
            for (std::size_t Index{0}; Index < Count; ++Index) {
                Expanded[Index * 4] = Pixels[Index * 3];
                Expanded[Index * 4 + 1] = Pixels[Index * 3 + 1];
                Expanded[Index * 4 + 2] = Pixels[Index * 3 + 2];
                Expanded[Index * 4 + 3] = 255;
            }
            Pixels = Expanded.data();
        }

        D3D11_TEXTURE2D_DESC Image{};
        Image.Width = static_cast<UINT>(Width);
        Image.Height = static_cast<UINT>(Height);
        Image.MipLevels = Mips ? 0 : 1;
        Image.ArraySize = 1;
        Image.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Image.SampleDesc.Count = 1;
        Image.BindFlags = D3D11_BIND_SHADER_RESOURCE | (Mips ? D3D11_BIND_RENDER_TARGET : 0);
        Image.MiscFlags = Mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        const D3D11_SUBRESOURCE_DATA Initial{Pixels, Image.Width * 4, 0};

        FImageTexture Result{};
        ComPtr<ID3D11Texture2D> Texture{};
        if (FAILED(Device->CreateTexture2D(&Image, Mips ? nullptr : &Initial, Texture.GetAddressOf())) || FAILED(Device->CreateShaderResourceView(Texture.Get(), nullptr, Result.View.GetAddressOf()))) {
            return;
        }

        D3D11_SAMPLER_DESC Sampler{};
        Sampler.Filter = D3D11_ENCODE_BASIC_FILTER(MinNearest ? D3D11_FILTER_TYPE_POINT : D3D11_FILTER_TYPE_LINEAR, MagNearest ? D3D11_FILTER_TYPE_POINT : D3D11_FILTER_TYPE_LINEAR, MinNearest ? D3D11_FILTER_TYPE_POINT : D3D11_FILTER_TYPE_LINEAR, D3D11_FILTER_REDUCTION_TYPE_STANDARD);
        Sampler.AddressU = WrapX ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.AddressV = WrapY ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        Sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        Sampler.MaxAnisotropy = 1;
        Sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(Device->CreateSamplerState(&Sampler, Result.Sampler.GetAddressOf()))) {
            return;
        }

        if (Mips) {
            Context->UpdateSubresource(Texture.Get(), 0, nullptr, Pixels, Initial.SysMemPitch, 0);
            Context->GenerateMips(Result.View.Get());
        }

        ReleaseTexture(Id);
        Id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(Result.View.Get()));
        Textures.emplace(Id, std::move(Result));
    }
}

void TextureImageTools::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext) {
    Device = InDevice;
    Context = InContext;
    InspectorReady = ImGuiTexInspect::ImplDX11_Init(Device, Context);
    ImGuiTexInspect::Init();
    ImGui::ImageEditor::SetFreeTextureCallback(ReleaseTexture);
    ImGui::ImageEditor::SetGenerateOrUpdateTextureCallback(UploadTexture);
    ImGui::ImageEditor::SetTextureSamplerCallback(ApplySampler);
}

void TextureImageTools::Shutdown() {
    ImGui::ImageEditor::Destroy();
    ImGui::ImageEditor::SetTextureSamplerCallback(nullptr);
    ImGui::ImageEditor::SetGenerateOrUpdateTextureCallback(nullptr);
    ImGui::ImageEditor::SetFreeTextureCallback(nullptr);
    EndFrame();
    Textures.clear();
    ImGuiTexInspect::Shutdown();
    ImGuiTexInspect::ImplDX11_Shutdown();
    InspectorReady = false;
    Device = nullptr;
    Context = nullptr;
}

bool TextureImageTools::IsInspectorReady() {
    return InspectorReady;
}

void TextureImageTools::RetireInspector(ImGuiTexInspect::Context* Inspector) {
    if (Inspector) {
        RetiredInspectors.push_back(Inspector);
    }
}

void TextureImageTools::RetireTexture(ComPtr<ID3D11ShaderResourceView> Texture) {
    if (Texture) {
        RetiredViews.push_back(std::move(Texture));
    }
}

void TextureImageTools::EndFrame() {
    for (auto* Inspector : RetiredInspectors) {
        ImGuiTexInspect::DestroyContext(Inspector);
    }
    RetiredInspectors.clear();
    for (const ImTextureID Id : RetiredIds) {
        Textures.erase(Id);
    }
    RetiredIds.clear();
    RetiredViews.clear();
}
