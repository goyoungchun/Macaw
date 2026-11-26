/**
 * @file FMeshViewer.h
 * @brief 에디터와 독립 실행 모드에서 공유하는 정적·스켈레탈 메시 뷰어입니다.
 * @ingroup Editor
 */

#pragma once

#include "CoreUObject/TObjectRef.h"

#include "Core/Input.h"
#include "Viewer/FAssetViewer.h"

#include "Core/ReflectionMacros.h"

#include "Core/Types.h"
#include "Core/Guid.h"
#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FRotator.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector2.h"
#include "Core/Math/FMatrix.h"
#include "Viewer/FFlyCameraController.h"
#include "Editor/FGizmoCore.h"
#include "Engine/EEditorMesh.h"
#include "Engine/FSceneView.h"
#include "Engine/EViewMode.h"
#include "Engine/FSkeletalMeshPose.h"
#include "PhysicsRuntime/FClothingSimulationContextCommon.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace Asset {
    class FAssetManager;
    class UStaticMesh;
    class USkeletalMesh;
    class USkeleton;
    class UMaterial;
}

namespace RenderCore {
    class IRendererModule;
}

namespace Engine {
    class FPrimitiveDrawInterface;
    class FSceneInterface;
    class FRenderTarget;
}

namespace Viewer {
    class FBoneShape;
    class FMeshViewerContent;
}

namespace PhysicsRuntime {
    class FClothingSystemRuntimeModuleNv;
    class IClothingSimulation;
    class UClothConfigNv;
    class UClothingAssetCommon;
}

namespace Editor {

    class FEditorTab;

    /**
     * @brief 편집 월드와 분리한 렌더 장면, 카메라와 재질 미리보기를 소유합니다.
     */
    class FMeshViewer {
    public:

        /**
         * @brief 렌더러와 프로젝트 에셋 관리자를 연결합니다.
         * @param Renderer 렌더러의 비소유 주소입니다.
         * @param Assets 에셋 관리자의 비소유 주소입니다.
         */
        FMeshViewer(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets);

        /**
         * @brief 미리보기 장면과 렌더 타깃을 해제합니다.
         */
        ~FMeshViewer();

        /**
         * @brief GPU 미리보기 자원의 복사를 금지합니다.
         * @param Other 복사할 뷰어입니다.
         */
        FMeshViewer(const Editor::FMeshViewer& Other) = delete;

        /**
         * @brief GPU 미리보기 자원의 복사를 금지합니다.
         * @param Other 대입할 뷰어입니다.
         * @return 이 객체의 참조입니다.
         */
        Editor::FMeshViewer& operator=(const Editor::FMeshViewer& Other) = delete;

    public:

        /**
         * @brief 에셋을 독립 미리보기 장면에 엽니다.
         * @param Path Content 기준 또는 절대 메시 에셋 경로입니다.
         * @param Error 실패 설명을 받습니다.
         * @return 미리보기를 구성하면 true입니다.
         */
        bool Open(const std::filesystem::path& Path, Core::FString& Error);

        /**
         * @brief 카메라·재질 조작 UI와 미리보기를 표시합니다.
         * @param Standalone 독립 실행용 전체 화면 창이면 true입니다.
         * @param[in] Input 키·마우스 상태를 조회할 공통 입력입니다.
         */
        void DrawUI(const Core::FInput& Input, bool Standalone = false);

        /**
         * @brief 호스트 주 탭에 편집기 위젯과 도킹 배치를 등록합니다.
         * @param[in] Tab 편집기를 포함할 호스트 탭 또는 등록할 외부 소유 탭입니다.
         * @param[in] Input 이번 프레임의 키·마우스 입력 상태입니다.
         */
        void InitializeToolkit(Editor::FEditorTab& Tab, const Core::FInput& Input);

        /**
         * @brief 툴킷에 포함된 편집기 상태와 저장 가능 여부를 프레임별로 갱신합니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         * @param[in] CanSave 현재 편집 내용을 저장할 수 있는지 지정합니다.
         */
        void UpdateToolkit(float DeltaTime, bool CanSave);

