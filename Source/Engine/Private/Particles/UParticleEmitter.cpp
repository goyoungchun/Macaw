#include "Engine/Particles/UParticleEmitter.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/UParticleLODLevel.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/Module/UParticleModuleColor.h"
#include "Engine/Particles/Module/UParticleModuleLifetime.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"
#include "Engine/Particles/Module/UParticleModuleSize.h"
#include "Engine/Particles/Module/UParticleModuleSpawn.h"
#include "Engine/Particles/Module/UParticleModuleTypeDataBase.h"
#include "Engine/Particles/Module/UParticleModuleVelocity.h"
#include "Core/Log/Log.h"
#include "CoreUObject/Reflection/NewObject.h"

#include "ParticleSerialization.h"

#include <algorithm>
#include <cmath>
#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogParticles, Log, All);

namespace Engine {

    namespace {

        // Payload마다 16바이트로 올려 FVector Payload가 정렬되도록 합니다.
        Int32 AlignPayload(Uint32 Bytes) {
            return static_cast<Int32>((Bytes + 15u) & ~15u);
        }

        // 같은 인덱스의 모듈끼리 같은 Payload 칸을 써도 되는지 봅니다.
        bool IsSameModuleSlot(const Engine::UParticleModule* High, const Engine::UParticleModule* Low) {
            return High != nullptr && Low != nullptr && High->GetClass() == Low->GetClass() && High->RequiredBytes() == Low->RequiredBytes();
        }

    }

    Engine::UParticleEmitter::UParticleEmitter() = default;

    Engine::UParticleEmitter::~UParticleEmitter() = default;

    std::unique_ptr<Engine::UParticleEmitter> Engine::UParticleEmitter::CreateDefault() {
        auto Emitter{CoreUObject::NewObject<Engine::UParticleEmitter>()};
        auto Level{Engine::UParticleLODLevel::CreateDefault()};

        Emitter->SetName("Particle Emitter");
        Level->AddModule(CoreUObject::NewObject<Engine::UParticleModuleLifetime>());
        Level->AddModule(CoreUObject::NewObject<Engine::UParticleModuleSize>());
        Level->AddModule(CoreUObject::NewObject<Engine::UParticleModuleVelocity>());
        Level->AddModule(CoreUObject::NewObject<Engine::UParticleModuleColor>());
        Emitter->AddLODLevel(std::move(Level));

        return Emitter;
    }

    void Engine::UParticleEmitter::Build() {
        // 어느 LOD로 바꿔도 바로 실행할 수 있도록 모든 레벨을 준비합니다.
        for (const auto& Level : mLODLevels) {
            Level->BakeDistributions();
            Level->UpdateModuleLists();
        }

        ValidateLODLevels();
        CacheEmitterModuleInfo();
    }

    std::unique_ptr<Engine::FParticleEmitterInstance> Engine::UParticleEmitter::CreateInstance(Engine::UParticleSystemComponent& Component) const {
        // Payload 배치는 에미터 일생 동안 하나여야 하므로 인스턴스 종류는 LOD 0 기준입니다.
        const Engine::UParticleLODLevel* Level{GetLODLevel(0)};

        if (Level == nullptr) {
            return nullptr;
        }

        if (const Engine::UParticleModuleTypeDataBase* TypeData{Level->GetTypeDataModule()}) {
            if (auto Typed{TypeData->CreateInstance(*this, Component)}) {
                return Typed;
            }
        }

        auto Instance{std::make_unique<Engine::FParticleEmitterInstance>()};

        Instance->Init(*this, Component);

        return Instance;
    }

    const Core::TArray<std::unique_ptr<Engine::UParticleLODLevel>>& Engine::UParticleEmitter::GetLODLevels() const {
        return mLODLevels;
    }

    Engine::UParticleLODLevel* Engine::UParticleEmitter::GetLODLevel(Index_t Index) const {
        return Index < mLODLevels.size() ? mLODLevels[Index].get() : nullptr;
    }

