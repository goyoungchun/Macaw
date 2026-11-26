/**
 * @file FPhysicsAssetEditing.cpp
 * @brief 물리 에셋 편집기가 바디 설명서의 충돌 형상을 찾고 추가하고 지우는 도우미를 구현합니다.
 * @ingroup Editor
 */

#include "FPhysicsAssetEditing.h"

#include "Asset/FAssetManager.h"
#include "Asset/FPhysicsAssetUtils.h"
#include "Core/FilePath.h"
#include "Asset/Import/FMeshImportBone.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Core/Literals.h"
#include "Core/Math/FRay.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/FPhysicsScene.h"
#include "PhysicsRuntime/PhysicsConversion.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "PhysicsRuntime/UPhysicsAsset.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <utility>

using namespace Core::Literals;
using namespace Editor::PhysicsAssetEditing;

namespace {
    /**
     * @brief 새로 추가하는 항목의 기본 크기이며 단위는 m입니다.
     */
    constexpr Core::FLength DefaultSphereRadius{5_cm};
    constexpr float DefaultBoxHalfExtent{0.05f};
    constexpr Core::FLength DefaultCapsuleRadius{4_cm};
    constexpr Core::FLength DefaultCapsuleLength{15_cm};
}

Index_t Editor::PhysicsAssetEditing::GetShapeCount(const PhysicsRuntime::UBodySetup& Setup, EShapeType Type) {
    const PhysicsRuntime::FKAggregateGeom& Geometry{Setup.GetAggGeom()};

    switch (Type) {
        case EShapeType::Sphere:
            return Geometry.mSphereElems.size();
        case EShapeType::Box:
            return Geometry.mBoxElems.size();
        case EShapeType::Sphyl:
            return Geometry.mSphylElems.size();
        case EShapeType::Convex:
            return Geometry.mConvexElems.size();
        default:
            return 0;
    }
}

Index_t Editor::PhysicsAssetEditing::GetTotalShapeCount(const PhysicsRuntime::UBodySetup& Setup) {
    return GetShapeCount(Setup, EShapeType::Sphere) + GetShapeCount(Setup, EShapeType::Box) + GetShapeCount(Setup, EShapeType::Sphyl) + GetShapeCount(Setup, EShapeType::Convex);
}

FShapeRef Editor::PhysicsAssetEditing::FindShapeByOrdinal(const PhysicsRuntime::UBodySetup& Setup, Index_t Ordinal) {
    // 바디를 만들 때 구, 상자, 캡슐, 볼록 순서로 Shape를 붙이므로 같은 순서로 세어 항목을 찾는다.
    for (const EShapeType Type : {EShapeType::Sphere, EShapeType::Box, EShapeType::Sphyl, EShapeType::Convex}) {
        const Index_t Count{GetShapeCount(Setup, Type)};

        if (Ordinal < Count) {
            return FShapeRef{Type, Ordinal};
        }

        Ordinal -= Count;
    }

    return FShapeRef{};
}

FShapeRef Editor::PhysicsAssetEditing::AddShape(PhysicsRuntime::UBodySetup& Setup, EShapeType Type) {
    PhysicsRuntime::FKAggregateGeom& Geometry{Setup.GetAggGeom()};

    if (Type == EShapeType::Sphere) {
        PhysicsRuntime::FKSphereElem Element{};
        Element.mRadius = DefaultSphereRadius;
        Geometry.mSphereElems.push_back(Element);

        return FShapeRef{Type, Geometry.mSphereElems.size() - 1};
    }

    if (Type == EShapeType::Box) {
        PhysicsRuntime::FKBoxElem Element{};
        Element.mHalfExtent = FVector{DefaultBoxHalfExtent, DefaultBoxHalfExtent, DefaultBoxHalfExtent};
        Geometry.mBoxElems.push_back(Element);

        return FShapeRef{Type, Geometry.mBoxElems.size() - 1};
    }

    if (Type == EShapeType::Sphyl) {
        PhysicsRuntime::FKSphylElem Element{};
        Element.mRadius = DefaultCapsuleRadius;
        Element.mLength = DefaultCapsuleLength;
        Geometry.mSphylElems.push_back(Element);

        return FShapeRef{Type, Geometry.mSphylElems.size() - 1};
    }

    return FShapeRef{};
}

