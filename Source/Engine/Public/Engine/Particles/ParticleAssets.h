/**
 * @file ParticleAssets.h
 * @brief 파티클 시스템 에셋을 에셋 관리자에 등록하고 로드하는 함수입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"

#include <filesystem>

namespace Asset {
    class FAssetManager;
}

namespace Engine {

    class UParticleSystem;

    /**
     * @brief 파티클 시스템 에셋 종류를 등록합니다. 로드한 에셋은 Build까지 끝난 상태로 캐시됩니다.
     * @return 이미 등록됐으면 false입니다.
     */
    bool RegisterParticleAssetTypes(Asset::FAssetManager& Assets);

    /**
     * @brief 파티클 시스템 에셋을 로드합니다.
     * @return 캐시 소유 객체의 비소유 주소이며 실패하면 nullptr입니다.
     */
    Engine::UParticleSystem* LoadParticleSystem(Asset::FAssetManager& Assets, const std::filesystem::path& Path, Core::FString& Error);

    /**
     * @brief 기본 에미터 하나를 가진 새 파티클 시스템 에셋을 만듭니다.
     * @param[out] Path 생성한 에셋의 Content 상대 경로입니다.
     * @return 캐시 소유 객체의 비소유 주소이며 실패하면 nullptr입니다.
     */
    Engine::UParticleSystem* CreateParticleSystem(Asset::FAssetManager& Assets, const std::filesystem::path& Directory, const Core::FString& Name, std::filesystem::path& Path, Core::FString& Error);

}