        /**
         * @brief 창의 열림 상태를 검사합니다.
         * @return 뷰어가 열려 있으면 true입니다.
         */
        bool IsOpen() const;

        /**
         * @brief 이 에디터 창의 ImGui 식별자를 지정하여 같은 종류의 에디터를 여러 탭으로 열 수 있게 합니다.
         * @param[in] Id 세 개의 해시 기호로 시작하는 ImGui 창 식별자입니다.
         */
        void SetWindowId(Core::FString Id);

        /**
         * @brief 이 에디터 창의 ImGui 식별자를 반환합니다.
         * @return 호스트 창을 구분하는 ImGui 창 식별자 문자열입니다.
         */
        const Core::FString& GetWindowId() const;

        /**
         * @brief 열린 메시나 스켈레톤이 지정한 경로 아래에 있으면 뷰어를 닫습니다.
         * @details 에셋을 지우거나 옮겨 캐시에서 은퇴시킬 때 호출해, 옛 에셋을 계속 편집하지 않게 합니다.
         * @param[in] Path 은퇴시킨 에셋 파일 또는 폴더 경로입니다.
         */
        void CloseRetired(const std::filesystem::path& Path);

        /**
         * @brief 전체 메시가 보이도록 카메라를 배치합니다.
         */
        void FrameMesh();

        /**
         * @brief 미리 보기에서 천을 움직일 천 시스템을 연결합니다.
         * @details 연결하지 않으면 천 칸에서 천을 만들고 칠할 수는 있지만 미리 보기에서 천이 움직이지 않습니다.
         * @param[in] ClothingSystem 엔진이 소유하는 천 시스템이며 nullptr이면 미리 보기 시뮬레이션을 끕니다.
         */
        void SetClothingSystem(PhysicsRuntime::FClothingSystemRuntimeModuleNv* ClothingSystem);

    private:

        /**
         * @brief 현재 메시와 슬롯별 재질을 미리보기 장면에 반영합니다.
         */
        void UpdateScene();

        /**
         * @brief 슬롯별 임시 재질 선택 UI를 표시합니다.
         */
        void DrawMaterials();

        /**
         * @brief 스켈레탈 메시의 미리보기 스켈레톤 선택기를 표시합니다.
         */
        void DrawSkeletonPicker();

        /**
         * @brief 미리보기 옆에 본 계층 선택·기즈모 설정·포즈 복원 패널을 표시합니다.
         * @param[in] FillWidth 포즈 표시가 사용 가능한 가로 영역을 채울지 지정합니다.
         */
        void DrawPose(bool FillWidth = false);

        /**
         * @brief 메시 미리 보기의 정보와 표시 속성을 그립니다.
         */
        void DrawProperties();

        /**
         * @brief 천 칸 전체를 그립니다. 섹션 표, 미리 보기 조작, 고른 천의 탭, 저장 순서입니다.
         */
        void DrawClothing();

        /**
         * @brief 정적 메시의 단순 충돌 형상(상자, 구, 캡슐)을 편집하는 칸을 그립니다. 추가, 삭제, 값 편집, 메시 경계에 맞추기, 저장입니다.
         */
        void DrawCollision();

        /**
         * @brief 정적 메시의 충돌 형상을 메시 로컬 공간의 선으로 그립니다. 고른 형상은 주황색입니다.
         * @param[in,out] Draw 선을 추가할 편집 표시 목록입니다.
         */
        void BuildCollisionLines(Engine::FPrimitiveDrawInterface& Draw) const;

        /**
         * @brief 섹션마다 머티리얼, 천 상태, 천 만들기 버튼을 표로 그리고 천을 고르게 합니다.
         */
        void DrawClothSections();

