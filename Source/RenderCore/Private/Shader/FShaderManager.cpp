/**
 * @file FShaderManager.cpp
 * @brief 셰이더 캐시 로드와 변경 감지 및 재컴파일에 대한 구현입니다.
 * @ingroup RenderCore
 */

#include "RenderCore/Shader/FShaderManager.h"
#include "Core/FilePath.h"
#include "Core/LoadingStatus.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <algorithm>
#include <unordered_set>

#include <utility>

namespace RenderCore {

    namespace {

        /**
         * @brief 셰이더의 객체 세대 핸들과 컴파일 변형 키로 변경 추적 키를 생성합니다.
         */
        Core::FString GetManagedKey(const RenderCore::UShader& Shader, const Core::FString& VariantKey) {
            const auto Handle{Shader.GetHandle()};

            return std::to_string(Handle.mIndex) + ":" + std::to_string(Handle.mGeneration) + "|" + VariantKey;
        }

    }

    /**
     * @brief 전달받은 설정과 의존성으로 FShaderManager의 초기 상태를 구성합니다.
     */
    RenderCore::FShaderManager::FShaderManager(std::filesystem::path CacheDirectory)
		: mCache{std::move(CacheDirectory)} {
    }

    /**
     * @brief 셰이더를 메모리 캐시에서 조회하거나 디스크 캐시·컴파일로 준비합니다.
     */
    CoreUObject::TObjectRef<RenderCore::UShader> RenderCore::FShaderManager::Load(const RenderCore::FShaderDescription& Description, Core::FString& Diagnostics) {
        RenderCore::FShaderDescription Normalized{Description};

        if (!Normalized.Normalize(Diagnostics)) {
            return nullptr;
        }

        const Core::FString Key{Normalized.GetCacheKey()};

        if (Key.empty()) {
            Diagnostics = "Cannot compute shader cache key";

            return nullptr;
        }

        const auto Existing{mOwnedShaders.find(Key)};

        if (Existing != mOwnedShaders.end()) {
            return Existing->second.get();
        }

        RenderCore::FShaderCompilation Compilation{};
        bool LoadedFromCache{};

        if (!LoadCompilation(Normalized, Compilation, LoadedFromCache, Diagnostics)) {
            return nullptr;
        }

        auto Shader{CoreUObject::NewObject<RenderCore::UShader>(std::move(Normalized), Key, std::move(Compilation), LoadedFromCache)};
        CoreUObject::TObjectRef<RenderCore::UShader> Result{Shader.get()};
        mShaders[GetManagedKey(*Shader, Key)] = FManagedVariant{Result, Key};
        mOwnedShaders.emplace(Key, std::move(Shader));

        return Result;
    }

    /**
     * @brief 호출자가 소유하는 셰이더 객체에 컴파일 변형을 준비하고 변경 추적에 등록합니다.
     */
    bool RenderCore::FShaderManager::Load(RenderCore::UShader& Shader, const RenderCore::FShaderDescription& Description, Core::FString& VariantKey, Core::FString& Diagnostics) {
        VariantKey.clear();
        RenderCore::FShaderDescription Normalized{Description};

        if (!Normalized.Normalize(Diagnostics)) {
            return false;
        }

        VariantKey = Normalized.GetCacheKey();

        if (VariantKey.empty()) {
            Diagnostics = "Cannot compute shader cache key";

            return false;
        }

        const Core::FString ManagedKey{GetManagedKey(Shader, VariantKey)};
        const auto Existing{mShaders.find(ManagedKey)};
        const auto Current{Shader.GetCompilation(VariantKey)};

        if (Existing != mShaders.end() && Existing->second.mShader == &Shader && Current != nullptr) {
            return true;
        }

        if (Current == nullptr || !mCache.IsCurrent(*Current)) {
            RenderCore::FShaderCompilation Compilation{};
            bool LoadedFromCache{};

            if (!LoadCompilation(Normalized, Compilation, LoadedFromCache, Diagnostics)) {
                return false;
            }

            Shader.Initialize(std::move(Normalized), VariantKey, std::move(Compilation), LoadedFromCache);
        }

        mShaders[ManagedKey] = FManagedVariant{&Shader, VariantKey};

        return true;
    }

    /**
     * @brief 셰이더의 의존성 스냅샷과 파일 상태를 비교하여 변경을 감지합니다.
     */
    bool RenderCore::FShaderManager::HasChanges(const RenderCore::UShader& Shader) const {
        return std::any_of(Shader.mCompiledVariants.begin(), Shader.mCompiledVariants.end(), [this](const auto& Entry) {
            return !mCache.IsCurrent(*Entry.second.mCompilation);
        });
    }

