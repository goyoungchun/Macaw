/**
 * @file FTextureEditorToolkit.cpp
 * @brief 텍스처 정보를 표시하고 이미지 미리 보기를 제공하는 에셋 툴킷입니다.
 * @ingroup Editor
 */

#include "FTextureEditorToolkit.h"
#include "Editor/FEditorToolkitHost.h"
#include "Editor/FEditorContext.h"
#include "EditorStyle.h"
#include "Core/FilePath.h"
#include "Engine/FProject.h"
#include "Asset/UTexture2D.h"
#include "FTextureImageTools.h"
#include "../ThirdParty/ImGuiImageEditor/addons/imguiyesaddons/imguiimageeditor.h"
#include "../ThirdParty/ImGuiTexInspect/imgui_tex_inspect.h"
#include "Asset/FAssetManager.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "RenderCore/IRendererModule.h"
#include "Serialization/MemoryArchive.h"
#include <DirectXTex.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <commdlg.h>
#include <cstring>
#include <fstream>
#include <functional>
#include <kubazip/zip/zip.h>
#include <limits>
#include <optional>
#include <shobjidl.h>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

namespace {
    constexpr float MinimumToolboxWidthInFonts{12.0f};
    constexpr float MinimumCanvasWidthInFonts{12.0f};
    constexpr Uint32 TextureLayerVersion{1};
    constexpr Uint32 MaximumTextureLayers{64};
    std::optional<DWORD> SelectionClipboardSequence{};

    constexpr COMDLG_FILTERSPEC ImageTypes[]{
        {L"PNG", L"*.png"}, {L"JPEG", L"*.jpg;*.jpeg"}, {L"Bitmap", L"*.bmp"}, {L"Targa", L"*.tga"}, {L"GIF", L"*.gif"},
        {L"Icon", L"*.ico"}, {L"QOI", L"*.qoi"}, {L"TIFF", L"*.tiff;*.tif"}
    };
    constexpr const wchar_t* ImageExtensions[]{L"png", L"jpg", L"bmp", L"tga", L"gif", L"ico", L"qoi", L"tiff"};
    constexpr auto OpenImageFilter{L"Images and Archives\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.ico;*.cur;*.qoi;*.svg;*.psd;*.hdr;*.pic;*.pnm;*.ppm;*.pgm;*.tif;*.tiff;*.dds;*.zip\0All files\0*.*\0"};

    struct FChannelBars {
        void DrawAnnotation(ImDrawList* DrawList, ImVec2 Texel, ImGuiTexInspect::Transform2D Transform, ImVec4 Value);
    };

    void FChannelBars::DrawAnnotation(ImDrawList* DrawList, ImVec2 Texel, ImGuiTexInspect::Transform2D Transform, ImVec4 Value) {
        const ImVec2 A{Transform * Texel};
        const ImVec2 B{Transform * ImVec2{Texel.x + 1.0f, Texel.y + 1.0f}};
        const float Width{std::abs(B.x - A.x)};
        const float Height{std::abs(B.y - A.y)};
        if (Width < 36.0f || Height < 36.0f) {
            return;
        }

        const float Left{std::min(A.x, B.x) + Width * 0.10f};
        const float Bottom{std::max(A.y, B.y) - Height * 0.10f};
        const float Spacing{Width * 0.22f};
        const float Values[]{Value.x, Value.y, Value.z, Value.w};
        constexpr ImU32 Colors[]{IM_COL32(240, 90, 90, 255), IM_COL32(90, 220, 110, 255), IM_COL32(100, 150, 250, 255), IM_COL32_WHITE};
        for (int Channel{0}; Channel < 4; ++Channel) {
            const float X{Left + Channel * Spacing};
            const float BarHeight{Height * 0.80f * std::clamp(Values[Channel], 0.0f, 1.0f)};
            DrawList->AddRectFilled({X, Bottom - BarHeight}, {X + Width * 0.13f, Bottom}, Colors[Channel]);
        }
    }

    bool IsFloatingTexture(Asset::ETextureFormat Format) {
        return Format == Asset::ETextureFormat::RGBA16Float || Format == Asset::ETextureFormat::RGBA32Float || Format == Asset::ETextureFormat::R32Float;
    }

    bool ConvertImage(const DirectX::Image& Source, DXGI_FORMAT Format, DirectX::ScratchImage& Result) {
        DirectX::Image Linear{Source};
        Linear.format = DirectX::MakeLinear(Source.format);
        if (Linear.format == Format) {
            return SUCCEEDED(Result.InitializeFromImage(Linear));
        }
        if (DirectX::IsCompressed(Linear.format)) {
            return SUCCEEDED(DirectX::Decompress(Linear, Format, Result));
        }
        return SUCCEEDED(DirectX::Convert(Linear, Format, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, Result));
    }

    bool ReadAssetImage(RenderCore::IRendererModule& Renderer, const Asset::UTexture2D& Texture, DXGI_FORMAT Format, DirectX::ScratchImage& Result) {
        DirectX::ScratchImage Captured{};
        const auto& DDS{Texture.GetDDSData()};
        const auto& Pixels{Texture.GetPixels()};
        if (!DDS.empty() && SUCCEEDED(DirectX::LoadFromDDSMemory(DDS.data(), DDS.size(), DirectX::DDS_FLAGS_NONE, nullptr, Captured))) {
            return ConvertImage(*Captured.GetImage(0, 0, 0), Format, Result);
        }
        if (!Pixels.empty()) {
            const DXGI_FORMAT SourceFormat{Texture.GetFormat() == Asset::ETextureFormat::BGRA8 ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_UNKNOWN};
            if (SourceFormat != DXGI_FORMAT_UNKNOWN) {
                const DirectX::Image Source{Texture.GetSizeX(), Texture.GetSizeY(), SourceFormat, static_cast<std::size_t>(Texture.GetSizeX()) * 4, Pixels.size(), const_cast<Uint8*>(Pixels.data())};
                return ConvertImage(Source, Format, Result);
            }
        }
        auto* Device{static_cast<ID3D11Device*>(Renderer.GetNativeDevice())};
        auto* Context{static_cast<ID3D11DeviceContext*>(Renderer.GetNativeContext())};
        return Texture.GetTexture() && SUCCEEDED(DirectX::CaptureTexture(Device, Context, Texture.GetTexture(), Captured)) && ConvertImage(*Captured.GetImage(0, 0, 0), Format, Result);
    }

