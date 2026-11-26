/**
 * @file USkeletalMeshComponent.h
 * @brief 스켈레탈 메시의 에셋·재질과 객체별 본 포즈를 관리합니다.
 * @ingroup Engine
 */

#pragma once

#include "CoreUObject/TObjectRef.h"

#include "Core/Types.h"
#include "Engine/Component/UMeshComponent.h"
#include "Engine/Animation/UAnimInstance.h"
#include "Engine/FSkeletalMeshPose.h"
#include "PhysicsRuntime/FClothSimulData.h"
#include "PhysicsRuntime/FClothingSimulationContextCommon.h"
#include "PhysicsRuntime/IClothingSimulation.h"

#include <memory>
#include "USkeletalMeshComponent.generated.h"

namespace Asset {
    class UAnimSequence;
    class USkeletalMesh;
    class USkeleton;
}

namespace PhysicsRuntime {
    class FPhysicsScene;
    struct FConstraintInstance;
    class UPhysicsAsset;
}

namespace Engine {

    class FPrimitiveDrawInterface;
    class UAnimInstance;
    class UAnimSingleNodeInstance;

    /**
     * @brief 공유 바인드 형상을 객체별 포즈로 변형하여 표시하는 컴포넌트입니다.
     */
    UCLASS()
    class USkeletalMeshComponent final : public Engine::UMeshComponent {
        GENERATED_BODY()

    private:
        class FPostPhysicsTick final : public Engine::FTickFunction {
        public:
            explicit FPostPhysicsTick(Engine::USkeletalMeshComponent& Target);

        private:
            const CoreUObject::UObject* GetTarget() const override;
            void ExecuteTick(float DeltaTime) override;

        private:
            Engine::USkeletalMeshComponent& mTarget;
        };

        struct FArticulatedBody {
            std::unique_ptr<PhysicsRuntime::FBodyInstance> mInstance{};
            float mBlendWeight{1.0f};
            bool mCanSimulate{};
        };

    public:

        /**
         * @brief 에디터에서도 에셋 변경을 감지하는 컴포넌트를 생성합니다.
         */
        USkeletalMeshComponent();

        /**
         * @brief 컴포넌트를 파괴합니다.
         */
        ~USkeletalMeshComponent() override;

    public:

        /**
         * @brief 기본 프리미티브 구분값을 반환합니다.
         * @return 에셋이 없을 때도 대신 표시하지 않는 Cube 구분값입니다.
         */
        Engine::EPrimitiveType GetPrimitiveType() const override;

        /**
         * @brief 공유 바인드 형상을 반환합니다.
         * @return 공통 렌더 경로용 형상이며 없으면 nullptr입니다.
         */
        Asset::UStaticMesh* GetStaticMesh() const override;

        /**
         * @brief 연결된 스켈레탈 메시를 반환합니다.
         * @return 에셋 관리자가 소유하는 비소유 주소입니다.
         */
        Asset::USkeletalMesh* GetSkeletalMesh() const;

        /**
         * @brief 현재 변형 위치와 경계를 가진 CPU 형상을 반환합니다.
         * @return 피킹용 형상입니다.
         */
        const Asset::FMeshData& GetMeshData() const override;
        FBox GetWorldBounds() const override;

        /**
         * @brief 프로젝트 상대 에셋 경로를 반환합니다.
         * @return 저장되는 메시 경로입니다.
         */
        const Core::FString& GetMeshPath() const;

        /**
         * @brief 에셋 경로를 설정하고 포즈를 초기화합니다.
         * @param Path Content 기준 .masset 경로입니다.
         * @return 경로가 유효하고 가능한 경우 로드했으면 true입니다.
         */
        bool SetMeshPath(const Core::FString& Path);

        /**
         * @brief 현재 미리보기 또는 컴포넌트가 참조하는 스켈레톤을 반환합니다.
         * @return 외부 소유의 비소유 포인터이며 미설정 시 nullptr입니다.
         */
        Asset::USkeleton* GetSkeleton() const;

        /**
         * @brief 컴포넌트의 스켈레톤 에셋 경로를 반환합니다.
         * @return 컴포넌트가 보관하는 경로의 읽기 전용 참조입니다.
         */
        const Core::FString& GetSkeletonPath() const;

        /**
         * @brief 스켈레톤 경로를 설정하고 메시 포즈·재생 인스턴스를 갱신합니다.
         * @param Path 처리할 에셋 또는 파일 경로입니다.
         * @return 경로 설정을 허용하면 true입니다.
         */
        bool SetSkeletonPath(const Core::FString& Path);

        /**
         * @brief EditorPreview 월드에 한해 경로 로드 없이 메시·스켈레톤 포인터를 연결합니다.
         * @param Mesh 호출자가 수명을 보장하는 미리보기 메시입니다.
         * @param Skeleton 호출자가 수명을 보장하는 스켈레톤입니다.
         * @return 월드와 에셋 조합이 유효하면 true이며 호출자가 에셋 수명을 보장해야 합니다.
         */
        bool SetPreviewAssets(Asset::USkeletalMesh* Mesh, Asset::USkeleton* Skeleton);