bool Editor::PhysicsAssetEditing::DeleteShape(PhysicsRuntime::UBodySetup& Setup, const FShapeRef& Shape) {
    if (!Shape.IsValid() || Shape.mIndex >= GetShapeCount(Setup, Shape.mType)) {
        return false;
    }

    PhysicsRuntime::FKAggregateGeom& Geometry{Setup.GetAggGeom()};
    const auto Offset{static_cast<std::ptrdiff_t>(Shape.mIndex)};

    switch (Shape.mType) {
        case EShapeType::Sphere:
            Geometry.mSphereElems.erase(Geometry.mSphereElems.begin() + Offset);
            break;
        case EShapeType::Box:
            Geometry.mBoxElems.erase(Geometry.mBoxElems.begin() + Offset);
            break;
        case EShapeType::Sphyl:
            Geometry.mSphylElems.erase(Geometry.mSphylElems.begin() + Offset);
            break;
        case EShapeType::Convex:
            Geometry.mConvexElems.erase(Geometry.mConvexElems.begin() + Offset);
            break;
        default:
            return false;
    }

    return true;
}

Index_t Editor::PhysicsAssetEditing::AddBodyForBone(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Asset::USkeletalMesh& Mesh, const Asset::USkeleton& Skeleton, Index_t BoneIndex) {
    const auto& Bones{Skeleton.GetBones()};

    if (BoneIndex >= Bones.size()) {
        return InvalidIndex;
    }

    const Asset::FPhysAssetCreateParams Params{};
    const Index_t BodyIndex{Asset::FPhysicsAssetUtils::CreateNewBody(PhysicsAsset, Bones[BoneIndex].GetName())};

    // 뼈에 배정된 정점으로 모양을 맞춘다. 정점이 없으면 최소 크기의 형상이 만들어진다.
    Core::TArray<Asset::FBoneVertInfo> Infos{};
    Asset::FPhysicsAssetUtils::CalcBoneVertInfos(Mesh, Skeleton, Params.mVertWeight, Infos);

    const Asset::FBoneVertInfo Info{BoneIndex < Infos.size() ? Infos[BoneIndex] : Asset::FBoneVertInfo{}};

    if (!Asset::FPhysicsAssetUtils::CreateCollisionFromBone(*PhysicsAsset.mSkeletalBodySetups[BodyIndex], Skeleton, BoneIndex, Params, Info)) {
        Asset::FPhysicsAssetUtils::DestroyBody(PhysicsAsset, BodyIndex);

        return InvalidIndex;
    }

    return BodyIndex;
}

