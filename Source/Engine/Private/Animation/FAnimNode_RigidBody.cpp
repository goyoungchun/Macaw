/**
 * @file FAnimNode_RigidBody.cpp
 * @brief 입력 포즈의 일부 뼈를 피직스 에셋으로 독립 시뮬레이션하는 노드를 구현합니다.
 * @ingroup Engine
 */

#include "Engine/Animation/FAnimNode_RigidBody.h"

#include "Asset/FAssetManager.h"
#include "Asset/USkeleton.h"
#include "Core/FilePath.h"
#include "Core/Math/FMatrix.h"
#include "Engine/Animation/UAnimInstance.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Engine/FWindField.h"
#include "Engine/UWorld.h"
#include "PhysicsRuntime/FRigidBodySimulation.h"
#include "PhysicsRuntime/PhysicsStats.h"
#include "PhysicsRuntime/UPhysicsAsset.h"

namespace Engine {

    FAnimNode_RigidBody::FAnimNode_RigidBody() = default;

    FAnimNode_RigidBody::~FAnimNode_RigidBody() = default;

    void FAnimNode_RigidBody::Initialize(Engine::UAnimInstance& AnimInstance) {
        Engine::FAnimNode_Base::Initialize(AnimInstance);

        // 그래프를 다시 초기화하면 시뮬레이션도 처음 자세에서 다시 시작한다.
        mSimulation.reset();
        mPendingDeltaTime = {};
    }

    void FAnimNode_RigidBody::Update(const Engine::FAnimationUpdateContext& Context) {
        mInput.Update(Context);

        // 시간만 모은다. 스텝은 입력 포즈가 나오는 Evaluate에서 돌린다.
        mPendingDeltaTime += Core::FTime::FromBase(Context.GetDeltaTime());
    }

    bool FAnimNode_RigidBody::EnsureSimulation(Engine::FPoseContext& Output, const Core::TArray<FMatrix>& Global, const FMatrix& ComponentToWorld) {
        if (mSimulation != nullptr) {
            return true;
        }

        Engine::USkeletalMeshComponent* Component{Output.GetAnimInstance().GetOwningComponent()};
        Engine::UWorld* World{Component != nullptr ? Component->GetWorld() : nullptr};
        const Asset::USkeleton* Skeleton{Component != nullptr ? Component->GetSkeleton() : nullptr};
        PhysicsRuntime::FPhysicsRuntime* Runtime{World != nullptr ? World->GetPhysicsRuntime() : nullptr};
        Asset::FAssetManager* Assets{World != nullptr ? World->GetAssets() : nullptr};

        if (Skeleton == nullptr || Runtime == nullptr || Assets == nullptr || mPhysicsAssetPath.empty()) {
            return false;
        }

        // 에셋은 한 번만 읽는다. 실패하면 그래프를 다시 초기화할 때까지 시도하지 않는다.
        if (mAsset == nullptr) {
            if (mLoadAttempted) {
                return false;
            }

            mLoadAttempted = true;

            Core::FString Error{};

            mAsset = Assets->LoadPhysicsAsset(Core::FilePath::FromUtf8(mPhysicsAssetPath), Error);

            if (mAsset == nullptr) {
                return false;
            }
        }

        const auto& Bones{Skeleton->GetBones()};

        mBoneIndices.clear();

        for (Index_t Bone{}; Bone < Bones.size(); ++Bone) {
            mBoneIndices[Bones[Bone].GetName()] = Bone;
        }

        // 바디가 붙은 뼈의 현재(입력 포즈의) 월드 변환. 스케일을 포함해 바디를 만든다.
        const auto GetBoneWorld{[&](const Core::FString& BoneName, FTransform& OutWorld) {
            const auto Found{mBoneIndices.find(BoneName)};

            return Found != mBoneIndices.end() && Found->second < Global.size() && (Global[Found->second] * ComponentToWorld).TryGetTransform(OutWorld);
        }};

        auto Simulation{std::make_unique<PhysicsRuntime::FRigidBodySimulation>()};

        if (!Simulation->Initialize(*Runtime, *mAsset, GetBoneWorld, Component->GetTransform().mScale)) {
            // 만들 수 없는 에셋은 다시 시도하지 않는다.
            mAsset = nullptr;

            return false;
        }

        mBodyToBone.assign(Simulation->GetBodyCount(), InvalidIndex);
        mBoneToSimulatedBody.assign(Bones.size(), InvalidIndex);

        for (Index_t Body{}; Body < Simulation->GetBodyCount(); ++Body) {
            const auto Found{mBoneIndices.find(Simulation->GetBoneName(Body))};

            if (!Simulation->HasBody(Body) || Found == mBoneIndices.end()) {
                continue;
            }

            mBodyToBone[Body] = Found->second;

            if (Simulation->IsSimulated(Body)) {
                mBoneToSimulatedBody[Found->second] = Body;
            }
        }

        mSimulation = std::move(Simulation);

        return true;
    }

