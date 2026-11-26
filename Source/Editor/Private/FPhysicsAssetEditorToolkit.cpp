/**
 * @file FPhysicsAssetEditorToolkit.cpp
 * @brief 스켈레탈 메시의 물리 에셋을 미리보기와 함께 편집하는 에셋 툴킷을 구현합니다.
 * @ingroup Editor
 */

#include "FPhysicsAssetEditorToolkit.h"

#include "EditorStyle.h"
#include "FPhysicsAssetDrawing.h"
#include "FPhysicsAssetViewerContent.h"
#include "Asset/FAssetFile.h"
#include "Asset/FAssetManager.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Core/FilePath.h"
#include "Core/Literals.h"
#include "Core/Math/FQuat.h"
#include "Core/Math/FRay.h"
#include "Editor/EEditorIcon.h"
#include "Editor/FEditorContext.h"
#include "Editor/FEditorToolkitHost.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/FSceneView.h"
#include "Engine/FProject.h"
#include "Engine/UWorld.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/FPhysicsScene.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "PhysicsRuntime/UPhysicsAsset.h"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>

using namespace Core::Literals;
using namespace Editor;

namespace {
    /**
     * @brief 주 축과 보조 축으로 만든 프레임 방향을 오일러 회전(도)으로 바꿉니다.
     * @param[in] Primary 프레임의 X축으로 쓰는 주 축입니다.
     * @param[in] Secondary 프레임의 Y축을 정하는 보조 축입니다.
     * @return 프레임 방향의 회전이며 두 축이 평행하면 회전 없음을 반환합니다.
     */
    FRotator AxesToRotator(const FVector& Primary, const FVector& Secondary) {
        const FVector Cross{Primary.Cross(Secondary)};

        if (Primary.LengthSquared() < 1e-8f || Cross.LengthSquared() < 1e-8f) {
            return FRotator{};
        }

        const FVector AxisX{Primary.Normalized()};
        const FVector AxisZ{Cross.Normalized()};
        const FVector AxisY{AxisZ.Cross(AxisX)};
        const FVector Axes[]{AxisX, AxisY, AxisZ};
        FMatrix Matrix{FMatrix::Identity()};

        // 관절 프레임을 만드는 방식과 같이 축을 행으로 쌓는다.
        for (Index_t Row{}; Row < 3; ++Row) {
            Matrix(Row, 0) = Axes[Row].x;
            Matrix(Row, 1) = Axes[Row].y;
            Matrix(Row, 2) = Axes[Row].z;
        }

        FTransform Result{};

        return Matrix.TryGetTransform(Result) ? FRotator{Result.mRotation} : FRotator{};
    }

    /**
     * @brief 벡터의 성분 하나를 참조로 반환합니다.
     * @param[in,out] Value 대상 벡터입니다.
     * @param[in] Index 0(X), 1(Y), 2(Z) 중 하나입니다.
     * @return 성분의 참조입니다.
     */
    float& ComponentOf(FVector& Value, int Index) {
        return Index == 0 ? Value.x : Index == 1 ? Value.y : Value.z;
    }

    /**
     * @brief 회전의 성분 하나를 참조로 반환합니다.
     * @param[in,out] Value 대상 회전입니다.
     * @param[in] Index 0(Pitch), 1(Yaw), 2(Roll) 중 하나입니다.
     * @return 성분의 참조입니다.
     */
    float& ComponentOf(FRotator& Value, int Index) {
        return Index == 0 ? Value.pitch : Index == 1 ? Value.yaw : Value.roll;
    }

    /**
     * @brief 값이 기본값과 다를 때 행 오른쪽 끝에 기본값으로 되돌리는 화살표 버튼을 그립니다.
     * @param[in] Id 행을 구분하는 이름입니다.
     * @param[in] Differs 값이 기본값과 다르면 true이며, 같으면 아무것도 그리지 않습니다.
     * @return 이번 프레임에 버튼을 눌렀으면 true입니다.
     */
    bool ResetButton(const char* Id, bool Differs) {
        if (!Differs) {
            return false;
        }

        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
        ImGui::PushID(Id);

        const bool Clicked{ImGui::ArrowButton("##Reset", ImGuiDir_Left)};

        ImGui::PopID();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset to default");
        }

        return Clicked;
    }

    /**
     * @brief 두 벡터가 사실상 같은 값인지 반환합니다.
     * @param[in] A 비교할 벡터입니다.
     * @param[in] B 비교할 벡터입니다.
     * @return 같으면 true입니다.
     */
    bool NearlyEqual(const FVector& A, const FVector& B) {
        return (A - B).LengthSquared() < 1e-8f;
    }

    /**
     * @brief 같은 줄에 드래그 필드 여러 개를 놓고 값이 서로 다른 성분은 "Multiple Values"로 표시합니다.
     * @param[in] Label 필드 오른쪽에 표시할 이름입니다.
     * @param[in] Count 성분의 개수입니다.
     * @param[in,out] Values 성분 값입니다.
     * @param[in] Mixed 성분마다 대상들의 값이 서로 다른지 나타냅니다.
     * @param[in] Speed 드래그 한 픽셀당 값의 변화량입니다.
     * @param[in] Min 성분의 최소값입니다.
     * @param[in] Max 성분의 최대값입니다.
     * @param[in] Format 값이 같을 때 쓰는 printf 형식입니다.
     * @return 이번 프레임에 값이 바뀐 성분을 비트로 나타낸 값(성분 i가 바뀌면 1 << i)입니다.
     */
    unsigned DragComponents(const char* Label, int Count, float* Values, const bool* Mixed, float Speed, float Min, float Max, const char* Format) {
        const float Spacing{ImGui::GetStyle().ItemInnerSpacing.x};
        const float Width{(ImGui::CalcItemWidth() - Spacing * static_cast<float>(Count - 1)) / static_cast<float>(Count)};
        unsigned Changed{};

        ImGui::PushID(Label);

        for (int Component{}; Component < Count; ++Component) {
            if (Component > 0) {
                ImGui::SameLine(0.0f, Spacing);
            }

            ImGui::PushID(Component);
            ImGui::SetNextItemWidth(Width);

            // 서식에 변환 지정자가 없으면 값 대신 그 문자열이 그대로 표시되고, 값은 드래그와 직접 입력으로만 바뀐다.
            if (ImGui::DragFloat("##Value", &Values[Component], Speed, Min, Max, Mixed[Component] ? "Multiple Values" : Format)) {
                Values[Component] = std::clamp(Values[Component], Min, Max);
                Changed |= 1u << Component;
            }

            ImGui::PopID();
        }

        ImGui::SameLine(0.0f, Spacing);
        ImGui::TextUnformatted(Label);
        ImGui::PopID();

        return Changed;
    }

    /**
     * @brief 항목 여러 개의 같은 벡터·회전 멤버를 한 필드로 편집합니다. 바꾼 성분만 모든 항목에 적용합니다.
     * @tparam TElem 항목 형식입니다.
     * @tparam TValue 멤버 형식이며 FVector 또는 FRotator입니다.
     * @param[in] Label 필드 이름입니다.
     * @param[in] Elems 편집할 항목들이며 비어 있으면 안 됩니다.
     * @param[in] Member 편집할 멤버입니다.
     * @param[in] Speed 드래그 속도입니다.
     * @param[in] Min 성분의 최소값입니다.
     * @param[in] Max 성분의 최대값입니다.
     * @param[in] Format 값이 같을 때 쓰는 printf 형식입니다.
     * @return 값이 바뀌었으면 true입니다.
     */
    template <typename TElem, typename TValue>
    bool EditVectorMember(const char* Label, const Core::TArray<TElem*>& Elems, const Core::TArray<TElem>& Defaults, TValue TElem::* Member, float Speed, float Min, float Max, const char* Format) {
        TValue First{Elems.front()->*Member};
        float Values[3]{ComponentOf(First, 0), ComponentOf(First, 1), ComponentOf(First, 2)};
        bool Mixed[3]{};

        for (const TElem* Elem : Elems) {
            TValue Other{Elem->*Member};

            for (int Component{}; Component < 3; ++Component) {
                Mixed[Component] |= ComponentOf(Other, Component) != Values[Component];
            }
        }

        const unsigned Mask{DragComponents(Label, 3, Values, Mixed, Speed, Min, Max, Format)};

        // 항목마다 기준값(Defaults)과 비교해 하나라도 다르면 기준값으로 되돌리는 화살표를 보인다.
        bool Differs{};

        for (Index_t Index{}; Index < Elems.size(); ++Index) {
            TValue Other{Elems[Index]->*Member};
            TValue Default{Defaults[Index].*Member};

            for (int Component{}; Component < 3; ++Component) {
                Differs |= ComponentOf(Other, Component) != ComponentOf(Default, Component);
            }
        }

        if (ResetButton(Label, Differs)) {
            for (Index_t Index{}; Index < Elems.size(); ++Index) {
                Elems[Index]->*Member = Defaults[Index].*Member;
            }

            return true;
        }

        if (Mask == 0) {
            return false;
        }

        for (TElem* Elem : Elems) {
            for (int Component{}; Component < 3; ++Component) {
                if ((Mask & (1u << Component)) != 0) {
                    ComponentOf(Elem->*Member, Component) = Values[Component];
                }
            }
        }

        return true;
    }

    /**
     * @brief 항목 여러 개의 같은 길이 멤버를 미터 단위로 편집합니다.
     * @tparam TElem 항목 형식입니다.
     * @param[in] Label 필드 이름입니다.
     * @param[in] Elems 편집할 항목들이며 비어 있으면 안 됩니다.
     * @param[in] Member 편집할 멤버입니다.
     * @param[in] Speed 드래그 속도입니다.
     * @param[in] Min 최소값입니다.
     * @param[in] Max 최대값입니다.
     * @return 값이 바뀌었으면 true입니다.
     */
    template <typename TElem>
    bool EditLengthMember(const char* Label, const Core::TArray<TElem*>& Elems, const Core::TArray<TElem>& Defaults, Core::FLength TElem::* Member, float Speed, float Min, float Max) {
        const Core::FLength InitialValue{Elems.front()->*Member};
        float Value{InitialValue.GetBase()};
        bool Mixed{};

        for (const TElem* Elem : Elems) {
            Mixed |= Elem->*Member != InitialValue;
        }

        const bool Dragged{DragComponents(Label, 1, &Value, &Mixed, Speed, Min, Max, "%.4f") != 0};

        // 항목마다 기준값(Defaults)과 비교한다.
        bool Differs{};

        for (Index_t Index{}; Index < Elems.size(); ++Index) {
            Differs |= Elems[Index]->*Member != Defaults[Index].*Member;
        }

        if (ResetButton(Label, Differs)) {
            for (Index_t Index{}; Index < Elems.size(); ++Index) {
                Elems[Index]->*Member = Defaults[Index].*Member;
            }

            return true;
        }

        if (!Dragged) {
            return false;
        }

        for (TElem* Elem : Elems) {
            Elem->*Member = Value * 1_m;
        }

        return true;
    }
}

struct FPhysicsAssetEditorToolkit::FBaselineBody {
    Core::TArray<PhysicsRuntime::FKSphereElem> mSpheres{};
    Core::TArray<PhysicsRuntime::FKBoxElem> mBoxes{};
    Core::TArray<PhysicsRuntime::FKSphylElem> mSphyls{};
    bool mSimulatePhysics{true};
    bool mCollisionEnabled{true};
    float mLinearDamping{0.1f};
    float mAngularDamping{0.5f};
    float mMassInKg{0.0f};
    float mInertiaScale{1.0f};
    float mWindInfluence{0.0f};
};

struct FPhysicsAssetEditorToolkit::FBaseline {
    Core::TMap<Core::FString, FBaselineBody> mBodies{};
    Core::TArray<std::unique_ptr<PhysicsRuntime::FConstraintInstance>> mConstraints{};
};

void FPhysicsAssetEditorToolkit::EnsureBaseline() {
    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (mBaseline != nullptr || PhysicsAsset == nullptr) {
        return;
    }

    // Convex는 편집하지 않으므로 기준에 담지 않는다.
    mBaseline = std::make_unique<FBaseline>();

    for (const auto& Setup : PhysicsAsset->mSkeletalBodySetups) {
        if (Setup == nullptr) {
            continue;
        }

        FBaselineBody& Body{mBaseline->mBodies[Setup->GetBoneName()]};

        Body.mSpheres = Setup->GetAggGeom().mSphereElems;
        Body.mBoxes = Setup->GetAggGeom().mBoxElems;
        Body.mSphyls = Setup->GetAggGeom().mSphylElems;
        Body.mSimulatePhysics = Setup->ShouldSimulatePhysics();
        Body.mCollisionEnabled = Setup->IsCollisionEnabled();
        Body.mLinearDamping = Setup->GetLinearDamping().GetBase();
        Body.mAngularDamping = Setup->GetAngularDamping().GetBase();
        Body.mMassInKg = Setup->GetMassInKg().GetBase();
        Body.mInertiaScale = Setup->GetInertiaScale();
        Body.mWindInfluence = Setup->GetWindInfluence();
    }

    for (const auto& Template : PhysicsAsset->mConstraintSetup) {
        if (Template != nullptr) {
            auto Copy{std::make_unique<PhysicsRuntime::FConstraintInstance>()};

            Copy->CopyConstraintParamsFrom(Template->mDefaultInstance);
            mBaseline->mConstraints.push_back(std::move(Copy));
        }
    }
}

const FPhysicsAssetEditorToolkit::FBaselineBody* FPhysicsAssetEditorToolkit::FindBaselineBody(const Core::FString& BoneName) const {
    if (mBaseline == nullptr) {
        return nullptr;
    }

    const auto Found{mBaseline->mBodies.find(BoneName)};

    return Found != mBaseline->mBodies.end() ? &Found->second : nullptr;
}

const PhysicsRuntime::FConstraintInstance* FPhysicsAssetEditorToolkit::FindBaselineConstraint(const Core::FString& Bone1, const Core::FString& Bone2) const {
    if (mBaseline == nullptr) {
        return nullptr;
    }

    for (const auto& Constraint : mBaseline->mConstraints) {
        if (Constraint->mConstraintBone1 == Bone1 && Constraint->mConstraintBone2 == Bone2) {
            return Constraint.get();
        }
    }

    return nullptr;
}