    bool ReadEditorImage(const ImGui::ImageEditor& Editor, DirectX::ScratchImage& Result) {
        int Width{};
        int Height{};
        int Channels{};
        Editor.getImageInfo(&Width, &Height, &Channels);
        const auto* Pixels{Editor.getImagePixels()};
        if (!Pixels || Width <= 0 || Height <= 0 || Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || (Channels != 1 && Channels != 3 && Channels != 4)) {
            return false;
        }
        if (FAILED(Result.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, Width, Height, 1, 1))) {
            return false;
        }
        auto* Output{Result.GetPixels()};
        const std::size_t PixelCount{static_cast<std::size_t>(Width) * Height};
        if (Channels == 4) {
            std::memcpy(Output, Pixels, PixelCount * 4);
            return true;
        }
        for (std::size_t Index{0}; Index < PixelCount; ++Index) {
            const auto* Source{Pixels + Index * Channels};
            Output[Index * 4] = Channels == 1 ? 255 : Source[0];
            Output[Index * 4 + 1] = Channels == 1 ? 255 : Source[1];
            Output[Index * 4 + 2] = Channels == 1 ? 255 : Source[2];
            Output[Index * 4 + 3] = Channels == 1 ? Source[0] : 255;
        }
        return true;
    }

    bool IsClipboardImageSize(std::size_t Width, std::size_t Height) {
        return Width > 0 && Height > 0 && Width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION && Height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    }

    UINT ClipboardPngFormat() {
        static const UINT Format{RegisterClipboardFormatW(L"PNG")};
        return Format;
    }

    bool HasExternalClipboardImage() {
        if (SelectionClipboardSequence == GetClipboardSequenceNumber()) {
            return false;
        }
        const UINT Png{ClipboardPngFormat()};
        return (Png && IsClipboardFormatAvailable(Png)) || IsClipboardFormatAvailable(CF_BITMAP);
    }

    bool ReadClipboardPng(DirectX::ScratchImage& Result) {
        const UINT Format{ClipboardPngFormat()};
        const HGLOBAL Handle{Format ? GetClipboardData(Format) : nullptr};
        if (!Handle) {
            return false;
        }
        const auto* Data{static_cast<const Uint8*>(GlobalLock(Handle))};
        if (!Data) {
            return false;
        }

        const auto Size{GlobalSize(Handle)};
        constexpr auto Flags{DirectX::WIC_FLAGS_IGNORE_SRGB};
        DirectX::TexMetadata Metadata{};
        DirectX::ScratchImage Decoded{};
        const bool Loaded{SUCCEEDED(DirectX::GetMetadataFromWICMemory(Data, Size, Flags, Metadata)) && IsClipboardImageSize(Metadata.width, Metadata.height) && SUCCEEDED(DirectX::LoadFromWICMemory(Data, Size, Flags, nullptr, Decoded))};
        GlobalUnlock(Handle);
        return Loaded && ConvertImage(*Decoded.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, Result);
    }

    bool ReadClipboardBitmap(DirectX::ScratchImage& Result) {
        const auto Bitmap{static_cast<HBITMAP>(GetClipboardData(CF_BITMAP))};
        if (!Bitmap) {
            return false;
        }
        bool IsWic2{};
        auto* Factory{DirectX::GetWICFactory(IsWic2)};
        const auto Palette{static_cast<HPALETTE>(GetClipboardData(CF_PALETTE))};
        ComPtr<IWICBitmap> Source{};
        UINT Width{};
        UINT Height{};
        if (!Factory || FAILED(Factory->CreateBitmapFromHBITMAP(Bitmap, Palette, WICBitmapIgnoreAlpha, Source.GetAddressOf())) || FAILED(Source->GetSize(&Width, &Height)) || !IsClipboardImageSize(Width, Height)) {
            return false;
        }

        ComPtr<IWICFormatConverter> Converter{};
        if (FAILED(Factory->CreateFormatConverter(Converter.GetAddressOf())) || FAILED(Converter->Initialize(Source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) || FAILED(Result.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, Width, Height, 1, 1))) {
            return false;
        }
        return SUCCEEDED(Converter->CopyPixels(nullptr, Width * 4, static_cast<UINT>(Result.GetPixelsSize()), Result.GetPixels()));
    }

    bool ReadClipboardImage(DirectX::ScratchImage& Result) {
        if (!OpenClipboard(nullptr)) {
            return false;
        }
        const bool Loaded{ReadClipboardPng(Result) || ReadClipboardBitmap(Result)};
        CloseClipboard();
        return Loaded;
    }

    bool PrepareUpdatedTexture(DirectX::ScratchImage& Pixels, const Asset::UTexture2D& Original, ID3D11Device* Device, Asset::UTexture2D& Updated, bool PreserveBC7) {
        DirectX::ScratchImage Converted{};
        const DirectX::ScratchImage* Result{&Pixels};
        if (Original.GetFormat() == Asset::ETextureFormat::BGRA8) {
            if (!ConvertImage(*Pixels.GetImage(0, 0, 0), DXGI_FORMAT_B8G8R8A8_UNORM, Converted)) {
                return false;
            }
            Result = &Converted;
        }
        DirectX::ScratchImage Mips{};
        if (Original.GetMipLevels() > 1 && (Result->GetMetadata().width > 1 || Result->GetMetadata().height > 1)) {
            const auto Filter{Original.IsSRGB() ? DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT};
            if (FAILED(DirectX::GenerateMipMaps(*Result->GetImage(0, 0, 0), Filter, 0, Mips))) {
                return false;
            }
            Result = &Mips;
        }
        DirectX::ScratchImage Compressed{};
        if (Original.GetFormat() == Asset::ETextureFormat::BC7 && PreserveBC7) {
            const auto Format{Original.IsSRGB() ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM};
            if (Original.IsSRGB()) {
                Pixels.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
                if (Result == &Mips) {
                    Mips.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
                }
            }
            if (FAILED(DirectX::Compress(Result->GetImages(), Result->GetImageCount(), Result->GetMetadata(), Format, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, Compressed))) {
                return false;
            }
            Result = &Compressed;
        }
        DirectX::Blob DDS{};
        return SUCCEEDED(DirectX::SaveToDDSMemory(Result->GetImages(), Result->GetImageCount(), Result->GetMetadata(), DirectX::DDS_FLAGS_NONE, DDS)) && Updated.SetDDSData({static_cast<const Uint8*>(DDS.GetBufferPointer()), DDS.GetBufferSize()}, Original.IsSRGB()) && Updated.SetUsage(Original.GetUsage()) && Updated.CreateResources(Device);
    }

    std::filesystem::path ChooseImage(const std::filesystem::path& Initial) {
        std::array<wchar_t, 32768> Buffer{};
        OPENFILENAMEW Dialog{};
        Dialog.lStructSize = sizeof(Dialog);
        Dialog.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        Dialog.lpstrFilter = OpenImageFilter;
        Dialog.lpstrInitialDir = Initial.c_str();
        Dialog.lpstrFile = Buffer.data();
        Dialog.nMaxFile = static_cast<DWORD>(Buffer.size());
        Dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
        return GetOpenFileNameW(&Dialog) ? std::filesystem::path{Buffer.data()} : std::filesystem::path{};
    }

    std::filesystem::path ChooseExport(const std::filesystem::path& Initial) {
        ComPtr<IFileSaveDialog> Dialog{};
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(Dialog.GetAddressOf())))) {
            return {};
        }
        Dialog->SetTitle(L"Export Image");
        Dialog->SetFileName(Initial.filename().c_str());
        const auto FormatCount{static_cast<UINT>(std::size(ImageTypes))};
        Dialog->SetFileTypes(FormatCount, ImageTypes);
        Dialog->SetDefaultExtension(L"png");
        Dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        const auto Directory{Initial.parent_path()};
        ComPtr<IShellItem> Folder{};
        if (!Directory.empty() && SUCCEEDED(SHCreateItemFromParsingName(Directory.c_str(), nullptr, IID_PPV_ARGS(Folder.GetAddressOf())))) {
            Dialog->SetFolder(Folder.Get());
        }
        ComPtr<IShellItem> Item{};
        PWSTR File{};
        if (FAILED(Dialog->Show(static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw))) || FAILED(Dialog->GetResult(Item.GetAddressOf())) || FAILED(Item->GetDisplayName(SIGDN_FILESYSPATH, &File))) {
            return {};
        }
        std::filesystem::path Path{File};
        CoTaskMemFree(File);
        if (Path.extension().empty()) {
            UINT Format{1};
            Dialog->GetFileTypeIndex(&Format);
            Path.replace_extension(ImageExtensions[std::clamp(Format, 1u, FormatCount) - 1]);
        }
        return Path;
    }

    bool IsBrowsableImage(const std::filesystem::path& Path) {
        constexpr std::wstring_view Extensions[]{L".png", L".jpg", L".jpeg", L".bmp", L".tga", L".gif", L".ico", L".cur", L".qoi", L".svg", L".psd", L".hdr", L".pic", L".pnm", L".ppm", L".pgm", L".tif", L".tiff", L".dds"};
        const auto Extension{Core::FilePath::ToLowerExtension(Path)};
        return std::ranges::find(Extensions, Extension) != std::end(Extensions);
    }

    std::filesystem::path GetArchiveImageType(std::string_view Name) {
        const auto Dot{Name.find_last_of('.')};
        if (Dot == std::string_view::npos || Name.size() - Dot > 6) {
            return {};
        }
        const auto Suffix{Name.substr(Dot)};
        if (!std::ranges::all_of(Suffix.substr(1), [](char Character) {
            return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z');
        })) {
            return {};
        }
        return std::filesystem::path{L"image"}.concat(Core::FilePath::FromUtf8(Suffix).native());
    }

    bool LoadExternalImage(ImGui::ImageEditor& Editor, const std::filesystem::path& Path) {
        const auto Extension{Core::FilePath::ToLowerExtension(Path)};
        if (Extension == L".webp") {
            return false;
        }

        const auto Name{Core::FilePath::ToUtf8(Path)};
        if (Editor.loadFromFile(Name.c_str())) {
            return true;
        }
        DirectX::ScratchImage Original{};
        DirectX::ScratchImage Pixels{};
        const HRESULT Status{Extension == L".dds" ? DirectX::LoadFromDDSFile(Path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, Original) : DirectX::LoadFromWICFile(Path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, Original)};
        if (FAILED(Status) || !ConvertImage(*Original.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, Pixels)) {
            return false;
        }
        return Editor.LoadPixels(Pixels.GetPixels(), static_cast<int>(Pixels.GetMetadata().width), static_cast<int>(Pixels.GetMetadata().height), 4, Name.c_str());
    }

    using FZipReader = std::unique_ptr<zip_t, decltype(&zip_stream_close)>;

    FZipReader ReadArchive(const std::filesystem::path& Path, Core::TArray<char>& Bytes) {
        std::ifstream File{Path, std::ios::binary | std::ios::ate};
        const auto Size{File.tellg()};
        if (Size <= 0 || Size > std::numeric_limits<int>::max()) {
            return {nullptr, zip_stream_close};
        }
        Bytes.resize(static_cast<std::size_t>(Size));
        File.seekg(0);
        if (!File.read(Bytes.data(), Size)) {
            return {nullptr, zip_stream_close};
        }
        return {zip_stream_open(Bytes.data(), Bytes.size(), 0, 'r'), zip_stream_close};
    }

    bool LoadArchivedImage(ImGui::ImageEditor& Editor, const std::filesystem::path& Archive, const Core::FString& Member) {
        Core::TArray<char> Bytes{};
        auto Zip{ReadArchive(Archive, Bytes)};
        if (!Zip || zip_entry_open(Zip.get(), Member.c_str()) != 0) {
            return false;
        }
        const auto Size{zip_entry_size(Zip.get())};
        Core::TArray<Uint8> Entry{};
        if (Size > 0 && Size <= std::numeric_limits<int>::max()) {
            Entry.resize(static_cast<std::size_t>(Size));
            if (zip_entry_noallocread(Zip.get(), Entry.data(), Entry.size()) != static_cast<std::ptrdiff_t>(Entry.size())) {
                Entry.clear();
            }
        }
        zip_entry_close(Zip.get());
        if (Entry.empty()) {
            return false;
        }

        const auto Extension{Core::FilePath::ToLowerExtension(GetArchiveImageType(Member))};
        const auto ExtUtf8{Core::FilePath::ToUtf8(Extension)};
        if (Editor.LoadMemory(Entry.data(), static_cast<int>(Entry.size()), Member.c_str(), ExtUtf8.c_str())) {
            return true;
        }
        DirectX::ScratchImage Original{};
        DirectX::ScratchImage Pixels{};
        const HRESULT Status{Extension == L".dds" ? DirectX::LoadFromDDSMemory(Entry.data(), Entry.size(), DirectX::DDS_FLAGS_NONE, nullptr, Original) : DirectX::LoadFromWICMemory(Entry.data(), Entry.size(), DirectX::WIC_FLAGS_NONE, nullptr, Original)};
        return SUCCEEDED(Status) && ConvertImage(*Original.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, Pixels) && Editor.LoadPixels(Pixels.GetPixels(), static_cast<int>(Pixels.GetMetadata().width), static_cast<int>(Pixels.GetMetadata().height), 4, Member.c_str());
    }
}

using namespace Editor;

FTextureEditorToolkit::FTextureEditorToolkit(FEditorContext* Context, const std::filesystem::path& Path, Asset::UTexture2D* Texture, FMessageBox& Messages)
	: FAssetEditorToolkit{Path, Asset::EAssetType::Texture, "###Texture Preview:" + Core::FilePath::ToGenericUtf8(Path)},
	  mContext{Context},
	  mMessages{Messages},
	  mPreviewTexture{Texture} {
    GetTab().SetCanCloseHandler([this]() {
        return RequestTabClose();
    });
    GetTab().SetIcon(EEditorIcon::Sprite, IM_COL32(220, 80, 80, 255));
    GetToolkitHost().AddWidget("Preview", "Image Workspace", [this](float) {
        DrawPreview();
    }, EEditorDockRegion::Center, false);
    if (auto* Workspace{GetToolkitHost().GetTabManager().FindTab("Preview")}) {
        Workspace->SetAutoHideTabBar(true);
    }
    GetToolkitHost().RequestLayoutReset();
}

FTextureEditorToolkit::~FTextureEditorToolkit() {
    TextureImageTools::RetireInspector(mInspectorContext);
    TextureImageTools::RetireTexture(std::move(mInspectorTexture));
    TextureImageTools::RetireTexture(std::move(mCompositeView));
}

void FTextureEditorToolkit::UpdateEditor(float) {
    RefreshEditState();
    GetTab().SetName(EditorStyle::TabIconPadding + Core::FilePath::ToUtf8(GetAssetPath().stem()) + (mDirty ? "*" : "") + "###Texture Preview:" + Core::FilePath::ToGenericUtf8(GetAssetPath()));

    if (mPreviewTexture == nullptr || !mPreviewTexture->HasResources()) {
        mPreviewTexture = nullptr;
        GetTab().SetVisible(false);
    }
}

void FTextureEditorToolkit::DrawPreview() {
    if (!mPreviewTexture) {
        return;
    }

    if (!mImageEditor && !LoadTextureForEditing()) {
        ResetLayers(std::make_unique<ImGui::ImageEditor>(false, true, true, true));
        mShowSaved = true;
    }

    DrawCommands();
    DrawArchivePicker();
    if (!mEditError.empty()) {
        ImGui::TextWrapped("%s", mEditError.c_str());
    }
    if (mShowSaved && !PrepareInspectorTexture()) {
        ImGui::TextWrapped("%s", mInspectError.c_str());
        mShowSaved = false;
    }

    const bool ReadOnly{!CanEditActiveLayer()};
    mHasPixel = false;
    mHasInspectorView = false;
    mPixelZoom = 0.0f;
    DrawWorkspace(ImGui::GetContentRegionAvail(), ReadOnly);
    RefreshEditState();
}

