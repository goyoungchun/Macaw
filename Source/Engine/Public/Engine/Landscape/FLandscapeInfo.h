#pragma once

#include "CoreUObject/TObjectRef.h"

namespace Engine {

    class ALandscapeStreamingProxy;
    class UWorld;

    class FLandscapeInfo {
    public:
        const Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>>& GetStreamingProxies() const;

    private:
        friend class UWorld;

        void RegisterProxy(CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy> Proxy);
        void UnregisterProxy(CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy> Proxy);

    private:
        Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> mStreamingProxies{};
    };

}