FPhysicsAssetEditorToolkit::FPhysicsAssetEditorToolkit(FEditorContext* Context, const std::filesystem::path& AssetPath, const std::filesystem::path& MeshPath, Asset::USkeletalMesh* Mesh, Asset::USkeleton* Skeleton, const Core::FString& PhysicsAssetPath)
    : FAssetEditorToolkit{AssetPath, Asset::EAssetType::PhysicsAsset, "###PhysicsAssetEditor:" + Core::FilePath::ToGenericUtf8(AssetPath)},
      mContext{Context},
      mMeshPath{MeshPath},
      mPhysicsAssetPath{PhysicsAssetPath},
      mMesh{Mesh},
      mViewer{Context->GetRenderer(), Context->GetProject()->GetAssets(), true} {
    GetTab().SetIcon(EEditorIcon::Skeleton, IM_COL32(255, 120, 90, 255));

    // 바디 형상은 수 cm 단위이므로 이동 스냅의 기본 간격을 1cm로 둔다.
    Editor::FGizmoSettings GizmoSettings{};

    GizmoSettings.mTranslationStep = 1_cm;
    mGizmo.SetSettings(GizmoSettings);

    FEditorToolkitHost& Host{GetToolkitHost()};
    Host.AddWidget("Preview", "Viewport", [this](float) {
        DrawViewport();
    });
    Host.GetTabManager().FindTab("Preview")->SetWindowFlags(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Host.AddWidget("Hierarchy", "Hierarchy", [this](float) {
        DrawHierarchy();
    },
        EEditorDockRegion::Left);
    Host.AddWidget("Details", "Details", [this](float) {
        DrawDetails();
    },
        EEditorDockRegion::Details);
    Host.AddWidget("Graph", "Graph", [this](float) {
        DrawGraph();
    },
        EEditorDockRegion::Bottom);

    Core::FString DefaultPath{};

    if (Skeleton == nullptr) {
        Skeleton = FindDefaultSkeleton(DefaultPath);
    }

    if (Skeleton != nullptr) {
        if (BuildPreview(Skeleton)) {
            mSkeletonPath = std::move(DefaultPath);
        } else {
            mSkeleton = nullptr;
        }
    }
}

FPhysicsAssetEditorToolkit::~FPhysicsAssetEditorToolkit() {
    if (mGraphEditor != nullptr) {
        ax::NodeEditor::DestroyEditor(mGraphEditor);
    }
}

void FPhysicsAssetEditorToolkit::DrawGraph() {
    namespace ed = ax::NodeEditor;

    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (PhysicsAsset == nullptr || mSkeleton == nullptr || mContent == nullptr || !mContent->IsReady()) {
        ImGui::TextDisabled("No preview");

        return;
    }

    const auto IsValidBody{[PhysicsAsset](Index_t Index) {
        return Index != InvalidIndex && Index < PhysicsAsset->mSkeletalBodySetups.size() && PhysicsAsset->mSkeletalBodySetups[Index] != nullptr;
    }};

    // 그래프의 중심은 선택과 따로 관리한다. Hierarchy나 뷰포트에서 바디를 고르거나 노드를 더블클릭할 때만 바뀌고,
    // 그래프 안에서 노드를 한 번 누르는 것은 선택만 바꾼다. 유효한 중심이 하나도 없으면 선택한 바디를 쓴다.
    mGraphFocus.erase(std::remove_if(mGraphFocus.begin(), mGraphFocus.end(), [&IsValidBody](Index_t Index) {
                          return !IsValidBody(Index);
                      }),
        mGraphFocus.end());

    if (mGraphFocus.empty()) {
        for (const Index_t Index : mSelectedBodies) {
            if (IsValidBody(Index)) {
                mGraphFocus.push_back(Index);
            }
        }
    }

    if (mGraphFocus.empty()) {
        ImGui::TextDisabled("Select a body to see its constraints.");

        return;
    }

    const Core::TArray<Index_t> Focus{mGraphFocus};

    // 노드와 핀, 링크의 ID는 구역(중심 바디마다 하나), 번호, 종류를 한 정수로 묶어 서로 겹치지 않게 한다.
    // 같은 바디가 다른 구역에도 나올 수 있으므로 구역 번호가 ID에 들어간다.
    enum EGraphId : std::uintptr_t {
        BodyNode,
        ConstraintNode,
        BodyPin,
        ConstraintLeftPin,
        ConstraintRightPin,
        LeftLink,
        RightLink
    };
    constexpr std::uintptr_t KindCount{8};
    constexpr std::uintptr_t IndexStride{100000};
    const auto MakeId{[](EGraphId Kind, Index_t Index, Index_t Section) {
        return (static_cast<std::uintptr_t>(Section) * IndexStride + Index) * KindCount + Kind + 1;
    }};
    // ID에서 종류와 번호를 꺼낸다. 구역은 버린다.
    const auto DecodeId{[](std::uintptr_t Id, std::uintptr_t& Kind, Index_t& Index) {
        const std::uintptr_t Key{Id - 1};

        Kind = Key % KindCount;
        Index = static_cast<Index_t>((Key / KindCount) % IndexStride);
    }};

    if (mGraphEditor == nullptr) {
        ed::Config Config{};

        Config.SettingsFile = nullptr;
        mGraphEditor = ed::CreateEditor(&Config);
    }

    // 중심 바디마다 별개의 구역을 만든다. 구역은 그 바디의 뼈가 양 끝 중 하나인 관절과 반대편 바디로 이루어진다.
    struct FGraphLink {
        Index_t mConstraint{InvalidIndex};
        Index_t mOtherBody{InvalidIndex};
    };
    Core::TArray<Core::TArray<FGraphLink>> Sections{};

    for (const Index_t FocusBody : Focus) {
        const Core::FString FocusName{PhysicsAsset->mSkeletalBodySetups[FocusBody]->GetBoneName()};
        Core::TArray<FGraphLink>& Links{Sections.emplace_back()};

        for (Index_t Index{}; Index < PhysicsAsset->mConstraintSetup.size(); ++Index) {
            const auto& Template{PhysicsAsset->mConstraintSetup[Index]};

            if (Template == nullptr) {
                continue;
            }

            const PhysicsRuntime::FConstraintInstance& Instance{Template->mDefaultInstance};

            if (Instance.mConstraintBone1 == FocusName) {
                Links.push_back({Index, PhysicsAsset->FindBodyIndex(Instance.mConstraintBone2)});
            } else if (Instance.mConstraintBone2 == FocusName) {
                Links.push_back({Index, PhysicsAsset->FindBodyIndex(Instance.mConstraintBone1)});
            }
        }
    }

    // 중심 바디가 바뀌면 노드를 다시 배치하고 화면을 맞춘다. 이후에는 사용자가 옮긴 위치를 유지한다.
    const bool Relayout{mGraphLaidOutFocus != Focus};

    if (Relayout) {
        mGraphLaidOutFocus = Focus;
        mGraphFitFrames = 3;
    }

    ed::SetCurrentEditor(mGraphEditor);
    ed::Begin("PhysicsAssetGraph", ImGui::GetContentRegionAvail());

    // 노드 크기(글꼴 크기에 따라 가로 약 300px, 세로 약 90px)보다 넉넉하게 간격을 둔다.
    const float RowHeight{115.0f};
    const float SectionGap{70.0f};
    const float ConstraintX{270.0f};
    const float OtherBodyX{680.0f};

    // 바디 노드는 이름과 항목 수를 보여 주고, 핀은 관절과 잇는 쪽에 둔다.
    const auto DrawBodyNode{[&](Index_t BodyIndex, Index_t Section, float X, float Y, bool PinOnRight) {
        const PhysicsRuntime::UBodySetup& Setup{*PhysicsAsset->mSkeletalBodySetups[BodyIndex]};
        const ed::NodeId Id{MakeId(BodyNode, BodyIndex, Section)};

        if (Relayout) {
            ed::SetNodePosition(Id, {X, Y});
        }

        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor{150, 100, 90, 230});
        ed::BeginNode(Id);

        if (!PinOnRight) {
            ed::BeginPin(ed::PinId{MakeId(BodyPin, BodyIndex, Section)}, ed::PinKind::Output);
            ImGui::TextUnformatted("o");
            ed::EndPin();
            ImGui::SameLine();
        }

        ImGui::TextUnformatted("Body");

        if (PinOnRight) {
            ImGui::SameLine();
            ed::BeginPin(ed::PinId{MakeId(BodyPin, BodyIndex, Section)}, ed::PinKind::Output);
            ImGui::TextUnformatted("o");
            ed::EndPin();
        }

        ImGui::TextUnformatted(Setup.GetBoneName().c_str());
        ImGui::Text("%zu shape(s)", PhysicsAssetEditing::GetTotalShapeCount(Setup));
        ed::EndNode();
        ed::PopStyleColor();
    }};

    // 그려진 노드를 모아 두고 선택 동기화에 쓴다. (바디 노드는 바디 번호, 관절 노드는 관절 번호와 함께)
    Core::TArray<std::pair<std::uintptr_t, Index_t>> DrawnBodyNodes{};
    Core::TArray<std::pair<std::uintptr_t, Index_t>> DrawnConstraintNodes{};
    float SectionY{};

    for (Index_t Section{}; Section < Focus.size(); ++Section) {
        const Core::TArray<FGraphLink>& Links{Sections[Section]};
        const float CenterY{SectionY + (static_cast<float>(std::max<Index_t>(Links.size(), 1)) - 1.0f) * RowHeight * 0.5f};

        DrawBodyNode(Focus[Section], Section, 0.0f, CenterY, true);
        DrawnBodyNodes.push_back({MakeId(BodyNode, Focus[Section], Section), Focus[Section]});

        Core::TArray<Index_t> DrawnOthers{};

        for (Index_t Row{}; Row < Links.size(); ++Row) {
            const auto [ConstraintIndex, OtherBody] = Links[Row];
            const PhysicsRuntime::FConstraintInstance& Instance{PhysicsAsset->mConstraintSetup[ConstraintIndex]->mDefaultInstance};
            const ed::NodeId Id{MakeId(ConstraintNode, ConstraintIndex, Section)};
            const float Y{SectionY + static_cast<float>(Row) * RowHeight};

            if (Relayout) {
                ed::SetNodePosition(Id, {ConstraintX, Y});
            }

            // 관절 노드의 제목은 UE처럼 "자식 : 부모"이다.
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor{150, 140, 80, 230});
            ed::BeginNode(Id);
            ed::BeginPin(ed::PinId{MakeId(ConstraintLeftPin, ConstraintIndex, Section)}, ed::PinKind::Input);
            ImGui::TextUnformatted("o");
            ed::EndPin();
            ImGui::SameLine();
            ImGui::TextUnformatted("Constraint");
            ImGui::SameLine();
            ed::BeginPin(ed::PinId{MakeId(ConstraintRightPin, ConstraintIndex, Section)}, ed::PinKind::Output);
            ImGui::TextUnformatted("o");
            ed::EndPin();
            ImGui::Text("%s : %s", Instance.mConstraintBone1.c_str(), Instance.mConstraintBone2.c_str());
            ed::EndNode();
            ed::PopStyleColor();
            DrawnConstraintNodes.push_back({Id.Get(), ConstraintIndex});

            ed::Link(ed::LinkId{MakeId(LeftLink, ConstraintIndex, Section)}, ed::PinId{MakeId(BodyPin, Focus[Section], Section)}, ed::PinId{MakeId(ConstraintLeftPin, ConstraintIndex, Section)}, ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, 2.0f);

            if (!IsValidBody(OtherBody)) {
                continue;
            }

            // 같은 바디에 관절이 여러 개 걸려도 구역 안에서 바디 노드는 한 번만 그린다.
            if (std::find(DrawnOthers.begin(), DrawnOthers.end(), OtherBody) == DrawnOthers.end()) {
                DrawnOthers.push_back(OtherBody);
                DrawBodyNode(OtherBody, Section, OtherBodyX, Y, false);
                DrawnBodyNodes.push_back({MakeId(BodyNode, OtherBody, Section), OtherBody});
            }

            ed::Link(ed::LinkId{MakeId(RightLink, ConstraintIndex, Section)}, ed::PinId{MakeId(ConstraintRightPin, ConstraintIndex, Section)}, ed::PinId{MakeId(BodyPin, OtherBody, Section)}, ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, 2.0f);
        }

        SectionY += static_cast<float>(std::max<Index_t>(Links.size(), 1)) * RowHeight + SectionGap;
    }

    // 툴킷의 선택과 노드 에디터의 선택을 맞춘다. 바깥에서 선택이 바뀌면 노드를 고르고, 노드를 직접 누르면 툴킷의 선택을 바꾼다.
    // 같은 바디나 관절이 여러 구역에 그려져 있으면 모두 고른다.
    // 노드가 아닌 빈 공간을 누르면 선택을 모두 푼다. (그래프의 중심은 그대로 둔다.)
    if (ed::IsBackgroundClicked()) {
        ClearSelection();
    }

    Core::TArray<std::uintptr_t> Ours{};

    for (const auto& [Id, ConstraintIndex] : DrawnConstraintNodes) {
        if (ConstraintIndex == mSelectedConstraint) {
            Ours.push_back(Id);
        }
    }

    if (Ours.empty()) {
        for (const auto& [Id, BodyIndex] : DrawnBodyNodes) {
            if (IsBodySelected(BodyIndex)) {
                Ours.push_back(Id);
            }
        }
    }

    std::sort(Ours.begin(), Ours.end());

    if (Ours != mGraphSyncedKeys || Relayout) {
        ed::ClearSelection();

        for (const std::uintptr_t Id : Ours) {
            ed::SelectNode(ed::NodeId{Id}, true);
        }

        mGraphSyncedKeys = Ours;
    } else {
        // 노드 에디터에서 사용자가 선택을 바꿨으면 툴킷의 선택에 반영한다. (Ctrl+클릭으로 여러 노드도 고를 수 있다.)
        constexpr int MaxPicked{64};
        ed::NodeId PickedNodes[MaxPicked]{};
        const int PickedCount{ed::GetSelectedNodes(PickedNodes, MaxPicked)};
        Core::TArray<std::uintptr_t> Picked{};

        for (int Index{}; Index < PickedCount; ++Index) {
            Picked.push_back(PickedNodes[Index].Get());
        }

        std::sort(Picked.begin(), Picked.end());

        if (Picked != Ours) {
            mGraphSyncedKeys = Picked;

            // 그래프 안의 클릭은 선택만 바꾸고 그래프의 중심은 그대로 둔다.
            Core::TArray<Index_t> PickedBodies{};
            Index_t PickedConstraint{InvalidIndex};

            for (const std::uintptr_t Id : Picked) {
                std::uintptr_t Kind{};
                Index_t Index{};

                DecodeId(Id, Kind, Index);

                if (Kind == BodyNode && IsValidBody(Index)) {
                    if (std::find(PickedBodies.begin(), PickedBodies.end(), Index) == PickedBodies.end()) {
                        PickedBodies.push_back(Index);
                    }
                } else if (Kind == ConstraintNode) {
                    PickedConstraint = Index;
                }
            }

            if (!PickedBodies.empty()) {
                // 새로 더해진 바디를 대표로 삼고, 없으면 첫 바디를 쓴다.
                Index_t Primary{PickedBodies.front()};

                for (const Index_t Index : PickedBodies) {
                    if (!IsBodySelected(Index)) {
                        Primary = Index;
                    }
                }

                // 대표 바디로 뼈 선택을 맞춘 뒤 목록을 노드 에디터의 선택으로 바꾼다.
                SelectBody(Primary, PhysicsAssetEditing::FShapeRef{}, false);
                mSelectedBodies = PickedBodies;
            } else if (PickedConstraint != InvalidIndex) {
                mSelectedConstraint = PickedConstraint;
                mSelectedShape = PhysicsAssetEditing::FShapeRef{};
            }
        }
    }

    // 바디 노드를 더블클릭하면 그 바디를 그래프의 새 중심 하나로 삼는다.
    if (const std::uintptr_t Double{ed::GetDoubleClickedNode().Get()}; Double != 0) {
        std::uintptr_t Kind{};
        Index_t Index{};

        DecodeId(Double, Kind, Index);

        if (Kind == BodyNode && IsValidBody(Index)) {
            mGraphFocus = {Index};
            SelectBody(Index, PhysicsAssetEditing::FShapeRef{}, false);
        }
    }

    if (mGraphFitFrames > 0) {
        ed::NavigateToContent(0.0f);
        --mGraphFitFrames;
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void FPhysicsAssetEditorToolkit::UpdateEditor(float) {
    // 저장하지 않은 수정이 있으면 제목 끝에 *를 붙인다.
    GetTab().SetName(EditorStyle::TabIconPadding + Core::FilePath::ToUtf8(GetAssetPath().stem()) + " Physics" + (mDirty ? " *" : "") + "###PhysicsAssetEditor:" + Core::FilePath::ToGenericUtf8(GetAssetPath()));
}

bool FPhysicsAssetEditorToolkit::BuildPreview(Asset::USkeleton* Skeleton) {
    // 바닥 바디는 이전 물리 장면에 속하므로 콘텐츠를 바꾸기 전에 해제한다.
    mGrab.End();
    mFloorBody.reset();
    mBaseline.reset();
    mSimulating = false;

    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};
    auto Content{std::make_unique<FPhysicsAssetViewerContent>(mContext->GetRenderer(), Assets)};

    // 미리보기 월드는 엔진이 어댑트한 월드가 아니므로 메인 월드가 쓰는 PhysX 런타임을 빌려 준다.
    const Engine::UWorld* MainWorld{mContext->GetWorld()};
    PhysicsRuntime::FPhysicsRuntime* Runtime{MainWorld != nullptr ? MainWorld->GetPhysicsRuntime() : nullptr};

    if (!Content->SetMesh(mMesh, Skeleton, Runtime, mError)) {
        return false;
    }

    FPhysicsAssetViewerContent* Raw{Content.get()};

    if (!mViewer.SetContent(std::move(Content))) {
        mError = mViewer.GetError();

        return false;
    }

    mContent = Raw;
    mSkeleton = Skeleton;
    mNeedsFrame = true;
    mError.clear();
    RebuildBoneTree();

    // 물리 에셋 파일을 편집할 때는 미리보기 컴포넌트가 그 파일을 쓰도록 지정한다.
    if (!mPhysicsAssetPath.empty() && !Raw->GetComponent()->SetPhysicsAssetOverride(mPhysicsAssetPath)) {
        mError = "Cannot load the physics asset: " + mPhysicsAssetPath;

        return false;
    }

    return true;
}

void FPhysicsAssetEditorToolkit::AssignToMesh() {
    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};

    if (mPhysicsAssetPath.empty() || mMesh == nullptr) {
        return;
    }

    if (Assets->FindLegacySkeleton(mMeshPath) != nullptr) {
        mError = "This mesh uses an embedded skeleton, so its file cannot be saved safely. Set the physics asset on the component instead.";

        return;
    }

    const Core::FString Previous{mMesh->GetPhysicsAssetPath()};
    Core::FString Error{};

    mMesh->SetPhysicsAssetPath(mPhysicsAssetPath);

    if (!Assets->SaveSkeletalMesh(mMeshPath, *mMesh, Error)) {
        mMesh->SetPhysicsAssetPath(Previous);
        mError = Error.empty() ? "Cannot save the skeletal mesh." : Error;

        return;
    }

    mError.clear();
    mContext->AddLog("Assigned physics asset to the mesh: " + mPhysicsAssetPath);
}

void FPhysicsAssetEditorToolkit::SaveAsset() {
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (Component == nullptr || PhysicsAsset == nullptr) {
        mError = "There is no physics asset to save.";

        return;
    }

    // 이름을 바꿔도 같은 메시로 열리도록 미리보기 메시 경로를 에셋에 함께 저장한다.
    PhysicsAsset->mPreviewMeshPath = Core::FilePath::ToGenericUtf8(mMeshPath);

    const bool NewFile{mPhysicsAssetPath.empty()};
    const Core::FString TargetPath{NewFile ? Core::FilePath::ToGenericUtf8(PhysicsAssetEditing::GetDefaultPhysicsAssetPath(mMeshPath)) : mPhysicsAssetPath};
    Core::FString Error{};

    if (!mContext->GetProject()->GetAssets()->SavePhysicsAsset(Core::FilePath::FromUtf8(TargetPath), *PhysicsAsset, Error)) {
        mError = Error.empty() ? "Cannot save the physics asset." : Error;

        return;
    }

    mError.clear();
    mDirty = false;
    // 저장한 값이 새 기준이 된다. 다음 Details 표시 때 다시 만든다.
    mBaseline.reset();

    // 처음 저장하면 이후 저장이 같은 에셋 객체를 대상으로 하도록 저장한 파일을 미리보기에 연결한다.
    if (NewFile) {
        mPhysicsAssetPath = TargetPath;

        mFloorBody.reset();
        mSimulating = false;

        if (!Component->SetPhysicsAssetOverride(mPhysicsAssetPath)) {
            mError = "Saved, but cannot reload the physics asset: " + mPhysicsAssetPath;
        }
    }

    mContext->AddLog("Saved physics asset: " + TargetPath);
}

