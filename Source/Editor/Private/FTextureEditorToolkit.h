/**
 * @file FTextureEditorToolkit.h
 * @brief 텍스처 정보를 표시하고 이미지 미리 보기를 제공하는 에셋 툴킷입니다.
 * @ingroup Editor
 */

#pragma once

#include "FAssetEditorToolkit.h"
#include "CoreUObject/TObjectRef.h"
#include <d3d11.h>
#include <imgui.h>
#include <memory>
#include <wrl/client.h>

namespace Asset {
    class UTexture2D;
}

namespace DirectX {
    class ScratchImage;
}

namespace ImGui {
    class ImageEditor;
}

namespace ImGuiTexInspect {
    struct Context;
    struct FCanvasView;
}

namespace Editor {
    class FEditorContext;
}

namespace Editor {

    /**
     * @brief 텍스처 정보를 표시하고 이미지 미리 보기를 제공하는 에셋 툴킷입니다.
     */
    class FTextureEditorToolkit : public Editor::FAssetEditorToolkit {

    private:
        enum class ECanvasTool : Uint8 {
            Select,
            Fill,
            Brush,
            Eraser,
            Hand,
            Zoom
        };

        enum class EZoomRequest : Uint8 {
            None,
            Fit,
            FitWidth,
            FitHeight
        };

        enum class EChannelView {
            RGBA = -1,
            Red,
            Green,
            Blue,
            Alpha
        };

        struct FLayerState {
            Core::FString mName{};

            std::size_t mPixelHash{};
            int mWidth{};
            int mHeight{};
            int mChannels{};

            bool mVisible{true};

            bool operator==(const FLayerState& Other) const = default;
        };

        struct FLayer {
            std::unique_ptr<ImGui::ImageEditor> mEditor{};
            FLayerState mState{};
            Uint64 mCheckedRevision{};
        };

        struct FLayerStroke {
            const Uint8* mPixels{};

            int mX{};
            int mY{};
            int mWidth{};
            int mHeight{};
        };

    public:
        /**
         * @brief FTextureEditorToolkit의 초기 상태와 전달받은 의존성을 구성합니다.
         * @param[in] Context 편집 대상과 공유 서비스를 제공하는 에디터 컨텍스트의 비소유 주소입니다.
         * @param[in] Path 대상 에셋 또는 콘텐츠 경로입니다.
         * @param[in] Texture 미리 보기에 사용할 텍스처 또는 D3D11 텍스처 뷰입니다.
         */
        FTextureEditorToolkit(Editor::FEditorContext* Context, const std::filesystem::path& Path, Asset::UTexture2D* Texture, Editor::FMessageBox& Messages);

        /**
         * @brief 객체를 소멸하고 소유 멤버와 연결 상태를 정리합니다.
         */
        ~FTextureEditorToolkit() override;

    public:
        std::optional<Editor::FMessageBox::FSavePrompt> GetClosePrompt() override;

    private:
        /**
         * @brief 텍스처 탭 이름을 갱신하고 텍스처나 GPU 리소스가 없으면 탭을 닫습니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         */
        void UpdateEditor(float DeltaTime) override;

        /**
         * @brief 에셋 또는 프로젝트의 미리 보기 이미지를 표시합니다.
         */
        void DrawPreview();
        void DrawCommands();
        void DrawChannelButtons();
        void DrawFileMenu();
        void DrawEditCommands(bool Enabled);
        void DrawZoomMenu();
        void DrawTools(bool ReadOnly, bool Horizontal);
        void DrawToolOptions(bool ReadOnly);
        void DrawToolbox(const ImVec2& Size, bool ReadOnly, bool Horizontal);
        void DrawWorkspace(const ImVec2& Available, bool ReadOnly);
        void DrawCanvasArea(const ImVec2& Size, bool ReadOnly);
        void DrawCanvas(const ImVec2& Size, bool ReadOnly);
        void DrawCanvasStatus(bool ReadOnly);
        bool DrawCanvasImage(ImGuiTexInspect::FCanvasView View, const FLayerStroke& Stroke);
        void DrawProperties(bool ReadOnly);
        void DrawLayers();
        void DrawAssetProperties();
        void DrawInspectorOptions();