        /**
         * @brief 저장된 에셋 경로와 현재 에셋 개정을 해석합니다.
         * @return 에셋과 포즈를 준비했으면 true입니다.
         */
        bool ResolveMesh() override;

        /**
         * @brief 뼈별 바디와 관절을 만들 물리 에셋을 반환합니다.
         * @return 덮어쓰기 에셋, 메시의 에셋, 메시가 만든 에셋 순으로 찾은 물리 에셋이며, 없으면 nullptr입니다.
         */
        PhysicsRuntime::UPhysicsAsset* GetPhysicsAsset() const;

        /**
         * @brief 메시의 물리 에셋 대신 쓸 물리 에셋 경로를 반환합니다.
         * @return Content 상대 경로이며, 메시의 것을 쓰면 빈 문자열입니다.
         */
        const Core::FString& GetPhysicsAssetOverride() const;

        /**
         * @brief 메시의 물리 에셋 대신 쓸 물리 에셋 경로를 설정하고 뼈별 바디를 다시 만듭니다.
         * @param[in] Path Content 상대 경로이며, 비우면 메시의 물리 에셋을 씁니다.
         * @return 경로가 올바르고 에셋을 불러왔으면 true입니다.
         */
        bool SetPhysicsAssetOverride(const Core::FString& Path);

        /** @brief 메시가 뼈별 바디나 메시 전체 바디로 물리 충돌을 만드는지 반환합니다. */
        bool IsCollisionEnabled() const;

        /** @brief 메시의 표시를 유지하며 물리 충돌을 켜거나 끄고 물리 상태를 다시 만듭니다. */
        void SetCollisionEnabled(bool Enabled);

        /**
         * @brief 뼈 이름으로 뼈 번호를 찾습니다.
         * @param[in] BoneName 찾을 뼈의 이름입니다.
         * @return 뼈 번호이며, 없으면 InvalidIndex입니다.
         */
        Index_t GetBoneIndex(const Core::FString& BoneName) const;

        /**
         * @brief 뼈의 현재 월드 변환을 구합니다.
         * @param[in] BoneIndex 변환을 구할 뼈 번호입니다.
         * @param[out] OutTransform 컴포넌트 변환을 적용한 뼈의 월드 변환입니다.
         * @return 포즈가 준비되어 변환을 구했으면 true입니다.
         */
        bool GetBoneWorldTransform(Index_t BoneIndex, FTransform& OutTransform) const;

        /**
         * @brief 모든 뼈별 바디의 시뮬레이션 여부를 바꿉니다.
         * @param[in] Simulate true이면 물리로 움직이고, false이면 키네마틱으로 애니메이션을 따라갑니다.
         */
        void SetAllBodiesSimulatePhysics(bool Simulate);
        bool SetBodySimulatePhysics(const Core::FString& BoneName, bool Simulate);
        bool SetAllBodiesBelowSimulatePhysics(const Core::FString& BoneName, bool Simulate, bool IncludeSelf = true);
        bool IsBodySimulatingPhysics(const Core::FString& BoneName) const;

        bool SetBodyPhysicsBlendWeight(const Core::FString& BoneName, float Weight);
        bool SetAllBodiesBelowPhysicsBlendWeight(const Core::FString& BoneName, float Weight, bool IncludeSelf = true);

        bool AddImpulseToBody(const Core::FString& BoneName, const FVector& Impulse, bool VelocityChange = false);
        bool AddImpulseAtLocationToBody(const Core::FString& BoneName, const FVector& Impulse, const FVector& Location);

        /**
         * @brief 현재 본 포즈를 반환합니다.
         * @return 컴포넌트가 소유한 포즈입니다.
         */
        const Engine::FSkeletalMeshPose& GetPose() const;

        /**
         * @brief 천 시뮬레이션을 준비하고 섹션에 연결된 천 에셋마다 천을 다시 만듭니다.
         * @details 물리 장면이 있는 월드에서만 천을 만들고, 메시에 천 에셋이 없으면 시뮬레이션을 해제합니다.
         */
        void RecreateClothingActors();

        /**
         * @brief 만든 천과 마지막 결과를 모두 해제합니다.
         */
        void ReleaseAllClothingResources();

        /**
         * @brief 천 시뮬레이션이 있으면 이번 프레임의 입력을 채워 천을 진행합니다.
         * @param[in] DeltaTime 초 단위 프레임 시간입니다.
         */
        void TickClothing(float DeltaTime);

        /**
         * @brief 천마다 시뮬레이션 결과를 새로 꺼냅니다.
         * @param[out] OutClothSimData 메시 천 에셋 번호를 키로 한 결과입니다.
         */
        void GetUpdateClothSimulationData(Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& OutClothSimData) const;

