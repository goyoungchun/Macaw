/**
 * @file FAnimSequenceViewportClient.cpp
 * @brief 애니메이션 뷰어의 이미지 표시, 카메라 입력과 오버레이를 연결합니다.
 * @ingroup Editor
 */

#include "FAnimSequenceViewportClient.h"
#include "Core/Literals/UnitLiterals.h"

#include "Viewer/FAnimationViewerContent.h"
#include "Viewer/FBonePreview.h"
#include "Asset/USkeleton.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Engine/FPrimitiveDrawInterface.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

using namespace Core::Literals;

using namespace Editor;

FAnimSequenceViewportClient::FAnimSequenceViewportClient(RenderCore::IRendererModule* Renderer, std::unique_ptr<Viewer::FAnimationViewerContent> Scene)
	: mViewer{Renderer, nullptr, true},
	  mScene{Scene.get()} {
    mViewer.SetContent(std::move(Scene));
}

FAnimSequenceViewportClient::~FAnimSequenceViewportClient() = default;

void FAnimSequenceViewportClient::FrameMesh() {
    mViewer.GetCamera().SetRotation({-20_deg, 45_deg, 0_deg});
    mViewer.Fit();
    mNeedsFit = true;
}

void FAnimSequenceViewportClient::SetDisplay(bool ShowBones, bool ShowGrid, bool Wireframe) {
    mShowBones = ShowBones;
    mShowGrid = ShowGrid;
    mWireframe = Wireframe;
}

void FAnimSequenceViewportClient::DrawUI(const FVector2& Size) {
    if (!mScene->IsReady()) {
        ImGui::TextWrapped("Select a skeletal mesh and its skeleton in Preview Settings to display this animation.");

        return;
    }

    if (Size.x < 1.0f || Size.y < 1.0f || !mViewer.Resize(static_cast<Uint32>(Size.x), static_cast<Uint32>(Size.y))) {
        return;
    }

    if (mNeedsFit) {
        mNeedsFit = !mViewer.Fit();
    }

    ImGui::InvisibleButton("##AnimationPreview", {Size.x, Size.y}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const bool Hovered{ImGui::IsItemHovered()};
    const bool Active{ImGui::IsItemActive()};
    const ImGuiIO& Input{ImGui::GetIO()};

    // 오른쪽 버튼을 누른 동안은 비행(제자리 시선 회전 + WASD/QE 이동, 휠은 속도 조절), 왼쪽은 궤도 회전, 가운데는 이동이다.
    const bool Flying{Active && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)};

    if (Flying) {
        Viewer::FFlyInput Fly{};

        Fly.mMouseDelta = {Input.MouseDelta.x, Input.MouseDelta.y};
        Fly.mWheel = Input.MouseWheel;
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

        mFlyCamera.Update(mViewer.GetCamera(), Fly);
    } else if (Active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        mViewer.GetCamera().Pan({Input.MouseDelta.x, Input.MouseDelta.y}, Size.y);
    } else if (Active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        mViewer.GetCamera().Orbit({Input.MouseDelta.x, Input.MouseDelta.y});
    }

    if (Hovered) {
        if (!Flying) {
            mViewer.GetCamera().Zoom(Input.MouseWheel);
        }

        if (!Input.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) {
            FrameMesh();
        }
    }

    const ImVec2 Origin{ImGui::GetItemRectMin()};
    const ImVec2 End{ImGui::GetItemRectMax()};

    ImGui::GetWindowDrawList()->AddImage(ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mViewer.GetNativeTexture()))}, Origin, End);
    ImGui::GetWindowDrawList()->AddText({Origin.x + 12.0f, End.y - ImGui::GetTextLineHeight() - 10.0f}, IM_COL32(220, 225, 235, 255), "LMB: orbit | MMB: pan | Wheel: zoom | RMB + WASD/QE: fly | RMB + Wheel: speed | Shift: fast | F: frame");
}

void FAnimSequenceViewportClient::Render() {
    Engine::USkeletalMeshComponent* Component{mScene->GetComponent()};

    if (Component == nullptr) {
        return;
    }

    const float Radius{mViewer.GetCamera().GetRadius()};
    Engine::FPrimitiveDrawInterface Bones{};

    if (mShowBones && Component->GetSkeleton() != nullptr) {
        for (const Viewer::FBoneShape& Shape : Viewer::BuildBoneShapes(Component->GetSkeleton()->GetBones(), Component->GetPose().GetGlobalTransforms(), Radius)) {
            Bones.DrawMesh({Shape.GetMesh(), Shape.GetWorld() * Component->GetWorldMatrix(), {0.45f, 0.8f, 1.0f, 1.0f}});
        }
    }

    mViewer.SetViewMode(mWireframe ? Engine::EViewMode::Wireframe : Engine::EViewMode::Lit);
    mViewer.SetGridSpacing(mShowGrid ? std::max(std::pow(10.0f, std::floor(std::log10(Radius)) - 1.0f), (0.001_m).GetBase()) : 0.0f);
    mViewer.Render(nullptr, mShowBones ? &Bones : nullptr);
}
