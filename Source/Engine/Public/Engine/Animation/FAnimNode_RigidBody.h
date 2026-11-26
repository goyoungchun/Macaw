/**
 * @file FAnimNode_RigidBody.h
 * @brief 입력 포즈의 일부 뼈를 피직스 에셋으로 독립 시뮬레이션하는 노드입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FMatrix.h"
#include "Engine/Animation/FAnimNode_Base.h"

#include <memory>

namespace PhysicsRuntime {
    class FRigidBodySimulation;
    class UPhysicsAsset;
}

namespace Engine {

    /**
     * @brief 입력 포즈에서 피직스 에셋의 바디가 붙은 뼈를 독립 물리 장면으로 시뮬레이션해 출력 포즈에 씁니다.
     * @details 언리얼 FAnimNode_RigidBody에 대응합니다. 머리카락이나 장신구처럼 캐릭터의 일부를 월드와 분리해 시뮬레이션하는 데 씁니다.
     *          피직스 에셋에서 Kinematic으로 지정한 바디는 입력 포즈를 따라가는 지지점이고, Dynamic 바디는 관절로 이어져 중력과 지지점의 움직임에 반응합니다.
     *          시뮬레이션은 월드 공간에서 돌고, 월드 물리 장면과는 충돌하지 않습니다.
     * @note 월드나 물리 런타임, 피직스 에셋이 없으면(예: 그래프 에디터 미리보기) 입력 포즈를 그대로 통과시킵니다.
     *       스텝은 Evaluate에서 돌립니다. Kinematic 목표가 입력 포즈에서 나오는데 입력 포즈는 Evaluate에서만 만들어지기 때문입니다.
     *       피직스 에셋은 뼈가 부모보다 뒤에 오는 스켈레톤(부모 번호 < 자식 번호)을 가정합니다.
     */
    struct FAnimNode_RigidBody : public Engine::FAnimNode_Base {
    public:

        /**
         * @brief 입력과 시뮬레이션 없이 구성합니다.
         */
        FAnimNode_RigidBody();

        /**
         * @brief 시뮬레이션의 장면과 바디를 해제합니다.
         */
        ~FAnimNode_RigidBody() override;

    public:

        void Initialize(Engine::UAnimInstance& AnimInstance) override;

        void Update(const Engine::FAnimationUpdateContext& Context) override;

        void Evaluate(Engine::FPoseContext& Output) override;

        const Core::FString& GetNodeType() const override;

        void GetPoseInputs(Core::TArray<Engine::FAnimNodePoseInput>& OutInputs) override;

    public:

        /**
         * @brief 시뮬레이션할 피직스 에셋의 Content 기준 경로입니다. 비어 있으면 입력 포즈를 그대로 통과시킵니다.
         */
        Core::FString mPhysicsAssetPath{};

        /**
         * @brief 시뮬레이션할 뼈가 들어 있는 입력 포즈입니다.
         */
        Engine::FPoseLink mInput{};

    private:

        /**
         * @brief 피직스 에셋을 로드하고 시뮬레이션을 만듭니다. 이미 만들었거나 만들 수 없으면 아무것도 하지 않습니다.
         * @param[in] Output 입력 포즈를 평가한 결과이며 시작 자세로 씁니다.
         * @param[in] Global 입력 포즈의 컴포넌트 공간 변환입니다.
         * @param[in] ComponentToWorld 컴포넌트에서 월드로 가는 변환입니다.
         * @return 시뮬레이션을 쓸 수 있으면 true입니다.
         */
        bool EnsureSimulation(Engine::FPoseContext& Output, const Core::TArray<FMatrix>& Global, const FMatrix& ComponentToWorld);

    private:

        /**
         * @brief 독립 물리 장면과 바디입니다. 만들기 전에는 비어 있습니다.
         */
        std::unique_ptr<PhysicsRuntime::FRigidBodySimulation> mSimulation{};

        /**
         * @brief 로드한 피직스 에셋의 비소유 주소입니다. 에셋 관리자가 소유합니다.
         */
        PhysicsRuntime::UPhysicsAsset* mAsset{};

        /**
         * @brief 에셋 로드를 이미 시도했는지 나타냅니다. 실패한 경로를 매 프레임 다시 읽지 않기 위한 값입니다.
         */
        bool mLoadAttempted{};

        /**
         * @brief 스켈레톤 뼈 이름에서 뼈 번호를 찾는 표입니다.
         */
        Core::TMap<Core::FString, Index_t> mBoneIndices{};

        /**
         * @brief 바디 번호에서 스켈레톤 뼈 번호로 가는 표입니다. 대응하는 뼈가 없으면 InvalidIndex입니다.
         */
        Core::TArray<Index_t> mBodyToBone{};

        /**
         * @brief 뼈 번호에서 그 뼈에 붙은 Dynamic 바디 번호로 가는 표입니다. 없으면 InvalidIndex입니다.
         */
        Core::TArray<Index_t> mBoneToSimulatedBody{};

        /**
         * @brief 아직 시뮬레이션에 넘기지 않은 경과 시간(초)입니다.
         */
        Core::FTime mPendingDeltaTime{};
    };

}
