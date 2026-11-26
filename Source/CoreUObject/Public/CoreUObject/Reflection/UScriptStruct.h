/**
 * @file UScriptStruct.h
 * @brief 일반 C++ 타입의 프로퍼티와 구조체 상속 관계를 기술합니다.
 * @ingroup CoreUObject
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "CoreUObject/Reflection/UStruct.h"

#include <memory>

namespace CoreUObject {

    struct FStructOps;

    /**
     * @brief 일반 C++ 타입의 프로퍼티와 구조체 상속 관계를 기술합니다.
     * @ingroup CoreUObject
     */
    class UScriptStruct final : public CoreUObject::UStruct {
    public:
        /**
         * @brief mClone으로 만든 작업용 값을 소유하고 mDestroy로 파괴하는 포인터입니다.
         */
        using FValuePtr = std::unique_ptr<void, void (*)(void*)>;

    public:
        /**
         * @brief UScriptStruct의 초기 상태와 전달받은 의존성을 구성합니다.
         * @param[in] Name 조회하거나 기록할 이름입니다.
         * @param[in] Size 네이티브 타입의 sizeof 바이트 수입니다.
         * @param[in] Parent 부모 타입의 메타데이터이며, 없으면 nullptr입니다.
         * @param[in] Ops 네이티브 값을 복제·파괴하는 함수 테이블이며, 복사할 수 없는 타입이면 nullptr입니다.
         */
        UScriptStruct(Core::FString Name, std::size_t Size, const CoreUObject::UScriptStruct* Parent = nullptr, const CoreUObject::FStructOps* Ops = nullptr);

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~UScriptStruct() override;

    public:
        /**
         * @brief 부모 구조체 메타데이터를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 해당 값이 없으면 nullptr입니다.
         */
        const CoreUObject::UScriptStruct* GetSuperStruct() const;

        /**
         * @brief 자기 자신을 포함한 상속 체인에 지정한 부모 타입이 있는지 검사합니다.
         * @param[in] Parent 부모 타입의 메타데이터이며, 없으면 nullptr입니다.
         * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool IsChildOf(const CoreUObject::UScriptStruct* Parent) const;

        /**
         * @brief 네이티브 값을 복제·파괴하는 함수 테이블을 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 복사할 수 없는 타입이면 nullptr입니다.
         */
        const CoreUObject::FStructOps* GetStructOps() const;

        /**
         * @brief 이 구조체 타입의 값을 복사 생성한 작업용 사본을 만듭니다.
         * @param[in] Source 이 구조체 타입의 유효한 값 주소입니다.
         * @return 사본을 소유하는 포인터이며, 함수 테이블이 없거나 원본이 nullptr이면 비어 있습니다.
         */
        FValuePtr CloneValue(const void* Source) const;

    private:
        /**
         * @brief 부모 구조체 메타데이터이며, 없으면 nullptr입니다.
         */
        const CoreUObject::UScriptStruct* mParent{};

        /**
         * @brief 네이티브 값을 복제·파괴하는 함수 테이블이며, 복사할 수 없는 타입이면 nullptr입니다.
         */
        const CoreUObject::FStructOps* mOps{};
    };

}
