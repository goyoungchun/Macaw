#include "Engine/Particles/Module/UParticleModuleEventGenerator.h"

namespace Engine {

    Engine::UParticleModuleEventGenerator::UParticleModuleEventGenerator() {
        SetName("Event Generator");
    }

    Engine::UParticleModuleEventGenerator::~UParticleModuleEventGenerator() = default;

    Engine::EParticleModuleType Engine::UParticleModuleEventGenerator::GetModuleType() const {
        return Engine::EParticleModuleType::Event;
    }

    void Engine::UParticleModuleEventGenerator::BakeDistributions() {
        // 이벤트를 보낼 때마다 문자열을 FName으로 바꾸지 않도록 미리 만들어 둡니다.
        mEventNames.clear();

        for (const Engine::FParticleEventGeneratorInfo& Info : mEvents) {
            mEventNames.push_back(Info.mCustomName.empty() ? Core::FName{} : Core::FName{Info.mCustomName});
        }
    }

    bool Engine::UParticleModuleEventGenerator::HasEvent(Engine::EParticleEventType Type) const {
        for (const Engine::FParticleEventGeneratorInfo& Info : mEvents) {
            if (Info.mType == Type) {
                return true;
            }
        }

        return false;
    }

    const Core::TArray<Engine::FParticleEventGeneratorInfo>& Engine::UParticleModuleEventGenerator::GetEvents() const {
        return mEvents;
    }

    Core::FName Engine::UParticleModuleEventGenerator::GetEventName(Index_t Index) const {
        return Index < mEventNames.size() ? mEventNames[Index] : Core::FName{};
    }

    void Engine::UParticleModuleEventGenerator::AddEvent(const Engine::FParticleEventGeneratorInfo& Info) {
        mEvents.push_back(Info);
        BakeDistributions();
    }

}