        /**
         * @brief 마지막 프레임에 저장한 천 시뮬레이션 결과를 반환합니다.
         * @return 메시 천 에셋 번호를 키로 한 결과의 읽기 전용 참조입니다.
         */
        const Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& GetCurrentClothingData() const;

        /**
         * @brief 마지막 천 시뮬레이션 결과의 삼각형 변을 선분으로 그립니다.
         * @details 두 끝 입자가 모두 고정된 변은 빨강, 나머지는 초록으로 그립니다.
         * @param[in,out] DrawInterface 선분을 수집할 그리기 인터페이스입니다.
         * @param[in] Thickness 선분 두께입니다.
         */
        void DebugDrawClothing(Engine::FPrimitiveDrawInterface& DrawInterface, float Thickness) const;

        /**
         * @brief 천 입자의 최대 거리에 곱할 배율을 반환합니다.
         * @return 1이면 천 에셋의 최대 거리를 그대로 씁니다.
         */
        float GetClothMaxDistanceScale() const;

        /**
         * @brief 천 입자의 최대 거리에 곱할 배율을 설정합니다.
         * @details 0이면 모든 입자가 스키닝 위치에 붙어 애니메이션만 따라갑니다.
         * @param[in] Scale 최대 거리 배율입니다.
         */
        void SetClothMaxDistanceScale(float Scale);

        /**
         * @brief 천 섹션을 그릴 때 시뮬레이션 위치를 섞는 비율을 반환합니다.
         * @return 0이면 스키닝 위치, 1이면 시뮬레이션 위치입니다.
         */
        float GetClothBlendWeight() const;

        /**
         * @brief 천 섹션을 그릴 때 시뮬레이션 위치를 섞는 비율을 설정합니다.
         * @param[in] BlendWeight 0에서 1 사이 값이며 범위를 벗어나면 잘라 냅니다.
         */
        void SetClothBlendWeight(float BlendWeight);

        /**
         * @brief 천에 부는 월드 좌표 바람 속도를 반환합니다.
         * @return 바람 속도(m/s)입니다.
         */
        const FVector& GetClothWindVelocity() const;

        /**
         * @brief 천에 부는 월드 좌표 바람 속도를 설정합니다.
         * @param[in] Velocity 바람 속도(m/s)입니다.
         * @return 항상 true입니다.
         */
        bool SetClothWindVelocity(const FVector& Velocity);

        /**
         * @brief 다음 천 진행에서 천을 새 위치로 옮기되 속도는 유지하게 합니다.
         */
        void ForceClothNextUpdateTeleport();

        /**
         * @brief 다음 천 진행에서 천을 새 위치로 옮기고 입자를 스키닝 위치에 다시 놓게 합니다.
         */
        void ForceClothNextUpdateTeleportAndReset();

        /**
         * @brief 요청한 천 순간 이동을 취소합니다.
         */
        void ResetClothTeleportMode();

        /**
         * @brief 본 하나의 부모 기준 변환을 변경합니다.
         * @param Bone 본 인덱스입니다.
         * @param Transform 부모 기준 아핀 행렬입니다.
         * @return 유효한 포즈를 적용하면 true입니다.
         */
        bool SetBoneLocalTransform(Index_t Bone, const FMatrix& Transform);

        /**
         * @brief 전체 로컬 포즈를 변경합니다.
         * @param Transforms 본 인덱스순 부모 기준 행렬입니다.
         * @return 적용하면 true입니다.
         */
        bool SetLocalPose(const Core::TArray<FMatrix>& Transforms);

        /**
         * @brief 매 프레임 바뀌는 런타임 포즈를 스키닝 행렬에만 반영합니다.
         * @details 애니메이션 재생과 같이 검증과 CPU 형상·경계 갱신을 생략하므로, 피킹 형상과 경계는 마지막으로 계산한 값을 유지합니다.
         * @param Transforms 본 인덱스순 부모 기준 행렬입니다.
         * @return 본 수가 스켈레톤과 같아 적용했으면 true입니다.
         */
        bool SetAnimatedLocalPose(Core::TArray<FMatrix> Transforms);

        /**
         * @brief 모든 본을 바인드 포즈로 복원합니다.
         * @return 복원하면 true입니다.
         */
        bool ResetPose();

        /**
         * @brief 저장 대상인 로컬 본 포즈를 반환합니다.
         * @return 포즈가 유지하는 로컬 행렬의 참조입니다.
         */
        const Core::TArray<FMatrix>& GetSerializedPose() const;

        /**
         * @brief 저장된 로컬 본 포즈를 검증하여 복원합니다.
         * @param Transforms 복원할 로컬 행렬입니다.
         * @return 모든 행렬이 유한하고 역행렬을 가지면 true입니다.
         */
        bool SetSerializedPose(const Core::TArray<FMatrix>& Transforms);

        /**
         * @brief 현재 포즈와 재질을 복사한 프록시를 생성합니다.
         * @return GPU 스키닝에 필요한 프록시입니다.
         */
        Engine::FPrimitiveSceneProxy CreateSceneProxy() const override;

