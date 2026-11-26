/**
 * @file PhysicsStats.cpp
 * @brief 물리 시뮬레이션 처리 시간과 바디·접촉 개수 통계를 정의합니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/PhysicsStats.h"

namespace PhysicsRuntime {

    namespace PhysicsStats {
        Core::FStat Simulate{"physics", "Simulate CPU (incl. fetch)", Core::EStatUnit::Milliseconds};
        Core::FStat FetchResults{"physics", "Fetch results CPU (incl. wait)", Core::EStatUnit::Milliseconds};
        Core::FStat VehicleUpdate{"physics", "Vehicle update CPU (incl. suspension)", Core::EStatUnit::Milliseconds};
        Core::FStat Dispatch{"physics", "Result dispatch CPU", Core::EStatUnit::Milliseconds};
        Core::FStat Steps{"physics", "Simulation steps"};
        Core::FStat Contacts{"physics", "New contacts"};
        Core::FStat ActiveDynamicBodies{"physics", "Active dynamic bodies (step sum)"};
        Core::FStat ActiveKinematicBodies{"physics", "Active kinematic bodies (step sum)"};
        Core::FStat ActiveConstraints{"physics", "Active constraints (step sum)"};
        Core::FStat DiscreteContactPairs{"physics", "Discrete pairs processed (step sum)"};
        Core::FStat DynamicBodies{"physics", "Dynamic bodies (last scene)", Core::EStatUnit::Count, Core::EStatMode::Gauge};
        Core::FStat StaticBodies{"physics", "Static bodies (last scene)", Core::EStatUnit::Count, Core::EStatMode::Gauge};

        Core::FStat SweepQueries{"physicsquery", "Sweep queries"};
        Core::FStat SweepQueryTime{"physicsquery", "Sweep query CPU", Core::EStatUnit::Milliseconds};
        Core::FStat OverlapQueries{"physicsquery", "Overlap queries"};
        Core::FStat OverlapQueryTime{"physicsquery", "Overlap query CPU", Core::EStatUnit::Milliseconds};
        Core::FStat OverlapUpdate{"physicsquery", "Overlap event update CPU (inclusive)", Core::EStatUnit::Milliseconds};
        Core::FStat OverlapPairs{"physicsquery", "Overlapping component pairs"};

        Core::FStat ArticulatedSetup{"ragdoll", "Physics asset setup CPU", Core::EStatUnit::Milliseconds};
        Core::FStat KinematicSync{"ragdoll", "Kinematic bone sync CPU", Core::EStatUnit::Milliseconds};
        Core::FStat KinematicBoneUpdates{"ragdoll", "Kinematic bone updates"};
        Core::FStat RagdollComponents{"ragdoll", "Ticked ragdoll components"};
        Core::FStat SimulatedBoneBodies{"ragdoll", "Simulated bodies on ticked components"};
        Core::FStat PoseBlend{"ragdoll", "Physics pose blend CPU", Core::EStatUnit::Milliseconds};
        Core::FStat BlendedBones{"ragdoll", "Bones blended with physics"};
        Core::FStat AnimRigidBody{"ragdoll", "AnimGraph rigid body CPU (inclusive)", Core::EStatUnit::Milliseconds};

        Core::FStat ClothSimulate{"cloth", "Cloth simulate CPU", Core::EStatUnit::Milliseconds};
        Core::FStat ClothSolve{"cloth", "Cloth solver CPU", Core::EStatUnit::Milliseconds};
        Core::FStat ClothSkinPhysMesh{"cloth", "Cloth skin physics mesh CPU", Core::EStatUnit::Milliseconds};
        Core::FStat ClothUpdateCollisions{"cloth", "Cloth update collisions CPU", Core::EStatUnit::Milliseconds};
        Core::FStat ClothWriteback{"cloth", "Cloth result writeback CPU", Core::EStatUnit::Milliseconds};
        Core::FStat ClothParticles{"cloth", "Cloth particles"};
    }

}
