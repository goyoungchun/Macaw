#include "FAssetTileWidget.h"

#include "EditorStyle.h"
#include "Asset/UTexture2D.h"
#include "Platform/WindowResources.h"
#include "RenderCore/IRendererModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <d3d11.h>
#include <filesystem>
#include <imgui_internal.h>
#include <span>

using namespace Editor;

namespace {
    struct FAssetStyle {
        Asset::EAssetType mType{};
        FAssetTileWidget::FStyle mStyle{};
    };

    constexpr FAssetTileWidget::FStyle ContentStyles[]{
        {"File", "파일", MacawContentFileIconResource, EEditorIcon::Component, IM_COL32(143, 151, 164, 255)},
        {"Folder", "폴더", MacawContentFolderIconResource, EEditorIcon::Folder, IM_COL32(221, 176, 94, 255), IM_COL32(255, 208, 111, 255)},
        {"Lua Script", "Lua 스크립트", MacawContentLuaIconResource, EEditorIcon::Component, IM_COL32(141, 173, 231, 255), IM_COL32(164, 197, 255, 255)},
        {"Shader Source", "셰이더 소스", MacawContentLuaIconResource, EEditorIcon::Component, IM_COL32(120, 214, 236, 255), IM_COL32(120, 214, 236, 255)}
    };

    constexpr FAssetStyle AssetStyles[]{
        {Asset::EAssetType::Mesh, {"Mesh", "메시", MacawContentMeshIconResource, EEditorIcon::Local, IM_COL32(100, 194, 232, 255)}},
        {Asset::EAssetType::SkeletalMesh, {"Skeletal Mesh", "스켈레탈 메시", MacawContentSkeletalMeshIconResource, EEditorIcon::Skeleton, IM_COL32(137, 164, 251, 255)}},
        {Asset::EAssetType::Skeleton, {"Skeleton", "스켈레톤", MacawContentSkeletonIconResource, EEditorIcon::Skeleton, IM_COL32(231, 190, 105, 255)}},
        {Asset::EAssetType::PhysicsAsset, {"Physics Asset", "물리 에셋", 0, EEditorIcon::Collision, IM_COL32(112, 180, 245, 255)}},
        {Asset::EAssetType::Material, {"Material", "머티리얼", MacawContentMaterialIconResource, EEditorIcon::Material, IM_COL32(117, 207, 158, 255)}},
        {Asset::EAssetType::Texture, {"Texture", "텍스처", MacawContentTextureIconResource, EEditorIcon::Sprite, IM_COL32(213, 150, 236, 255)}},
        {Asset::EAssetType::Scene, {"Scene", "씬", MacawContentSceneIconResource, EEditorIcon::World, IM_COL32(123, 178, 156, 255)}},
        {Asset::EAssetType::Sequence, {"Sequence", "시퀀스", MacawContentSequenceIconResource, EEditorIcon::Sequence, IM_COL32(177, 154, 226, 255)}},
        {Asset::EAssetType::AnimSequence, {"Animation Sequence", "애니메이션 시퀀스", MacawContentAnimationSequenceIconResource, EEditorIcon::Sequence, IM_COL32(239, 158, 113, 255)}},
        {Asset::EAssetType::BlendSpace, {"Blend Space", "블렌드 스페이스", MacawContentAnimationSequenceIconResource, EEditorIcon::Sequence, IM_COL32(106, 218, 189, 255), IM_COL32(120, 214, 236, 255)}},
        {Asset::EAssetType::AnimGraph, {"Anim Graph", "애니메이션 그래프", MacawContentAnimationSequenceIconResource, EEditorIcon::Sequence, IM_COL32(150, 165, 255, 255), IM_COL32(150, 165, 255, 255)}},
        {Asset::EAssetType::ParticleSystem, {"Particle System", "파티클 시스템", MacawContentParticleSystemIconResource, EEditorIcon::Sprite, IM_COL32(238, 187, 106, 255)}},
        {Asset::EAssetType::Shader, {"Shader", "셰이더", MacawContentLuaIconResource, EEditorIcon::Component, IM_COL32(120, 214, 236, 255)}}
    };

    constexpr float ImageInset{4.0f};
    constexpr float NameSpacing{8.0f};
    constexpr float TypeSpacing{5.0f};
    constexpr float BottomPadding{8.0f};
    constexpr float TypeFontScale{0.78f};

