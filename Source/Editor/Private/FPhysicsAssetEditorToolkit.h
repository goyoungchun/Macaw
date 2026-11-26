/**
 * @file FPhysicsAssetEditorToolkit.h
 * @brief 스켈레탈 메시의 물리 에셋을 미리보기와 함께 편집하는 에셋 툴킷을 정의합니다.
 * @ingroup Editor
 */

#pragma once

#include "FAssetEditorToolkit.h"
#include "FPhysicsAssetEditing.h"
#include "Asset/FPhysicsAssetUtils.h"
#include "Viewer/FAssetViewer.h"

#include "Core/Math/FRotator.h"
#include "Core/Math/FTransform.h"
#include "Editor/FGizmoCore.h"
#include "Engine/EViewMode.h"

#include <filesystem>
#include <memory>

namespace Asset {
    class USkeletalMesh;
    class USkeleton;
}

struct ImVec2;

namespace ax::NodeEditor {
    struct EditorContext;
}

namespace Engine {
    class FPrimitiveDrawInterface;
}

namespace PhysicsRuntime {
    struct FBodyInstance;
    struct FConstraintInstance;
    class UBodySetup;
    class UPhysicsAsset;
}

namespace Editor {
    class FEditorContext;
    class FPhysicsAssetViewerContent;
}

namespace Editor {

    /**
     * @brief 스켈레탈 메시의 물리 에셋을 미리보기와 함께 편집하는 에셋 툴킷입니다.
     * @details 편집 대상은 메시가 가리키는 물리 에셋이며, 지정된 에셋이 없으면 메시와 뼈대로 자동 생성한 에셋입니다.
     *          에셋 경로는 편집 중인 스켈레탈 메시의 경로입니다.
     * @ingroup Editor
     */
    class FPhysicsAssetEditorToolkit final : public Editor::FAssetEditorToolkit {
    private:

        /**
         * @brief 기즈모 드래그를 시작할 때 항목 하나의 시작 상태입니다.
         */
        struct FGizmoShapeState {
            /**
             * @brief 항목이 속한 바디 설명서의 비소유 주소입니다.
             */
            PhysicsRuntime::UBodySetup* mSetup{};

            /**
             * @brief 항목이 붙은 뼈의 월드 변환입니다.
             */
            FTransform mBoneWorld{};

            /**
             * @brief 시작 시점의 항목 월드 위치입니다.
             */
            FVector mStartPosition{};

            /**
             * @brief 시작 시점의 항목 월드 회전입니다.
             */
            FQuat mStartRotation{};

            /**
             * @brief 시작 시점의 뼈 기준 중심과 회전이며 취소할 때 복원합니다.
             */
            FVector mStartCenter{};
            FRotator mStartLocalRotation{};
        };

        /**
         * @brief 기즈모 드래그를 시작할 때 관절 프레임의 시작 상태입니다.
         */
        struct FGizmoFrameState {
            /**
             * @brief 프레임이 속한 뼈의 월드 변환입니다.
             */
            FTransform mBoneWorld{};

            /**
             * @brief 시작 시점의 프레임 월드 위치와 X(주 축), Y(보조 축) 방향입니다.
             */
            FVector mPosition{};
            FVector mAxisX{};
            FVector mAxisY{};

            /**
             * @brief 시작 시점의 뼈 기준 위치와 축이며 취소할 때 복원합니다.
             */
            FVector mStartPos{};
            FVector mStartPri{};
            FVector mStartSec{};
        };

        /**
         * @brief 편집 값을 되돌릴 기준(마지막으로 저장하거나 연 시점의 바디와 관절 설정)을 담는 구조체이며 정의는 cpp에 있습니다.
         */
        struct FBaseline;

        /**
         * @brief 기준에 담긴 바디 하나(뼈 이름으로 찾는 형상 목록과 Physics Type)이며 정의는 cpp에 있습니다.
         */
        struct FBaselineBody;

    public:

