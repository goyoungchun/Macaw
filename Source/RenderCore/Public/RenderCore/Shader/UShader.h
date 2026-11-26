/**
 * @file UShader.h
 * @brief 셰이더 설명과 공유 컴파일 결과 및 갱신 세대 관리에 대한 선언입니다.
 * @ingroup RenderCore
 */

#pragma once

#include "CoreUObject/UObject.h"
#include "RenderCore/Shader/FShaderCompilation.h"
#include "RenderCore/Shader/FShaderDescription.h"

#include <span>

#include "UShader.generated.h"

namespace RenderCore {

    class FShaderManager;

    /**
     * @brief 셰이더 설명과 공유 컴파일 결과 및 갱신 세대 관리를 담당합니다.
     * @note UObject 상속 규칙에 따라 복사·이동 생성과 대입을 금지합니다.
     */
    UCLASS()
    class UShader final : public CoreUObject::UObject {
        GENERATED_BODY()

    private:
        /**
         * @brief 한 컴파일 변형의 설정·공유 바이트코드·갱신 세대를 보관합니다.
         */
        struct FCompiledVariant {
        public:
            /**
             * @brief 셰이더 컴파일 경로와 진입점·프로필·매크로 설정입니다.
             */
            RenderCore::FShaderDescription mDescription{};

            /**
             * @brief 이전에 받은 참조의 수명도 유지하는 불변 컴파일 결과입니다.
             */
            std::shared_ptr<const RenderCore::FShaderCompilation> mCompilation{};

            /**
             * @brief 생성과 성공한 컴파일 결과 교체마다 Core::FRevision에서 새로 받는 갱신 세대입니다.
             */
            Uint64 mRevision{};

            /**
             * @brief 현재 바이트코드를 디스크 캐시에서 불러왔는지 나타냅니다.
             */
            bool mLoadedFromCache{};
        };

    public:
        /**
         * @brief 기본 소스 설정과 비어 있는 컴파일 결과로 UShader 객체를 생성합니다.
         */
        UShader();

        /**
         * @brief 전달받은 설정과 의존성으로 UShader의 초기 상태를 구성합니다.
         * @param[in] Description 소스 경로·진입점·프로필·매크로 등의 컴파일 설정입니다.
         * @param[in] CacheKey 캐시 파일을 구분하는 셰이더 해시 키입니다.
         * @param[in] Compilation 셰이더 바이트코드와 의존성을 포함하는 컴파일 결과입니다.
         * @param[in] LoadedFromCache 컴파일 결과가 디스크 캐시에서 읽힌 것인지 나타냅니다.
         */
        UShader(RenderCore::FShaderDescription Description, Core::FString CacheKey, RenderCore::FShaderCompilation Compilation, bool LoadedFromCache);

        /**
         * @brief 객체의 수명을 끝내고 소유한 자원을 정리합니다.
         */
        ~UShader() override;

    public:
        /**
         * @brief 셰이더의 정규화된 컴파일 설명을 반환합니다.
         * @param[in] VariantKey 조회할 컴파일 변형 키이며, 비우면 기본 변형을 조회합니다.
         * @return 지정한 변형의 설정이며, 준비되지 않은 변형이면 비어 있는 설정입니다.
         */
        const RenderCore::FShaderDescription& GetDescription(const Core::FString& VariantKey = {}) const;

        /**
         * @brief 정규화한 셰이더 설명에서 사용하는 캐시 키를 반환합니다.
         * @return 기본 컴파일 변형의 캐시 키 참조이며, 준비된 변형이 없으면 빈 문자열입니다.
         */
        const Core::FString& GetCacheKey() const;

        /**
         * @brief 현재 컴파일 결과의 셰이더 바이트코드를 비소유 뷰로 반환합니다.
         * @param[in] VariantKey 조회할 컴파일 변형 키이며, 비우면 기본 변형을 조회합니다.
         * @return 컴파일 결과가 살아 있는 동안 유효한 읽기 전용 뷰이며, 결과가 없으면 빈 뷰입니다.
         */
        std::span<const Uint8> GetByteCode(const Core::FString& VariantKey = {}) const;