    Index_t Engine::UParticleEmitter::GetNumLODLevels() const {
        return mLODLevels.size();
    }

    void Engine::UParticleEmitter::AddLODLevel(std::unique_ptr<Engine::UParticleLODLevel> Level) {
        if (Level == nullptr) {
            return;
        }

        mLODLevels.push_back(std::move(Level));
        RenumberLODLevels();
    }

    Engine::UParticleLODLevel* Engine::UParticleEmitter::DuplicateLODLevel(Index_t SourceIndex, Index_t InsertIndex) {
        const Engine::UParticleLODLevel* Source{GetLODLevel(SourceIndex)};

        if (Source == nullptr || InsertIndex > mLODLevels.size()) {
            return nullptr;
        }

        auto Copy{Source->Duplicate()};

        if (Copy == nullptr) {
            return nullptr;
        }

        Engine::UParticleLODLevel* Result{Copy.get()};

        mLODLevels.insert(mLODLevels.begin() + static_cast<std::ptrdiff_t>(InsertIndex), std::move(Copy));
        RenumberLODLevels();

        return Result;
    }

    bool Engine::UParticleEmitter::RemoveLODLevel(Index_t Index) {
        if (Index >= mLODLevels.size() || mLODLevels.size() <= 1) {
            return false;
        }

        mLODLevels.erase(mLODLevels.begin() + static_cast<std::ptrdiff_t>(Index));
        RenumberLODLevels();

        return true;
    }

    Int32 Engine::UParticleEmitter::GetParticleSize() const {
        return mParticleSize;
    }

    Int32 Engine::UParticleEmitter::GetModuleOffset(const Engine::UParticleModule* Module) const {
        const auto Found{mModuleOffsets.find(Module)};

        return Found != mModuleOffsets.end() ? Found->second : 0;
    }

    void Engine::UParticleEmitter::Serialize(Core::FArchive& Archive) {
        const bool Loading{Archive.IsLoading()};

        Engine::ParticleSerialization::SerializeProperties(*this, Archive);

        if (Loading) {
            mLODLevels.clear();
        }

        Uint32 Count{static_cast<Uint32>(mLODLevels.size())};

        if (!Archive.BeginArray("LODLevels", Count)) {
            return;
        }

        for (Uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
            if (!Archive.BeginObject(std::to_string(Index))) {
                break;
            }

            if (Loading) {
                mLODLevels.push_back(CoreUObject::NewObject<Engine::UParticleLODLevel>());
                mLODLevels.back()->SetLevel(static_cast<Int32>(Index));
            }

            mLODLevels[Index]->Serialize(Archive);
            Archive.EndObject();
        }

        Archive.EndArray();

        if (Loading && !Archive.HasError() && mLODLevels.empty()) {
            Archive.SetError("Particle emitter has no LOD level");
        }
    }

    void Engine::UParticleEmitter::CacheEmitterModuleInfo() {
        mModuleOffsets.clear();
        mParticleSize = static_cast<Int32>(sizeof(Engine::FBaseParticle));

        const Engine::UParticleLODLevel* High{GetLODLevel(0)};

        if (High == nullptr) {
            return;
        }

        const auto& HighModules{High->GetModules()};

        for (const auto& Module : HighModules) {
            if (const Uint32 Bytes{Module->RequiredBytes()}; Bytes > 0) {
                mModuleOffsets.emplace(Module.get(), mParticleSize);
                mParticleSize += AlignPayload(Bytes);
            }
        }

        // 다른 LOD의 모듈은 같은 인덱스의 LOD 0 모듈과 같은 칸을 씁니다. 짝이 맞지 않는 모듈은
        // 등록하지 않아 오프셋 0(Payload 없음)으로 남깁니다. 잘못된 칸에 쓰는 것보다 낫습니다.
        for (Index_t LODIndex{1}; LODIndex < mLODLevels.size(); ++LODIndex) {
            const auto& Modules{mLODLevels[LODIndex]->GetModules()};
            const Index_t Common{std::min(Modules.size(), HighModules.size())};

            for (Index_t Index{}; Index < Common; ++Index) {
                if (!IsSameModuleSlot(HighModules[Index].get(), Modules[Index].get())) {
                    continue;
                }

                if (const auto Found{mModuleOffsets.find(HighModules[Index].get())}; Found != mModuleOffsets.end()) {
                    mModuleOffsets.emplace(Modules[Index].get(), Found->second);
                }
            }
        }
    }