        /**
         * @brief 메시 미리보기와 상세 패널을 가진 툴킷을 구성합니다.
         * @param[in] Context 편집 대상과 공유 서비스를 제공하는 에디터 컨텍스트의 비소유 주소입니다.
         * @param[in] AssetPath 탭을 식별하는 경로입니다. 물리 에셋 파일을 열면 그 경로이고, 메시에서 열면 메시 경로입니다.
         * @param[in] MeshPath 미리보기에 쓰는 스켈레탈 메시의 콘텐츠 경로입니다.
         * @param[in] Mesh 호출자가 수명을 보장하는 스켈레탈 메시입니다.
         * @param[in] Skeleton 메시와 호환되는 스켈레톤이며 nullptr이면 상세 패널에서 고릅니다.
         * @param[in] PhysicsAssetPath 편집할 물리 에셋 파일의 콘텐츠 경로이며, 비어 있으면 메시가 가리키거나 자동 생성한 에셋을 편집합니다.
         */
        FPhysicsAssetEditorToolkit(Editor::FEditorContext* Context, const std::filesystem::path& AssetPath, const std::filesystem::path& MeshPath, Asset::USkeletalMesh* Mesh, Asset::USkeleton* Skeleton, const Core::FString& PhysicsAssetPath);

        /**
         * @brief 미리보기와 소유 멤버를 정리합니다.
         */
        ~FPhysicsAssetEditorToolkit() override;

    private:

        /**
         * @brief 탭 제목을 갱신합니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         */
        void UpdateEditor(float DeltaTime) override;

        /**
         * @brief 지정한 스켈레톤으로 미리보기 콘텐츠를 새로 만듭니다.
         * @param[in] Skeleton 미리보기에 사용할 스켈레톤입니다.
         * @return 미리보기를 준비했으면 true이며 실패 원인은 mError에 기록합니다.
         */
        bool BuildPreview(Asset::USkeleton* Skeleton);

        /**
         * @brief 메시에 내장된 스켈레톤이나 같은 이름 규칙의 스켈레톤 에셋을 찾습니다.
         * @param[out] OutPath 스켈레톤 에셋을 찾았으면 그 콘텐츠 경로이고, 내장 스켈레톤이면 비어 있습니다.
         * @return 찾은 스켈레톤의 비소유 주소이며 없으면 nullptr입니다.
         */
        Asset::USkeleton* FindDefaultSkeleton(Core::FString& OutPath) const;

        /**
         * @brief 미리보기 이미지를 표시하고 카메라 입력을 처리합니다.
         */
        void DrawViewport();

        /**
         * @brief 메시, 스켈레톤과 물리 에셋의 상태를 표시합니다.
         */
        void DrawDetails();

        /**
         * @brief 스켈레톤 에셋을 고르는 콤보 상자를 표시합니다.
         */
        void DrawSkeletonPicker();

        /**
         * @brief 뼈 계층, 바디와 관절 항목을 트리로 표시하고 선택을 처리합니다.
         */
        void DrawHierarchy();

        /**
         * @brief 뼈 하나와 그 자식 뼈, 뼈에 붙은 바디의 항목을 재귀로 표시합니다.
         * @param[in] Bone 표시할 뼈의 번호입니다.
         */
        void DrawBoneNode(Index_t Bone);

        /**
         * @brief 스켈레톤의 부모·자식 관계로 트리 표시용 목록을 다시 만들고 선택을 지웁니다.
         */
        void RebuildBoneTree();

        /**
         * @brief 미리보기 컴포넌트가 사용하는 물리 에셋을 반환합니다.
         * @return 비소유 주소이며 미리보기가 없으면 nullptr입니다.
         */
        PhysicsRuntime::UPhysicsAsset* GetPhysicsAsset() const;

        /**
         * @brief 물리 에셋의 모든 바디를 뼈의 현재 월드 변환 기준 선으로 기록합니다.
         * @param[in,out] Draw 선을 기록할 그리기 인터페이스입니다.
         */
        void BuildBodyLines(Engine::FPrimitiveDrawInterface& Draw) const;

        /**
         * @brief 바디(와 선택적으로 항목)를 선택하고 그 뼈도 함께 선택합니다.
         * @param[in] BodyIndex 선택할 바디 설명서의 번호입니다.
         * @param[in] Shape 선택할 항목이며 유효하지 않으면 바디만 선택합니다. Toggle이 true이면 무시하고 기존 항목 선택을 유지합니다.
         * @param[in] FocusGraph true이면 Graph 창의 중심도 선택한 바디 전체로 옮깁니다. Graph 안의 클릭은 false로 호출합니다.
         * @param[in] Toggle true이면 기존 선택을 지우지 않고 이 바디를 선택 목록에 넣거나(이미 있으면) 뺍니다.
         */
        void SelectBody(Index_t BodyIndex, const PhysicsAssetEditing::FShapeRef& Shape, bool FocusGraph = true, bool Toggle = false);