        /**
         * @brief 호출 후에도 현재 컴파일 결과의 수명을 유지할 공유 참조를 반환합니다.
         * @param[in] VariantKey 조회할 컴파일 변형 키이며, 비우면 기본 변형을 조회합니다.
         * @return 현재 컴파일 결과의 수명을 유지하는 공유 참조이며, 결과가 없으면 nullptr입니다.
         */
        std::shared_ptr<const RenderCore::FShaderCompilation> GetCompilation(const Core::FString& VariantKey = {}) const;

        /**
         * @brief 컴파일 결과를 교체할 때 증가하는 셰이더 갱신 세대를 반환합니다.
         * @param[in] VariantKey 조회할 컴파일 변형 키이며, 비우면 기본 변형을 조회합니다.
         * @return 지정한 변형의 갱신 세대이며, 준비되지 않은 변형이면 0입니다.
         */
        Uint64 GetRevision(const Core::FString& VariantKey = {}) const;

        /**
         * @brief 현재 컴파일 결과를 디스크 캐시에서 읽었는지 반환합니다.
         * @param[in] VariantKey 조회할 컴파일 변형 키이며, 비우면 기본 변형을 조회합니다.
         * @return 지정한 변형의 컴파일 결과를 디스크 캐시에서 읽었으면 true입니다.
         */
        bool IsLoadedFromCache(const Core::FString& VariantKey = {}) const;

        /**
         * @brief 이 객체가 보관하는 컴파일 변형의 개수를 반환합니다.
         * @return 현재 준비된 컴파일 변형의 개수입니다.
         */
        Index_t GetCompilationCount() const;

        /**
         * @brief 셰이더에 설정한 HLSL 원본 경로를 반환합니다.
         * @return 이 객체가 보관하는 원본 경로 문자열의 참조입니다.
         */
        const Core::FString& GetSourcePath() const;

        /**
         * @brief Content 상대 HLSL 경로를 검증하고 설정합니다.
         * @param[in] Path 적용할 Content 기준 상대 HLSL 경로입니다.
         * @return 경로를 수락하면 true이며, 내장 셰이더이거나 경로가 유효하지 않으면 false입니다.
         */
        bool SetSourcePath(const Core::FString& Path);

        /**
         * @brief 원본 HLSL에서 사용할 진입 함수 이름을 반환합니다.
         * @return 이 객체가 보관하는 진입 함수 이름의 참조입니다.
         */
        const Core::FString& GetEntryPoint() const;

        /**
         * @brief HLSL 식별자로 사용할 수 있는 진입 함수 이름을 설정합니다.
         * @param[in] EntryPoint 적용할 진입 함수 이름입니다.
         * @return 이름을 수락하면 true이며, 내장 셰이더이거나 식별자가 유효하지 않으면 false입니다.
         */
        bool SetEntryPoint(const Core::FString& EntryPoint);

        /**
         * @brief 조명 계산을 생략하는 셰이더인지 반환합니다.
         * @return 조명 계산을 생략하도록 설정되었으면 true입니다.
         */
        bool IsUnlit() const;

        /**
         * @brief 셰이더의 조명 계산 생략 여부를 설정합니다.
         * @param[in] Unlit 조명 계산을 생략할지 지정합니다.
         * @return 설정을 수락하면 true이며, 변경할 수 없는 내장 셰이더이면 false입니다.
         */
        bool SetUnlit(bool Unlit);

        /**
         * @brief 불투명 렌더링에서 GBuffer 출력을 지원하는지 반환합니다.
         * @return GBuffer 출력을 지원하도록 설정되었으면 true입니다.
         */
        bool WritesGBuffer() const;

        /**
         * @brief 셰이더의 GBuffer 출력 지원 여부를 설정합니다.
         * @param[in] WritesGBuffer GBuffer 출력을 지원할지 지정합니다.
         * @return 설정을 수락하면 true이며, 변경할 수 없는 내장 셰이더이면 false입니다.
         */
        bool SetWritesGBuffer(bool WritesGBuffer);

        /**
         * @brief 예약된 GUID를 사용하는 내장 셰이더인지 검사합니다.
         * @return 내장 Lit 또는 Unlit 셰이더의 GUID이면 true입니다.
         */
        bool IsBuiltin() const;

        /**
         * @brief 기본 Lit 셰이더 객체를 반환합니다.
         * @return 프로세스에서 공유하는 내장 Lit 셰이더의 비소유 주소입니다.
         */
        static RenderCore::UShader* GetDefault();