    void DrawName(ImDrawList& DrawList, const Core::FString& Name, const ImVec2& Position, float Width, float FontSize) {
        ImFont* Font{ImGui::GetFont()};
        const char* NameEnd{Name.data() + Name.size()};
        const char* FirstLineEnd{Font->CalcWordWrapPosition(FontSize, Name.data(), NameEnd, Width)};
        const float Right{Position.x + Width};

        if (FirstLineEnd != NameEnd && *FirstLineEnd != ' ' && *FirstLineEnd != '\t') {
            const char* Middle{Name.data() + (FirstLineEnd - Name.data()) / 2};

            for (const char* Boundary{FirstLineEnd}; Boundary > Middle; --Boundary) {
                const auto Previous{static_cast<unsigned char>(Boundary[-1])};
                const auto Next{static_cast<unsigned char>(*Boundary)};

                if (Previous == '_' || Previous == '-' || (std::islower(Previous) && std::isupper(Next))) {
                    FirstLineEnd = Boundary;
                    break;
                }
            }
        }

        const ImVec4 Clip{Position.x, Position.y, Right, Position.y + FontSize};
        DrawList.AddText(Font, FontSize, Position, ImGui::GetColorU32(ImGuiCol_Text), Name.data(), FirstLineEnd, 0.0f, &Clip);
        const char* SecondLine{FirstLineEnd};

        while (SecondLine != NameEnd && (*SecondLine == ' ' || *SecondLine == '\t')) {
            ++SecondLine;
        }

        if (SecondLine != NameEnd) {
            ImGui::RenderTextEllipsis(&DrawList, {Position.x, Position.y + FontSize}, {Right, Position.y + FontSize * 2.0f}, Right, SecondLine, NameEnd, nullptr);
        }
    }
}

FAssetTileWidget::FAssetTileWidget() = default;

FAssetTileWidget::~FAssetTileWidget() = default;

void FAssetTileWidget::LoadIcons(RenderCore::IRendererModule* Renderer) {
    auto* Device{Renderer != nullptr ? static_cast<ID3D11Device*>(Renderer->GetNativeDevice()) : nullptr};

    if (Device == nullptr) {
        return;
    }

    const HMODULE Module{GetModuleHandleW(nullptr)};
    const std::filesystem::path ImageName{L"ContentIcon.png"};
    const auto LoadIcon{[this, Module, Device, &ImageName](const FStyle& Style) {
        if (Style.mResourceId == 0 || mIcons.contains(Style.mResourceId)) {
            return;
        }

        const HRSRC Resource{FindResourceW(Module, MAKEINTRESOURCEW(Style.mResourceId), RT_RCDATA)};
        const HGLOBAL Data{Resource != nullptr ? LoadResource(Module, Resource) : nullptr};
        const auto* Bytes{Data != nullptr ? static_cast<const Uint8*>(LockResource(Data)) : nullptr};

        if (Bytes == nullptr) {
            return;
        }

        const std::span<const Uint8> Image{Bytes, SizeofResource(Module, Resource)};
        Asset::UTexture2D Texture{};

        if (Texture.LoadFromMemory(Image, ImageName, false) && Texture.CreateResources(Device)) {
            mIcons.try_emplace(Style.mResourceId, Texture.GetShaderResourceView());
        }
    }};

    for (const FStyle& Style : ContentStyles) {
        LoadIcon(Style);
    }

    for (const FAssetStyle& Entry : AssetStyles) {
        LoadIcon(Entry.mStyle);
    }
}

const FAssetTileWidget::FStyle& FAssetTileWidget::GetStyle(Asset::EAssetType Type) {
    for (const FAssetStyle& Entry : AssetStyles) {
        if (Entry.mType == Type) {
            return Entry.mStyle;
        }
    }

    return GetStyle(EContentKind::File);
}

const FAssetTileWidget::FStyle& FAssetTileWidget::GetStyle(EContentKind Kind) {
    return ContentStyles[static_cast<Index_t>(Kind)];
}

ImVec2 FAssetTileWidget::Measure(float Width, float Scale) {
    const float FontSize{ImGui::GetFontSize()};
    const float LabelsHeight{FontSize * (2.0f + TypeFontScale) + (NameSpacing + TypeSpacing + BottomPadding) * Scale};

    return {Width, std::ceil(Width - ImageInset * Scale + LabelsHeight)};
}

bool FAssetTileWidget::Draw(const char* Id, const Core::FString& Name, const FStyle& Style, void* Thumbnail, const ImVec2& Size, float Scale, bool Selected) const {
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{});
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{});
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{});
    const ImGuiSelectableFlags Flags{ImGuiSelectableFlags_AllowDoubleClick | static_cast<ImGuiSelectableFlags>(ImGuiSelectableFlags_NoPadWithHalfSpacing)};
    const bool Pressed{ImGui::Selectable(Id, Selected, Flags, Size)};
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemVisible()) {
        const ImRect Bounds{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        DrawCard(Bounds, Name, Style, Thumbnail, Scale, Selected || Pressed || ImGui::IsItemClicked(ImGuiMouseButton_Right), ImGui::IsItemHovered());
        ImGui::RenderNavCursor(Bounds, ImGui::GetItemID(), ImGuiNavRenderCursorFlags_Compact);
    }

    return Pressed;
}