void FPhysicsAssetEditorToolkit::RebuildBoneTree() {
    mBoneChildren.clear();
    mRootBones.clear();
    ClearSelection();
    mGraphFocus.clear();

    if (mSkeleton == nullptr) {
        return;
    }

    const auto& Bones{mSkeleton->GetBones()};

    mBoneChildren.resize(Bones.size());

    for (Index_t Bone{}; Bone < Bones.size(); ++Bone) {
        const Int32 Parent{Bones[Bone].GetParentIndex()};

        if (Parent >= 0 && static_cast<Index_t>(Parent) < Bones.size()) {
            mBoneChildren[static_cast<Index_t>(Parent)].push_back(Bone);
        } else {
            mRootBones.push_back(Bone);
        }
    }
}

PhysicsRuntime::UPhysicsAsset* FPhysicsAssetEditorToolkit::GetPhysicsAsset() const {
    const Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};

    return Component != nullptr ? Component->GetPhysicsAsset() : nullptr;
}

void FPhysicsAssetEditorToolkit::StartSimulation() {
    mGrab.End();
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    Engine::UWorld* World{mContent != nullptr ? mContent->GetWorld() : nullptr};
    PhysicsRuntime::FPhysicsScene* Scene{World != nullptr ? World->GetPhysicsScene() : nullptr};

    if (Component == nullptr || Scene == nullptr) {
        return;
    }

    // 애니메이션 자세의 키네마틱 바디로 다시 만든 뒤 시뮬레이션으로 바꾼다.
    Component->ResetPose();
    Component->RecreatePhysicsState();
    // 바닥은 미리보기가 그리는 바닥과 같은 기준(메시의 바인드 범위)으로 만든다. 현재 포즈의 범위를 쓰면
    // 재시작할 때 이전 시뮬레이션에서 떨어진 포즈 아래에 바닥이 생긴다.
    mFloorBounds = mMesh->GetBounds();
    mFloorBody = PhysicsAssetEditing::CreateFloorBody(*Scene, mFloorBounds);
    Component->SetAllBodiesSimulatePhysics(true);
    mSimulating = true;
    mSimulationHalted = false;
    mSimulationWarning.clear();
    mSimulationStartPositions.clear();
}

void FPhysicsAssetEditorToolkit::StopSimulation() {
    mGrab.End();
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};

    mFloorBody.reset();
    mSimulating = false;
    mSimulationHalted = false;
    mSimulationWarning.clear();
    mSimulationStartPositions.clear();

    // 시뮬레이션이 바꾼 뼈 포즈를 기준 자세로 되돌린 뒤 키네마틱 바디를 다시 만든다.
    if (Component != nullptr) {
        Component->ResetPose();
        Component->RecreatePhysicsState();
    }
}

void FPhysicsAssetEditorToolkit::BuildBodyLines(Engine::FPrimitiveDrawInterface& Draw) const {
    const Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{Component != nullptr ? Component->GetPhysicsAsset() : nullptr};

    if (PhysicsAsset == nullptr) {
        return;
    }

    // 시뮬레이션 중에는 뼈 자세 대신 PhysX 바디의 현재 자세로 그린다.
    Core::TArray<PhysicsAssetEditing::FBodyPose> Poses{};

    if (mSimulating) {
        Engine::UWorld* World{mContent->GetWorld()};

        if (World != nullptr && World->GetPhysicsScene() != nullptr) {
            PhysicsAssetEditing::CollectBoneBodyPoses(*World->GetPhysicsScene(), Poses);
        }

        // 바닥은 윗면 사각형과 대각선으로 표시한다.
        FVector HalfExtent{};
        const FTransform Floor{PhysicsAssetEditing::GetFloorTransform(mFloorBounds, HalfExtent)};
        const FVector Top{Floor.mLocation + FVector{0.0f, 0.0f, HalfExtent.z}};
        const FVector Corners[4]{Top + FVector{-HalfExtent.x, -HalfExtent.y, 0.0f}, Top + FVector{HalfExtent.x, -HalfExtent.y, 0.0f}, Top + FVector{HalfExtent.x, HalfExtent.y, 0.0f}, Top + FVector{-HalfExtent.x, HalfExtent.y, 0.0f}};

        for (int Corner{}; Corner < 4; ++Corner) {
            Draw.DrawLine(Corners[Corner], Corners[(Corner + 1) % 4], FVector{0.5f, 0.5f, 0.5f}, false, 1.5f);
        }

        Draw.DrawLine(Corners[0], Corners[2], FVector{0.35f, 0.35f, 0.35f}, false, 1.0f);
        Draw.DrawLine(Corners[1], Corners[3], FVector{0.35f, 0.35f, 0.35f}, false, 1.0f);
    }

    for (Index_t Index{}; Index < PhysicsAsset->mSkeletalBodySetups.size(); ++Index) {
        const PhysicsRuntime::UBodySetup* Setup{PhysicsAsset->mSkeletalBodySetups[Index].get()};
        FTransform BoneWorld{};

        if (Setup == nullptr) {
            continue;
        }

        if (mSimulating) {
            const auto Found{std::find_if(Poses.begin(), Poses.end(), [Index](const PhysicsAssetEditing::FBodyPose& Pose) {
                return Pose.mBodyIndex == Index;
            })};

            if (Found == Poses.end()) {
                continue;
            }

            BoneWorld = Found->mWorld;
        } else {
            // 바디를 만들 때와 같은 뼈 월드 변환(스케일 포함)을 써서 실제 충돌 모양과 같은 크기로 그린다.
            const Index_t BoneIndex{Component->GetBoneIndex(Setup->GetBoneName())};

            if (BoneIndex == InvalidIndex || !Component->GetBoneWorldTransform(BoneIndex, BoneWorld)) {
                continue;
            }
        }

        const bool Selected{IsBodySelected(Index)};

        PhysicsAssetDrawing::DrawBodySetup(Draw, *Setup, BoneWorld, Selected ? FVector{1.0f, 0.65f, 0.15f} : FVector{0.35f, 0.6f, 1.0f}, Selected ? 2.5f : 1.2f);
    }
}

void FPhysicsAssetEditorToolkit::DrawHierarchy() {
    if (mSkeleton == nullptr || mContent == nullptr || !mContent->IsReady()) {
        ImGui::TextDisabled("No preview");

        return;
    }

    ImGui::Checkbox("Show Bodies", &mShowBodies);

    // 오른쪽 위의 설정 드롭다운.
    const float SettingsWidth{ImGui::CalcTextSize("Settings").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f};

    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - SettingsWidth));

    if (ImGui::BeginCombo("##HierarchySettings", "Settings", ImGuiComboFlags_WidthFitPreview)) {
        ImGui::Checkbox("Show All Bones", &mShowAllBones);
        ImGui::EndCombo();
    }

    ImGui::Separator();

    // Hierarchy 창에 포커스가 있을 때 Ctrl+A로 바디를 모두 선택한다.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

        if (PhysicsAsset != nullptr) {
            Core::TArray<Index_t> All{};

            for (Index_t Index{}; Index < PhysicsAsset->mSkeletalBodySetups.size(); ++Index) {
                if (PhysicsAsset->mSkeletalBodySetups[Index] != nullptr) {
                    All.push_back(Index);
                }
            }

            if (!All.empty()) {
                // 대표 바디는 기존 대표를 유지하고, 없으면 마지막 바디로 한다.
                const Index_t Primary{IsBodySelected(mSelectedBody) ? mSelectedBody : All.back()};

                SelectBody(Primary, PhysicsAssetEditing::FShapeRef{});
                mSelectedBodies = All;
                mGraphFocus = All;
            }
        }
    }

    // 깊은 본 이름이 잘리지 않도록 가로 스크롤을 두고, 들여쓰기는 스켈레탈 메시 뷰어의 본 트리와 같은 폭(기본의 1/3)으로 맞춘다.
    if (ImGui::BeginChild("##BoneTree", ImVec2{0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetStyle().IndentSpacing / 3.0f);

        for (const Index_t Root : mRootBones) {
            DrawBoneNode(Root);
        }

        ImGui::PopStyleVar();
    }

    ImGui::EndChild();
}

void FPhysicsAssetEditorToolkit::DrawBoneNode(Index_t Bone) {
    const auto& Bones{mSkeleton->GetBones()};
    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};
    const Core::FString& Name{Bones[Bone].GetName()};
    const Index_t BodyIndex{PhysicsAsset != nullptr ? PhysicsAsset->FindBodyIndex(Name) : InvalidIndex};
    const bool HasBody{BodyIndex != InvalidIndex};

    // 자손 중에 바디가 있는 뼈가 있는지 확인한다.
    const std::function<bool(Index_t)> HasBodyBelow{[&](Index_t Parent) {
        for (const Index_t Child : mBoneChildren[Parent]) {
            if ((PhysicsAsset != nullptr && PhysicsAsset->FindBodyIndex(Bones[Child].GetName()) != InvalidIndex) || HasBodyBelow(Child)) {
                return true;
            }
        }

        return false;
    }};

    // Show All Bones가 꺼져 있으면 바디가 없는 뼈는 표시하지 않고 그 자식들을 같은 깊이에 이어 붙인다.
    if (!mShowAllBones && !HasBody) {
        for (const Index_t Child : mBoneChildren[Bone]) {
            DrawBoneNode(Child);
        }

        return;
    }

    ImGuiTreeNodeFlags Flags{ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen};

    if (mShowAllBones ? mBoneChildren[Bone].empty() : !HasBodyBelow(Bone)) {
        Flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if ((mSelectedBone == Bone || (HasBody && IsBodySelected(BodyIndex))) && mSelectedConstraint == InvalidIndex) {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushID(static_cast<int>(Bone));

    const Core::FString Label{HasBody ? Name + "   [Body]" : Name};
    const bool Open{ImGui::TreeNodeEx("##Bone", Flags, "%s", Label.c_str())};

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        const ImGuiIO& Input{ImGui::GetIO()};

        if (HasBody) {
            // Shift나 Ctrl을 누르고 바디가 있는 뼈를 고르면 선택 목록에 넣거나 뺀다. 그래프는 선택한 모든 바디를 중심으로 바뀐다.
            SelectBody(BodyIndex, PhysicsAssetEditing::FShapeRef{}, true, Input.KeyShift || Input.KeyCtrl);
        } else {
            ClearSelection();
            mSelectedBone = Bone;
        }
    }

    if (Open) {
        for (const Index_t Child : mBoneChildren[Bone]) {
            DrawBoneNode(Child);
        }

        ImGui::TreePop();
    }

    ImGui::PopID();
}

Asset::USkeleton* FPhysicsAssetEditorToolkit::FindDefaultSkeleton(Core::FString& OutPath) const {
    return PhysicsAssetEditing::FindSkeletonForMesh(*mContext->GetProject()->GetAssets(), mMeshPath, OutPath);
}

void FPhysicsAssetEditorToolkit::DrawSkeletonPicker() {
    const char* Label{mSkeleton == nullptr ? "None" : mSkeletonPath.empty() ? "Embedded Skeleton" : mSkeletonPath.c_str()};

    if (!ImGui::BeginCombo("Skeleton", Label)) {
        return;
    }

    Asset::FAssetManager* Assets{mContext->GetProject()->GetAssets()};
    Core::FString Error{};
    Core::FString SelectedPath{};
    bool Selected{};

    for (const auto& Candidate : Assets->GetAssetPaths(Error)) {
        Asset::EAssetType Type{};

        if (Asset::FAssetFile::GetType(Assets->ResolvePath(Candidate, Error), Type, Error) && Type == Asset::EAssetType::Skeleton) {
            const Core::FString Relative{Core::FilePath::ToGenericUtf8(Candidate)};

            if (ImGui::Selectable(Relative.c_str(), Relative == mSkeletonPath)) {
                SelectedPath = Relative;
                Selected = true;
            }
        }
    }

    ImGui::EndCombo();

    if (!Selected || SelectedPath == mSkeletonPath) {
        return;
    }

    Error.clear();

    Asset::USkeleton* Skeleton{Assets->LoadSkeleton(Core::FilePath::FromUtf8(SelectedPath), Error)};

    if (Skeleton == nullptr) {
        mError = Error.empty() ? "Cannot load the skeleton." : Error;

        return;
    }

    if (BuildPreview(Skeleton)) {
        mSkeletonPath = std::move(SelectedPath);
    }
}

