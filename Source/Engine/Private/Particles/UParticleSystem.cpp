#include "Engine/Particles/UParticleSystem.h"
#include "Engine/Particles/UParticleEmitter.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/Log/Log.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Serialization/JsonArchive.h"

#include <algorithm>
#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogParticles, Log, All);

using namespace Core::Literals;

namespace Engine {

    namespace {

        /**
         * @brief 새 LOD를 맨 뒤에 붙일 때 이전 거리에 더하는 간격(m)입니다.
         */
        constexpr Core::FLength DefaultLODDistanceStep{10_m};

        /**
         * @brief 에셋 본문 형식 버전입니다.
         */
        constexpr Uint32 ParticleSystemVersion{1};

    }

    Engine::UParticleSystem::UParticleSystem()
        : mLODDistanceCheckTime{0.25_s} {
    }

    Engine::UParticleSystem::~UParticleSystem() = default;

    void Engine::UParticleSystem::InitializeDefault() {
        ClearEmitters();
        mLODDistances.clear();
        AddEmitter(Engine::UParticleEmitter::CreateDefault());
        BuildEmitters();
    }

    void Engine::UParticleSystem::BuildEmitters() {
        FixupLODSettings();

        for (const auto& Emitter : mEmitters) {
            Emitter->Build();
        }
    }

    std::unique_ptr<Engine::UParticleSystem> Engine::UParticleSystem::Duplicate(Core::FString& Error) const {
        Serialization::FJsonArchive Writer{Core::EArchiveMode::Saving};

        const_cast<Engine::UParticleSystem*>(this)->Serialize(Writer);

        if (Writer.HasError()) {
            Error = Writer.GetError();

            return nullptr;
        }

        Serialization::FJsonArchive Reader{Core::EArchiveMode::Loading};

        if (!Reader.Parse(Writer.ToString())) {
            Error = Reader.GetError();

            return nullptr;
        }

        auto Copy{CoreUObject::NewObject<Engine::UParticleSystem>()};

        Copy->Serialize(Reader);

        if (Reader.HasError()) {
            Error = Reader.GetError();

            return nullptr;
        }

        Copy->SetName(GetName());
        Copy->BuildEmitters();

        return Copy;
    }

    Index_t Engine::UParticleSystem::GetNumLODs() const {
        return mLODDistances.size();
    }

    const Core::TArray<Core::FLength>& Engine::UParticleSystem::GetLODDistances() const {
        return mLODDistances;
    }

    Core::FLength Engine::UParticleSystem::GetLODDistance(Index_t Index) const {
        return Index < mLODDistances.size() ? mLODDistances[Index] : 0_m;
    }

    void Engine::UParticleSystem::SetLODDistance(Index_t Index, Core::FLength Distance) {
        if (Index > 0 && Index < mLODDistances.size()) {
            mLODDistances[Index] = Distance;
        }
    }

    Engine::EParticleSystemLODMethod Engine::UParticleSystem::GetLODMethod() const {
        return mLODMethod;
    }

    void Engine::UParticleSystem::SetLODMethod(Engine::EParticleSystemLODMethod Method) {
        mLODMethod = Method;
    }

    Core::FTime Engine::UParticleSystem::GetLODDistanceCheckTime() const {
        return mLODDistanceCheckTime;
    }

    bool Engine::UParticleSystem::SetLODDistanceCheckTime(Core::FTime Interval) {
        if (!Interval.IsFinite() || Interval < Core::FTime{}) {
            return false;
        }

        mLODDistanceCheckTime = Interval;

        return true;
    }

    bool Engine::UParticleSystem::InsertLOD(Index_t Index) {
        const Index_t Count{GetNumLODs()};

        if (Index < 1 || Index > Count) {
            return false;
        }

        for (const auto& Emitter : mEmitters) {
            if (Emitter->GetNumLODLevels() == 0) {
                continue;
            }

            // LOD 수가 모자란 에미터는 자기 마지막 레벨을 복사해 개수를 맞춥니다.
            const Index_t InsertIndex{std::min(Index, Emitter->GetNumLODLevels())};

            if (Emitter->DuplicateLODLevel(InsertIndex - 1, InsertIndex) == nullptr) {
                MACAW_LOG(LogParticles, Warning, "Particle LOD {} could not be added to an emitter", Index);
            }
        }

        // 사이에 넣으면 양옆의 중간, 맨 뒤면 이전 거리에 간격을 더합니다.
        const Core::FLength Previous{mLODDistances[Index - 1]};
        const Core::FLength Distance{Index < Count ? (Previous + mLODDistances[Index]) * 0.5f : Previous + DefaultLODDistanceStep};

        mLODDistances.insert(mLODDistances.begin() + static_cast<std::ptrdiff_t>(Index), Distance);
        BuildEmitters();

        return true;
    }

    bool Engine::UParticleSystem::RemoveLOD(Index_t Index) {
        if (Index < 1 || Index >= GetNumLODs()) {
            return false;
        }

        for (const auto& Emitter : mEmitters) {
            if (Index < Emitter->GetNumLODLevels()) {
                Emitter->RemoveLODLevel(Index);
            }
        }

        mLODDistances.erase(mLODDistances.begin() + static_cast<std::ptrdiff_t>(Index));
        BuildEmitters();

        return true;
    }