        /**
         * @brief 고른 천 에셋의 설정을 묶음별 표로 그리고 바꾼 값을 미리 보기에 반영합니다.
         * @param[in,out] Config 고른 천 에셋의 설정입니다.
         */
        void DrawClothSettings(PhysicsRuntime::UClothConfigNv& Config);

        /**
         * @brief 칠하기 켜기, 대상, 도구, 값, 붓 크기, 색 범례를 그립니다.
         * @param[in,out] ClothingAsset 칠할 천 에셋입니다.
         */
        void DrawClothPaintSettings(PhysicsRuntime::UClothingAssetCommon& ClothingAsset);

        /**
         * @brief 천이 부딪힐 물리 에셋을 고르는 칸과 충돌체 수를 그립니다.
         * @param[in,out] ClothingAsset 물리 에셋을 바꿀 천 에셋입니다.
         */
        void DrawClothCollision(PhysicsRuntime::UClothingAssetCommon& ClothingAsset);

        /**
         * @brief 칠할 천 메시를 가중치 색 와이어프레임으로 그리고, 왼쪽 버튼으로 고른 도구를 적용합니다.
         * @details 마우스 광선이 천 표면에 처음 맞은 점을 붓 중심으로 삼고, 그 점에서 3D 반지름 안의 입자만 바꿉니다.
         *          천 메시는 바인드 포즈 위치에 그리며, 버튼을 놓으면 칠한 값을 천 에셋에 반영합니다.
         * @param[in] View 미리 보기의 뷰입니다.
         * @param[in] Origin 미리 보기 이미지 왼쪽 위의 화면 위치입니다.
         * @param[in] Mouse 미리 보기 안의 마우스 위치입니다.
         * @param[in] Hovered 마우스가 미리 보기 위에 있는지 나타냅니다.
         * @param[in] Input 이번 프레임의 입력입니다.
         * @param[in,out] DrawInterface 와이어프레임과 붓 원을 담을 편집 표시 목록입니다.
         */
        void DrawClothPaint(const Engine::FSceneView& View, const FVector2& Origin, const FVector2& Mouse, bool Hovered, const Core::FInput& Input, Engine::FPrimitiveDrawInterface& DrawInterface);

        /**
         * @brief 천 칠하기를 쓸 수 있는 상태인지 검사합니다.
         * @return 칠하기를 켰고 고른 천 에셋이 있으면 true입니다.
         */
        bool IsClothPainting() const;

        /**
         * @brief 고른 천 에셋을 반환합니다.
         * @return 고른 천 에셋이며 없으면 nullptr입니다.
         */
        PhysicsRuntime::UClothingAssetCommon* GetSelectedClothingAsset() const;

        /**
         * @brief 천 에셋이 바뀌었음을 기록하고 미리 보기 천을 다시 만들게 합니다.
         * @param[in] Message 천 칸 아래에 잠시 보여 줄 결과 문장입니다.
         */
        void MarkClothChanged(const Core::FString& Message);

        /**
         * @brief 천 칸 아래에 결과 문장을 잠시 보여 줍니다.
         * @param[in] Message 보여 줄 문장입니다.
         */
        void ShowClothMessage(const Core::FString& Message);

        /**
         * @brief 미리 보기 천 시뮬레이션을 지금 천 에셋과 포즈로 다시 만듭니다.
         */
        void RecreateClothPreview();

        /**
         * @brief 미리 보기 천 시뮬레이션을 해제하고 천 섹션을 스키닝 위치로 그리게 합니다.
         */
        void ReleaseClothPreview();

        /**
         * @brief 미리 보기 천을 한 프레임 진행하고 결과를 미리 보기 장면에 넣습니다.
         * @param[in] DeltaTime 지난 프레임부터 지난 시간(초)입니다.
         */
        void TickClothPreview(float DeltaTime);

        /**
         * @brief 미리 보기 포즈로 천 시뮬레이션 입력을 채웁니다.
         * @param[in] DeltaTime 이번에 진행할 시간(초)이며 처음 만들 때는 0입니다.
         */
        void UpdateClothPreviewContext(float DeltaTime);