        /**
         * @brief 뼈, 바디, 항목, 관절 선택을 모두 지웁니다. Graph의 중심은 그대로 둡니다.
         */
        void ClearSelection();

        /**
         * @brief 선택한 항목이나 관절 프레임에 기즈모를 놓을 위치, 축과 모드를 구합니다.
         * @param[out] Out 기즈모 배치입니다.
         * @return 기즈모로 편집할 대상이 있으면 true입니다.
         */
        bool GetGizmoPlacement(Editor::FGizmoPlacement& Out) const;

        /**
         * @brief 기즈모의 호버와 드래그 입력을 처리하고 드래그 결과를 선택한 대상에 적용합니다.
         * @param[in] View 입력 해석에 쓰는 카메라 뷰입니다.
         * @param[in] Mouse 뷰포트 이미지 왼쪽 위 기준의 마우스 위치(픽셀)입니다.
         * @param[in] Hovered 뷰포트 위에 마우스가 있는지 나타냅니다.
         * @return 기즈모가 마우스를 차지하고 있으면(핸들 위이거나 드래그 중) true이며, 이때 카메라 조작과 선택은 건너뜁니다.
         */
        bool UpdateGizmo(const Engine::FSceneView& View, const FVector2& Mouse, bool Hovered);

        /**
         * @brief 기즈모 드래그를 시작하는 시점의 대상 상태를 저장합니다.
         * @return 드래그할 대상을 하나 이상 모았으면 true입니다.
         */
        bool CaptureGizmoStart();

        /**
         * @brief 기즈모의 현재 이동량·회전량을 시작 상태에 더해 대상에 적용합니다.
         */
        void ApplyGizmoDrag();

        /**
         * @brief 기즈모 드래그를 취소하고 대상을 시작 상태로 되돌립니다.
         */
        void CancelGizmoDrag();

        /**
         * @brief 뷰포트 위쪽의 기즈모, 뷰 모드, 표시 설정 툴바를 그립니다.
         */
        void DrawViewportToolbar();

        /**
         * @brief 기준이 없으면 현재 물리 에셋의 값으로 만듭니다. 편집하기 전에 호출해야 합니다.
         */
        void EnsureBaseline();

        /**
         * @brief 기준에서 뼈 이름이 같은 바디를 찾습니다.
         * @param[in] BoneName 바디가 붙은 뼈의 이름입니다.
         * @return 기준의 바디 주소이며 없으면 nullptr입니다.
         */
        const FBaselineBody* FindBaselineBody(const Core::FString& BoneName) const;

        /**
         * @brief 기준에서 양 끝 뼈가 같은 관절을 찾습니다.
         * @param[in] Bone1 관절의 자식 쪽 뼈 이름입니다.
         * @param[in] Bone2 관절의 부모 쪽 뼈 이름입니다.
         * @return 기준의 관절 설정 주소이며 없으면 nullptr입니다.
         */
        const PhysicsRuntime::FConstraintInstance* FindBaselineConstraint(const Core::FString& Bone1, const Core::FString& Bone2) const;

        /**
         * @brief 바디가 선택 목록에 들어 있는지 반환합니다.
         * @param[in] BodyIndex 확인할 바디 설명서의 번호입니다.
         * @return 선택되어 있으면 true입니다.
         */
        bool IsBodySelected(Index_t BodyIndex) const;

        /**
         * @brief 편집 결과를 미리보기의 물리 상태에 반영하고 수정 표시를 남깁니다.
         */
        void ApplyEdit();

        /**
         * @brief 충돌을 끌 바디 쌍을 편집하는 UI를 표시합니다. 선택한 바디의 비활성 상대 목록과 추가, 선택한 여러 바디 사이 일괄 설정, 겹치는 쌍 자동 설정이 있습니다.
         */
        void DrawCollisionPairs();

        /**
         * @brief 선택한 바디의 추가·삭제 버튼과 선택한 항목의 편집 필드를 표시합니다.
         */
        void DrawBodyEditor();

