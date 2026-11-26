/**
 * @file AWindSourceActor.h
 * @brief 바람 원천 컴포넌트를 기본으로 가지는 액터입니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/AActor.h"

#include "AWindSourceActor.generated.h"

namespace Engine {
    class UWindSourceComponent;
}

namespace Engine {

    /**
     * @brief 바람 원천 컴포넌트를 기본으로 가지는 액터입니다. 에디터의 액터 배치 목록에서 골라 놓습니다.
     * @ingroup Engine
     */
    UCLASS()
    class AWindSourceActor final : public Engine::AActor {
        GENERATED_BODY()

    public:

        /**
         * @brief 기본 상태로 구성합니다.
         */
        AWindSourceActor();

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~AWindSourceActor() override;

    public:

        /**
         * @brief 바람 원천 컴포넌트를 만들어 루트에 연결합니다.
         * @return 컴포넌트를 추가했으면 true이며, 이미 컴포넌트가 있으면 false입니다.
         */
        bool InitializeDefaultComponents() override;

        /**
         * @brief 액터의 바람 원천 컴포넌트를 조회합니다.
         * @return 컴포넌트의 비소유 주소이며 없으면 nullptr입니다.
         */
        Engine::UWindSourceComponent* GetWindSourceComponent() const;
    };

}