Index_t Editor::PhysicsAssetEditing::AddConstraintForBone(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Asset::USkeleton& Skeleton, Index_t BoneIndex) {
    const auto& Bones{Skeleton.GetBones()};
    const auto& LocalPose{Skeleton.GetReferencePose()};

    if (BoneIndex >= Bones.size() || PhysicsAsset.FindBodyIndex(Bones[BoneIndex].GetName()) == InvalidIndex || PhysicsAsset.FindConstraintIndex(Bones[BoneIndex].GetName()) != InvalidIndex) {
        return InvalidIndex;
    }

    // 자동 생성과 같은 규칙으로 바디가 있는 조상을 찾고 자식의 부모 기준 변환을 쌓는다.
    FTransform RelTM{};
    Int32 ParentIndex{static_cast<Int32>(BoneIndex)};
    Index_t ParentBodyIndex{InvalidIndex};

    do {
        RelTM = RelTM * LocalPose[ParentIndex];
        ParentIndex = Bones[ParentIndex].GetParentIndex();

        if (ParentIndex < 0) {
            return InvalidIndex;
        }

        ParentBodyIndex = PhysicsAsset.FindBodyIndex(Bones[ParentIndex].GetName());
    } while (ParentBodyIndex == InvalidIndex);

    const Core::FString& BoneName{Bones[BoneIndex].GetName()};
    const Index_t ConstraintIndex{Asset::FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, BoneName)};
    PhysicsRuntime::FConstraintInstance& Constraint{PhysicsAsset.mConstraintSetup[ConstraintIndex]->mDefaultInstance};

    Constraint.mSwing1Motion = Constraint.mSwing2Motion = Constraint.mTwistMotion = PhysicsRuntime::EAngularConstraintMotion::Limited;
    Constraint.mConstraintBone1 = BoneName;
    Constraint.mPos1 = FVector{};
    Constraint.mPriAxis1 = FVector{1.0f, 0.0f, 0.0f};
    Constraint.mSecAxis1 = FVector{0.0f, 1.0f, 0.0f};
    Constraint.mConstraintBone2 = Bones[ParentIndex].GetName();
    Constraint.mPos2 = RelTM.mLocation;
    Constraint.mPriAxis2 = RelTM.mRotation.RotateVector(FVector{1.0f, 0.0f, 0.0f});
    Constraint.mSecAxis2 = RelTM.mRotation.RotateVector(FVector{0.0f, 1.0f, 0.0f});

    return ConstraintIndex;
}

bool Editor::PhysicsAssetEditing::DeleteConstraint(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, Index_t ConstraintIndex) {
    if (ConstraintIndex >= PhysicsAsset.mConstraintSetup.size()) {
        return false;
    }

    PhysicsAsset.mConstraintSetup.erase(PhysicsAsset.mConstraintSetup.begin() + static_cast<std::ptrdiff_t>(ConstraintIndex));

    return true;
}

Asset::USkeleton* Editor::PhysicsAssetEditing::FindSkeletonForMesh(Asset::FAssetManager& Assets, const std::filesystem::path& MeshPath, Core::FString& OutSkeletonPath) {
    OutSkeletonPath.clear();

    if (Asset::USkeleton* Legacy{Assets.FindLegacySkeleton(MeshPath)}) {
        return Legacy;
    }

    // 임포트가 만드는 "<메시 이름>_Skeleton" 에셋이 같은 폴더에 있으면 그것을 기본값으로 쓴다.
    std::filesystem::path Sibling{MeshPath};
    Sibling.replace_filename(Sibling.stem().native() + L"_Skeleton" + Sibling.extension().native());

    Core::FString Error{};
    Asset::USkeleton* Skeleton{Assets.LoadSkeleton(Sibling, Error)};

    if (Skeleton != nullptr) {
        OutSkeletonPath = Core::FilePath::ToGenericUtf8(Sibling);
    }

    return Skeleton;
}

std::filesystem::path Editor::PhysicsAssetEditing::GetDefaultPhysicsAssetPath(const std::filesystem::path& MeshPath) {
    std::filesystem::path Result{MeshPath};
    Result.replace_filename(MeshPath.stem().native() + L"_PhysicsAsset" + MeshPath.extension().native());

    return Result;
}

std::filesystem::path Editor::PhysicsAssetEditing::GetMeshPathForPhysicsAsset(const std::filesystem::path& PhysicsAssetPath) {
    constexpr std::wstring_view Suffix{L"_PhysicsAsset"};
    std::wstring Stem{PhysicsAssetPath.stem().native()};

    // 같은 메시의 추가 물리 에셋은 "_PhysicsAsset_<숫자>"로 저장되므로 끝의 "_<숫자>"를 먼저 뗀다.
    size_t DigitStart{Stem.size()};

    while (DigitStart > 0 && Stem[DigitStart - 1] >= L'0' && Stem[DigitStart - 1] <= L'9') {
        --DigitStart;
    }

    if (DigitStart < Stem.size() && DigitStart > 0 && Stem[DigitStart - 1] == L'_' && Stem.substr(0, DigitStart - 1).ends_with(Suffix)) {
        Stem.resize(DigitStart - 1);
    }

    if (Stem.size() <= Suffix.size() || !Stem.ends_with(Suffix)) {
        return {};
    }

    std::filesystem::path Result{PhysicsAssetPath};
    Result.replace_filename(Stem.substr(0, Stem.size() - Suffix.size()) + PhysicsAssetPath.extension().native());

    return Result;
}