void FPhysicsAssetEditorToolkit::DrawViewport() {
    if (!GetTab().IsVisible()) {
        return;
    }

    if (mContent == nullptr || !mContent->IsReady()) {
        ImGui::TextWrapped("%s", mError.empty() ? "Select a skeleton in Details to preview this mesh." : mError.c_str());

        return;
    }

    if (EditorStyle::Button("Reset View")) {
        mNeedsFrame = true;
    }

    ImGui::SameLine();

    if (EditorStyle::Button(mSimulating ? "Stop" : "Simulate")) {
        if (mSimulating) {
            StopSimulation();
        } else {
            StartSimulation();
        }
    }

    if (mSimulating) {
        ImGui::SameLine();

        if (EditorStyle::Button("Restart")) {
            StartSimulation();
        }
    }

    ImGui::SameLine();

    if (EditorStyle::Button(mDirty ? "Save *" : "Save")) {
        SaveAsset();
    }

    DrawViewportToolbar();

    const ImVec2 Size{ImGui::GetContentRegionAvail()};

    if (Size.x < 1.0f || Size.y < 1.0f) {
        return;
    }

    if (!mViewer.Resize(static_cast<Uint32>(Size.x), static_cast<Uint32>(Size.y))) {
        ImGui::TextUnformatted("Cannot resize the physics asset viewport.");

        return;
    }

    ImGui::InvisibleButton("##PhysicsAssetViewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const bool Hovered{ImGui::IsItemHovered()};
    const ImGuiIO& Input{ImGui::GetIO()};
    Viewer::FViewerCamera& Camera{mViewer.GetCamera()};

    const bool FramePressed{Hovered && !Input.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)};

    if (FramePressed && mSimulating && mContent->GetWorld() != nullptr && mContent->GetWorld()->GetPhysicsScene() != nullptr) {
        // 시뮬레이션 중에는 메시가 기준 자세에서 멀리 있을 수 있으므로, 선택한 바디(없으면 움직이는 바디 전체)의 현재 위치에 카메라를 맞춘다.
        Core::TArray<PhysicsAssetEditing::FBodyPose> FramePoses{};

        PhysicsAssetEditing::CollectBoneBodyPoses(*mContent->GetWorld()->GetPhysicsScene(), FramePoses);

        FVector Minimum{};
        FVector Maximum{};
        bool HasPoint{};

        for (const PhysicsAssetEditing::FBodyPose& Pose : FramePoses) {
            const FVector& Location{Pose.mWorld.mLocation};

            if (!std::isfinite(Location.x) || !std::isfinite(Location.y) || !std::isfinite(Location.z)) {
                continue;
            }

            if (IsBodySelected(Pose.mBodyIndex) || mSelectedBodies.empty()) {
                Minimum = HasPoint ? FVector{std::min(Minimum.x, Location.x), std::min(Minimum.y, Location.y), std::min(Minimum.z, Location.z)} : Location;
                Maximum = HasPoint ? FVector{std::max(Maximum.x, Location.x), std::max(Maximum.y, Location.y), std::max(Maximum.z, Location.z)} : Location;
                HasPoint = true;
            }
        }

        if (HasPoint) {
            // 점 하나나 아주 작은 범위는 카메라가 너무 가까워지지 않도록 최소 크기를 준다.
            const FVector Margin{0.3f, 0.3f, 0.3f};
            FBox Bounds{};

            Bounds.mMinimum = Minimum - Margin;
            Bounds.mMaximum = Maximum + Margin;
            Camera.Fit(Bounds, {Size.x, Size.y}, 1.2f);
        }
    } else if (mNeedsFrame || FramePressed) {
        Camera = Viewer::FViewerCamera{};
        mNeedsFrame = !mViewer.Fit(1.15f);
    }

    const bool Flying{ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right)};

    // 기즈모: W는 이동, E는 회전. 핸들 위에서 누르면 기즈모가 마우스를 차지해 카메라 조작과 클릭 선택을 건너뛴다.
    const ImVec2 GizmoOrigin{ImGui::GetItemRectMin()};
    const Engine::FSceneView GizmoView{Camera.CalcSceneView({Size.x, Size.y})};
    const FVector2 GizmoMouse{Input.MousePos.x - GizmoOrigin.x, Input.MousePos.y - GizmoOrigin.y};

    if (Hovered && !Flying && !Input.WantTextInput && !mGizmo.IsDragging()) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            mGizmoMode = Editor::EGizmoMode::Translate;
        } else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            mGizmoMode = Editor::EGizmoMode::Rotate;
        }
    }

    const bool GizmoCaptured{UpdateGizmo(GizmoView, GizmoMouse, Hovered)};

    // 시뮬레이션 중 Ctrl + 우클릭을 누르고 있는 동안 광선이 맞은 바디를 마우스로 끈다.
    PhysicsRuntime::FPhysicsScene* GrabScene{mSimulating && mContent->GetWorld() != nullptr ? mContent->GetWorld()->GetPhysicsScene() : nullptr};

    if (GrabScene == nullptr) {
        mGrab.End();
    } else {
        FRay GrabRay{};
        const bool HasRay{GizmoView.Deproject(GizmoMouse, GrabRay)};

        if (HasRay && Hovered && Input.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            mGrab.Begin(*GrabScene, GrabRay, 1000.0f);
        }

        if (mGrab.IsActive()) {
            if (HasRay && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                mGrab.Update(GrabRay);
            } else {
                mGrab.End();
            }
        }
    }

    if (Flying && !mGrab.IsActive()) {
        // 오른쪽 버튼을 누른 동안은 제자리에서 시선을 돌리고 WASD로 이동한다 (E, Q는 위아래).
        Camera.LookAround({Input.MouseDelta.x, Input.MouseDelta.y});

        if (!Input.WantTextInput) {
            const FQuat Rotation{Camera.GetRotation().Quaternion()};
            FVector Move{};

            Move += ImGui::IsKeyDown(ImGuiKey_W) ? Rotation.ForwardVector() : FVector{};
            Move -= ImGui::IsKeyDown(ImGuiKey_S) ? Rotation.ForwardVector() : FVector{};
            Move += ImGui::IsKeyDown(ImGuiKey_D) ? Rotation.RightVector() : FVector{};
            Move -= ImGui::IsKeyDown(ImGuiKey_A) ? Rotation.RightVector() : FVector{};
            Move += ImGui::IsKeyDown(ImGuiKey_E) ? FVector{0.0f, 0.0f, 1.0f} : FVector{};
            Move -= ImGui::IsKeyDown(ImGuiKey_Q) ? FVector{0.0f, 0.0f, 1.0f} : FVector{};

            if (Move.LengthSquared() > 0.0f) {
                const float Speed{Camera.GetRadius() * mCameraSpeed * (Input.KeyShift ? 3.0f : 1.0f)};

                Camera.MoveEye(Move.Normalized() * Speed * Input.DeltaTime);
            }
        }
    } else if (ImGui::IsItemActive() && !GizmoCaptured && !mGizmoUsed) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            Camera.Pan({Input.MouseDelta.x, Input.MouseDelta.y}, Size.y);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            Camera.Orbit({Input.MouseDelta.x, Input.MouseDelta.y});
        }
    }

    // 왼쪽 버튼을 거의 움직이지 않고 떼면 클릭으로 보고 그 위치의 바디를 고른다.
    if (!mSimulating && Hovered && !GizmoCaptured && !mGizmoUsed && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
        PickAt({Input.MousePos.x - GizmoOrigin.x, Input.MousePos.y - GizmoOrigin.y}, Size);
    }

    // 기즈모에서 시작한 누름은 뗄 때까지 클릭 선택에서 제외한다.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        mGizmoUsed = false;
    }

    if (Hovered && Input.MouseWheel != 0.0f) {
        // 오른쪽 버튼을 누른 채 휠을 돌리면 카메라 속도를, 그냥 돌리면 확대와 축소를 조절한다.
        if (Flying) {
            mCameraSpeed = std::clamp(mCameraSpeed * std::pow(1.25f, Input.MouseWheel), 0.1f, 64.0f);
        } else {
            Camera.Zoom(Input.MouseWheel);
        }
    }

    const ImVec2 Origin{ImGui::GetItemRectMin()};
    const ImVec2 End{ImGui::GetItemRectMax()};
    ImDrawList* DrawList{ImGui::GetWindowDrawList()};

    // 선택과 무관한 고정 정보는 뷰포트 왼쪽 위에 겹쳐 표시한다. 렌더 결과 이미지를 그린 뒤에 그려야 이미지에 가려지지 않는다.
    const Engine::USkeletalMeshComponent* InfoComponent{mContent->GetComponent()};
    const PhysicsRuntime::UPhysicsAsset* InfoAsset{InfoComponent != nullptr ? InfoComponent->GetPhysicsAsset() : nullptr};
    Core::FString Source{"Source: Generated from the mesh (not saved)"};

    if (!mPhysicsAssetPath.empty()) {
        Source = "Source: File (" + mPhysicsAssetPath + ")" + (mDirty ? " - unsaved changes" : "");
    } else if (InfoComponent != nullptr && !InfoComponent->GetPhysicsAssetOverride().empty()) {
        Source = "Source: Override (" + InfoComponent->GetPhysicsAssetOverride() + ")";
    } else if (mMesh != nullptr && !mMesh->GetPhysicsAssetPath().empty()) {
        Source = "Source: Saved (" + mMesh->GetPhysicsAssetPath() + ")";
    }

    Core::TArray<Core::FString> InfoLines{
        "Camera speed x" + std::to_string(mCameraSpeed).substr(0, 4),
        Source,
        InfoAsset != nullptr ? "Bodies: " + std::to_string(InfoAsset->mSkeletalBodySetups.size()) + "   Constraints: " + std::to_string(InfoAsset->mConstraintSetup.size()) : Core::FString{"No physics asset"},
        Core::FString{"Physics scene: "} + (mContent->HasPhysicsScene() ? "Created" : "Not created")};
    constexpr ImU32 InfoWhite{IM_COL32(220, 225, 235, 255)};
    constexpr ImU32 InfoGreen{IM_COL32(120, 230, 140, 255)};
    constexpr ImU32 InfoYellow{IM_COL32(255, 215, 90, 255)};
    constexpr ImU32 InfoRed{IM_COL32(255, 110, 110, 255)};
    Core::TArray<ImU32> InfoColors(InfoLines.size(), InfoWhite);
    const auto AddInfo{[&](const Core::FString& Text, ImU32 Color) {
        InfoLines.push_back(Text);
        InfoColors.push_back(Color);
    }};

    // 물리는 보이는 탭에서만 고정 스텝 누적기를 통해 진행한다. 긴 프레임은 잘라 낸다. 불안정해져 멈춘 뒤에는 진행하지 않는다.
    if (mSimulating && mContent->GetWorld() != nullptr && !mSimulationHalted) {
        mContent->GetWorld()->StepPhysics(std::min(Input.DeltaTime * 1_s, 100_ms));

        // 월드 Tick이 돌지 않으므로 스텝 결과를 직접 뼈 포즈에 써서 메시가 바디를 따라가게 한다.
        if (Engine::USkeletalMeshComponent* Component{mContent->GetComponent()}) {
            Component->BlendInPhysics();
        }
    }

    // 시뮬레이션 상태를 한눈에 알 수 있게 표시한다: 전체 판정(색), Dynamic과 Kinematic 개수, 선택한 바디의 종류와 움직인 거리.
    // 불안정하면(NaN, 너무 먼 거리, 너무 빠른 속도) 그 자세에서 멈춰 원인을 볼 수 있게 한다.
    if (mSimulating && mContent->GetWorld() != nullptr && mContent->GetWorld()->GetPhysicsScene() != nullptr && InfoAsset != nullptr) {
        Core::TArray<PhysicsAssetEditing::FBodyPose> StatePoses{};

        PhysicsAssetEditing::CollectBoneBodyPoses(*mContent->GetWorld()->GetPhysicsScene(), StatePoses);

        constexpr float MaxSpeed{300.0f};
        constexpr float MaxDistance{1000.0f};
        Index_t DynamicCount{};
        Index_t KinematicCount{};
        float LargestMove{};
        Core::FString LargestMoveName{};
        Core::FString SelectedLine{};
        ImU32 SelectedColor{InfoWhite};

        for (const PhysicsAssetEditing::FBodyPose& Pose : StatePoses) {
            const FVector& Location{Pose.mWorld.mLocation};
            const bool HasSetup{Pose.mBodyIndex < InfoAsset->mSkeletalBodySetups.size() && InfoAsset->mSkeletalBodySetups[Pose.mBodyIndex] != nullptr};
            const Core::FString BodyName{HasSetup ? InfoAsset->mSkeletalBodySetups[Pose.mBodyIndex]->GetBoneName() : Core::FString{"?"}};
            const bool IsDynamic{HasSetup && InfoAsset->mSkeletalBodySetups[Pose.mBodyIndex]->ShouldSimulatePhysics()};
            const float Distance{Location.Length()};
            const float Speed{Pose.mLinearVelocity.Length()};

            ++(IsDynamic ? DynamicCount : KinematicCount);

            if (!mSimulationHalted && (!std::isfinite(Distance) || !std::isfinite(Speed) || Distance > MaxDistance || Speed > MaxSpeed)) {
                mSimulationHalted = true;
                mSimulationWarning = "'" + BodyName + "' " + (std::isfinite(Distance) && std::isfinite(Speed) ? "flew " + std::to_string(static_cast<int>(Distance)) + " m away at " + std::to_string(static_cast<int>(Speed)) + " m/s" : Core::FString{"became NaN"});
            }

            // 시작 직후의 위치를 기준으로 얼마나 움직였는지 계산한다. 기준은 처음 본 프레임의 위치다.
            if (mSimulationStartPositions.find(Pose.mBodyIndex) == mSimulationStartPositions.end() && std::isfinite(Distance)) {
                mSimulationStartPositions.emplace(Pose.mBodyIndex, Location);
            }

            const auto Start{mSimulationStartPositions.find(Pose.mBodyIndex)};
            const float Moved{Start != mSimulationStartPositions.end() && std::isfinite(Distance) ? (Location - Start->second).Length() : 0.0f};

            if (IsDynamic && std::isfinite(Moved) && Moved >= LargestMove) {
                LargestMove = Moved;
                LargestMoveName = BodyName;
            }

            if (Pose.mBodyIndex == mSelectedBody) {
                if (IsDynamic) {
                    SelectedLine = "Selected '" + BodyName + "': Dynamic  moved " + std::to_string(Moved).substr(0, 6) + " m  speed " + std::to_string(Speed).substr(0, 6) + " m/s";
                } else {
                    SelectedLine = "Selected '" + BodyName + "': Kinematic - it follows the animation pose and will not move. Set Physics Type to Dynamic to simulate it.";
                    SelectedColor = InfoYellow;
                }
            }
        }

        // 전체 판정: 불안정(빨강), 움직일 바디가 없음(노랑), 정상 진행(초록)
        if (mSimulationHalted) {
            AddInfo("UNSTABLE - simulation paused: " + mSimulationWarning, InfoRed);
            AddInfo("Press F to find it, then Stop or Restart. Check overlapping bodies (Collision Pairs) and constraint limits.", InfoRed);
        } else if (DynamicCount == 0) {
            AddInfo("Nothing to simulate: all " + std::to_string(KinematicCount) + " bodies are Kinematic. Set Physics Type to Dynamic on the bodies you want to move.", InfoYellow);
        } else {
            AddInfo("Simulation running: " + std::to_string(DynamicCount) + " Dynamic, " + std::to_string(KinematicCount) + " Kinematic. Stable.", InfoGreen);

            if (!LargestMoveName.empty()) {
                AddInfo("Largest movement from start: '" + LargestMoveName + "' " + std::to_string(LargestMove).substr(0, 6) + " m", InfoWhite);
            }
        }

        if (!SelectedLine.empty()) {
            AddInfo(SelectedLine, SelectedColor);
        } else if (mSelectedBody != InvalidIndex) {
            AddInfo("Selected body has no active PhysX actor (it was not created).", InfoYellow);
        }
    }

    Engine::FPrimitiveDrawInterface BodyLines{};

    if (mShowBodies) {
        BuildBodyLines(BodyLines);
    }

    BuildConstraintLines(BodyLines);

    // 잡고 있는 바디는 잡은 점에서 목표점까지 선으로 표시한다.
    FVector GrabPoint{};
    FVector GrabTarget{};

    if (mGrab.GetEndpoints(GrabPoint, GrabTarget)) {
        BodyLines.DrawLine(GrabPoint, GrabTarget, FVector{1.0f, 0.9f, 0.2f}, false, 2.5f);
    }

    // 기즈모 핸들은 현재 선택에서 매 프레임 만든다. 드래그 중에는 시작 시점의 축을 쓴다.
    Editor::FGizmoPlacement GizmoPlacement{};

    if (!mSimulating && GetGizmoPlacement(GizmoPlacement)) {
        mGizmo.BuildHandles(GizmoView, GizmoPlacement, BodyLines);
        mGizmo.DrawFeedback(BodyLines);
    }

    mViewer.SetViewMode(mViewMode);

    if (!mViewer.Render(&BodyLines)) {
        DrawList->AddText(Origin, ImGui::GetColorU32(ImGuiCol_Text), "Cannot render the physics asset viewport.");

        return;
    }

    const ImTextureRef Texture{static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mViewer.GetNativeTexture()))};
    DrawList->AddImage(Texture, Origin, End);

    // 밝은 장면 위에서도 읽히도록 반투명한 배경 위에 그린다.
    float InfoWidth{};

    for (const Core::FString& Line : InfoLines) {
        InfoWidth = std::max(InfoWidth, ImGui::CalcTextSize(Line.c_str()).x);
    }

    const float LineHeight{ImGui::GetTextLineHeightWithSpacing()};

    DrawList->AddRectFilled({Origin.x + 6.0f, Origin.y + 6.0f}, {Origin.x + 18.0f + InfoWidth, Origin.y + 14.0f + LineHeight * static_cast<float>(InfoLines.size())}, IM_COL32(0, 0, 0, 120), 4.0f);

    float InfoY{Origin.y + 10.0f};

    for (Index_t Index{}; Index < InfoLines.size(); ++Index) {
        DrawList->AddText({Origin.x + 12.0f, InfoY}, Index < InfoColors.size() ? InfoColors[Index] : InfoWhite, InfoLines[Index].c_str());
        InfoY += LineHeight;
    }

    DrawList->AddText({Origin.x + 12.0f, End.y - ImGui::GetTextLineHeight() - 10.0f}, IM_COL32(220, 225, 235, 255), "LMB: orbit | MMB: pan | Wheel: zoom | RMB + WASD/QE: fly | RMB + Wheel: speed | Shift: fast | F: frame | W/E: move/rotate | Shift/Ctrl + click: multi-select");
}