    const Core::TArray<std::unique_ptr<Engine::UParticleEmitter>>& Engine::UParticleSystem::GetEmitters() const {
        return mEmitters;
    }

    Engine::UParticleEmitter* Engine::UParticleSystem::GetEmitter(Index_t Index) const {
        return Index < mEmitters.size() ? mEmitters[Index].get() : nullptr;
    }

    Index_t Engine::UParticleSystem::GetNumEmitters() const {
        return mEmitters.size();
    }

    Int32 Engine::UParticleSystem::FindEmitterIndex(const Engine::UParticleEmitter* Emitter) const {
        for (Index_t Index{}; Index < mEmitters.size(); ++Index) {
            if (mEmitters[Index].get() == Emitter) {
                return static_cast<Int32>(Index);
            }
        }

        return -1;
    }

    void Engine::UParticleSystem::AddEmitter(std::unique_ptr<Engine::UParticleEmitter> Emitter) {
        InsertEmitter(std::move(Emitter), mEmitters.size());
    }

    bool Engine::UParticleSystem::InsertEmitter(std::unique_ptr<Engine::UParticleEmitter> Emitter, Index_t Index) {
        if (Emitter == nullptr) {
            return false;
        }

        mEmitters.insert(mEmitters.begin() + static_cast<std::ptrdiff_t>(std::min(Index, mEmitters.size())), std::move(Emitter));

        return true;
    }

    std::unique_ptr<Engine::UParticleEmitter> Engine::UParticleSystem::RemoveEmitter(const Engine::UParticleEmitter* Emitter) {
        const Int32 Index{FindEmitterIndex(Emitter)};

        if (Index < 0) {
            return nullptr;
        }

        std::unique_ptr<Engine::UParticleEmitter> Removed{std::move(mEmitters[static_cast<Index_t>(Index)])};

        mEmitters.erase(mEmitters.begin() + Index);

        return Removed;
    }

    bool Engine::UParticleSystem::MoveEmitter(Index_t From, Index_t To) {
        if (From >= mEmitters.size() || To >= mEmitters.size()) {
            return false;
        }

        if (From != To) {
            std::unique_ptr<Engine::UParticleEmitter> Emitter{std::move(mEmitters[From])};

            mEmitters.erase(mEmitters.begin() + static_cast<std::ptrdiff_t>(From));
            mEmitters.insert(mEmitters.begin() + static_cast<std::ptrdiff_t>(To), std::move(Emitter));
        }

        return true;
    }

    void Engine::UParticleSystem::ClearEmitters() {
        mEmitters.clear();
    }

    void Engine::UParticleSystem::Serialize(Core::FArchive& Archive) {
        const bool Loading{Archive.IsLoading()};

        CoreUObject::UObject::Serialize(Archive);

        Uint32 Version{ParticleSystemVersion};

        Archive.Serialize("ParticleSystemVersion", Version);

        if (Archive.HasError()) {
            return;
        }

        if (Version != ParticleSystemVersion) {
            Archive.SetError("Unsupported particle system version: " + std::to_string(Version));

            return;
        }

        if (Loading) {
            mEmitters.clear();
        }

        Uint32 Count{static_cast<Uint32>(mEmitters.size())};

        if (!Archive.BeginArray("Emitters", Count)) {
            return;
        }

        for (Uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
            if (!Archive.BeginObject(std::to_string(Index))) {
                break;
            }

            if (Loading) {
                mEmitters.push_back(CoreUObject::NewObject<Engine::UParticleEmitter>());
            }

            mEmitters[Index]->Serialize(Archive);
            Archive.EndObject();
        }

        Archive.EndArray();
    }

    void Engine::UParticleSystem::FixupLODSettings() {
        Index_t MinimumLevels{};
        Index_t MaximumLevels{};

        for (const auto& Emitter : mEmitters) {
            const Index_t Count{Emitter->GetNumLODLevels()};

            MinimumLevels = MaximumLevels == 0 ? Count : std::min(MinimumLevels, Count);
            MaximumLevels = std::max(MaximumLevels, Count);
        }

        if (MinimumLevels != MaximumLevels) {
            MACAW_LOG(LogParticles, Warning, "Particle system emitters have different LOD counts ({} ~ {})", MinimumLevels, MaximumLevels);
        }

        // LOD 거리가 없던 에셋은 에미터 LOD 수만큼 채웁니다.
        const Index_t Count{std::max<Index_t>(MaximumLevels, 1)};

        while (mLODDistances.size() < Count) {
            mLODDistances.push_back(mLODDistances.empty() ? 0_m : mLODDistances.back() + DefaultLODDistanceStep);
        }

        mLODDistances.resize(Count);
        mLODDistances[0] = 0_m;

        for (Index_t Index{1}; Index < mLODDistances.size(); ++Index) {
            if (mLODDistances[Index] <= mLODDistances[Index - 1]) {
                MACAW_LOG(LogParticles, Warning, "Particle LOD {} distance {} is not greater than LOD {}", Index, mLODDistances[Index].GetBase(), Index - 1);
            }
        }
    }

}
