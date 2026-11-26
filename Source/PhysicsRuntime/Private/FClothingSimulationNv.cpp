/**
 * @file FClothingSimulationNv.cpp
 * @brief NvCloth 솔버로 천 입자를 시뮬레이션하는 클래스를 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FClothingSimulationNv.h"
#include "PhysicsRuntime/ClothingMeshUtils.h"
#include "PhysicsRuntime/FClothingSystemRuntimeModuleNv.h"
#include "PhysicsRuntime/PhysicsConversion.h"
#include "PhysicsRuntime/PhysicsStats.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"
#include "PhysicsRuntime/UPhysicsAsset.h"
#include "Core/Log/Log.h"

#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <NvClothExt/ClothMeshDesc.h>
#include <NvClothExt/ClothMeshQuadifier.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace PhysicsRuntime {

    /**
     * @brief 천 시뮬레이션 로그 범주, 상수, 내부 함수입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogCloth, Log, All);

        /**
         * @brief 한 프레임에 진행할 최대 시간(초)입니다. 에디터가 멈췄다 풀릴 때 천이 튀지 않게 합니다.
         */
        constexpr float MaxDeltaTime{1.0f / 30.0f};

        /**
         * @brief 격자 테스트 천의 속도 감쇠 비율입니다. 흔들림이 시간이 지나면 멈추게 합니다.
         */
        constexpr float GridDamping{0.2f};

        /**
         * @brief NvCloth가 받는 구, 캡슐, 평면의 최대 개수입니다. 각각 32비트 마스크로 다루기 때문입니다.
         */
        constexpr Index_t MaxClothCollisionShapes{32};

        /**
         * @brief 배열 전체를 NvCloth 읽기 범위로 바꿉니다.
         * @param[in] Values 가리킬 배열입니다.
         * @return 배열의 처음부터 끝까지를 가리키는 범위입니다.
         */
        template <typename T>
        nv::cloth::Range<const T> MakeRange(const Core::TArray<T>& Values) {
            return nv::cloth::Range<const T>{Values.data(), Values.data() + Values.size()};
        }

        /**
         * @brief 뼈 하나의 컴포넌트 좌표 변환을 구합니다.
         * @param[in] ComponentSpaceTransforms 스켈레톤 뼈 순서의 컴포넌트 좌표 뼈 행렬입니다.
         * @param[in] BoneIndex 뼈 번호입니다.
         * @return 뼈 변환이며 뼈가 없거나 변환으로 바꿀 수 없으면 단위 변환입니다.
         */
        FTransform GetBoneTransformCS(const Core::TArray<FMatrix>& ComponentSpaceTransforms, Index_t BoneIndex) {
            FTransform Result{};

            if (BoneIndex >= ComponentSpaceTransforms.size() || !ComponentSpaceTransforms[BoneIndex].TryGetTransform(Result)) {
                return FTransform{};
            }

            return Result;
        }

        /**
         * @brief 기준 뼈의 월드 변환을 구합니다.
         * @param[in] ComponentToWorld 컴포넌트의 월드 행렬입니다.
         * @param[in] ComponentSpaceTransforms 스켈레톤 뼈 순서의 컴포넌트 좌표 뼈 행렬입니다.
         * @param[in] ReferenceBoneIndex 기준 뼈 번호입니다.
         * @return 기준 뼈의 월드 변환이며 변환으로 바꿀 수 없으면 단위 변환입니다.
         */
        FTransform GetSimRootWorldTransform(const FMatrix& ComponentToWorld, const Core::TArray<FMatrix>& ComponentSpaceTransforms, Index_t ReferenceBoneIndex) {
            const FMatrix SimRootMatrix{ReferenceBoneIndex < ComponentSpaceTransforms.size() ? ComponentSpaceTransforms[ReferenceBoneIndex] * ComponentToWorld : ComponentToWorld};
            FTransform SimRootTransform{};

            if (!SimRootMatrix.TryGetTransform(SimRootTransform)) {
                return FTransform{};
            }

            return SimRootTransform;
        }

        /**
         * @brief 기준 뼈의 월드 변환을 천의 위치와 회전으로 넣습니다.
         * @param[in,out] Cloth 변환을 넣을 천입니다.
         * @param[in] ComponentToWorld 컴포넌트의 월드 행렬입니다.
         * @param[in] ComponentSpaceTransforms 스켈레톤 뼈 순서의 컴포넌트 좌표 뼈 행렬입니다.
         * @param[in] ReferenceBoneIndex 기준 뼈 번호입니다.
         */
        void SetSimRootTransform(nv::cloth::Cloth& Cloth, const FMatrix& ComponentToWorld, const Core::TArray<FMatrix>& ComponentSpaceTransforms, Index_t ReferenceBoneIndex) {
            const FMatrix SimRootMatrix{ReferenceBoneIndex < ComponentSpaceTransforms.size() ? ComponentSpaceTransforms[ReferenceBoneIndex] * ComponentToWorld : ComponentToWorld};
            FTransform SimRootTransform{};

            if (!SimRootMatrix.TryGetTransform(SimRootTransform)) {
                return;
            }

            Cloth.setTranslation(ToPxVec3(SimRootTransform.mLocation));
            Cloth.setRotation(ToPxQuat(SimRootTransform.mRotation));
        }

        /**
         * @brief 천 상세 단계 하나의 천과 패브릭을 해제합니다.
         * @param[in,out] LodData 해제할 데이터이며 주소를 비웁니다.
         */
        void ReleaseLodData(FClothingActorNv::FActorLodData& LodData) {
            delete LodData.mCloth;
            LodData.mCloth = nullptr;

            if (LodData.mFabric != nullptr) {
                LodData.mFabric->decRefCount();
                LodData.mFabric = nullptr;
            }
        }
    }

    /**
     * @brief 현재 뼈 포즈로 현재 천 상세 단계의 입자를 스키닝해 현재 스키닝 버퍼에 저장합니다.
     */
    void FClothingActorNv::SkinPhysicsMesh(const FClothingSimulationContextCommon& Context) {
        if (mAssetCreatedFrom == nullptr || mCurrentLodIndex >= mAssetCreatedFrom->mLodData.size()) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsStats::ClothSkinPhysMesh};
        const FClothPhysicalMeshData& PhysMesh{mAssetCreatedFrom->mLodData[mCurrentLodIndex].mPhysicalMeshData};
        const FTransform RootBoneTransform{GetBoneTransformCS(Context.mBoneTransforms, mAssetCreatedFrom->mReferenceBoneIndex)};

        ClothingMeshUtils::SkinPhysicsMesh(mAssetCreatedFrom->mUsedBoneIndices, PhysMesh, RootBoneTransform, Context.mRefToLocals, mSkinnedPhysicsMeshPositions[mCurrentSkinnedPositionIndex], mSkinnedPhysicsMeshNormals);
    }

    /**
     * @brief 현재 스키닝 위치로 모션 제약과 분리 제약을 갱신합니다.
     */
    void FClothingActorNv::UpdateMotionConstraints(const FClothingSimulationContextCommon& Context) {
        if (mAssetCreatedFrom == nullptr || mCurrentLodIndex >= mLodData.size() || mCurrentLodIndex >= mAssetCreatedFrom->mLodData.size() || mLodData[mCurrentLodIndex].mCloth == nullptr) {
            return;
        }

        const Core::TArray<FVector>& CurrentSkinnedPositions{GetCurrentSkinnedPositions()};
        nv::cloth::Cloth& CurrentCloth{*mLodData[mCurrentLodIndex].mCloth};
        const FClothPhysicalMeshData& PhysMesh{mAssetCreatedFrom->mLodData[mCurrentLodIndex].mPhysicalMeshData};

        // 입자마다 이번 프레임 스키닝 위치를 중심으로 하는 구를 주어 그 밖으로 나가지 못하게 합니다.
        nv::cloth::Range<physx::PxVec4> MotionConstraints{CurrentCloth.getMotionConstraints()};
        const Index_t NumMotionConstraints{std::min<Index_t>(CurrentCloth.getNumMotionConstraints(), CurrentSkinnedPositions.size())};
        const FPointWeightMap* const MaxDistances{PhysMesh.FindWeightMap(EWeightMapTargetCommon::MaxDistance)};

        if (MaxDistances != nullptr && MaxDistances->Num() > 0 && MaxDistances->Num() >= NumMotionConstraints) {
            for (Index_t ConstraintIndex{}; ConstraintIndex < NumMotionConstraints; ++ConstraintIndex) {
                MotionConstraints[static_cast<Uint32>(ConstraintIndex)] = physx::PxVec4{ToPxVec3(CurrentSkinnedPositions[ConstraintIndex]), (*MaxDistances)[ConstraintIndex] * Context.mMaxDistanceScale};
            }
        } else {
            // 최대 거리를 칠하지 않았으면 반지름을 무한으로 두어 제한하지 않습니다.
            for (Index_t ConstraintIndex{}; ConstraintIndex < NumMotionConstraints; ++ConstraintIndex) {
                MotionConstraints[static_cast<Uint32>(ConstraintIndex)] = physx::PxVec4{ToPxVec3(CurrentSkinnedPositions[ConstraintIndex]), std::numeric_limits<float>::max()};
            }
        }

        // 뒤쪽 거리와 반지름을 칠했으면 법선 반대쪽에 구를 두어 입자가 몸 안으로 파고들지 못하게 합니다.
        const FPointWeightMap* const BackstopDistances{PhysMesh.FindWeightMap(EWeightMapTargetCommon::BackstopDistance)};
        const FPointWeightMap* const BackstopRadiuses{PhysMesh.FindWeightMap(EWeightMapTargetCommon::BackstopRadius)};

        if (BackstopDistances != nullptr && BackstopDistances->Num() > 0 && BackstopRadiuses != nullptr && BackstopRadiuses->Num() > 0) {
            nv::cloth::Range<physx::PxVec4> SeparationConstraints{CurrentCloth.getSeparationConstraints()};
            const Index_t NumSeparationConstraints{std::min({static_cast<Index_t>(CurrentCloth.getNumSeparationConstraints()), CurrentSkinnedPositions.size(), mSkinnedPhysicsMeshNormals.size(), BackstopDistances->Num(), BackstopRadiuses->Num()})};

            for (Index_t ConstraintIndex{}; ConstraintIndex < NumSeparationConstraints; ++ConstraintIndex) {
                const FVector Center{CurrentSkinnedPositions[ConstraintIndex] - mSkinnedPhysicsMeshNormals[ConstraintIndex] * (*BackstopDistances)[ConstraintIndex]};

                SeparationConstraints[static_cast<Uint32>(ConstraintIndex)] = physx::PxVec4{ToPxVec3(Center), (*BackstopRadiuses)[ConstraintIndex]};
            }
        }
    }

    /**
     * @brief 이번 프레임에 스키닝한 입자 위치를 반환합니다.
     */
    const Core::TArray<FVector>& FClothingActorNv::GetCurrentSkinnedPositions() const {
        return mSkinnedPhysicsMeshPositions[mCurrentSkinnedPositionIndex];
    }

    /**
     * @brief 지난 프레임에 스키닝한 입자 위치를 반환합니다.
     */
    const Core::TArray<FVector>& FClothingActorNv::GetPreviousSkinnedPositions() const {
        return mSkinnedPhysicsMeshPositions[(mCurrentSkinnedPositionIndex + 1) % 2];
    }

    /**
     * @brief 충돌체가 바뀌었으면 천 에셋, 물리 에셋, 바깥 충돌체를 하나의 목록으로 다시 모읍니다.
     */
    void FClothingActorNv::ConditionalRebuildCollisions() {
        if (!mCollisionsDirty || mAssetCreatedFrom == nullptr || mCurrentLodIndex >= mAssetCreatedFrom->mLodData.size()) {
            return;
        }

        mAggregatedCollisions.Reset();

        // 천 에셋에 넣어 둔 충돌체, 물리 에셋의 충돌체, 바깥 충돌체 순서로 붙입니다.
        mAggregatedCollisions.Append(mAssetCreatedFrom->mLodData[mCurrentLodIndex].mCollisionData);
        mAggregatedCollisions.Append(mExtractedCollisions);
        mAggregatedCollisions.Append(mExternalCollisions);

        Index_t NumPlanes{};

        for (const FClothCollisionPrim_Convex& Convex : mAggregatedCollisions.mConvexes) {
            NumPlanes += Convex.mPlanes.size();
        }

        // NvCloth는 앞의 32개만 쓰므로 넘으면 뒤쪽 충돌체가 빠진다는 것을 알립니다.
        if (mAggregatedCollisions.mSpheres.size() > MaxClothCollisionShapes || mAggregatedCollisions.mSphereConnections.size() > MaxClothCollisionShapes || NumPlanes > MaxClothCollisionShapes) {
            MACAW_LOG(LogCloth, Warning, "Cloth collision has {} spheres, {} capsules and {} planes. Only the first {} of each are used. Assign a smaller physics asset to the clothing asset.", mAggregatedCollisions.mSpheres.size(), mAggregatedCollisions.mSphereConnections.size(), NumPlanes, MaxClothCollisionShapes);
        }

        mCollisionsDirty = false;
    }

    /**
     * @brief 배열을 비우되 할당한 메모리는 남겨 다음 프레임에 다시 씁니다.
     */
    void FClothingActorScratchData::Reset() {
        mSphereData.clear();
        mCapsuleSphereIndices.clear();
        mPlaneData.clear();
        mConvexMasks.clear();
    }

    /**
     * @brief 바람을 천에 넣습니다.
     */
    void FClothingActorNv::UpdateWind(const FClothingSimulationContextCommon& Context, const FVector& WindVelocity, float WindAdaption) {
        if (mAssetCreatedFrom == nullptr || mCurrentLodIndex >= mLodData.size() || mCurrentLodIndex >= mAssetCreatedFrom->mLodData.size() || mLodData[mCurrentLodIndex].mCloth == nullptr) {
            return;
        }

        nv::cloth::Cloth& CurrentCloth{*mLodData[mCurrentLodIndex].mCloth};

        switch (mWindMethod) {
            case EClothingWindMethodNv::Legacy: {
                // 바람을 천 좌표로 바꾸고, 입자마다 바람 속도와의 차이를 면 방향에 맞춰 가속도로 넣습니다.
                const FTransform RootBoneWorldTransform{GetSimRootWorldTransform(Context.mComponentToWorld, Context.mBoneTransforms, mAssetCreatedFrom->mReferenceBoneIndex)};
                const FVector TransformedWindVelocity{RootBoneWorldTransform.InverseTransformVectorNoScale(WindVelocity)};
                Core::TArray<FVector>& ParticleVelocities{mScratch.mParticleVelocities};

                CalculateParticleVelocities(ParticleVelocities);

                const Core::TArray<float>& InverseMasses{mAssetCreatedFrom->mLodData[mCurrentLodIndex].mPhysicalMeshData.mInverseMasses};
                nv::cloth::Range<physx::PxVec4> ParticleAccelerations{CurrentCloth.getParticleAccelerations()};
                const Index_t NumAccelerations{static_cast<Index_t>(ParticleAccelerations.size())};

                // 전체 범위를 써야 하므로 바람을 받지 않는 입자도 0으로 채웁니다.
                for (Index_t AccelerationIndex{}; AccelerationIndex < NumAccelerations; ++AccelerationIndex) {
                    const bool Valid{AccelerationIndex < ParticleVelocities.size() && AccelerationIndex < InverseMasses.size() && AccelerationIndex < mCurrentNormals.size()};
                    FVector VelocityDelta{Valid ? TransformedWindVelocity - ParticleVelocities[AccelerationIndex] : FVector{}};
                    const float DeltaLengthSquared{VelocityDelta.LengthSquared()};

                    if (Valid && InverseMasses[AccelerationIndex] > 0.0f && DeltaLengthSquared > 1.e-12f) {
                        // 바람이 면에 수직일수록 세게 받고, 면을 스치듯 지나가면 약하게 받습니다.
                        const float DirectionDot{(VelocityDelta * (1.0f / std::sqrt(DeltaLengthSquared))).Dot(mCurrentNormals[AccelerationIndex])};
                        VelocityDelta *= std::min(1.0f, std::abs(DirectionDot) * WindAdaption);
                        ParticleAccelerations[static_cast<Uint32>(AccelerationIndex)] = physx::PxVec4{VelocityDelta.x, VelocityDelta.y, VelocityDelta.z, 0.0f};
                    } else {
                        ParticleAccelerations[static_cast<Uint32>(AccelerationIndex)] = physx::PxVec4{0.0f, 0.0f, 0.0f, 0.0f};
                    }
                }

                break;
            }
            case EClothingWindMethodNv::Accurate:
                // NvCloth가 삼각형마다 항력과 양력을 계산합니다.
                CurrentCloth.setWindVelocity(ToPxVec3(WindVelocity));
                break;
            default:
                break;
        }
    }

    /**
     * @brief 입자를 스키닝 위치 쪽으로 당기는 스프링 가속도를 더합니다.
     */
    void FClothingActorNv::UpdateAnimDrive(const FClothingSimulationContextCommon&) {
        if (mAssetCreatedFrom == nullptr || mCurrentLodIndex >= mLodData.size() || mCurrentLodIndex >= mAssetCreatedFrom->mLodData.size() || mLodData[mCurrentLodIndex].mCloth == nullptr) {
            return;
        }

        const FClothPhysicalMeshData& PhysMesh{mAssetCreatedFrom->mLodData[mCurrentLodIndex].mPhysicalMeshData};
        const FPointWeightMap* const AnimDriveMultipliers{PhysMesh.FindWeightMap(EWeightMapTargetCommon::AnimDriveMultiplier)};

        if (AnimDriveMultipliers == nullptr || AnimDriveMultipliers->Num() == 0) {
            return;
        }

        nv::cloth::Cloth& CurrentCloth{*mLodData[mCurrentLodIndex].mCloth};
        const Core::TArray<FVector>& CurrentSkinPositions{GetCurrentSkinnedPositions()};
        const Core::TArray<FVector>& PreviousSkinPositions{GetPreviousSkinnedPositions()};
        nv::cloth::Range<physx::PxVec4> Accelerations{CurrentCloth.getParticleAccelerations()};
        const Index_t NumAccelerations{static_cast<Index_t>(Accelerations.size())};

        // 정확한 바람 방식은 가속도를 매 프레임 덮어쓰지 않으므로, 더하기 전에 비워 지난 프레임 값이 쌓이지 않게 합니다.
        if (mWindMethod != EClothingWindMethodNv::Legacy) {
            for (Index_t AccelerationIndex{}; AccelerationIndex < NumAccelerations; ++AccelerationIndex) {
                Accelerations[static_cast<Uint32>(AccelerationIndex)] = physx::PxVec4{0.0f, 0.0f, 0.0f, 0.0f};
            }
        }

        nv::cloth::MappedRange<physx::PxVec4> Locations{CurrentCloth.getCurrentParticles()};
        nv::cloth::MappedRange<physx::PxVec4> PrevLocations{CurrentCloth.getPreviousParticles()};
        const Index_t NumParticles{std::min({NumAccelerations, static_cast<Index_t>(Locations.size()), static_cast<Index_t>(PrevLocations.size()), AnimDriveMultipliers->Num(), CurrentSkinPositions.size(), PreviousSkinPositions.size()})};

        for (Index_t ParticleIndex{}; ParticleIndex < NumParticles; ++ParticleIndex) {
            const physx::PxVec4& Location{Locations[static_cast<Uint32>(ParticleIndex)]};
            const physx::PxVec4& PrevLocation{PrevLocations[static_cast<Uint32>(ParticleIndex)]};

            // W는 역질량이며 0이면 고정 입자라 당기지 않습니다.
            if (Location.w == 0.0f) {
                continue;
            }

            const FVector ToSkinned{CurrentSkinPositions[ParticleIndex] - FVector{Location.x, Location.y, Location.z}};
            const FVector ParticleVelocity{Location.x - PrevLocation.x, Location.y - PrevLocation.y, Location.z - PrevLocation.z};
            const FVector SkinnedVelocity{CurrentSkinPositions[ParticleIndex] - PreviousSkinPositions[ParticleIndex]};

            // 가속도 = (스프링 힘 - 감쇠 힘) / 질량이며, 질량의 역수가 W입니다.
            const FVector AddedAcceleration{(ToSkinned * (mCurrentAnimDriveSpringStiffness * (*AnimDriveMultipliers)[ParticleIndex]) - (ParticleVelocity - SkinnedVelocity) * mCurrentAnimDriveDamperStiffness) * Location.w};

            Accelerations[static_cast<Uint32>(ParticleIndex)] += physx::PxVec4{AddedAcceleration.x, AddedAcceleration.y, AddedAcceleration.z, 0.0f};
        }
    }

    /**
     * @brief 지난 시뮬레이션의 입자 위치 차이와 시간으로 입자 속도를 구합니다.
     */
    void FClothingActorNv::CalculateParticleVelocities(Core::TArray<FVector>& OutVelocities) {
        if (mCurrentLodIndex >= mLodData.size() || mLodData[mCurrentLodIndex].mCloth == nullptr) {
            OutVelocities.clear();

            return;
        }

        nv::cloth::Cloth& CurrentCloth{*mLodData[mCurrentLodIndex].mCloth};
        const Core::FFrequency InverseTimestep{mPreviousTimestep != Core::FTime{} ? 1.0f / mPreviousTimestep : Core::FFrequency{}};
        nv::cloth::MappedRange<physx::PxVec4> PreviousPositions{CurrentCloth.getPreviousParticles()};
        nv::cloth::MappedRange<physx::PxVec4> CurrentPositions{CurrentCloth.getCurrentParticles()};
        const Index_t NumParticles{static_cast<Index_t>(std::min(PreviousPositions.size(), CurrentPositions.size()))};

        OutVelocities.resize(NumParticles);

        for (Index_t ParticleIndex{}; ParticleIndex < NumParticles; ++ParticleIndex) {
            const physx::PxVec4& OldPosition{PreviousPositions[static_cast<Uint32>(ParticleIndex)]};
            const physx::PxVec4& NewPosition{CurrentPositions[static_cast<Uint32>(ParticleIndex)]};

            OutVelocities[ParticleIndex] = FVector{NewPosition.x - OldPosition.x, NewPosition.y - OldPosition.y, NewPosition.z - OldPosition.z} * InverseTimestep.GetBase();
        }
    }

    /**
     * @brief 솔버가 없는 빈 시뮬레이션을 구성합니다.
     */
    FClothingSimulationNv::FClothingSimulationNv() = default;

    /**
     * @brief 등록한 천과 솔버를 해제합니다.
     */
    FClothingSimulationNv::~FClothingSimulationNv() {
        Shutdown();
    }

    /**
     * @brief 천 시스템 모듈의 팩토리로 솔버를 만듭니다.
     */
    bool FClothingSimulationNv::Initialize(const FClothingSystemRuntimeModuleNv& Module) {
        if (mSolver != nullptr) {
            return true;
        }

        if (Module.GetSoftwareFactory() == nullptr || Module.GetMeshQuadifier() == nullptr) {
            MACAW_LOG(LogCloth, Error, "Cannot initialize cloth simulation without a factory.");

            return false;
        }

        mFactory = Module.GetSoftwareFactory();
        mMeshQuadifier = Module.GetMeshQuadifier();
        mSolver = mFactory->createSolver();

        if (mSolver == nullptr) {
            MACAW_LOG(LogCloth, Error, "Cannot create the NvCloth solver.");
            mFactory = nullptr;
            mMeshQuadifier = nullptr;

            return false;
        }

        return true;
    }

    /**
     * @brief 등록한 천을 모두 해제한 뒤 솔버를 해제합니다.
     */
    void FClothingSimulationNv::Shutdown() {
        DestroyActors();

        delete mSolver;
        mSolver = nullptr;
        mFactory = nullptr;
        mMeshQuadifier = nullptr;
    }

    /**
     * @brief 한 변을 고정한 격자 천을 만들어 솔버에 등록합니다.
     */
    bool FClothingSimulationNv::CreateGridActor(const FVector& Origin, Uint32 Width, Uint32 Height, float Spacing) {
        if (mSolver == nullptr || Width < 2 || Height < 2 || !(Spacing > 0.0f)) {
            return false;
        }

        DestroyActors();

        const Uint32 NumParticles{Width * Height};
        Core::TArray<physx::PxVec3> Points{};
        Core::TArray<float> InvMasses{};
        Core::TArray<Uint32> Triangles{};

        Points.reserve(NumParticles);
        InvMasses.reserve(NumParticles);
        Triangles.reserve(static_cast<Index_t>(Width - 1) * (Height - 1) * 6);

        for (Uint32 Row{}; Row < Height; ++Row) {
            for (Uint32 Column{}; Column < Width; ++Column) {
                Points.push_back(ToPxVec3(Origin + FVector{static_cast<float>(Column) * Spacing, static_cast<float>(Row) * Spacing, 0.0f}));
                InvMasses.push_back(Row == 0 ? 0.0f : 1.0f);
            }
        }

        for (Uint32 Row{}; Row + 1 < Height; ++Row) {
            for (Uint32 Column{}; Column + 1 < Width; ++Column) {
                const Uint32 Corner{Row * Width + Column};

                Triangles.insert(Triangles.end(), {Corner, Corner + Width, Corner + 1});
                Triangles.insert(Triangles.end(), {Corner + 1, Corner + Width, Corner + Width + 1});
            }
        }

        nv::cloth::ClothMeshDesc MeshDesc{};
        MeshDesc.points.data = Points.data();
        MeshDesc.points.count = NumParticles;
        MeshDesc.points.stride = sizeof(physx::PxVec3);
        MeshDesc.invMasses.data = InvMasses.data();
        MeshDesc.invMasses.count = NumParticles;
        MeshDesc.invMasses.stride = sizeof(float);
        MeshDesc.triangles.data = Triangles.data();
        MeshDesc.triangles.count = static_cast<Uint32>(Triangles.size() / 3);
        MeshDesc.triangles.stride = sizeof(Uint32) * 3;

        nv::cloth::Vector<int32_t>::Type PhaseTypes{};
        nv::cloth::Fabric* Fabric{NvClothCookFabricFromMesh(mFactory, MeshDesc, ToPxVec3(mGravity.Normalized()), &PhaseTypes, true)};

        if (Fabric == nullptr) {
            MACAW_LOG(LogCloth, Error, "Cannot cook a cloth fabric for the {}x{} grid.", Width, Height);

            return false;
        }

        FClothingActorNv::FActorLodData LodData{};
        LodData.mFabric = Fabric;
        LodData.mRestPositions.reserve(NumParticles);

        for (Uint32 Index{}; Index < NumParticles; ++Index) {
            LodData.mRestPositions.emplace_back(Points[Index], InvMasses[Index]);
        }

        LodData.mCloth = mFactory->createCloth(MakeRange(LodData.mRestPositions), *Fabric);

        if (LodData.mCloth == nullptr) {
            MACAW_LOG(LogCloth, Error, "Cannot create cloth for the {}x{} grid.", Width, Height);
            ReleaseLodData(LodData);

            return false;
        }

        // 위상마다 설정을 넣어야 솔버가 그 위상의 거리 제약을 풉니다.
        LodData.mPhaseConfigs.resize(Fabric->getNumPhases());

        for (Index_t PhaseIndex{}; PhaseIndex < LodData.mPhaseConfigs.size(); ++PhaseIndex) {
            LodData.mPhaseConfigs[PhaseIndex].mPhaseIndex = static_cast<uint16_t>(PhaseIndex);
            LodData.mPhaseTypes.push_back(PhaseTypes[static_cast<Uint32>(PhaseIndex)]);
        }

        LodData.mCloth->setPhaseConfig(MakeRange(LodData.mPhaseConfigs));
        LodData.mCloth->setDamping(physx::PxVec3{GridDamping, GridDamping, GridDamping});

        mSolver->addCloth(LodData.mCloth);

        FClothingActorNv& NewActor{mActors.emplace_back()};
        NewActor.mLodData.push_back(std::move(LodData));
        NewActor.mCurrentLodIndex = 0;
        mGridWidth = Width;
        mGridHeight = Height;

        MACAW_LOG(LogCloth, Log, "Created a {}x{} grid cloth with {} phases.", Width, Height, NewActor.mLodData[0].mPhaseConfigs.size());

        return true;
    }

    /**
     * @brief 천 에셋의 상세 단계마다 NvCloth 천을 만들고 현재 상세 단계의 천을 솔버에 넣습니다.
     */
    bool FClothingSimulationNv::CreateActor(const UClothingAssetCommon& Asset, Index_t SimDataIndex, const FMatrix& ComponentToWorld, const Core::TArray<FMatrix>& ComponentSpaceTransforms, const Core::TArray<FMatrix>& RefToLocals) {
        if (mSolver == nullptr || mMeshQuadifier == nullptr) {
            return false;
        }

        FClothingActorNv NewActor{};
        NewActor.mSimDataIndex = SimDataIndex;
        NewActor.mAssetCreatedFrom = &Asset;

        // 이미 자세를 취한 메시에서도 천이 제자리에 생기도록 입자를 현재 자세로 스키닝합니다.
        const FTransform SimBoneTransformCS{GetBoneTransformCS(ComponentSpaceTransforms, Asset.mReferenceBoneIndex)};

        for (Index_t LodIndex{}; LodIndex < Asset.mLodData.size(); ++LodIndex) {
            const FClothPhysicalMeshData& PhysMesh{Asset.mLodData[LodIndex].mPhysicalMeshData};
            const Index_t NumVerts{PhysMesh.mVertices.size()};
            const Index_t NumTriangles{PhysMesh.mIndices.size() / 3};

            if (NumVerts == 0 || NumTriangles == 0 || PhysMesh.mInverseMasses.size() != NumVerts) {
                MACAW_LOG(LogCloth, Error, "Cannot create cloth LOD{}. The sim mesh is empty.", LodIndex);

                for (FClothingActorNv::FActorLodData& CreatedLod : NewActor.mLodData) {
                    ReleaseLodData(CreatedLod);
                }

                return false;
            }

            Core::TArray<FVector> SkinnedVerts{};
            Core::TArray<FVector> SkinnedNormals{};
            ClothingMeshUtils::SkinPhysicsMesh(Asset.mUsedBoneIndices, PhysMesh, SimBoneTransformCS, RefToLocals, SkinnedVerts, SkinnedNormals);

            Core::TArray<physx::PxVec3> Points{};
            Points.reserve(NumVerts);

            for (const FVector& SkinnedVert : SkinnedVerts) {
                Points.push_back(ToPxVec3(SkinnedVert));
            }

            nv::cloth::ClothMeshDesc MeshDesc{};
            MeshDesc.points.data = Points.data();
            MeshDesc.points.count = static_cast<Uint32>(NumVerts);
            MeshDesc.points.stride = sizeof(physx::PxVec3);
            MeshDesc.triangles.data = PhysMesh.mIndices.data();
            MeshDesc.triangles.count = static_cast<Uint32>(NumTriangles);
            MeshDesc.triangles.stride = sizeof(Uint32) * 3;

            // 모든 입자가 고정이면 제약을 만들 수 없으므로 그때는 역질량을 넘기지 않습니다.
            const bool HasValidMasses{std::any_of(PhysMesh.mInverseMasses.begin(), PhysMesh.mInverseMasses.end(), [](float InvMass) {
                return InvMass > 0.0f;
            })};

            if (HasValidMasses) {
                MeshDesc.invMasses.data = PhysMesh.mInverseMasses.data();
                MeshDesc.invMasses.count = static_cast<Uint32>(NumVerts);
                MeshDesc.invMasses.stride = sizeof(float);
            }

            // NvCloth는 사각형 메시에서 제약을 더 고르게 만들므로 삼각형을 사각형으로 합친 뒤 굽습니다.
            mMeshQuadifier->quadify(MeshDesc);

            nv::cloth::Vector<int32_t>::Type PhaseTypes{};
            nv::cloth::Fabric* Fabric{NvClothCookFabricFromMesh(mFactory, mMeshQuadifier->getDescriptor(), ToPxVec3(mGravity.Normalized()), &PhaseTypes, true)};

            if (Fabric == nullptr) {
                MACAW_LOG(LogCloth, Error, "Cannot cook a cloth fabric for LOD{}.", LodIndex);

                for (FClothingActorNv::FActorLodData& CreatedLod : NewActor.mLodData) {
                    ReleaseLodData(CreatedLod);
                }

                return false;
            }

            FClothingActorNv::FActorLodData ActorLodData{};
            ActorLodData.mFabric = Fabric;
            ActorLodData.mRestPositions.reserve(NumVerts);

            for (Index_t VertIndex{}; VertIndex < NumVerts; ++VertIndex) {
                ActorLodData.mRestPositions.emplace_back(Points[VertIndex], PhysMesh.mInverseMasses[VertIndex]);
            }

            ActorLodData.mCloth = mFactory->createCloth(MakeRange(ActorLodData.mRestPositions), *Fabric);

            if (ActorLodData.mCloth == nullptr) {
                MACAW_LOG(LogCloth, Error, "Cannot create cloth for LOD{}.", LodIndex);
                ReleaseLodData(ActorLodData);

                for (FClothingActorNv::FActorLodData& CreatedLod : NewActor.mLodData) {
                    ReleaseLodData(CreatedLod);
                }

                return false;
            }

            // 위상마다 번호를 넣어 둡니다. 세기 같은 값은 천 설정에서 채웁니다.
            const Uint32 NumPhases{Fabric->getNumPhases()};
            ActorLodData.mPhaseConfigs.resize(NumPhases);
            ActorLodData.mPhaseTypes.resize(NumPhases);

            for (Uint32 PhaseIndex{}; PhaseIndex < NumPhases; ++PhaseIndex) {
                ActorLodData.mPhaseTypes[PhaseIndex] = PhaseTypes[PhaseIndex];
                ActorLodData.mPhaseConfigs[PhaseIndex].mPhaseIndex = static_cast<uint16_t>(PhaseIndex);
            }

            ActorLodData.mCloth->setPhaseConfig(MakeRange(ActorLodData.mPhaseConfigs));

            // 천끼리 부딪힐 입자를 넣습니다. 실제로 부딪히는지는 설정의 거리와 세기가 정합니다.
            ActorLodData.mCloth->setSelfCollisionIndices(MakeRange(PhysMesh.mSelfCollisionIndices));

            // 입자마다 스키닝 위치를 중심, 최대 거리를 반지름으로 하는 구 안에서만 움직이게 합니다.
            const FPointWeightMap* const MaxDistances{PhysMesh.FindWeightMap(EWeightMapTargetCommon::MaxDistance)};
            const bool HasMaxDistances{MaxDistances != nullptr && MaxDistances->Num() == NumVerts};
            nv::cloth::Range<physx::PxVec4> MotionConstraints{ActorLodData.mCloth->getMotionConstraints()};
            const Index_t NumMotionConstraints{std::min<Index_t>(ActorLodData.mCloth->getNumMotionConstraints(), NumVerts)};

            for (Index_t ConstraintIndex{}; ConstraintIndex < NumMotionConstraints; ++ConstraintIndex) {
                const float Radius{HasMaxDistances ? (*MaxDistances)[ConstraintIndex] : std::numeric_limits<float>::max()};

                MotionConstraints[static_cast<Uint32>(ConstraintIndex)] = physx::PxVec4{Points[ConstraintIndex], Radius};
            }

            // 첫 프레임에 원점에서 순간 이동한 것으로 보지 않도록 변환을 넣고 관성을 비웁니다.
            SetSimRootTransform(*ActorLodData.mCloth, ComponentToWorld, ComponentSpaceTransforms, Asset.mReferenceBoneIndex);
            ActorLodData.mCloth->clearInertia();

            // 첫 프레임은 상세 단계 0이 맡으므로 그 스키닝 결과로 두 버퍼를 채워 지난 프레임 값도 있게 합니다.
            if (LodIndex == 0) {
                NewActor.mSkinnedPhysicsMeshPositions[0] = SkinnedVerts;
                NewActor.mSkinnedPhysicsMeshPositions[1] = SkinnedVerts;
                NewActor.mSkinnedPhysicsMeshNormals = SkinnedNormals;
            }

            NewActor.mLodData.push_back(std::move(ActorLodData));
        }

        if (NewActor.mLodData.empty()) {
            return false;
        }

        // 몸과 부딪히도록 물리 에셋의 구와 캡슐을 꺼내 둡니다.
        ExtractActorCollisions(Asset, NewActor);

        // 세기, 감쇠, 관성 같은 설정을 넣습니다.
        if (const UClothConfigNv* const Config{Asset.GetClothConfig()}) {
            ApplyClothConfig(*Config, NewActor);
        }

        mActors.push_back(std::move(NewActor));

        // 새 천이 현재 상세 단계로 솔버에 들어가도록 모든 천을 다시 검사합니다.
        UpdateLod(mCurrentMeshLodIndex == InvalidIndex ? 0 : mCurrentMeshLodIndex, ComponentToWorld, ComponentSpaceTransforms, RefToLocals, true, true);

        MACAW_LOG(LogCloth, Log, "Created a cloth actor with {} LODs and {} particles.", mActors.back().mLodData.size(), Asset.mLodData[0].mPhysicalMeshData.mVertices.size());

        return true;
    }

    /**
     * @brief 입력의 포즈와 변환으로 천 에셋의 천을 만들어 솔버에 넣습니다.
     */
    bool FClothingSimulationNv::CreateActor(const UClothingAssetCommon& Asset, Index_t SimDataIndex, const FClothingSimulationContextCommon& Context) {
        if (!CreateActor(Asset, SimDataIndex, Context.mComponentToWorld, Context.mBoneTransforms, Context.mRefToLocals)) {
            return false;
        }

        // 결과를 처음 꺼낼 때도 법선이 있도록 만든 직후의 입자로 법선을 구합니다.
        ComputePhysicalMeshNormals(mActors.back());

        return true;
    }

    /**
     * @brief 천 에셋의 물리 에셋에서 구와 캡슐을 꺼내 천의 충돌체로 둡니다.
     */
    void FClothingSimulationNv::ExtractActorCollisions(const UClothingAssetCommon& Asset, FClothingActorNv& Actor) const {
        Actor.mExtractedCollisions.Reset();

        const UPhysicsAsset* const PhysicsAsset{Asset.mPhysicsAsset.Get()};

        if (PhysicsAsset == nullptr) {
            return;
        }

        bool AddedBodies{};

        for (const std::unique_ptr<UBodySetup>& BodySetup : PhysicsAsset->mSkeletalBodySetups) {
            if (BodySetup == nullptr) {
                continue;
            }

            // 몸통 뼈는 물리 에셋을 정할 때 사용 뼈에 넣어 두었으므로 이름으로 번호를 찾습니다.
            const auto Found{std::find(Asset.mUsedBoneNames.begin(), Asset.mUsedBoneNames.end(), BodySetup->GetBoneName())};
            const Index_t MappedBoneIndex{Found != Asset.mUsedBoneNames.end() ? static_cast<Index_t>(Found - Asset.mUsedBoneNames.begin()) : InvalidIndex};
            const FKAggregateGeom& AggGeom{BodySetup->GetAggGeom()};

            for (const FKSphereElem& Sphere : AggGeom.mSphereElems) {
                FClothCollisionPrim_Sphere NewSphere{};
                NewSphere.mLocalPosition = Sphere.mCenter;
                NewSphere.mRadius = Sphere.mRadius;
                NewSphere.mBoneIndex = MappedBoneIndex;

                Actor.mExtractedCollisions.mSpheres.push_back(NewSphere);
                AddedBodies = true;
            }

            // 캡슐 축은 회전 전 Z축이고 길이는 양 끝 반구 중심 사이 거리입니다.
            for (const FKSphylElem& Sphyl : AggGeom.mSphylElems) {
                const FVector OrientedDirection{Sphyl.mRotation.RotateVector(FVector{0.0f, 0.0f, 1.0f})};
                const FVector HalfDim{OrientedDirection * (Sphyl.mLength.GetBase() * 0.5f)};

                FClothCollisionPrim_Sphere Sphere0{};
                Sphere0.mLocalPosition = Sphyl.mCenter - HalfDim;
                Sphere0.mRadius = Sphyl.mRadius;
                Sphere0.mBoneIndex = MappedBoneIndex;

                FClothCollisionPrim_Sphere Sphere1{};
                Sphere1.mLocalPosition = Sphyl.mCenter + HalfDim;
                Sphere1.mRadius = Sphyl.mRadius;
                Sphere1.mBoneIndex = MappedBoneIndex;

                Actor.mExtractedCollisions.mSpheres.push_back(Sphere0);
                Actor.mExtractedCollisions.mSpheres.push_back(Sphere1);

                FClothCollisionPrim_SphereConnection Connection{};
                Connection.mSphereIndices[0] = Actor.mExtractedCollisions.mSpheres.size() - 2;
                Connection.mSphereIndices[1] = Actor.mExtractedCollisions.mSpheres.size() - 1;

                Actor.mExtractedCollisions.mSphereConnections.push_back(Connection);
                AddedBodies = true;
            }
        }

        Actor.mCollisionsDirty = Actor.mCollisionsDirty || AddedBodies;
    }

    /**
     * @brief 설정을 천의 모든 상세 단계에 넣습니다.
     */
    void FClothingSimulationNv::ApplyClothConfig(const UClothConfigNv& Config, FClothingActorNv& Actor) const {
        // 감쇠와 항력은 세기 기준 횟수 10 Hz일 때의 비율로 보고 실제 기준 횟수에 맞게 바꿉니다.
        constexpr Core::FFrequency DampStiffnessFrequency{Core::FFrequency::FromBase(10.0f)};
        const float DampStiffFreqRatio{DampStiffnessFrequency / Config.mStiffnessFrequency};
        const auto AdjustRatio{[DampStiffFreqRatio](float Value) {
            return 1.0f - std::exp2(DampStiffFreqRatio * std::log2(1.0f - Value));
        }};
        const FVector AdjustedDamping{AdjustRatio(Config.mDamping.x), AdjustRatio(Config.mDamping.y), AdjustRatio(Config.mDamping.z)};
        const FVector AdjustedDragLin{AdjustRatio(Config.mLinearDrag.x), AdjustRatio(Config.mLinearDrag.y), AdjustRatio(Config.mLinearDrag.z)};
        const FVector AdjustedDragAng{AdjustRatio(Config.mAngularDrag.x), AdjustRatio(Config.mAngularDrag.y), AdjustRatio(Config.mAngularDrag.z)};

        for (FClothingActorNv::FActorLodData& LodData : Actor.mLodData) {
            nv::cloth::Cloth* const LodCloth{LodData.mCloth};

            if (LodCloth == nullptr) {
                continue;
            }

            // 위상 종류마다 해당하는 세기 설정을 넣습니다.
            for (Index_t PhaseIndex{}; PhaseIndex < LodData.mPhaseConfigs.size() && PhaseIndex < LodData.mPhaseTypes.size(); ++PhaseIndex) {
                nv::cloth::PhaseConfig& PhaseConfig{LodData.mPhaseConfigs[PhaseIndex]};
                const FClothConstraintSetupNv* ConfigToUse{};

                switch (static_cast<nv::cloth::ClothFabricPhaseType::Enum>(LodData.mPhaseTypes[PhaseIndex])) {
                    case nv::cloth::ClothFabricPhaseType::eVERTICAL:
                        ConfigToUse = &Config.mVerticalConstraint;
                        break;
                    case nv::cloth::ClothFabricPhaseType::eHORIZONTAL:
                        ConfigToUse = &Config.mHorizontalConstraint;
                        break;
                    case nv::cloth::ClothFabricPhaseType::eBENDING:
                        ConfigToUse = &Config.mBendConstraint;
                        break;
                    case nv::cloth::ClothFabricPhaseType::eSHEARING:
                        ConfigToUse = &Config.mShearConstraint;
                        break;
                    default:
                        break;
                }

                if (ConfigToUse == nullptr) {
                    continue;
                }

                PhaseConfig.mStiffness = ConfigToUse->mStiffness;
                PhaseConfig.mStiffnessMultiplier = ConfigToUse->mStiffnessMultiplier;
                PhaseConfig.mCompressionLimit = ConfigToUse->mCompressionLimit;
                PhaseConfig.mStretchLimit = ConfigToUse->mStretchLimit;
            }

            LodCloth->setPhaseConfig(MakeRange(LodData.mPhaseConfigs));

            // 항력과 양력 계수는 정확한 바람 방식에서만 씁니다.
            if (Config.mClothingWindMethod == EClothingWindMethodNv::Accurate) {
                LodCloth->setDragCoefficient(Config.mWindDragCoefficient);
                LodCloth->setLiftCoefficient(Config.mWindLiftCoefficient);
            } else {
                LodCloth->setDragCoefficient(0.0f);
                LodCloth->setLiftCoefficient(0.0f);
            }

            // 단순한 바람 방식에서 정확한 방식으로 바뀌면 남아 있던 입자 가속도를 지웁니다.
            if (Actor.mWindMethod == EClothingWindMethodNv::Legacy && Config.mClothingWindMethod == EClothingWindMethodNv::Accurate) {
                LodCloth->clearParticleAccelerations();
            }

            LodCloth->setSolverFrequency(Config.mSolverFrequency.GetBase());
            LodCloth->setStiffnessFrequency(Config.mStiffnessFrequency.GetBase());
            LodCloth->setAcceleationFilterWidth(2u * static_cast<Uint32>(Config.mSolverFrequency.GetBase()));

            LodCloth->setSelfCollisionDistance(Config.mSelfCollisionRadius.GetBase());
            LodCloth->setSelfCollisionStiffness(Config.mSelfCollisionStiffness);

            // 천끼리 부딪히려면 처음 모양의 입자 위치가 있어야 합니다.
            if (Config.UseSelfCollisions()) {
                LodCloth->setRestPositions(MakeRange(LodData.mRestPositions));
            }

            LodCloth->setDamping(ToPxVec3(AdjustedDamping));
            LodCloth->setFriction(Config.mFriction);
            LodCloth->setLinearDrag(ToPxVec3(AdjustedDragLin));
            LodCloth->setAngularDrag(ToPxVec3(AdjustedDragAng));
            LodCloth->setLinearInertia(ToPxVec3(Config.mLinearInertiaScale));
            LodCloth->setAngularInertia(ToPxVec3(Config.mAngularInertiaScale));
            LodCloth->setCentrifugalInertia(ToPxVec3(Config.mCentrifugalInertiaScale));

            LodCloth->setTetherConstraintScale(Config.mTetherLimit);
            LodCloth->setTetherConstraintStiffness(Config.mTetherStiffness);
        }

        Actor.mCollisionThickness = Config.mCollisionThickness;
        Actor.mWindMethod = Config.mClothingWindMethod;
        Actor.mCurrentAnimDriveSpringStiffness = Config.mAnimDriveSpringStiffness;
        Actor.mCurrentAnimDriveDamperStiffness = Config.mAnimDriveDamperStiffness;
    }

    /**
     * @brief 메시 상세 단계에 맞는 천 상세 단계를 골라 솔버의 천을 바꿉니다.
     */
    void FClothingSimulationNv::UpdateLod(Index_t PredictedLod, const FMatrix& ComponentToWorld, const Core::TArray<FMatrix>& ComponentSpaceTransforms, const Core::TArray<FMatrix>& RefToLocals, bool ForceNoRemap, bool ForceActorChecks) {
        if (mSolver == nullptr || (PredictedLod == mCurrentMeshLodIndex && !ForceActorChecks)) {
            return;
        }

        for (FClothingActorNv& Actor : mActors) {
            if (Actor.mAssetCreatedFrom == nullptr) {
                continue;
            }

            const UClothingAssetCommon& Asset{*Actor.mAssetCreatedFrom};
            const Core::TArray<Index_t>& LodMap{Asset.mLodMap};

            // 메시 상세 단계에 천이 없으면 지금 천을 솔버에서 빼고 넘어갑니다.
            if (PredictedLod >= LodMap.size()) {
                if (Actor.mCurrentLodIndex != InvalidIndex) {
                    mSolver->removeCloth(Actor.mLodData[Actor.mCurrentLodIndex].mCloth);
                }

                Actor.mCurrentLodIndex = InvalidIndex;

                continue;
            }

            const bool OldLodMapped{mCurrentMeshLodIndex < LodMap.size() && LodMap[mCurrentMeshLodIndex] != InvalidIndex};

            // 메시 상세 단계에 연결된 천 상세 단계가 없으면 더 낮은 단계에서 찾습니다.
            Index_t PredictedClothingLod{InvalidIndex};

            for (Index_t PotentialLod{PredictedLod}; PotentialLod < LodMap.size(); ++PotentialLod) {
                if (LodMap[PotentialLod] != InvalidIndex) {
                    PredictedClothingLod = LodMap[PotentialLod];

                    break;
                }
            }

            if (PredictedClothingLod == Actor.mCurrentLodIndex) {
                continue;
            }

            if (PredictedClothingLod >= Actor.mLodData.size()) {
                if (Actor.mCurrentLodIndex != InvalidIndex) {
                    mSolver->removeCloth(Actor.mLodData[Actor.mCurrentLodIndex].mCloth);
                }

                Actor.mCurrentLodIndex = InvalidIndex;

                continue;
            }

            FClothingActorNv::FActorLodData& NewLodData{Actor.mLodData[PredictedClothingLod]};
            nv::cloth::Cloth& NewCloth{*NewLodData.mCloth};

            if (OldLodMapped && !ForceNoRemap && Actor.mCurrentLodIndex != InvalidIndex) {
                // 상세 단계를 바꿉니다. 메시 상세 단계가 하나뿐이라 이전 입자를 옮기지 않고 처음 상태에서 다시 시작합니다.
                mSolver->removeCloth(Actor.mLodData[Actor.mCurrentLodIndex].mCloth);
                mSolver->addCloth(&NewCloth);

                nv::cloth::MappedRange<physx::PxVec4> NewParticles{NewCloth.getCurrentParticles()};
                nv::cloth::MappedRange<physx::PxVec4> NewPrevParticles{NewCloth.getPreviousParticles()};

                for (Uint32 ParticleIndex{}; ParticleIndex < NewParticles.size(); ++ParticleIndex) {
                    NewParticles[ParticleIndex] = NewLodData.mRestPositions[ParticleIndex];
                    NewPrevParticles[ParticleIndex] = NewLodData.mRestPositions[ParticleIndex];
                }

                NewCloth.clearParticleAccelerations();
            } else {
                mSolver->addCloth(&NewCloth);

                nv::cloth::MappedRange<physx::PxVec4> NewParticles{NewCloth.getCurrentParticles()};
                nv::cloth::MappedRange<physx::PxVec4> NewPrevParticles{NewCloth.getPreviousParticles()};

                if (Asset.mReferenceBoneIndex < ComponentSpaceTransforms.size()) {
                    // 이전 상세 단계가 없으므로 입자를 현재 자세의 스키닝 위치에 놓습니다.
                    Core::TArray<FVector> SkinnedPositions{};
                    Core::TArray<FVector> SkinnedNormals{};
                    ClothingMeshUtils::SkinPhysicsMesh(Asset.mUsedBoneIndices, Asset.mLodData[PredictedClothingLod].mPhysicalMeshData, GetBoneTransformCS(ComponentSpaceTransforms, Asset.mReferenceBoneIndex), RefToLocals, SkinnedPositions, SkinnedNormals);

                    for (Uint32 ParticleIndex{}; ParticleIndex < NewParticles.size() && ParticleIndex < SkinnedPositions.size(); ++ParticleIndex) {
                        NewParticles[ParticleIndex] = physx::PxVec4{ToPxVec3(SkinnedPositions[ParticleIndex]), NewParticles[ParticleIndex].w};
                        NewPrevParticles[ParticleIndex] = physx::PxVec4{ToPxVec3(SkinnedPositions[ParticleIndex]), NewPrevParticles[ParticleIndex].w};
                    }
                } else {
                    for (Uint32 ParticleIndex{}; ParticleIndex < NewParticles.size(); ++ParticleIndex) {
                        NewParticles[ParticleIndex] = NewLodData.mRestPositions[ParticleIndex];
                        NewPrevParticles[ParticleIndex] = NewLodData.mRestPositions[ParticleIndex];
                    }
                }

                NewCloth.clearParticleAccelerations();
            }

            SetSimRootTransform(NewCloth, ComponentToWorld, ComponentSpaceTransforms, Asset.mReferenceBoneIndex);
            NewCloth.clearInertia();

            // 이전에 시뮬레이션하던 충돌체와 제약이 남지 않게 비웁니다.
            NewCloth.setSpheres(nv::cloth::Range<const physx::PxVec4>{}, 0, NewCloth.getNumSpheres());
            NewCloth.setPlanes(nv::cloth::Range<const physx::PxVec4>{}, 0, NewCloth.getNumPlanes());
            NewCloth.clearMotionConstraints();
            NewCloth.clearSeparationConstraints();

            Actor.mCurrentLodIndex = PredictedClothingLod;
        }

        mCurrentMeshLodIndex = PredictedLod;
    }

    /**
     * @brief 등록한 천을 솔버에서 빼고 천과 패브릭을 해제합니다.
     */
    void FClothingSimulationNv::DestroyActors() {
        for (FClothingActorNv& Actor : mActors) {
            if (mSolver != nullptr && Actor.mCurrentLodIndex < Actor.mLodData.size()) {
                mSolver->removeCloth(Actor.mLodData[Actor.mCurrentLodIndex].mCloth);
            }

            for (FClothingActorNv::FActorLodData& LodData : Actor.mLodData) {
                ReleaseLodData(LodData);
            }
        }

        mActors.clear();
        mCurrentMeshLodIndex = InvalidIndex;
        mGridWidth = 0;
        mGridHeight = 0;
    }

    /**
     * @brief 모든 천에 중력을 적용하고 솔버를 한 프레임 진행합니다.
     */
    void FClothingSimulationNv::Simulate(float DeltaTime) {
        if (mSolver == nullptr || mActors.empty() || !(DeltaTime > 0.0f)) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsStats::ClothSimulate};
        Index_t NumParticles{};

        for (const FClothingActorNv& Actor : mActors) {
            if (Actor.mCurrentLodIndex >= Actor.mLodData.size()) {
                continue;
            }

            nv::cloth::Cloth& Cloth{*Actor.mLodData[Actor.mCurrentLodIndex].mCloth};
            Cloth.setGravity(ToPxVec3(mGravity));
            NumParticles += Cloth.getNumParticles();
        }

        Solve(std::min(DeltaTime, MaxDeltaTime));
        PhysicsStats::ClothParticles.Add(static_cast<double>(NumParticles));
    }

    /**
     * @brief 입력으로 천 에셋의 천을 한 프레임 진행합니다.
     */
    void FClothingSimulationNv::Simulate(const FClothingSimulationContextCommon& Context) {
        if (mSolver == nullptr || mActors.empty() || Context.mBoneTransforms.empty() || !(Context.mDeltaTime > Core::FTime{})) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsStats::ClothSimulate};
        Index_t NumParticles{};

        UpdateLod(Context.mPredictedLod, Context.mComponentToWorld, Context.mBoneTransforms, Context.mRefToLocals, false, false);

        for (FClothingActorNv& Actor : mActors) {
            if (Actor.mAssetCreatedFrom == nullptr || Actor.mCurrentLodIndex >= Actor.mLodData.size()) {
                continue;
            }

            Actor.mScratch.Reset();

            // 충돌체가 바뀌었으면 목록을 다시 모읍니다.
            Actor.ConditionalRebuildCollisions();

            // 모션 제약을 만들려면 시뮬레이션 메시 전체를 이번 포즈로 스키닝해야 합니다.
            Actor.SkinPhysicsMesh(Context);

            nv::cloth::Cloth& Cloth{*Actor.mLodData[Actor.mCurrentLodIndex].mCloth};
            const bool Teleport{Context.mTeleportMode != EClothingTeleportMode::None};
            const bool Reset{Context.mTeleportMode == EClothingTeleportMode::TeleportAndReset};

            // 다시 놓기를 요청하면 입자를 이번 포즈의 스키닝 위치에 두고 지난 위치도 같게 해 속도를 없앱니다.
            if (Reset) {
                const Core::TArray<FVector>& CurrSkinnedPositions{Actor.GetCurrentSkinnedPositions()};
                nv::cloth::MappedRange<physx::PxVec4> CurrParticles{Cloth.getCurrentParticles()};
                nv::cloth::MappedRange<physx::PxVec4> PrevParticles{Cloth.getPreviousParticles()};

                for (Uint32 ParticleIndex{}; ParticleIndex < CurrParticles.size() && ParticleIndex < PrevParticles.size() && ParticleIndex < CurrSkinnedPositions.size(); ++ParticleIndex) {
                    CurrParticles[ParticleIndex] = physx::PxVec4{ToPxVec3(CurrSkinnedPositions[ParticleIndex]), CurrParticles[ParticleIndex].w};
                    PrevParticles[ParticleIndex] = CurrParticles[ParticleIndex];
                }

                Cloth.clearParticleAccelerations();
            }

            // 순간 이동이면 관성 없이 새 위치로 옮기되, 지금 속도는 남겨 튀지 않게 합니다.
            const FTransform RootBoneWorldTransform{GetSimRootWorldTransform(Context.mComponentToWorld, Context.mBoneTransforms, Actor.mAssetCreatedFrom->mReferenceBoneIndex)};

            if (Teleport) {
                Cloth.teleportToLocation(ToPxVec3(RootBoneWorldTransform.mLocation), ToPxQuat(RootBoneWorldTransform.mRotation));
                Cloth.ignoreVelocityDiscontinuity();
            }

            // 기준 뼈의 월드 변환이 지난 프레임과 달라진 만큼 NvCloth가 입자에 관성을 줍니다.
            SetSimRootTransform(Cloth, Context.mComponentToWorld, Context.mBoneTransforms, Actor.mAssetCreatedFrom->mReferenceBoneIndex);

            // 설정이 중력을 덮어쓰면 그 값을, 아니면 천 전용 중력이나 월드 중력에 배율을 곱해 씁니다.
            if (const UClothConfigNv* const Config{Actor.mAssetCreatedFrom->GetClothConfig()}) {
                if (Config->mUseGravityOverride) {
                    Cloth.setGravity(ToPxVec3(Config->mGravityOverride));
                } else if (Actor.mUseGravityOverride) {
                    Cloth.setGravity(ToPxVec3(Actor.mGravityOverride * Config->mGravityScale));
                } else {
                    Cloth.setGravity(ToPxVec3(Context.mWorldGravity * Config->mGravityScale));
                }
            } else {
                Cloth.setGravity(ToPxVec3(Context.mWorldGravity));
            }

            // 최대 거리가 0인 입자는 스키닝 위치에 붙어 애니메이션을 따라가고, 나머지는 그 거리 안에서만 흔들립니다.
            Actor.UpdateMotionConstraints(Context);

            {
                const Core::FScopedStatTimer CollisionTimer{PhysicsStats::ClothUpdateCollisions};
                const FClothCollisionData& CollisionData{Actor.mAggregatedCollisions};
                FClothingActorScratchData& Scratch{Actor.mScratch};
                const FTransform RootBoneTransform{GetBoneTransformCS(Context.mBoneTransforms, Actor.mAssetCreatedFrom->mReferenceBoneIndex)};
                const Core::TArray<Index_t>& UsedBoneIndices{Actor.mAssetCreatedFrom->mUsedBoneIndices};

                // 구는 이번 프레임 뼈 위치로 옮긴 뒤 천 좌표(기준 뼈 좌표)로 바꾸고, 천 두께만큼 반지름을 키웁니다.
                for (const FClothCollisionPrim_Sphere& Sphere : CollisionData.mSpheres) {
                    FVector SphereLocation{Sphere.mLocalPosition};
                    float RadiusScale{1.0f};
                    FTransform BoneTransform{};

                    // 물리 바디와 같이 뼈의 배율도 중심과 반지름에 적용합니다. 배율이 축마다 같다고 보고 가장 작은 축 배율을 반지름에 씁니다.
                    if (Sphere.mBoneIndex < UsedBoneIndices.size() && UsedBoneIndices[Sphere.mBoneIndex] < Context.mBoneTransforms.size() && Context.mBoneTransforms[UsedBoneIndices[Sphere.mBoneIndex]].TryGetTransform(BoneTransform)) {
                        const FVector AbsoluteScale{BoneTransform.mScale.Abs()};

                        SphereLocation = BoneTransform.mLocation + BoneTransform.mRotation.RotateVector(Sphere.mLocalPosition * BoneTransform.mScale);
                        RadiusScale = std::min({AbsoluteScale.x, AbsoluteScale.y, AbsoluteScale.z});
                    }

                    SphereLocation = RootBoneTransform.InverseTransformPositionNoScale(SphereLocation);
                    Scratch.mSphereData.emplace_back(ToPxVec3(SphereLocation), (Sphere.mRadius * RadiusScale + Actor.mCollisionThickness).GetBase());
                }

                // NvCloth는 구를 32개까지만 받고 넘기면 충돌 마스크가 넘치므로, 앞의 32개만 넘깁니다.
                const Index_t NumKeptSpheres{std::min(Scratch.mSphereData.size(), MaxClothCollisionShapes)};
                const physx::PxVec4* const SpheresBegin{Scratch.mSphereData.data()};

                Cloth.setSpheres(nv::cloth::Range<const physx::PxVec4>{SpheresBegin, SpheresBegin + NumKeptSpheres}, 0, Cloth.getNumSpheres());

                // 캡슐도 32개까지만 받으므로, 넘기지 않은 구를 잇는 캡슐과 32개를 넘는 캡슐은 넣지 않습니다.

                for (const FClothCollisionPrim_SphereConnection& Capsule : CollisionData.mSphereConnections) {
                    if (Scratch.mCapsuleSphereIndices.size() / 2 >= MaxClothCollisionShapes) {
                        break;
                    }

                    if (Capsule.mSphereIndices[0] >= NumKeptSpheres || Capsule.mSphereIndices[1] >= NumKeptSpheres) {
                        continue;
                    }

                    Scratch.mCapsuleSphereIndices.push_back(static_cast<Uint32>(Capsule.mSphereIndices[0]));
                    Scratch.mCapsuleSphereIndices.push_back(static_cast<Uint32>(Capsule.mSphereIndices[1]));
                }

                Cloth.setCapsules(MakeRange(Scratch.mCapsuleSphereIndices), 0, Cloth.getNumCapsules());

                // 볼록 다면체는 평면마다 비트 하나를 쓰므로 평면이 32장을 넘으면 더 넣지 않습니다.
                for (const FClothCollisionPrim_Convex& Convex : CollisionData.mConvexes) {
                    if (Scratch.mPlaneData.size() >= MaxClothCollisionShapes) {
                        continue;
                    }

                    Uint32& ConvexMask{Scratch.mConvexMasks.emplace_back()};

                    for (const FVector4& Plane : Convex.mPlanes) {
                        // 평면 위의 점과 법선을 천 좌표로 옮겨 평면을 다시 만듭니다.
                        const FVector Normal{Plane.x, Plane.y, Plane.z};
                        const FVector LocalNormal{RootBoneTransform.InverseTransformVectorNoScale(Normal)};
                        const FVector LocalPoint{RootBoneTransform.InverseTransformPositionNoScale(Normal * Plane.w)};

                        Scratch.mPlaneData.emplace_back(ToPxVec3(LocalNormal), -LocalNormal.Dot(LocalPoint));
                        ConvexMask |= 1u << static_cast<Uint32>(Scratch.mPlaneData.size() - 1);

                        if (Scratch.mPlaneData.size() >= MaxClothCollisionShapes) {
                            break;
                        }
                    }
                }

                Cloth.setPlanes(MakeRange(Scratch.mPlaneData), 0, Cloth.getNumPlanes());
                Cloth.setConvexes(MakeRange(Scratch.mConvexMasks), 0, Cloth.getNumConvexes());
            }

            // 바람과 애니메이션 당김은 입자 가속도로 넣습니다.
            // 컴포넌트에 바람이 없으면 천 에셋의 기본 바람을 쓰고, 그때도 컴포넌트처럼 매 프레임 조금씩 흔들어 돌풍처럼 보이게 합니다.
            const UClothConfigNv* const WindConfig{Actor.mAssetCreatedFrom->GetClothConfig()};
            const bool UseAssetWind{Context.mWindVelocity.IsNearlyZero() && WindConfig != nullptr && !WindConfig->mDefaultWindVelocity.IsNearlyZero()};
            const FVector WindVelocity{UseAssetWind ? WindConfig->mDefaultWindVelocity : Context.mWindVelocity};
            const float WindAdaption{UseAssetWind ? static_cast<float>(std::rand() % 20) * 0.1f : Context.mWindAdaption};

            Actor.UpdateWind(Context, WindVelocity, WindAdaption);
            Actor.UpdateAnimDrive(Context);

            // 이번 프레임 기준 뼈의 속도와 변환을 남깁니다.
            Actor.mLastVelocity = Context.mDeltaTime > Core::FTime{} ? (RootBoneWorldTransform.mLocation - Actor.mLastRootTransform.mLocation) * (1.0f / Context.mDeltaTime).GetBase() : FVector{};
            Actor.mLastRootTransform = RootBoneWorldTransform;

            NumParticles += Cloth.getNumParticles();
        }

        Solve(Context.mDeltaTime.GetBase());

        for (FClothingActorNv& Actor : mActors) {
            if (Actor.mAssetCreatedFrom == nullptr || Actor.mCurrentLodIndex >= Actor.mLodData.size()) {
                continue;
            }

            ComputePhysicalMeshNormals(Actor);

            // 다음 프레임에 입자 속도를 구할 때 쓰도록 진행한 시간을 남깁니다.
            Actor.mPreviousTimestep = Context.mDeltaTime;

            // 다음 프레임 스키닝은 다른 버퍼에 써서 이번 프레임 값을 지난 프레임 값으로 남깁니다.
            Actor.mCurrentSkinnedPositionIndex = (Actor.mCurrentSkinnedPositionIndex + 1) % 2;
        }

        PhysicsStats::ClothParticles.Add(static_cast<double>(NumParticles));
    }

    void FClothingSimulationNv::Solve(float DeltaTime) {
        const Core::FScopedStatTimer StatTimer{PhysicsStats::ClothSolve};

        if (!mSolver->beginSimulation(DeltaTime)) {
            return;
        }

        const int ChunkCount{mSolver->getSimulationChunkCount()};

        for (int ChunkIndex{}; ChunkIndex < ChunkCount; ++ChunkIndex) {
            mSolver->simulateChunk(ChunkIndex);
        }

        mSolver->endSimulation();
    }

    /**
     * @brief 모든 천에 바깥에서 정한 충돌체를 더합니다.
     */
    void FClothingSimulationNv::AddExternalCollisions(const FClothCollisionData& Data) {
        for (FClothingActorNv& Actor : mActors) {
            Actor.mExternalCollisions.Append(Data);
            Actor.mCollisionsDirty = true;
        }
    }

    /**
     * @brief 모든 천에서 바깥에서 정한 충돌체를 뺍니다.
     */
    void FClothingSimulationNv::ClearExternalCollisions() {
        for (FClothingActorNv& Actor : mActors) {
            Actor.mExternalCollisions.Reset();
            Actor.mCollisionsDirty = true;
        }
    }

    /**
     * @brief 천들이 쓰는 충돌체를 모읍니다.
     */
    void FClothingSimulationNv::GetCollisions(FClothCollisionData& OutCollisions, bool IncludeExternal) const {
        OutCollisions.Reset();

        for (const FClothingActorNv& Actor : mActors) {
            if (IncludeExternal) {
                OutCollisions.Append(Actor.mAggregatedCollisions);
            } else if (Actor.mAssetCreatedFrom != nullptr && Actor.mCurrentLodIndex < Actor.mAssetCreatedFrom->mLodData.size()) {
                OutCollisions.Append(Actor.mAssetCreatedFrom->mLodData[Actor.mCurrentLodIndex].mCollisionData);
            }
        }
    }

    /**
     * @brief 천마다 현재 입자 위치와 법선, 기준 뼈 변환을 복사합니다.
     */
    void FClothingSimulationNv::GetSimulationData(Core::TMap<Index_t, FClothSimulData>& OutData, const FClothingSimulationContextCommon& Context) const {
        if (mActors.empty()) {
            OutData.clear();

            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::ClothWriteback};

        for (const FClothingActorNv& Actor : mActors) {
            if (Actor.mAssetCreatedFrom == nullptr || Actor.mCurrentLodIndex >= Actor.mLodData.size()) {
                continue;
            }

            const Index_t ReferenceBoneIndex{Actor.mAssetCreatedFrom->mReferenceBoneIndex};
            FClothSimulData& ClothData{OutData[Actor.mSimDataIndex]};

            if (ReferenceBoneIndex >= Context.mBoneTransforms.size()) {
                MACAW_LOG(LogCloth, Warning, "Failed to write back cloth simulation data because the bone transforms are invalid.");
                ClothData.Reset();

                return;
            }

            // 입자는 크기를 뺀 기준 뼈 좌표이므로 크기를 뺀 기준 뼈 변환에 컴포넌트 월드 변환을 곱해 기준 변환을 만듭니다.
            FTransform RootBoneTransform{GetBoneTransformCS(Context.mBoneTransforms, ReferenceBoneIndex)};
            RootBoneTransform.mScale = FVector{1.0f, 1.0f, 1.0f};

            FTransform RootBoneWorldTransform{};

            if (!(RootBoneTransform.ToMatrix() * Context.mComponentToWorld).TryGetTransform(RootBoneWorldTransform)) {
                ClothData.Reset();

                continue;
            }

            const nv::cloth::Cloth& Cloth{*Actor.mLodData[Actor.mCurrentLodIndex].mCloth};
            const nv::cloth::MappedRange<const physx::PxVec4> Particles{Cloth.getCurrentParticles()};

            ClothData.Reset();
            ClothData.mTransform = RootBoneWorldTransform;
            ClothData.mComponentRelativeTransform = RootBoneTransform;
            ClothData.mPositions.reserve(Particles.size());

            for (const physx::PxVec4& Particle : Particles) {
                ClothData.mPositions.push_back(ToFVector(Particle.getXYZ()));
            }

            ClothData.mNormals = Actor.mCurrentNormals;
        }
    }

    /**
     * @brief 솔버에 들어간 천이 있는지 검사합니다.
     */
    bool FClothingSimulationNv::ShouldSimulate() const {
        return std::any_of(mActors.begin(), mActors.end(), [](const FClothingActorNv& Actor) {
            return Actor.mCurrentLodIndex < Actor.mLodData.size();
        });
    }

    /**
     * @brief 천의 현재 입자 위치로 삼각형 법선을 더해 입자 법선을 구합니다.
     */
    void FClothingSimulationNv::ComputePhysicalMeshNormals(FClothingActorNv& Actor) const {
        if (Actor.mAssetCreatedFrom == nullptr || Actor.mCurrentLodIndex >= Actor.mLodData.size()) {
            return;
        }

        const nv::cloth::Cloth& Cloth{*Actor.mLodData[Actor.mCurrentLodIndex].mCloth};
        const nv::cloth::MappedRange<const physx::PxVec4> Particles{Cloth.getCurrentParticles()};
        const Core::TArray<Uint32>& Indices{Actor.mAssetCreatedFrom->mLodData[Actor.mCurrentLodIndex].mPhysicalMeshData.mIndices};

        Actor.mCurrentNormals.assign(Particles.size(), FVector{0.0f, 0.0f, 0.0f});

        // 입자가 닿은 삼각형마다 넓이만큼 큰 면 법선을 더한 뒤 마지막에 정규화합니다.
        for (Index_t BaseIndex{}; BaseIndex + 2 < Indices.size(); BaseIndex += 3) {
            const Uint32 IndexA{Indices[BaseIndex]};
            const Uint32 IndexB{Indices[BaseIndex + 1]};
            const Uint32 IndexC{Indices[BaseIndex + 2]};

            if (IndexA >= Particles.size() || IndexB >= Particles.size() || IndexC >= Particles.size()) {
                continue;
            }

            const FVector A{ToFVector(Particles[IndexA].getXYZ())};
            const FVector B{ToFVector(Particles[IndexB].getXYZ())};
            const FVector C{ToFVector(Particles[IndexC].getXYZ())};
            const FVector TriangleNormal{(B - A).Cross(C - A)};

            Actor.mCurrentNormals[IndexA] += TriangleNormal;
            Actor.mCurrentNormals[IndexB] += TriangleNormal;
            Actor.mCurrentNormals[IndexC] += TriangleNormal;
        }

        for (FVector& Normal : Actor.mCurrentNormals) {
            Normal.Normalize();
        }
    }

    /**
     * @brief 천 하나의 현재 입자 위치를 복사합니다.
     */
    bool FClothingSimulationNv::GetParticles(Index_t ActorIndex, Core::TArray<FVector>& OutPositions) const {
        OutPositions.clear();

        if (ActorIndex >= mActors.size() || mActors[ActorIndex].mCurrentLodIndex >= mActors[ActorIndex].mLodData.size()) {
            return false;
        }

        const FClothingActorNv& Actor{mActors[ActorIndex]};
        const nv::cloth::Cloth& Cloth{*Actor.mLodData[Actor.mCurrentLodIndex].mCloth};
        const nv::cloth::MappedRange<const physx::PxVec4> Particles{Cloth.getCurrentParticles()};

        OutPositions.reserve(Particles.size());

        for (const physx::PxVec4& Particle : Particles) {
            OutPositions.push_back(ToFVector(Particle.getXYZ()));
        }

        return true;
    }

    /**
     * @brief 등록한 천의 개수를 반환합니다.
     */
    Index_t FClothingSimulationNv::GetNumActors() const {
        return mActors.size();
    }

    /**
     * @brief 마지막으로 만든 격자의 X 방향 입자 수를 반환합니다.
     */
    Uint32 FClothingSimulationNv::GetGridWidth() const {
        return mGridWidth;
    }

    /**
     * @brief 마지막으로 만든 격자의 Y 방향 입자 수를 반환합니다.
     */
    Uint32 FClothingSimulationNv::GetGridHeight() const {
        return mGridHeight;
    }
}
