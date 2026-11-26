/**
 * @file FProperty.cpp
 * @brief 타입이 지워진 프로퍼티의 읽기, 쓰기, 편집 및 직렬화 규약입니다.
 * @ingroup CoreUObject
 */

#include "Core/Types.h"
#include "CoreUObject/Reflection/FProperty.h"
#include "CoreUObject/FObjectReference.h"
#include "Core/Archive.h"
#include "CoreUObject/Reflection/FArrayOps.h"
#include "CoreUObject/Reflection/FEnumValue.h"
#include "CoreUObject/Reflection/EPropertyType.h"
#include "Core/Reflection/EPropertyFlags.h"
#include "CoreUObject/Reflection/FPropertyValue.h"
#include "CoreUObject/Reflection/UStruct.h"
#include "CoreUObject/Reflection/UScriptStruct.h"
#include "Core/Base/ErrorHandler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace CoreUObject {

    /**
     * @brief 이 파일에서 사용하는 내부 보조 기능과 상태를 묶습니다.
     */
    namespace {
        /**
         * @brief 과도한 배열 할당을 막는 원소 수 상한입니다.
         */
        constexpr Uint32 MaximumArrayCount{100000};
    }

    /**
     * @brief FProperty의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    CoreUObject::FProperty::FProperty(Core::FString Name, CoreUObject::EPropertyType Type, Core::EPropertyFlags Flags, FReader Reader, FWriter Writer, const CoreUObject::UStruct* Struct, Core::TArray<CoreUObject::FEnumValue> Values, FWriter Loader, const CoreUObject::FNativeType* NativeType, const CoreUObject::FArrayOps* ArrayOps)
		: mName{std::move(Name)},
		  mType{Type},
		  mFlags{Flags},
		  mReader{Reader},
		  mWriter{Writer},
		  mLoader{Loader != nullptr ? Loader : Writer},
		  mStruct{Struct},
		  mEnumValues{std::move(Values)},
		  mNativeType{NativeType},
		  mArrayOps{ArrayOps} {
    }

    /**
     * @brief 프로퍼티 이름을 반환합니다.
     */
    const Core::FString& CoreUObject::FProperty::GetName() const {
        return mName;
    }

    /**
     * @brief 키에 해당하는 메타데이터 값을 반환합니다.
     */
    const Core::FString& CoreUObject::FProperty::GetMetaData(std::string_view Key) const {
        static const Core::FString Empty{};

        const auto Found{std::find_if(mMetaData.begin(), mMetaData.end(), [Key](const auto& Entry) {
            return Entry.first == Key;
        })};

        return Found != mMetaData.end() ? Found->second : Empty;
    }

    /**
     * @brief 키에 해당하는 메타데이터가 있는지 검사합니다.
     */
    bool CoreUObject::FProperty::HasMetaData(std::string_view Key) const {
        return std::any_of(mMetaData.begin(), mMetaData.end(), [Key](const auto& Entry) {
            return Entry.first == Key;
        });
    }

    /**
     * @brief 메타데이터 목록 전체를 반환합니다.
     */
    const Core::FPropertyMetaData& CoreUObject::FProperty::GetMetaDataMap() const {
        return mMetaData;
    }

    /**
     * @brief 메타데이터 목록 전체를 바꿉니다.
     */
    void CoreUObject::FProperty::SetMetaData(Core::FPropertyMetaData MetaData) {
        mMetaData = std::move(MetaData);
    }

    /**
     * @brief 에디터 디테일 패널에서 프로퍼티를 묶을 카테고리를 반환합니다.
     */
    const Core::FString& CoreUObject::FProperty::GetCategory() const {
        return GetMetaData("Category");
    }

    /**
     * @brief 프로퍼티 저장 타입을 반환합니다.
     */
    CoreUObject::EPropertyType CoreUObject::FProperty::GetType() const {
        return mType;
    }

    /**
     * @brief 프로퍼티 값의 네이티브 타입 식별 메타데이터를 반환합니다.
     */
    const CoreUObject::FNativeType* CoreUObject::FProperty::GetNativeType() const {
        return mNativeType;
    }

    /**
     * @brief 요청한 프로퍼티 플래그가 설정되어 있는지 검사합니다.
     */
    bool CoreUObject::FProperty::HasFlag(Core::EPropertyFlags Flag) const {
        return (static_cast<Uint32>(mFlags) & static_cast<Uint32>(Flag)) != 0;
    }

    /**
     * @brief 복합 값의 구조체 메타데이터를 반환합니다.
     */
    const CoreUObject::UStruct* CoreUObject::FProperty::GetStruct() const {
        return mStruct;
    }

    /**
     * @brief 열거값 이름과 정수 값 목록을 반환합니다.
     */
    const Core::TArray<CoreUObject::FEnumValue>& CoreUObject::FProperty::GetEnumValues() const {
        return mEnumValues;
    }

    /**
     * @brief 배열 프로퍼티의 함수 테이블을 반환합니다.
     */
    const CoreUObject::FArrayOps* CoreUObject::FProperty::GetArrayOps() const {
        return mArrayOps;
    }

    /**
     * @brief 컨테이너에 등록된 읽기 함수를 호출하여 프로퍼티 값을 가져옵니다.
     */
    CoreUObject::FPropertyValue CoreUObject::FProperty::ReadValue(const void* Container) const {
        return mReader(Container);
    }

    /**
     * @brief 값 타입과 쓰기 정책을 검사한 뒤 등록된 함수로 프로퍼티를 변경합니다.
     */
    bool CoreUObject::FProperty::WriteValue(void* Container, const CoreUObject::FPropertyValue& Value) const {
        if (HasFlag(Core::EPropertyFlags::ReadOnly)) {
            return false;
        }

        return WriteValue(Container, Value, mWriter);
    }

    std::optional<Core::FQuantityUnit> CoreUObject::FProperty::GetQuantityDisplayUnit(Core::FDimension Dimension) const {
        const Core::FString& DisplayUnit{GetMetaData("DisplayUnit")};

        return GetQuantityUnit(DisplayUnit.empty() ? GetMetaData("SerializeUnit") : DisplayUnit, Dimension);
    }

    std::optional<float> CoreUObject::FProperty::ToNumericValue(const CoreUObject::FPropertyValue& Value) const {
        if (const auto* Number{std::get_if<float>(&Value)}; mType == CoreUObject::EPropertyType::Float && Number != nullptr) {
            return *Number;
        }

        if (const auto* Quantity{std::get_if<CoreUObject::FQuantityPropertyValue>(&Value)}; mType == CoreUObject::EPropertyType::Quantity && Quantity != nullptr) {
            const auto Unit{GetQuantityUnit(GetMetaData("SerializeUnit"), Quantity->mDimension)};

            return Unit.has_value() ? Core::FromQuantityBase(Quantity->mBaseValue, *Unit) : std::nullopt;
        }

        return std::nullopt;
    }

    bool CoreUObject::FProperty::FromNumericValue(float Number, CoreUObject::FPropertyValue& Value) const {
        if (auto* Scalar{std::get_if<float>(&Value)}; mType == CoreUObject::EPropertyType::Float && Scalar != nullptr) {
            *Scalar = Number;

            return true;
        }

        if (auto* Quantity{std::get_if<CoreUObject::FQuantityPropertyValue>(&Value)}; mType == CoreUObject::EPropertyType::Quantity && Quantity != nullptr) {
            const auto Unit{GetQuantityUnit(GetMetaData("SerializeUnit"), Quantity->mDimension)};
            const auto Base{Unit.has_value() ? Core::ToQuantityBase(Number, *Unit) : std::nullopt};

            if (Base.has_value()) {
                Quantity->mBaseValue = *Base;

                return true;
            }
        }

        return false;
    }

    std::optional<Core::FQuantityUnit> CoreUObject::FProperty::GetQuantityUnit(std::string_view Symbol, Core::FDimension Dimension) const {
        if (Symbol.empty()) {
            return Core::GetBaseQuantityUnit(Dimension);
        }

        if (const auto* Unit{Core::FindQuantityUnit(Symbol, Dimension)}; Unit != nullptr) {
            return *Unit;
        }

        return std::nullopt;
    }

    /**
     * @brief 로드 전용 쓰기 경로로 동일 프로퍼티의 값을 복사합니다.
     */
    bool CoreUObject::FProperty::CopyValue(void* Destination, const void* Source) const {
        if (Destination == nullptr || Source == nullptr || HasFlag(Core::EPropertyFlags::Transient)) {
            return false;
        }

        const CoreUObject::FPropertyValue Value{ReadValue(Source)};

        // 구조체 멤버의 쓰기 함수는 같은 타입 구조체의 주소를 받으므로 원본 주소를 그대로 전달합니다.
        if (std::holds_alternative<const void*>(Value) && mType != CoreUObject::EPropertyType::Struct) {
            return false;
        }

        return WriteValue(Destination, Value, mLoader);
    }

    /**
     * @brief 값 타입과 쓰기 정책을 검사한 뒤 등록된 함수로 프로퍼티를 변경합니다.
     */
    bool CoreUObject::FProperty::WriteValue(void* Container, const CoreUObject::FPropertyValue& Value, FWriter Writer) const {
        if (Writer == nullptr || Container == nullptr) {
            return false;
        }

        if (mType == CoreUObject::EPropertyType::Enum) {
            const Int32* Number{std::get_if<Int32>(&Value)};

            if (Number == nullptr || std::none_of(mEnumValues.begin(), mEnumValues.end(), [Number](const CoreUObject::FEnumValue& Entry) {
                return Entry.mValue == *Number;
            })) {
                return false;
            }
        }

        return Writer(Container, Value);
    }

    /**
     * @brief 프로퍼티의 저장 정책과 타입에 맞추어 값을 직렬화합니다.
     */
    void CoreUObject::FProperty::SerializeValue(void* Container, Core::FArchive& Archive) const {
        if ((!Archive.IsDuplicating() && !HasFlag(Core::EPropertyFlags::Serialize)) || HasFlag(Core::EPropertyFlags::Transient) || Archive.HasError() || (Archive.IsLoading() && !Archive.HasField(mName))) {
            return;
        }

        SerializeNamed(mName, Container, Archive);
    }

    /**
     * @brief 저장 정책 검사를 마친 값을 지정한 이름으로 직렬화합니다.
     */
    void CoreUObject::FProperty::SerializeNamed(std::string_view Name, void* Container, Core::FArchive& Archive) const {
        if (mArrayOps != nullptr) {
            SerializeArray(Name, Container, Archive);

            return;
        }

        if (mType == CoreUObject::EPropertyType::Struct) {
            if (mReader != nullptr) {
                SerializeStruct(Name, Container, Archive);
            } else if (Archive.BeginObject(Name)) {
                // 배열 원소는 읽기 함수 없이 원소 주소가 곧 구조체 주소입니다.
                mStruct->SerializeProperties(Container, Archive);

                Archive.EndObject();
            }

            return;
        }

        CoreUObject::FPropertyValue Value{ReadValue(Container)};

        if (auto* Quantity{std::get_if<CoreUObject::FQuantityPropertyValue>(&Value)}; mType == CoreUObject::EPropertyType::Quantity && Quantity != nullptr) {
            const auto Unit{GetQuantityUnit(GetMetaData("SerializeUnit"), Quantity->mDimension)};

            if (!Unit.has_value()) {
                Archive.SetError("Invalid quantity unit: " + mName);

                return;
            }

            Archive.SerializeQuantity(Name, Quantity->mBaseValue, *Unit);
        } else if (mStruct != nullptr) {
            if (Archive.BeginObject(Name)) {
                mStruct->SerializeProperties(CoreUObject::GetPropertyData(Value), Archive);

                Archive.EndObject();
            }
        } else {
            std::visit([&Archive, Name, this](auto& Typed) {
                using FTyped = std::remove_cvref_t<decltype(Typed)>;

                if constexpr (std::is_same_v<FTyped, CoreUObject::FObjectReference>) {
                    // 빈 참조는 FGuid 형식으로 읽을 수 없으므로 월드 참조와 같이 빈 문자열로 저장합니다.
                    Core::FGuid Guid{Typed.GetGuid()};
                    Core::FString Text{Guid.IsValid() ? Guid.ToString() : Core::FString{}};
                    Archive.Serialize(Name, Text);

                    if (Archive.IsLoading() && !Archive.HasError()) {
                        Guid = Core::FGuid::Null();

                        if (!Text.empty() && !Core::FGuid::Parse(Text, Guid)) {
                            Archive.SetError("Invalid object reference: " + mName);
                        } else {
                            Archive.RemapWeakObjectReference(Guid);
                            Typed = CoreUObject::FObjectReference{Guid};
                        }
                    }
                } else if constexpr (requires { Archive.Serialize(Name, Typed); }) {
                    Archive.Serialize(Name, Typed);
                } else {
                    Archive.SetError("Cannot serialize opaque property: " + mName);
                }
            }, Value);
        }

        if (Archive.IsLoading() && !Archive.HasError() && !WriteValue(Container, Value, mLoader)) {
            Archive.SetError("Property validation failed: " + mName);
        }
    }

    /**
     * @brief 배열을 원소 단위로 직렬화합니다. 로드는 작업용 배열을 채운 뒤 한 번에 커밋합니다.
     */
    void CoreUObject::FProperty::SerializeArray(std::string_view Name, void* Container, Core::FArchive& Archive) const {
        const CoreUObject::FArrayOps& Ops{*mArrayOps};
        const bool Loading{Archive.IsLoading()};
        std::unique_ptr<void, void (*)(void*)> Working{Loading ? Ops.mCreate() : nullptr, Ops.mDestroy};

        // 저장 경로는 원소를 읽기만 하므로 getter가 돌려준 주소를 그대로 순회합니다.
        void* Array{Loading ? Working.get() : const_cast<void*>(std::get<const void*>(ReadValue(Container)))};
        Uint32 Count{Loading ? 0 : Ops.mSize(Array)};

        if (!Archive.BeginArray(Name, Count)) {
            return;
        }

        if (Count > MaximumArrayCount) {
            Archive.SetError("Array exceeds the element limit: " + mName);
        } else {
            if (Loading) {
                Ops.mResize(Array, Count);
            }

            for (Index_t Index{}; Index < Count && !Archive.HasError(); ++Index) {
                Ops.mElement->SerializeNamed(std::to_string(Index), Ops.mAt(Array, Index), Archive);
            }
        }

        Archive.EndArray();

        if (Loading && !Archive.HasError() && !WriteValue(Container, CoreUObject::FPropertyValue{static_cast<const void*>(Array)}, mLoader)) {
            Archive.SetError("Property validation failed: " + mName);
        }
    }

    /**
     * @brief 구조체 멤버를 직렬화합니다. 로드는 현재 값의 사본을 채운 뒤 한 번에 커밋합니다.
     */
    void CoreUObject::FProperty::SerializeStruct(std::string_view Name, void* Container, Core::FArchive& Archive) const {
        const void* Current{std::get<const void*>(ReadValue(Container))};

        if (!Archive.IsLoading()) {
            // 저장 경로는 필드를 읽기만 하므로 getter가 돌려준 주소를 그대로 사용합니다.
            if (Archive.BeginObject(Name)) {
                mStruct->SerializeProperties(const_cast<void*>(Current), Archive);

                Archive.EndObject();
            }

            return;
        }

        // 아카이브에 없는 필드는 현재 값을 유지하도록 현재 값을 복제해 작업용 사본으로 사용합니다.
        const CoreUObject::UScriptStruct::FValuePtr Working{static_cast<const CoreUObject::UScriptStruct*>(mStruct)->CloneValue(Current)};

        if (Working == nullptr) {
            Archive.SetError("Cannot load a struct property that cannot be copied: " + mName);

            return;
        }

        if (Archive.BeginObject(Name)) {
            mStruct->SerializeProperties(Working.get(), Archive);

            Archive.EndObject();
        }

        if (!Archive.HasError() && !WriteValue(Container, CoreUObject::FPropertyValue{static_cast<const void*>(Working.get())}, mLoader)) {
            Archive.SetError("Property validation failed: " + mName);
        }
    }

    /**
     * @brief 이 프로퍼티와 하위 원소의 객체 참조에 핸들을 연결합니다.
     */
    void CoreUObject::FProperty::BindObjectReferences(void* Container, const CoreUObject::FObjectGuidMap& Objects) const {
        if (HasFlag(Core::EPropertyFlags::Transient)) {
            return;
        }

        if (mArrayOps != nullptr) {
            const CoreUObject::EPropertyType ElementType{mArrayOps->mElement->GetType()};

            if (ElementType == CoreUObject::EPropertyType::ObjectReference || ElementType == CoreUObject::EPropertyType::Struct) {
                // 핸들은 영속 상태가 아닌 캐시이므로 setter를 거치지 않고 원소를 직접 갱신합니다.
                void* Array{const_cast<void*>(std::get<const void*>(ReadValue(Container)))};

                for (Uint32 Index{}, Count{mArrayOps->mSize(Array)}; Index < Count; ++Index) {
                    mArrayOps->mElement->BindObjectReferences(mArrayOps->mAt(Array, Index), Objects);
                }
            }
        } else if (mType == CoreUObject::EPropertyType::Struct) {
            // 배열 원소는 원소 주소를, 멤버는 읽기 함수가 돌려준 주소를 사용하며 핸들은 캐시이므로 setter를 거치지 않습니다.
            void* Struct{mReader != nullptr ? const_cast<void*>(std::get<const void*>(ReadValue(Container))) : Container};

            mStruct->BindObjectReferences(Struct, Objects);
        } else if (mType == CoreUObject::EPropertyType::ObjectReference) {
            CoreUObject::FPropertyValue Value{ReadValue(Container)};

            std::get<CoreUObject::FObjectReference>(Value).Bind(Objects);
            WriteValue(Container, Value, mLoader);
        }
    }

}
