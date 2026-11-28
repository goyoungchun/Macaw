#include "Engine/Landscape/FLandscapeInfo.h"
#include "Engine/Landscape/ALandscapeStreamingProxy.h"

#include <algorithm>

namespace Engine {

    const Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>>& FLandscapeInfo::GetStreamingProxies() const {
        return mStreamingProxies;
    }

    void FLandscapeInfo::RegisterProxy(CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy> Proxy) {
        if (std::find(mStreamingProxies.begin(), mStreamingProxies.end(), Proxy) == mStreamingProxies.end()) {
            mStreamingProxies.push_back(Proxy);
        }
    }

    void FLandscapeInfo::UnregisterProxy(CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy> Proxy) {
        std::erase(mStreamingProxies, Proxy);
    }

}