bool Editor::PhysicsAssetEditing::CreatePhysicsAssetFile(Asset::FAssetManager& Assets, const std::filesystem::path& MeshPath, std::filesystem::path& OutPath, Core::FString& Error) {
    Asset::USkeletalMesh* Mesh{Assets.LoadSkeletalMesh(MeshPath, Error)};
    Core::FString SkeletonPath{};
    Asset::USkeleton* Skeleton{Mesh != nullptr ? FindSkeletonForMesh(Assets, MeshPath, SkeletonPath) : nullptr};

    if (Mesh == nullptr || Skeleton == nullptr) {
        if (Error.empty()) {
            Error = "Cannot find the skeleton of this mesh. Open the Physics Asset Editor and pick one.";
        }

        return false;
    }

    OutPath = GetDefaultPhysicsAssetPath(MeshPath);

    std::error_code Code{};

    // 이미 있으면 "_PhysicsAsset_1", "_2" ... 로 비어 있는 이름을 찾아 추가로 만든다.
    for (Index_t Number{1}; std::filesystem::exists(Assets.GetContentRoot() / OutPath, Code); ++Number) {
        if (Code || Number > 999) {
            Error = "Cannot find a free physics asset name: " + Core::FilePath::ToGenericUtf8(OutPath);

            return false;
        }

        OutPath = GetDefaultPhysicsAssetPath(MeshPath);
        OutPath.replace_filename(OutPath.stem().native() + L"_" + std::to_wstring(Number) + OutPath.extension().native());
    }

    if (Code) {
        Error = "Cannot check the physics asset path: " + Core::FilePath::ToGenericUtf8(OutPath);

        return false;
    }

    auto PhysicsAsset{CoreUObject::NewObject<PhysicsRuntime::UPhysicsAsset>()};
    PhysicsAsset->SetName(Core::FilePath::ToUtf8(OutPath.stem()));
    PhysicsAsset->mPreviewMeshPath = Core::FilePath::ToGenericUtf8(MeshPath);

    if (!Asset::FPhysicsAssetUtils::CreateFromSkeletalMesh(*PhysicsAsset, *Mesh, *Skeleton, Asset::FPhysAssetCreateParams{})) {
        Error = "Cannot generate bodies from the mesh.";

        return false;
    }

    return Assets.SavePhysicsAsset(OutPath, *PhysicsAsset, Error);
}

void Editor::PhysicsAssetEditing::CollectBoneBodyPoses(PhysicsRuntime::FPhysicsScene& Scene, Core::TArray<FBodyPose>& OutPoses) {
    OutPoses.clear();

    physx::PxScene* PxSceneHandle{Scene.GetScene()};

    if (PxSceneHandle == nullptr) {
        return;
    }

    const physx::PxU32 Count{PxSceneHandle->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC)};
    Core::TArray<physx::PxActor*> Actors(Count);

    PxSceneHandle->getActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC, Actors.data(), Count);

    for (const physx::PxActor* Actor : Actors) {
        const PhysicsRuntime::FBodyInstance* Body{Actor != nullptr ? PhysicsRuntime::FBodyInstance::FromPxActor(*Actor) : nullptr};

        if (Body == nullptr || Body->mInstanceBodyIndex == InvalidIndex) {
            continue;
        }

        FBodyPose Pose{};
        Pose.mBodyIndex = Body->mInstanceBodyIndex;
        Pose.mWorld = Body->GetBodyTransform();
        Pose.mWorld.mScale = Body->GetBodyScale();
        Pose.mLinearVelocity = Body->GetLinearVelocity();
        OutPoses.push_back(Pose);
    }
}