    void FAnimNode_RigidBody::Evaluate(Engine::FPoseContext& Output) {
        mInput.Evaluate(Output);

        const Engine::USkeletalMeshComponent* Component{Output.GetAnimInstance().GetOwningComponent()};
        const Asset::USkeleton* Skeleton{Component != nullptr ? Component->GetSkeleton() : nullptr};

        if (Skeleton == nullptr || Output.mPose.size() != Skeleton->GetBones().size()) {
            return;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::AnimRigidBody};
        const auto& Bones{Skeleton->GetBones()};
        const FMatrix ComponentToWorld{Component->GetTransform().ToMatrix()};
        FMatrix WorldToComponent{};

        if (!ComponentToWorld.TryInverse(WorldToComponent)) {
            return;
        }

        // 입력 포즈를 컴포넌트 공간으로 올린다. 컴포넌트가 가진 포즈는 지난 프레임 값이므로 쓰지 않는다.
        Core::TArray<FMatrix> InputGlobal(Bones.size(), FMatrix::Identity());

        for (Index_t Bone{}; Bone < Bones.size(); ++Bone) {
            const Int32 Parent{Bones[Bone].GetParentIndex()};

            InputGlobal[Bone] = Output.mPose[Bone].ToMatrix() * (Parent >= 0 ? InputGlobal[static_cast<Index_t>(Parent)] : FMatrix::Identity());
        }

        if (!EnsureSimulation(Output, InputGlobal, ComponentToWorld)) {
            return;
        }

        // Kinematic 바디는 입력 포즈의 월드 변환을 목표로 따라간다.
        for (Index_t Body{}; Body < mBodyToBone.size(); ++Body) {
            FTransform World{};

            if (mBodyToBone[Body] != InvalidIndex && mSimulation->HasBody(Body) && !mSimulation->IsSimulated(Body) && (InputGlobal[mBodyToBone[Body]] * ComponentToWorld).TryGetTransform(World)) {
                mSimulation->SetKinematicTarget(Body, World);
            }
        }

        // 월드의 바람장에서 바람 영향이 있는 Dynamic 바디의 현재 위치의 바람을 읽어 준다. 그래프 변수가 필요 없다.
        if (const Engine::UWorld* World{Component->GetWorld()}) {
            const Engine::FWindField& Wind{World->GetWindField()};
            const bool HasWind{Wind.GetSourceCount() > 0};

            for (Index_t Body{}; Body < mBodyToBone.size(); ++Body) {
                if (mSimulation->GetWindInfluence(Body) <= 0.0f) {
                    continue;
                }

                FTransform BodyWorld{};

                mSimulation->SetBodyWind(Body, HasWind && mSimulation->GetBodyTransform(Body, BodyWorld) ? Wind.Sample(BodyWorld.mLocation) : FVector{});
            }
        }

        mSimulation->Advance(mPendingDeltaTime);
        mPendingDeltaTime = {};

        // 부모부터 내려가며 Dynamic 바디가 붙은 뼈는 바디의 월드 변환으로, 나머지는 입력 로컬을 새 부모에 이어 붙인다.
        Core::TArray<FMatrix> NewGlobal(Bones.size(), FMatrix::Identity());

        for (Index_t Bone{}; Bone < Bones.size(); ++Bone) {
            const Int32 Parent{Bones[Bone].GetParentIndex()};
            const FMatrix ParentGlobal{Parent >= 0 ? NewGlobal[static_cast<Index_t>(Parent)] : FMatrix::Identity()};
            const Index_t Body{Bone < mBoneToSimulatedBody.size() ? mBoneToSimulatedBody[Bone] : InvalidIndex};
            FTransform BodyWorld{};

            if (Body == InvalidIndex || !mSimulation->GetBodyTransform(Body, BodyWorld)) {
                NewGlobal[Bone] = Output.mPose[Bone].ToMatrix() * ParentGlobal;

                continue;
            }

            // 바디에는 스케일이 없으므로 입력 포즈의 월드 스케일을 그대로 쓴다.
            FTransform InputWorld{};
            (InputGlobal[Bone] * ComponentToWorld).TryGetTransform(InputWorld);

            const FTransform NewWorld{BodyWorld.mLocation, BodyWorld.mRotation, InputWorld.mScale};
            FMatrix ParentInverse{};

            NewGlobal[Bone] = NewWorld.ToMatrix() * WorldToComponent;

            if (Parent < 0) {
                FMatrix Local{NewGlobal[Bone]};

                // 역행렬의 부동소수점 오차가 아핀 검사에 걸리지 않도록 마지막 열을 고정한다.
                for (Index_t Row{}; Row < 3; ++Row) {
                    Local(Row, 3) = 0.0f;
                }

                Local(3, 3) = 1.0f;
                Local.TryGetTransform(Output.mPose[Bone]);
            } else if (ParentGlobal.TryInverse(ParentInverse)) {
                FMatrix Local{NewGlobal[Bone] * ParentInverse};

                for (Index_t Row{}; Row < 3; ++Row) {
                    Local(Row, 3) = 0.0f;
                }

                Local(3, 3) = 1.0f;
                Local.TryGetTransform(Output.mPose[Bone]);
            }
        }
    }

    const Core::FString& FAnimNode_RigidBody::GetNodeType() const {
        static const Core::FString Type{"Rigid Body"};

        return Type;
    }

    void FAnimNode_RigidBody::GetPoseInputs(Core::TArray<Engine::FAnimNodePoseInput>& OutInputs) {
        OutInputs.push_back({"Pose", &mInput});
    }

}