    Core::TArray<RenderCore::FShaderDescription> RenderCore::FShaderManager::GetSourceVariants(const std::filesystem::path& Path) const {
        const auto Key{Core::FilePath::ToLower(Path.lexically_normal())};
        std::unordered_set<Core::FString> VariantKeys{};
        Core::TArray<RenderCore::FShaderDescription> Variants{};

        for (const auto& Pair : mShaders) {
            const auto& Entry{Pair.second};
            const auto* Shader{Entry.mShader.Get()};
            const auto Compilation{Shader != nullptr ? Shader->GetCompilation(Entry.mVariantKey) : nullptr};
            if (Compilation == nullptr || VariantKeys.contains(Entry.mVariantKey)) {
                continue;
            }

            if (std::ranges::any_of(Compilation->mDependencies, [&Key](const RenderCore::FShaderDependency& Dependency) {
                return Dependency.mExists && Core::FilePath::ToLower(Dependency.mPath) == Key;
            })) {
                VariantKeys.insert(Entry.mVariantKey);
                Variants.push_back(Shader->GetDescription(Entry.mVariantKey));
            }
        }

        return Variants;
    }

    /**
     * @brief 변경된 셰이더를 재컴파일하고 갱신에 성공한 셰이더를 모읍니다.
     */
    bool RenderCore::FShaderManager::ReloadChanged(Core::TArray<CoreUObject::TObjectRef<RenderCore::UShader>>& ChangedShaders, Core::FString& Diagnostics) {
        return Reload(ChangedShaders, Diagnostics, false);
    }

    bool RenderCore::FShaderManager::ReloadAll(Core::TArray<CoreUObject::TObjectRef<RenderCore::UShader>>& ChangedShaders, Core::FString& Diagnostics) {
        return Reload(ChangedShaders, Diagnostics, true);
    }

    bool RenderCore::FShaderManager::Reload(Core::TArray<CoreUObject::TObjectRef<RenderCore::UShader>>& ChangedShaders, Core::FString& Diagnostics, bool ForceCompile) {
        ChangedShaders.clear();
        Diagnostics.clear();
        bool Success{true};

        for (auto Iterator{mShaders.begin()}; Iterator != mShaders.end();) {
            const FManagedVariant& Entry{Iterator->second};
            RenderCore::UShader* Shader{Entry.mShader.Get()};
            const auto Current{Shader != nullptr ? Shader->GetCompilation(Entry.mVariantKey) : nullptr};

            if (Current == nullptr) {
                Iterator = mShaders.erase(Iterator);

                continue;
            }

            ++Iterator;

            if (!ForceCompile && mCache.IsCurrent(*Current)) {
                continue;
            }

            RenderCore::FShaderCompilation Compilation{};
            bool LoadedFromCache{};
            Core::FString Message{};

            if (!LoadCompilation(Shader->GetDescription(Entry.mVariantKey), Compilation, LoadedFromCache, Message, ForceCompile)) {
                Success = false;
            } else {
                Shader->Commit(Entry.mVariantKey, std::move(Compilation), LoadedFromCache);

                if (std::find(ChangedShaders.begin(), ChangedShaders.end(), Entry.mShader) == ChangedShaders.end()) {
                    ChangedShaders.push_back(Entry.mShader);
                }
            }

            if (!Message.empty()) {
                Diagnostics += Message + "\n";
            }
        }

        return Success;
    }

    /**
     * @brief 관리 중인 셰이더 참조와 소유한 셰이더 객체를 정리합니다.
     */
    void RenderCore::FShaderManager::Clear() {
        mShaders.clear();
        mOwnedShaders.clear();
    }

    /**
     * @brief 이후 셰이더 로드·저장에 사용할 캐시 폴더를 변경합니다.
     */
    void RenderCore::FShaderManager::SetCacheDirectory(std::filesystem::path Directory) {
        Clear();

        mCache = RenderCore::FShaderCache{std::move(Directory)};
    }

    /**
     * @brief 유효한 캐시를 먼저 조회하고 사용할 수 없으면 셰이더를 컴파일합니다.
     */
    bool RenderCore::FShaderManager::LoadCompilation(const RenderCore::FShaderDescription& Description, RenderCore::FShaderCompilation& Compilation, bool& LoadedFromCache, Core::FString& Diagnostics, bool ForceCompile) const {
        LoadedFromCache = !ForceCompile && mCache.Load(Description, Compilation, Diagnostics);

        if (LoadedFromCache) {
            return true;
        }

        Core::LoadingStatus::ReportFile("Compiling", Description.mSourcePath);

        if (!mCompiler.Compile(Description, Compilation, Diagnostics)) {
            return false;
        }

        Core::FString CacheDiagnostics{};

        if (!mCache.Save(Description, Compilation, CacheDiagnostics)) {
            Diagnostics += "\n" + CacheDiagnostics;
        }

        return true;
    }

}