        /**
         * @brief 에셋과 포즈가 준비되었는지 검사합니다.
         * @return 표시 가능한 경우 true입니다.
         */
        bool ShouldCreateRenderState() const override;

        /**
         * @brief 메시 전체를 하나의 바디로 다룰 충돌 형상을 반환합니다.
         * @return 불러온 스켈레탈 메시의 바인드 포즈 경계 바디 셋업이며, 메시가 없으면 nullptr입니다.
         * @note 바디는 애니메이션 포즈를 따라가지 않습니다. 본별 바디는 피직스 에셋에서 다룹니다.
         */
        const PhysicsRuntime::UBodySetup* GetBodySetup() const override;

        /**
         * @brief 뼈별 바디가 현재 자세에서 겹치는 다른 바디를 장면 쿼리로 찾습니다.
         * @param[out] OutOverlaps 겹친 바디 인스턴스의 비소유 주소 목록이며, 중복 없이 채웁니다.
         * @return 쿼리할 바디가 하나라도 있어 쿼리를 수행했으면 true입니다.
         * @note 뼈별 바디가 없으면 메시 전체 바디로 쿼리합니다. 결과에는 이 컴포넌트의 다른 뼈 바디가 들어 있을 수 있습니다.
         */
        bool ComponentOverlapMulti(Core::TArray<PhysicsRuntime::FBodyInstance*>& OutOverlaps) const override;

        /**
         * @brief 물리 에셋이나 메시 경계 바디 셋업이 있으면 물리 상태가 필요하다고 판단합니다.
         * @return 뼈별 바디나 메시 전체 바디를 만들 수 있으면 true입니다.
         */
        bool ShouldCreatePhysicsState() const override;

        /**
         * @brief 재등록 때 재해석하도록 에셋 참조를 해제합니다.
         */
        void UnregisterComponent() override;
        void RegisterComponentTickFunctions(bool Register) override;

        /**
         * @brief 플레이 중이면 애니메이션 포즈를 갱신합니다.
         * @param DeltaTime 프레임 경과 시간입니다.
         */
        void Tick(float DeltaTime) override;

        /**
         * @brief 지정한 애니메이션 에셋이 있으면 재생을 시작합니다.
         */
        void BeginPlay() override;

        /**
         * @brief 애니메이션 인스턴스를 해제하고 마지막 포즈를 유지합니다.
         */
        void EndPlay() override;

        /**
         * @brief 단일 노드 애니메이션 인스턴스로 시퀀스 하나를 재생합니다.
         * @details 언리얼 PlayAnimation과 같이 AnimationSingleNode 모드로 바꾸고 시퀀스를 지정한 뒤 재생을 시작합니다. 포즈는 플레이 중 Tick에서만 갱신됩니다.
         * @param[in] Sequence 재생할 시퀀스입니다. 에셋 관리자가 소유합니다.
         * @param[in] Looping 반복 재생 여부입니다.
         */
        void PlayAnimation(const Asset::UAnimSequence* Sequence, bool Looping);

        /**
         * @brief 애니메이션 인스턴스를 갱신하고 결과 포즈를 스키닝 행렬에 반영합니다.
         * @param DeltaTime 프레임 경과 시간입니다.
         */
        void TickAnimation(float DeltaTime);

        /**
         * @brief 시뮬레이션한 바디 위치를 읽어 뼈 포즈에 씁니다.
         * @note 바디가 없는 뼈는 부모 뼈 기준 위치를 그대로 유지합니다.
         * @note Tick이 돌지 않는 에디터 미리보기가 물리 스텝 뒤에 직접 호출할 수 있도록 공개합니다.
         */
        void BlendInPhysics();

        /**
         * @brief 현재 애니메이션 인스턴스를 반환합니다.
         * @details AnimClass를 지정했으면 에디터에서도 항상 그 클래스의 인스턴스가 있으며, 설정 값을 담고 시각화 도구가 그래프를 표시합니다.
         * AnimClass가 비어 있으면 플레이 중 단일 노드 인스턴스만 있습니다.
         * @return 없으면 nullptr입니다.
         */
        Engine::UAnimInstance* GetAnimInstance() const;

        /**
         * @brief 실행 애니메이션 그래프의 Float 변수를 변경합니다.
         * @param Name 조회하거나 설정할 그래프 변수 이름입니다.
         * @param Value 표시·비교하거나 적용할 값입니다.
         * @return 지정한 변수를 설정했으면 true입니다.
         */
        bool SetAnimGraphFloat(const Core::FString& Name, float Value);

        /**
         * @brief 실행 애니메이션 그래프의 Bool 변수를 변경합니다.
         * @param Name 조회하거나 설정할 그래프 변수 이름입니다.
         * @param Value 표시·비교하거나 적용할 값입니다.
         * @return 지정한 변수를 설정했으면 true입니다.
         */
        bool SetAnimGraphBool(const Core::FString& Name, bool Value);

