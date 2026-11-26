/**
 * @file FMaterialEditorToolkit.cpp
 * @brief 머티리얼 속성·미리 보기·저장과 변경 확인을 제공하는 에셋 툴킷입니다.
 * @ingroup Editor
 */

#include "FMaterialEditorToolkit.h"
#include "Editor/FEditorToolkitHost.h"
#include "Core/FilePath.h"
#include "EditorStyle.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorAssetCatalog.h"
#include "Editor/FAssetThumbnailCache.h"
#include "Engine/FProject.h"
#include "Engine/PrimitiveMesh.h"
#include "Asset/UMaterial.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Renderer/Renderer.h"
#include "Viewer/FMeshViewerContent.h"
#include <imgui.h>
#include <utility>

using namespace Editor;

FMaterialEditorToolkit::FMaterialEditorToolkit(FEditorContext* Context, const std::filesystem::path& Path, Asset::UMaterial* Material, FEditorAssetCatalog& Catalog, FAssetThumbnailCache& Thumbnails, FMessageBox& Messages)
	: FAssetEditorToolkit{Path, Asset::EAssetType::Material, "###MaterialEditor:" + Core::FilePath::ToGenericUtf8(Path)},
	  mContext{Context},
	  mMessages{Messages},
	  mEditingMaterial{Material},
	  mAssetCatalog{&Catalog},
	  mThumbnails{&Thumbnails},
	  mViewer{Context->GetRenderer(), Context->GetProject()->GetAssets(), true} {
    GetTab().SetIcon(EEditorIcon::Material, IM_COL32(64, 192, 64, 255));
    FEditorToolkitHost& Host{GetToolkitHost()};
    Host.AddWidget("Preview", "Viewport", [this](float) {
        DrawViewport();
    });
    Host.GetTabManager().FindTab("Preview")->SetWindowFlags(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Host.AddWidget("Details", "Details", [this](float) {
        DrawDetails();
    },
        EEditorDockRegion::Details);

    GetTab().SetCanCloseHandler([this]() {
        return RequestTabClose();
    });

    if (!mViewer.SetAsset(Material, Asset::EAssetType::Material)) {
        mViewportError = mViewer.GetError();
    }
}

FMaterialEditorToolkit::~FMaterialEditorToolkit() = default;

void FMaterialEditorToolkit::UpdateEditor(float) {
    GetTab().SetName(EditorStyle::TabIconPadding + Core::FilePath::ToUtf8(GetAssetPath().stem()) + (mMaterialDirty ? "*" : "") + "###MaterialEditor:" + Core::FilePath::ToGenericUtf8(GetAssetPath()));
}

bool FMaterialEditorToolkit::SaveMaterial() {
    if (mEditingMaterial == nullptr || !mContext->GetProject()->GetAssets()->SaveMaterial(GetAssetPath(), *mEditingMaterial, mMaterialError)) {
        return false;
    }

    mMaterialDirty = false;
    NotifyChanged();
    mContext->AddLog("Saved " + Core::FilePath::ToUtf8(GetAssetPath()));

    return true;
}

std::optional<FMessageBox::FSavePrompt> FMaterialEditorToolkit::GetClosePrompt() {
    if (!mMaterialDirty || mEditingMaterial == nullptr) {
        return {};
    }

    return FMessageBox::FSavePrompt{"Material", "Save changes to " + Core::FilePath::ToUtf8(GetAssetPath().filename()) + "?", [this]() {
        const bool Saved{SaveMaterial()};

        if (!Saved) {
            GetTab().Open();
        }

        return Saved;
    }, [this]() {
        return ReloadMaterial();
    }};
}

bool FMaterialEditorToolkit::RequestTabClose() {
    auto Prompt{GetClosePrompt()};

    if (!Prompt) {
        return true;
    }

    mMessages.ConfirmSave({std::move(*Prompt)}, [this]() {
        GetTab().SetVisible(false);
    });

    return false;
}

bool FMaterialEditorToolkit::ReloadMaterial() {
    if (!mContext->GetProject()->GetAssets()->ReloadMaterial(GetAssetPath(), mMaterialError)) {
        GetTab().Open();

        return false;
    }

    mMaterialDirty = false;

    return true;
}

void FMaterialEditorToolkit::RequestReload() {
    if (auto Prompt{GetClosePrompt()}) {
        Prompt->mDiscard = {};
        mMessages.ConfirmSave({std::move(*Prompt)}, [this]() {
            ReloadMaterial();
        });

        return;
    }

    ReloadMaterial();
}

bool FMaterialEditorToolkit::SetMaterialTexture(const Core::FString& Path) {
    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};

    if (!Path.empty() && Assets->LoadTexture(Core::FilePath::FromUtf8(Path), mMaterialError) == nullptr) {
        return false;
    }

    if (!mEditingMaterial->SetBaseColorTexturePath(Path) || !mEditingMaterial->ResolveTexture(*Assets, mMaterialError)) {
        if (mMaterialError.empty()) {
            mMaterialError = "Invalid texture path";
        }

        return false;
    }

    mMaterialError.clear();
    mMaterialDirty = true;

    return true;
}

