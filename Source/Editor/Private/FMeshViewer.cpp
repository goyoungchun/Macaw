/**
 * @file FMeshViewer.cpp
 * @brief 독립 메시 미리보기와 궤도 카메라의 구현입니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Editor/FMeshViewer.h"
#include "Editor/FEditorToolkitHost.h"
#include "Viewer/FMeshViewerContent.h"
#include "Viewer/FBonePreview.h"
#include "Core/FilePath.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FQuat.h"
#include "Core/Math/FRay.h"
#include "Core/Math/FVector4.h"
#include "Asset/FAssetManager.h"
#include "Asset/FAssetFile.h"
#include "Engine/FSceneInterface.h"
#include "Engine/FRenderTarget.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Engine/FLightSceneProxy.h"
#include "Engine/FSceneViewFamily.h"
#include "Asset/UStaticMesh.h"
#include "Asset/USkeletalMesh.h"
#include "PhysicsRuntime/UClothConfigNv.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"
#include "Asset/USkeleton.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/EditorMesh.h"
#include "Engine/MeshPicking.h"
#include "Asset/UMaterial.h"
#include "RenderCore/IRendererModule.h"

#include "Core/Base/ErrorHandler.h"
#include "EditorStyle.h"
#include "PhysicsRuntime/IClothingSimulation.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <d3d11.h>
#include <limits>

using namespace Core::Literals;

namespace Editor {

    /**
     * @brief 렌더러와 프로젝트 에셋 관리자를 연결합니다.
     */
    Editor::FMeshViewer::FMeshViewer(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets)
		: mAssets{Assets},
		  mViewer{Renderer, Assets, true} {
    }

    /**
     * @brief 미리보기 장면과 렌더 타깃을 해제합니다.
     */
    Editor::FMeshViewer::~FMeshViewer() {
        // 천 시뮬레이션은 미리 보기 장면보다 먼저 해제합니다.
        ReleaseClothPreview();
    }

    /**
     * @brief 에셋을 독립 미리보기 장면에 엽니다.
     */
    bool Editor::FMeshViewer::Open(const std::filesystem::path& Path, Core::FString& Error) {
        Error.clear();

        // 이전 메시의 천 에셋을 가리키는 미리 보기 천을 먼저 해제합니다.
        ReleaseClothPreview();
        Asset::EAssetType Type{};

        if (mAssets == nullptr || !Asset::FAssetFile::GetType(mAssets->ResolvePath(Path, Error), Type, Error) || (Type != Asset::EAssetType::Mesh && Type != Asset::EAssetType::SkeletalMesh)) {
            return false;
        }

        const Core::FString MeshPath{Core::FilePath::ToGenericUtf8(Path)};
        const Core::FString SkeletonPath{Type == Asset::EAssetType::SkeletalMesh && MeshPath == mPath ? mSkeletonPath : Core::FString{}};
        CoreUObject::UObject* Object{Type == Asset::EAssetType::SkeletalMesh ? static_cast<CoreUObject::UObject*>(mAssets->LoadSkeletalMesh(Path, Error)) : mAssets->LoadMesh(Path, Error)};

        if (Object == nullptr) {
            return false;
        }

        if (!mViewer.SetAsset(Object, Type)) {
            Error = mViewer.GetError();

            return false;
        }

        mContent = static_cast<Viewer::FMeshViewerContent*>(mViewer.GetContent());

        if (Type == Asset::EAssetType::SkeletalMesh) {
            Asset::USkeleton* Skeleton{SkeletonPath.empty() ? mAssets->FindLegacySkeleton(Path) : mAssets->LoadSkeleton(Core::FilePath::FromUtf8(SkeletonPath), Error)};

            if ((!SkeletonPath.empty() && Skeleton == nullptr) || !mContent->SetSkeleton(Skeleton)) {
                Error = Error.empty() ? "Cannot prepare the skeleton preview" : Error;
                mViewer.Close();
                mContent = nullptr;
                mOpen = false;

                return false;
            }
        }

        mMesh = mContent->GetMesh();
        mSkeletalMesh = mContent->GetSkeletalMesh();
        mSkeleton = mContent->GetSkeleton();
        mSkeletonPath = SkeletonPath;
        mPoseBone.reset();
        mGizmo.EndDrag();
        mMaterialPaths.assign(mContent->GetMaterials().size(), Core::FString{});
        mPath = MeshPath;
        mError.clear();
        mClothSelectedAsset = 0;
        mCollisionShapeType = 0;
        mCollisionShapeIndex = 0;
        mCollisionUnsaved = false;
        mCollisionMessage.clear();
        mClothUnsaved = false;
        mClothPaintEnabled = false;
        mClothPreviewStale = true;
        mOpen = true;
        mMouseCaptured = false;
        FrameMesh();

        return true;
    }

    /**
     * @brief 창의 열림 상태를 검사합니다.
     */
    void Editor::FMeshViewer::SetWindowId(Core::FString Id) {
        mWindowId = std::move(Id);
    }

    const Core::FString& Editor::FMeshViewer::GetWindowId() const {
        return mWindowId;
    }

    bool Editor::FMeshViewer::IsOpen() const {
        return mOpen;
    }

    void Editor::FMeshViewer::CloseRetired(const std::filesystem::path& Path) {
        if (!mOpen || mAssets == nullptr) {
            return;
        }

        Core::FString Error{};
        const auto Normalize{[this, &Error](const std::filesystem::path& Target) {
            return Core::FilePath::ToGenericUtf8(Core::FilePath::ToLower(Core::FilePath::FromUtf8(mAssets->GetRelativePath(Target, Error))));
        }};
        const Core::FString Root{Normalize(Path)};
        const auto Contains{[&](const Core::FString& Open) {
            if (Open.empty() || Root.empty()) {
                return false;
            }

            const Core::FString Relative{Normalize(Core::FilePath::FromUtf8(Open))};

            return Root == "." || Relative == Root || Relative.starts_with(Root + "/");
        }};

        if (Contains(mPath) || Contains(mSkeletonPath)) {
            mOpen = false;
        }
    }

    /**
     * @brief 전체 메시가 보이도록 카메라를 배치합니다.
     */
    void Editor::FMeshViewer::FrameMesh() {
        if (mContent != nullptr) {
            mViewer.GetCamera().SetRotation({-20_deg, 45_deg, 0_deg});
            mViewer.Fit();
            mNeedsFit = true;
        }
    }

    /**
     * @brief 현재 메시와 슬롯별 재질을 미리보기 장면에 반영합니다.
     */
    void Editor::FMeshViewer::UpdateScene() {
        mContent->Refresh();
    }

    /**
     * @brief 슬롯별 임시 재질 선택 UI를 표시합니다.
     */
    void Editor::FMeshViewer::DrawMaterials() {
        if (!ImGui::CollapsingHeader("Material Slots")) {
            return;
        }

        Core::FString Error{};
        const auto Paths{mAssets->GetAssetPaths(Error)};
        const auto& Slots{mMesh->GetMeshData().GetMaterialSlots()};

        for (Index_t Slot{}; Slot < Slots.size(); ++Slot) {
            ImGui::PushID(static_cast<int>(Slot));

            Core::FString Path{mMaterialPaths[Slot]};
            bool Changed{};

            if (ImGui::BeginCombo(Slots[Slot].GetName().c_str(), Path.empty() ? "Mesh Default" : Path.c_str())) {
                if (ImGui::Selectable("Mesh Default", Path.empty())) {
                    Path.clear();

                    Changed = true;
                }

                for (const auto& Candidate : Paths) {
                    Asset::EAssetType Type{};

                    if (Asset::FAssetFile::GetType(mAssets->ResolvePath(Candidate, Error), Type, Error) && Type == Asset::EAssetType::Material) {
                        const Core::FString Relative{Core::FilePath::ToGenericUtf8(Candidate)};

                        if (ImGui::Selectable(Relative.c_str(), Path == Relative)) {
                            Path = Relative;
                            Changed = true;
                        }
                    }
                }

                ImGui::EndCombo();
            }

            if (Changed) {
                const Core::FString Resolved{Path.empty() ? Slots[Slot].GetMaterialPath() : Path};
                Asset::UMaterial* Material{Resolved.empty() ? nullptr : mAssets->LoadMaterial(Core::FilePath::FromUtf8(Resolved), mError)};

                if (Resolved.empty() || Material != nullptr) {
                    mContent->GetMaterials()[Slot] = Material;
                    mMaterialPaths[Slot] = Path;

                    UpdateScene();
                }
            }

            ImGui::PopID();
        }
    }

    namespace {
        /**
         * @brief 메시가 기본으로 쓸 물리 에셋을 콤보 상자로 고르고 메시 파일에 저장합니다.
         * @param[in,out] Assets 에셋 관리자입니다.
         * @param[in,out] Mesh 물리 에셋 경로를 바꿀 스켈레탈 메시입니다.
         * @param[in] MeshPath 스켈레탈 메시의 콘텐츠 경로입니다.
         * @param[out] Error 실패 원인입니다.
         */
        void DrawPhysicsAssetPicker(Asset::FAssetManager& Assets, Asset::USkeletalMesh& Mesh, const Core::FString& MeshPath, Core::FString& Error) {
            const Core::FString Current{Mesh.GetPhysicsAssetPath()};

            if (!ImGui::BeginCombo("Physics Asset", Current.empty() ? "None (generated)" : Current.c_str())) {
                return;
            }

            Core::FString Selected{Current};
            bool Picked{};
            Core::FString ListError{};

            if (ImGui::Selectable("None (generated)", Current.empty())) {
                Selected.clear();
                Picked = true;
            }

            for (const auto& Candidate : Assets.GetAssetPaths(ListError)) {
                Asset::EAssetType Type{};

                if (Asset::FAssetFile::GetType(Assets.ResolvePath(Candidate, ListError), Type, ListError) && Type == Asset::EAssetType::PhysicsAsset) {
                    const Core::FString Relative{Core::FilePath::ToGenericUtf8(Candidate)};

                    if (ImGui::Selectable(Relative.c_str(), Relative == Current)) {
                        Selected = Relative;
                        Picked = true;
                    }
                }
            }

            ImGui::EndCombo();

            if (!Picked || Selected == Current) {
                return;
            }

            // 버전 1 메시는 본을 파일 안에 내장하는데 저장하면 새 형식으로 쓰여 본이 사라질 수 있으므로 막는다.
            if (Assets.FindLegacySkeleton(Core::FilePath::FromUtf8(MeshPath)) != nullptr) {
                Error = "This mesh uses an embedded skeleton, so its file cannot be saved safely. Set the physics asset on the component instead.";
                Core::ErrorHandler::Report("MeshViewer", "Cannot assign the physics asset '" + Selected + "' to '" + MeshPath + "': " + Error, Core::ErrorHandler::EErrorLevel::Warning);

                return;
            }

            Mesh.SetPhysicsAssetPath(Selected);
            Error.clear();

            if (!Assets.SaveSkeletalMesh(Core::FilePath::FromUtf8(MeshPath), Mesh, Error)) {
                // 저장하지 못하면 메시 파일과 같은 값으로 되돌린다. 원인은 로그에도 남겨 어떤 조건에서 되돌아가는지 알 수 있게 한다.
                Mesh.SetPhysicsAssetPath(Current);

                if (Error.empty()) {
                    Error = "Cannot save the skeletal mesh.";
                }

                Error = "Could not assign '" + Selected + "': " + Error;
                Core::ErrorHandler::Report("MeshViewer", Error + " (mesh '" + MeshPath + "')", Core::ErrorHandler::EErrorLevel::Warning);
            }
        }
    }

    void Editor::FMeshViewer::DrawSkeletonPicker() {
        const char* Label{mSkeletonPath.empty() ? mSkeleton == nullptr ? "None" : "Embedded Skeleton" : mSkeletonPath.c_str()};

        if (!ImGui::BeginCombo("Skeleton", Label)) {
            return;
        }

        Core::FString SelectedPath{mSkeletonPath};
        Core::FString Error{};

        if (ImGui::Selectable("Default", SelectedPath.empty())) {
            SelectedPath.clear();
        }

        for (const auto& Candidate : mAssets->GetAssetPaths(Error)) {
            Asset::EAssetType Type{};

            if (Asset::FAssetFile::GetType(mAssets->ResolvePath(Candidate, Error), Type, Error) && Type == Asset::EAssetType::Skeleton) {
                const Core::FString Relative{Core::FilePath::ToGenericUtf8(Candidate)};

                if (ImGui::Selectable(Relative.c_str(), Relative == SelectedPath)) {
                    SelectedPath = Relative;
                }
            }
        }

        ImGui::EndCombo();

        if (SelectedPath == mSkeletonPath) {
            return;
        }

        Error.clear();

        Asset::USkeleton* Skeleton{SelectedPath.empty() ? mAssets->FindLegacySkeleton(Core::FilePath::FromUtf8(mPath)) : mAssets->LoadSkeleton(Core::FilePath::FromUtf8(SelectedPath), Error)};
        if ((!SelectedPath.empty() && Skeleton == nullptr) || !mContent->SetSkeleton(Skeleton)) {
            mError = Error.empty() ? "Cannot prepare the skeleton preview pose" : std::move(Error);

            return;
        }

        mSkeleton = mContent->GetSkeleton();
        mSkeletonPath = std::move(SelectedPath);
        mPoseBone.reset();
        mGizmo.EndDrag();
        mError.clear();

        // 스켈레톤이 바뀌면 천이 따라갈 뼈도 바뀌므로 미리 보기 천을 다시 만듭니다.
        ReleaseClothPreview();
        mClothPreviewStale = true;

        UpdateScene();
    }

    /**
     * @brief 카메라·재질 조작 UI와 미리보기를 표시합니다.
     */
    void Editor::FMeshViewer::DrawUI(const Core::FInput& Input, bool Standalone) {
        if (!mOpen || mMesh == nullptr) {
            return;
        }

        ImGuiWindowFlags Flags{ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};

        if (Standalone) {
            const ImGuiViewport* Viewport{ImGui::GetMainViewport()};

            ImGui::SetNextWindowPos(Viewport->WorkPos);
            ImGui::SetNextWindowSize(Viewport->WorkSize);

            Flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        } else {
            ImGui::SetNextWindowSize({860.0f, 700.0f}, ImGuiCond_FirstUseEver);
        }

        // 탭에는 에셋 이름을 보여 주고 ### 뒤의 식별자는 유지합니다.
        const Core::FString Title{(Standalone ? "" : Editor::EditorStyle::TabIconPadding) + Core::FilePath::ToUtf8(Core::FilePath::FromUtf8(mPath).stem()) + mWindowId};

        const bool Expanded{Editor::EditorStyle::BeginEditorWindow(Title.c_str(), Standalone ? nullptr : &mOpen, Flags)};

        if (!Standalone) {
            Editor::EditorStyle::TabIcon(Editor::EditorStyle::EIcon::Local, IM_COL32(0, 220, 220, 255));
        }

        if (Expanded) {
            DrawProperties();

            if (mSkeleton != nullptr) {
                DrawPose();
                ImGui::SameLine();
            }

            DrawPreview(Input);
        } else {
            mMouseCaptured = false;
        }

        ImGui::End();
    }

    void Editor::FMeshViewer::InitializeToolkit(FEditorTab& Tab, const Core::FInput& Input) {
        mHostTab = &Tab;
        Tab.SetIcon(EEditorIcon::Local, IM_COL32(0, 220, 220, 255));
        FEditorToolkitHost& Host{Tab.GetToolkitHost()};
        Host.AddWidget("Preview", "Preview", [this, &Input](float) {
            if (mOpen && mMesh != nullptr) {
                DrawPreview(Input);
            }
        });
        Host.AddWidget("Details", "Details", [this](float) {
            DrawProperties();
        }, EEditorDockRegion::Details);

        if (mSkeletalMesh != nullptr) {
            Host.AddWidget("Skeleton", "Skeleton", [this](float) {
                if (mSkeleton != nullptr) {
                    DrawPose(true);
                }
            }, EEditorDockRegion::Left);
        }
    }

    void Editor::FMeshViewer::UpdateToolkit(float, bool) {
        mHostTab->SetName(EditorStyle::TabIconPadding + Core::FilePath::ToUtf8(Core::FilePath::FromUtf8(mPath).stem()) + mWindowId);

        if (!mHostTab->IsFocused()) {
            mMouseCaptured = false;
        }
    }

    void Editor::FMeshViewer::DrawProperties() {
        if (!mOpen || mMesh == nullptr) {
            return;
        }

        ImGui::TextUnformatted(mPath.c_str());
        ImGui::Text("Vertices: %u | Triangles: %u | SubMeshes: %zu | Materials: %zu", mMesh->GetVertexCount(), mMesh->GetIndexCount() / 3, mMesh->GetMeshData().GetSubMeshes().size(), mContent->GetMaterials().size());

        if (ImGui::Button("Frame Mesh")) {
            FrameMesh();
        }

        ImGui::SameLine();
        ImGui::TextDisabled(mSkeleton != nullptr ? "Click bone | LMB: orbit | MMB: pan | Wheel: zoom | RMB + WASD/QE: fly | RMB + Wheel: speed | Shift: fast | F: frame | W/E: move/rotate" : "LMB: orbit | MMB: pan | Wheel: zoom | RMB + WASD/QE: fly | RMB + Wheel: speed | Shift: fast | F: frame");
        DrawMaterials();

        if (mSkeletalMesh != nullptr) {
            DrawSkeletonPicker();
            DrawPhysicsAssetPicker(*mAssets, *mSkeletalMesh.Get(), mPath, mError);

            if (ImGui::CollapsingHeader("Clothing")) {
                DrawClothing();
            } else {
                // 천 칸을 닫으면 칠하기도 끝내 왼쪽 버튼을 카메라와 뼈 선택에 돌려줍니다.
                mClothPaintEnabled = false;
            }
        } else {
            // 정적 메시는 충돌 형상(상자, 구, 캡슐)을 편집할 수 있다.
            DrawCollision();
        }

        if (!mError.empty()) {
            // 실패 원인은 눈에 띄게 붉은색으로 보인다.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.45f, 0.45f, 1.0f});
            ImGui::TextWrapped("%s", mError.c_str());
            ImGui::PopStyleColor();
        }
    }

    /**
     * @brief 카메라 입력을 처리하고 공통 렌더 경로로 메시와 본 도형을 그리며 아이콘·스프라이트·텍스트 표시는 제외합니다.
     */
    void Editor::FMeshViewer::DrawPreview(const Core::FInput& Input) {
        // 미리 보기 위에 보기 방식과 표시 항목을 고르는 도구줄을 둡니다.
        DrawViewportToolbar();

        const ImVec2 Size{ImGui::GetContentRegionAvail()};

        if (Size.x < 1.0f || Size.y < 1.0f) {
            mMouseCaptured = false;

            return;
        }

        const Uint32 Width{static_cast<Uint32>(Size.x)};
        const Uint32 Height{static_cast<Uint32>(Size.y)};

        if (!mViewer.Resize(Width, Height)) {
            return;
        }

        // 미리 보기 천을 이번 프레임 포즈로 한 번 진행한 뒤 그립니다.
        TickClothPreview(ImGui::GetIO().DeltaTime);

        if (mNeedsFit) {
            mNeedsFit = !mViewer.Fit();
        }

        // 이미지는 입력을 소유하지 않아 드래그가 창 이동으로 처리되므로 버튼이 마우스를 받게 합니다.
        ImGui::InvisibleButton("##Preview", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

        const bool Hovered{ImGui::IsItemHovered()};

        using Core::EKey;

        const FVector2 Mouse{Input.GetMousePosition()};
        const FVector2 Delta{Input.GetMouseDelta()};
        const FVector2 Extent{Size.x, Size.y};
        const ImVec2 Origin{ImGui::GetItemRectMin()};

        ImGui::GetWindowDrawList()->AddImage(ImTextureRef{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mViewer.GetNativeTexture()))}, Origin, ImGui::GetItemRectMax());

        const Engine::FSceneView InputView{CalcSceneView(Extent)};
        const FVector2 PreviewMouse{Mouse.x - Origin.x, Mouse.y - Origin.y};
        const bool GizmoActive{mPoseBone && UpdateBoneGizmo(*mPoseBone, Input, InputView, PreviewMouse, Hovered)};
        const Core::TArray<Viewer::FBoneShape> BoneShapes{mSkeleton != nullptr && mShowBones ? Viewer::BuildBoneShapes(mSkeleton->GetBones(), mContent->GetPose().GetGlobalTransforms(), mViewer.GetCamera().GetRadius()) : Core::TArray<Viewer::FBoneShape>{}};
        // 천을 칠하는 동안 왼쪽 버튼은 붓으로 쓰고, 뼈 선택과 카메라 회전에는 쓰지 않습니다.
        const bool Painting{IsClothPainting()};
        const bool CanPickBone{Hovered && !mMouseCaptured && !GizmoActive && !BoneShapes.empty() && !Painting};
        const bool LeftClick{Input.WasPressed(EKey::MouseLeft) && !Input.IsDown(EKey::MouseRight) && !Input.IsDown(EKey::MouseMiddle)};
        const bool PickedBone{CanPickBone && LeftClick && PickBone(InputView, PreviewMouse, BoneShapes)};

        if (PickedBone) {
            ImGui::SetWindowFocus();
        } else if (!GizmoActive && Hovered && ((Input.WasPressed(EKey::MouseLeft) && !Painting) || Input.WasPressed(EKey::MouseRight) || Input.WasPressed(EKey::MouseMiddle))) {
            mMouseCaptured = true;

            ImGui::SetWindowFocus();
        }

        if (!ImGui::IsWindowFocused() || (!Input.IsDown(EKey::MouseLeft) && !Input.IsDown(EKey::MouseRight) && !Input.IsDown(EKey::MouseMiddle))) {
            mMouseCaptured = false;
        }

        // 오른쪽 버튼을 누른 동안은 비행(제자리 시선 회전 + WASD/QE 이동, 휠은 속도 조절), 왼쪽은 궤도 회전, 가운데는 이동이다.
        const bool Flying{mMouseCaptured && Input.IsDown(EKey::MouseRight) && !Input.IsDown(EKey::MouseMiddle)};

        if (Flying) {
            const ImGuiIO& Io{ImGui::GetIO()};
            Viewer::FFlyInput Fly{};

            Fly.mMouseDelta = Delta;
            Fly.mWheel = Hovered ? Input.GetMouseWheel() : 0.0f;
            Fly.mDeltaTime = Io.DeltaTime;
            Fly.mFast = Input.IsDown(EKey::Shift);

            if (!Io.WantTextInput) {
                Fly.mForward = Input.IsDown(EKey::W);
                Fly.mBackward = Input.IsDown(EKey::S);
                Fly.mRight = Input.IsDown(EKey::D);
                Fly.mLeft = Input.IsDown(EKey::A);
                Fly.mUp = Input.IsDown(EKey::E);
                Fly.mDown = Input.IsDown(EKey::Q);
            }

            mFlyCamera.Update(mViewer.GetCamera(), Fly);
        } else if (mMouseCaptured) {
            if (Input.IsDown(EKey::MouseMiddle)) {
                mViewer.GetCamera().Pan(Delta, Size.y);
            } else {
                mViewer.GetCamera().Orbit(Delta);
            }
        }

        if (Hovered && !Flying && Input.GetMouseWheel() != 0.0f) {
            mViewer.GetCamera().Zoom(Input.GetMouseWheel());
        }

        if (Hovered && !ImGui::GetIO().WantTextInput && Input.WasPressed(EKey::F)) {
            FrameMesh();
        }

        if (mSkeleton != nullptr && Hovered && !ImGui::GetIO().WantTextInput && !mGizmo.IsDragging() && !Input.IsDown(EKey::MouseRight)) {
            if (Input.WasPressed(EKey::W)) {
                mGizmoMode = Editor::EGizmoMode::Translate;
            } else if (Input.WasPressed(EKey::E)) {
                mGizmoMode = Editor::EGizmoMode::Rotate;
            }
        }

        // 기즈모와 천 와이어프레임은 같은 편집 표시 목록에 담아 장면 위에 그립니다.
        Engine::FPrimitiveDrawInterface Gizmo{};

        if (Painting) {
            DrawClothPaint(InputView, FVector2{Origin.x, Origin.y}, PreviewMouse, Hovered, Input, Gizmo);
        }

        const Engine::FSceneView View{CalcSceneView(Extent)};

        Engine::FPrimitiveDrawInterface Bones{};

        for (const Viewer::FBoneShape& Shape : BoneShapes) {
            const float Gray{Shape.GetMesh() == Engine::EEditorMesh::Sphere ? 0.76f : 0.66f};
            const FVector4 Color{Shape.GetBone() == mPoseBone ? FVector4{0.26f, 0.66f, 1.0f, 1.0f} : FVector4{Gray, Gray, Gray, 1.0f}};

            Bones.DrawMesh({Shape.GetMesh(), Shape.GetWorld(), Color});
        }

        if (mPoseBone) {
            mGizmo.BuildHandles(View, GetBonePlacement(*mPoseBone), Gizmo);
            mGizmo.DrawFeedback(Gizmo);
        }

        // 정적 메시의 충돌 형상도 같은 편집 표시 목록에 담는다.
        const bool ShowCollision{mSkeletalMesh == nullptr && mMesh != nullptr && mShowCollision};

        if (ShowCollision) {
            BuildCollisionLines(Gizmo);
        }

        mViewer.SetViewMode(mViewMode);
        mViewer.Render(mPoseBone || Painting || ShowCollision ? &Gizmo : nullptr, BoneShapes.empty() ? nullptr : &Bones);
    }

    /**
     * @brief 미리 보기 위에 보기 방식 선택 칸과 표시 항목 메뉴를 그립니다.
     */
    void Editor::FMeshViewer::DrawViewportToolbar() {
        // 보기 방식은 물리 에셋 편집기와 같은 목록과 순서를 씁니다.
        struct FViewModeEntry {
            const char* mName;
            Engine::EViewMode mMode;
        };
        static constexpr FViewModeEntry ViewModes[]{{"Lit", Engine::EViewMode::Lit}, {"Unlit", Engine::EViewMode::Unlit}, {"Wireframe", Engine::EViewMode::Wireframe}, {"Lit (Lambert)", Engine::EViewMode::Lit_Lambert}, {"Lit (Gouraud)", Engine::EViewMode::Lit_Gouraud}};
        const char* CurrentName{"Lit"};

        for (const FViewModeEntry& Entry : ViewModes) {
            if (Entry.mMode == mViewMode) {
                CurrentName = Entry.mName;
            }
        }

        ImGui::SetNextItemWidth(120.0f * ImGui::GetStyle().FontScaleMain);

        if (ImGui::BeginCombo("##ViewMode", CurrentName)) {
            for (const FViewModeEntry& Entry : ViewModes) {
                if (ImGui::Selectable(Entry.mName, Entry.mMode == mViewMode)) {
                    mViewMode = Entry.mMode;
                }
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();

        if (ImGui::Button("Show")) {
            ImGui::OpenPopup("##ShowFlags");
        }

        if (ImGui::BeginPopup("##ShowFlags")) {
            Engine::FEngineShowFlags& Flags{mViewer.GetShowFlags()};

            if (mSkeleton != nullptr) {
                ImGui::Checkbox("Show Bones", &mShowBones);
            }

            ImGui::Checkbox("Grid", &Flags.mGrid);
            ImGui::Checkbox("World Axes", &Flags.mAxes);
            ImGui::Checkbox("Sky", &Flags.mSky);
            ImGui::Checkbox("Shadows", &Flags.mShadows);
            ImGui::Checkbox("FXAA", &Flags.mFXAA);

            // 천이 있는 메시면 미리 보기 천에만 쓰는 바람과 중력을 고르게 합니다. 천 에셋 설정은 바꾸지 않습니다.
            if (mSkeletalMesh != nullptr && !mSkeletalMesh->GetMeshClothingAssets().empty()) {
                ImGui::SeparatorText("Clothing");

                if (ImGui::Checkbox("Simulate Cloth", &mClothSimulate)) {
                    mClothPreviewStale = true;
                }

                if (ImGui::Button("Reset Cloth")) {
                    mClothPreviewStale = true;
                }

                // 바람과 중력은 고른 천 에셋의 설정을 바로 바꿉니다. 저장하면 게임에서도 같은 값을 씁니다.
                PhysicsRuntime::UClothingAssetCommon* const ClothingAsset{GetSelectedClothingAsset()};
                PhysicsRuntime::UClothConfigNv* const Config{ClothingAsset != nullptr ? ClothingAsset->GetClothConfig() : nullptr};

                if (Config != nullptr) {
                    ImGui::TextDisabled("Cloth %zu settings (saved with the mesh)", mClothSelectedAsset);

                    // 바람은 세기와 방향으로 나눠 보여 주고, 저장은 방향 x 세기 하나로 합니다.
                    float WindStrength{Config->mDefaultWindVelocity.Length()};

                    if (WindStrength > 1.e-4f) {
                        mClothWindDirection = Config->mDefaultWindVelocity * (1.0f / WindStrength);
                    }

                    bool WindChanged{};

                    ImGui::SetNextItemWidth(160.0f * ImGui::GetStyle().FontScaleMain);
                    WindChanged = ImGui::DragFloat("Wind Strength", &WindStrength, 0.1f, 0.0f, 50.0f, "%.1f m/s", ImGuiSliderFlags_AlwaysClamp) || WindChanged;
                    ImGui::SetItemTooltip("이 천의 기본 바람 세기(m/s). 0 = 바람 없음\n컴포넌트의 Cloth Wind Velocity가 0이면 게임에서도 이 바람을 씀");

                    ImGui::SetNextItemWidth(160.0f * ImGui::GetStyle().FontScaleMain);
                    WindChanged = ImGui::DragFloat3("Wind Direction", &mClothWindDirection.x, 0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp) || WindChanged;
                    ImGui::SetItemTooltip("바람이 부는 방향(x, y, z). 길이는 자동으로 1로 맞춤");

                    if (WindChanged) {
                        Config->mDefaultWindVelocity = mClothWindDirection.IsNearlyZero() ? FVector{} : mClothWindDirection.Normalized() * WindStrength;
                        mClothUnsaved = true;
                    }

                    ImGui::SetNextItemWidth(160.0f * ImGui::GetStyle().FontScaleMain);

                    if (ImGui::DragFloat("Gravity Scale", &Config->mGravityScale, 0.01f, 0.0f, 10.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
                        mClothUnsaved = true;
                    }

                    ImGui::SetItemTooltip("이 천의 중력 배율. Settings > Simulation의 Gravity Scale과 같은 값");
                }
            }

            ImGui::EndPopup();
        }
    }

    /**
     * @brief 미리보기 옆에 본 계층 선택·기즈모 설정·포즈 복원 패널을 표시합니다.
     */
    void Editor::FMeshViewer::DrawPose(bool FillWidth) {
        const float Scale{ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};

        if (ImGui::BeginChild("Skeleton", {FillWidth ? 0.0f : 320.0f * Scale, 0.0f}, ImGuiChildFlags_Borders)) {
            const auto& Bones{mSkeleton->GetBones()};

            ImGui::Text("Bones: %zu", Bones.size());

            // 아래쪽 기즈모 설정 영역을 남기고 나머지 높이를 계층 트리에 할당합니다.
            const float ControlsHeight{ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() * 2.0f};

            if (ImGui::BeginChild("Bone Hierarchy", {0.0f, -ControlsHeight}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
                Core::TArray<Core::TArray<Index_t>> Children(Bones.size());

                for (Index_t Index{}; Index < Bones.size(); ++Index) {
                    const Int32 Parent{static_cast<Int32>(Bones[Index].GetParentIndex())};

                    if (Parent >= 0) {
                        Children[Parent].push_back(Index);
                    }
                }

                // 본 계층은 깊으므로 들여쓰기를 줄여 깊은 본의 이름까지 패널 안에 표시합니다.
                ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetStyle().IndentSpacing / 3.0f);

                for (Index_t Index{}; Index < Bones.size(); ++Index) {
                    if (Bones[Index].GetParentIndex() < 0) {
                        DrawBoneNode(Index, Children);
                    }
                }

                ImGui::PopStyleVar();
            }

            ImGui::EndChild();
            ImGui::TextUnformatted(mPoseBone ? Bones[*mPoseBone].GetName().c_str() : "Select a bone");

            if (ImGui::RadioButton("Rotate", mGizmoMode == Editor::EGizmoMode::Rotate)) {
                mGizmoMode = Editor::EGizmoMode::Rotate;
            }

            ImGui::SameLine();

            if (ImGui::RadioButton("Move", mGizmoMode == Editor::EGizmoMode::Translate)) {
                mGizmoMode = Editor::EGizmoMode::Translate;
            }

            ImGui::SameLine();

            bool Local{mGizmoSpace == Editor::EGizmoSpace::Local};

            if (ImGui::Checkbox("Local", &Local)) {
                mGizmoSpace = Local ? Editor::EGizmoSpace::Local : Editor::EGizmoSpace::World;
            }

            ImGui::BeginDisabled(!mPoseBone);

            if (ImGui::Button("Reset Bone") && mPoseBone && mContent->GetPose().SetLocalTransform(*mSkeletalMesh, *mSkeleton, *mPoseBone, Bones[*mPoseBone].GetBindPose())) {
                UpdateScene();
            }

            ImGui::EndDisabled();
            ImGui::SameLine();

            if (ImGui::Button("Reset Bind Pose") && mContent->GetPose().Reset(*mSkeletalMesh, *mSkeleton)) {
                UpdateScene();
            }
        }

        ImGui::EndChild();
    }

    /**
     * @brief 본 하나와 그 자손을 계층 트리 노드로 표시하고 클릭하면 조작 대상으로 선택합니다.
     */
    void Editor::FMeshViewer::DrawBoneNode(Index_t Bone, const Core::TArray<Core::TArray<Index_t>>& Children) {
        const auto& BoneChildren{Children[Bone]};
        const ImGuiTreeNodeFlags Flags{ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | (Bone == mPoseBone ? ImGuiTreeNodeFlags_Selected : 0) | (BoneChildren.empty() ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0)};

        // 본 이름은 중복될 수 있으므로 번호를 트리 노드 식별자로 사용합니다.
        const bool Open{ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::uintptr_t>(Bone)), Flags, "%s", mSkeleton->GetBones()[Bone].GetName().c_str())};

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            mGizmo.EndDrag();
            mPoseBone = Bone;
        }

        if (Open && !BoneChildren.empty()) {
            for (const Index_t Child : BoneChildren) {
                DrawBoneNode(Child, Children);
            }

            ImGui::TreePop();
        }
    }

    bool Editor::FMeshViewer::PickBone(const Engine::FSceneView& View, const FVector2& Mouse, const Core::TArray<Viewer::FBoneShape>& Shapes) {
        FRay Ray{};

        if (!View.Deproject(Mouse, Ray)) {
            return false;
        }

        const Viewer::FBoneShape* Hit{};
        float Closest{std::numeric_limits<float>::max()};

        for (auto Shape{Shapes.rbegin()}; Shape != Shapes.rend(); ++Shape) {
            if (Engine::RaycastMesh(&Engine::GetEditorMesh(Shape->GetMesh()), Shape->GetWorld(), Ray, Closest)) {
                Hit = &*Shape;
            }
        }

        if (Hit == nullptr) {
            return false;
        }

        const FVector4 Clip{FVector4{Ray.mOrigin + Ray.mDirection * Closest, 1.0f} * View.mViewProjection};

        if (Clip.z > Clip.w) {
            return false;
        }

        mPoseBone = Hit->GetBone();
        mGizmo.EndDrag();

        return true;
    }

    /**
     * @brief 현재 궤도 카메라의 렌더링과 입력 해석에 쓸 뷰를 계산합니다.
     */
    Engine::FSceneView Editor::FMeshViewer::CalcSceneView(const FVector2& Size) const {
        return mViewer.GetCamera().CalcSceneView(Size);
    }

    /**
     * @brief 선택한 본의 현재 전역 포즈로 기즈모 배치를 구성합니다.
     */
    Editor::FGizmoPlacement Editor::FMeshViewer::GetBonePlacement(Index_t Bone) const {
        const FMatrix& Global{mContent->GetPose().GetGlobalTransforms()[Bone]};
        Editor::FGizmoPlacement Placement{};
        Placement.mPivot = Global.TransformPosition({});
        Placement.mMode = mGizmoMode;

        // 본 행렬에는 스케일이 있을 수 있으므로 축을 정규화하고, 퇴화한 축은 월드 축으로 대체합니다.
        for (Index_t Index{}; Index < 3; ++Index) {
            const FVector Axis{Global.GetAxis(Index).Normalized()};

            Placement.mAxes[Index] = mGizmoSpace == Editor::EGizmoSpace::Local && !Axis.IsNearlyZero() ? Axis : Editor::FGizmoCore::GetUnitAxes()[Index];
        }

        return Placement;
    }

    /**
     * @brief 기즈모의 호버와 드래그 입력을 처리합니다.
     */
    bool Editor::FMeshViewer::UpdateBoneGizmo(Index_t Bone, const Core::FInput& Input, const Engine::FSceneView& View, const FVector2& Mouse, bool Hovered) {
        using Core::EKey;

        if (mGizmo.IsDragging()) {
            if (Input.WasPressed(EKey::Escape)) {
                mGizmo.EndDrag();

                if (mContent->GetPose().SetLocalTransform(*mSkeletalMesh, *mSkeleton, Bone, mDragStartLocal)) {
                    UpdateScene();
                }

                return true;
            }

            if (mGizmo.UpdateDrag(Mouse)) {
                ApplyBoneGizmo(Bone);
            }

            if (!Input.IsDown(EKey::MouseLeft)) {
                mGizmo.EndDrag();
            }

            return true;
        }

        const bool CanHover{Hovered && !mMouseCaptured && !Input.IsDown(EKey::MouseRight) && !Input.IsDown(EKey::MouseMiddle)};
        const Editor::FGizmoPlacement Placement{GetBonePlacement(Bone)};
        const Engine::FHitProxy Hit{CanHover ? mGizmo.HitTest(Mouse, View, Placement) : Engine::FHitProxy{}};
        const Int32 Axis{static_cast<Int32>(Hit.mType == Engine::EHitProxyType::GizmoAxis ? Hit.mAxis : -1)};

        mGizmo.SetHoveredAxis(Axis);

        if (Axis < 0 || !Input.WasPressed(EKey::MouseLeft) || !mGizmo.BeginDrag(Axis, Mouse, View, Placement)) {
            return false;
        }

        mDragStartGlobal = mContent->GetPose().GetGlobalTransforms()[Bone];
        mDragStartLocal = mContent->GetPose().GetLocalTransforms()[Bone];

        ImGui::SetWindowFocus();

        return true;
    }

    /**
     * @brief 기즈모의 드래그 결과를 선택한 본의 부모 기준 로컬 변환으로 바꾸어 적용합니다.
     */
    void Editor::FMeshViewer::ApplyBoneGizmo(Index_t Bone) {
        FMatrix Delta{FMatrix::Translation(mGizmo.GetTranslation())};

        if (mGizmo.GetDragMode() == Editor::EGizmoMode::Rotate) {
            const FVector& Pivot{mGizmo.GetStartPivot()};

            Delta = FMatrix::Translation(-Pivot) * FMatrix::Rotation(mGizmo.GetRotation()) * FMatrix::Translation(Pivot);
        }

        const Int32 Parent{static_cast<Int32>(mSkeleton->GetBones()[Bone].GetParentIndex())};
        FMatrix ParentInverse{FMatrix::Identity()};

        if (Parent >= 0 && !mContent->GetPose().GetGlobalTransforms()[Parent].TryInverse(ParentInverse)) {
            return;
        }

        // 행 벡터 규약에서 전역 변환은 Local * ParentGlobal이므로 부모의 역행렬을 오른쪽에 곱해 로컬 변환을 되찾습니다.
        FMatrix Local{mDragStartGlobal * Delta * ParentInverse};

        // 역행렬의 부동소수점 오차가 아핀 검사에 걸리지 않도록 마지막 열을 고정합니다.
        for (Index_t Row{}; Row < 3; ++Row) {
            Local(Row, 3) = 0.0f;
        }

        Local(3, 3) = 1.0f;

        if (mContent->GetPose().SetLocalTransform(*mSkeletalMesh, *mSkeleton, Bone, Local)) {
            UpdateScene();
        }
    }

}