        /**
         * @brief 이름으로 지정한 그래프 에셋 플레이어를 다시 시작합니다.
         * @param Name 실행 그래프에서 찾을 에셋 플레이어 이름입니다.
         * @return 플레이어를 찾아 다시 시작했으면 true입니다.
         */
        bool RestartAnimGraphPlayer(const Core::FString& Name);

        /**
         * @brief 이름으로 지정한 그래프 에셋 플레이어의 종료 여부를 확인합니다.
         * @param Name 실행 그래프에서 찾을 에셋 플레이어 이름입니다.
         * @return 플레이어가 존재하고 재생을 마쳤으면 true입니다.
         */
        bool IsAnimGraphPlayerFinished(const Core::FString& Name) const;

        /**
         * @brief 애니메이션 인스턴스 클래스 이름을 반환합니다.
         * @return UAnimInstance 파생 클래스 이름이며, 비어 있으면 Animation 시퀀스를 단일 노드로 재생합니다.
         */
        const Core::FString& GetAnimClass() const;

        /**
         * @brief 애니메이션 인스턴스 클래스를 바꾸고 그 클래스의 인스턴스를 새로 만듭니다.
         * @details 언리얼의 AnimClass(애니메이션 블루프린트)에 대응합니다. 클래스를 바꾸면 이전 인스턴스의 설정 값은 버립니다.
         * @param[in] ClassName 생성할 수 있는 UAnimInstance 파생 클래스 이름이거나 빈 문자열입니다.
         * @return 클래스가 유효해 적용했으면 true입니다.
         */
        bool SetAnimClass(const Core::FString& ClassName);

        /**
         * @brief 단일 시퀀스 재생에 사용하는 에셋 경로를 반환합니다.
         * @return 컴포넌트가 보관하는 경로의 읽기 전용 참조입니다.
         */
        const Core::FString& GetAnimationPath() const;

        /**
         * @brief 단일 시퀀스 경로를 설정하고 현재 재생 인스턴스에 반영합니다.
         * @param Path 처리할 에셋 또는 파일 경로입니다.
         * @return 경로 설정을 허용하면 true입니다.
         */
        bool SetAnimationPath(const Core::FString& Path);

        /**
         * @brief 애니메이션 반복 재생 설정을 반환합니다.
         * @return 반복 재생을 사용하면 true입니다.
         */
        bool IsLooping() const;

        /**
         * @brief 애니메이션의 반복 재생 여부를 설정합니다.
         * @param Looping 끝점을 넘어 반복할지 여부입니다.
         */
        void SetLooping(bool Looping);

        /**
         * @brief 애니메이션 재생 속도 배율을 반환합니다.
         * @return 원래 속도에 곱하는 재생 배율입니다.
         */
        float GetPlayRate() const;

        /**
         * @brief 애니메이션 재생 속도 배율을 설정합니다.
         * @param PlayRate 원래 재생 속도에 곱하는 배율입니다.
         */
        void SetPlayRate(float PlayRate);

        /**
         * @brief 단일 시퀀스 재생용 루트 모션 설정을 반환합니다.
         * @return 현재 단일 노드 모드입니다.
         */
        Engine::ERootMotionMode GetSingleNodeRootMotionMode() const;

        /**
         * @brief 단일 시퀀스 재생에서 사용할 루트 모션 모드를 설정합니다.
         * @param Mode 설정할 동작 또는 표시 모드입니다.
         */
        void SetSingleNodeRootMotionMode(Engine::ERootMotionMode Mode);

        /**
         * @brief 루트 모션 추출·전달 정책을 반환합니다.
         * @return 현재 루트 모션 모드입니다.
         */
        Engine::ERootMotionMode GetRootMotionMode() const;

        /**
         * @brief 루트 모션 추출·전달 정책을 변경합니다.
         * @param Mode 설정할 동작 또는 표시 모드입니다.
         */
        void SetRootMotionMode(Engine::ERootMotionMode Mode);

        /**
         * @brief 추출한 루트 모션을 액터에 자동 적용하는지 확인합니다.
         * @return 자동 적용을 사용하면 true입니다.
         * @note 물리 시뮬레이션 중에는 이 설정과 관계없이 루트 모션을 소비만 하고 적용하지 않습니다.
         */
        bool IsRootMotionAppliedToActor() const;

        /**
         * @brief 소비한 루트 모션을 액터 변환에 자동 적용할지 설정합니다.
         * @param Apply 루트 모션을 액터에 자동 적용할지 여부입니다.
         */
        void SetApplyRootMotionToActor(bool Apply);

        /**
         * @brief 애니메이션 인스턴스에 누적된 루트 모션을 소비합니다.
         * @return 소비한 증분이며 사용 가능한 인스턴스가 없으면 항등 변환입니다.
         */
        FTransform ConsumeRootMotion();