        /**
         * @brief 항목의 위치, 회전, 크기 필드를 표시하고 값이 바뀌면 true를 반환합니다.
         * @details 바디를 여러 개 선택했으면 항목이 있는 바디들의 값을 비교해 같으면 그 값을, 다르면 "Multiple Values"를 보여 주고,
         *          값을 바꾸면 바꾼 필드를 항목이 있는 모든 바디에 적용합니다.
         * @param[in,out] Setups 편집할 바디 설명서들입니다. 모두 유효한 주소여야 합니다.
         * @param[in] Shape 편집할 항목입니다. 항목이 없는 바디는 건너뜁니다.
         * @return 값이 바뀌었으면 true입니다.
         */
        bool DrawShapeFields(const Core::TArray<PhysicsRuntime::UBodySetup*>& Setups, const PhysicsAssetEditing::FShapeRef& Shape);

        /**
         * @brief 선택한 바디와 그 바디에 연결된 관절, 반대편 바디를 노드 그래프로 표시합니다.
         * @details 노드를 누르면 바디나 관절이 선택됩니다.
         */
        void DrawGraph();

        /**
         * @brief 편집 중인 물리 에셋을 파일에 저장합니다.
         * @details 아직 파일이 없으면 "<메시 이름>_PhysicsAsset" 경로에 저장하고 이후 그 파일을 편집 대상으로 삼습니다.
         */
        void SaveAsset();

        /**
         * @brief 저장한 물리 에셋을 미리보기 메시의 기본 물리 에셋으로 지정하고 메시 파일을 저장합니다.
         * @note 본을 내장한 레거시 메시는 저장하면 본이 사라질 수 있어 거부합니다.
         */
        void AssignToMesh();

        /**
         * @brief 바닥을 만들고 모든 뼈 바디를 시뮬레이션으로 바꿔 물리를 시작합니다.
         * @details 시작 전에 물리 상태를 다시 만들어 애니메이션 자세에서 출발합니다.
         */
        void StartSimulation();

        /**
         * @brief 바닥을 제거하고 물리 상태를 다시 만들어 시뮬레이션 전 자세로 되돌립니다.
         */
        void StopSimulation();

        /**
         * @brief 선택한 관절의 움직임 방식, 한계 각도와 프레임 필드를 표시하고 관절을 추가·삭제합니다.
         */
        void DrawConstraintEditor();

        /**
         * @brief 선택한 관절의 두 프레임(위치와 세 축)을 뼈의 현재 월드 변환 기준 선으로 기록합니다.
         * @param[in,out] Draw 선을 기록할 그리기 인터페이스입니다.
         */
        void BuildConstraintLines(Engine::FPrimitiveDrawInterface& Draw) const;

        /**
         * @brief 모든 바디를 자동 생성으로 다시 만드는 설정과 확인 창을 표시합니다.
         */
        void DrawRegenerate();

        /**
         * @brief 뷰포트를 클릭한 위치에서 PhysX 광선을 쏘아 바디를 선택합니다.
         * @param[in] Mouse 뷰포트 이미지 왼쪽 위 기준의 마우스 위치(픽셀)입니다.
         * @param[in] Size 뷰포트 이미지 크기(픽셀)입니다.
         */
        void PickAt(const ImVec2& Mouse, const ImVec2& Size);

    private:

        /**
         * @brief 편집 대상과 공유 서비스에 접근하는 에디터 컨텍스트의 비소유 주소입니다.
         */
        Editor::FEditorContext* mContext{};

        /**
         * @brief 미리보기에 쓰는 스켈레탈 메시의 콘텐츠 경로입니다.
         */
        std::filesystem::path mMeshPath{};

        /**
         * @brief 편집 중인 물리 에셋 파일의 콘텐츠 경로이며, 파일이 없으면 비어 있습니다.
         */
        Core::FString mPhysicsAssetPath{};

        /**
         * @brief 편집 중인 스켈레탈 메시의 비소유 주소입니다.
         */
        Asset::USkeletalMesh* mMesh{};

        /**
         * @brief 미리보기에 사용 중인 스켈레톤의 비소유 주소입니다.
         */
        Asset::USkeleton* mSkeleton{};

        /**
         * @brief 사용 중인 스켈레톤 에셋의 콘텐츠 경로이며 메시에 내장된 스켈레톤이면 비어 있습니다.
         */
        Core::FString mSkeletonPath{};

        /**
         * @brief 미리보기 카메라와 렌더 타깃을 관리하는 공통 뷰어입니다.
         */
        Viewer::FAssetViewer mViewer;