    void Engine::UParticleEmitter::ValidateLODLevels() const {
        const Engine::UParticleLODLevel* High{GetLODLevel(0)};

        if (High == nullptr) {
            return;
        }

        const auto& HighModules{High->GetModules()};

        for (Index_t LODIndex{1}; LODIndex < mLODLevels.size(); ++LODIndex) {
            const Engine::UParticleLODLevel& Level{*mLODLevels[LODIndex]};
            const auto& Modules{Level.GetModules()};

            // 모듈 구성이 다르면 살아 있는 파티클의 Payload를 다른 칸으로 읽게 됩니다.
            if (Modules.size() != HighModules.size()) {
                MACAW_LOG(LogParticles, Warning, "Particle LOD {} has {} modules, LOD 0 has {}", LODIndex, Modules.size(), HighModules.size());
            }

            const Index_t Common{std::min(Modules.size(), HighModules.size())};

            for (Index_t Index{}; Index < Common; ++Index) {
                if (!IsSameModuleSlot(HighModules[Index].get(), Modules[Index].get())) {
                    MACAW_LOG(LogParticles, Warning, "Particle LOD {} module {} does not match LOD 0", LODIndex, Index);
                }
            }

            // 인스턴스 종류는 LOD 0 기준으로 한 번만 정해집니다.
            const auto* HighTypeData{High->GetTypeDataModule()};
            const auto* TypeData{Level.GetTypeDataModule()};

            if ((HighTypeData != nullptr ? HighTypeData->GetClass() : nullptr) != (TypeData != nullptr ? TypeData->GetClass() : nullptr)) {
                MACAW_LOG(LogParticles, Warning, "Particle LOD {} TypeData differs from LOD 0", LODIndex);
            }

            // 좌표계가 바뀌면 살아 있는 파티클의 위치 해석이 달라집니다.
            if (High->GetRequiredModule()->IsUseLocalSpace() != Level.GetRequiredModule()->IsUseLocalSpace()) {
                MACAW_LOG(LogParticles, Warning, "Particle LOD {} local space setting differs from LOD 0", LODIndex);
            }

            // Burst 발사 기록은 항목 번호로 남으므로 항목 수와 시간이 같아야 합니다.
            const auto& HighBursts{High->GetSpawnModule()->GetBurstList()};
            const auto& Bursts{Level.GetSpawnModule()->GetBurstList()};

            if (HighBursts.size() != Bursts.size()) {
                MACAW_LOG(LogParticles, Warning, "Particle LOD {} has {} bursts, LOD 0 has {}", LODIndex, Bursts.size(), HighBursts.size());

                continue;
            }

            for (Index_t Index{}; Index < Bursts.size(); ++Index) {
                if (std::abs(HighBursts[Index].mTime - Bursts[Index].mTime) > 0.0001f) {
                    MACAW_LOG(LogParticles, Warning, "Particle LOD {} burst {} time differs from LOD 0", LODIndex, Index);
                }
            }
        }
    }

    void Engine::UParticleEmitter::RenumberLODLevels() {
        for (Index_t Index{}; Index < mLODLevels.size(); ++Index) {
            mLODLevels[Index]->SetLevel(static_cast<Int32>(Index));
        }
    }

}