        /**
         * @brief 내장 Unlit 셰이더 객체를 반환합니다.
         * @return 프로세스에서 공유하는 내장 Unlit 셰이더의 비소유 주소입니다.
         */
        static RenderCore::UShader* GetUnlit();

        /**
         * @brief GUID에 해당하는 내장 셰이더를 조회합니다.
         * @param[in] Guid 조회할 셰이더의 GUID입니다.
         * @return 대응하는 내장 셰이더의 비소유 주소이며, 일치하지 않으면 nullptr입니다.
         */
        static RenderCore::UShader* FindBuiltin(const Core::FGuid& Guid);

        /**
         * @brief 기존 HLSL 경로에 해당하는 내장 셰이더를 조회합니다.
         * @param[in] SourcePath Content 기준 HLSL 경로이며, Lit·Mesh·Unlit의 기존 경로를 지원합니다.
         * @return 대응하는 내장 셰이더의 비소유 주소이며, 일치하지 않으면 nullptr입니다.
         */
        static RenderCore::UShader* FindBuiltin(const Core::FString& SourcePath);

        static void ReleaseBuiltins();

    private:
        /**
         * @brief 셰이더 관리자가 컴파일 결과를 교체하도록 접근을 허용합니다.
         */
        friend class FShaderManager;

        /**
         * @brief 컴파일 변형의 설정과 공유 결과를 등록하고 갱신 세대를 설정합니다.
         * @param[in] Description 정규화한 소스·진입점·프로필·매크로 설정입니다.
         * @param[in] CacheKey 등록할 컴파일 변형의 캐시 키입니다.
         * @param[in] Compilation 바이트코드와 파일 의존성을 포함하는 컴파일 결과입니다.
         * @param[in] LoadedFromCache 디스크 캐시에서 읽은 결과인지 지정합니다.
         */
        void Initialize(RenderCore::FShaderDescription Description, const Core::FString& CacheKey, RenderCore::FShaderCompilation Compilation, bool LoadedFromCache);

        /**
         * @brief 새 컴파일 결과를 공유 스냅샷으로 교체하고 갱신 세대를 증가시킵니다.
         * @param[in] VariantKey 교체할 컴파일 변형의 캐시 키입니다.
         * @param[in] Compilation 셰이더 바이트코드와 의존성을 포함하는 컴파일 결과입니다.
         * @param[in] LoadedFromCache 컴파일 결과가 디스크 캐시에서 읽힌 것인지 나타냅니다.
         */
        void Commit(const Core::FString& VariantKey, RenderCore::FShaderCompilation Compilation, bool LoadedFromCache);

        /**
         * @brief 변형 키에 해당하는 컴파일 상태를 조회합니다.
         * @param[in] VariantKey 조회할 변형 키이며, 비우면 기본 변형을 조회합니다.
         * @return 보관 중인 컴파일 상태의 비소유 주소이며, 없으면 nullptr입니다.
         */
        const RenderCore::UShader::FCompiledVariant* FindVariant(const Core::FString& VariantKey) const;

    private:
        /**
         * @brief HLSL 원본 파일의 경로이며, 에셋 저장 시 Content 상대 경로로 정규화합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Name = "Source", Getter = GetSourcePath, Setter = SetSourcePath)
        Core::FString mSourcePath{"/Engine/Shaders/Lit.hlsl"};

        /**
         * @brief 원본 HLSL에서 사용할 진입 함수 이름입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Name = "EntryPoint", Getter = GetEntryPoint, Setter = SetEntryPoint)
        Core::FString mEntryPoint{"PixelMain"};

        /**
         * @brief 조명 계산을 생략하는 렌더링 정책입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Name = "Unlit", Getter = IsUnlit, Setter = SetUnlit)
        bool mUnlit{};

        /**
         * @brief 불투명 렌더링에서 GBuffer 출력을 지원하는지 나타냅니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Name = "WritesGBuffer", Getter = WritesGBuffer, Setter = SetWritesGBuffer)
        bool mWritesGBuffer{};

        /**
         * @brief 셰이더 설명과 컴파일러 정보를 구분하는 캐시 키입니다.
         */
        Core::FString mDefaultVariantKey{};

        /**
         * @brief 캐시 키별로 보관하는 컴파일 설정과 공유 결과 및 갱신 세대입니다.
         */
        Core::TMap<Core::FString, RenderCore::UShader::FCompiledVariant> mCompiledVariants{};
    };

}