        /**
         * @brief 본 하나와 그 자손을 계층 트리 노드로 표시하고 클릭하면 조작 대상으로 선택합니다.
         * @param Bone 표시할 본 번호입니다.
         * @param Children 본 번호별 자식 본 번호 목록입니다.
         */
        void DrawBoneNode(Index_t Bone, const Core::TArray<Core::TArray<Index_t>>& Children);

        /**
         * @brief 마우스 위치의 광선과 본 표시 도형을 비교하여 선택 본을 갱신합니다.
         * @param View 표시할 그래프 보기 또는 장면 뷰입니다.
         * @param Mouse 뷰포트 기준 마우스 좌표(픽셀)입니다.
         * @param Shapes 본 선택에 사용할 도형 목록입니다.
         * @return 선택할 본 도형과 교차했으면 true입니다.
         */
        bool PickBone(const Engine::FSceneView& View, const FVector2& Mouse, const Core::TArray<Viewer::FBoneShape>& Shapes);

        /**
         * @brief 현재 궤도 카메라의 렌더링과 입력 해석에 쓸 뷰를 계산합니다.
         * @param Size 미리보기의 픽셀 크기입니다.
         * @return 계산한 카메라 뷰입니다.
         */
        Engine::FSceneView CalcSceneView(const FVector2& Size) const;

        /**
         * @brief 선택한 본의 현재 전역 포즈로 기즈모 배치를 구성합니다.
         * @param Bone 월드 위치·회전을 계산할 스켈레톤 본 인덱스입니다.
         * @return 공통 코어에 전달할 기즈모 배치입니다.
         */
        Editor::FGizmoPlacement GetBonePlacement(Index_t Bone) const;

        /**
         * @brief 기즈모의 호버와 드래그 입력을 처리합니다.
         * @param View 입력 해석에 사용할 카메라 뷰입니다.
         * @param Mouse 미리보기 기준 마우스 픽셀 좌표입니다.
         * @param Hovered 마우스가 미리보기 위에 있으면 true입니다.
         * @param Bone 입력으로 조작할 스켈레톤 본 인덱스입니다.
         * @return 기즈모가 마우스를 사용 중이어서 카메라 조작을 막아야 하면 true입니다.
         * @param[in] Input 키·마우스 상태를 조회할 공통 입력입니다.
         */
        bool UpdateBoneGizmo(Index_t Bone, const Core::FInput& Input, const Engine::FSceneView& View, const FVector2& Mouse, bool Hovered);

        /**
         * @brief 기즈모의 드래그 결과를 선택한 본의 부모 기준 로컬 변환으로 바꾸어 적용합니다.
         * @param Bone 기즈모 변환을 적용할 스켈레톤 본 인덱스입니다.
         */
        void ApplyBoneGizmo(Index_t Bone);

        /**
         * @brief 미리 보기 위에 보기 방식 선택 칸과 표시 항목 메뉴를 그립니다.
         */
        void DrawViewportToolbar();

        /**
         * @brief 카메라 입력을 처리하고 공통 렌더 경로로 메시와 본 도형을 그리며 아이콘·스프라이트·텍스트 표시는 제외합니다.
         * @param[in] Input 키·마우스 상태를 조회할 공통 입력입니다.
         */
        void DrawPreview(const Core::FInput& Input);

    private:

        /**
         * @brief 프로젝트 에셋 관리자의 비소유 주소입니다.
         */
        Asset::FAssetManager* mAssets{};

        /**
         * @brief 카메라·렌더 타깃·촬영을 담당하는 공통 에셋 뷰어입니다.
         */
        Viewer::FAssetViewer mViewer;

        /**
         * @brief 공통 뷰어가 소유하는 메시 콘텐츠의 비소유 포인터입니다.
         */
        Viewer::FMeshViewerContent* mContent{};

        /**
         * @brief 다음 표시에서 에셋 경계에 카메라를 맞출지 여부입니다.
         */
        bool mNeedsFit{true};