        /**
         * @brief 프로퍼티를 직렬화하고 로드한 포즈를 에셋에 다시 연결합니다.
         * @param Archive 저장 또는 복원할 아카이브입니다.
         */
        void Serialize(Core::FArchive& Archive) override;

    protected:
        /**
         * @brief 메시와 머티리얼을 해석하고 천 에셋마다 천을 만듭니다.
         */
        void OnRegister() override;

        /**
         * @brief 물리 에셋이 있으면 뼈마다 바디를 만들고 관절로 잇고, 없으면 메시 전체 바디 하나를 만듭니다.
         */
        void OnCreatePhysicsState() override;

        /**
         * @brief 관절과 뼈별 바디를 모두 해제합니다.
         */
        void OnDestroyPhysicsState() override;

    private:
        /**
         * @brief 물리 에셋의 설계도마다 뼈 위치에 바디를 만들고 관절로 이어 물리 장면에 넣습니다.
         * @param[in] PhysScene 바디를 넣을 물리 장면입니다.
         * @note 이미 바디가 있으면 아무것도 하지 않습니다.
         */
        void InitArticulated(PhysicsRuntime::FPhysicsScene& PhysScene);

        /**
         * @brief 컴포넌트 바디 설정에서 물리 시뮬레이션이 켜져 있는지 반환합니다.
         * @return 뼈별 바디나 메시 전체 바디를 물리로 움직이도록 설정했으면 true입니다.
         */
        bool ShouldSimulateBodies() const;

        bool ShouldAnimatePhysicsPose() const;
        Index_t FindPhysicsBodyIndex(const Core::FString& BoneName) const;
        Core::TArray<Index_t> FindPhysicsBodiesBelow(const Core::FString& BoneName, bool IncludeSelf) const;
        bool SetBodySimulation(FArticulatedBody& Body, bool Simulate);

        /**
         * @brief 관절과 뼈별 바디를 모두 해제합니다.
         * @note 관절이 바디를 가리키므로 관절을 먼저 해제합니다.
         */
        void TermArticulated();

        /**
         * @brief 키네마틱 바디를 현재 애니메이션 포즈의 뼈 위치로 옮깁니다.
         */
        void UpdateKinematicBonesToAnim();

        const Core::FString& GetAssignedPhysicsAssetPath() const;

        /**
         * @brief 만든 천을 모두 해제합니다.
         */
        void RemoveAllClothingActors();

        /**
         * @brief 이번 프레임의 입력을 채워 천을 진행하고 결과를 저장합니다.
         * @param[in] DeltaTime 초 단위 프레임 시간이며 0이면 진행하지 않습니다.
         */
        void UpdateClothStateAndSimulate(float DeltaTime);

        /**
         * @brief 현재 포즈, 변환, 시간, 중력으로 천 시뮬레이션 입력을 채웁니다.
         * @param[in] DeltaTime 초 단위 프레임 시간입니다.
         */
        void UpdateClothSimulationContext(Core::FTime DeltaTime);

        /**
         * @brief 기준 뼈가 한 프레임에 너무 멀리 움직이거나 많이 돌았으면 천 순간 이동을 요청합니다.
         */
        void CheckClothTeleport();

        /**
         * @brief 기준 뼈의 지금 월드 행렬을 구합니다.
         * @return 기준 뼈의 월드 행렬이며 포즈가 없으면 컴포넌트의 월드 행렬입니다.
         */
        FMatrix GetClothRootBoneWorldMatrix() const;

        /**
         * @brief 천 시뮬레이션 결과를 컴포넌트에 저장합니다.
         */
        void WritebackClothingSimulationData();

        /**
         * @brief 메시 경로를 에셋 관리자에서 해석합니다.
         * @param Path Content 상대 경로입니다.
         * @return 로드 실패 시 nullptr입니다.
         */
        Asset::USkeletalMesh* LoadMesh(const Core::FString& Path) const;

        /**
         * @brief 스켈레톤을 캐시에서 찾거나 파일에서 읽어 캐시에 보관합니다.
         * @param Path 처리할 에셋 또는 파일 경로입니다.
         * @return 관리자가 소유하는 스켈레톤이며 실패하면 nullptr입니다.
         */
        Asset::USkeleton* LoadSkeleton(const Core::FString& Path) const;

        /**
         * @brief 월드의 에셋 관리자를 통해 물리 에셋을 로드합니다.
         * @param[in] Path Content 상대 경로입니다.
         * @return 관리자가 소유하는 물리 에셋이며 실패하면 nullptr입니다.
         */
        PhysicsRuntime::UPhysicsAsset* LoadPhysicsAsset(const Core::FString& Path) const;

        /**
         * @brief 월드의 에셋 관리자를 통해 시퀀스를 로드합니다.
         * @param Path 처리할 에셋 또는 파일 경로입니다.
         * @return 관리자가 소유하는 시퀀스이며 실패하면 nullptr입니다.
         */
        Asset::UAnimSequence* LoadAnimation(const Core::FString& Path) const;

