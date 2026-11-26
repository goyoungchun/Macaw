#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleSpawn.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBase.h"
#include "Engine/Particles/Module/UParticleModuleEventGenerator.h"
#include "Engine/Particles/Module/UParticleModuleEventReceiverBase.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Core/Log/Log.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Serialization/JsonArchive.h"

#include "ParticleSerialization.h"

#include <algorithm>
#include <string>
#include <utility>

DEFINE_LOG_CATEGORY_STATIC(LogParticles, Log, All);

namespace Engine {

    namespace {

        bool IsDedicatedSlotModule(const Engine::UParticleModule& Module) {
            return Module.IsA<Engine::UParticleModuleRequired>() || Module.IsA<Engine::UParticleModuleSpawn>() || Module.IsA<Engine::UParticleModuleTypeDataBase>();
        }

    }

    Engine::UParticleLODLevel::UParticleLODLevel() = default;

    Engine::UParticleLODLevel::~UParticleLODLevel() = default;

    std::unique_ptr<Engine::UParticleLODLevel> Engine::UParticleLODLevel::CreateDefault() {
        auto Level{CoreUObject::NewObject<Engine::UParticleLODLevel>()};

        Level->mRequiredModule = CoreUObject::NewObject<Engine::UParticleModuleRequired>();
        Level->mSpawnModule = CoreUObject::NewObject<Engine::UParticleModuleSpawn>();

        return Level;
    }

    void Engine::UParticleLODLevel::UpdateModuleLists() {
        mSpawnModules.clear();
        mUpdateModules.clear();
        mEventGenerator = nullptr;
        mEventReceivers.clear();

        // 꺼진 모듈도 목록에 두고 실행할 때 IsEnabled를 봅니다.
        for (const auto& Module : mModules) {
            if (Module->IsSpawnModule()) {
                mSpawnModules.push_back(Module.get());
            }

            if (Module->IsUpdateModule()) {
                mUpdateModules.push_back(Module.get());
            }

            if (mEventGenerator == nullptr) {
                mEventGenerator = CoreUObject::Cast<Engine::UParticleModuleEventGenerator>(Module.get());
            }

            if (auto* Receiver{CoreUObject::Cast<Engine::UParticleModuleEventReceiverBase>(Module.get())}) {
                mEventReceivers.push_back(Receiver);
            }
        }
    }

    void Engine::UParticleLODLevel::BakeDistributions() {
        if (mRequiredModule != nullptr) {
            mRequiredModule->BakeDistributions();
        }

        if (mSpawnModule != nullptr) {
            mSpawnModule->BakeDistributions();
        }

        if (mTypeDataModule != nullptr) {
            mTypeDataModule->BakeDistributions();
        }

        for (const auto& Module : mModules) {
            Module->BakeDistributions();
        }
    }

    std::unique_ptr<Engine::UParticleLODLevel> Engine::UParticleLODLevel::Duplicate() const {
        // 에셋 저장과 같은 경로로 복사하므로 모듈 종류가 늘어도 복사 코드를 고치지 않아도 됩니다.
        Serialization::FJsonArchive Writer{Core::EArchiveMode::Saving};

        const_cast<Engine::UParticleLODLevel*>(this)->Serialize(Writer);

        if (Writer.HasError()) {
            MACAW_LOG(LogParticles, Warning, "Particle LOD {} cannot be serialized for copy: {}", mLevel, Writer.GetError());

            return nullptr;
        }

        Serialization::FJsonArchive Reader{Core::EArchiveMode::Loading};

        if (!Reader.Parse(Writer.ToString())) {
            return nullptr;
        }

        auto Copy{CoreUObject::NewObject<Engine::UParticleLODLevel>()};

        Copy->Serialize(Reader);

        if (Reader.HasError()) {
            MACAW_LOG(LogParticles, Warning, "Particle LOD {} copy cannot be loaded: {}", mLevel, Reader.GetError());

            return nullptr;
        }

        Copy->mLevel = mLevel;

        return Copy;
    }

    Int32 Engine::UParticleLODLevel::GetLevel() const {
        return mLevel;
    }

    void Engine::UParticleLODLevel::SetLevel(Int32 Level) {
        mLevel = Level;
    }

    bool Engine::UParticleLODLevel::IsEnabled() const {
        return mEnabled;
    }

    void Engine::UParticleLODLevel::SetEnabled(bool Enabled) {
        mEnabled = Enabled;
    }

    Engine::UParticleModuleRequired* Engine::UParticleLODLevel::GetRequiredModule() const {
        return mRequiredModule.get();
    }

    Engine::UParticleModuleSpawn* Engine::UParticleLODLevel::GetSpawnModule() const {
        return mSpawnModule.get();
    }

    Engine::UParticleModuleTypeDataBase* Engine::UParticleLODLevel::GetTypeDataModule() const {
        return mTypeDataModule.get();
    }

    void Engine::UParticleLODLevel::SetTypeDataModule(std::unique_ptr<Engine::UParticleModuleTypeDataBase> Module) {
        mTypeDataModule = std::move(Module);
    }