        /**
         * @brief 뷰어가 소유하는 미리보기 콘텐츠의 비소유 주소입니다.
         */
        Editor::FPhysicsAssetViewerContent* mContent{};

        /**
         * @brief 마지막 작업의 오류 메시지입니다.
         */
        Core::FString mError{};

        /**
         * @brief 다음 그리기에서 카메라를 메시에 맞춰야 하는지 나타냅니다.
         */
        bool mNeedsFrame{true};

        /**
         * @brief 오른쪽 버튼을 누른 채 이동할 때의 카메라 속도이며 단위는 메시 반지름/초입니다.
         */
        float mCameraSpeed{1.0f};

        /**
         * @brief 스켈레톤 뼈마다 자식 뼈 번호를 담은 트리 표시용 목록입니다.
         */
        Core::TArray<Core::TArray<Index_t>> mBoneChildren{};

        /**
         * @brief 부모가 없는 뼈의 번호 목록입니다.
         */
        Core::TArray<Index_t> mRootBones{};

        /**
         * @brief 선택한 뼈의 번호이며 없으면 InvalidIndex입니다.
         */
        Index_t mSelectedBone{InvalidIndex};

        /**
         * @brief 가장 최근에 고른(대표) 바디 설명서의 번호(물리 에셋 안의 순서)이며 없으면 InvalidIndex입니다.
         */
        Index_t mSelectedBody{InvalidIndex};

        /**
         * @brief 선택한 모든 바디 설명서의 번호입니다. 대표 바디가 있으면 이 목록에 들어 있습니다.
         */
        Core::TArray<Index_t> mSelectedBodies{};

        /**
         * @brief 선택한 관절 설정의 번호이며 없으면 InvalidIndex입니다.
         */
        Index_t mSelectedConstraint{InvalidIndex};

        /**
         * @brief 선택한 바디 안의 항목이며 항목을 고르지 않았으면 유효하지 않은 값입니다.
         */
        PhysicsAssetEditing::FShapeRef mSelectedShape{};

        /**
         * @brief 바디·관절 그래프의 노드 에디터 컨텍스트이며 처음 그릴 때 만듭니다.
         */
        ax::NodeEditor::EditorContext* mGraphEditor{};

        /**
         * @brief Graph 창이 보여 줄 중심 바디들의 번호이며 선택과 따로 바뀝니다. 비어 있으면 선택한 바디를 씁니다.
         */
        Core::TArray<Index_t> mGraphFocus{};

        /**
         * @brief 그래프가 마지막으로 배치한 중심 바디들의 번호입니다.
         */
        Core::TArray<Index_t> mGraphLaidOutFocus{};

        /**
         * @brief 툴킷의 선택과 마지막으로 맞춘 그래프 노드 ID들이며 정렬되어 있습니다.
         */
        Core::TArray<std::uintptr_t> mGraphSyncedKeys{};

        /**
         * @brief 그래프를 화면에 맞추기 위해 남은 프레임 수입니다. 노드 크기가 정해지는 데 몇 프레임이 걸립니다.
         */
        int mGraphFitFrames{};

        /**
         * @brief 관절 프레임의 회전 필드가 가리키는 관절 번호이며, 다시 계산해야 하면 InvalidIndex입니다.
         */
        Index_t mFrameRotationConstraint{InvalidIndex};

        /**
         * @brief 편집 중인 관절의 Frame 1, Frame 2 회전(도)입니다.
         * @details 축에서 오일러 각을 매 프레임 다시 구하면 드래그 중 값이 튀므로 선택할 때 한 번만 구해 둡니다.
         */
        FRotator mFrameRotation[2]{};

        /**
         * @brief 에셋을 수정했지만 저장하지 않았는지 나타냅니다.
         */
        bool mDirty{};

        /**
         * @brief 자동 생성 다시 만들기에 쓸 설정입니다.
         */
        Asset::FPhysAssetCreateParams mRegenParams{};

        /**
         * @brief 다시 만들기 확인 창을 열어야 하는지 나타냅니다.
         */
        bool mOpenRegeneratePopup{};

        /**
         * @brief 핸들 표시, 교차 판정과 드래그 계산을 맡는 공통 기즈모 코어입니다.
         */
        Editor::FGizmoCore mGizmo{};