void FTextureEditorToolkit::DrawWorkspace(const ImVec2& Available, bool ReadOnly) {
    const float FontSize{ImGui::GetFontSize()};
    if (Available.x < FontSize * 8.0f || Available.y < ImGui::GetFrameHeight() * 2.0f) {
        return;
    }
    if (!mShowProperties) {
        DrawCanvasArea(Available, ReadOnly);
        return;
    }

    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const float SplitterSize{std::max(5.0f, FontSize * 0.48f)};
    const float MinimumWidth{FontSize * 14.0f};
    const float MinimumCanvasWidth{FontSize * MinimumToolboxWidthInFonts + FontSize * MinimumCanvasWidthInFonts + SplitterSize};
    const bool SideBySide{Available.x >= MinimumWidth + MinimumCanvasWidth + SplitterSize};
    const float Span{(SideBySide ? Available.x : Available.y) - SplitterSize};
    const float MinimumPanel{SideBySide ? MinimumWidth : std::min(FontSize * 9.0f, Span * 0.38f)};
    const float MinimumCanvas{SideBySide ? MinimumCanvasWidth : std::min(FontSize * 12.0f, Span * 0.52f)};
    float& Preferred{SideBySide ? mPropertiesWidth : mPropertiesHeight};
    float Panel{std::clamp(Preferred * FontSize, MinimumPanel, Span - MinimumCanvas)};
    float Canvas{Span - Panel};
    const float PreviousPanel{Panel};

    const ImVec2 HandleMin{SideBySide ? ImVec2{Origin.x + Canvas, Origin.y} : ImVec2{Origin.x, Origin.y + Canvas}};
    const ImVec2 HandleMax{SideBySide ? ImVec2{HandleMin.x + SplitterSize, Origin.y + Available.y} : ImVec2{Origin.x + Available.x, HandleMin.y + SplitterSize}};
    const auto Id{ImGui::GetID(SideBySide ? "Properties Width" : "Properties Height")};
    if (ImGui::SplitterBehavior(ImRect{HandleMin, HandleMax}, Id, SideBySide ? ImGuiAxis_X : ImGuiAxis_Y, &Canvas, &Panel, MinimumCanvas, MinimumPanel) && Panel != PreviousPanel) {
        Preferred = Panel / FontSize;
    }
    ImGui::SetItemTooltip(SideBySide ? "Drag to resize the properties panel" : "Drag to change the properties panel height");

    DrawCanvasArea(SideBySide ? ImVec2{Canvas, Available.y} : ImVec2{Available.x, Canvas}, ReadOnly);
    if (SideBySide) {
        ImGui::SameLine(0.0f, SplitterSize);
    } else {
        ImGui::SetCursorScreenPos({Origin.x, Origin.y + Canvas + SplitterSize});
    }

    const ImVec2 Size{SideBySide ? ImVec2{Panel, Available.y} : ImVec2{Available.x, Panel}};
    if (ImGui::BeginChild("Properties", Size, ImGuiChildFlags_Borders)) {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.56f);
        DrawProperties(ReadOnly);
        ImGui::PopItemWidth();
    }
    ImGui::EndChild();
}

void FTextureEditorToolkit::DrawCanvasArea(const ImVec2& Size, bool ReadOnly) {
    const float FontSize{ImGui::GetFontSize()};
    const float SplitterSize{std::max(5.0f, FontSize * 0.48f)};
    const float MinimumToolboxWidth{FontSize * MinimumToolboxWidthInFonts};
    const float MinimumCanvasWidth{FontSize * MinimumCanvasWidthInFonts};
    const bool SideBySide{Size.x >= MinimumToolboxWidth + MinimumCanvasWidth + SplitterSize};
    const float Span{(SideBySide ? Size.x : Size.y) - SplitterSize};
    if (Span <= 0.0f) {
        return;
    }

    const float MinimumToolbox{SideBySide ? MinimumToolboxWidth : std::min(FontSize * 7.0f, Span * 0.4f)};
    const float MinimumCanvas{SideBySide ? MinimumCanvasWidth : std::min(FontSize * 7.5f, Span * 0.4f)};
    float& Preferred{SideBySide ? mToolboxWidth : mToolboxHeight};
    float Toolbox{std::clamp(Preferred * FontSize, MinimumToolbox, std::max(MinimumToolbox, Span - MinimumCanvas))};
    float CanvasSpan{Span - Toolbox};
    const float PreviousToolbox{Toolbox};
    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const ImVec2 HandleMin{SideBySide ? ImVec2{Origin.x + Toolbox, Origin.y} : ImVec2{Origin.x, Origin.y + Toolbox}};
    const ImVec2 HandleMax{SideBySide ? ImVec2{HandleMin.x + SplitterSize, Origin.y + Size.y} : ImVec2{Origin.x + Size.x, HandleMin.y + SplitterSize}};
    const ImGuiID Id{ImGui::GetID(SideBySide ? "Toolbox Width" : "Toolbox Height")};
    if (ImGui::SplitterBehavior(ImRect{HandleMin, HandleMax}, Id, SideBySide ? ImGuiAxis_X : ImGuiAxis_Y, &Toolbox, &CanvasSpan, MinimumToolbox, MinimumCanvas) && Toolbox != PreviousToolbox) {
        Preferred = Toolbox / FontSize;
    }
    ImGui::SetItemTooltip(SideBySide ? "Drag to resize the toolbox" : "Drag to change the toolbox height");

    DrawToolbox(SideBySide ? ImVec2{Toolbox, Size.y} : ImVec2{Size.x, Toolbox}, ReadOnly, !SideBySide);
    if (SideBySide) {
        ImGui::SameLine(0.0f, SplitterSize);
    } else {
        ImGui::SetCursorScreenPos({Origin.x, Origin.y + Toolbox + SplitterSize});
    }

    const ImVec2 CanvasSize{SideBySide ? ImVec2{CanvasSpan, Size.y} : ImVec2{Size.x, CanvasSpan}};
    if (ImGui::BeginChild("Canvas", CanvasSize, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 Canvas{ImGui::GetContentRegionAvail()};
        const float ImageHeight{Canvas.y - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y};
        if (Canvas.x > 1.0f && ImageHeight > 1.0f) {
            DrawCanvas({Canvas.x, ImageHeight}, ReadOnly);
            DrawCanvasStatus(ReadOnly);
        }
    }
    ImGui::EndChild();
}

void FTextureEditorToolkit::DrawToolbox(const ImVec2& Size, bool ReadOnly, bool Horizontal) {
    if (ImGui::BeginChild("Tools", Size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (Horizontal) {
            DrawTools(ReadOnly, true);
        } else {
            if (ImGui::BeginChild("Tool Icons", {ImGui::GetFrameHeight(), 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
                DrawTools(ReadOnly, false);
            }
            ImGui::EndChild();
            ImGui::SameLine();
        }
        if (ImGui::BeginChild("Tool Options")) {
            DrawToolOptions(ReadOnly);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void FTextureEditorToolkit::DrawCommands() {
    using EditorStyle::EIcon;
    using EditorStyle::EIconSelectionStyle;
    using EditorStyle::IconButton;

    if (mPickingColor && !ImGui::GetIO().WantTextInput && ImGui::Shortcut(ImGuiKey_Escape)) {
        mPickingColor = false;
    } else if (mImageEditor->HasFloatingPaste() && !ImGui::GetIO().WantTextInput) {
        if (ImGui::Shortcut(ImGuiKey_Escape)) {
            mImageEditor->CancelFloatingPaste();
        } else if (ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_KeypadEnter)) {
            mImageEditor->CommitFloatingPaste();
        }
    }

    const float FrameWidth{ImGui::GetFrameHeight()};
    const float DropdownWidth{FrameWidth + 12.0f * ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};
    const auto PlaceItem{[](float Width, float Gap = 1.0f) {
        const float Right{ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x};
        const float Spacing{ImGui::GetStyle().ItemSpacing.x * Gap};
        if (ImGui::GetItemRectMax().x + Spacing + Width <= Right) {
            ImGui::SameLine(0.0f, Spacing);
        }
    }};

    if (IconButton("File", EIcon::Folder, false, true, true, "Image file commands")) {
        ImGui::OpenPopup("Texture File");
    }
    DrawFileMenu();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) {
        OpenImage();
    }

    PlaceItem(FrameWidth);
    const bool ConvertToRGBA8{IsFloatingTexture(mPreviewTexture->GetFormat()) || (mPreviewTexture->GetFormat() == Asset::ETextureFormat::BC7 && !mPreserveBC7)};
    const bool CanSave{mDirty || mImageEditor->HasFloatingPaste()};
    const bool Save{IconButton("Save Asset", EIcon::Save, false, CanSave, false, ConvertToRGBA8 ? "Save asset as RGBA8 (Ctrl+S)" : "Save asset (Ctrl+S)", 0.0f, EIconSelectionStyle::Icon)};
    if (CanSave && (Save || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S))) {
        SaveTexture();
    }
    PlaceItem(FrameWidth);
    const bool HasImage{mImageEditor->getImagePixels() != nullptr};
    if (IconButton("Export", EIcon::Export, false, HasImage, false, "Export image...", 0.0f, EIconSelectionStyle::Icon)) {
        ExportImage();
    }

    PlaceItem(FrameWidth, 2.0f);
    const bool CanEdit{CanEditActiveLayer()};
    const bool Undo{IconButton("Undo", EIcon::Undo, false, CanEdit, false, "Undo (Ctrl+Z)", 0.0f, EIconSelectionStyle::Icon)};
    PlaceItem(FrameWidth);
    const bool CanRedo{CanEdit && !mImageEditor->HasFloatingPaste()};
    const bool Redo{IconButton("Redo", EIcon::Redo, false, CanRedo, false, "Redo (Ctrl+Y or Ctrl+Shift+Z)", 0.0f, EIconSelectionStyle::Icon)};
    if (CanEdit && (Undo || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z))) {
        mImageEditor->Undo(mLayers.size() > 1);
    } else if (CanRedo && (Redo || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))) {
        mImageEditor->Redo(mLayers.size() > 1);
    }
    PlaceItem(DropdownWidth);
    if (IconButton("Edit", EIcon::Marquee, false, CanEdit, true, "Selection and clipboard")) {
        ImGui::OpenPopup("Texture Edit");
    }
    DrawEditCommands(CanEdit);

    PlaceItem(DropdownWidth, 2.0f);
    if (IconButton("Zoom Presets", EIcon::Maximize, false, true, true, "Zoom presets")) {
        ImGui::OpenPopup("Texture Zoom");
    }
    DrawZoomMenu();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_0)) {
        mZoomRequest = EZoomRequest::Fit;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_1)) {
        mZoomRequest = EZoomRequest::None;
        mRequestedPixelScale = 1.0f;
    }

    PlaceItem(FrameWidth);
    if (IconButton("Compare Saved Asset", EIcon::Compare, mShowSaved, true, false, mShowSaved ? "Show working image" : "Compare with saved asset")) {
        mImageEditor->CommitFloatingPaste();
        mShowSaved = !mShowSaved;
    }

    const auto& Style{ImGui::GetStyle()};
    const float GroupWidth{ImGui::CalcTextSize("RGBA").x + ImGui::CalcTextSize("R").x + ImGui::CalcTextSize("G").x + ImGui::CalcTextSize("B").x + ImGui::CalcTextSize("A").x + 10.0f * Style.FramePadding.x + 4.0f * Style.ItemInnerSpacing.x};
    PlaceItem(GroupWidth, 2.0f);
    DrawChannelButtons();

    PlaceItem(FrameWidth, 2.0f);
    if (IconButton("Properties", EIcon::Details, mShowProperties, true, false, mShowProperties ? "Hide properties" : "Show properties")) {
        mShowProperties = !mShowProperties;
    }

    RefreshEditState();
}

