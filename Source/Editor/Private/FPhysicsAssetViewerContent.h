/**
 * @file FPhysicsAssetViewerContent.h
 * @brief 물리 에셋 편집기가 스켈레탈 메시와 물리 장면을 담아 두는 미리보기 뷰어 콘텐츠를 정의합니다.
 * @ingroup Editor
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FBox.h"
#include "CoreUObject/TObjectRef.h"
#include "Viewer/IViewerContent.h"

#include <memory>

namespace Asset {
    class FAssetManager;
    class USkeletalMesh;
    class USkeleton;
}

namespace Engine {
    class FSceneInterface;
    class UWorld;
    class USkeletalMeshComponent;
}

namespace PhysicsRuntime {
    class FPhysicsRuntime;
}

namespace RenderCore {
    class IRendererModule;
}

namespace Viewer {
    class FPreviewScene;
}

namespace Editor {

    /**
     * @brief 미리보기 월드에 스켈레탈 메시를 놓고 뼈별 바디를 만들 물리 장면까지 갖춘 뷰어 콘텐츠입니다.
     * @details 애니메이션 시퀀스 없이 레퍼런스 포즈를 보여 주며, 월드에 물리 장면이 생기면 컴포넌트가 물리 에셋의 바디와 관절을 만듭니다.
     * @ingroup Editor
     */
    class FPhysicsAssetViewerContent final : public Viewer::IViewerContent {
    public:

        /**
         * @brief 렌더러와 에셋 관리자를 참조하는 빈 콘텐츠를 구성합니다.
         * @param Renderer 외부에서 소유하는 렌더러 서비스입니다.
         * @param Assets 외부에서 소유하는 에셋 관리자입니다.
         */
        FPhysicsAssetViewerContent(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets);

        /**
         * @brief 미리보기 월드와 물리 장면을 해제합니다.
         */
        ~FPhysicsAssetViewerContent() override;

    public:

        /**
         * @brief 메시와 스켈레톤으로 미리보기 월드를 만들고 물리 장면을 연결합니다.
         * @param Mesh 호출자가 수명을 보장하는 스켈레탈 메시입니다.
         * @param Skeleton 메시와 호환되며 수명이 보장되는 스켈레톤입니다.
         * @param Runtime 물리 장면을 만들 PhysX 런타임이며, nullptr이면 물리 없이 표시합니다.
         * @param Error 실패 원인을 기록할 출력 문자열입니다.
         * @return 미리보기를 준비했으면 true이며, 실패하면 기존 콘텐츠를 유지합니다.
         */
        bool SetMesh(Asset::USkeletalMesh* Mesh, Asset::USkeleton* Skeleton, PhysicsRuntime::FPhysicsRuntime* Runtime, Core::FString& Error);

        /**
         * @brief 미리보기 월드를 반환합니다.
         * @return 콘텐츠가 소유하는 월드의 비소유 포인터이며 준비 전에는 nullptr입니다.
         */
        Engine::UWorld* GetWorld() const;

        /**
         * @brief 미리보기 스켈레탈 메시 컴포넌트를 반환합니다.
         * @return 미리보기 월드 소유의 비소유 포인터이며 준비 전에는 nullptr입니다.
         */
        Engine::USkeletalMeshComponent* GetComponent() const;

        /**
         * @brief 미리보기 월드에 물리 장면이 있는지 반환합니다.
         * @return 물리 장면이 만들어졌으면 true입니다.
         */
        bool HasPhysicsScene() const;

        /**
         * @brief 컴포넌트가 준비되어 렌더링할 수 있는지 반환합니다.
         * @return 렌더링할 수 있으면 true입니다.
         */
        bool IsReady() const override;

        /**
         * @brief 현재 포즈를 감싸는 월드 공간 경계를 반환합니다.
         * @return 콘텐츠 수명에 종속되는 경계의 읽기 전용 참조입니다.
         */
        const FBox& GetBounds() const override;

        /**
         * @brief 렌더링에 사용할 미리보기 장면을 반환합니다.
         * @return 월드가 소유하는 비소유 장면 포인터이며 없으면 nullptr입니다.
         */
        Engine::FSceneInterface* GetScene() const override;

        /**
         * @brief 메시 경계에 맞춰 미리보기 하늘과 바닥을 준비합니다.
         * @param[in,out] Family 하늘 설정을 기록할 이번 렌더링의 뷰 패밀리입니다.
         */
        void PrepareEnvironment(Engine::FSceneViewFamily& Family) override;

        /**
         * @brief 현재 포즈의 경계를 다시 계산하고 변경을 렌더 장면에 반영합니다.
         */
        void Synchronize() override;

    private:

        /**
         * @brief 렌더러가 만든 장면과 월드의 변경을 반영하고 컴포넌트의 경계를 갱신합니다.
         */
        void SynchronizeScene();

    private:

        /**
         * @brief 외부에서 소유하는 렌더러 서비스입니다.
         */
        RenderCore::IRendererModule* mRenderer{};

        /**
         * @brief 외부에서 소유하는 에셋 관리자입니다.
         */
        Asset::FAssetManager* mAssets{};

        /**
         * @brief 이 콘텐츠가 소유하는 미리보기 월드와 렌더 장면입니다.
         */
        std::unique_ptr<Viewer::FPreviewScene> mScene{};

        /**
         * @brief 미리보기 월드가 소유하는 스켈레탈 메시 컴포넌트입니다.
         */
        CoreUObject::TObjectRef<Engine::USkeletalMeshComponent> mComponent{};

        /**
         * @brief 현재 포즈를 감싸는 월드 공간 경계입니다.
         */
        FBox mBounds{};
    };

}