        bool CanEditActiveLayer() const;
        bool NeedsComposite() const;
        void SelectLayer(std::size_t Index);
        void AddLayer();
        void RemoveLayer();
        void MoveLayer(int Offset);
        void ResetLayers(std::unique_ptr<ImGui::ImageEditor> Editor);
        bool LoadLayers(const Core::FString& Data, int Width, int Height);
        bool SaveLayers(Core::FString& Data) const;
        void RecordSavedLayers();
        bool BuildCompositePixels(const FLayerStroke& Stroke, int Top, int Bottom);
        bool PrepareCompositeView(const FLayerStroke& Stroke);
        bool ReadCompositeImage(DirectX::ScratchImage& Image);

        bool PrepareInspectorTexture();
        bool LoadTextureForEditing();
        bool SaveTexture();
        void OpenImage();
        void ReloadImage();
        void LoadImage(const std::filesystem::path& Path);
        void OpenArchive(const std::filesystem::path& Path);
        void DrawArchivePicker();
        void LoadArchiveImage(const std::filesystem::path& Archive, const Core::TArray<Core::FString>& Entries, std::size_t Index);
        void ExportImage();
        void BrowseImage(int Offset);
        void RefreshEditState();
        bool RequestTabClose();
        void ConfirmReplace(std::function<void()> Action);

    private:
        /**
         * @brief 편집 대상과 공유 서비스에 접근하는 에디터 컨텍스트의 비소유 주소입니다.
         */
        Editor::FEditorContext* mContext{};
        Editor::FMessageBox& mMessages;

        /**
         * @brief 현재 미리 보는 텍스처의 비소유 주소입니다.
         */
        CoreUObject::TObjectRef<Asset::UTexture2D> mPreviewTexture{};

        /**
         * @brief 텍스처 이미지의 확대·이동·표시를 담당하는 뷰어입니다.
         */
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mInspectorTexture{};
        ImGuiTexInspect::Context* mInspectorContext{};

        ImGui::ImageEditor* mImageEditor{};
        Core::TArray<FLayer> mLayers{};
        Core::TArray<FLayerState> mSavedLayers{};

        Core::TArray<Uint8> mCompositePixels{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mCompositeView{};
        ImTextureID mCompositeSourceTexture{};

        std::size_t mActiveLayer{};

        int mCompositeWidth{};
        int mCompositeHeight{};
        int mCompositeStrokeTop{};
        int mCompositeStrokeBottom{};

        bool mLayerDocument{};
        bool mLayerListChanged{};

        bool mCompositeDirty{true};

        std::filesystem::path mImagePath{};

        std::filesystem::path mArchivePath{};
        Core::TArray<Core::FString> mArchiveEntries{};
        std::optional<std::size_t> mArchiveIndex{};

        std::filesystem::path mArchivePickerPath{};
        Core::TArray<Core::FString> mArchivePickerEntries{};

        Core::FString mEditError{};
        Core::FString mInspectError{};

        ImVec4 mPixelColor{};
        int mPixelX{};
        int mPixelY{};

        int mInspectionWidth{};
        int mInspectionHeight{};

        int mAnnotationFormat{3};
        int mAnnotationLimit{1000};
        int mArrowPreset{};
        int mArrowX{};
        int mArrowY{1};

        int mExportChannels{};

        float mArrowZero[2]{0.5f, 0.5f};
        float mArrowScale[2]{1.0f, 1.0f};

        float mZoomRate{1.3f};
        float mPixelZoom{};
        float mRequestedPixelScale{};

        float mToolboxWidth{13.0f};
        float mToolboxHeight{11.5f};
        float mPropertiesWidth{15.0f};
        float mPropertiesHeight{9.0f};

        ECanvasTool mTool{ECanvasTool::Brush};
        EZoomRequest mZoomRequest{EZoomRequest::None};

        bool mDirty{};
        bool mShowSaved{};

        EChannelView mChannelView{EChannelView::RGBA};
        EChannelView mPreviousChannelView{EChannelView::RGBA};

        bool mShowProperties{true};

        bool mHasPixel{};
        bool mHasInspectorView{};

        bool mShowValues{};
        bool mShowArrows{};
        bool mShowChannelBars{};
        bool mShowTooltip{};

        bool mPickingColor{};
        bool mConsumeColorClick{};

        bool mShowWrap{};
        bool mForceNearest{true};
        bool mSnapZoom{true};
        bool mFlipX{};
        bool mFlipY{};

        bool mPreserveBC7{true};
        bool mOptimizeRGBA{};

        bool mShowArchivePicker{};
    };

}