        /**
         * @brief AnimClass의 인스턴스를 새로 만들고 Outer를 이 컴포넌트로 지정합니다.
         * @return AnimClass가 비어 있거나 생성할 수 없으면 false입니다.
         */
        bool CreateAnimClassInstance();

        /**
         * @brief AnimClass 인스턴스를 초기화합니다. 월드가 없으면 에셋을 로드할 수 없으므로 등록 뒤 ResolveMesh가 다시 호출합니다.
         */
        void InitializeAnimClassInstance();

        /**
         * @brief 컴포넌트 기준의 루트 모션 증분을 소유 액터 변환에 적용합니다.
         * @param Delta 이동 입력 또는 루트 모션 증분입니다.
         */
        void ApplyRootMotion(const FTransform& Delta);

    private:

        /**
         * @brief Content 기준 스켈레탈 메시 경로입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Mesh", Name = "SkeletalMesh", Meta = (AssetType = "SkeletalMesh"), Getter = GetMeshPath, Setter = SetMeshPath)
        Core::FString mMeshPath{};

        /**
         * @brief 스켈레톤 에셋 경로입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Mesh", Name = "Skeleton", Meta = (AssetType = "Skeleton"), Getter = GetSkeletonPath, Setter = SetSkeletonPath)
        Core::FString mSkeletonPath{};

        /**
         * @brief 메시의 물리 에셋 대신 쓸 물리 에셋 경로입니다.
         */
        UPROPERTY(Edit, Serialize, Name = "PhysicsAssetOverride", Category = "Physics", Meta = (AssetType = "PhysicsAsset", EmptyLabel = "Use mesh physics asset"), Getter = GetPhysicsAssetOverride, Setter = SetPhysicsAssetOverride)
        Core::FString mPhysicsAssetOverride{};

        /**
         * @brief 물리 충돌을 만들지 여부입니다. 차량 바퀴 본처럼 표시만 하는 메시는 끕니다.
         */
        UPROPERTY(Edit, Serialize, Name = "CollisionEnabled", Getter = IsCollisionEnabled, Setter = SetCollisionEnabled)
        bool mCollisionEnabled{true};

        /**
         * @brief 애니메이션 그래프를 만드는 UAnimInstance 파생 클래스 이름입니다. 비어 있으면 Animation을 단일 노드로 재생합니다.
         * @details 인스턴스의 Serialize 프로퍼티는 이 컴포넌트를 저장할 때 AnimInstance 객체 안에 함께 저장합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Animation", Name = "AnimClass", Getter = GetAnimClass, Setter = SetAnimClass)
        Core::FString mAnimClass{};

        /**
         * @brief 다른 재생 모드에서도 유지할 애니메이션 그래프 경로입니다.
         */
        Core::FString mInactiveAnimGraph{};

        /**
         * @brief 플레이를 시작하면 재생할 Content 기준 애니메이션 시퀀스 경로입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Animation", Name = "Animation", Meta = (AssetType = "AnimSequence"), Getter = GetAnimationPath, Setter = SetAnimationPath)
        Core::FString mAnimationPath{};

        /**
         * @brief 애니메이션을 반복 재생할지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Animation", Name = "Looping", Getter = IsLooping, Setter = SetLooping)
        bool mLooping{true};

        /**
         * @brief 애니메이션 재생 속도 배율입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Animation", Name = "PlayRate", Getter = GetPlayRate, Setter = SetPlayRate)
        float mPlayRate{1.0f};

        /**
         * @brief 단일 시퀀스 재생 시 적용할 루트 모션 방식입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Name = "SingleNodeRootMotionMode", Getter = GetSingleNodeRootMotionMode, Setter = SetSingleNodeRootMotionMode)
        Engine::ERootMotionMode mSingleNodeRootMotionMode{Engine::ERootMotionMode::NoRootMotionExtraction};

        /**
         * @brief 소비한 루트 모션을 소유 액터 변환에 자동 적용할지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Settings", Name = "ApplyRootMotionToActor", Getter = IsRootMotionAppliedToActor, Setter = SetApplyRootMotionToActor)
        bool mApplyRootMotionToActor{true};

        /**
         * @brief 애니메이션 인스턴스입니다. AnimClass가 있으면 항상 존재하고, 없으면 플레이 중 단일 노드 인스턴스만 존재합니다.
         * @details Outer는 이 컴포넌트이므로 에디터는 인스턴스를 월드 객체로 편집할 수 있습니다.
         */
        std::unique_ptr<Engine::UAnimInstance> mAnimInstance{};

        /**
         * @brief 에셋 관리자가 소유하는 메시입니다.
         */
        CoreUObject::TObjectRef<Asset::USkeletalMesh> mSkeletalMesh{};

        /**
         * @brief 에셋 관리자가 소유하거나 미리보기 호출자가 수명을 보장하는 스켈레톤입니다.
         */
        CoreUObject::TObjectRef<Asset::USkeleton> mSkeleton{};