void Editor::PhysicsAssetEditing::FindOverlappingBodyPairs(PhysicsRuntime::FPhysicsScene& Scene, Core::TArray<std::pair<Index_t, Index_t>>& OutPairs) {
    OutPairs.clear();

    physx::PxScene* PxSceneHandle{Scene.GetScene()};

    if (PxSceneHandle == nullptr) {
        return;
    }

    const physx::PxU32 Count{PxSceneHandle->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC)};
    Core::TArray<physx::PxActor*> Actors(Count);

    PxSceneHandle->getActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC, Actors.data(), Count);

    // 뼈 바디의 액터를 바디 번호와 함께 모은다. 바닥 같은 바디 번호가 없는 액터는 제외한다.
    Core::TArray<std::pair<Index_t, const physx::PxRigidActor*>> Bodies{};

    for (const physx::PxActor* Actor : Actors) {
        const PhysicsRuntime::FBodyInstance* Body{Actor != nullptr ? PhysicsRuntime::FBodyInstance::FromPxActor(*Actor) : nullptr};

        if (Body != nullptr && Body->mInstanceBodyIndex != InvalidIndex) {
            Bodies.emplace_back(Body->mInstanceBodyIndex, Actor->is<physx::PxRigidActor>());
        }
    }

    // 모든 형상 쌍을 PhysX 기하 질의로 검사한다. 한 쌍이라도 겹치면 그 바디 쌍은 겹치는 것이다.
    const auto ShapesOf{[](const physx::PxRigidActor& Actor) {
        Core::TArray<physx::PxShape*> Shapes(Actor.getNbShapes());

        Actor.getShapes(Shapes.data(), static_cast<physx::PxU32>(Shapes.size()));

        return Shapes;
    }};

    for (Index_t First{}; First < Bodies.size(); ++First) {
        const Core::TArray<physx::PxShape*> FirstShapes{ShapesOf(*Bodies[First].second)};

        for (Index_t Second{First + 1}; Second < Bodies.size(); ++Second) {
            const Core::TArray<physx::PxShape*> SecondShapes{ShapesOf(*Bodies[Second].second)};
            bool Overlaps{};

            for (const physx::PxShape* A : FirstShapes) {
                for (const physx::PxShape* B : SecondShapes) {
                    if (physx::PxGeometryQuery::overlap(A->getGeometry().any(), physx::PxShapeExt::getGlobalPose(*A, *Bodies[First].second), B->getGeometry().any(), physx::PxShapeExt::getGlobalPose(*B, *Bodies[Second].second))) {
                        Overlaps = true;

                        break;
                    }
                }

                if (Overlaps) {
                    break;
                }
            }

            if (Overlaps) {
                OutPairs.emplace_back(std::min(Bodies[First].first, Bodies[Second].first), std::max(Bodies[First].first, Bodies[Second].first));
            }
        }
    }
}

FTransform Editor::PhysicsAssetEditing::GetFloorTransform(const FBox& Bounds, FVector& OutHalfExtent) {
    // 미리보기 장면이 그리는 바닥(FPreviewScene::PrepareEnvironment)과 같은 위치와 크기로 맞춘다.
    const FVector Size{Bounds.mMaximum - Bounds.mMinimum};
    const float AssetSize{std::max({Size.x, Size.y, Size.z, 0.02f})};
    const float HalfWidth{AssetSize * 8.0f * 0.5f};
    const float HalfThickness{AssetSize * 0.18f * 0.5f};
    const float FloorTop{Bounds.mMinimum.z - AssetSize * 0.001f};

    OutHalfExtent = FVector{HalfWidth, HalfWidth, HalfThickness};

    FTransform Result{};
    Result.mLocation = FVector{(Bounds.mMinimum.x + Bounds.mMaximum.x) * 0.5f, (Bounds.mMinimum.y + Bounds.mMaximum.y) * 0.5f, FloorTop - HalfThickness};

    return Result;
}