        /**
         * @brief 현재 메시의 비소유 주소입니다.
         */
        CoreUObject::TObjectRef<Asset::UStaticMesh> mMesh{};

        /**
         * @brief 스켈레탈 미리보기에서 사용하는 공유 에셋입니다.
         */
        CoreUObject::TObjectRef<Asset::USkeletalMesh> mSkeletalMesh{};

        /**
         * @brief 에셋 관리자가 소유하는 미리보기 스켈레톤입니다.
         */
        CoreUObject::TObjectRef<Asset::USkeleton> mSkeleton{};

        /**
         * @brief 스켈레톤 에셋 경로입니다.
         */
        Core::FString mSkeletonPath{};

        /**
         * @brief 본 조작에 사용하는 기즈모 표시와 드래그 계산입니다.
         */
        Editor::FGizmoCore mGizmo{};

        /**
         * @brief 본 기즈모의 변환 모드입니다.
         */
        Editor::EGizmoMode mGizmoMode{Editor::EGizmoMode::Rotate};

        /**
         * @brief 본 기즈모 축의 좌표계입니다.
         */
        Editor::EGizmoSpace mGizmoSpace{Editor::EGizmoSpace::Local};

        /**
         * @brief 드래그 시작 시 선택한 본의 전역 변환입니다.
         */
        FMatrix mDragStartGlobal{};

        /**
         * @brief 드래그를 취소할 때 복원할 선택한 본의 로컬 변환입니다.
         */
        FMatrix mDragStartLocal{};

        /**
         * @brief 기즈모 조작 대상으로 선택한 본입니다.
         */
        std::optional<Index_t> mPoseBone{};

        /**
         * @brief 현재 본 계층을 보조 도형으로 표시할지 지정합니다.
         */
        bool mShowBones{true};

        /**
         * @brief 미리보기 슬롯별 오버라이드 경로입니다.
         */
        Core::TArray<Core::FString> mMaterialPaths{};

        /**
         * @brief 미리보기 에셋의 표시 경로입니다.
         */
        Core::FString mPath{};

        /**
         * @brief 이 뷰어 창의 ImGui 식별자입니다.
         */
        Core::FString mWindowId{"###Mesh Viewer"};

        /**
         * @brief 편집기를 포함하는 호스트 주 탭의 비소유 주소입니다.
         */
        Editor::FEditorTab* mHostTab{};

        /**
         * @brief 최근 미리보기 실패의 설명입니다.
         */
        Core::FString mError{};

        /**
         * @brief 천 칸에서 마지막으로 한 작업의 결과입니다.
         */
        Core::FString mClothMessage{};

        /**
         * @brief 결과 문장을 보여 주기 시작한 ImGui 시각(초)입니다.
         */
        double mClothMessageTime{};

        /**
         * @brief 천 에셋을 바꾼 뒤 아직 메시 파일에 저장하지 않았는지 나타냅니다.
         */
        bool mClothUnsaved{};

        /**
         * @brief 천 칸에서 고른 천 에셋 번호입니다.
         */
        Index_t mClothSelectedAsset{};

        /**
         * @brief 충돌 형상을 미리 보기에 그릴지 나타냅니다.
         */
        bool mShowCollision{true};

        /**
         * @brief 충돌 칸에서 고른 형상의 종류입니다. 0은 없음, 1은 구, 2는 상자, 3은 캡슐입니다.
         */
        Uint32 mCollisionShapeType{};

        /**
         * @brief 충돌 칸에서 고른 형상의 같은 종류 안의 번호입니다.
         */
        Index_t mCollisionShapeIndex{};

        /**
         * @brief 저장하지 않은 충돌 형상 편집이 있는지 나타냅니다.
         */
        bool mCollisionUnsaved{};

        /**
         * @brief 충돌 칸의 마지막 저장 결과나 오류 안내입니다.
         */
        Core::FString mCollisionMessage{};