        /**
         * @brief 기즈모의 변환 모드(이동 또는 회전)입니다.
         */
        Editor::EGizmoMode mGizmoMode{Editor::EGizmoMode::Translate};

        /**
         * @brief 기즈모 축의 기준 좌표계입니다.
         * @details World는 월드 축, Bone은 바디가 붙은 뼈의 축(Details의 Center, Rotation, Position 값이 정의된 좌표계라서 필드의 X, Y, Z와 기즈모 축이 일치),
         *          Local은 선택한 항목(형상의 회전을 반영한 축)이나 관절 프레임의 축입니다.
         */
        enum class EGizmoAxes {
            World,
            Bone,
            Local
        };

        EGizmoAxes mGizmoAxes{EGizmoAxes::Bone};

        /**
         * @brief 관절을 골랐을 때 기즈모가 편집할 프레임입니다. 0은 Frame 1(자식 뼈), 1은 Frame 2(부모 뼈)입니다.
         */
        int mGizmoConstraintFrame{1};

        /**
         * @brief 이번 마우스 누름이 기즈모에서 시작했는지 나타냅니다. 뗄 때 클릭 선택과 카메라 조작을 막는 데 씁니다.
         */
        bool mGizmoUsed{};

        /**
         * @brief 드래그 중인 항목들의 시작 상태입니다.
         */
        Core::TArray<FGizmoShapeState> mGizmoShapes{};

        /**
         * @brief 드래그 중인 관절 프레임의 시작 상태입니다.
         */
        FGizmoFrameState mGizmoFrame{};

        /**
         * @brief 이번 드래그가 관절 프레임을 편집하는지(true) 항목을 편집하는지(false) 나타냅니다.
         */
        bool mGizmoEditsFrame{};

        /**
         * @brief 시뮬레이션 중 Ctrl + 우클릭으로 잡고 끄는 바디입니다. 물리 상태를 내리기 전에 반드시 놓아야 합니다.
         */
        PhysicsAssetEditing::FBodyGrab mGrab{};

        /**
         * @brief 시뮬레이션이 불안정해져 멈춘 상태인지 나타냅니다. true이면 스텝을 진행하지 않고 마지막 자세를 보여 줘 원인을 볼 수 있습니다.
         */
        bool mSimulationHalted{};

        /**
         * @brief 시뮬레이션을 멈춘 이유와 문제가 된 바디의 설명입니다.
         */
        Core::FString mSimulationWarning{};

        /**
         * @brief 시뮬레이션을 시작한 직후 각 바디의 월드 위치입니다. 바디 번호로 찾으며, 얼마나 움직였는지 표시하는 기준입니다.
         */
        Core::TMap<Index_t, FVector> mSimulationStartPositions{};

        /**
         * @brief 충돌 쌍 편집의 마지막 결과 안내(예: 자동 설정으로 끈 쌍의 개수)입니다.
         */
        Core::FString mCollisionMessage{};

        /**
         * @brief 뷰포트의 화면 채우기 방식입니다.
         */
        Engine::EViewMode mViewMode{Engine::EViewMode::Lit};

        /**
         * @brief 편집 값을 되돌릴 기준입니다. 비어 있으면 다음 Details 표시 때 현재 값으로 다시 만듭니다.
         */
        std::unique_ptr<FBaseline> mBaseline{};

        /**
         * @brief 뷰포트에 바디 형상을 그릴지 나타냅니다.
         */
        bool mShowBodies{true};

        /**
         * @brief Hierarchy에 모든 뼈를 표시할지 나타냅니다. false이면 바디가 있는 뼈만 트리로 표시합니다.
         */
        bool mShowAllBones{true};

        /**
         * @brief 미리보기에서 물리를 진행 중인지 나타냅니다.
         */
        bool mSimulating{};

        /**
         * @brief 시뮬레이션을 시작할 때의 메시 월드 범위이며 바닥 위치를 정합니다.
         * @note 시뮬레이션 중 메시가 움직여도 바닥이 따라 움직이지 않도록 시작 시점 값을 고정합니다.
         */
        FBox mFloorBounds{};

        /**
         * @brief 시뮬레이션 중에만 존재하는 바닥 바디입니다.
         * @note 물리 장면보다 먼저 해제되도록 가장 마지막에 선언합니다.
         */
        std::unique_ptr<PhysicsRuntime::FBodyInstance> mFloorBody{};
    };

}