void FPhysicsAssetEditorToolkit::DrawDetails() {
    ImGui::TextUnformatted(Core::FilePath::ToGenericUtf8(GetAssetPath()).c_str());
    ImGui::Separator();

    DrawSkeletonPicker();

    if (!mError.empty()) {
        ImGui::TextWrapped("%s", mError.c_str());
    }

    if (mContent == nullptr || !mContent->IsReady()) {
        return;
    }

    const Engine::USkeletalMeshComponent* Component{mContent->GetComponent()};

    if (Component == nullptr) {
        return;
    }

    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{Component->GetPhysicsAsset()};

    // 편집이 일어나기 전에 되돌릴 기준을 만들어 둔다.
    EnsureBaseline();

    // 저장한 파일이 아직 메시의 기본 물리 에셋이 아니면 지정할 수 있다. 저장하지 않은 수정이 있으면 먼저 저장해야 한다.
    if (!mPhysicsAssetPath.empty() && mMesh != nullptr && mMesh->GetPhysicsAssetPath() != mPhysicsAssetPath) {
        ImGui::BeginDisabled(mDirty);

        if (ImGui::Button("Assign To Mesh")) {
            AssignToMesh();
        }

        ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Selection");

    // 관절을 고르면 관절 설정만, 바디나 뼈를 고르면 바디 설정만 보여 준다.
    const bool ConstraintSelected{PhysicsAsset != nullptr && mSelectedConstraint != InvalidIndex && mSelectedConstraint < PhysicsAsset->mConstraintSetup.size() && PhysicsAsset->mConstraintSetup[mSelectedConstraint] != nullptr};

    if (ConstraintSelected) {
        const PhysicsRuntime::FConstraintInstance& Instance{PhysicsAsset->mConstraintSetup[mSelectedConstraint]->mDefaultInstance};

        ImGui::Text("Constraint: %s -> %s", Instance.mConstraintBone1.c_str(), Instance.mConstraintBone2.c_str());
        DrawConstraintEditor();
    } else {
        if (mSelectedBodies.size() > 1) {
            ImGui::Text("%zu bodies selected", mSelectedBodies.size());
        } else if (mSelectedBone != InvalidIndex && mSkeleton != nullptr) {
            ImGui::Text("Bone: %s", mSkeleton->GetBones()[mSelectedBone].GetName().c_str());
        } else {
            ImGui::TextDisabled("Nothing selected");
        }

        mFrameRotationConstraint = InvalidIndex;
        DrawBodyEditor();
        DrawCollisionPairs();
    }

    ImGui::Separator();
    DrawRegenerate();
}

void FPhysicsAssetEditorToolkit::DrawConstraintEditor() {
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};

    if (PhysicsAsset == nullptr || Component == nullptr || mSkeleton == nullptr) {
        return;
    }

    const bool HasConstraint{mSelectedConstraint != InvalidIndex && mSelectedConstraint < PhysicsAsset->mConstraintSetup.size() && PhysicsAsset->mConstraintSetup[mSelectedConstraint] != nullptr};

    if (!HasConstraint) {
        mFrameRotationConstraint = InvalidIndex;

        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Constraint");

    PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset->mConstraintSetup[mSelectedConstraint]->mDefaultInstance};
    bool Changed{};

    ImGui::Text("%s -> %s", Constraint.mConstraintBone1.c_str(), Constraint.mConstraintBone2.c_str());

    if (ImGui::Button("Delete Constraint")) {
        Component->DestroyPhysicsState();
        PhysicsAssetEditing::DeleteConstraint(*PhysicsAsset, mSelectedConstraint);
        mSelectedConstraint = InvalidIndex;
        ApplyEdit();

        return;
    }

    // 되돌릴 기준: 마지막으로 저장하거나 연 시점의 같은 관절이며, 그때 없던 관절은 FConstraintInstance의 기본값이다.
    const PhysicsRuntime::FConstraintInstance TypeDefaults{};
    const PhysicsRuntime::FConstraintInstance* const Baseline{FindBaselineConstraint(Constraint.mConstraintBone1, Constraint.mConstraintBone2)};
    const PhysicsRuntime::FConstraintInstance& Defaults{Baseline != nullptr ? *Baseline : TypeDefaults};

    // 회전 축마다 Free/Limited/Locked를 고르고 Limited일 때만 한계 각도를 보여 준다. 값마다 기준으로 되돌리는 화살표가 있다.
    const auto EditAngular{[&Changed](const char* Label, PhysicsRuntime::EAngularConstraintMotion& Motion, Core::FAngle& Limit, PhysicsRuntime::EAngularConstraintMotion DefaultMotion, Core::FAngle DefaultLimit) {
        int Mode{static_cast<int>(Motion)};

        ImGui::PushID(Label);

        if (ImGui::Combo(Label, &Mode, "Free\0Limited\0Locked\0")) {
            Motion = static_cast<PhysicsRuntime::EAngularConstraintMotion>(Mode);
            Changed = true;
        }

        if (ResetButton("Motion", Motion != DefaultMotion)) {
            Motion = DefaultMotion;
            Changed = true;
        }

        if (Motion == PhysicsRuntime::EAngularConstraintMotion::Limited) {
            float LimitDegrees{Core::Math::ToDegrees(Limit)};

            if (ImGui::DragFloat("Limit (deg)", &LimitDegrees, 0.5f, 0.0f, 180.0f, "%.1f")) {
                Limit = LimitDegrees * 1_deg;
                Changed = true;
            }

            if (ResetButton("Limit", Limit != DefaultLimit)) {
                Limit = DefaultLimit;
                Changed = true;
            }
        }

        ImGui::PopID();
    }};

    // 이동은 Free와 Locked만 지원한다. Limited는 엔진이 Locked로 취급한다.
    const auto EditLinear{[&Changed](const char* Label, PhysicsRuntime::ELinearConstraintMotion& Motion, PhysicsRuntime::ELinearConstraintMotion DefaultMotion) {
        int Mode{Motion == PhysicsRuntime::ELinearConstraintMotion::Free ? 0 : 1};
        const bool DefaultFree{DefaultMotion == PhysicsRuntime::ELinearConstraintMotion::Free};

        if (ImGui::Combo(Label, &Mode, "Free\0Locked\0")) {
            Motion = Mode == 0 ? PhysicsRuntime::ELinearConstraintMotion::Free : PhysicsRuntime::ELinearConstraintMotion::Locked;
            Changed = true;
        }

        if (ResetButton(Label, (Mode == 0) != DefaultFree)) {
            Motion = DefaultFree ? PhysicsRuntime::ELinearConstraintMotion::Free : PhysicsRuntime::ELinearConstraintMotion::Locked;
            Changed = true;
        }
    }};

    // 선택한 관절이 바뀌면 현재 축에서 프레임 회전을 한 번 구해 둔다.
    if (mFrameRotationConstraint != mSelectedConstraint) {
        mFrameRotation[0] = AxesToRotator(Constraint.mPriAxis1, Constraint.mSecAxis1);
        mFrameRotation[1] = AxesToRotator(Constraint.mPriAxis2, Constraint.mSecAxis2);
        mFrameRotationConstraint = mSelectedConstraint;
    }

    const auto EditPosition{[&Changed](const char* Label, FVector& Value, const FVector& Default) {
        float Fields[3]{Value.x, Value.y, Value.z};

        if (ImGui::DragFloat3(Label, Fields, 0.001f, -100.0f, 100.0f, "%.4f")) {
            Value = FVector{Fields[0], Fields[1], Fields[2]};
            Changed = true;
        }

        if (ResetButton(Label, !NearlyEqual(Value, Default))) {
            Value = Default;
            Changed = true;
        }
    }};

    // 회전에서 주 축(X)과 보조 축(Y)을 만들므로 두 축은 항상 직교하는 단위 벡터가 된다.
    const auto EditRotation{[&Changed](FRotator& Rotation, FVector& Primary, FVector& Secondary, const FVector& DefaultPrimary, const FVector& DefaultSecondary) {
        float Fields[3]{Rotation.pitch, Rotation.yaw, Rotation.roll};

        if (ImGui::DragFloat3("Rotation (P/Y/R)", Fields, 0.5f, -360.0f, 360.0f, "%.1f")) {
            Rotation = FRotator{Fields[0], Fields[1], Fields[2]};

            const FQuat Quaternion{Rotation.Quaternion()};

            Primary = Quaternion.RotateVector(FVector{1.0f, 0.0f, 0.0f});
            Secondary = Quaternion.RotateVector(FVector{0.0f, 1.0f, 0.0f});
            Changed = true;
        }

        // 회전 필드는 축에서 구한 값이므로 기준과는 두 축으로 비교하고, 되돌리면 회전 필드도 다시 구한다.
        if (ResetButton("Rotation (P/Y/R)", !NearlyEqual(Primary, DefaultPrimary) || !NearlyEqual(Secondary, DefaultSecondary))) {
            Primary = DefaultPrimary;
            Secondary = DefaultSecondary;
            Rotation = AxesToRotator(Primary, Secondary);
            Changed = true;
        }
    }};

    ImGui::TextUnformatted("Angular");
    EditAngular("Swing 1", Constraint.mSwing1Motion, Constraint.mSwing1Limit, Defaults.mSwing1Motion, Defaults.mSwing1Limit);
    EditAngular("Swing 2", Constraint.mSwing2Motion, Constraint.mSwing2Limit, Defaults.mSwing2Motion, Defaults.mSwing2Limit);
    EditAngular("Twist", Constraint.mTwistMotion, Constraint.mTwistLimit, Defaults.mTwistMotion, Defaults.mTwistLimit);

    ImGui::TextUnformatted("Linear");
    EditLinear("Linear X", Constraint.mLinearXMotion, Defaults.mLinearXMotion);
    EditLinear("Linear Y", Constraint.mLinearYMotion, Defaults.mLinearYMotion);
    EditLinear("Linear Z", Constraint.mLinearZMotion, Defaults.mLinearZMotion);

    ImGui::TextUnformatted("Frame 1 (child bone)");
    ImGui::PushID("Frame1");
    EditPosition("Position", Constraint.mPos1, Defaults.mPos1);
    EditRotation(mFrameRotation[0], Constraint.mPriAxis1, Constraint.mSecAxis1, Defaults.mPriAxis1, Defaults.mSecAxis1);
    ImGui::PopID();

    ImGui::TextUnformatted("Frame 2 (parent bone)");
    ImGui::PushID("Frame2");
    EditPosition("Position", Constraint.mPos2, Defaults.mPos2);
    EditRotation(mFrameRotation[1], Constraint.mPriAxis2, Constraint.mSecAxis2, Defaults.mPriAxis2, Defaults.mSecAxis2);
    ImGui::PopID();

    if (Changed) {
        ApplyEdit();
    }
}

void FPhysicsAssetEditorToolkit::BuildConstraintLines(Engine::FPrimitiveDrawInterface& Draw) const {
    const Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (Component == nullptr || PhysicsAsset == nullptr) {
        return;
    }

    constexpr float MaxLimitRadians{(179_deg).GetBase()};
    constexpr float FullTurnRadians{(1_turn).GetBase()};

    // 뼈의 월드 변환에 프레임(위치, 주 축, 보조 축)을 얹어 원점과 X, Y, Z축을 구한다.
    struct FWorldFrame {
        FVector mOrigin{};
        FVector mAxisX{};
        FVector mAxisY{};
        FVector mAxisZ{};
    };

    const auto MakeFrame{[&](const Core::FString& BoneName, const FVector& Position, const FVector& Primary, const FVector& Secondary, FWorldFrame& Out) {
        FTransform BoneWorld{};
        const Index_t BoneIndex{Component->GetBoneIndex(BoneName)};

        if (BoneIndex == InvalidIndex || !Component->GetBoneWorldTransform(BoneIndex, BoneWorld)) {
            return false;
        }

        Out.mOrigin = BoneWorld.TransformPosition(Position);
        Out.mAxisX = BoneWorld.TransformVectorNoScale(Primary).Normalized();
        Out.mAxisY = BoneWorld.TransformVectorNoScale(Secondary).Normalized();
        Out.mAxisZ = Out.mAxisX.Cross(Out.mAxisY).Normalized();

        return true;
    }};

    // 프레임 하나를 X(빨강), Y(초록), Z(파랑) 축으로 그린다.
    const auto DrawFrame{[&Draw](const FWorldFrame& Frame, float Length, float Brightness, float Width) {
        Draw.DrawLine(Frame.mOrigin, Frame.mOrigin + Frame.mAxisX * Length, FVector{Brightness, 0.1f, 0.1f}, false, Width);
        Draw.DrawLine(Frame.mOrigin, Frame.mOrigin + Frame.mAxisY * Length, FVector{0.1f, Brightness, 0.1f}, false, Width);
        Draw.DrawLine(Frame.mOrigin, Frame.mOrigin + Frame.mAxisZ * Length, FVector{0.1f, 0.1f, Brightness}, false, Width);
    }};

    // 관절은 선택하지 않아도 모두 작게 표시하고, 선택한 관절은 크게 그려 구분한다.
    for (Index_t Index{}; Index < PhysicsAsset->mConstraintSetup.size(); ++Index) {
        if (PhysicsAsset->mConstraintSetup[Index] == nullptr || Index == mSelectedConstraint) {
            continue;
        }

        const PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset->mConstraintSetup[Index]->mDefaultInstance};
        FWorldFrame Child{};
        FWorldFrame Parent{};

        if (MakeFrame(Constraint.mConstraintBone1, Constraint.mPos1, Constraint.mPriAxis1, Constraint.mSecAxis1, Child)) {
            DrawFrame(Child, 0.02f, 0.8f, 1.5f);
        }

        if (MakeFrame(Constraint.mConstraintBone2, Constraint.mPos2, Constraint.mPriAxis2, Constraint.mSecAxis2, Parent)) {
            DrawFrame(Parent, 0.02f, 0.45f, 1.5f);
        }
    }

    if (mSelectedConstraint >= PhysicsAsset->mConstraintSetup.size() || PhysicsAsset->mConstraintSetup[mSelectedConstraint] == nullptr) {
        return;
    }

    const PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset->mConstraintSetup[mSelectedConstraint]->mDefaultInstance};
    FWorldFrame Child{};
    FWorldFrame Parent{};

    if (MakeFrame(Constraint.mConstraintBone1, Constraint.mPos1, Constraint.mPriAxis1, Constraint.mSecAxis1, Child)) {
        DrawFrame(Child, 0.08f, 1.0f, 3.0f);
    }

    if (!MakeFrame(Constraint.mConstraintBone2, Constraint.mPos2, Constraint.mPriAxis2, Constraint.mSecAxis2, Parent)) {
        return;
    }

    DrawFrame(Parent, 0.08f, 0.55f, 3.0f);

    // 한계 각도는 부모 쪽 프레임(Frame 2) 기준이다. Locked는 0도, Free는 거의 180도로 그린다.
    const auto LimitRadians{[MaxLimitRadians](PhysicsRuntime::EAngularConstraintMotion Motion, Core::FAngle Limit) {
        switch (Motion) {
            case PhysicsRuntime::EAngularConstraintMotion::Locked:
                return 0.0f;
            case PhysicsRuntime::EAngularConstraintMotion::Free:
                return MaxLimitRadians;
            default:
                return std::clamp(Limit.GetBase(), 0.0f, MaxLimitRadians);
        }
    }};

    constexpr int Segments{32};
    const FVector ConeColor{0.9f, 0.15f, 0.15f};
    const FVector TwistColor{0.15f, 0.9f, 0.15f};

    // Swing 1, 2: 프레임 X축이 움직일 수 있는 범위를 빨간 원뿔로 그린다. Swing 2는 Y 방향, Swing 1은 Z 방향의 한계이다.
    const float SwingY{LimitRadians(Constraint.mSwing2Motion, Constraint.mSwing2Limit)};
    const float SwingZ{LimitRadians(Constraint.mSwing1Motion, Constraint.mSwing1Limit)};

    if (SwingY > 0.0f || SwingZ > 0.0f) {
        constexpr float ConeLength{0.12f};
        const float SafeY{std::max(SwingY, 0.001f)};
        const float SafeZ{std::max(SwingZ, 0.001f)};
        FVector Previous{};

        for (int Step{}; Step <= Segments; ++Step) {
            const float Phi{FullTurnRadians * static_cast<float>(Step) / static_cast<float>(Segments)};
            const float Cos{std::cos(Phi)};
            const float Sin{std::sin(Phi)};
            // 방향마다 타원 경계까지의 각도를 구한다.
            const float Theta{std::min(1.0f / std::sqrt((Cos / SafeY) * (Cos / SafeY) + (Sin / SafeZ) * (Sin / SafeZ)), MaxLimitRadians)};
            const FVector Direction{Parent.mAxisX * std::cos(Theta) + (Parent.mAxisY * Cos + Parent.mAxisZ * Sin) * std::sin(Theta)};
            const FVector Point{Parent.mOrigin + Direction * ConeLength};

            if (Step > 0) {
                Draw.DrawLine(Previous, Point, ConeColor, false, 2.0f);
            }

            if (Step % (Segments / 8) == 0) {
                Draw.DrawLine(Parent.mOrigin, Point, ConeColor, false, 1.0f);
            }

            Previous = Point;
        }
    }

    // Twist: X축 둘레로 Y축이 돌 수 있는 범위(-한계 ~ +한계)를 초록 부채꼴로 그린다.
    const float Twist{LimitRadians(Constraint.mTwistMotion, Constraint.mTwistLimit)};

    if (Twist > 0.0f) {
        constexpr float TwistRadius{0.09f};
        FVector Previous{};

        for (int Step{}; Step <= Segments; ++Step) {
            const float Angle{-Twist + 2.0f * Twist * static_cast<float>(Step) / static_cast<float>(Segments)};
            const FVector Point{Parent.mOrigin + (Parent.mAxisY * std::cos(Angle) + Parent.mAxisZ * std::sin(Angle)) * TwistRadius};

            if (Step > 0) {
                Draw.DrawLine(Previous, Point, TwistColor, false, 2.0f);
            }

            if (Step == 0 || Step == Segments || Step % (Segments / 4) == 0) {
                Draw.DrawLine(Parent.mOrigin, Point, TwistColor, false, 1.0f);
            }

            Previous = Point;
        }
    }
}

void FPhysicsAssetEditorToolkit::ClearSelection() {
    mSelectedBone = InvalidIndex;
    mSelectedBody = InvalidIndex;
    mSelectedBodies.clear();
    mSelectedShape = PhysicsAssetEditing::FShapeRef{};
    mSelectedConstraint = InvalidIndex;
}

bool FPhysicsAssetEditorToolkit::IsBodySelected(Index_t BodyIndex) const {
    return std::find(mSelectedBodies.begin(), mSelectedBodies.end(), BodyIndex) != mSelectedBodies.end();
}

void FPhysicsAssetEditorToolkit::SelectBody(Index_t BodyIndex, const PhysicsAssetEditing::FShapeRef& Shape, bool FocusGraph, bool Toggle) {
    const Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (Toggle && IsBodySelected(BodyIndex)) {
        // 이미 선택한 바디를 다시 누르면 목록에서 뺀다. 대표 바디였으면 남은 마지막 바디가 대표가 된다.
        mSelectedBodies.erase(std::find(mSelectedBodies.begin(), mSelectedBodies.end(), BodyIndex));

        if (mSelectedBody == BodyIndex) {
            mSelectedBody = mSelectedBodies.empty() ? InvalidIndex : mSelectedBodies.back();
        }
    } else if (Toggle) {
        mSelectedBodies.push_back(BodyIndex);
        mSelectedBody = BodyIndex;
    } else {
        mSelectedBodies = {BodyIndex};
        mSelectedBody = BodyIndex;
        mSelectedShape = Shape;
    }

    mSelectedConstraint = InvalidIndex;
    mSelectedBone = InvalidIndex;

    if (Component != nullptr && PhysicsAsset != nullptr && mSelectedBody < PhysicsAsset->mSkeletalBodySetups.size() && PhysicsAsset->mSkeletalBodySetups[mSelectedBody] != nullptr) {
        mSelectedBone = Component->GetBoneIndex(PhysicsAsset->mSkeletalBodySetups[mSelectedBody]->GetBoneName());
    }

    if (FocusGraph) {
        mGraphFocus = mSelectedBodies;
    }
}

void FPhysicsAssetEditorToolkit::ApplyEdit() {
    mGrab.End();
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};

    mDirty = true;

    // 편집하면 시뮬레이션 상태가 무효가 되므로 바닥을 치우고 키네마틱 바디로 다시 만든다.
    mFloorBody.reset();
    mSimulating = false;

    // 바디는 생성 시점의 설명서로 만들어지므로 편집 후에는 물리 상태를 다시 만들어 반영한다.
    if (Component != nullptr) {
        Component->RecreatePhysicsState();
    }
}