std::unique_ptr<PhysicsRuntime::FBodyInstance> Editor::PhysicsAssetEditing::CreateFloorBody(PhysicsRuntime::FPhysicsScene& Scene, const FBox& Bounds) {
    FVector HalfExtent{};
    const FTransform Transform{GetFloorTransform(Bounds, HalfExtent)};

    // 형상은 PhysX Shape로 복사되므로 설명서는 이 함수 안에서만 필요하다.
    const std::unique_ptr<PhysicsRuntime::UBodySetup> Setup{CoreUObject::NewObject<PhysicsRuntime::UBodySetup>()};
    PhysicsRuntime::FKBoxElem Element{};
    Element.mHalfExtent = HalfExtent;
    Setup->GetAggGeom().mBoxElems.push_back(Element);

    auto Body{std::make_unique<PhysicsRuntime::FBodyInstance>()};

    if (!Body->InitBody(*Setup, Transform, nullptr, Scene, PhysicsRuntime::FInitBodySpawnParams{true})) {
        return nullptr;
    }

    return Body;
}

bool Editor::PhysicsAssetEditing::PickBody(PhysicsRuntime::FPhysicsScene& Scene, const FRay& Ray, float MaxDistance, Index_t& OutBodyIndex, Index_t& OutShapeOrdinal) {
    physx::PxScene* PxSceneHandle{Scene.GetScene()};

    if (PxSceneHandle == nullptr) {
        return false;
    }

    physx::PxRaycastBuffer Hit{};

    if (!PxSceneHandle->raycast(PhysicsRuntime::ToPxVec3(Ray.mOrigin), PhysicsRuntime::ToPxVec3(Ray.mDirection), MaxDistance, Hit) || !Hit.hasBlock || Hit.block.actor == nullptr) {
        return false;
    }

    const PhysicsRuntime::FBodyInstance* Body{PhysicsRuntime::FBodyInstance::FromPxActor(*Hit.block.actor)};

    if (Body == nullptr || Body->mInstanceBodyIndex == InvalidIndex) {
        return false;
    }

    OutBodyIndex = Body->mInstanceBodyIndex;
    OutShapeOrdinal = InvalidIndex;

    // 맞은 Shape가 Actor의 Shape 목록에서 몇 번째인지 찾는다. 바디 생성 때 붙인 순서와 같다.
    const physx::PxRigidActor* RigidActor{Hit.block.actor->is<physx::PxRigidActor>()};

    if (RigidActor != nullptr && Hit.block.shape != nullptr) {
        const physx::PxU32 Count{RigidActor->getNbShapes()};
        Core::TArray<physx::PxShape*> Shapes(Count);

        RigidActor->getShapes(Shapes.data(), Count);

        for (physx::PxU32 Ordinal{}; Ordinal < Count; ++Ordinal) {
            if (Shapes[Ordinal] == Hit.block.shape) {
                OutShapeOrdinal = Ordinal;

                break;
            }
        }
    }

    return true;
}

struct Editor::PhysicsAssetEditing::FBodyGrab::FImpl {
    /**
     * @brief 잡고 있는 Dynamic Actor입니다. 비소유이며 물리 장면이 소유합니다.
     */
    physx::PxRigidDynamic* mActor{};

    /**
     * @brief 잡은 점을 Actor 로컬 좌표로 나타낸 위치입니다.
     */
    physx::PxVec3 mLocalPoint{};

    /**
     * @brief 잡을 때의 카메라에서 잡은 점까지의 거리이며, 목표점은 이 거리만큼 광선을 따라간 위치입니다.
     */
    float mDistance{};

    /**
     * @brief 마우스 광선이 가리키는 목표점입니다.
     */
    physx::PxVec3 mTarget{};
};