        /**
         * @brief EditorPreview 월드에서 경로 해석 대신 직접 에셋 참조를 유지하는지 여부입니다.
         */
        bool mPreviewAssets{};

        /**
         * @brief 객체별 로컬·전역 포즈와 CPU 피킹 형상입니다.
         */
        UPROPERTY(Serialize, SerializeLate, Name = "BoneLocalTransforms", Getter = GetSerializedPose, Setter = SetSerializedPose, Array)
        Engine::FSkeletalMeshPose mPose{};

        /**
         * @brief 물리 에셋의 설계도 순서대로 만든 뼈별 바디입니다.
         * @note 바디는 userData가 주소를 가리키므로 포인터로 보관합니다.
         */
        Core::TArray<FArticulatedBody> mBodies{};
        Core::TArray<FMatrix> mUnblendedLocalPose{};
        FPostPhysicsTick mPostPhysicsTick{*this};

        /**
         * @brief 물리 에셋의 관절 설정 순서대로 만든 관절입니다.
         * @note 관절은 userData가 주소를 가리키므로 포인터로 보관합니다.
         */
        Core::TArray<std::unique_ptr<PhysicsRuntime::FConstraintInstance>> mConstraints{};

        /**
         * @brief 이 컴포넌트의 천을 진행하는 시뮬레이션이며 천 에셋이 없으면 비어 있습니다.
         * @note 천 시스템 모듈이 만들었으므로 모듈보다 먼저 해제해야 합니다.
         */
        std::unique_ptr<PhysicsRuntime::IClothingSimulation> mClothingSimulation{};

        /**
         * @brief 천 시뮬레이션에 넘기는 이번 프레임의 입력입니다.
         */
        PhysicsRuntime::FClothingSimulationContextCommon mClothingSimulationContext{};

        /**
         * @brief 천 입자의 최대 거리에 곱할 배율이며 매 프레임 천 시뮬레이션 입력에 넣습니다.
         */
        UPROPERTY(Edit, Serialize, Name = "ClothMaxDistanceScale", Getter = GetClothMaxDistanceScale, Setter = SetClothMaxDistanceScale)
        float mClothMaxDistanceScale{1.0f};

        /**
         * @brief 천 섹션을 그릴 때 시뮬레이션 위치를 섞는 비율입니다.
         */
        UPROPERTY(Edit, Serialize, Name = "ClothBlendWeight", Getter = GetClothBlendWeight, Setter = SetClothBlendWeight)
        float mClothBlendWeight{1.0f};

        /**
         * @brief 천 결과를 마지막으로 저장할 때 받은 개정이며 렌더러가 입자 버퍼를 다시 올릴지 판단합니다.
         */
        Core::FRevision mClothRevision{};

        /**
         * @brief 천에 부는 월드 좌표 바람 속도(m/s)입니다.
         */
        UPROPERTY(Edit, Serialize, Name = "ClothWindVelocity", Getter = GetClothWindVelocity, Setter = SetClothWindVelocity)
        FVector mClothWindVelocity{};

        /**
         * @brief true이면 월드의 바람장에서 이 컴포넌트 위치의 바람을 읽어 위의 ClothWindVelocity에 더합니다. 바람 원천이 없으면 영향이 없습니다.
         */
        UPROPERTY(Edit, Serialize, Name = "UseWorldWind")
        bool mUseWorldWind{true};

        /**
         * @brief 기준 뼈가 한 프레임에 이 거리(m)보다 멀리 움직이면 천을 순간 이동시키며 0이면 검사하지 않습니다.
         */
        Core::FLength mTeleportDistanceThreshold{Core::FLength::FromBase(3.0f)};

        /**
         * @brief 기준 뼈가 한 프레임에 이 각도(도)보다 많이 돌면 천을 순간 이동시키며 0이면 검사하지 않습니다.
         */
        Core::FAngle mTeleportRotationThreshold{};

        /**
         * @brief 순간 이동할 때 입자를 스키닝 위치에 다시 놓을지 나타냅니다.
         */
        bool mResetAfterTeleport{true};

        /**
         * @brief 다음 천 진행에 쓸 순간 이동 방식입니다.
         */
        PhysicsRuntime::EClothingTeleportMode mClothTeleportMode{PhysicsRuntime::EClothingTeleportMode::None};

        /**
         * @brief 지난 천 진행 때 기준 뼈의 월드 행렬이며 순간 이동을 판단할 때 씁니다.
         */
        FMatrix mPrevRootBoneMatrix{};

        /**
         * @brief 메시 천 에셋 번호를 키로 한 마지막 천 시뮬레이션 결과입니다.
         */
        Core::TMap<Index_t, PhysicsRuntime::FClothSimulData> mCurrentSimulationData{};

        // 메시 setter가 포즈를 초기화하므로 메시 경로를 복원한 뒤에 로드합니다.
    };

}
