#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Core/Literals/UnitLiterals.h"

#include <algorithm>

using namespace Core::Literals;

namespace Engine {

    Engine::UParticleModuleRequired::UParticleModuleRequired()
        : mEmitterDuration{1_s} {
        SetName("Required");
    }

    Engine::UParticleModuleRequired::~UParticleModuleRequired() = default;

    Engine::EParticleModuleType Engine::UParticleModuleRequired::GetModuleType() const {
        return Engine::EParticleModuleType::Required;
    }

    Core::FTime Engine::UParticleModuleRequired::GetEmitterDuration() const {
        return mEmitterDuration;
    }

    bool Engine::UParticleModuleRequired::SetEmitterDuration(Core::FTime Duration) {
        if (!Duration.IsFinite() || Duration < Core::FTime{}) {
            return false;
        }

        mEmitterDuration = Duration;

        return true;
    }

    Uint32 Engine::UParticleModuleRequired::GetEmitterLoops() const {
        return mEmitterLoops;
    }

    void Engine::UParticleModuleRequired::SetEmitterLoops(Uint32 Loops) {
        mEmitterLoops = Loops;
    }

    Core::FTime Engine::UParticleModuleRequired::GetEmitterDelay() const {
        return mEmitterDelay;
    }

    bool Engine::UParticleModuleRequired::SetEmitterDelay(Core::FTime Delay) {
        if (!Delay.IsFinite() || Delay < Core::FTime{}) {
            return false;
        }

        mEmitterDelay = Delay;

        return true;
    }

    bool Engine::UParticleModuleRequired::IsDelayFirstLoopOnly() const {
        return mDelayFirstLoopOnly;
    }

    void Engine::UParticleModuleRequired::SetDelayFirstLoopOnly(bool FirstLoopOnly) {
        mDelayFirstLoopOnly = FirstLoopOnly;
    }

    Core::FTime Engine::UParticleModuleRequired::GetDelayForLoop(Uint32 LoopIndex) const {
        return mDelayFirstLoopOnly && LoopIndex > 0 ? Core::FTime{} : mEmitterDelay;
    }

    bool Engine::UParticleModuleRequired::IsUseLocalSpace() const {
        return mUseLocalSpace;
    }

    void Engine::UParticleModuleRequired::SetUseLocalSpace(bool UseLocalSpace) {
        mUseLocalSpace = UseLocalSpace;
    }

    Uint32 Engine::UParticleModuleRequired::GetMaxActiveParticles() const {
        return mMaxActiveParticles;
    }

    void Engine::UParticleModuleRequired::SetMaxActiveParticles(Uint32 MaxActiveParticles) {
        mMaxActiveParticles = std::min<Uint32>(MaxActiveParticles, 65535u);
    }

    const Core::FString& Engine::UParticleModuleRequired::GetMaterialPath() const {
        return mMaterialPath;
    }

    void Engine::UParticleModuleRequired::SetMaterialPath(const Core::FString& Path) {
        mMaterialPath = Path;
    }

    Engine::EParticleSortMode Engine::UParticleModuleRequired::GetSortMode() const {
        return mSortMode;
    }

    void Engine::UParticleModuleRequired::SetSortMode(Engine::EParticleSortMode SortMode) {
        mSortMode = SortMode;
    }

    Engine::EParticleSubUVInterpMethod Engine::UParticleModuleRequired::GetSubUVMethod() const {
        return mInterpolationMethod;
    }

    Uint32 Engine::UParticleModuleRequired::GetSubImagesHorizontal() const {
        return std::max<Uint32>(mSubImagesHorizontal, 1u);
    }

    Uint32 Engine::UParticleModuleRequired::GetSubImagesVertical() const {
        return std::max<Uint32>(mSubImagesVertical, 1u);
    }

    Uint32 Engine::UParticleModuleRequired::GetTotalSubImages() const {
        return GetSubImagesHorizontal() * GetSubImagesVertical();
    }

    float Engine::UParticleModuleRequired::GetRandomImageTime() const {
        return mRandomImageTime;
    }

    bool Engine::UParticleModuleRequired::UsesSubUV() const {
        return mInterpolationMethod != Engine::EParticleSubUVInterpMethod::None && GetTotalSubImages() > 1;
    }

    void Engine::UParticleModuleRequired::SetSubUV(Engine::EParticleSubUVInterpMethod Method, Uint32 Horizontal, Uint32 Vertical, float RandomImageTime) {
        mInterpolationMethod = Method;
        mSubImagesHorizontal = std::max<Uint32>(Horizontal, 1u);
        mSubImagesVertical = std::max<Uint32>(Vertical, 1u);
        mRandomImageTime = RandomImageTime;
    }

}