    const Core::TArray<std::unique_ptr<Engine::UParticleModule>>& Engine::UParticleLODLevel::GetModules() const {
        return mModules;
    }

    Int32 Engine::UParticleLODLevel::FindModuleIndex(const Engine::UParticleModule* Module) const {
        for (Index_t Index{}; Index < mModules.size(); ++Index) {
            if (mModules[Index].get() == Module) {
                return static_cast<Int32>(Index);
            }
        }

        return -1;
    }

    bool Engine::UParticleLODLevel::AddModule(std::unique_ptr<Engine::UParticleModule> Module) {
        return InsertModule(std::move(Module), mModules.size());
    }

    bool Engine::UParticleLODLevel::InsertModule(std::unique_ptr<Engine::UParticleModule> Module, Index_t Index) {
        if (Module == nullptr || IsDedicatedSlotModule(*Module)) {
            return false;
        }

        mModules.insert(mModules.begin() + static_cast<std::ptrdiff_t>(std::min(Index, mModules.size())), std::move(Module));
        UpdateModuleLists();

        return true;
    }

    std::unique_ptr<Engine::UParticleModule> Engine::UParticleLODLevel::RemoveModule(const Engine::UParticleModule* Module) {
        const Int32 Index{FindModuleIndex(Module)};

        if (Index < 0) {
            return nullptr;
        }

        std::unique_ptr<Engine::UParticleModule> Removed{std::move(mModules[static_cast<Index_t>(Index)])};

        mModules.erase(mModules.begin() + Index);
        UpdateModuleLists();

        return Removed;
    }

    bool Engine::UParticleLODLevel::MoveModule(Index_t From, Index_t To) {
        if (From >= mModules.size() || To >= mModules.size()) {
            return false;
        }

        if (From != To) {
            std::unique_ptr<Engine::UParticleModule> Module{std::move(mModules[From])};

            mModules.erase(mModules.begin() + static_cast<std::ptrdiff_t>(From));
            mModules.insert(mModules.begin() + static_cast<std::ptrdiff_t>(To), std::move(Module));
            UpdateModuleLists();
        }

        return true;
    }

    const Core::TArray<Engine::UParticleModule*>& Engine::UParticleLODLevel::GetSpawnModules() const {
        return mSpawnModules;
    }

    const Core::TArray<Engine::UParticleModule*>& Engine::UParticleLODLevel::GetUpdateModules() const {
        return mUpdateModules;
    }

    Engine::UParticleModuleEventGenerator* Engine::UParticleLODLevel::GetEventGenerator() const {
        return mEventGenerator;
    }

    const Core::TArray<Engine::UParticleModuleEventReceiverBase*>& Engine::UParticleLODLevel::GetEventReceivers() const {
        return mEventReceivers;
    }

    void Engine::UParticleLODLevel::Serialize(Core::FArchive& Archive) {
        const bool Loading{Archive.IsLoading()};

        Engine::ParticleSerialization::SerializeProperties(*this, Archive);

        if (Loading) {
            mRequiredModule = CoreUObject::NewObject<Engine::UParticleModuleRequired>();
            mSpawnModule = CoreUObject::NewObject<Engine::UParticleModuleSpawn>();
            mTypeDataModule.reset();
            mModules.clear();
        }

        if (mRequiredModule == nullptr || mSpawnModule == nullptr) {
            Archive.SetError("Particle LOD level has no Required or Spawn module");

            return;
        }

        if (Archive.BeginObject("RequiredModule")) {
            mRequiredModule->Serialize(Archive);
            Archive.EndObject();
        }

        if (Archive.BeginObject("SpawnModule")) {
            mSpawnModule->Serialize(Archive);
            Archive.EndObject();
        }

        if (Archive.HasError()) {
            return;
        }

        if ((Loading && Archive.HasField("TypeDataModule")) || (!Loading && mTypeDataModule != nullptr)) {
            if (Archive.BeginObject("TypeDataModule")) {
                if (!Engine::ParticleSerialization::SerializeTypedObject(Archive, mTypeDataModule) && !Archive.HasError()) {
                    MACAW_LOG(LogParticles, Warning, "Particle TypeData module class is not available; it was dropped");
                    mTypeDataModule.reset();
                }

                Archive.EndObject();
            }
        }

        Uint32 Count{static_cast<Uint32>(mModules.size())};

        if (!Archive.BeginArray("Modules", Count)) {
            return;
        }

        for (Uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
            if (!Archive.BeginObject(std::to_string(Index))) {
                break;
            }

            if (Loading) {
                std::unique_ptr<Engine::UParticleModule> Module{};

                if (Engine::ParticleSerialization::SerializeTypedObject(Archive, Module) && !IsDedicatedSlotModule(*Module)) {
                    mModules.push_back(std::move(Module));
                } else if (!Archive.HasError()) {
                    MACAW_LOG(LogParticles, Warning, "Particle module {} of LOD {} cannot be loaded; it was dropped", Index, mLevel);
                }
            } else {
                Engine::ParticleSerialization::SerializeTypedObject(Archive, mModules[Index]);
            }

            Archive.EndObject();
        }

        Archive.EndArray();

        if (Loading) {
            UpdateModuleLists();
        }
    }

}
