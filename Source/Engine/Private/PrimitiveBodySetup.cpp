/**
 * @file PrimitiveBodySetup.cpp
 * @brief Plane, Cube와 Sphere 기본 메시가 공유하는 충돌 형상을 제공합니다.
 * @ingroup Engine
 */

#include "Core/Types.h"
#include "Core/Literals/UnitLiterals.h"
#include "Engine/PrimitiveBodySetup.h"
#include "Engine/PrimitiveMesh.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <memory>

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief 기본 프리미티브 바디 셋업을 만드는 보조 함수입니다.
     */
    namespace {
        /**
         * @brief 두께가 없는 Plane을 근사하는 상자의 절반 두께입니다.
         * @note 너무 얇으면 빠른 물체가 통과하므로 기본 메시 경계(0.001)보다 두껍게 잡습니다.
         */
        constexpr float PlaneHalfThickness{(0.01_m).GetBase()};
        constexpr float HalfSize{(0.5_m).GetBase()};

        /**
         * @brief 기본 프리미티브 종류에 맞는 충돌 형상을 가진 바디 셋업을 만듭니다.
         * @param[in] Type 기본 프리미티브 종류입니다.
         * @return 새로 만든 바디 셋업입니다.
         */
        std::unique_ptr<PhysicsRuntime::UBodySetup> BuildPrimitiveBodySetup(Engine::EPrimitiveType Type) {
            auto Setup{CoreUObject::NewObject<PhysicsRuntime::UBodySetup>()};
            Setup->SetName(Engine::PrimitiveName(Type) + "BodySetup");

            PhysicsRuntime::FKAggregateGeom& Geometry{Setup->GetAggGeom()};

            if (Type == Engine::EPrimitiveType::Sphere) {
                PhysicsRuntime::FKSphereElem& Sphere{Geometry.mSphereElems.emplace_back()};
                Sphere.mRadius = 0.5_m;
            } else {
                PhysicsRuntime::FKBoxElem& Box{Geometry.mBoxElems.emplace_back()};
                Box.mHalfExtent = Type == Engine::EPrimitiveType::Plane ? FVector{HalfSize, HalfSize, PlaneHalfThickness} : FVector{HalfSize, HalfSize, HalfSize};
            }

            return Setup;
        }
    }

    /**
     * @brief 기본 프리미티브 종류별로 공유하는 읽기 전용 바디 셋업을 반환합니다.
     */
    std::shared_ptr<const PhysicsRuntime::UBodySetup> AcquirePrimitiveBodySetup(Engine::EPrimitiveType Type) {
        if (Type >= Engine::EPrimitiveType::Count) {
            return {};
        }

        static Core::TStaticArray<std::weak_ptr<const PhysicsRuntime::UBodySetup>, static_cast<Index_t>(Engine::EPrimitiveType::Count)> Setups{};
        auto& CachedSetup{Setups[static_cast<Index_t>(Type)]};
        auto Setup{CachedSetup.lock()};

        if (Setup == nullptr) {
            Setup = BuildPrimitiveBodySetup(Type);
            CachedSetup = Setup;
        }

        return Setup;
    }

}