Editor::PhysicsAssetEditing::FBodyGrab::FBodyGrab() = default;

Editor::PhysicsAssetEditing::FBodyGrab::~FBodyGrab() = default;

bool Editor::PhysicsAssetEditing::FBodyGrab::Begin(PhysicsRuntime::FPhysicsScene& Scene, const FRay& Ray, float MaxDistance) {
    mImpl.reset();

    physx::PxScene* PxSceneHandle{Scene.GetScene()};

    if (PxSceneHandle == nullptr) {
        return false;
    }

    physx::PxRaycastBuffer Hit{};

    if (!PxSceneHandle->raycast(PhysicsRuntime::ToPxVec3(Ray.mOrigin), PhysicsRuntime::ToPxVec3(Ray.mDirection), MaxDistance, Hit) || !Hit.hasBlock || Hit.block.actor == nullptr) {
        return false;
    }

    // 뼈 바디만 잡는다. 바닥(바디 번호 없음)과 Kinematic 바디는 제외한다.
    const PhysicsRuntime::FBodyInstance* Body{PhysicsRuntime::FBodyInstance::FromPxActor(*Hit.block.actor)};
    physx::PxRigidDynamic* Dynamic{Hit.block.actor->is<physx::PxRigidDynamic>()};

    if (Body == nullptr || Body->mInstanceBodyIndex == InvalidIndex || Dynamic == nullptr || (Dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
        return false;
    }

    mImpl = std::make_unique<FImpl>();
    mImpl->mActor = Dynamic;
    mImpl->mLocalPoint = Dynamic->getGlobalPose().transformInv(Hit.block.position);
    mImpl->mDistance = Hit.block.distance;
    mImpl->mTarget = Hit.block.position;

    return true;
}

void Editor::PhysicsAssetEditing::FBodyGrab::Update(const FRay& Ray) {
    if (mImpl == nullptr) {
        return;
    }

    physx::PxRigidDynamic& Actor{*mImpl->mActor};

    mImpl->mTarget = PhysicsRuntime::ToPxVec3(Ray.mOrigin + Ray.mDirection * mImpl->mDistance);

    const physx::PxTransform Pose{Actor.getGlobalPose()};
    const physx::PxVec3 Point{Pose.transform(mImpl->mLocalPoint)};
    const physx::PxVec3 PointVelocity{physx::PxRigidBodyExt::getVelocityAtPos(Actor, Point)};

    // 질량에 비례하는 스프링-댐퍼. 고유 진동수 ω에서 가속도 = ω²·오차 - 2ζω·속도(ζ=1, 임계 감쇠)이다.
    constexpr float Omega{15.0f};
    constexpr float MaxAcceleration{400.0f};
    physx::PxVec3 Acceleration{(mImpl->mTarget - Point) * (Omega * Omega) - PointVelocity * (2.0f * Omega)};

    if (Acceleration.magnitude() > MaxAcceleration) {
        Acceleration = Acceleration.getNormalized() * MaxAcceleration;
    }

    Actor.wakeUp();
    physx::PxRigidBodyExt::addForceAtPos(Actor, Acceleration * Actor.getMass(), Point, physx::PxForceMode::eFORCE, true);
}

void Editor::PhysicsAssetEditing::FBodyGrab::End() {
    mImpl.reset();
}

bool Editor::PhysicsAssetEditing::FBodyGrab::IsActive() const {
    return mImpl != nullptr;
}

bool Editor::PhysicsAssetEditing::FBodyGrab::GetEndpoints(FVector& OutGrabPoint, FVector& OutTarget) const {
    if (mImpl == nullptr) {
        return false;
    }

    const physx::PxVec3 Point{mImpl->mActor->getGlobalPose().transform(mImpl->mLocalPoint)};

    OutGrabPoint = PhysicsRuntime::ToFVector(Point);
    OutTarget = PhysicsRuntime::ToFVector(mImpl->mTarget);

    return true;
}
