#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace ImGuiTexInspect {
    struct Context;
}

namespace Editor::TextureImageTools {
    void Initialize(ID3D11Device* Device, ID3D11DeviceContext* Context);
    void Shutdown();

    bool IsInspectorReady();
    void RetireInspector(ImGuiTexInspect::Context* Inspector);
    void RetireTexture(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Texture);
    void EndFrame();
}