void FTextureEditorToolkit::DrawChannelButtons() {
    constexpr const char* Labels[]{"RGBA", "R", "G", "B", "A"};
    constexpr const char* Tooltips[]{"Show RGBA with the composite view settings", "View red as grayscale", "View green as grayscale", "View blue as grayscale", "View alpha as grayscale (A)"};
    const auto Select{[this](EChannelView View) {
        if (View == EChannelView::Alpha) {
            if (mChannelView == EChannelView::Alpha) {
                mChannelView = mPreviousChannelView;
                return;
            }
            mPreviousChannelView = mChannelView;
        }
        mChannelView = View;
    }};

    const bool CanInspect{TextureImageTools::IsInspectorReady()};
    ImGui::BeginGroup();
    ImGui::BeginDisabled(!CanInspect);
    for (int Index{}; Index < static_cast<int>(std::size(Labels)); ++Index) {
        const auto View{static_cast<EChannelView>(Index - 1)};
        if (Index != 0) {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        }
        if (EditorStyle::Button(Labels[Index], {}, mChannelView == View)) {
            Select(View);
        }
        ImGui::SetItemTooltip("%s", Tooltips[Index]);
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();

    if (CanInspect && !ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::Shortcut(ImGuiKey_A)) {
        Select(EChannelView::Alpha);
    }
}

void FTextureEditorToolkit::DrawFileMenu() {
    if (!ImGui::BeginPopup("Texture File")) {
        return;
    }

    if (ImGui::MenuItem("Open Image...", "Ctrl+O")) {
        OpenImage();
    }
    const bool HasSource{!mImagePath.empty() || mArchiveIndex.has_value()};
    if (ImGui::MenuItem("Reload Opened Image", nullptr, false, HasSource)) {
        ReloadImage();
    }
    if (ImGui::MenuItem("Previous Image", nullptr, false, HasSource)) {
        BrowseImage(-1);
    }
    if (ImGui::MenuItem("Next Image", nullptr, false, HasSource)) {
        BrowseImage(1);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reset Working Image to Asset")) {
        ConfirmReplace([this]() {
            LoadTextureForEditing();
        });
    }
    ImGui::EndPopup();
}

void FTextureEditorToolkit::DrawEditCommands(bool Enabled) {
    using EditorStyle::EIcon;
    using ECommand = ImGui::ImageEditor::ESelectionCommand;

    struct FCommand {
        ECommand mCommand{};
        const char* mLabel{};
        ImGuiKeyChord mKey{};
        const char* mShortcut{};
        std::optional<EIcon> mToolbarIcon{};
    };
    constexpr FCommand Commands[]{
        {ECommand::SelectAll, "Select All", ImGuiMod_Ctrl | ImGuiKey_A, "Ctrl+A"},
        {ECommand::Deselect, "Deselect", ImGuiMod_Ctrl | ImGuiKey_D, "Ctrl+D"},
        {ECommand::Cut, "Cut", ImGuiMod_Ctrl | ImGuiKey_X, "Ctrl+X", EIcon::Cut},
        {ECommand::Copy, "Copy", ImGuiMod_Ctrl | ImGuiKey_C, "Ctrl+C", EIcon::Copy},
        {ECommand::Paste, "Paste", ImGuiMod_Ctrl | ImGuiKey_V, "Ctrl+V", EIcon::Paste},
        {ECommand::PasteBlended, "Paste Blended", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_V, "Ctrl+Shift+V"},
        {ECommand::Clear, "Clear Selection Pixels", ImGuiKey_Delete, "Delete"},
        {ECommand::Crop, "Crop to Selection", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_X, "Ctrl+Shift+X", EIcon::Crop}
    };

    const bool HasExternalImage{Enabled && mImageEditor->CanEditSelection() && HasExternalClipboardImage()};
    const auto IsPaste{[](ECommand Command) {
        return Command == ECommand::Paste || Command == ECommand::PasteBlended;
    }};
    const auto CanExecute{[this, Enabled, HasExternalImage, &IsPaste](ECommand Command) {
        return Enabled && (Command != ECommand::Crop || mLayers.size() == 1) && ((IsPaste(Command) && HasExternalImage) || mImageEditor->CanExecuteSelectionCommand(Command));
    }};
    constexpr auto ToolbarButtonCount{std::ranges::count_if(Commands, [](const FCommand& Item) {
        return Item.mToolbarIcon.has_value();
    })};
    const float ToolbarWidth{static_cast<float>(ToolbarButtonCount) * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x)};
    const float Right{ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x};
    std::optional<ECommand> ToolbarSelection{};
    if (ImGui::GetItemRectMax().x + ToolbarWidth <= Right) {
        for (const FCommand& Item : Commands) {
            if (!Item.mToolbarIcon) {
                continue;
            }

            ImGui::SameLine();
            char Hint[64]{};
            ImFormatString(Hint, sizeof(Hint), "%s (%s)", Item.mLabel, Item.mShortcut);
            if (EditorStyle::IconButton(Item.mLabel, *Item.mToolbarIcon, false, CanExecute(Item.mCommand), false, Hint)) {
                ToolbarSelection = Item.mCommand;
            }
        }
    }

    const bool MenuOpen{ImGui::BeginPopup("Texture Edit")};
    const bool AllowShortcut{!MenuOpen && !ImGui::GetIO().WantTextInput};
    for (const FCommand& Item : Commands) {
        const bool Available{CanExecute(Item.mCommand)};
        if (MenuOpen && (Item.mCommand == ECommand::Cut || Item.mCommand == ECommand::Clear)) {
            ImGui::Separator();
        }
        const bool Selected{MenuOpen && ImGui::MenuItem(Item.mLabel, Item.mShortcut, false, Available)};
        const bool Invoked{AllowShortcut && Enabled && ImGui::Shortcut(Item.mKey)};
        if (!Available || (!Selected && !Invoked && ToolbarSelection != Item.mCommand)) {
            continue;
        }
        if (IsPaste(Item.mCommand) && HasExternalImage) {
            DirectX::ScratchImage Image{};
            if (!ReadClipboardImage(Image) || !mImageEditor->PastePixels(Image.GetPixels(), static_cast<int>(Image.GetMetadata().width), static_cast<int>(Image.GetMetadata().height), 4, Item.mCommand == ECommand::PasteBlended)) {
                mMessages.Show("Paste Image", "Could not paste the image from the Windows clipboard.");
            }
        } else if (mImageEditor->ExecuteSelectionCommand(Item.mCommand) && (Item.mCommand == ECommand::Copy || Item.mCommand == ECommand::Cut)) {
            SelectionClipboardSequence = GetClipboardSequenceNumber();
        }
    }
    if (MenuOpen) {
        ImGui::EndPopup();
    }
}

void FTextureEditorToolkit::DrawZoomMenu() {
    if (!ImGui::BeginPopup("Texture Zoom")) {
        return;
    }

    if (ImGui::MenuItem("Fit Image", "Ctrl+0")) {
        mZoomRequest = EZoomRequest::Fit;
    }
    if (ImGui::MenuItem("Fit Width")) {
        mZoomRequest = EZoomRequest::FitWidth;
    }
    if (ImGui::MenuItem("Fit Height")) {
        mZoomRequest = EZoomRequest::FitHeight;
    }
    if (ImGui::MenuItem("Actual Size (100%)", "Ctrl+1")) {
        mZoomRequest = EZoomRequest::None;
        mRequestedPixelScale = 1.0f;
    }
    ImGui::EndPopup();
}

void FTextureEditorToolkit::DrawTools(bool ReadOnly, bool Horizontal) {
    using EditorStyle::EIcon;
    using EditorStyle::EIconSelectionStyle;

    struct FToolButton {
        const char* mName{};
        const char* mHint{};
        ImGuiKey mKey{};
        EIcon mIcon{};
        std::optional<ECanvasTool> mTool{};
    };
    constexpr FToolButton Buttons[]{
        {"Select", "Rectangular selection (M)", ImGuiKey_M, EIcon::Marquee, ECanvasTool::Select},
        {"Fill", "Flood fill (G)", ImGuiKey_G, EIcon::PaintBucket, ECanvasTool::Fill},
        {"Brush", "Brush (B)", ImGuiKey_B, EIcon::PaintBrush, ECanvasTool::Brush},
        {"Eraser", "Clear pixel colors and alpha (E)", ImGuiKey_E, EIcon::Eraser, ECanvasTool::Eraser},
        {"Eyedropper", "Pick a pixel color once (I)", ImGuiKey_I, EIcon::Eyedropper},
        {"Hand", "Pan (H or hold Space)", ImGuiKey_H, EIcon::Hand, ECanvasTool::Hand},
        {"Zoom", "Zoom (Z, Alt+click to zoom out)", ImGuiKey_Z, EIcon::Zoom, ECanvasTool::Zoom}
    };
    const bool CanPickColor{!ReadOnly && TextureImageTools::IsInspectorReady()};
    if (!CanPickColor) {
        mPickingColor = false;
    }
    for (std::size_t Index{}; Index < std::size(Buttons); ++Index) {
        if (Horizontal && Index > 0) {
            const float Right{ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x};
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetFrameHeight() <= Right) {
                ImGui::SameLine();
            }
        }
        const auto& Button{Buttons[Index]};
        const bool PicksColor{!Button.mTool};
        const bool Enabled{PicksColor ? CanPickColor : !ReadOnly || Button.mTool == ECanvasTool::Hand || Button.mTool == ECanvasTool::Zoom};
        const bool Selected{PicksColor ? mPickingColor : Button.mTool == mTool};
        const bool Clicked{EditorStyle::IconButton(Button.mName, Button.mIcon, Selected, Enabled, false, Button.mHint, 0.0f, PicksColor ? EIconSelectionStyle::Icon : EIconSelectionStyle::Background)};
        if (Enabled && (Clicked || (!ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::Shortcut(Button.mKey, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteFromRootWindow)))) {
            mImageEditor->CommitFloatingPaste();
            if (PicksColor) {
                mPickingColor = !mPickingColor;
            } else {
                mPickingColor = false;
                mTool = *Button.mTool;
            }
        }
        if (!Horizontal && PicksColor) {
            ImGui::Separator();
        }
    }
}

void FTextureEditorToolkit::DrawToolOptions(bool ReadOnly) {
    constexpr const char* Titles[]{"Selection", "Fill", "Brush", "Eraser", "Hand", "Zoom"};
    ImGui::SeparatorText(Titles[static_cast<int>(mTool)]);
    const bool CanEdit{!ReadOnly && !mImageEditor->HasFloatingPaste()};
    switch (mTool) {
    case ECanvasTool::Select:
    case ECanvasTool::Fill:
        ImGui::BeginDisabled(!CanEdit);
        mImageEditor->RenderToolControls(static_cast<int>(mTool));
        ImGui::EndDisabled();
        break;
    case ECanvasTool::Brush:
    case ECanvasTool::Eraser:
        mImageEditor->RenderBrushSizeControl(CanEdit);
        ImGui::BeginDisabled(!CanEdit);
        mImageEditor->RenderToolControls(static_cast<int>(ECanvasTool::Brush), mTool == ECanvasTool::Eraser);
        ImGui::EndDisabled();
        break;
    case ECanvasTool::Hand:
        ImGui::TextWrapped("Drag to pan. Scroll to zoom.");
        break;
    case ECanvasTool::Zoom:
        ImGui::TextWrapped("Click to zoom in. Alt+click to zoom out.");
        break;
    }
}

void FTextureEditorToolkit::DrawCanvas(const ImVec2& Size, bool ReadOnly) {
    ImGui::ImageEditor::FCanvasOptions Options{};
    Options.mDrawImage = [](void* UserData, const ImGui::ImageEditor::FCanvasView& View) {
        auto* Toolkit{static_cast<FTextureEditorToolkit*>(UserData)};
        const auto& Bounds{View.mStrokeBounds};
        const FLayerStroke Stroke{View.mStrokePixels, static_cast<int>(Bounds.x), static_cast<int>(Bounds.y), static_cast<int>(Bounds.z), static_cast<int>(Bounds.w)};
        return Toolkit->DrawCanvasImage({View.mTexture, View.mTextureSize, View.mCanvasMin, View.mCanvasMax, View.mImageMin, View.mImageMax, View.mUVMin, View.mUVMax, 0, View.mHovered}, Stroke);
    };
    Options.mUserData = this;
    if (mShowSaved && mInspectorTexture) {
        Options.mReferenceTexture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mInspectorTexture.Get()));
        Options.mReferenceSize = {static_cast<float>(mInspectionWidth), static_cast<float>(mInspectionHeight)};
    }
    Options.mTool = mTool <= ECanvasTool::Brush ? static_cast<int>(mTool) : static_cast<int>(ECanvasTool::Brush);
    switch (mZoomRequest) {
    case EZoomRequest::None:
        break;
    case EZoomRequest::Fit:
        Options.mZoomPreset = ImGui::ImageEditor::ECanvasZoom::Fit;
        break;
    case EZoomRequest::FitWidth:
        Options.mZoomPreset = ImGui::ImageEditor::ECanvasZoom::FitWidth;
        break;
    case EZoomRequest::FitHeight:
        Options.mZoomPreset = ImGui::ImageEditor::ECanvasZoom::FitHeight;
        break;
    }
    Options.mPixelScale = mRequestedPixelScale;
    Options.mZoomStep = mZoomRate;
    const bool PlacingPaste{mImageEditor->HasFloatingPaste()};
    if (PlacingPaste) {
        mPickingColor = false;
    }
    const bool ConsumingColorClick{mConsumeColorClick && !ImGui::IsMouseClicked(ImGuiMouseButton_Left)};
    mConsumeColorClick = ConsumingColorClick && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool BlockingToolInput{mPickingColor || ConsumingColorClick};
    Options.mReadOnly = ReadOnly || BlockingToolInput;
    Options.mPan = mTool == ECanvasTool::Hand && !PlacingPaste && !BlockingToolInput;
    Options.mErase = mTool == ECanvasTool::Eraser;
    Options.mVisibleChannel = TextureImageTools::IsInspectorReady() ? static_cast<int>(mChannelView) : -1;
    Options.mWrap = mShowWrap;
    Options.mZoomTool = mTool == ECanvasTool::Zoom && !PlacingPaste && !BlockingToolInput;
    Options.mSnapZoom = mSnapZoom && mForceNearest;
    Options.mDrawComposite = !mShowSaved && NeedsComposite();
    if (mImageEditor->RenderCanvas(Size, Options)) {
        mRequestedPixelScale = 0.0f;
        mZoomRequest = EZoomRequest::None;
    } else {
        ImGui::TextUnformatted("Image preview is unavailable.");
    }
}

