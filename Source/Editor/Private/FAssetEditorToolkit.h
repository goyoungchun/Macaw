/**
 * @file FAssetEditorToolkit.h
 * @brief 에셋 경로와 주 탭을 보관하고 편집기의 갱신·명령·변경 통지를 연결합니다.
 * @ingroup Editor
 */

#pragma once

#include "Core/Types.h"
#include "FMessageBox.h"
#include "Asset/FAssetFile.h"
#include "Editor/FEditorToolkit.h"
#include "Editor/FEditorTab.h"
#include <filesystem>
#include <functional>
#include <optional>

namespace Editor {
    /**
     * @brief 에셋 경로와 주 탭을 보관하고 편집기의 갱신·명령·변경 통지를 연결합니다.
     */
    class FAssetEditorToolkit : public Editor::FEditorToolkit {
    public:
        /**
         * @brief FAssetEditorToolkit의 초기 상태와 전달받은 의존성을 구성합니다.
         * @param[in] Path 대상 에셋 또는 콘텐츠 경로입니다.
         * @param[in] Type 편집할 에셋의 종류입니다.
         * @param[in] WindowName 탭 콘텐츠에 사용할 고유 ImGui 창 이름입니다.
         */
        FAssetEditorToolkit(std::filesystem::path Path, Asset::EAssetType Type, Core::FString WindowName);

        /**
         * @brief 객체를 소멸하고 소유 멤버와 연결 상태를 정리합니다.
         */
        ~FAssetEditorToolkit() override;

    public:
        /**
         * @brief 에셋 툴킷의 프레임 갱신 진입점이며 기본 구현은 아무 작업도 하지 않습니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         */
        void Tick(float DeltaTime) override;

        /**
         * @brief 표시 중인 에셋 편집기 상태를 갱신하고 주 탭과 내부 도킹 공간을 그립니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         */
        void Draw(float DeltaTime) override;

        /**
         * @brief 폐기된 에셋 경로를 참조하는 편집기 콘텐츠를 닫거나 해제합니다.
         * @param[in] Absolute 폐기 또는 경로 작업 대상의 절대 경로입니다.
         */
        virtual void CloseRetired(const std::filesystem::path& Absolute);
        virtual std::optional<Editor::FMessageBox::FSavePrompt> GetClosePrompt();

        /**
         * @brief 편집 중인 에셋의 경로를 반환합니다.
         * @return 툴킷이 보관한 에셋 경로입니다.
         */
        const std::filesystem::path& GetAssetPath() const;

        /**
         * @brief 편집 중인 에셋의 종류를 반환합니다.
         * @return 툴킷이 보관한 에셋 종류입니다.
         */
        Asset::EAssetType GetAssetType() const;

        /**
         * @brief 연결된 주 탭을 반환합니다.
         * @return 위젯 또는 툴킷에 연결된 탭의 참조입니다.
         */
        Editor::FEditorTab& GetTab();

        /**
         * @brief 주 탭 내부의 툴킷 호스트를 조회하거나 생성합니다.
         * @return 주 탭이 소유하는 호스트의 참조입니다.
         */
        Editor::FEditorToolkitHost& GetToolkitHost();

        /**
         * @brief 변경이 발생했을 때 호출할 알림 콜백을 지정합니다.
         * @param[in] Handler 변경 내용을 알릴 콜백입니다.
         */
        void SetChangedHandler(std::function<void()> Handler);

        /**
         * @brief 등록된 콜백으로 편집 내용 변경을 알립니다.
         */
        void NotifyChanged();

    private:
        /**
         * @brief 에셋 편집기의 프레임별 상태를 갱신합니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         */
        virtual void UpdateEditor(float DeltaTime) = 0;

        /**
         * @brief 파생 툴킷의 단축키 처리 진입점이며 기본 구현은 아무 작업도 하지 않습니다.
         */
        virtual void HandleCommands();

    private:
        /**
         * @brief 편집 중인 에셋의 경로입니다.
         */
        std::filesystem::path mPath{};

        /**
         * @brief 편집 중인 에셋의 종류입니다.
         */
        Asset::EAssetType mType{};

        /**
         * @brief 에셋 편집기의 주 탭과 내부 툴킷 호스트입니다.
         */
        Editor::FEditorTab mTab;

        /**
         * @brief 편집 내용 또는 설정 변경을 알리는 콜백입니다.
         */
        std::function<void()> mChanged{};
    };
}
