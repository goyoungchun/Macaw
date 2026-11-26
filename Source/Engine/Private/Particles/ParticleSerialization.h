#pragma once

#include "Core/Archive.h"
#include "Core/Types.h"
#include "CoreUObject/UObject.h"
#include "CoreUObject/Reflection/FReflectionRegistry.h"
#include "CoreUObject/Reflection/UClass.h"

#include <memory>
#include <string_view>

namespace Engine::ParticleSerialization {

    // 파티클 에셋의 하위 객체는 다른 곳에서 참조하지 않으므로 식별자 없이 프로퍼티만 저장합니다.
    // 식별자를 저장하면 LOD 복제 같은 복사본이 같은 식별자를 갖게 됩니다.
    inline void SerializeProperties(CoreUObject::UObject& Object, Core::FArchive& Archive) {
        Object.GetClass()->SerializeClassProperties(&Object, Archive, CoreUObject::UObject::StaticClass()->FindClassProperty("Id"));
    }

    // 현재 컨테이너에 {"Class": 이름, "Data": {...}}를 읽고 씁니다. 읽을 때 Base를 상속하지 않거나
    // 만들 수 없는 클래스면 Object를 비워 두고 false를 돌려주며, 아카이브 오류는 만들지 않습니다.
    template <typename TBase>
    bool SerializeTypedObject(Core::FArchive& Archive, std::unique_ptr<TBase>& Object) {
        Core::FString ClassName{Object != nullptr ? Object->GetClass()->GetName() : Core::FString{}};

        Archive.Serialize("Class", ClassName);

        if (Archive.HasError()) {
            return false;
        }

        if (Archive.IsLoading()) {
            const CoreUObject::UClass* Class{CoreUObject::FReflectionRegistry::FindClass(ClassName)};

            if (Class == nullptr || !Class->IsChildOf(TBase::StaticClass()) || !Class->CanCreate()) {
                return false;
            }

            auto Created{Class->CreateInstance()};

            Object.reset(static_cast<TBase*>(Created.release()));
        }

        if (!Archive.BeginObject("Data")) {
            return false;
        }

        Object->Serialize(Archive);
        Archive.EndObject();

        return !Archive.HasError();
    }

}