void FAssetTileWidget::DrawPreview(const Core::FString& Name, const FStyle& Style, void* Thumbnail, const ImVec2& Size, float Scale) const {
    ImGui::Dummy(Size);
    const ImRect Bounds{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    DrawCard(Bounds, Name, Style, Thumbnail, Scale, false, false);
}

void FAssetTileWidget::DrawIcon(ImDrawList& DrawList, const FStyle& Style, const ImVec2& Position, float Size) const {
    const auto Found{mIcons.find(Style.mResourceId)};
    const ImU32 Tint{ImGui::GetColorU32(Style.mIconTint)};

    if (Found != mIcons.end()) {
        const ImTextureRef Texture{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(Found->second.Get()))};
        DrawList.AddImage(Texture, Position, {Position.x + Size, Position.y + Size}, {}, {1.0f, 1.0f}, Tint);
    } else {
        EditorStyle::DrawIcon(&DrawList, Style.mFallback, Position, Size, Tint);
    }
}

void FAssetTileWidget::DrawCard(const ImRect& Bounds, const Core::FString& Name, const FStyle& Style, void* Thumbnail, float Scale, bool Selected, bool Hovered) const {
    const ImGuiStyle& Theme{ImGui::GetStyle()};
    const ImVec4 Surface{ImLerp(Theme.Colors[ImGuiCol_WindowBg], Theme.Colors[ImGuiCol_Button], 0.8f)};
    const ImVec4 Fill{Selected ? ImLerp(Surface, Theme.Colors[ImGuiCol_HeaderActive], 0.26f) : ImLerp(Surface, Theme.Colors[ImGuiCol_ButtonHovered], Hovered ? 0.28f : 0.0f)};
    const ImGuiCol Border{Selected ? ImGuiCol_HeaderActive : Hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border};
    const float Rounding{std::max(3.0f * Scale, Theme.FrameRounding)};
    const float Inset{ImageInset * Scale};
    const float FontSize{ImGui::GetFontSize()};
    const float TextInset{8.0f * Scale};
    const ImRect Image{ImVec2{Bounds.Min.x + Inset, Bounds.Min.y + Inset}, ImVec2{Bounds.Max.x - Inset, Bounds.Min.y + Bounds.GetWidth() - Inset}};
    const ImVec2 NamePosition{Bounds.Min.x + TextInset, Image.Max.y + NameSpacing * Scale};
    const ImVec2 TypePosition{NamePosition.x, NamePosition.y + 2.0f * FontSize + TypeSpacing * Scale};
    const float TextRight{Bounds.Max.x - TextInset};
    ImDrawList& DrawList{*ImGui::GetWindowDrawList()};

    DrawList.AddRectFilled(Bounds.Min, Bounds.Max, ImGui::GetColorU32(Fill), Rounding);
    DrawImage(DrawList, Image, Style, Thumbnail, std::max(2.0f * Scale, Rounding - Inset));
    DrawList.AddRectFilled({Image.Min.x, Image.Max.y - std::max(1.0f, 2.0f * Scale)}, Image.Max, ImGui::GetColorU32(Style.mAccent));
    DrawList.AddRect(Bounds.Min, Bounds.Max, ImGui::GetColorU32(Border), Rounding);

    DrawName(DrawList, Name, NamePosition, TextRight - NamePosition.x, FontSize);

    ImGui::PushFont(nullptr, Theme.FontSizeBase * TypeFontScale);
    ImGui::PushStyleColor(ImGuiCol_Text, ImLerp(Theme.Colors[ImGuiCol_TextDisabled], Theme.Colors[ImGuiCol_Text], 0.45f));
    ImGui::RenderTextEllipsis(&DrawList, TypePosition, {TextRight, Bounds.Max.y - BottomPadding * Scale}, TextRight, Style.mLabel, nullptr, nullptr);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void FAssetTileWidget::DrawImage(ImDrawList& DrawList, const ImRect& Bounds, const FStyle& Style, void* Thumbnail, float Rounding) const {
    constexpr ImDrawFlags Corners{ImDrawFlags_RoundCornersTop};

    if (Thumbnail != nullptr) {
        const ImTextureRef Texture{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(Thumbnail))};
        DrawList.AddImageRounded(Texture, Bounds.Min, Bounds.Max, {}, {1.0f, 1.0f}, ImGui::GetColorU32(IM_COL32_WHITE), Rounding, Corners);
        return;
    }

    const ImGuiStyle& Theme{ImGui::GetStyle()};
    const ImVec4 PreviewFill{ImLerp(Theme.Colors[ImGuiCol_FrameBg], Theme.Colors[ImGuiCol_WindowBg], 0.28f)};
    DrawList.AddRectFilled(Bounds.Min, Bounds.Max, ImGui::GetColorU32(PreviewFill), Rounding, Corners);

    const float IconSize{Bounds.GetWidth() * 0.82f};
    const float Padding{(Bounds.GetWidth() - IconSize) * 0.5f};
    DrawIcon(DrawList, Style, {Bounds.Min.x + Padding, Bounds.Min.y + Padding}, IconSize);
}
