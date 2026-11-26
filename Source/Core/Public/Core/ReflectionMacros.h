/**
 * @file ReflectionMacros.h
 * @brief 코드 생성에 사용하는 리플렉션 선언 또는 구현 매크로입니다.
 * @ingroup Core
 */

#pragma once

#include "Core/Types.h"
#include "Core/Reflection/EPropertyFlags.h"
#include "Core/Reflection/FPropertyMetaData.h"

namespace CoreUObject {

    class UClass;
    class UScriptStruct;
    class FReflectionBuilder;

    /**
     * @brief 타입이 생성한 멤버 등록 함수를 리플렉션 빌더로 실행합니다.
     * @tparam T 적용할 네이티브 타입입니다.
     * @tparam TMetadata 프로퍼티를 등록할 메타데이터 타입입니다.
     * @param[in,out] Information 프로퍼티를 등록할 클래스 또는 구조체 메타데이터입니다.
     */
    template <typename T, typename TMetadata>
    void BuildNativeReflection(TMetadata* Information);

    /**
     * @brief 필드 포인터와 선택적 접근자로 네이티브 프로퍼티를 등록합니다.
     */
    template <auto Member, auto Getter = nullptr, auto Setter = nullptr>
    void RegisterProperty(CoreUObject::FReflectionBuilder& Information, Core::FString Name, Core::EPropertyFlags Flags = Core::EPropertyFlags::None, Core::FPropertyMetaData MetaData = {});

    /**
     * @brief 필드 포인터와 선택적 읽기 접근자로 읽기 전용 프로퍼티를 등록합니다.
     */
    template <auto Member, auto Getter = nullptr>
    void RegisterReadOnlyProperty(CoreUObject::FReflectionBuilder& Information, Core::FString Name, Core::EPropertyFlags Flags = Core::EPropertyFlags::None);

    /**
     * @brief 배열 필드와 선택적 접근자로 원소 조회·직렬화 함수를 등록합니다.
     */
    template <auto Member, auto Getter = nullptr, auto Setter = nullptr>
    void RegisterArrayProperty(CoreUObject::FReflectionBuilder& Information, Core::FString Name, Core::EPropertyFlags Flags = Core::EPropertyFlags::None, Core::FPropertyMetaData MetaData = {});

}

/**
 * @brief 다음 class 정의를 UObject 파생 리플렉션 클래스로 표시합니다. 부모가 없으면 최상위 클래스입니다.
 * @param ... Abstract, NoFactory 같은 EClassFlags 값 이름입니다.
 */
#define UCLASS(...)

/**
 * @brief 다음 class 또는 struct 정의를 리플렉션 구조체로 표시합니다.
 */
#define USTRUCT(...)

/**
 * @brief 다음 열거형을 표시합니다. 네임스페이스 범위에 있어야 합니다.
 */
#define UENUM(...)

/**
 * @brief UENUM 값의 지정자입니다. Hidden 값은 리플렉션에서 빠집니다.
 */
#define UMETA(...)

/**
 * @brief 다음 멤버 변수를 프로퍼티로 표시합니다.
 * @param ... Edit, Serialize, ReadOnly, Transient, SerializeLate, ShowOnlyInnerProperties 플래그와 Getter = 함수, Setter = 함수,
 * Name = "이름", Category = "카테고리", Meta = (키 = "값", ...), Array 지정자입니다. 이름을 생략하면 멤버 이름에서 앞의 m을 뗀 이름을 씁니다.
 * Category는 에디터 디테일 패널에서 프로퍼티를 묶을 분류 이름이며, Category 키의 메타데이터로 저장합니다.
 * Meta는 에디터용 키·값 메타데이터입니다. 값은 따옴표 문자열이나 식별자·숫자이며, 값을 생략하면 "true"입니다.
 * 디테일 패널은 AssetType(EAssetType 값 이름), EmptyLabel(빈 에셋 경로의 표시 문구), MultiLine 키를 읽습니다.
 * TArray 멤버는 Array를 적지 않아도 배열 프로퍼티가 됩니다.
 */
#define UPROPERTY(...)

/**
 * @brief 파일 ID·줄 번호·접미어 토큰을 생성 본문 식별자로 붙입니다.
 * @param[in] A 식별자 결합에 사용할 A 토큰입니다.
 * @param[in] B 식별자 결합에 사용할 B 토큰입니다.
 * @param[in] C 식별자 결합에 사용할 C 토큰입니다.
 * @param[in] D 식별자 결합에 사용할 D 토큰입니다.
 */
#define MACAW_GENERATED_BODY_COMBINE_INNER(A, B, C, D) A##B##C##D

/**
 * @brief 인자를 먼저 확장한 뒤 생성 본문 식별자로 결합합니다.
 * @param[in] A 식별자 결합에 사용할 A 토큰입니다.
 * @param[in] B 식별자 결합에 사용할 B 토큰입니다.
 * @param[in] C 식별자 결합에 사용할 C 토큰입니다.
 * @param[in] D 식별자 결합에 사용할 D 토큰입니다.
 */
#define MACAW_GENERATED_BODY_COMBINE(A, B, C, D) MACAW_GENERATED_BODY_COMBINE_INNER(A, B, C, D)

/**
 * @brief 헤더 툴이 generated.h에 만든 리플렉션 선언으로 바뀝니다. UCLASS 또는 USTRUCT 본문의 맨 앞에 둡니다.
 * 펼친 뒤의 접근 수준은 class면 private, struct면 public입니다.
 * 헤더의 마지막 include 지시문은 자기 이름에 .generated.h를 붙인 헤더여야 합니다.
 */
#define GENERATED_BODY(...) MACAW_GENERATED_BODY_COMBINE(CURRENT_FILE_ID, _, __LINE__, _GENERATED_BODY)
