/**
 * @file UShader.cpp
 * @brief 셰이더 설명과 공유 컴파일 결과 및 갱신 세대 관리에 대한 구현입니다.
 * @ingroup RenderCore
 */

#include "RenderCore/Shader/UShader.h"

#include "Core/FilePath.h"
#include "Core/FRevision.h"
#include "CoreUObject/FObjectRegistry.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

using namespace RenderCore;

namespace {

    struct FBuiltinShaders {
        std::unique_ptr<UShader> mLit{};
        std::unique_ptr<UShader> mUnlit{};
    };

    FBuiltinShaders& GetBuiltinShaders() {
        CoreUObject::FObjectRegistry::Get();
        static FBuiltinShaders Shaders{};

        return Shaders;
    }

    /**
     * @brief 고정 GUID와 소스 설정으로 공유 내장 셰이더 객체를 생성합니다.
     */
    std::unique_ptr<UShader> CreateBuiltin(const Core::FString& Identity, const Core::FString& Name, const Core::FString& SourcePath, bool Unlit) {
        auto Shader{CoreUObject::NewObject<UShader>()};
        Core::FGuid Guid{Core::FGuid::Null()};
        Core::FGuid::Parse(Identity, Guid);
        Shader->SetName(Name);
        Shader->SetSourcePath(SourcePath);
        Shader->SetUnlit(Unlit);
        Shader->SetWritesGBuffer(true);
        Shader->RestoreGuid(Guid);

        return Shader;
    }

}

/**
 * @brief 기본 소스 설정과 비어 있는 컴파일 결과로 UShader 객체를 생성합니다.
 */
UShader::UShader() = default;

/**
 * @brief 전달받은 설정과 의존성으로 UShader의 초기 상태를 구성합니다.
 */
UShader::UShader(RenderCore::FShaderDescription Description, Core::FString CacheKey, RenderCore::FShaderCompilation Compilation, bool LoadedFromCache)
	: mSourcePath{Core::FilePath::ToGenericUtf8(Description.mSourcePath)},
	  mEntryPoint{Description.mEntryPoint} {
    Initialize(std::move(Description), CacheKey, std::move(Compilation), LoadedFromCache);
}

/**
 * @brief 객체의 수명을 끝내고 소유한 자원을 정리합니다.
 */
UShader::~UShader() = default;

/**
 * @brief 변형 키에 해당하는 컴파일 상태를 조회합니다.
 */
const UShader::FCompiledVariant* UShader::FindVariant(const Core::FString& VariantKey) const {
    const auto Found{mCompiledVariants.find(VariantKey.empty() ? mDefaultVariantKey : VariantKey)};

    return Found != mCompiledVariants.end() ? &Found->second : nullptr;
}

/**
 * @brief 셰이더의 정규화된 컴파일 설명을 반환합니다.
 */
const FShaderDescription& UShader::GetDescription(const Core::FString& VariantKey) const {
    const FCompiledVariant* Variant{FindVariant(VariantKey)};
    static const FShaderDescription Empty{};

    return Variant != nullptr ? Variant->mDescription : Empty;
}

/**
 * @brief 정규화한 셰이더 설명에서 사용하는 캐시 키를 반환합니다.
 */
const Core::FString& UShader::GetCacheKey() const {
    return mDefaultVariantKey;
}

/**
 * @brief 현재 컴파일 결과의 셰이더 바이트코드를 비소유 뷰로 반환합니다.
 */
std::span<const Uint8> UShader::GetByteCode(const Core::FString& VariantKey) const {
    const auto Compilation{GetCompilation(VariantKey)};

    return Compilation != nullptr ? std::span<const Uint8>{Compilation->mByteCode} : std::span<const Uint8>{};
}

/**
 * @brief 호출 후에도 현재 컴파일 결과의 수명을 유지할 공유 참조를 반환합니다.
 */
std::shared_ptr<const FShaderCompilation> UShader::GetCompilation(const Core::FString& VariantKey) const {
    const FCompiledVariant* Variant{FindVariant(VariantKey)};

    return Variant != nullptr ? Variant->mCompilation : nullptr;
}

/**
 * @brief 컴파일 결과를 교체할 때 증가하는 셰이더 갱신 세대를 반환합니다.
 */
Uint64 UShader::GetRevision(const Core::FString& VariantKey) const {
    const FCompiledVariant* Variant{FindVariant(VariantKey)};

    return Variant != nullptr ? Variant->mRevision : 0;
}

/**
 * @brief 현재 컴파일 결과를 디스크 캐시에서 읽었는지 반환합니다.
 */
bool UShader::IsLoadedFromCache(const Core::FString& VariantKey) const {
    const FCompiledVariant* Variant{FindVariant(VariantKey)};

    return Variant != nullptr && Variant->mLoadedFromCache;
}

/**
 * @brief 이 객체가 보관하는 컴파일 변형의 개수를 반환합니다.
 */
Index_t UShader::GetCompilationCount() const {
    return mCompiledVariants.size();
}

/**
 * @brief 컴파일 변형의 설정과 공유 결과를 등록하고 갱신 세대를 설정합니다.
 */
void UShader::Initialize(FShaderDescription Description, const Core::FString& CacheKey, FShaderCompilation Compilation, bool LoadedFromCache) {
    FCompiledVariant Variant{std::move(Description), std::make_shared<const FShaderCompilation>(std::move(Compilation)), Core::FRevision::Next().GetValue(), LoadedFromCache};
    mCompiledVariants.insert_or_assign(CacheKey, std::move(Variant));

    if (mDefaultVariantKey.empty()) {
        mDefaultVariantKey = CacheKey;
    }
}

