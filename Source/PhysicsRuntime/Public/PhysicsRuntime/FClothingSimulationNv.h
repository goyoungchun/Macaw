/**
 * @file FClothingSimulationNv.h
 * @brief NvCloth 솔버로 천 입자를 시뮬레이션하는 클래스를 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FTransform.h"
#include "Core/Math/FVector.h"
#include "PhysicsRuntime/IClothingSimulation.h"
#include "PhysicsRuntime/UClothConfigNv.h"

#include <NvCloth/PhaseConfig.h>
#include <foundation/PxVec4.h>

#include <array>

namespace nv::cloth {
    class Cloth;
    class ClothMeshQuadifier;
    class Fabric;
    class Factory;
    class Solver;
}

namespace PhysicsRuntime {
    class FClothingSystemRuntimeModuleNv;
    class UClothingAssetCommon;

    /**
     * @brief 시뮬레이션 중에 NvCloth에 넘길 값을 모으는 임시 배열이며, 매 프레임 비우고 다시 씁니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothingActorScratchData {
        /**
         * @brief 배열을 비우되 할당한 메모리는 남겨 다음 프레임에 다시 씁니다.
         */
        void Reset();

        /**
         * @brief 천 좌표의 구 중심과 반지름입니다.
         */
        Core::TArray<physx::PxVec4> mSphereData{};

        /**
         * @brief 캡슐마다 양 끝 구의 번호 두 개입니다.
         */
        Core::TArray<Uint32> mCapsuleSphereIndices{};

        /**
         * @brief 천 좌표의 평면입니다.
         */
        Core::TArray<physx::PxVec4> mPlaneData{};

        /**
         * @brief 볼록 다면체마다 쓰는 평면 번호를 비트로 표시한 값입니다.
         */
        Core::TArray<Uint32> mConvexMasks{};

        /**
         * @brief 지난 시뮬레이션의 입자 속도(m/s)입니다.
         */
        Core::TArray<FVector> mParticleVelocities{};
    };

    /**
     * @brief 천 에셋 하나로 만든 NvCloth 천과 그 상세 단계별 데이터입니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothingActorNv {
        /**
         * @brief 천 상세 단계 하나에 대응하는 패브릭, 천, 제약 위상 정보입니다.
         */
        struct FActorLodData {
            /**
             * @brief 입자 사이 제약 구조를 담은 패브릭이며 참조 카운트로 해제합니다.
             */
            nv::cloth::Fabric* mFabric{};

            /**
             * @brief 입자 위치와 속도를 가진 천 인스턴스입니다.
             */
            nv::cloth::Cloth* mCloth{};

            /**
             * @brief 제약 위상마다 솔버가 쓸 세기 설정입니다.
             */
            Core::TArray<nv::cloth::PhaseConfig> mPhaseConfigs{};

            /**
             * @brief 제약 위상마다의 종류(늘어남, 굽힘, 전단 등)입니다.
             */
            Core::TArray<Int32> mPhaseTypes{};

            /**
             * @brief 천을 만들 때의 입자 위치와 역질량이며, 처음 상태로 되돌릴 때 씁니다.
             */
            Core::TArray<physx::PxVec4> mRestPositions{};
        };

        /**
         * @brief 현재 뼈 포즈로 현재 천 상세 단계의 입자를 스키닝해 현재 스키닝 버퍼에 저장합니다.
         * @details 결과는 기준 뼈 좌표이며 모션 제약을 만들 때 씁니다.
         * @param[in] Context 이번 프레임의 뼈 행렬과 스키닝 행렬입니다.
         */
        void SkinPhysicsMesh(const FClothingSimulationContextCommon& Context);

        /**
         * @brief 현재 스키닝 위치로 모션 제약과 분리 제약을 갱신합니다.
         * @details 모션 제약은 스키닝 위치를 중심, 최대 거리에 배율을 곱한 값을 반지름으로 하는 구이며 입자는 그 밖으로 나가지 못합니다.
         *          분리 제약은 스키닝 위치에서 법선 반대로 뒤쪽 거리만큼 물러난 곳을 중심, 뒤쪽 반지름을 반지름으로 하는 구이며 입자는 그 안으로 들어가지 못합니다.
         * @param[in] Context 최대 거리 배율을 담은 입력입니다.
         */
        void UpdateMotionConstraints(const FClothingSimulationContextCommon& Context);

        /**
         * @brief 충돌체가 바뀌었으면 천 에셋, 물리 에셋, 바깥 충돌체를 하나의 목록으로 다시 모읍니다.
         */
        void ConditionalRebuildCollisions();

        /**
         * @brief 바람을 천에 넣습니다.
         * @details 단순한 방식은 입자마다 바람 속도 쪽으로 가속도를 넣고, 정확한 방식은 NvCloth에 바람 속도를 넣습니다.
         * @param[in] Context 기준 뼈 변환을 담은 입력입니다.
         * @param[in] WindVelocity 월드 좌표의 바람 속도(m/s)입니다.
         * @param[in] WindAdaption 단순한 방식에서 입자 속도를 바람에 맞추는 정도입니다.
         */
        void UpdateWind(const FClothingSimulationContextCommon& Context, const FVector& WindVelocity, float WindAdaption);

        /**
         * @brief 입자를 스키닝 위치 쪽으로 당기는 스프링 가속도를 더합니다.
         * @details 입자마다 세기를 칠한 맵이 있을 때만 동작합니다.
         * @param[in] Context 이번 프레임의 입력입니다.
         */
        void UpdateAnimDrive(const FClothingSimulationContextCommon& Context);

        /**
         * @brief 지난 시뮬레이션의 입자 위치 차이와 시간으로 입자 속도를 구합니다.
         * @param[out] OutVelocities 입자 속도(m/s)이며 입자 수에 맞게 크기를 바꿉니다.
         */
        void CalculateParticleVelocities(Core::TArray<FVector>& OutVelocities);

        /**
         * @brief 이번 프레임에 스키닝한 입자 위치를 반환합니다.
         * @return 기준 뼈 좌표의 입자 위치입니다.
         */
        const Core::TArray<FVector>& GetCurrentSkinnedPositions() const;

        /**
         * @brief 지난 프레임에 스키닝한 입자 위치를 반환합니다.
         * @return 기준 뼈 좌표의 입자 위치입니다.
         */
        const Core::TArray<FVector>& GetPreviousSkinnedPositions() const;

        /**
         * @brief 천 상세 단계 순서의 데이터입니다.
         */
        Core::TArray<FActorLodData> mLodData{};

        /**
         * @brief 이 천을 만든 천 에셋이며 소유하지 않습니다. 코드로 만든 격자 천이면 nullptr입니다.
         */
        const UClothingAssetCommon* mAssetCreatedFrom{};

        /**
         * @brief 시뮬레이션 결과를 돌려줄 때 쓰는 번호입니다.
         */
        Index_t mSimDataIndex{InvalidIndex};

        /**
         * @brief 솔버에 들어가 있는 천 상세 단계 번호이며 시뮬레이션하지 않으면 InvalidIndex입니다.
         */
        Index_t mCurrentLodIndex{InvalidIndex};

        /**
         * @brief 마지막 시뮬레이션 뒤에 삼각형으로 구한 기준 뼈 좌표의 입자 법선입니다.
         */
        Core::TArray<FVector> mCurrentNormals{};

        /**
         * @brief 이번 프레임 스키닝 결과를 쓸 버퍼 번호(0 또는 1)이며 시뮬레이션 뒤에 바꿉니다.
         */
        Index_t mCurrentSkinnedPositionIndex{};

        /**
         * @brief 기준 뼈 좌표로 스키닝한 입자 위치이며, 지난 프레임 값도 쓸 수 있도록 두 벌을 번갈아 씁니다.
         */
        std::array<Core::TArray<FVector>, 2> mSkinnedPhysicsMeshPositions{};

        /**
         * @brief 기준 뼈 좌표로 스키닝한 입자 법선입니다.
         */
        Core::TArray<FVector> mSkinnedPhysicsMeshNormals{};

        /**
         * @brief 지금 시뮬레이션에 쓰는 모든 충돌체입니다.
         */
        FClothCollisionData mAggregatedCollisions{};

        /**
         * @brief 바깥에서 넣은 충돌체입니다.
         */
        FClothCollisionData mExternalCollisions{};

        /**
         * @brief 물리 에셋에서 가져온 충돌체입니다.
         */
        FClothCollisionData mExtractedCollisions{};

        /**
         * @brief 다음 시뮬레이션 전에 충돌체 목록을 다시 모아야 하는지 나타냅니다.
         */
        bool mCollisionsDirty{};

        /**
         * @brief 천 두께를 흉내 내려고 충돌체 반지름에 더하는 값(m)입니다.
         */
        Core::FLength mCollisionThickness{Core::FLength::FromBase(0.01f)};

        /**
         * @brief 시뮬레이션 중에 쓰는 임시 배열입니다.
         */
        FClothingActorScratchData mScratch{};

        /**
         * @brief 바람을 적용하는 방식이며 설정에서 받습니다.
         */
        EClothingWindMethodNv mWindMethod{EClothingWindMethodNv::Legacy};

        /**
         * @brief 입자를 스키닝 위치 쪽으로 당기는 지금의 스프링 세기이며 설정에서 받습니다.
         */
        float mCurrentAnimDriveSpringStiffness{};

        /**
         * @brief 입자 속도를 스키닝 속도에 맞추는 지금의 감쇠 세기이며 설정에서 받습니다.
         */
        float mCurrentAnimDriveDamperStiffness{};

        /**
         * @brief 설정의 중력 배율을 곱해 쓸 천 전용 중력을 쓰는지 나타냅니다.
         */
        bool mUseGravityOverride{};

        /**
         * @brief 천 전용 중력 가속도(m/s²)입니다.
         */
        FVector mGravityOverride{};

        /**
         * @brief 지난 시뮬레이션에 진행한 시간(초)이며 입자 속도를 구할 때 씁니다.
         */
        Core::FTime mPreviousTimestep{};

        /**
         * @brief 지난 프레임 기준 뼈의 월드 속도(m/s)입니다.
         */
        FVector mLastVelocity{};

        /**
         * @brief 지난 프레임 기준 뼈의 월드 변환입니다.
         */
        FTransform mLastRootTransform{};
    };

    /**
     * @brief 솔버 하나에 천들을 등록하고 프레임마다 시뮬레이션합니다.
     * @details 팩토리는 천 시스템 모듈이 소유하므로 이 객체는 모듈보다 먼저 정리되어야 합니다.
     * @ingroup PhysicsRuntime
     */
    class FClothingSimulationNv final : public IClothingSimulation {
    public:

        /**
         * @brief 솔버가 없는 빈 시뮬레이션을 구성합니다.
         */
        FClothingSimulationNv();

        /**
         * @brief 등록한 천과 솔버를 해제합니다.
         */
        ~FClothingSimulationNv() override;

        /**
         * @brief 솔버와 천을 두 객체가 해제하지 않도록 복사를 금지합니다.
         */
        FClothingSimulationNv(const FClothingSimulationNv&) = delete;

        /**
         * @brief 솔버와 천을 두 객체가 해제하지 않도록 대입을 금지합니다.
         */
        FClothingSimulationNv& operator=(const FClothingSimulationNv&) = delete;

    public:

        /**
         * @brief 천 시스템 모듈의 팩토리로 솔버를 만듭니다.
         * @param[in] Module 팩토리와 사각형 변환 도구를 소유한 천 시스템 모듈입니다.
         * @return 솔버를 만들었거나 이미 초기화되었으면 true, 그렇지 않으면 false입니다.
         */
        bool Initialize(const FClothingSystemRuntimeModuleNv& Module);

        /**
         * @brief 등록한 천을 모두 해제한 뒤 솔버를 해제합니다.
         * @note 여러 번 호출해도 안전합니다.
         */
        void Shutdown() override;

        /**
         * @brief 한 변을 고정한 격자 천을 만들어 솔버에 등록합니다.
         * @details 격자는 XY 평면에 눕혀 만들고 첫 줄 입자의 역질량을 0으로 두어 고정합니다.
         *          메시 에셋 없이 솔버 동작을 확인하기 위한 임시 함수입니다.
         * @param[in] Origin 첫 입자의 월드 위치입니다.
         * @param[in] Width X 방향 입자 수이며 2 이상이어야 합니다.
         * @param[in] Height Y 방향 입자 수이며 2 이상이어야 합니다.
         * @param[in] Spacing 이웃 입자 사이의 거리(미터)입니다.
         * @return 천을 만들어 등록했으면 true, 그렇지 않으면 false입니다.
         */
        bool CreateGridActor(const FVector& Origin, Uint32 Width, Uint32 Height, float Spacing);

        /**
         * @brief 천 에셋의 상세 단계마다 NvCloth 천을 만들고 현재 상세 단계의 천을 솔버에 넣습니다.
         * @details 입자는 현재 자세로 스키닝한 뒤 기준 뼈 좌표로 바꿔 넣고, 기준 뼈의 월드 변환을 천의 변환으로 둡니다.
         * @param[in] Asset 천 에셋이며 천보다 오래 살아 있어야 합니다.
         * @param[in] SimDataIndex 시뮬레이션 결과를 돌려줄 때 쓸 번호입니다.
         * @param[in] ComponentToWorld 컴포넌트의 월드 행렬입니다.
         * @param[in] ComponentSpaceTransforms 스켈레톤 뼈 순서의 컴포넌트 좌표 뼈 행렬입니다.
         * @param[in] RefToLocals 스켈레톤 뼈 순서의 스키닝 행렬(역바인드 * 컴포넌트 좌표 뼈 행렬)입니다.
         * @return 천을 하나 이상 만들었으면 true입니다.
         */
        bool CreateActor(const UClothingAssetCommon& Asset, Index_t SimDataIndex, const FMatrix& ComponentToWorld, const Core::TArray<FMatrix>& ComponentSpaceTransforms, const Core::TArray<FMatrix>& RefToLocals);

        /**
         * @brief 입력의 포즈와 변환으로 천 에셋의 천을 만들어 솔버에 넣습니다.
         * @param[in] Asset 천 에셋이며 천보다 오래 살아 있어야 합니다.
         * @param[in] SimDataIndex 결과를 돌려줄 때 쓸 번호입니다.
         * @param[in] Context 현재 포즈와 변환을 담은 입력입니다.
         * @return 천을 하나 이상 만들었으면 true입니다.
         */
        bool CreateActor(const UClothingAssetCommon& Asset, Index_t SimDataIndex, const FClothingSimulationContextCommon& Context) override;

        /**
         * @brief 메시 상세 단계에 맞는 천 상세 단계를 골라 솔버의 천을 바꿉니다.
         * @details 새로 넣는 천은 입자를 현재 자세로 다시 놓고 관성, 충돌체, 모션 제약을 비웁니다.
         * @param[in] PredictedLod 메시의 상세 단계 번호입니다.
         * @param[in] ComponentToWorld 컴포넌트의 월드 행렬입니다.
         * @param[in] ComponentSpaceTransforms 스켈레톤 뼈 순서의 컴포넌트 좌표 뼈 행렬입니다.
         * @param[in] RefToLocals 스켈레톤 뼈 순서의 스키닝 행렬입니다.
         * @param[in] ForceNoRemap true이면 이전 상세 단계의 입자 위치를 옮기지 않습니다.
         * @param[in] ForceActorChecks true이면 상세 단계가 같아도 모든 천을 다시 검사합니다.
         */
        void UpdateLod(Index_t PredictedLod, const FMatrix& ComponentToWorld, const Core::TArray<FMatrix>& ComponentSpaceTransforms, const Core::TArray<FMatrix>& RefToLocals, bool ForceNoRemap, bool ForceActorChecks);

        /**
         * @brief 등록한 천을 솔버에서 빼고 천과 패브릭을 해제합니다.
         */
        void DestroyActors() override;

        /**
         * @brief 모든 천에 중력을 적용하고 솔버를 한 프레임 진행합니다.
         * @param[in] DeltaTime 진행할 초 단위 시간이며 너무 크면 잘라 냅니다.
         */
        void Simulate(float DeltaTime);

        /**
         * @brief 입력으로 천 에셋의 천을 한 프레임 진행합니다.
         * @details 입자를 현재 포즈로 스키닝해 모션 제약을 갱신하고, 기준 뼈의 월드 변환을 천에 넣어 캐릭터의 이동과 회전이 관성으로 전해지게 합니다.
         *          솔버를 진행한 뒤 입자 법선을 다시 구하고 스키닝 버퍼를 바꿉니다.
         * @param[in] Context 이번 프레임의 포즈, 변환, 시간, 중력입니다.
         */
        void Simulate(const FClothingSimulationContextCommon& Context) override;

        /**
         * @brief 천마다 현재 입자 위치와 법선, 기준 뼈 변환을 복사합니다.
         * @param[out] OutData 결과 번호를 키로 한 결과입니다.
         * @param[in] Context 기준 뼈 변환을 구할 때 쓰는 입력입니다.
         */
        void GetSimulationData(Core::TMap<Index_t, FClothSimulData>& OutData, const FClothingSimulationContextCommon& Context) const override;

        /**
         * @brief 솔버에 들어간 천이 있는지 검사합니다.
         * @return 시뮬레이션 중인 천이 하나라도 있으면 true입니다.
         */
        bool ShouldSimulate() const override;

        /**
         * @brief 모든 천에 바깥에서 정한 충돌체를 더합니다.
         * @param[in] Data 컴포넌트 좌표 또는 뼈 좌표의 충돌체입니다.
         */
        void AddExternalCollisions(const FClothCollisionData& Data) override;

        /**
         * @brief 모든 천에서 바깥에서 정한 충돌체를 뺍니다.
         */
        void ClearExternalCollisions() override;

        /**
         * @brief 천들이 쓰는 충돌체를 모읍니다.
         * @param[out] OutCollisions 모은 충돌체이며 기존 내용은 지웁니다.
         * @param[in] IncludeExternal true이면 물리 에셋과 바깥 충돌체까지, false이면 천 에셋에 넣어 둔 충돌체만 모읍니다.
         */
        void GetCollisions(FClothCollisionData& OutCollisions, bool IncludeExternal = true) const override;

        /**
         * @brief 천 하나의 현재 입자 위치를 복사합니다.
         * @details 에셋으로 만든 천은 기준 뼈 좌표이고, 격자 천은 월드 좌표입니다.
         * @param[in] ActorIndex 조회할 천의 순서입니다.
         * @param[out] OutPositions 입자 위치를 받을 배열이며 기존 내용은 지웁니다.
         * @return 시뮬레이션 중인 천이 있으면 true, 그렇지 않으면 false입니다.
         */
        bool GetParticles(Index_t ActorIndex, Core::TArray<FVector>& OutPositions) const;

        /**
         * @brief 등록한 천의 개수를 반환합니다.
         * @return 솔버에 등록한 천의 개수입니다.
         */
        Index_t GetNumActors() const;

        /**
         * @brief 마지막으로 만든 격자의 X 방향 입자 수를 반환합니다.
         * @return 격자가 없으면 0입니다.
         */
        Uint32 GetGridWidth() const;

        /**
         * @brief 마지막으로 만든 격자의 Y 방향 입자 수를 반환합니다.
         * @return 격자가 없으면 0입니다.
         */
        Uint32 GetGridHeight() const;

    private:

        void Solve(float DeltaTime);

        /**
         * @brief 천의 현재 입자 위치로 삼각형 법선을 더해 입자 법선을 구합니다.
         * @param[in,out] Actor 법선을 갱신할 천입니다.
         */
        void ComputePhysicalMeshNormals(FClothingActorNv& Actor) const;

        /**
         * @brief 천 에셋의 물리 에셋에서 구와 캡슐을 꺼내 천의 충돌체로 둡니다.
         * @details 캡슐은 양 끝 구 두 개와 둘을 잇는 캡슐 하나로 바꿉니다.
         * @param[in] Asset 물리 에셋과 사용 뼈 이름을 가진 천 에셋입니다.
         * @param[in,out] Actor 충돌체를 받을 천입니다.
         */
        void ExtractActorCollisions(const UClothingAssetCommon& Asset, FClothingActorNv& Actor) const;

        /**
         * @brief 설정을 천의 모든 상세 단계에 넣습니다.
         * @details 감쇠와 항력은 세기 기준 횟수에 맞게 바꿔 넣습니다.
         * @param[in] Config 넣을 설정입니다.
         * @param[in,out] Actor 설정을 받을 천입니다.
         */
        void ApplyClothConfig(const UClothConfigNv& Config, FClothingActorNv& Actor) const;

    private:

        /**
         * @brief 천, 패브릭, 솔버를 만드는 팩토리이며 소유하지 않습니다.
         */
        nv::cloth::Factory* mFactory{};

        /**
         * @brief 패브릭을 굽기 전에 삼각형 메시를 사각형 메시로 바꾸는 도구이며 소유하지 않습니다.
         */
        nv::cloth::ClothMeshQuadifier* mMeshQuadifier{};

        /**
         * @brief 등록한 천을 함께 진행하는 솔버입니다.
         */
        nv::cloth::Solver* mSolver{};

        /**
         * @brief 솔버에 등록한 천 목록입니다.
         */
        Core::TArray<FClothingActorNv> mActors{};

        /**
         * @brief 모든 천에 적용할 중력 가속도(m/s²)입니다.
         */
        FVector mGravity{0.0f, 0.0f, -9.81f};

        /**
         * @brief 마지막으로 천을 맞춘 메시 상세 단계 번호입니다.
         */
        Index_t mCurrentMeshLodIndex{InvalidIndex};

        /**
         * @brief 마지막으로 만든 격자의 X 방향 입자 수입니다.
         */
        Uint32 mGridWidth{};

        /**
         * @brief 마지막으로 만든 격자의 Y 방향 입자 수입니다.
         */
        Uint32 mGridHeight{};
    };
}