bool FPhysicsAssetEditorToolkit::DrawShapeFields(const Core::TArray<PhysicsRuntime::UBodySetup*>& Setups, const PhysicsAssetEditing::FShapeRef& Shape) {
    // 항목 크기의 하한이며 단위는 m입니다. 0이면 PhysX 형상 생성이 실패한다.
    constexpr float MinSize{0.005f};
    constexpr float MaxValue{100.0f};

    // 항목이 있는 바디의 항목 주소만 모은다. 같은 종류와 번호의 항목끼리 값을 비교하고 함께 수정한다.
    Core::TArray<PhysicsRuntime::FKSphereElem*> Spheres{};
    Core::TArray<PhysicsRuntime::FKBoxElem*> Boxes{};
    Core::TArray<PhysicsRuntime::FKSphylElem*> Sphyls{};

    // 항목마다 되돌릴 기준값. 마지막으로 저장한(또는 연) 시점의 같은 뼈, 같은 번호 항목이며, 그때 없던 항목은 형식의 기본값이다.
    Core::TArray<PhysicsRuntime::FKSphereElem> SphereDefaults{};
    Core::TArray<PhysicsRuntime::FKBoxElem> BoxDefaults{};
    Core::TArray<PhysicsRuntime::FKSphylElem> SphylDefaults{};

    const auto BaselineOf{[&Shape](const auto* Source) {  // Source는 기준 바디의 형상 목록이며 nullptr이면 기본값을 돌려준다.
        using TElem = typename std::decay_t<decltype(*Source)>::value_type;

        return Source != nullptr && Shape.mIndex < Source->size() ? (*Source)[Shape.mIndex] : TElem{};
    }};

    for (PhysicsRuntime::UBodySetup* Setup : Setups) {
        if (Shape.mIndex >= PhysicsAssetEditing::GetShapeCount(*Setup, Shape.mType)) {
            continue;
        }

        PhysicsRuntime::FKAggregateGeom& Geometry{Setup->GetAggGeom()};
        const FBaselineBody* Baseline{FindBaselineBody(Setup->GetBoneName())};

        switch (Shape.mType) {
            case PhysicsAssetEditing::EShapeType::Sphere:
                Spheres.push_back(&Geometry.mSphereElems[Shape.mIndex]);
                SphereDefaults.push_back(BaselineOf(Baseline != nullptr ? &Baseline->mSpheres : nullptr));
                break;
            case PhysicsAssetEditing::EShapeType::Box:
                Boxes.push_back(&Geometry.mBoxElems[Shape.mIndex]);
                BoxDefaults.push_back(BaselineOf(Baseline != nullptr ? &Baseline->mBoxes : nullptr));
                break;
            case PhysicsAssetEditing::EShapeType::Sphyl:
                Sphyls.push_back(&Geometry.mSphylElems[Shape.mIndex]);
                SphylDefaults.push_back(BaselineOf(Baseline != nullptr ? &Baseline->mSphyls : nullptr));
                break;
            default:
                break;
        }
    }

    bool Changed{};

    switch (Shape.mType) {
        case PhysicsAssetEditing::EShapeType::Sphere:
            if (!Spheres.empty()) {
                Changed |= EditVectorMember("Center", Spheres, SphereDefaults, &PhysicsRuntime::FKSphereElem::mCenter, 0.001f, -MaxValue, MaxValue, "%.4f");
                Changed |= EditLengthMember("Radius", Spheres, SphereDefaults, &PhysicsRuntime::FKSphereElem::mRadius, 0.001f, MinSize, MaxValue);
            }

            break;
        case PhysicsAssetEditing::EShapeType::Box:
            if (!Boxes.empty()) {
                Changed |= EditVectorMember("Center", Boxes, BoxDefaults, &PhysicsRuntime::FKBoxElem::mCenter, 0.001f, -MaxValue, MaxValue, "%.4f");
                Changed |= EditVectorMember("Rotation (P/Y/R)", Boxes, BoxDefaults, &PhysicsRuntime::FKBoxElem::mRotation, 0.5f, -360.0f, 360.0f, "%.1f");
                Changed |= EditVectorMember("Half Extent", Boxes, BoxDefaults, &PhysicsRuntime::FKBoxElem::mHalfExtent, 0.001f, MinSize, MaxValue, "%.4f");
            }

            break;
        case PhysicsAssetEditing::EShapeType::Sphyl:
            if (!Sphyls.empty()) {
                Changed |= EditVectorMember("Center", Sphyls, SphylDefaults, &PhysicsRuntime::FKSphylElem::mCenter, 0.001f, -MaxValue, MaxValue, "%.4f");
                Changed |= EditVectorMember("Rotation (P/Y/R)", Sphyls, SphylDefaults, &PhysicsRuntime::FKSphylElem::mRotation, 0.5f, -360.0f, 360.0f, "%.1f");
                Changed |= EditLengthMember("Radius", Sphyls, SphylDefaults, &PhysicsRuntime::FKSphylElem::mRadius, 0.001f, MinSize, MaxValue);
                Changed |= EditLengthMember("Length", Sphyls, SphylDefaults, &PhysicsRuntime::FKSphylElem::mLength, 0.001f, MinSize, MaxValue);
            }

            break;
        default:
            ImGui::TextDisabled("Convex shapes cannot be edited here.");
            break;
    }

    return Changed;
}

void FPhysicsAssetEditorToolkit::DrawCollisionPairs() {
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};

    if (PhysicsAsset == nullptr || Component == nullptr || mSkeleton == nullptr || PhysicsAsset->mSkeletalBodySetups.empty()) {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Collision Pairs");
    ImGui::TextDisabled("%zu pair(s) with collision disabled. Bodies joined by a constraint never collide.", PhysicsAsset->mCollisionDisabledPairs.size());

    // 선택한 바디의 뼈 이름들
    Core::TArray<Core::FString> SelectedBones{};

    for (const Index_t Index : mSelectedBodies) {
        if (Index < PhysicsAsset->mSkeletalBodySetups.size() && PhysicsAsset->mSkeletalBodySetups[Index] != nullptr) {
            SelectedBones.push_back(PhysicsAsset->mSkeletalBodySetups[Index]->GetBoneName());
        }
    }

    if (SelectedBones.size() == 1) {
        const Core::FString& Bone{SelectedBones.front()};
        Core::FString ToEnable{};

        // 이 바디와 충돌을 끈 상대 목록
        for (const auto& Pair : PhysicsAsset->mCollisionDisabledPairs) {
            const Core::FString* Partner{Pair.first == Bone ? &Pair.second : Pair.second == Bone ? &Pair.first : nullptr};

            if (Partner == nullptr) {
                continue;
            }

            ImGui::PushID(Partner->c_str());
            ImGui::TextUnformatted(Partner->c_str());
            ImGui::SameLine();

            if (ImGui::SmallButton("Enable collision")) {
                ToEnable = *Partner;
            }

            ImGui::PopID();
        }

        if (!ToEnable.empty()) {
            Component->DestroyPhysicsState();
            PhysicsAsset->SetCollisionDisabled(Bone, ToEnable, false);
            ApplyEdit();
        }

        // 충돌을 끌 상대 추가
        if (ImGui::BeginCombo("Disable collision with", "Select a body...")) {
            for (const auto& Other : PhysicsAsset->mSkeletalBodySetups) {
                if (Other == nullptr || Other->GetBoneName() == Bone || PhysicsAsset->IsCollisionDisabled(Bone, Other->GetBoneName())) {
                    continue;
                }

                if (ImGui::Selectable(Other->GetBoneName().c_str())) {
                    Component->DestroyPhysicsState();
                    PhysicsAsset->SetCollisionDisabled(Bone, Other->GetBoneName(), true);
                    ApplyEdit();
                }
            }

            ImGui::EndCombo();
        }
    } else if (SelectedBones.size() >= 2) {
        // 선택한 바디 사이의 모든 쌍을 한 번에 끄거나 켠다.
        for (const bool Disable : {true, false}) {
            if (Disable ? ImGui::Button("Disable Collision Between Selected") : ImGui::Button("Enable Collision Between Selected")) {
                Component->DestroyPhysicsState();

                for (Index_t First{}; First < SelectedBones.size(); ++First) {
                    for (Index_t Second{First + 1}; Second < SelectedBones.size(); ++Second) {
                        PhysicsAsset->SetCollisionDisabled(SelectedBones[First], SelectedBones[Second], Disable);
                    }
                }

                ApplyEdit();
            }

            if (Disable) {
                ImGui::SameLine();
            }
        }
    }

    // 현재 자세에서 겹치는데 이어져 있지 않은 쌍을 자동으로 끈다. 시뮬레이션 전 기준 자세에서 실행한다.
    ImGui::BeginDisabled(mSimulating || mContent == nullptr || mContent->GetWorld() == nullptr || mContent->GetWorld()->GetPhysicsScene() == nullptr);

    if (ImGui::Button("Auto-Disable Overlapping Pairs")) {
        Core::TArray<std::pair<Index_t, Index_t>> Overlaps{};

        PhysicsAssetEditing::FindOverlappingBodyPairs(*mContent->GetWorld()->GetPhysicsScene(), Overlaps);

        Index_t Added{};
        Index_t Joined{};
        Index_t BothKinematic{};
        const Core::TArray<std::pair<Index_t, Index_t>> Found{Overlaps};

        for (const auto& [A, B] : Found) {
            if (A >= PhysicsAsset->mSkeletalBodySetups.size() || B >= PhysicsAsset->mSkeletalBodySetups.size() || PhysicsAsset->mSkeletalBodySetups[A] == nullptr || PhysicsAsset->mSkeletalBodySetups[B] == nullptr) {
                continue;
            }

            const PhysicsRuntime::UBodySetup& SetupA{*PhysicsAsset->mSkeletalBodySetups[A]};
            const PhysicsRuntime::UBodySetup& SetupB{*PhysicsAsset->mSkeletalBodySetups[B]};
            const bool IsJoined{std::any_of(PhysicsAsset->mConstraintSetup.begin(), PhysicsAsset->mConstraintSetup.end(), [&](const auto& Template) {
                return Template != nullptr && ((Template->mDefaultInstance.mConstraintBone1 == SetupA.GetBoneName() && Template->mDefaultInstance.mConstraintBone2 == SetupB.GetBoneName()) || (Template->mDefaultInstance.mConstraintBone1 == SetupB.GetBoneName() && Template->mDefaultInstance.mConstraintBone2 == SetupA.GetBoneName()));
            })};

            // 관절로 이어진 쌍은 이미 충돌하지 않고, 둘 다 Kinematic이면 접촉이 만들어지지 않으므로 목록에 넣을 필요가 없다.
            if (IsJoined) {
                ++Joined;

                continue;
            }

            if (!SetupA.ShouldSimulatePhysics() && !SetupB.ShouldSimulatePhysics()) {
                ++BothKinematic;

                continue;
            }

            if (PhysicsAsset->SetCollisionDisabled(SetupA.GetBoneName(), SetupB.GetBoneName(), true)) {
                ++Added;
            }
        }

        mCollisionMessage = "Found " + std::to_string(Found.size()) + " overlapping pair(s): disabled " + std::to_string(Added) + ", skipped " + std::to_string(Joined) + " joined and " + std::to_string(BothKinematic) + " kinematic-only.";

        if (Added > 0) {
            Component->DestroyPhysicsState();
            ApplyEdit();
        }
    }

    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Clear All Pairs") && !PhysicsAsset->mCollisionDisabledPairs.empty()) {
        Component->DestroyPhysicsState();
        PhysicsAsset->mCollisionDisabledPairs.clear();
        mCollisionMessage = "Cleared all disabled pairs.";
        ApplyEdit();
    }

    if (!mCollisionMessage.empty()) {
        ImGui::TextWrapped("%s", mCollisionMessage.c_str());
    }
}