        /**
         * @brief 엔진이 소유하는 천 시스템이며 없으면 미리 보기에서 천이 움직이지 않습니다.
         */
        PhysicsRuntime::FClothingSystemRuntimeModuleNv* mClothingSystem{};

        /**
         * @brief 미리 보기에서 천을 움직이는 시뮬레이션입니다.
         */
        std::unique_ptr<PhysicsRuntime::IClothingSimulation> mClothPreview{};

        /**
         * @brief 미리 보기 천 시뮬레이션에 매 프레임 넣는 입력입니다.
         */
        PhysicsRuntime::FClothingSimulationContextCommon mClothPreviewContext{};

        /**
         * @brief 미리 보기에서 천을 움직일지 나타냅니다.
         */
        bool mClothSimulate{true};

        /**
         * @brief 다음 프레임에 미리 보기 천을 다시 만들어야 하는지 나타냅니다.
         */
        bool mClothPreviewStale{true};

        /**
         * @brief Show 메뉴에서 바람 세기를 0으로 내렸다가 다시 올릴 때 쓸 바람 방향입니다.
         */
        FVector mClothWindDirection{1.0f, 0.0f, 0.0f};

        /**
         * @brief 미리 보기에서 천 가중치를 칠하는지 나타냅니다.
         */
        bool mClothPaintEnabled{};

        /**
         * @brief 고른 칠하기 도구입니다.
         */
        int mClothPaintTool{};

        /**
         * @brief 다듬기 도구가 한 프레임에 이웃 평균 쪽으로 옮기는 비율입니다.
         */
        float mClothSmoothStrength{0.2f};

        /**
         * @brief 채우기 도구가 시작 값과 이만큼까지 다른 입자를 채우는 기준입니다.
         */
        float mClothFillThreshold{0.01f};

        /**
         * @brief 마우스 아래 입자 번호이며 없으면 InvalidIndex입니다.
         */
        Index_t mClothHoveredParticle{InvalidIndex};

        /**
         * @brief 마우스 아래 입자의 칠한 값입니다.
         */
        float mClothHoveredValue{};

        /**
         * @brief 색 범례의 가장 큰 값이며 칠한 값 중 최댓값입니다.
         */
        float mClothPaintMaxValue{};

        /**
         * @brief 칠할 가중치 맵 대상 번호입니다.
         */
        int mClothPaintTarget{};

        /**
         * @brief 붓으로 칠할 값입니다. 최대 거리와 뒤쪽 거리, 반지름은 미터입니다.
         */
        float mClothPaintValue{};

        /**
         * @brief 붓의 3D 반지름(m)이며 천 표면에 맞은 점에서 이 거리 안의 입자를 칠합니다.
         */
        Core::FLength mClothPaintRadius{Core::FLength::FromBase(0.05f)};

        /**
         * @brief 카메라 반대쪽을 향한 삼각형의 와이어프레임을 그리지 않을지 나타냅니다.
         */
        bool mClothCullBackFaces{true};

        /**
         * @brief 카메라를 향한 입자만 칠할지 나타냅니다.
         */
        bool mClothFrontFacingOnly{true};

        /**
         * @brief 칠한 뒤 아직 천 에셋에 반영하지 않은 값이 있는지 나타냅니다.
         */
        bool mClothPaintDirty{};

        /**
         * @brief 뷰어의 표시 모드입니다.
         */
        Engine::EViewMode mViewMode{Engine::EViewMode::Lit};

        /**
         * @brief 창의 열림 여부입니다.
         */
        bool mOpen{};

        /**
         * @brief 미리보기에서 시작한 마우스 드래그의 캡처 상태입니다.
         */
        bool mMouseCaptured{};

        /**
         * @brief 오른쪽 버튼을 누른 동안 시선 회전과 WASD/QE 이동을 처리하는 비행 카메라 컨트롤러입니다.
         */
        Viewer::FFlyCameraController mFlyCamera{};
    };

}