void FTextureEditorToolkit::DrawCanvasStatus(bool ReadOnly) {
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    if (mImageEditor->HasFloatingPaste() || mPickingColor) {
        const char* Hint{mPickingColor ? "Click a pixel to pick its color | Esc to cancel" : "Drag to move | Enter to apply | Esc to cancel"};
        const char* ShortHint{mPickingColor ? "Pick color | Esc to cancel" : "Enter to apply | Esc to cancel"};
        ImGui::TextDisabled("%s", ImGui::CalcTextSize(Hint).x <= ImGui::GetContentRegionAvail().x ? Hint : ShortHint);
        ImGui::SetItemTooltip("%s", Hint);
        return;
    }

    ImGui::TextUnformatted("Zoom");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::BeginDisabled(mPixelZoom <= 0.0f);
    float Percent{mPixelZoom};
    if (ImGui::InputFloat("##CanvasZoom", &Percent, 0.0f, 0.0f, "%.1f%%", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal) && std::isfinite(Percent)) {
        mZoomRequest = EZoomRequest::None;
        mRequestedPixelScale = std::clamp(Percent, 1.0f, 6400.0f) / 100.0f;
    }
    ImGui::SetItemTooltip("Enter a zoom percentage. Ctrl+0 fits the canvas and Ctrl+1 shows 100%%.");
    ImGui::EndDisabled();

    const bool Hidden{!mLayers.empty() && !mLayers[mActiveLayer].mState.mVisible};
    const char* Status{mShowSaved ? "Saved asset" : Hidden ? "Selected layer is hidden" : ReadOnly ? "Preview only" : mDirty ? "Unsaved changes" : "Working image"};
    const float Right{ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x};
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(Status).x <= Right) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Status);
    }
}

bool FTextureEditorToolkit::DrawCanvasImage(ImGuiTexInspect::FCanvasView View, const FLayerStroke& Stroke) {
    mPixelZoom = 100.0f * (View.mImageMax.x - View.mImageMin.x) / ((View.mUVMax.x - View.mUVMin.x) * View.mTextureSize.x);
    const bool Composite{!mShowSaved && NeedsComposite()};
    if (Composite) {
        mCompositeDirty |= View.mTexture != mCompositeSourceTexture;
        mCompositeSourceTexture = View.mTexture;
        if (!PrepareCompositeView(Stroke)) {
            return true;
        }
        View.mTexture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mCompositeView.Get()));
    }
    if (!TextureImageTools::IsInspectorReady()) {
        if (Composite) {
            ImGui::GetWindowDrawList()->AddImage(View.mTexture, View.mImageMin, View.mImageMax, View.mUVMin, View.mUVMax);
            return true;
        }
        return false;
    }

    using namespace ImGuiTexInspect;
    if (mInspectorContext == nullptr) {
        mInspectorContext = CreateContext();
    }
    SetCurrentContext(mInspectorContext);
    View.mFlags = ((mShowTooltip || mPickingColor) ? 0 : InspectorFlags_NoTooltip) | (mShowWrap ? InspectorFlags_ShowWrap : 0) | (mForceNearest ? 0 : InspectorFlags_NoForceFilterNearest) | (mFlipX ? InspectorFlags_FlipX : 0) | (mFlipY ? InspectorFlags_FlipY : 0);
    View.mVisibleChannel = static_cast<int>(mChannelView);

    BufferDesc Pixels{};
    if (Composite) {
        Pixels.Width = mCompositeWidth;
        Pixels.Height = mCompositeHeight;
        Pixels.Stride = 4;
        Pixels.Data_uint8_t = mCompositePixels.data();
        Pixels.LineStride = mCompositeWidth * 4;
        Pixels.BufferByteSize = mCompositePixels.size();
        Pixels.ChannelCount = 4;
        Pixels.Green = 1;
        Pixels.Blue = 2;
        Pixels.Alpha = 3;
    } else if (!mShowSaved) {
        mImageEditor->getImageInfo(&Pixels.Width, &Pixels.Height, &Pixels.Stride);
        if (Pixels.Stride >= 3) {
            Pixels.Data_uint8_t = mImageEditor->getImagePixels();
            Pixels.LineStride = Pixels.Width * Pixels.Stride;
            Pixels.BufferByteSize = static_cast<std::size_t>(Pixels.LineStride) * Pixels.Height;
            Pixels.ChannelCount = static_cast<unsigned char>(Pixels.Stride);
            Pixels.Green = 1;
            Pixels.Blue = 2;
            Pixels.Alpha = 3;
        }
    }

    const FPixelSample Sample{ImGuiTexInspect::DrawCanvasImage("Inspector", View, Pixels.Data_uint8_t ? &Pixels : nullptr)};
    mHasPixel = Sample.mValid;
    mPixelColor = Sample.mColor;
    mPixelX = Sample.mX;
    mPixelY = Sample.mY;
    mHasInspectorView = true;
    if (mPickingColor && Sample.mValid && !ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        mImageEditor->SetPaintColor(Sample.mColor);
        mPickingColor = false;
        mConsumeColorClick = true;
    }

    CurrentInspector_SetMaxAnnotations(mAnnotationLimit);
    ImDrawList* DrawList{ImGui::GetWindowDrawList()};
    DrawList->PushClipRect(mShowWrap ? View.mCanvasMin : View.mImageMin, mShowWrap ? View.mCanvasMax : View.mImageMax, true);
    if (mShowArrows) {
        Arrow Vector{mArrowX, mArrowY, {mArrowScale[0], mArrowScale[1]}};
        Vector.ZeroPoint = {mArrowZero[0], mArrowZero[1]};
        DrawAnnotations(Vector);
    }
    if (mShowValues) {
        DrawAnnotations(ValueText{static_cast<ValueText::Format>(mAnnotationFormat)});
    }
    if (mShowChannelBars) {
        DrawAnnotations(FChannelBars{});
    }
    DrawList->PopClipRect();
    return true;
}

bool FTextureEditorToolkit::PrepareInspectorTexture() {
    if (mInspectorTexture) {
        return true;
    }

    auto* Renderer{mContext->GetRenderer()};
    const auto Format{IsFloatingTexture(mPreviewTexture->GetFormat()) ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM};
    DirectX::ScratchImage Pixels{};
    if (!ReadAssetImage(*Renderer, *mPreviewTexture, Format, Pixels)) {
        mInspectError = "Could not read the saved texture.";
        return false;
    }

    auto* Device{static_cast<ID3D11Device*>(Renderer->GetNativeDevice())};
    if (FAILED(DirectX::CreateShaderResourceView(Device, Pixels.GetImages(), Pixels.GetImageCount(), Pixels.GetMetadata(), mInspectorTexture.GetAddressOf()))) {
        mInspectError = "Could not create the saved texture preview.";
        return false;
    }

    mInspectionWidth = static_cast<int>(Pixels.GetMetadata().width);
    mInspectionHeight = static_cast<int>(Pixels.GetMetadata().height);
    mInspectError.clear();
    return true;
}