void FPhysicsAssetEditorToolkit::DrawBodyEditor() {
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};

    if (PhysicsAsset == nullptr || Component == nullptr || mSkeleton == nullptr) {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Body");

    // 선택한 바디 중 유효한 것만 모은다. 여러 개이면 아래 편집은 모두에 적용된다.
    Core::TArray<Index_t> BodyIndices{};
    Core::TArray<PhysicsRuntime::UBodySetup*> Setups{};

    for (const Index_t Index : mSelectedBodies) {
        if (Index < PhysicsAsset->mSkeletalBodySetups.size() && PhysicsAsset->mSkeletalBodySetups[Index] != nullptr) {
            BodyIndices.push_back(Index);
            Setups.push_back(PhysicsAsset->mSkeletalBodySetups[Index].get());
        }
    }

    if (Setups.empty()) {
        if (mSelectedBone == InvalidIndex) {
            ImGui::TextDisabled("Select a bone or a body.");

            return;
        }

        // 선택한 뼈에 바디가 없으면 정점 분포에 맞춘 기본 캡슐 바디를 추가할 수 있다.
        if (ImGui::Button("Add Body To Bone")) {
            Component->DestroyPhysicsState();

            const Index_t NewBody{PhysicsAssetEditing::AddBodyForBone(*PhysicsAsset, *mMesh, *mSkeleton, mSelectedBone)};

            if (NewBody != InvalidIndex) {
                SelectBody(NewBody, PhysicsAssetEditing::FShapeRef{});
            }

            ApplyEdit();
        }

        return;
    }

    const Index_t Count{Setups.size()};

    if (Count == 1) {
        ImGui::Text("Body %zu (%s): %zu shapes", BodyIndices.front(), Setups.front()->GetBoneName().c_str(), PhysicsAssetEditing::GetTotalShapeCount(*Setups.front()));
    } else {
        Core::FString Names{};

        for (Index_t Index{}; Index < Count && Index < 4; ++Index) {
            Names += (Index == 0 ? "" : ", ") + Setups[Index]->GetBoneName();
        }

        ImGui::Text("%zu bodies selected", Count);
        ImGui::TextWrapped("%s%s", Names.c_str(), Count > 4 ? ", ..." : "");
    }

    if (ImGui::Button(Count == 1 ? "Delete Body" : "Delete Bodies")) {
        // 바디를 지우면 그 뼈를 잇던 관절 설정도 함께 지워지고 번호가 당겨지므로, 큰 번호부터 지우고 선택을 해제한다.
        Component->DestroyPhysicsState();
        std::sort(BodyIndices.begin(), BodyIndices.end(), std::greater<Index_t>{});

        for (const Index_t Index : BodyIndices) {
            Asset::FPhysicsAssetUtils::DestroyBody(*PhysicsAsset, Index);
        }

        ClearSelection();
        mGraphFocus.clear();
        ApplyEdit();

        return;
    }

    // 관절이 없는 바디 하나를 골랐으면 바디가 있는 가장 가까운 조상과 잇는 관절을 추가할 수 있다. 추가하면 그 관절이 선택된다.
    if (Count == 1 && mSelectedBone != InvalidIndex && PhysicsAsset->FindConstraintIndex(Setups.front()->GetBoneName()) == InvalidIndex) {
        if (ImGui::Button("Add Constraint To Parent Body")) {
            Component->DestroyPhysicsState();

            const Index_t NewIndex{PhysicsAssetEditing::AddConstraintForBone(*PhysicsAsset, *mSkeleton, mSelectedBone)};

            if (NewIndex != InvalidIndex) {
                mSelectedConstraint = NewIndex;
            }

            ApplyEdit();

            return;
        }
    }

    // Physics Type: Kinematic은 시뮬레이션 중에도 애니메이션 자세에 고정되고, Dynamic은 물리로 움직인다.
    // 선택한 바디의 값이 서로 다르면 "Multiple Values"를 표시하고, 항목을 고르면 모두 같은 값으로 바꾼다.
    {
        const bool First{Setups.front()->ShouldSimulatePhysics()};
        const bool MixedValue{std::any_of(Setups.begin(), Setups.end(), [First](const PhysicsRuntime::UBodySetup* Setup) {
            return Setup->ShouldSimulatePhysics() != First;
        })};
        constexpr const char* TypeNames[]{"Kinematic", "Dynamic"};

        if (ImGui::BeginCombo("Physics Type", MixedValue ? "Multiple Values" : TypeNames[First ? 1 : 0])) {
            for (int Type{}; Type < 2; ++Type) {
                if (ImGui::Selectable(TypeNames[Type], !MixedValue && (Type == 1) == First)) {
                    Component->DestroyPhysicsState();

                    for (PhysicsRuntime::UBodySetup* Setup : Setups) {
                        Setup->SetSimulatePhysics(Type == 1);
                    }

                    ApplyEdit();
                }
            }

            ImGui::EndCombo();
        }

        // 기준(마지막으로 저장하거나 연 시점)의 값과 다르면 되돌리는 화살표를 보인다. 기준에 없는 바디는 Dynamic이 기본이다.
        const auto BaselineSimulate{[this](const PhysicsRuntime::UBodySetup* Setup) {
            const FBaselineBody* Body{FindBaselineBody(Setup->GetBoneName())};

            return Body != nullptr ? Body->mSimulatePhysics : true;
        }};
        const bool Differs{std::any_of(Setups.begin(), Setups.end(), [&](const PhysicsRuntime::UBodySetup* Setup) {
            return Setup->ShouldSimulatePhysics() != BaselineSimulate(Setup);
        })};

        if (ResetButton("Physics Type", Differs)) {
            Component->DestroyPhysicsState();

            for (PhysicsRuntime::UBodySetup* Setup : Setups) {
                Setup->SetSimulatePhysics(BaselineSimulate(Setup));
            }

            ApplyEdit();
        }
    }

    // Collision: 이 바디의 모양이 다른 모양과 접촉(충돌)을 만드는지 정한다. Physics Type(움직임을 누가 정하는가)과는 별개이며,
    // Disabled이면 Dynamic이어도 중력과 관절만 따르고 같은 에셋의 바디와도 월드의 다른 것과도 부딪히지 않는다.
    {
        const bool FirstEnabled{Setups.front()->IsCollisionEnabled()};
        const bool MixedCollision{std::any_of(Setups.begin(), Setups.end(), [FirstEnabled](const PhysicsRuntime::UBodySetup* Setup) {
            return Setup->IsCollisionEnabled() != FirstEnabled;
        })};
        constexpr const char* CollisionNames[]{"Disabled", "Enabled"};

        if (ImGui::BeginCombo("Collision", MixedCollision ? "Multiple Values" : CollisionNames[FirstEnabled ? 1 : 0])) {
            for (int Value{}; Value < 2; ++Value) {
                if (ImGui::Selectable(CollisionNames[Value], !MixedCollision && (Value == 1) == FirstEnabled)) {
                    Component->DestroyPhysicsState();

                    for (PhysicsRuntime::UBodySetup* Setup : Setups) {
                        Setup->SetCollisionEnabled(Value == 1);
                    }

                    ApplyEdit();
                }
            }

            ImGui::EndCombo();
        }

        const auto BaselineCollision{[this](const PhysicsRuntime::UBodySetup* Setup) {
            const FBaselineBody* Body{FindBaselineBody(Setup->GetBoneName())};

            return Body != nullptr ? Body->mCollisionEnabled : true;
        }};
        const bool CollisionDiffers{std::any_of(Setups.begin(), Setups.end(), [&](const PhysicsRuntime::UBodySetup* Setup) {
            return Setup->IsCollisionEnabled() != BaselineCollision(Setup);
        })};

        if (ResetButton("Collision", CollisionDiffers)) {
            Component->DestroyPhysicsState();

            for (PhysicsRuntime::UBodySetup* Setup : Setups) {
                Setup->SetCollisionEnabled(BaselineCollision(Setup));
            }

            ApplyEdit();
        }
    }

    // 감쇠, 질량, 관성: 시뮬레이션되는 바디의 움직임을 다듬는다. 감쇠와 관성 배율을 키우면 가는 바디의 떨림이 줄고,
    // 질량이 0이면 형상 부피와 기본 밀도로 계산한다. 선택한 바디의 값이 서로 다르면 "Multiple Values"를 표시한다.
    {
        struct FDynamicsField {
            const char* mLabel;
            const char* mTooltip;
            float mSpeed;
            float mMin;
            float mMax;
            const char* mFormat;
            float (*mGet)(const PhysicsRuntime::UBodySetup&);
            void (*mSet)(PhysicsRuntime::UBodySetup&, float);
            float (*mBaseline)(const FBaselineBody&);
        };

        static const FDynamicsField Fields[]{
            {"Linear Damping", "Slows the translation. 0 never loses speed on its own.", 0.01f, 0.0f, 100.0f, "%.3f",
                [](const PhysicsRuntime::UBodySetup& Setup) { return Setup.GetLinearDamping().GetBase(); },
                [](PhysicsRuntime::UBodySetup& Setup, float Value) { Setup.SetLinearDamping(Core::FFrequency::FromBase(Value)); },
                [](const FBaselineBody& Body) { return Body.mLinearDamping; }},
            {"Angular Damping", "Slows the rotation. Raise it to reduce jitter on thin bodies.", 0.01f, 0.0f, 100.0f, "%.3f",
                [](const PhysicsRuntime::UBodySetup& Setup) { return Setup.GetAngularDamping().GetBase(); },
                [](PhysicsRuntime::UBodySetup& Setup, float Value) { Setup.SetAngularDamping(Core::FFrequency::FromBase(Value)); },
                [](const FBaselineBody& Body) { return Body.mAngularDamping; }},
            {"Mass (kg)", "0 computes the mass from the shape volume and the default density.", 0.001f, 0.0f, 1000.0f, "%.3f",
                [](const PhysicsRuntime::UBodySetup& Setup) { return Setup.GetMassInKg().GetBase(); },
                [](PhysicsRuntime::UBodySetup& Setup, float Value) { Setup.SetMassInKg(Core::FMass::FromBase(Value)); },
                [](const FBaselineBody& Body) { return Body.mMassInKg; }},
            {"Inertia Scale", "Multiplies the moment of inertia. Larger values make the body rotate more slowly.", 0.05f, 0.01f, 1000.0f, "%.2f",
                [](const PhysicsRuntime::UBodySetup& Setup) { return Setup.GetInertiaScale(); },
                [](PhysicsRuntime::UBodySetup& Setup, float Value) { Setup.SetInertiaScale(Value); },
                [](const FBaselineBody& Body) { return Body.mInertiaScale; }},
            {"Wind Influence", "How strongly world wind pushes this body (1/s). 0 ignores wind. Used by the Rigid Body node.", 0.05f, 0.0f, 100.0f, "%.2f",
                [](const PhysicsRuntime::UBodySetup& Setup) { return Setup.GetWindInfluence(); },
                [](PhysicsRuntime::UBodySetup& Setup, float Value) { Setup.SetWindInfluence(Value); },
                [](const FBaselineBody& Body) { return Body.mWindInfluence; }}};

        // 기준에 없는 바디는 새 바디의 기본값과 비교한다.
        const FBaselineBody DefaultBody{};

        for (const FDynamicsField& Field : Fields) {
            float Value{Field.mGet(*Setups.front())};
            const bool Mixed{std::any_of(Setups.begin(), Setups.end(), [&](const PhysicsRuntime::UBodySetup* Setup) {
                return std::abs(Field.mGet(*Setup) - Value) > 1e-6f;
            })};

            if (DragComponents(Field.mLabel, 1, &Value, &Mixed, Field.mSpeed, Field.mMin, Field.mMax, Field.mFormat) != 0) {
                Component->DestroyPhysicsState();

                for (PhysicsRuntime::UBodySetup* Setup : Setups) {
                    Field.mSet(*Setup, Value);
                }

                ApplyEdit();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Field.mTooltip);
            }

            const auto BaselineOf{[&](const PhysicsRuntime::UBodySetup* Setup) {
                const FBaselineBody* Body{FindBaselineBody(Setup->GetBoneName())};

                return Field.mBaseline(Body != nullptr ? *Body : DefaultBody);
            }};
            const bool Differs{std::any_of(Setups.begin(), Setups.end(), [&](const PhysicsRuntime::UBodySetup* Setup) {
                return std::abs(Field.mGet(*Setup) - BaselineOf(Setup)) > 1e-6f;
            })};

            if (ResetButton(Field.mLabel, Differs)) {
                Component->DestroyPhysicsState();

                for (PhysicsRuntime::UBodySetup* Setup : Setups) {
                    Field.mSet(*Setup, BaselineOf(Setup));
                }

                ApplyEdit();
            }
        }
    }

    ImGui::TextUnformatted("Add Shape");

    for (const auto& [Label, Type] : {std::pair{"Sphere", PhysicsAssetEditing::EShapeType::Sphere}, std::pair{"Box", PhysicsAssetEditing::EShapeType::Box}, std::pair{"Capsule", PhysicsAssetEditing::EShapeType::Sphyl}}) {
        ImGui::SameLine();

        if (ImGui::Button(Label)) {
            Component->DestroyPhysicsState();

            // 바디마다 새 항목은 맨 뒤에 붙는다. 대표 바디의 새 항목을 선택한다.
            for (Index_t Index{}; Index < Count; ++Index) {
                const PhysicsAssetEditing::FShapeRef Added{PhysicsAssetEditing::AddShape(*Setups[Index], Type)};

                if (Index == 0 || BodyIndices[Index] == mSelectedBody) {
                    mSelectedShape = Added;
                }
            }

            ApplyEdit();
        }
    }

    // 선택한 바디들이 가진 항목 목록이다. 종류와 번호가 같은 항목은 한 행으로 묶고, 일부 바디에만 있으면 개수를 덧붙인다.
    // 행을 누르면 그 항목을 선택해 아래에서 편집한다.
    const auto CountWithShape{[&Setups](PhysicsAssetEditing::EShapeType Type, Index_t Index) {
        return static_cast<Index_t>(std::count_if(Setups.begin(), Setups.end(), [Type, Index](const PhysicsRuntime::UBodySetup* Setup) {
            return Index < PhysicsAssetEditing::GetShapeCount(*Setup, Type);
        }));
    }};

    // 선택한 항목이 없거나 선택한 바디들에 없으면 첫 번째 항목을 자동으로 고른다.
    if (!mSelectedShape.IsValid() || CountWithShape(mSelectedShape.mType, mSelectedShape.mIndex) == 0) {
        mSelectedShape = PhysicsAssetEditing::FShapeRef{};

        for (const PhysicsAssetEditing::EShapeType Type : {PhysicsAssetEditing::EShapeType::Sphere, PhysicsAssetEditing::EShapeType::Box, PhysicsAssetEditing::EShapeType::Sphyl, PhysicsAssetEditing::EShapeType::Convex}) {
            if (CountWithShape(Type, 0) > 0) {
                mSelectedShape = PhysicsAssetEditing::FShapeRef{Type, 0};

                break;
            }
        }
    }

    for (const auto& [Label, Type] : {std::pair{"Sphere", PhysicsAssetEditing::EShapeType::Sphere}, std::pair{"Box", PhysicsAssetEditing::EShapeType::Box}, std::pair{"Capsule", PhysicsAssetEditing::EShapeType::Sphyl}, std::pair{"Convex", PhysicsAssetEditing::EShapeType::Convex}}) {
        Index_t MaxCount{};

        for (const PhysicsRuntime::UBodySetup* Setup : Setups) {
            MaxCount = std::max(MaxCount, PhysicsAssetEditing::GetShapeCount(*Setup, Type));
        }

        for (Index_t Index{}; Index < MaxCount; ++Index) {
            const bool IsSelected{mSelectedShape.mType == Type && mSelectedShape.mIndex == Index};
            const Index_t Have{CountWithShape(Type, Index)};
            Core::FString RowLabel{Core::FString{Label} + " " + std::to_string(Index)};

            if (Have != Count) {
                RowLabel += " (" + std::to_string(Have) + "/" + std::to_string(Count) + ")";
            }

            RowLabel += "##Shape" + std::to_string(static_cast<int>(Type)) + "_" + std::to_string(Index);

            if (ImGui::Selectable(RowLabel.c_str(), IsSelected)) {
                mSelectedShape = PhysicsAssetEditing::FShapeRef{Type, Index};
            }
        }
    }

    const Index_t SelectedHave{mSelectedShape.IsValid() ? CountWithShape(mSelectedShape.mType, mSelectedShape.mIndex) : 0};

    if (SelectedHave == 0) {
        ImGui::TextDisabled("Select a shape above or click one in the viewport.");

        return;
    }

    ImGui::Separator();
    ImGui::Text("Shape: %s %zu", mSelectedShape.mType == PhysicsAssetEditing::EShapeType::Sphere ? "Sphere" : mSelectedShape.mType == PhysicsAssetEditing::EShapeType::Box ? "Box" : mSelectedShape.mType == PhysicsAssetEditing::EShapeType::Sphyl ? "Capsule" : "Convex", mSelectedShape.mIndex);

    if (SelectedHave != Count) {
        ImGui::TextDisabled("Applies to %zu of %zu selected bodies.", SelectedHave, Count);
    }

    if (DrawShapeFields(Setups, mSelectedShape)) {
        ApplyEdit();
    }

    // 항목이 하나도 없는 바디는 PhysX Actor를 만들 수 없으므로 바디의 마지막 항목은 지울 수 없다.
    const auto CanDelete{[this](const PhysicsRuntime::UBodySetup* Setup) {
        return mSelectedShape.mIndex < PhysicsAssetEditing::GetShapeCount(*Setup, mSelectedShape.mType) && PhysicsAssetEditing::GetTotalShapeCount(*Setup) > 1;
    }};

    ImGui::BeginDisabled(std::none_of(Setups.begin(), Setups.end(), CanDelete));

    if (ImGui::Button("Delete Shape")) {
        Component->DestroyPhysicsState();

        for (PhysicsRuntime::UBodySetup* Setup : Setups) {
            if (CanDelete(Setup)) {
                PhysicsAssetEditing::DeleteShape(*Setup, mSelectedShape);
            }
        }

        mSelectedShape = PhysicsAssetEditing::FShapeRef{};
        ApplyEdit();
    }

    ImGui::EndDisabled();
}