/**
 * @brief 새 컴파일 결과를 공유 스냅샷으로 교체하고 갱신 세대를 증가시킵니다.
 */
void UShader::Commit(const Core::FString& VariantKey, FShaderCompilation Compilation, bool LoadedFromCache) {
    const auto Found{mCompiledVariants.find(VariantKey)};

    if (Found == mCompiledVariants.end()) {
        return;
    }

    Found->second.mCompilation = std::make_shared<const FShaderCompilation>(std::move(Compilation));
    Found->second.mLoadedFromCache = LoadedFromCache;
    Found->second.mRevision = Core::FRevision::Next().GetValue();
}

/**
 * @brief 셰이더에 설정한 HLSL 원본 경로를 반환합니다.
 */
const Core::FString& UShader::GetSourcePath() const {
    return mSourcePath;
}

/**
 * @brief Content 상대 HLSL 경로를 검증하고 설정합니다.
 */
bool UShader::SetSourcePath(const Core::FString& Path) {
    if (IsBuiltin() || Path.empty() || Path.find('\0') != Core::FString::npos || Path.find_first_of("\r\n#\"") != Core::FString::npos) {
        return false;
    }

    const std::filesystem::path Source{Core::FilePath::FromUtf8(Path)};

    if (Core::FilePath::GetContentMount(Source) == Core::FilePath::EContentMount::Invalid || Source.extension() != L".hlsl") {
        return false;
    }

    mSourcePath = Core::FilePath::ToGenericUtf8(Source.lexically_normal());

    return true;
}

/**
 * @brief 원본 HLSL에서 사용할 진입 함수 이름을 반환합니다.
 */
const Core::FString& UShader::GetEntryPoint() const {
    return mEntryPoint;
}

/**
 * @brief HLSL 식별자로 사용할 수 있는 진입 함수 이름을 설정합니다.
 */
bool UShader::SetEntryPoint(const Core::FString& EntryPoint) {
    if (IsBuiltin() || EntryPoint.empty() || !(std::isalpha(static_cast<unsigned char>(EntryPoint.front())) || EntryPoint.front() == '_') || !std::all_of(EntryPoint.begin(), EntryPoint.end(), [](unsigned char Character) {
        return std::isalnum(Character) || Character == '_';
    })) {
        return false;
    }

    mEntryPoint = EntryPoint;

    return true;
}

/**
 * @brief 조명 계산을 생략하는 셰이더인지 반환합니다.
 */
bool UShader::IsUnlit() const {
    return mUnlit;
}

/**
 * @brief 셰이더의 조명 계산 생략 여부를 설정합니다.
 */
bool UShader::SetUnlit(bool Unlit) {
    if (IsBuiltin()) {
        return false;
    }

    mUnlit = Unlit;

    return true;
}

/**
 * @brief 불투명 렌더링에서 GBuffer 출력을 지원하는지 반환합니다.
 */
bool UShader::WritesGBuffer() const {
    return mWritesGBuffer;
}

/**
 * @brief 셰이더의 GBuffer 출력 지원 여부를 설정합니다.
 */
bool UShader::SetWritesGBuffer(bool WritesGBuffer) {
    if (IsBuiltin()) {
        return false;
    }

    mWritesGBuffer = WritesGBuffer;

    return true;
}

/**
 * @brief 예약된 GUID를 사용하는 내장 셰이더인지 검사합니다.
 */
bool UShader::IsBuiltin() const {
    const Core::FString Identity{GetGuid().ToString()};

    return Identity == "6D414341-5753-4844-8000-000000000001" || Identity == "6D414341-5753-4844-8000-000000000002";
}

/**
 * @brief 기본 Lit 셰이더 객체를 반환합니다.
 */
UShader* UShader::GetDefault() {
    auto& Shader{GetBuiltinShaders().mLit};

    if (Shader == nullptr) {
        Shader = CreateBuiltin("6D414341-5753-4844-8000-000000000001", "Lit", "/Engine/Shaders/Lit.hlsl", false);
    }

    return Shader.get();
}

/**
 * @brief 내장 Unlit 셰이더 객체를 반환합니다.
 */
UShader* UShader::GetUnlit() {
    auto& Shader{GetBuiltinShaders().mUnlit};

    if (Shader == nullptr) {
        Shader = CreateBuiltin("6D414341-5753-4844-8000-000000000002", "Unlit", "/Engine/Shaders/Unlit.hlsl", true);
    }

    return Shader.get();
}

void UShader::ReleaseBuiltins() {
    auto& Shaders{GetBuiltinShaders()};

    Shaders.mLit.reset();
    Shaders.mUnlit.reset();
}

/**
 * @brief GUID에 해당하는 내장 셰이더를 조회합니다.
 */
UShader* UShader::FindBuiltin(const Core::FGuid& Guid) {
    if (Guid == GetDefault()->GetGuid()) {
        return GetDefault();
    }

    return Guid == GetUnlit()->GetGuid() ? GetUnlit() : nullptr;
}

/**
 * @brief 기존 HLSL 경로에 해당하는 내장 셰이더를 조회합니다.
 */
UShader* UShader::FindBuiltin(const Core::FString& SourcePath) {
    const Core::FString Path{Core::FilePath::ToGenericUtf8(Core::FilePath::ToLower(Core::FilePath::FromUtf8(SourcePath).lexically_normal()))};

    if (Path == "/engine/shaders/lit.hlsl" || Path == "/engine/shaders/mesh.hlsl" || Path == "shaders/lit.hlsl" || Path == "shaders/mesh.hlsl") {
        return GetDefault();
    }

    return Path == "/engine/shaders/unlit.hlsl" || Path == "shaders/unlit.hlsl" ? GetUnlit() : nullptr;
}
