/**
 * @file AWindSourceActor.cpp
 * @brief 바람 원천 컴포넌트를 기본으로 가지는 액터를 구현합니다.
 * @ingroup Engine
 */

#include "Engine/AWindSourceActor.h"
#include "Engine/Component/UWindSourceComponent.h"
#include "CoreUObject/Reflection/NewObject.h"

namespace Engine {

    /**
     * @brief 기본 상태로 구성합니다.
     */
    Engine::AWindSourceActor::AWindSourceActor() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Engine::AWindSourceActor::~AWindSourceActor() = default;

    /**
     * @brief 바람 원천 컴포넌트를 만들어 루트에 연결합니다.
     */
    bool Engine::AWindSourceActor::InitializeDefaultComponents() {
        if (!GetComponents().empty()) {
            return false;
        }

        auto Component{CoreUObject::NewObject<Engine::UWindSourceComponent>()};
        Component->SetName("WindSourceComponent");

        return AddComponent(std::move(Component));
    }

    /**
     * @brief 액터의 바람 원천 컴포넌트를 조회합니다.
     */
    Engine::UWindSourceComponent* Engine::AWindSourceActor::GetWindSourceComponent() const {
        return static_cast<Engine::UWindSourceComponent*>(FindDefaultComponent(Engine::UWindSourceComponent::StaticClass(), Core::FName{"WindSourceComponent"}));
    }

}