void FPhysicsAssetEditorToolkit::DrawRegenerate() {
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (PhysicsAsset == nullptr || mSkeleton == nullptr) {
        return;
    }

    ImGui::TextUnformatted("Regenerate (auto fit)");

    constexpr const char* GeomNames[]{"Box", "Capsule", "Sphere"};
    constexpr Asset::EPhysAssetFitGeomType GeomTypes[]{Asset::EPhysAssetFitGeomType::Box, Asset::EPhysAssetFitGeomType::Sphyl, Asset::EPhysAssetFitGeomType::Sphere};
    int Geom{};

    for (int Index{}; Index < 3; ++Index) {
        if (GeomTypes[Index] == mRegenParams.mGeomType) {
            Geom = Index;
        }
    }

    if (ImGui::Combo("Primitive", &Geom, GeomNames, 3)) {
        mRegenParams.mGeomType = GeomTypes[Geom];
    }

    int Weight{mRegenParams.mVertWeight == Asset::EPhysAssetFitVertWeight::AnyWeight ? 0 : 1};

    if (ImGui::Combo("Vertex Weight", &Weight, "Any\0Dominant\0")) {
        mRegenParams.mVertWeight = Weight == 0 ? Asset::EPhysAssetFitVertWeight::AnyWeight : Asset::EPhysAssetFitVertWeight::DominantWeight;
    }

    ImGui::DragFloat("Min Bone Size", &mRegenParams.mMinBoneSize, 0.005f, 0.0f, 10.0f, "%.3f");
    ImGui::Checkbox("Create Constraints", &mRegenParams.mCreateConstraints);
    ImGui::Checkbox("Body For All Bones", &mRegenParams.mBodyForAll);

    int Angular{static_cast<int>(mRegenParams.mAngularConstraintMode)};

    if (ImGui::Combo("Angular Motion", &Angular, "Free\0Limited\0Locked\0")) {
        mRegenParams.mAngularConstraintMode = static_cast<PhysicsRuntime::EAngularConstraintMotion>(Angular);
    }

    if (ImGui::Button("Regenerate...")) {
        mOpenRegeneratePopup = true;
    }

    if (mOpenRegeneratePopup) {
        ImGui::OpenPopup("Regenerate Physics Asset");
        mOpenRegeneratePopup = false;
    }

    if (ImGui::BeginPopupModal("Regenerate Physics Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("All bodies, shapes and constraints will be replaced.");
        ImGui::TextUnformatted("This cannot be undone.");

        if (ImGui::Button("Regenerate")) {
            Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};

            if (Component != nullptr) {
                Component->DestroyPhysicsState();
            }

            if (!Asset::FPhysicsAssetUtils::CreateFromSkeletalMesh(*PhysicsAsset, *mMesh, *mSkeleton, mRegenParams)) {
                mError = "Cannot regenerate the physics asset.";
            }

            ClearSelection();
            mGraphFocus.clear();
            // 다시 만든 값이 새 기준이 된다.
            mBaseline.reset();
            ApplyEdit();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

namespace {
    /**
     * @brief 항목의 중심과 회전 멤버의 주소를 구합니다.
     * @param[in,out] Setup 항목이 속한 바디 설명서입니다.
     * @param[in] Shape 대상 항목입니다.
     * @param[out] Center 중심 멤버의 주소입니다.
     * @param[out] Rotation 회전 멤버의 주소이며 회전이 없는 형상(Sphere)이면 nullptr입니다.
     * @return 편집할 수 있는 항목이면 true이고, Convex이거나 번호가 범위를 벗어나면 false입니다.
     */
    bool GetShapeMembers(PhysicsRuntime::UBodySetup& Setup, const PhysicsAssetEditing::FShapeRef& Shape, FVector*& Center, FRotator*& Rotation) {
        if (!Shape.IsValid() || Shape.mIndex >= PhysicsAssetEditing::GetShapeCount(Setup, Shape.mType)) {
            return false;
        }

        PhysicsRuntime::FKAggregateGeom& Geometry{Setup.GetAggGeom()};

        switch (Shape.mType) {
            case PhysicsAssetEditing::EShapeType::Sphere:
                Center = &Geometry.mSphereElems[Shape.mIndex].mCenter;
                Rotation = nullptr;

                return true;
            case PhysicsAssetEditing::EShapeType::Box:
                Center = &Geometry.mBoxElems[Shape.mIndex].mCenter;
                Rotation = &Geometry.mBoxElems[Shape.mIndex].mRotation;

                return true;
            case PhysicsAssetEditing::EShapeType::Sphyl:
                Center = &Geometry.mSphylElems[Shape.mIndex].mCenter;
                Rotation = &Geometry.mSphylElems[Shape.mIndex].mRotation;

                return true;
            default:
                return false;
        }
    }
}

bool FPhysicsAssetEditorToolkit::GetGizmoPlacement(Editor::FGizmoPlacement& Out) const {
    const Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (Component == nullptr || PhysicsAsset == nullptr || mGizmoMode == Editor::EGizmoMode::None) {
        return false;
    }

    Out.mMode = mGizmoMode;

    // 세 축을 월드 축, 뼈의 축, 또는 대상(형상이나 관절 프레임)의 축으로 정한다.
    // Details의 Center, Rotation, Position 값은 뼈 기준이므로 Bone 축을 쓰면 필드의 X, Y, Z와 기즈모 축이 같은 방향이다.
    const auto SetAxes{[this, &Out](const FQuat& BoneRotation, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ) {
        const FVector Target[3]{AxisX, AxisY, AxisZ};

        for (Index_t Index{}; Index < 3; ++Index) {
            const FVector Unit{Editor::FGizmoCore::GetUnitAxes()[Index]};

            switch (mGizmoAxes) {
                case EGizmoAxes::Bone:
                    Out.mAxes[Index] = BoneRotation.RotateVector(Unit).Normalized();
                    break;
                case EGizmoAxes::Local:
                    Out.mAxes[Index] = !Target[Index].IsNearlyZero() ? Target[Index].Normalized() : Unit;
                    break;
                default:
                    Out.mAxes[Index] = Unit;
                    break;
            }
        }
    }};

    // 관절을 골랐으면 선택한 쪽 프레임에 놓는다.
    if (mSelectedConstraint < PhysicsAsset->mConstraintSetup.size() && PhysicsAsset->mConstraintSetup[mSelectedConstraint] != nullptr) {
        const PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset->mConstraintSetup[mSelectedConstraint]->mDefaultInstance};
        const bool Child{mGizmoConstraintFrame == 0};
        const Index_t BoneIndex{Component->GetBoneIndex(Child ? Constraint.mConstraintBone1 : Constraint.mConstraintBone2)};
        FTransform BoneWorld{};

        if (BoneIndex == InvalidIndex || !Component->GetBoneWorldTransform(BoneIndex, BoneWorld)) {
            return false;
        }

        const FVector AxisX{BoneWorld.TransformVectorNoScale(Child ? Constraint.mPriAxis1 : Constraint.mPriAxis2).Normalized()};
        const FVector AxisY{BoneWorld.TransformVectorNoScale(Child ? Constraint.mSecAxis1 : Constraint.mSecAxis2).Normalized()};

        Out.mPivot = BoneWorld.TransformPosition(Child ? Constraint.mPos1 : Constraint.mPos2);
        SetAxes(BoneWorld.mRotation, AxisX, AxisY, AxisX.Cross(AxisY));

        return true;
    }

    // 아니면 대표 바디에서 선택한 항목에 놓는다.
    if (mSelectedBody >= PhysicsAsset->mSkeletalBodySetups.size() || PhysicsAsset->mSkeletalBodySetups[mSelectedBody] == nullptr) {
        return false;
    }

    PhysicsRuntime::UBodySetup& Setup{*PhysicsAsset->mSkeletalBodySetups[mSelectedBody]};
    FVector* Center{};
    FRotator* Rotation{};

    // 회전이 없는 형상(Sphere)은 회전 모드에서 기즈모를 표시하지 않는다.
    if (!GetShapeMembers(Setup, mSelectedShape, Center, Rotation) || (mGizmoMode == Editor::EGizmoMode::Rotate && Rotation == nullptr)) {
        return false;
    }

    const Index_t BoneIndex{Component->GetBoneIndex(Setup.GetBoneName())};
    FTransform BoneWorld{};

    if (BoneIndex == InvalidIndex || !Component->GetBoneWorldTransform(BoneIndex, BoneWorld)) {
        return false;
    }

    const FQuat World{Rotation != nullptr ? BoneWorld.mRotation * Rotation->Quaternion() : BoneWorld.mRotation};

    Out.mPivot = BoneWorld.TransformPosition(*Center);
    SetAxes(BoneWorld.mRotation, World.RotateVector(FVector{1.0f, 0.0f, 0.0f}), World.RotateVector(FVector{0.0f, 1.0f, 0.0f}), World.RotateVector(FVector{0.0f, 0.0f, 1.0f}));

    return true;
}

bool FPhysicsAssetEditorToolkit::CaptureGizmoStart() {
    const Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    mGizmoShapes.clear();
    mGizmoEditsFrame = false;

    if (Component == nullptr || PhysicsAsset == nullptr) {
        return false;
    }

    if (mSelectedConstraint < PhysicsAsset->mConstraintSetup.size() && PhysicsAsset->mConstraintSetup[mSelectedConstraint] != nullptr) {
        const PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset->mConstraintSetup[mSelectedConstraint]->mDefaultInstance};
        const bool Child{mGizmoConstraintFrame == 0};
        const Index_t BoneIndex{Component->GetBoneIndex(Child ? Constraint.mConstraintBone1 : Constraint.mConstraintBone2)};

        if (BoneIndex == InvalidIndex || !Component->GetBoneWorldTransform(BoneIndex, mGizmoFrame.mBoneWorld)) {
            return false;
        }

        mGizmoFrame.mStartPos = Child ? Constraint.mPos1 : Constraint.mPos2;
        mGizmoFrame.mStartPri = Child ? Constraint.mPriAxis1 : Constraint.mPriAxis2;
        mGizmoFrame.mStartSec = Child ? Constraint.mSecAxis1 : Constraint.mSecAxis2;
        mGizmoFrame.mPosition = mGizmoFrame.mBoneWorld.TransformPosition(mGizmoFrame.mStartPos);
        mGizmoFrame.mAxisX = mGizmoFrame.mBoneWorld.TransformVectorNoScale(mGizmoFrame.mStartPri).Normalized();
        mGizmoFrame.mAxisY = mGizmoFrame.mBoneWorld.TransformVectorNoScale(mGizmoFrame.mStartSec).Normalized();
        mGizmoEditsFrame = true;

        return true;
    }

    // 선택한 모든 바디의 같은 항목을 함께 움직인다. 회전은 항목마다 자기 중심을 축으로 돌린다.
    for (const Index_t BodyIndex : mSelectedBodies) {
        if (BodyIndex >= PhysicsAsset->mSkeletalBodySetups.size() || PhysicsAsset->mSkeletalBodySetups[BodyIndex] == nullptr) {
            continue;
        }

        PhysicsRuntime::UBodySetup& Setup{*PhysicsAsset->mSkeletalBodySetups[BodyIndex]};
        FVector* Center{};
        FRotator* Rotation{};
        FGizmoShapeState State{};
        const Index_t BoneIndex{Component->GetBoneIndex(Setup.GetBoneName())};

        if (!GetShapeMembers(Setup, mSelectedShape, Center, Rotation) || BoneIndex == InvalidIndex || !Component->GetBoneWorldTransform(BoneIndex, State.mBoneWorld)) {
            continue;
        }

        // 회전이 없는 형상은 회전 드래그에서 제외한다.
        if (mGizmoMode == Editor::EGizmoMode::Rotate && Rotation == nullptr) {
            continue;
        }

        State.mSetup = &Setup;
        State.mStartCenter = *Center;
        State.mStartLocalRotation = Rotation != nullptr ? *Rotation : FRotator{};
        State.mStartPosition = State.mBoneWorld.TransformPosition(*Center);
        State.mStartRotation = State.mBoneWorld.mRotation * State.mStartLocalRotation.Quaternion();
        mGizmoShapes.push_back(State);
    }

    return !mGizmoShapes.empty();
}

void FPhysicsAssetEditorToolkit::ApplyGizmoDrag() {
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (Component == nullptr || PhysicsAsset == nullptr) {
        return;
    }

    const bool Rotate{mGizmo.GetDragMode() == Editor::EGizmoMode::Rotate};
    const FVector Translation{Rotate ? FVector{} : mGizmo.GetTranslation()};
    const FQuat Delta{Rotate ? mGizmo.GetRotation() : FQuat{}};

    // 편집 전에 물리 상태를 내려 바디가 바뀌는 설명서를 참조하지 않게 한다.
    Component->DestroyPhysicsState();

    if (mGizmoEditsFrame) {
        if (mSelectedConstraint >= PhysicsAsset->mConstraintSetup.size() || PhysicsAsset->mConstraintSetup[mSelectedConstraint] == nullptr) {
            return;
        }

        PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset->mConstraintSetup[mSelectedConstraint]->mDefaultInstance};
        const bool Child{mGizmoConstraintFrame == 0};
        const FTransform& BoneWorld{mGizmoFrame.mBoneWorld};
        const FVector Position{BoneWorld.InverseTransformPosition(mGizmoFrame.mPosition + Translation)};
        const FVector Primary{BoneWorld.InverseTransformVectorNoScale(Delta.RotateVector(mGizmoFrame.mAxisX)).Normalized()};
        FVector Secondary{BoneWorld.InverseTransformVectorNoScale(Delta.RotateVector(mGizmoFrame.mAxisY))};

        // 두 축이 직교하도록 보조 축에서 주 축 성분을 뺀다.
        Secondary = (Secondary - Primary * Secondary.Dot(Primary)).Normalized();

        (Child ? Constraint.mPos1 : Constraint.mPos2) = Position;
        (Child ? Constraint.mPriAxis1 : Constraint.mPriAxis2) = Primary;
        (Child ? Constraint.mSecAxis1 : Constraint.mSecAxis2) = Secondary;

        // Details의 회전 필드도 새 축에 맞춘다.
        mFrameRotation[Child ? 0 : 1] = AxesToRotator(Primary, Secondary);
        mFrameRotationConstraint = mSelectedConstraint;
    } else {
        for (const FGizmoShapeState& State : mGizmoShapes) {
            FVector* Center{};
            FRotator* Rotation{};

            if (!GetShapeMembers(*State.mSetup, mSelectedShape, Center, Rotation)) {
                continue;
            }

            *Center = State.mBoneWorld.InverseTransformPosition(State.mStartPosition + Translation);

            if (Rotation != nullptr && Rotate) {
                *Rotation = FRotator{(State.mBoneWorld.mRotation.Inverse() * (Delta * State.mStartRotation)).Normalized()};
            }
        }
    }

    ApplyEdit();
}

void FPhysicsAssetEditorToolkit::CancelGizmoDrag() {
    Engine::USkeletalMeshComponent* Component{mContent != nullptr ? mContent->GetComponent() : nullptr};
    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (Component == nullptr || PhysicsAsset == nullptr) {
        return;
    }

    Component->DestroyPhysicsState();

    if (mGizmoEditsFrame) {
        if (mSelectedConstraint < PhysicsAsset->mConstraintSetup.size() && PhysicsAsset->mConstraintSetup[mSelectedConstraint] != nullptr) {
            PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset->mConstraintSetup[mSelectedConstraint]->mDefaultInstance};
            const bool Child{mGizmoConstraintFrame == 0};

            (Child ? Constraint.mPos1 : Constraint.mPos2) = mGizmoFrame.mStartPos;
            (Child ? Constraint.mPriAxis1 : Constraint.mPriAxis2) = mGizmoFrame.mStartPri;
            (Child ? Constraint.mSecAxis1 : Constraint.mSecAxis2) = mGizmoFrame.mStartSec;
            mFrameRotation[Child ? 0 : 1] = AxesToRotator(mGizmoFrame.mStartPri, mGizmoFrame.mStartSec);
        }
    } else {
        for (const FGizmoShapeState& State : mGizmoShapes) {
            FVector* Center{};
            FRotator* Rotation{};

            if (GetShapeMembers(*State.mSetup, mSelectedShape, Center, Rotation)) {
                *Center = State.mStartCenter;

                if (Rotation != nullptr) {
                    *Rotation = State.mStartLocalRotation;
                }
            }
        }
    }

    ApplyEdit();
}

bool FPhysicsAssetEditorToolkit::UpdateGizmo(const Engine::FSceneView& View, const FVector2& Mouse, bool Hovered) {
    // 시뮬레이션 중에는 편집하지 않는다.
    if (mSimulating) {
        mGizmo.EndDrag();

        return false;
    }

    if (mGizmo.IsDragging()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            CancelGizmoDrag();
            mGizmo.EndDrag();

            return true;
        }

        if (mGizmo.UpdateDrag(Mouse)) {
            ApplyGizmoDrag();
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            mGizmo.EndDrag();
        }

        return true;
    }

    Editor::FGizmoPlacement Placement{};

    if (!GetGizmoPlacement(Placement)) {
        mGizmo.SetHoveredAxis(-1);

        return false;
    }

    const bool CanHover{Hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)};
    const Engine::FHitProxy Hit{CanHover ? mGizmo.HitTest(Mouse, View, Placement) : Engine::FHitProxy{}};
    const Int32 Axis{static_cast<Int32>(Hit.mType == Engine::EHitProxyType::GizmoAxis ? Hit.mAxis : -1)};

    mGizmo.SetHoveredAxis(Axis);

    if (Axis < 0) {
        return false;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!mGizmo.BeginDrag(Axis, Mouse, View, Placement)) {
            return true;
        }

        if (!CaptureGizmoStart()) {
            mGizmo.EndDrag();

            return true;
        }

        mGizmoUsed = true;
    }

    return true;
}

void FPhysicsAssetEditorToolkit::DrawViewportToolbar() {
    if (ImGui::RadioButton("Move (W)", mGizmoMode == Editor::EGizmoMode::Translate)) {
        mGizmo.EndDrag();
        mGizmoMode = Editor::EGizmoMode::Translate;
    }

    ImGui::SameLine();

    if (ImGui::RadioButton("Rotate (E)", mGizmoMode == Editor::EGizmoMode::Rotate)) {
        mGizmo.EndDrag();
        mGizmoMode = Editor::EGizmoMode::Rotate;
    }

    ImGui::SameLine();

    // World: 월드 축 / Bone: 뼈의 축(Details 필드의 X, Y, Z와 같은 방향) / Local: 형상이나 관절 프레임 자체의 축.
    int Space{static_cast<int>(mGizmoAxes)};

    ImGui::SetNextItemWidth(90.0f * ImGui::GetStyle().FontScaleMain);

    if (ImGui::Combo("##GizmoSpace", &Space, "World\0Bone\0Local\0")) {
        mGizmoAxes = static_cast<EGizmoAxes>(std::clamp(Space, 0, 2));
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("World: world axes\nBone: bone axes (same as the X, Y, Z fields in Details)\nLocal: the shape's or constraint frame's own axes");
    }

    // 관절은 프레임이 둘이므로 어느 프레임을 기즈모로 편집할지 고른다.
    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};

    if (PhysicsAsset != nullptr && mSelectedConstraint < PhysicsAsset->mConstraintSetup.size() && PhysicsAsset->mConstraintSetup[mSelectedConstraint] != nullptr) {
        ImGui::SameLine();

        if (ImGui::RadioButton("Frame 1", mGizmoConstraintFrame == 0)) {
            mGizmo.EndDrag();
            mGizmoConstraintFrame = 0;
        }

        ImGui::SameLine();

        if (ImGui::RadioButton("Frame 2", mGizmoConstraintFrame == 1)) {
            mGizmo.EndDrag();
            mGizmoConstraintFrame = 1;
        }
    }

    ImGui::SameLine();

    // 화면 채우기 방식.
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

        ImGui::Checkbox("Show Bodies", &mShowBodies);
        ImGui::Checkbox("Grid", &Flags.mGrid);
        ImGui::Checkbox("World Axes", &Flags.mAxes);
        ImGui::Checkbox("Sky", &Flags.mSky);
        ImGui::Checkbox("Shadows", &Flags.mShadows);
        ImGui::Checkbox("FXAA", &Flags.mFXAA);
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Gizmo")) {
        ImGui::OpenPopup("##GizmoSettings");
    }

    if (ImGui::BeginPopup("##GizmoSettings")) {
        Editor::FGizmoSettings Settings{*mGizmo.GetSettings()};
        float TranslationStep{Settings.mTranslationStep.GetBase()};
        float RotationStep{Core::Math::ToDegrees(Settings.mRotationStep)};
        bool Changed{};

        Changed |= ImGui::Checkbox("Translation Snap", &Settings.mTranslationSnap);

        if (ImGui::DragFloat("Step (m)", &TranslationStep, 0.001f, 0.001f, 10.0f, "%.3f")) {
            Settings.mTranslationStep = TranslationStep * 1_m;
            Changed = true;
        }

        Changed |= ImGui::Checkbox("Rotation Snap", &Settings.mRotationSnap);

        if (ImGui::DragFloat("Step (deg)", &RotationStep, 0.5f, 0.5f, 90.0f, "%.1f")) {
            Settings.mRotationStep = RotationStep * 1_deg;
            Changed = true;
        }

        Changed |= ImGui::SliderFloat("Size", &Settings.mSize, 0.5f, 2.0f, "%.2f");

        if (Changed) {
            mGizmo.SetSettings(Settings);
        }

        ImGui::EndPopup();
    }
}

void FPhysicsAssetEditorToolkit::PickAt(const ImVec2& Mouse, const ImVec2& Size) {
    Engine::UWorld* World{mContent != nullptr ? mContent->GetWorld() : nullptr};
    PhysicsRuntime::FPhysicsScene* Scene{World != nullptr ? World->GetPhysicsScene() : nullptr};

    if (Scene == nullptr) {
        return;
    }

    FRay Ray{};
    const Engine::FSceneView View{mViewer.GetCamera().CalcSceneView({Size.x, Size.y})};

    if (!View.Deproject({Mouse.x, Mouse.y}, Ray)) {
        return;
    }

    Index_t BodyIndex{InvalidIndex};
    Index_t ShapeOrdinal{InvalidIndex};

    const ImGuiIO& Input{ImGui::GetIO()};
    const bool Additive{Input.KeyShift || Input.KeyCtrl};

    if (!PhysicsAssetEditing::PickBody(*Scene, Ray, 1000.0f, BodyIndex, ShapeOrdinal)) {
        // 빈 곳을 누르면 선택을 해제한다. 여러 개를 고르는 중(Shift, Ctrl)이면 빗나간 클릭으로 해제하지 않는다.
        if (!Additive) {
            ClearSelection();
        }

        return;
    }

    const PhysicsRuntime::UPhysicsAsset* PhysicsAsset{GetPhysicsAsset()};
    PhysicsAssetEditing::FShapeRef Shape{};

    if (PhysicsAsset != nullptr && BodyIndex < PhysicsAsset->mSkeletalBodySetups.size() && PhysicsAsset->mSkeletalBodySetups[BodyIndex] != nullptr && ShapeOrdinal != InvalidIndex) {
        Shape = PhysicsAssetEditing::FindShapeByOrdinal(*PhysicsAsset->mSkeletalBodySetups[BodyIndex], ShapeOrdinal);
    }

    SelectBody(BodyIndex, Shape, true, Additive);
}