bool FMaterialEditorToolkit::SetMaterialShader(const Core::FString& Path) {
    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};
    RenderCore::UShader* Shader{Path.empty() ? RenderCore::UShader::GetDefault() : RenderCore::UShader::FindBuiltin(Path)};

    if (Shader == nullptr) {
        Shader = Assets->LoadShader(Core::FilePath::FromUtf8(Path), mMaterialError);
    }

    if (Shader == nullptr) {
        return false;
    }

    auto Candidate{CoreUObject::NewObject<Asset::UMaterial>()};
    Candidate->SetTwoSided(mEditingMaterial->IsTwoSided());

    Renderer::FRenderer* Renderer{dynamic_cast<Renderer::FRenderer*>(mContext->GetRenderer())};

    if (!Candidate->SetShader(Shader) || Renderer == nullptr || Renderer->GetMaterialPipeline(*Candidate) == nullptr) {
        mMaterialError = "Cannot use this material shader. Check the shader diagnostics in the log.";

        return false;
    }

    mEditingMaterial->SetShader(Shader);
    mMaterialError.clear();
    mMaterialDirty = true;

    return true;
}

void FMaterialEditorToolkit::HandleCommands() {
    if (mEditingMaterial != nullptr && mMaterialDirty && mContext->CanEditWorld() && GetTab().IsFocused() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        SaveMaterial();
    }
}

void FMaterialEditorToolkit::DrawViewport() {
    if (!GetTab().IsVisible()) {
        return;
    }

    auto* Content{static_cast<Viewer::FMeshViewerContent*>(mViewer.GetContent())};

    if (Content == nullptr || mEditingMaterial == nullptr) {
        ImGui::TextWrapped("%s", mViewportError.empty() ? "The preview material is unavailable." : mViewportError.c_str());
        return;
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);

    if (ImGui::BeginCombo("##PreviewMesh", Engine::PrimitiveName(mPreviewPrimitive).c_str())) {
        for (const Engine::EPrimitiveType Primitive : {Engine::EPrimitiveType::Sphere, Engine::EPrimitiveType::Cube, Engine::EPrimitiveType::Plane}) {
            const bool Selected{mPreviewPrimitive == Primitive};

            if (ImGui::Selectable(Engine::PrimitiveName(Primitive).c_str(), Selected) && !Selected && Content->SetMaterialPreviewPrimitive(Primitive, mViewportError)) {
                mPreviewPrimitive = Primitive;
                mNeedsFrame = true;
            }

            if (Selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();

    if (EditorStyle::Button("Reset View")) {
        mNeedsFrame = true;
    }

    if (!mViewportError.empty()) {
        ImGui::TextWrapped("%s", mViewportError.c_str());
    }

    const ImVec2 Size{ImGui::GetContentRegionAvail()};

    if (Size.x < 1.0f || Size.y < 1.0f) {
        return;
    }

    if (!mViewer.Resize(static_cast<Uint32>(Size.x), static_cast<Uint32>(Size.y))) {
        ImGui::TextUnformatted("Cannot resize the material viewport.");
        return;
    }

    ImGui::InvisibleButton("##MaterialViewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const bool Hovered{ImGui::IsItemHovered()};
    const ImGuiIO& Input{ImGui::GetIO()};
    Viewer::FViewerCamera& Camera{mViewer.GetCamera()};

    if (mNeedsFrame || (Hovered && !Input.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F))) {
        Camera = Viewer::FViewerCamera{};
        mNeedsFrame = !mViewer.Fit(1.15f);
    }

    // 오른쪽 버튼을 누른 동안은 비행(제자리 시선 회전 + WASD/QE 이동, 휠은 속도 조절), 왼쪽은 궤도 회전, 가운데는 이동이다.
    const bool Flying{ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)};

    if (Flying) {
        Viewer::FFlyInput Fly{};

        Fly.mMouseDelta = {Input.MouseDelta.x, Input.MouseDelta.y};
        Fly.mWheel = Hovered ? Input.MouseWheel : 0.0f;
        Fly.mDeltaTime = Input.DeltaTime;
        Fly.mFast = Input.KeyShift;

        if (!Input.WantTextInput) {
            Fly.mForward = ImGui::IsKeyDown(ImGuiKey_W);
            Fly.mBackward = ImGui::IsKeyDown(ImGuiKey_S);
            Fly.mRight = ImGui::IsKeyDown(ImGuiKey_D);
            Fly.mLeft = ImGui::IsKeyDown(ImGuiKey_A);
            Fly.mUp = ImGui::IsKeyDown(ImGuiKey_E);
            Fly.mDown = ImGui::IsKeyDown(ImGuiKey_Q);
        }

        mFlyCamera.Update(Camera, Fly);
    } else if (ImGui::IsItemActive()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            Camera.Pan({Input.MouseDelta.x, Input.MouseDelta.y}, Size.y);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            Camera.Orbit({Input.MouseDelta.x, Input.MouseDelta.y});
        }
    }

    if (Hovered && !Flying && Input.MouseWheel != 0.0f) {
        Camera.Zoom(Input.MouseWheel);
    }

    const ImVec2 Origin{ImGui::GetItemRectMin()};
    const ImVec2 End{ImGui::GetItemRectMax()};
    ImDrawList* DrawList{ImGui::GetWindowDrawList()};

    if (!mViewer.Render()) {
        DrawList->AddText(Origin, ImGui::GetColorU32(ImGuiCol_Text), "Cannot render the material viewport.");
        return;
    }

    const ImTextureRef Texture{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mViewer.GetNativeTexture()))};
    DrawList->AddImage(Texture, Origin, End);
}

