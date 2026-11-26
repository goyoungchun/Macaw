/**
 * @file FAssetThumbnailCache.cpp
 * @brief 공통 에셋 뷰어에서 촬영한 이미지를 캐시하고 변경된 에셋의 생성을 예약합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Editor/FAssetThumbnailCache.h"
#include "Editor/FEditorAssetCatalog.h"
#include "FAssetThumbnailRendering.h"
#include "Viewer/FMeshViewerContent.h"
#include "Asset/UStaticMesh.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Asset/UMaterial.h"
#include "Asset/UTexture2D.h"
#include "Asset/Animation/UAnimSequence.h"
#include "Asset/Animation/UAnimGraph.h"
#include "Asset/Animation/UBlendSpace.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/PrimitiveMesh.h"
#include "PhysicsRuntime/UPhysicsAsset.h"
#include "RenderCore/IRendererModule.h"

#include "Asset/FAssetManager.h"
#include "Core/FilePath.h"
#include "Serialization/JsonArchive.h"

#include <algorithm>
#include <bit>
#include <d3d11.h>
#include <DirectXTex.h>
#include <functional>
#include <utility>
#include <wincodec.h>
#include <wrl/client.h>

using namespace Editor;

namespace {
    constexpr Uint32 ThumbnailSize{256};

    bool EncodeThumbnail(const Viewer::FViewerCapture& Capture, const RenderCore::IRendererModule& Renderer, DirectX::Blob& Png) {
        auto* View{static_cast<ID3D11ShaderResourceView*>(Capture.GetNativeTexture())};
        auto* Device{static_cast<ID3D11Device*>(Renderer.GetNativeDevice())};
        auto* Context{static_cast<ID3D11DeviceContext*>(Renderer.GetNativeContext())};

        if (View == nullptr || Device == nullptr || Context == nullptr) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Resource> Resource{};
        DirectX::ScratchImage Image{};
        View->GetResource(Resource.GetAddressOf());

        if (FAILED(DirectX::CaptureTexture(Device, Context, Resource.Get(), Image))) {
            return false;
        }

        return SUCCEEDED(DirectX::SaveToWICMemory(*Image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB, GUID_ContainerFormatPng, Png, &GUID_WICPixelFormat24bppBGR));
    }

    std::size_t FileSignature(Asset::FAssetManager& Assets, const std::filesystem::path& Path) {
        if (Path.empty()) {
            return 0;
        }

        Core::FString Error{};
        const auto Resolved{Assets.ResolvePath(Path, Error)};
        std::error_code Code{};
        const auto Modified{Resolved.empty() ? std::filesystem::file_time_type::min() : std::filesystem::last_write_time(Resolved, Code)};

        return Code ? 0 : static_cast<std::size_t>(Modified.time_since_epoch().count());
    }
}

FAssetThumbnailCache::FThumbnail::FThumbnail() = default;
FAssetThumbnailCache::FThumbnail::~FThumbnail() = default;

FAssetThumbnailCache::FPendingThumbnail::FPendingThumbnail(const Core::FString& Key, const std::filesystem::path& Path, Asset::EAssetType Type)
	: mKey{Key},
	  mPath{Path},
	  mType{Type} {
}

FAssetThumbnailCache::FAssetThumbnailCache(FEditorAssetCatalog& Catalog, RenderCore::IRendererModule* Renderer)
	: mCatalog{Catalog},
	  mAssets{Catalog.GetAssets()},
	  mRenderer{Renderer},
	  mDefaultMesh{CoreUObject::NewObject<Asset::UStaticMesh>()},
	  mDefaultMaterial{CoreUObject::NewObject<Asset::UMaterial>()},
	  mCaptureViewer{Renderer, mAssets} {
    mCaptureViewer.GetShowFlags().mShadows = false;
    mCaptureViewer.GetShowFlags().mGrid = false;
    mDefaultMesh->SetMeshData(Engine::GetPrimitiveMesh(Engine::EPrimitiveType::Cube));
    LoadPreviewSettings();

    if (mAssets != nullptr && mRenderer != nullptr) {
        mAssets->SetAssetSavedHandler([this](const std::filesystem::path& Path, Asset::EAssetType Type) {
            QueueSavedThumbnail(Path, Type);
        });
    }
}

FAssetThumbnailCache::~FAssetThumbnailCache() {
    if (mAssets != nullptr && mRenderer != nullptr) {
        mAssets->SetAssetSavedHandler({});
    }
}

bool FAssetThumbnailCache::Supports(Asset::EAssetType Type) {
    return Viewer::FAssetViewer::Supports(Type) || Type == Asset::EAssetType::BlendSpace || Type == Asset::EAssetType::AnimGraph || Type == Asset::EAssetType::PhysicsAsset;
}

void FAssetThumbnailCache::BeginFrame() {
    ++mFrame;

    mRetiredCaptures.clear();

    while (mThumbnails.size() > 128) {
        const auto Oldest{std::min_element(mThumbnails.begin(), mThumbnails.end(), [](const auto& First, const auto& Second) {
            return First.second.mLastUsed < Second.second.mLastUsed;
        })};

        if (Oldest == mThumbnails.end() || Oldest->second.mLastUsed + 1 >= mFrame) {
            break;
        }

        mThumbnails.erase(Oldest);
    }

    SaveNextThumbnail();
    ProcessRequests();
}

void FAssetThumbnailCache::FlushSavedThumbnails() {
    while (!mSavedThumbnails.empty()) {
        SaveNextThumbnail();
    }
}

void FAssetThumbnailCache::Invalidate() {
    ++mGeneration;
    mRequests.clear();
    mCaptureViewer.Close(false);
    mCaptureMaterial.reset();

    for (auto& [Key, Thumbnail] : mThumbnails) {
        Thumbnail.mQueued = false;
        Thumbnail.mLastChecked = 0;
    }
}


Core::FString FAssetThumbnailCache::MakeKey(const std::filesystem::path& Path, Asset::EAssetType Type) const {
    if (Path.empty()) {
        return "@Default" + std::to_string(static_cast<Uint32>(Type));
    }

    if (Path.is_relative()) {
        return Core::FilePath::ToGenericUtf8(Core::FilePath::ToLower(Path.lexically_normal()));
    }

    Core::FString Error{};
    return Core::FilePath::ToGenericUtf8(Core::FilePath::ToLower(Core::FilePath::FromUtf8(mAssets->GetRelativePath(Path, Error))));
}

void* FAssetThumbnailCache::GetThumbnail(const std::filesystem::path& Path, Asset::EAssetType Type) {
    if (mAssets == nullptr || mRenderer == nullptr || !Supports(Type) || (Path.empty() && Type != Asset::EAssetType::Mesh && Type != Asset::EAssetType::Material)) {
        return nullptr;
    }

    const Core::FString Key{MakeKey(Path, Type)};

    if (Key.empty()) {
        return nullptr;
    }

    const Core::FString CacheKey{Key + "#" + std::to_string(static_cast<Uint32>(Type))};
    FThumbnail& Thumbnail{mThumbnails[CacheKey]};
    Thumbnail.mLastUsed = mFrame;

    if (!Thumbnail.mQueued && (Thumbnail.mLastChecked == 0 || Thumbnail.mLastChecked + 60 <= mFrame)) {
        Thumbnail.mQueued = true;
        mRequests.emplace_back(CacheKey, Path, Type);
    }

    return Thumbnail.mCapture != nullptr ? Thumbnail.mCapture->GetNativeTexture() : nullptr;
}

void FAssetThumbnailCache::ProcessRequests() {
    for (Uint32 Check{}; Check < 8 && !mRequests.empty(); ++Check) {
        const FPendingThumbnail Pending{std::move(mRequests.front())};
        mRequests.pop_front();
        const auto Found{mThumbnails.find(Pending.mKey)};

        if (Found == mThumbnails.end() || Found->second.mLastUsed + 1 < mFrame) {
            if (Found != mThumbnails.end()) {
                Found->second.mQueued = false;
            }

            continue;
        }

        FThumbnail& Thumbnail{Found->second};
        Thumbnail.mQueued = false;
        Core::FString Error{};
        const auto Path{Pending.mPath.empty() ? std::filesystem::path{} : mAssets->ResolvePath(Pending.mPath, Error)};
        if (mSavedThumbnails.contains(Path)) {
            Thumbnail.mLastChecked = mFrame;
            continue;
        }

        std::error_code Code{};
        const auto FileTime{Path.empty() ? std::filesystem::file_time_type::min() : std::filesystem::last_write_time(Path, Code)};
        const bool CheckFile{Thumbnail.mLastChecked == 0 || Thumbnail.mFileTime != FileTime};
        Thumbnail.mLastChecked = mFrame;

        if (CheckFile) {
            Thumbnail.mFileTime = FileTime;
            Thumbnail.mEmbedded = false;

            if (!Code && Path.extension() == L".masset") {
                auto Capture{LoadEmbeddedThumbnail(Path, Pending.mType)};
                if (Capture != nullptr) {
                    ReplaceCapture(Thumbnail, std::move(Capture));
                    Thumbnail.mEmbedded = true;
                    Thumbnail.mHasSignature = false;
                    Thumbnail.mFailed = false;
                }
            }
        }

        if (Thumbnail.mEmbedded) {
            continue;
        }

        auto* Object{LoadAsset(Pending.mPath, Pending.mType, Error)};
        const std::size_t Signature{mGeneration ^ AssetSignature(Pending.mPath, Object, Pending.mType)};

        if ((Thumbnail.mHasSignature && Thumbnail.mSignature == Signature) || (Thumbnail.mFailed && Thumbnail.mFailedSignature == Signature)) {
            continue;
        }

        ReplaceCapture(Thumbnail, CaptureThumbnail(Pending, Object, Error));
        if (Thumbnail.mCapture != nullptr) {
            Thumbnail.mSignature = Signature;
            Thumbnail.mHasSignature = true;
            Thumbnail.mFailed = false;
        } else {
            Thumbnail.mHasSignature = false;
            Thumbnail.mFailedSignature = Signature;
            Thumbnail.mFailed = true;
        }

        return;
    }
}

void FAssetThumbnailCache::QueueSavedThumbnail(const std::filesystem::path& Path, Asset::EAssetType Type) {
    if (!Supports(Type) || Path.extension() != L".masset") {
        return;
    }

    Core::FString Error{};
    const auto WritablePath{mAssets->ResolveWritablePath(Path, Error)};

    if (WritablePath.empty()) {
        return;
    }

    std::error_code Code{};
    const auto SavedTime{std::filesystem::last_write_time(WritablePath, Code)};
    if (!Code) {
        mSavedThumbnails.insert_or_assign(WritablePath, std::pair{Type, SavedTime});
    }
}

void FAssetThumbnailCache::SaveNextThumbnail() {
    if (mSavedThumbnails.empty()) {
        return;
    }

    const auto Request{mSavedThumbnails.extract(mSavedThumbnails.begin())};
    const auto& Path{Request.key()};
    const auto [Type, SavedTime]{Request.mapped()};
    std::error_code Code{};
    if (std::filesystem::last_write_time(Path, Code) != SavedTime || Code) {
        return;
    }

    const Core::FString Key{MakeKey(Path, Type) + "#" + std::to_string(static_cast<Uint32>(Type))};
    const FPendingThumbnail Pending{Key, Path, Type};
    Core::FString Error{};
    auto* Object{LoadAsset(Path, Type, Error)};
    auto Capture{CaptureThumbnail(Pending, Object, Error)};
    DirectX::Blob Png{};
    const bool Saved{Capture != nullptr && EncodeThumbnail(*Capture, *mRenderer, Png) && Asset::FAssetFile::SaveThumbnail(Path, Type, {static_cast<const Uint8*>(Png.GetBufferPointer()), Png.GetBufferSize()}, SavedTime, Error)};

    const auto Found{mThumbnails.find(Key)};
    if (Found != mThumbnails.end()) {
        FThumbnail& Thumbnail{Found->second};
        Thumbnail.mLastChecked = 0;

        if (Saved) {
            ReplaceCapture(Thumbnail, std::move(Capture));
            Thumbnail.mFileTime = std::filesystem::last_write_time(Path, Code);
            Thumbnail.mLastChecked = mFrame;
            Thumbnail.mEmbedded = !Code;
            Thumbnail.mHasSignature = false;
            Thumbnail.mFailed = false;
        }
    }
}

void FAssetThumbnailCache::ReplaceCapture(FThumbnail& Thumbnail, std::unique_ptr<Viewer::FViewerCapture> Capture) {
    if (Thumbnail.mCapture != nullptr) {
        mRetiredCaptures.push_back(std::move(Thumbnail.mCapture));
    }

    Thumbnail.mCapture = std::move(Capture);
}

std::unique_ptr<Viewer::FViewerCapture> FAssetThumbnailCache::LoadEmbeddedThumbnail(const std::filesystem::path& Path, Asset::EAssetType Type) const {
    auto* Device{static_cast<ID3D11Device*>(mRenderer->GetNativeDevice())};
    Core::TArray<Uint8> Bytes{};
    if (Device == nullptr || !Asset::FAssetFile::LoadThumbnail(Path, Type, Bytes)) {
        return nullptr;
    }

    DirectX::TexMetadata Metadata{};
    if (FAILED(DirectX::GetMetadataFromWICMemory(Bytes.data(), Bytes.size(), DirectX::WIC_FLAGS_IGNORE_SRGB, Metadata)) || Metadata.width != ThumbnailSize || Metadata.height != ThumbnailSize) {
        return nullptr;
    }

    auto Texture{CoreUObject::NewObject<Asset::UTexture2D>()};
    if (!Texture->LoadFromMemory(Bytes, L"Thumbnail.png", false) || !Texture->CreateResources(Device)) {
        return nullptr;
    }

    return std::make_unique<Viewer::FViewerCapture>(std::move(Texture));
}

std::unique_ptr<Viewer::FViewerCapture> FAssetThumbnailCache::CaptureThumbnail(const FPendingThumbnail& Pending, CoreUObject::UObject* Object, Core::FString& Error) {
    std::unique_ptr<Viewer::FViewerCapture> Capture{};

    if (Object != nullptr && BindCaptureAsset(Pending, Object, Error)) {
        const auto* Sequence{CoreUObject::Cast<Asset::UAnimSequence>(Object)};
        const auto Prepared{Sequence != nullptr ? mCaptureViewer.PrepareCapture(Viewer::FViewerCaptureState{Sequence->GetPlayLength() * 0.25}, Error) : mCaptureViewer.PrepareCapture(Error)};
        if (Prepared == Viewer::EViewerPrepareResult::Ready && mCaptureViewer.Resize(ThumbnailSize, ThumbnailSize) && mCaptureViewer.Fit()) {
            mCaptureViewer.SetViewMode(Pending.mType == Asset::EAssetType::Material ? Engine::EViewMode::Lit : Engine::EViewMode::Unlit);
            Capture = mCaptureViewer.Capture(Error, true);
        }
    }

    mCaptureViewer.Close(false);
    mCaptureMaterial.reset();

    return Capture;
}

bool FAssetThumbnailCache::SetAnimationPreview(const std::filesystem::path& Path, const Core::FString& MeshPath, const Core::FString& SkeletonPath, Core::FString& Error) {
    Error.clear();
    const Core::FString Key{MakeKey(Path, Asset::EAssetType::AnimSequence)};
    const auto Found{mAnimationPreviews.find(Key)};

    if (Found != mAnimationPreviews.end() && Found->second.first == MeshPath && Found->second.second == SkeletonPath) {
        return true;
    }

    if (MeshPath.empty()) {
        mAnimationPreviews.erase(Key);
    } else {
        mAnimationPreviews[Key] = {MeshPath, SkeletonPath};
    }

    Invalidate();

    if (!SavePreviewSettings()) {
        Error = "Cannot save animation preview settings";

        return false;
    }

    if (mAssets != nullptr && mRenderer != nullptr) {
        Core::FString PathError{};
        QueueSavedThumbnail(mAssets->ResolvePath(Path, PathError), Asset::EAssetType::AnimSequence);
    }

    return true;
}

bool FAssetThumbnailCache::GetAnimationPreview(const std::filesystem::path& Path, Core::FString& MeshPath, Core::FString& SkeletonPath) const {
    const auto Found{mAnimationPreviews.find(MakeKey(Path, Asset::EAssetType::AnimSequence))};

    if (Found == mAnimationPreviews.end()) {
        return false;
    }

    MeshPath = Found->second.first;
    SkeletonPath = Found->second.second;

    return true;
}

void FAssetThumbnailCache::LoadPreviewSettings() {
    if (mAssets == nullptr) {
        return;
    }

    Serialization::FJsonArchive Archive{Core::EArchiveMode::Loading};
    const std::filesystem::path Path{mAssets->GetContentRoot().parent_path() / "Saved/ViewerPreviews.json"};
    Uint32 Count{};

    if (!Archive.Load(Path) || !Archive.BeginArray("Animations", Count) || Count > 10000) {
        return;
    }

    for (Index_t Index{}; Index < Count; ++Index) {
        if (Archive.BeginObject(std::to_string(Index))) {
            Core::FString AssetPath{};
            Core::FString MeshPath{};
            Core::FString SkeletonPath{};
            Archive.Serialize("Asset", AssetPath);
            Archive.Serialize("Mesh", MeshPath);
            Archive.Serialize("Skeleton", SkeletonPath);
            Archive.EndObject();

            if (!AssetPath.empty() && !MeshPath.empty()) {
                mAnimationPreviews[AssetPath] = {MeshPath, SkeletonPath};
            }
        }
    }

    Archive.EndArray();
}

bool FAssetThumbnailCache::SavePreviewSettings() {
    if (mAssets == nullptr) {
        return false;
    }

    Serialization::FJsonArchive Archive{Core::EArchiveMode::Saving};
    Uint32 Count{static_cast<Uint32>(mAnimationPreviews.size())};
    Archive.BeginArray("Animations", Count);
    Index_t Index{};

    for (const auto& [Key, Preview] : mAnimationPreviews) {
        Archive.BeginObject(std::to_string(Index++));
        Core::FString AssetPath{Key};
        Core::FString MeshPath{Preview.first};
        Core::FString SkeletonPath{Preview.second};
        Archive.Serialize("Asset", AssetPath);
        Archive.Serialize("Mesh", MeshPath);
        Archive.Serialize("Skeleton", SkeletonPath);
        Archive.EndObject();
    }

    Archive.EndArray();

    return Archive.Save(mAssets->GetContentRoot().parent_path() / "Saved/ViewerPreviews.json");
}

std::size_t FAssetThumbnailCache::MaterialSignature(const Asset::UMaterial* Material) {
    if (Material == nullptr) {
        return 0;
    }

    std::size_t Hash{std::hash<Core::FString>{}(Material->GetShader().GetGuid().ToString() + (Material->GetShader() != nullptr ? Material->GetShader()->GetSourcePath() + Material->GetShader()->GetEntryPoint() : Core::FString{}))};

    if (Material->GetShader() != nullptr) {
        Hash ^= static_cast<std::size_t>(Material->GetShader()->IsUnlit()) + 0x9E3779B9 + (Hash << 6) + (Hash >> 2);
        Hash ^= static_cast<std::size_t>(Material->GetShader()->WritesGBuffer()) + 0x9E3779B9 + (Hash << 6) + (Hash >> 2);
    }

    Hash ^= static_cast<std::size_t>(Material->GetBlendMode()) + 0x9E3779B9 + (Hash << 6) + (Hash >> 2);

    for (const Core::FString* Path : {&Material->GetBaseColorTexturePath(), &Material->GetNormalMapTexturePath(), &Material->GetSpecularTexturePath(), &Material->GetEmissiveTexturePath(), &Material->GetReflectionTexturePath()}) {
        Hash ^= std::hash<Core::FString>{}(*Path) + 0x9E3779B9 + (Hash << 6) + (Hash >> 2);
    }

    for (const float Value : {Material->GetSpecularStrength(), Material->GetShininess(), Material->GetNormalStrength(), Material->GetReflectionStrength()}) {
        Hash ^= std::bit_cast<Uint32>(Value) + 0x9E3779B9 + (Hash << 6) + (Hash >> 2);
    }

    for (const FVector4* Color : {&Material->GetBaseColor(), &Material->GetSpecularColor(), &Material->GetAmbientColor(), &Material->GetEmissiveColor(), &Material->GetReflectionColor()}) {
        for (const float Value : Color->ToArray()) {
            Hash ^= std::bit_cast<Uint32>(Value) + 0x9E3779B9 + (Hash << 6) + (Hash >> 2);
        }
    }

    return Hash ^ static_cast<std::size_t>(Material->IsTwoSided());
}

CoreUObject::UObject* FAssetThumbnailCache::LoadAsset(const std::filesystem::path& Path, Asset::EAssetType Type, Core::FString& Error) {
    switch (Type) {
    case Asset::EAssetType::Mesh:
        return Path.empty() ? mDefaultMesh.get() : mAssets->LoadMesh(Path, Error);
    case Asset::EAssetType::SkeletalMesh:
        return mAssets->LoadSkeletalMesh(Path, Error);
    case Asset::EAssetType::Material:
        return Path.empty() ? mDefaultMaterial.get() : mAssets->LoadMaterial(Path, Error);
    case Asset::EAssetType::Skeleton:
        return mAssets->LoadSkeleton(Path, Error);
    case Asset::EAssetType::Texture:
        return mAssets->LoadTexture(Path, Error);
    case Asset::EAssetType::AnimSequence:
        return mAssets->LoadAnimSequence(Path, Error);
    case Asset::EAssetType::BlendSpace:
        return mAssets->LoadBlendSpace(Path, Error);
    case Asset::EAssetType::AnimGraph:
        return mAssets->LoadAnimGraph(Path, Error);
    case Asset::EAssetType::PhysicsAsset:
        return mAssets->LoadPhysicsAsset(Path, Error);
    default:
        return nullptr;
    }
}

std::size_t FAssetThumbnailCache::AssetSignature(const std::filesystem::path& Path, CoreUObject::UObject* Object, Asset::EAssetType Type) {
    std::size_t Signature{std::hash<const void*>{}(Object) ^ static_cast<std::size_t>(Type) ^ FileSignature(*mAssets, Path)};

    if (auto* Material{CoreUObject::Cast<Asset::UMaterial>(Object)}) {
        Signature ^= MaterialSignature(Material);
    }

    auto* Skeletal{CoreUObject::Cast<Asset::USkeletalMesh>(Object)};
    Asset::UStaticMesh* Mesh{Skeletal != nullptr ? Skeletal->GetGeometry() : CoreUObject::Cast<Asset::UStaticMesh>(Object)};
    Core::FString Error{};

    if (Mesh != nullptr) {
        for (const Asset::FMeshMaterialSlot& Slot : Mesh->GetMeshData().GetMaterialSlots()) {
            Asset::UMaterial* Material{Slot.GetMaterialPath().empty() ? nullptr : mAssets->LoadMaterial(Core::FilePath::FromUtf8(Slot.GetMaterialPath()), Error)};
            const std::size_t Value{MaterialSignature(Material)};
            Signature ^= Value + 0x9E3779B9 + (Signature << 6) + (Signature >> 2);
        }
    }

    return Signature;
}

bool FAssetThumbnailCache::BindCaptureAsset(const FPendingThumbnail& Pending, CoreUObject::UObject* Object, Core::FString& Error) {
    if (Pending.mType == Asset::EAssetType::Skeleton || Pending.mType == Asset::EAssetType::PhysicsAsset) {
        return AssetThumbnailRendering::SetSkeletalAssetPreview(mCaptureViewer, mCatalog, Pending.mPath, Pending.mType, Object, Error);
    }

    if (Pending.mType == Asset::EAssetType::BlendSpace || Pending.mType == Asset::EAssetType::AnimGraph) {
        const auto AssetPath{Core::FilePath::FromUtf8(mAssets->GetRelativePath(Pending.mPath, Error))};

        return !AssetPath.empty() && AssetThumbnailRendering::SetAnimatedAssetPreview(mCaptureViewer, mCatalog, mRenderer, AssetPath, Object, Error);
    }

    const auto* Material{Pending.mType == Asset::EAssetType::Material ? CoreUObject::Cast<Asset::UMaterial>(Object) : nullptr};
    std::unique_ptr<Asset::UMaterial> CaptureMaterial{};

    if (Material != nullptr && Material->GetShader() != nullptr && Material->GetShader()->IsUnlit()) {
        CaptureMaterial = CoreUObject::NewObject<Asset::UMaterial>();

        if (!mAssets->ApplyMaterialSettings(*CaptureMaterial, *Material, Error) || !CaptureMaterial->SetShader(RenderCore::UShader::GetDefault())) {
            return false;
        }

        Object = CaptureMaterial.get();
    }

    if (!mCaptureViewer.SetAsset(Object, Pending.mType)) {
        Error = mCaptureViewer.GetError();

        return false;
    }

    mCaptureMaterial = std::move(CaptureMaterial);

    if (Pending.mType == Asset::EAssetType::SkeletalMesh) {
        auto* Content{static_cast<Viewer::FMeshViewerContent*>(mCaptureViewer.GetContent())};

        return Content->SetSkeleton(mAssets->FindLegacySkeleton(Pending.mPath));
    }

    if (Pending.mType == Asset::EAssetType::AnimSequence) {
        Core::FString MeshPath{};
        Core::FString SkeletonPath{};

        GetAnimationPreview(Pending.mPath, MeshPath, SkeletonPath);

        return AssetThumbnailRendering::SetAnimationPreview(mCaptureViewer, mCatalog, Pending.mPath, *static_cast<Asset::UAnimSequence*>(Object), MeshPath, SkeletonPath, Error);
    }

    return true;
}