void FTextureEditorToolkit::DrawProperties(bool ReadOnly) {
    if (mHasPixel) {
        ImGui::Text("Pixel (%d, %d)", mPixelX, mPixelY);
        ImGui::Text("R %.4f  G %.4f", mPixelColor.x, mPixelColor.y);
        ImGui::Text("B %.4f  A %.4f", mPixelColor.z, mPixelColor.w);
    } else {
        ImGui::TextUnformatted("Pixel");
        ImGui::TextDisabled("Hover over the image");
        ImGui::Spacing();
    }

    if (ImGui::BeginTabBar("Properties Tabs")) {
        if (ImGui::BeginTabItem("Image")) {
            DrawAssetProperties();
            if (mLayers.size() > 1) {
                ImGui::TextDisabled("All layers share the canvas size.");
            }
            ImGui::BeginDisabled(ReadOnly || mImageEditor->HasFloatingPaste());
            mImageEditor->RenderImageControls(mLayers.size() > 1);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("View")) {
            if (mHasInspectorView) {
                DrawInspectorOptions();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Layers")) {
            DrawLayers();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void FTextureEditorToolkit::DrawLayers() {
    using EditorStyle::EIcon;
    using EditorStyle::IconButton;

    const bool HasImage{mImageEditor->getImagePixels() != nullptr};
    ImGui::BeginDisabled(!HasImage || mShowSaved);
    ImGui::BeginDisabled(mLayers.size() >= MaximumTextureLayers);
    if (EditorStyle::Button("Add")) {
        AddLayer();
    }
    ImGui::SetItemTooltip("Add a transparent layer above the selected layer");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(mLayers.size() < 2);
    if (EditorStyle::Button("Remove")) {
        RemoveLayer();
    }
    ImGui::SetItemTooltip("Remove the selected layer");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(mActiveLayer + 1 >= mLayers.size());
    if (EditorStyle::Button("Up")) {
        MoveLayer(1);
    }
    ImGui::SetItemTooltip("Move the selected layer above the next layer");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(mActiveLayer == 0);
    if (EditorStyle::Button("Down")) {
        MoveLayer(-1);
    }
    ImGui::SetItemTooltip("Move the selected layer below the next layer");
    ImGui::EndDisabled();
    ImGui::Separator();

    for (std::size_t Row{mLayers.size()}; Row > 0; --Row) {
        const std::size_t Index{Row - 1};
        auto& Layer{mLayers[Index]};
        ImGui::PushID(static_cast<int>(Index));
        if (IconButton("Layer Visibility", Layer.mState.mVisible ? EIcon::Visible : EIcon::Hidden, false, true, false, Layer.mState.mVisible ? "Hide layer" : "Show layer")) {
            mImageEditor->CommitFloatingPaste();
            Layer.mState.mVisible = !Layer.mState.mVisible;
            mLayerDocument = true;
            mLayerListChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Selectable(Layer.mState.mName.c_str(), Index == mActiveLayer, ImGuiSelectableFlags_None, {0.0f, ImGui::GetFrameHeight()})) {
            SelectLayer(Index);
        }
        ImGui::PopID();
    }

    ImGui::EndDisabled();
}

bool FTextureEditorToolkit::CanEditActiveLayer() const {
    return mImageEditor && mImageEditor->getImagePixels() && mLayers[mActiveLayer].mState.mVisible && !mShowSaved && !mFlipX && !mFlipY && !mShowWrap;
}

bool FTextureEditorToolkit::NeedsComposite() const {
    return mLayers.size() > 1 || (!mLayers.empty() && !mLayers.front().mState.mVisible);
}

void FTextureEditorToolkit::SelectLayer(std::size_t Index) {
    if (Index == mActiveLayer) {
        return;
    }
    auto* Next{mLayers[Index].mEditor.get()};
    mImageEditor->CommitFloatingPaste();
    Next->CopyPaintSettingsFrom(*mImageEditor);
    Next->CopyCanvasViewFrom(*mImageEditor);
    mActiveLayer = Index;
    mImageEditor = Next;
}

void FTextureEditorToolkit::AddLayer() {
    int Width{};
    int Height{};
    mImageEditor->getImageInfo(&Width, &Height, nullptr);
    Core::TArray<Uint8> Pixels(static_cast<std::size_t>(Width) * Height * 4);
    Core::FString Name{};
    for (std::size_t Ordinal{1};; ++Ordinal) {
        Name = "Layer " + std::to_string(Ordinal);
        if (std::ranges::none_of(mLayers, [&Name](const FLayer& Layer) { return Layer.mState.mName == Name; })) {
            break;
        }
    }

    auto Editor{std::make_unique<ImGui::ImageEditor>(false, true, true, true)};
    if (!Editor->LoadPixels(Pixels.data(), Width, Height, 4, Name.c_str())) {
        mMessages.Show("Add Layer", "Could not allocate a new layer.");
        return;
    }
    const std::size_t Index{mActiveLayer + 1};
    mLayers.insert(mLayers.begin() + Index, FLayer{std::move(Editor), {std::move(Name)}});
    SelectLayer(Index);
    mLayerDocument = true;
    mLayerListChanged = true;
}

void FTextureEditorToolkit::RemoveLayer() {
    const std::size_t Removed{mActiveLayer};
    SelectLayer(Removed == 0 ? 1 : Removed - 1);
    mLayers.erase(mLayers.begin() + Removed);
    if (mActiveLayer > Removed) {
        --mActiveLayer;
    }
    mLayerListChanged = true;
}

void FTextureEditorToolkit::MoveLayer(int Offset) {
    mImageEditor->CommitFloatingPaste();
    const auto Next{static_cast<std::size_t>(static_cast<std::ptrdiff_t>(mActiveLayer) + Offset)};
    std::swap(mLayers[mActiveLayer], mLayers[Next]);
    mActiveLayer = Next;
    mLayerListChanged = true;
}

void FTextureEditorToolkit::ResetLayers(std::unique_ptr<ImGui::ImageEditor> Editor) {
    if (mImageEditor) {
        Editor->CopyPaintSettingsFrom(*mImageEditor);
    }
    mImageEditor = Editor.get();
    mLayers.clear();
    mLayers.push_back({std::move(Editor), {"Background"}});
    mActiveLayer = 0;
    mLayerDocument = false;
    mLayerListChanged = true;
}

bool FTextureEditorToolkit::LoadLayers(const Core::FString& Data, int Width, int Height) {
    const Core::TArray<Uint8> Bytes(Data.begin(), Data.end());
    Serialization::FMemoryArchive Archive{Bytes};
    Uint32 Version{};
    Uint32 Active{};
    Uint32 Count{};
    Archive.Serialize("Version", Version);
    Archive.Serialize("Selected", Active);
    if (Version != TextureLayerVersion || !Archive.BeginArray("Layers", Count) || Count == 0 || Count > MaximumTextureLayers || Active >= Count) {
        return false;
    }

    Core::TArray<FLayer> Layers{};
    Layers.reserve(Count);
    for (Uint32 Index{}; Index < Count; ++Index) {
        FLayer Layer{};
        Uint32 Channels{};
        Core::FString Encoded{};
        Archive.Serialize("Name", Layer.mState.mName);
        Archive.Serialize("Visible", Layer.mState.mVisible);
        Archive.Serialize("Channels", Channels);
        Archive.Serialize("PNG", Encoded);
        if (Archive.HasError() || Layer.mState.mName.empty() || (Channels != 1 && Channels != 3 && Channels != 4) || Encoded.empty()) {
            return false;
        }
        DirectX::TexMetadata Metadata{};
        DirectX::ScratchImage Decoded{};
        DirectX::ScratchImage Pixels{};
        const auto* EncodedBytes{reinterpret_cast<const Uint8*>(Encoded.data())};
        constexpr auto Flags{DirectX::WIC_FLAGS_IGNORE_SRGB};
        if (FAILED(DirectX::GetMetadataFromWICMemory(EncodedBytes, Encoded.size(), Flags, Metadata)) || Metadata.width != static_cast<std::size_t>(Width) || Metadata.height != static_cast<std::size_t>(Height) || FAILED(DirectX::LoadFromWICMemory(EncodedBytes, Encoded.size(), Flags, nullptr, Decoded)) || !ConvertImage(*Decoded.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, Pixels)) {
            return false;
        }

        const Uint8* Source{Pixels.GetPixels()};
        Core::TArray<Uint8> Original{};
        if (Channels != 4) {
            const std::size_t Size{static_cast<std::size_t>(Width) * Height};
            Original.resize(Size * Channels);
            for (std::size_t Pixel{}; Pixel < Size; ++Pixel) {
                std::memcpy(Original.data() + Pixel * Channels, Source + Pixel * 4 + (Channels == 1 ? 3 : 0), Channels);
            }
            Source = Original.data();
        }
        Layer.mEditor = std::make_unique<ImGui::ImageEditor>(false, true, true, true);
        if (!Layer.mEditor->LoadPixels(Source, Width, Height, static_cast<int>(Channels), Layer.mState.mName.c_str())) {
            return false;
        }
        Layers.push_back(std::move(Layer));
    }
    Archive.EndArray();
    if (Archive.HasError() || Archive.GetRemainingBytes() != 0) {
        return false;
    }

    auto* ActiveEditor{Layers[Active].mEditor.get()};
    if (mImageEditor) {
        ActiveEditor->CopyPaintSettingsFrom(*mImageEditor);
    }
    mLayers = std::move(Layers);
    mImageEditor = ActiveEditor;
    mActiveLayer = Active;
    mLayerDocument = true;
    mLayerListChanged = true;
    return true;
}

bool FTextureEditorToolkit::SaveLayers(Core::FString& Data) const {
    if (!mLayerDocument) {
        Data.clear();
        return true;
    }

    Core::TArray<Uint8> Bytes{};
    Serialization::FMemoryArchive Archive{Bytes};
    Uint32 Version{TextureLayerVersion};
    Uint32 Active{static_cast<Uint32>(mActiveLayer)};
    Uint32 Count{static_cast<Uint32>(mLayers.size())};
    Archive.Serialize("Version", Version);
    Archive.Serialize("Selected", Active);
    if (!Archive.BeginArray("Layers", Count)) {
        return false;
    }
    for (const auto& Layer : mLayers) {
        DirectX::ScratchImage Pixels{};
        DirectX::Blob Encoded{};
        if (!ReadEditorImage(*Layer.mEditor, Pixels) || FAILED(DirectX::SaveToWICMemory(*Pixels.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, Encoded, &GUID_WICPixelFormat32bppBGRA))) {
            return false;
        }
        Core::FString Name{Layer.mState.mName};
        bool Visible{Layer.mState.mVisible};
        Uint32 Channels{static_cast<Uint32>(Layer.mState.mChannels)};
        Core::FString Png{reinterpret_cast<const char*>(Encoded.GetBufferPointer()), Encoded.GetBufferSize()};
        Archive.Serialize("Name", Name);
        Archive.Serialize("Visible", Visible);
        Archive.Serialize("Channels", Channels);
        Archive.Serialize("PNG", Png);
    }
    Archive.EndArray();
    if (Archive.HasError()) {
        return false;
    }
    Data.assign(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
    return true;
}

void FTextureEditorToolkit::RecordSavedLayers() {
    RefreshEditState();
    mSavedLayers.clear();
    mSavedLayers.reserve(mLayers.size());
    for (const auto& Layer : mLayers) {
        mSavedLayers.push_back(Layer.mState);
    }
    mDirty = false;
}

bool FTextureEditorToolkit::BuildCompositePixels(const FLayerStroke& Stroke, int Top, int Bottom) {
    mImageEditor->getImageInfo(&mCompositeWidth, &mCompositeHeight, nullptr);
    if (mCompositeWidth <= 0 || mCompositeHeight <= 0) {
        return false;
    }
    const std::size_t Size{static_cast<std::size_t>(mCompositeWidth) * mCompositeHeight * 4};
    if (Bottom <= Top || mCompositePixels.size() != Size) {
        Top = 0;
        Bottom = mCompositeHeight;
    }
    mCompositePixels.resize(Size);
    const std::size_t RowSize{static_cast<std::size_t>(mCompositeWidth) * 4};
    std::fill_n(mCompositePixels.data() + Top * RowSize, (Bottom - Top) * RowSize, Uint8{});

    const auto BlendRow{[](Uint8* Destination, const Uint8* Source, int Count, int Channels) {
        for (int X{}; X < Count; ++X, Destination += 4, Source += Channels) {
            const unsigned SourceAlpha{Channels == 3 ? 255u : Source[Channels - 1]};
            if (SourceAlpha == 0) {
                continue;
            }
            if (SourceAlpha == 255 || Destination[3] == 0) {
                for (int Channel{}; Channel < 3; ++Channel) {
                    Destination[Channel] = Channels == 1 ? 255 : Source[Channel];
                }
                Destination[3] = static_cast<Uint8>(SourceAlpha);
                continue;
            }
            const unsigned Top{SourceAlpha * 255};
            const unsigned Bottom{Destination[3] * (255 - SourceAlpha)};
            const unsigned Alpha{Top + Bottom};
            for (int Channel{}; Channel < 3; ++Channel) {
                const unsigned Color{Channels == 1 ? 255u : Source[Channel]};
                Destination[Channel] = static_cast<Uint8>((Color * Top + Destination[Channel] * Bottom + Alpha / 2) / Alpha);
            }
            Destination[3] = static_cast<Uint8>((Alpha + 127) / 255);
        }
    }};
    for (const auto& Layer : mLayers) {
        if (!Layer.mState.mVisible) {
            continue;
        }
        const auto* Source{Layer.mEditor->getImagePixels()};
        int Width{};
        int Height{};
        int Channels{};
        Layer.mEditor->getImageInfo(&Width, &Height, &Channels);
        if (!Source || Width != mCompositeWidth || Height != mCompositeHeight || (Channels != 1 && Channels != 3 && Channels != 4)) {
            return false;
        }
        const bool ActiveStroke{Stroke.mPixels && Layer.mEditor.get() == mImageEditor};
        for (int Y{Top}; Y < Bottom; ++Y) {
            const std::size_t Offset{static_cast<std::size_t>(Y) * Width};
            auto* Destination{mCompositePixels.data() + Offset * 4};
            const auto* Row{Source + Offset * Channels};
            if (ActiveStroke && Y >= Stroke.mY && Y < Stroke.mY + Stroke.mHeight) {
                const int Edge{Stroke.mX + Stroke.mWidth};
                BlendRow(Destination, Row, Stroke.mX, Channels);
                BlendRow(Destination + Stroke.mX * 4, Stroke.mPixels + static_cast<std::size_t>(Y - Stroke.mY) * Stroke.mWidth * Channels, Stroke.mWidth, Channels);
                BlendRow(Destination + Edge * 4, Row + Edge * Channels, Width - Edge, Channels);
            } else {
                BlendRow(Destination, Row, Width, Channels);
            }
        }
    }
    return true;
}

bool FTextureEditorToolkit::PrepareCompositeView(const FLayerStroke& Stroke) {
    const bool Partial{mCompositeView && !mCompositeDirty};
    const bool HadStroke{mCompositeStrokeBottom > mCompositeStrokeTop};
    if (Partial && !HadStroke && !Stroke.mPixels) {
        return true;
    }
    int Top{};
    int Bottom{};
    if (Partial) {
        Top = HadStroke ? mCompositeStrokeTop : Stroke.mY;
        Bottom = HadStroke ? mCompositeStrokeBottom : Stroke.mY + Stroke.mHeight;
        if (Stroke.mPixels) {
            Top = std::min(Top, Stroke.mY);
            Bottom = std::max(Bottom, Stroke.mY + Stroke.mHeight);
        }
    }
    if (!BuildCompositePixels(Stroke, Top, Bottom)) {
        return false;
    }

    auto* Renderer{mContext->GetRenderer()};
    auto* Device{static_cast<ID3D11Device*>(Renderer->GetNativeDevice())};
    auto* Context{static_cast<ID3D11DeviceContext*>(Renderer->GetNativeContext())};
    ComPtr<ID3D11Texture2D> Texture{};
    D3D11_TEXTURE2D_DESC Desc{};
    if (mCompositeView) {
        ComPtr<ID3D11Resource> Resource{};
        mCompositeView->GetResource(Resource.GetAddressOf());
        if (SUCCEEDED(Resource.As(&Texture))) {
            Texture->GetDesc(&Desc);
        }
    }
    if (Texture && Desc.Width == static_cast<UINT>(mCompositeWidth) && Desc.Height == static_cast<UINT>(mCompositeHeight)) {
        const D3D11_BOX Region{0, static_cast<UINT>(Top), 0, Desc.Width, static_cast<UINT>(Bottom), 1};
        Context->UpdateSubresource(Texture.Get(), 0, Partial ? &Region : nullptr, mCompositePixels.data() + static_cast<std::size_t>(Top) * Desc.Width * 4, Desc.Width * 4, 0);
    } else {
        Desc = {};
        Desc.Width = static_cast<UINT>(mCompositeWidth);
        Desc.Height = static_cast<UINT>(mCompositeHeight);
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA Initial{mCompositePixels.data(), Desc.Width * 4, 0};
        ComPtr<ID3D11ShaderResourceView> View{};
        if (FAILED(Device->CreateTexture2D(&Desc, &Initial, Texture.ReleaseAndGetAddressOf())) || FAILED(Device->CreateShaderResourceView(Texture.Get(), nullptr, View.GetAddressOf()))) {
            return false;
        }
        TextureImageTools::RetireTexture(std::move(mCompositeView));
        mCompositeView = std::move(View);
    }
    mCompositeStrokeTop = Stroke.mPixels ? Stroke.mY : 0;
    mCompositeStrokeBottom = Stroke.mPixels ? Stroke.mY + Stroke.mHeight : 0;
    mCompositeDirty = false;
    return true;
}

bool FTextureEditorToolkit::ReadCompositeImage(DirectX::ScratchImage& Image) {
    if (!NeedsComposite()) {
        return ReadEditorImage(*mImageEditor, Image);
    }
    mCompositeDirty = true;
    if (!BuildCompositePixels({}, 0, 0) || FAILED(Image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, mCompositeWidth, mCompositeHeight, 1, 1))) {
        return false;
    }
    std::memcpy(Image.GetPixels(), mCompositePixels.data(), mCompositePixels.size());
    return true;
}

void FTextureEditorToolkit::DrawAssetProperties() {
    using Asset::ETextureFormat;

    if (ImGui::CollapsingHeader("Asset and Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto Format{mPreviewTexture->GetFormat()};
        constexpr const char* Formats[]{"RGBA8", "BGRA8", "RGBA16 Float", "RGBA32 Float", "R32 Float", "D24S8", "D32 Float", "BC7"};
        const auto FormatIndex{static_cast<std::size_t>(Format)};
        const char* FormatName{FormatIndex < std::size(Formats) ? Formats[FormatIndex] : "Unknown"};
        ImGui::TextWrapped("%s", Core::FilePath::ToGenericUtf8(GetAssetPath()).c_str());
        ImGui::Text("Saved image  %u x %u", mPreviewTexture->GetSizeX(), mPreviewTexture->GetSizeY());
        ImGui::Text("Storage  %s / %s", FormatName, mPreviewTexture->IsSRGB() ? "sRGB" : "Linear");
        ImGui::Text("Mip levels  %u", mPreviewTexture->GetMipLevels());
        ImGui::Text("GPU memory  %.2f MiB", static_cast<double>(mPreviewTexture->GetResourceBytes()) / (1024.0 * 1024.0));

        int Width{};
        int Height{};
        int Channels{};
        mImageEditor->getImageInfo(&Width, &Height, &Channels);
        if (mImageEditor->getImagePixels()) {
            ImGui::Text("Working image  %d x %d", Width, Height);
            ImGui::Text("Working channels  %d", Channels);
            if (mArchiveIndex) {
                ImGui::TextWrapped("Source  %s", mArchiveEntries[*mArchiveIndex].c_str());
            } else if (!mImagePath.empty()) {
                ImGui::TextWrapped("Source  %s", Core::FilePath::ToGenericUtf8(mImagePath.filename()).c_str());
            }
            if (IsFloatingTexture(Format)) {
                ImGui::TextWrapped("The working image uses 8-bit channels. Show Saved displays floating-point pixels.");
            }
        }
        if (Format == ETextureFormat::BC7) {
            ImGui::Checkbox("Keep BC7 on Save", &mPreserveBC7);
        }
        EditorStyle::DrawFieldLabel("Export Channels");
        ImGui::Combo("##ExportChannels", &mExportChannels, "Source\0RGBA\0RGB\0Alpha\0");
        ImGui::Checkbox("Optimize RGBA on Export", &mOptimizeRGBA);
        ImGui::SetItemTooltip("Normalize RGB in fully transparent pixels to improve compression.");
    }
}

void FTextureEditorToolkit::DrawInspectorOptions() {
    using namespace ImGuiTexInspect;
    ImGui::Checkbox("Pixel Tooltip", &mShowTooltip);
    ImGui::Checkbox("Wrap", &mShowWrap);
    ImGui::Checkbox("Force Nearest", &mForceNearest);
    ImGui::BeginDisabled(!mForceNearest);
    ImGui::Checkbox("Snap Enlarged Zoom", &mSnapZoom);
    ImGui::EndDisabled();
    ImGui::Checkbox("Flip X", &mFlipX);
    ImGui::SameLine();
    ImGui::Checkbox("Flip Y", &mFlipY);
    if (mShowWrap || mFlipX || mFlipY) {
        mImageEditor->CommitFloatingPaste();
    }
    EditorStyle::DrawFieldLabel("Zoom Rate");
    ImGui::SliderFloat("##ZoomRate", &mZoomRate, 1.05f, 3.0f, "%.2f");

    if (ImGui::CollapsingHeader("Composite View", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(mChannelView != EChannelView::RGBA);
        DrawColorChannelSelector();
        DrawAlphaModeSelector(true);
        ImGui::EndDisabled();
        DrawGridEditor();
    }
    if (ImGui::CollapsingHeader("Color Matrix")) {
        ImGui::BeginDisabled(mChannelView != EChannelView::RGBA);
        constexpr const char* Presets[]{"Original", "Negative", "Swap R/B", "Transparency"};
        constexpr float Matrices[][16]{
            {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
            {-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1},
            {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, 0}
        };
        constexpr float Offsets[][4]{{}, {1, 1, 1, 0}, {}, {1, 1, 1, 1}};
        constexpr int PresetCount{static_cast<int>(std::size(Presets))};
        int CurrentPreset{PresetCount};
        for (int Index{}; Index < PresetCount; ++Index) {
            if (CurrentInspector_MatchesColorMatrix(Matrices[Index], Offsets[Index])) {
                CurrentPreset = Index;
                break;
            }
        }

        EditorStyle::DrawFieldLabel("Preset");
        if (ImGui::BeginCombo("##ColorPreset", CurrentPreset < PresetCount ? Presets[CurrentPreset] : "Custom")) {
            for (int Index{}; Index < PresetCount; ++Index) {
                const bool IsSelected{CurrentPreset == Index};
                if (ImGui::Selectable(Presets[Index], IsSelected)) {
                    CurrentInspector_SetColorMatrix(Matrices[Index], Offsets[Index]);
                }
                if (IsSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Reset Matrix")) {
            CurrentInspector_ResetColorMatrix();
        }
        DrawColorMatrixEditor();
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Annotations")) {
        ImGui::Checkbox("Pixel Values", &mShowValues);
        if (mShowValues) {
            EditorStyle::DrawFieldLabel("Format");
            ImGui::Combo("##ValueFormat", &mAnnotationFormat, "RGBA Hex\0Hex Bytes\0Decimal Bytes\0Float\0");
        }
        ImGui::Checkbox("Vector Arrows", &mShowArrows);
        if (mShowArrows) {
            EditorStyle::DrawFieldLabel("Preset");
            if (ImGui::Combo("##ArrowPreset", &mArrowPreset, "Normal Map\0Normalized Float\0")) {
                mArrowX = 0;
                mArrowY = 1;
                Arrow Preset{};
                Preset.UsePreset(static_cast<Arrow::Preset>(mArrowPreset));
                mArrowZero[0] = Preset.ZeroPoint.x;
                mArrowZero[1] = Preset.ZeroPoint.y;
                mArrowScale[0] = Preset.LineScale.x;
                mArrowScale[1] = Preset.LineScale.y;
            }
            EditorStyle::DrawFieldLabel("X Channel");
            ImGui::Combo("##ArrowX", &mArrowX, "R\0G\0B\0A\0");
            EditorStyle::DrawFieldLabel("Y Channel");
            ImGui::Combo("##ArrowY", &mArrowY, "R\0G\0B\0A\0");
            EditorStyle::DrawFieldLabel("Zero Point");
            ImGui::DragFloat2("##ArrowZero", mArrowZero, 0.01f);
            EditorStyle::DrawFieldLabel("Scale");
            ImGui::DragFloat2("##ArrowScale", mArrowScale, 0.01f);
        }
        ImGui::Checkbox("Channel Bars", &mShowChannelBars);
        EditorStyle::DrawFieldLabel("Limit");
        ImGui::SliderInt("##AnnotationLimit", &mAnnotationLimit, 1, 10000);
    }
}

bool FTextureEditorToolkit::LoadTextureForEditing() {
    const auto& SavedLayers{mPreviewTexture->GetEditorLayerData()};
    if (SavedLayers.empty()) {
        DirectX::ScratchImage Pixels{};
        if (!ReadAssetImage(*mContext->GetRenderer(), *mPreviewTexture, DXGI_FORMAT_R8G8B8A8_UNORM, Pixels)) {
            mEditError = "Could not read this texture for editing.";
            return false;
        }
        auto Editor{std::make_unique<ImGui::ImageEditor>(false, true, true, true)};
        const auto Width{static_cast<int>(Pixels.GetMetadata().width)};
        const auto Height{static_cast<int>(Pixels.GetMetadata().height)};
        if (!Editor->LoadPixels(Pixels.GetPixels(), Width, Height, 4, Core::FilePath::ToUtf8(GetAssetPath()).c_str())) {
            mEditError = "Could not initialize the image editor.";
            return false;
        }
        ResetLayers(std::move(Editor));
    } else if (!LoadLayers(SavedLayers, static_cast<int>(mPreviewTexture->GetSizeX()), static_cast<int>(mPreviewTexture->GetSizeY()))) {
        mEditError = "Could not read the saved texture layers.";
        return false;
    }
    RecordSavedLayers();
    mShowSaved = false;
    mImagePath.clear();
    mArchivePath.clear();
    mArchiveIndex.reset();
    mArchiveEntries.clear();
    mEditError.clear();
    return true;
}

void FTextureEditorToolkit::RefreshEditState() {
    if (!mImageEditor || !mImageEditor->getImagePixels()) {
        return;
    }
    bool Changed{mLayerListChanged};
    mLayerListChanged = false;
    for (auto& Layer : mLayers) {
        const auto Revision{Layer.mEditor->GetContentRevision()};
        if (Layer.mCheckedRevision == Revision) {
            continue;
        }
        auto& State{Layer.mState};
        Layer.mEditor->getImageInfo(&State.mWidth, &State.mHeight, &State.mChannels);
        const std::size_t Size{static_cast<std::size_t>(State.mWidth) * State.mHeight * State.mChannels};
        const std::string_view Bytes{reinterpret_cast<const char*>(Layer.mEditor->getImagePixels()), Size};
        State.mPixelHash = std::hash<std::string_view>{}(Bytes);
        Layer.mCheckedRevision = Revision;
        Changed = true;
    }
    if (Changed) {
        mCompositeDirty = true;
        if (!NeedsComposite() && mCompositeView) {
            TextureImageTools::RetireTexture(std::move(mCompositeView));
            mCompositePixels = Core::TArray<Uint8>{};
        }
        mDirty = !std::ranges::equal(mLayers, mSavedLayers, {}, &FLayer::mState);
    }
}

bool FTextureEditorToolkit::SaveTexture() {
    if (mImageEditor) {
        mImageEditor->CommitFloatingPaste();
        RefreshEditState();
    }
    if (!mDirty || !mImageEditor || mPreviewTexture == nullptr) {
        return !mDirty;
    }
    auto Updated{CoreUObject::NewObject<Asset::UTexture2D>()};
    auto* Device{static_cast<ID3D11Device*>(mContext->GetRenderer()->GetNativeDevice())};
    if (mPreviewTexture->GetFormat() == Asset::ETextureFormat::BC7 && mPreserveBC7) {
        int Width{};
        int Height{};
        mImageEditor->getImageInfo(&Width, &Height, nullptr);
        if (Width % 4 != 0 || Height % 4 != 0) {
            mEditError = "BC7 requires width and height divisible by four. Clear Keep BC7 to save this size as RGBA8.";
            return false;
        }
    }
    DirectX::ScratchImage Pixels{};
    Core::FString Layers{};
    if (!ReadCompositeImage(Pixels) || !SaveLayers(Layers) || !PrepareUpdatedTexture(Pixels, *mPreviewTexture, Device, *Updated, mPreserveBC7)) {
        mEditError = "Could not prepare the edited texture for saving.";
        mContext->AddLog(mEditError);
        return false;
    }
    Updated->SetEditorLayerData(std::move(Layers));
    ComPtr<ID3D11ShaderResourceView> Previous{mPreviewTexture->GetShaderResourceView()};
    mEditError.clear();
    if (!mContext->GetProject()->GetAssets()->SaveTexture(GetAssetPath(), *mPreviewTexture, *Updated, mEditError)) {
        mContext->AddLog(mEditError);
        return false;
    }

    TextureImageTools::RetireTexture(std::move(Previous));
    TextureImageTools::RetireTexture(std::move(mInspectorTexture));
    RecordSavedLayers();
    mEditError.clear();
    NotifyChanged();
    return true;
}

void FTextureEditorToolkit::OpenImage() {
    const auto Source{mArchiveIndex ? mArchivePath : mImagePath};
    const auto Path{ChooseImage(Source.empty() ? mContext->GetProject()->GetRootPath() : Source.parent_path())};
    if (!Path.empty()) {
        if (Core::FilePath::ToLowerExtension(Path) == L".zip") {
            OpenArchive(Path);
            return;
        }
        ConfirmReplace([this, Path]() {
            LoadImage(Path);
        });
    }
}

void FTextureEditorToolkit::ReloadImage() {
    if (mArchiveIndex) {
        const auto Index{*mArchiveIndex};
        ConfirmReplace([this, Index]() {
            LoadArchiveImage(mArchivePath, mArchiveEntries, Index);
        });
    } else if (!mImagePath.empty()) {
        const auto Path{mImagePath};
        ConfirmReplace([this, Path]() {
            LoadImage(Path);
        });
    }
}

void FTextureEditorToolkit::LoadImage(const std::filesystem::path& Path) {
    auto Editor{std::make_unique<ImGui::ImageEditor>(false, true, true, true)};
    if (!LoadExternalImage(*Editor, Path)) {
        mEditError = "Could not open " + Core::FilePath::ToUtf8(Path);
        return;
    }
    ResetLayers(std::move(Editor));
    mShowSaved = false;
    RefreshEditState();
    mImagePath = Path;
    mArchivePath.clear();
    mArchiveEntries.clear();
    mArchiveIndex.reset();
    mEditError.clear();
}

void FTextureEditorToolkit::OpenArchive(const std::filesystem::path& Path) {
    Core::TArray<char> Bytes{};
    auto Zip{ReadArchive(Path, Bytes)};
    if (!Zip) {
        mEditError = "Could not read the ZIP archive.";
        return;
    }
    Core::TArray<Core::FString> Entries{};
    const auto Count{zip_entries_total(Zip.get())};
    for (std::ptrdiff_t Index{0}; Index < Count; ++Index) {
        if (zip_entry_openbyindex(Zip.get(), static_cast<std::size_t>(Index)) != 0) {
            continue;
        }
        const char* Name{zip_entry_name(Zip.get())};
        if (Name && zip_entry_isdir(Zip.get()) == 0 && IsBrowsableImage(GetArchiveImageType(Name))) {
            Entries.emplace_back(Name);
        }
        zip_entry_close(Zip.get());
    }
    if (Entries.empty()) {
        mEditError = "No images were found in the ZIP archive.";
        return;
    }
    std::ranges::sort(Entries);
    mArchivePickerPath = Path;
    mArchivePickerEntries = std::move(Entries);
    mShowArchivePicker = true;
}

void FTextureEditorToolkit::DrawArchivePicker() {
    if (mShowArchivePicker) {
        ImGui::OpenPopup("Images in ZIP");
        mShowArchivePicker = false;
    }
    if (ImGui::BeginPopup("Images in ZIP")) {
        for (std::size_t Index{0}; Index < mArchivePickerEntries.size(); ++Index) {
            ImGui::PushID(static_cast<int>(Index));
            if (ImGui::Selectable(mArchivePickerEntries[Index].c_str())) {
                ConfirmReplace([this, Index]() {
                    LoadArchiveImage(mArchivePickerPath, mArchivePickerEntries, Index);
                });
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
}

void FTextureEditorToolkit::LoadArchiveImage(const std::filesystem::path& Archive, const Core::TArray<Core::FString>& Entries, std::size_t Index) {
    auto Editor{std::make_unique<ImGui::ImageEditor>(false, true, true, true)};
    if (Index >= Entries.size() || !LoadArchivedImage(*Editor, Archive, Entries[Index])) {
        mEditError = "Could not open the image in the ZIP archive.";
        return;
    }
    ResetLayers(std::move(Editor));
    mShowSaved = false;
    RefreshEditState();
    mArchivePath = Archive;
    mArchiveEntries = Entries;
    mArchiveIndex = Index;
    mImagePath.clear();
    mEditError.clear();
}

void FTextureEditorToolkit::BrowseImage(int Offset) {
    if (mArchiveIndex && !mArchiveEntries.empty()) {
        const auto Count{static_cast<std::ptrdiff_t>(mArchiveEntries.size())};
        const auto Next{static_cast<std::size_t>((static_cast<std::ptrdiff_t>(*mArchiveIndex) + Offset + Count) % Count)};
        ConfirmReplace([this, Next]() {
            LoadArchiveImage(mArchivePath, mArchiveEntries, Next);
        });
        return;
    }
    Core::TArray<std::filesystem::path> Paths{};
    std::error_code Error{};
    for (std::filesystem::directory_iterator Entry{mImagePath.parent_path(), Error}; !Error && Entry != std::filesystem::directory_iterator{}; Entry.increment(Error)) {
        if (Entry->is_regular_file(Error) && IsBrowsableImage(Entry->path())) {
            Paths.push_back(Entry->path());
        }
    }
    std::ranges::sort(Paths);
    const auto Current{std::ranges::find(Paths, mImagePath)};
    if (Current == Paths.end() || Paths.size() < 2) {
        return;
    }
    const auto Count{static_cast<std::ptrdiff_t>(Paths.size())};
    const auto Path{Paths[(Current - Paths.begin() + Offset + Count) % Count]};
    ConfirmReplace([this, Path]() {
        LoadImage(Path);
    });
}

void FTextureEditorToolkit::ExportImage() {
    const auto Initial{(mImagePath.empty() ? mContext->GetProject()->GetRootPath() / GetAssetPath().filename() : mImagePath).replace_extension(L".png")};
    const auto Path{ChooseExport(Initial)};
    if (Path.empty()) {
        return;
    }

    mImageEditor->CommitFloatingPaste();
    constexpr int Channels[]{0, 4, 3, 1};
    const int ChannelCount{Channels[mExportChannels]};
    const auto Extension{Core::FilePath::ToLowerExtension(Path)};
    bool Saved{};
    if (Extension == L".tiff" || Extension == L".tif") {
        DirectX::ScratchImage Pixels{};
        if (ReadCompositeImage(Pixels)) {
            const WICPixelFormatGUID* TargetFormat{ChannelCount == 3 ? &GUID_WICPixelFormat24bppBGR : ChannelCount == 4 ? &GUID_WICPixelFormat32bppBGRA : nullptr};
            if (ChannelCount == 1) {
                DirectX::ScratchImage Alpha{};
                if (SUCCEEDED(Alpha.Initialize2D(DXGI_FORMAT_R8_UNORM, Pixels.GetMetadata().width, Pixels.GetMetadata().height, 1, 1))) {
                    for (std::size_t Index{0}; Index < Alpha.GetPixelsSize(); ++Index) {
                        Alpha.GetPixels()[Index] = Pixels.GetPixels()[Index * 4 + 3];
                    }
                    Saved = SUCCEEDED(DirectX::SaveToWICFile(*Alpha.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatTiff, Path.c_str()));
                }
            } else {
                if (mOptimizeRGBA && ChannelCount != 3) {
                    for (std::size_t Offset{0}; Offset < Pixels.GetPixelsSize(); Offset += 4) {
                        auto* Pixel{Pixels.GetPixels() + Offset};
                        if (Pixel[3] == 0) {
                            std::fill_n(Pixel, 3, Uint8{255});
                        }
                    }
                }
                Saved = SUCCEEDED(DirectX::SaveToWICFile(*Pixels.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatTiff, Path.c_str(), TargetFormat));
            }
        }
    } else if (NeedsComposite()) {
        DirectX::ScratchImage Pixels{};
        const auto Name{Core::FilePath::ToUtf8(Path)};
        ImGui::ImageEditor Export{false, true, true, true};
        Saved = ReadCompositeImage(Pixels) && Export.LoadPixels(Pixels.GetPixels(), static_cast<int>(Pixels.GetMetadata().width), static_cast<int>(Pixels.GetMetadata().height), 4, Name.c_str()) && Export.SaveCopy(Name.c_str(), ChannelCount, mOptimizeRGBA);
    } else {
        Saved = mImageEditor->SaveCopy(Core::FilePath::ToUtf8(Path).c_str(), ChannelCount, mOptimizeRGBA);
    }
    mEditError = Saved ? Core::FString{} : "Could not export " + Core::FilePath::ToUtf8(Path);
}

std::optional<FMessageBox::FSavePrompt> FTextureEditorToolkit::GetClosePrompt() {
    if (mImageEditor) {
        mImageEditor->CommitFloatingPaste();
    }
    RefreshEditState();
    if (!mDirty || mPreviewTexture == nullptr) {
        return {};
    }
    return FMessageBox::FSavePrompt{"Texture", "Save changes to " + Core::FilePath::ToUtf8(GetAssetPath().filename()) + "?", [this]() {
        return SaveTexture();
    }, [this]() {
        return LoadTextureForEditing();
    }};
}

bool FTextureEditorToolkit::RequestTabClose() {
    auto Prompt{GetClosePrompt()};
    if (!Prompt) {
        return true;
    }
    mMessages.ConfirmSave({std::move(*Prompt)}, [this]() {
        GetTab().SetVisible(false);
    });
    return false;
}

void FTextureEditorToolkit::ConfirmReplace(std::function<void()> Action) {
    if (auto Prompt{GetClosePrompt()}) {
        Prompt->mDiscard = {};
        mMessages.ConfirmSave({std::move(*Prompt)}, std::move(Action));
    } else {
        Action();
    }
}