void FMaterialEditorToolkit::DrawDetails() {
    if (mEditingMaterial == nullptr) {
        return;
    }

    ImGui::TextUnformatted(Core::FilePath::ToGenericUtf8(GetAssetPath()).c_str());
    ImGui::BeginDisabled(!mContext->CanEditWorld());

    if (EditorStyle::Button("Reload")) {
        RequestReload();
    }

    ImGui::Separator();

    const RenderCore::UShader* Shader{mEditingMaterial->GetShader().Get()};

    if (ImGui::BeginCombo("Builtin Shader", Shader != nullptr ? Shader->GetName().c_str() : "Missing")) {
        for (RenderCore::UShader* Builtin : {RenderCore::UShader::GetDefault(), RenderCore::UShader::GetUnlit()}) {
            if (ImGui::Selectable(Builtin->GetName().c_str(), Shader == Builtin)) {
                SetMaterialShader(Builtin->GetSourcePath());
            }
        }

        ImGui::EndCombo();
    }

    Core::FString ShaderPath{mContext->GetProject()->GetAssets()->GetShaderPath(Shader)};

    if (mAssetPicker.Draw(*mAssetCatalog, *mThumbnails, "Shader Asset", ShaderPath, Asset::EAssetType::Shader)) {
        SetMaterialShader(ShaderPath);
    }

    auto Color{mEditingMaterial->GetBaseColor().ToArray()};

    if (ImGui::ColorEdit4("Base Color", Color.data()) && mEditingMaterial->SetBaseColor({Color[0], Color[1], Color[2], Color[3]})) {
        mMaterialDirty = true;
    }

    int BlendMode{static_cast<int>(mEditingMaterial->GetBlendMode())};

    if (ImGui::Combo("Blend Mode", &BlendMode, "Opaque\0Translucent\0Additive\0") && mEditingMaterial->SetBlendMode(static_cast<Asset::EBlendMode>(BlendMode))) {
        mMaterialDirty = true;
    }

    bool TwoSided{mEditingMaterial->IsTwoSided()};

    if (ImGui::Checkbox("Two Sided", &TwoSided)) {
        mEditingMaterial->SetTwoSided(TwoSided);
        mMaterialDirty = true;
    }

    Core::FString TexturePath{mEditingMaterial->GetBaseColorTexturePath()};

    if (mAssetPicker.Draw(*mAssetCatalog, *mThumbnails, "Base Color Texture", TexturePath, Asset::EAssetType::Texture)) {
        SetMaterialTexture(TexturePath);
    }

    Core::FString NormalPath{mEditingMaterial->GetNormalMapTexturePath()};

    if (mAssetPicker.Draw(*mAssetCatalog, *mThumbnails, "Normal Map", NormalPath, Asset::EAssetType::Texture)) {
        const Core::FString Previous{mEditingMaterial->GetNormalMapTexturePath()};

        if (mEditingMaterial->SetNormalMapTexturePath(NormalPath) && mEditingMaterial->ResolveTexture(*mContext->GetProject()->GetAssets(), mMaterialError)) {
            mMaterialDirty = true;
        } else {
            mEditingMaterial->SetNormalMapTexturePath(Previous);

            Core::FString RestoreError{};

            mEditingMaterial->ResolveTexture(*mContext->GetProject()->GetAssets(), RestoreError);
        }
    }

    float Specular{mEditingMaterial->GetSpecularStrength()};
    float Shininess{mEditingMaterial->GetShininess()};
    float NormalStrength{mEditingMaterial->GetNormalStrength()};

    if (ImGui::SliderFloat("Specular Strength", &Specular, 0.0f, 1.0f) && mEditingMaterial->SetSpecularStrength(Specular)) {
        mMaterialDirty = true;
    }

    if (ImGui::SliderFloat("Shininess", &Shininess, 1.0f, 1024.0f, "%.1f", ImGuiSliderFlags_Logarithmic) && mEditingMaterial->SetShininess(Shininess)) {
        mMaterialDirty = true;
    }

    if (ImGui::SliderFloat("Normal Strength", &NormalStrength, 0.0f, 8.0f) && mEditingMaterial->SetNormalStrength(NormalStrength)) {
        mMaterialDirty = true;
    }

    ImGui::EndDisabled();

    if (!mMaterialError.empty()) {
        ImGui::TextWrapped("%s", mMaterialError.c_str());
    }
}
