/**
 * @file FEditorShell.h
 * @brief 주 메뉴·도구 모음·상태 표시줄과 실행 제어를 그립니다.
 * @ingroup Editor
 */

#pragma once

#include "FLevelEditorToolkit.h"
#include "Editor/FConsoleCommandInput.h"
#include "Editor/Widget/FEditorButtonWidget.h"
#include <wrl/client.h>

struct ID3D11ShaderResourceView;
namespace RenderCore {
    class IRendererModule;
}

namespace Editor {

    /**
     * @brief 주 메뉴·도구 모음·상태 표시줄과 실행 제어를 그립니다.
     */
    class FLevelEditorToolkit::FEditorShell {

    public:
        /**
         * @brief FEditorShell의 초기 상태와 전달받은 의존성을 구성합니다.
         */
        explicit FEditorShell(Editor::FLevelEditorToolkit& Toolkit);

        /**
         * @brief 객체를 소멸하고 소유 멤버와 연결 상태를 정리합니다.
         */
        ~FEditorShell();

    public:
        /**
         * @brief 에디터 또는 프로젝트 브라우저의 브랜드 이미지 리소스를 로드합니다.
         * @param[in] Renderer 렌더 대상과 GPU 리소스를 제공하는 렌더러입니다.
         */
        void LoadBranding(RenderCore::IRendererModule* Renderer);

        /**
         * @brief 등록된 탭 또는 에디터 명령을 메뉴 항목으로 표시합니다.
         */
        void DrawMenu();

        /**
         * @brief 레벨 편집 도구와 실행 제어를 도구 모음에 표시합니다.
         */
        void DrawEditorToolbar();

        /**
         * @brief 하단 상태 정보와 드로어 버튼·콘솔 명령 입력을 표시합니다.
         */
        void DrawStatusBar();

        /**
         * @brief 그림자 아틀라스 미리 보기와 확대 제어를 표시합니다.
         */
        void DrawShadowAtlas();

    private:
        /**
         * @brief 주 창의 최소화·최대화·닫기 버튼을 그립니다.
         */
        void DrawWindowControls();

        /**
         * @brief 레벨에 액터를 추가하는 메뉴를 그립니다.
         */
        void DrawAddMenu();

        /**
         * @brief PIE 시작·정지·일시 정지와 시뮬레이션 제어를 표시합니다.
         */
        void DrawPlayControls();

        /**
         * @brief PIE 시작 위치·입력·별도 창 옵션을 편집합니다.
         */
        void DrawPlayOptions();

    private:
        Editor::FLevelEditorToolkit& mToolkit;

        /**
         * @brief 에디터 브랜드 로고의 GPU 텍스처 뷰입니다.
         */
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mBrandLogo{};

        /**
         * @brief 상태 표시줄의 콘솔 명령 입력 위젯입니다.
         */
        Editor::FConsoleCommandInput mStatusCommand;

        /**
         * @brief 그림자 아틀라스 미리 보기 확대 배율입니다.
         */
        Editor::FEditorButtonWidget mSaveButton;
        Editor::FEditorButtonWidget mAddButton;
        Editor::FEditorButtonWidget mSequenceButton;
        Editor::FEditorButtonWidget mSettingsButton;
        Editor::FEditorButtonWidget mPlayButton;
        Editor::FEditorButtonWidget mPauseButton;
        Editor::FEditorButtonWidget mStepButton;
        Editor::FEditorButtonWidget mStopButton;
        Editor::FEditorButtonWidget mSimulateButton;
        Editor::FEditorButtonWidget mPlayOptionsButton;
        float mShadowAtlasZoom{1.0f};
    };

}
