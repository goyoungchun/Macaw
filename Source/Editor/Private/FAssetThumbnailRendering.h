#pragma once

#include "Asset/FAssetFile.h"

namespace CoreUObject {
    class UObject;
}

namespace Asset {
    class UAnimSequence;
}

namespace Editor {
    class FEditorAssetCatalog;
}

namespace RenderCore {
    class IRendererModule;
}

namespace Viewer {
    class FAssetViewer;
}

namespace Editor::AssetThumbnailRendering {
    bool SetAnimationPreview(Viewer::FAssetViewer& Viewer, Editor::FEditorAssetCatalog& Catalog, const std::filesystem::path& Path, const Asset::UAnimSequence& Sequence, const Core::FString& MeshPath, const Core::FString& SkeletonPath, Core::FString& Error);
    bool SetSkeletalAssetPreview(Viewer::FAssetViewer& Viewer, Editor::FEditorAssetCatalog& Catalog, const std::filesystem::path& Path, Asset::EAssetType Type, CoreUObject::UObject* Object, Core::FString& Error);
    bool SetAnimatedAssetPreview(Viewer::FAssetViewer& Viewer, Editor::FEditorAssetCatalog& Catalog, RenderCore::IRendererModule* Renderer, const std::filesystem::path& Path, CoreUObject::UObject* Object, Core::FString& Error);
}
