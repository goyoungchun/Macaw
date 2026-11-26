#pragma once

#include "Asset/FAssetFile.h"
#include "Core/Types.h"
#include "Editor/EEditorIcon.h"

#include <imgui.h>
#include <wrl/client.h>

struct ID3D11ShaderResourceView;
struct ImRect;

namespace RenderCore {
    class IRendererModule;
}

namespace Editor {
    class FAssetTileWidget {
    public:
        enum class EContentKind {
            File,
            Folder,
            LuaScript,
            ShaderSource
        };

        struct FStyle {
            const char* mLabel{};
            const char* mDescription{};
            Uint32 mResourceId{};
            Editor::EEditorIcon mFallback{};
            ImU32 mAccent{};
            ImU32 mIconTint{IM_COL32_WHITE};
        };

    public:
        FAssetTileWidget();
        ~FAssetTileWidget();

    public:
        void LoadIcons(RenderCore::IRendererModule* Renderer);

        static const FStyle& GetStyle(Asset::EAssetType Type);
        static const FStyle& GetStyle(EContentKind Kind);
        static ImVec2 Measure(float Width, float Scale);

        bool Draw(const char* Id, const Core::FString& Name, const FStyle& Style, void* Thumbnail, const ImVec2& Size, float Scale, bool Selected) const;
        void DrawPreview(const Core::FString& Name, const FStyle& Style, void* Thumbnail, const ImVec2& Size, float Scale) const;
        void DrawIcon(ImDrawList& DrawList, const FStyle& Style, const ImVec2& Position, float Size) const;

    private:
        void DrawCard(const ImRect& Bounds, const Core::FString& Name, const FStyle& Style, void* Thumbnail, float Scale, bool Selected, bool Hovered) const;
        void DrawImage(ImDrawList& DrawList, const ImRect& Bounds, const FStyle& Style, void* Thumbnail, float Rounding) const;

    private:
        Core::TMap<Uint32, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> mIcons{};
    };
}
