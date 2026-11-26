/**
 * @file FPhysicsAssetEditing.h
 * @brief 물리 에셋 편집기가 바디 설명서의 충돌 형상을 찾고 추가하고 지우는 도우미를 정의합니다.
 * @ingroup Editor
 */

#pragma once

#include "Core/Math/FBox.h"
#include "Core/Math/FTransform.h"
#include "Core/Types.h"

#include <filesystem>
#include <memory>

namespace Asset {
    class FAssetManager;
    class USkeletalMesh;
    class USkeleton;
}

struct FRay;

namespace PhysicsRuntime {
    struct FBodyInstance;
    class FPhysicsScene;
    class UBodySetup;
    class UPhysicsAsset;
}

namespace Editor::PhysicsAssetEditing {

    /**
     * @brief 바디 설명서가 가진 충돌 형상 항목의 종류입니다.
     */
    enum class EShapeType : Uint8 {
        /**
         * @brief 선택한 항목이 없습니다.
         */
        None,

        /**
         * @brief 구 항목입니다.
         */
        Sphere,

        /**
         * @brief 상자 항목입니다.
         */
        Box,

        /**
         * @brief 캡슐(Sphyl) 항목입니다.
         */
        Sphyl,

        /**
         * @brief 볼록 다면체 항목입니다.
         */
        Convex
    };

    /**
     * @brief 바디 설명서 안의 항목 하나를 종류와 종류별 번호로 가리킵니다.
     */
    struct FShapeRef {
    public:

        /**
         * @brief 항목의 종류입니다.
         */
        EShapeType mType{EShapeType::None};

        /**
         * @brief 같은 종류 항목 사이에서의 번호입니다.
         */
        Index_t mIndex{InvalidIndex};

        /**
         * @brief 항목을 가리키고 있는지 반환합니다.
         * @return 종류와 번호가 설정되어 있으면 true입니다.
         */
        bool IsValid() const {
            return mType != EShapeType::None && mIndex != InvalidIndex;
        }
    };

    /**
     * @brief 지정한 종류의 항목 개수를 반환합니다.
     * @param[in] Setup 조회할 바디 설명서입니다.
     * @param[in] Type 개수를 셀 항목 종류입니다.
     * @return 해당 종류의 항목 개수입니다.
     */
    Index_t GetShapeCount(const PhysicsRuntime::UBodySetup& Setup, EShapeType Type);

    /**
     * @brief 모든 종류의 항목 개수를 합해 반환합니다.
     * @param[in] Setup 조회할 바디 설명서입니다.
     * @return 구, 상자, 캡슐, 볼록 항목의 개수 합입니다.
     */
    Index_t GetTotalShapeCount(const PhysicsRuntime::UBodySetup& Setup);

    /**
     * @brief PhysX Actor에 붙은 Shape의 순서(구, 상자, 캡슐, 볼록 순)에서 항목을 찾습니다.
     * @param[in] Setup 조회할 바디 설명서입니다.
     * @param[in] Ordinal Actor의 Shape 목록에서의 순서입니다.
     * @return 해당 항목이며 범위를 벗어나면 유효하지 않은 값입니다.
     */
    FShapeRef FindShapeByOrdinal(const PhysicsRuntime::UBodySetup& Setup, Index_t Ordinal);

    /**
     * @brief 지정한 종류의 기본 크기 항목을 바디 설명서에 추가합니다.
     * @param[in,out] Setup 항목을 추가할 바디 설명서입니다.
     * @param[in] Type 추가할 항목 종류이며 Sphere, Box, Sphyl만 지원합니다.
     * @return 새로 추가한 항목이며 지원하지 않는 종류면 유효하지 않은 값입니다.
     */
    FShapeRef AddShape(PhysicsRuntime::UBodySetup& Setup, EShapeType Type);

    /**
     * @brief 바디 설명서에서 항목 하나를 지웁니다.
     * @param[in,out] Setup 항목을 지울 바디 설명서입니다.
     * @param[in] Shape 지울 항목입니다.
     * @return 항목을 지웠으면 true입니다.
     */
    bool DeleteShape(PhysicsRuntime::UBodySetup& Setup, const FShapeRef& Shape);

    /**
     * @brief 뼈 하나에 바디를 추가하고 그 뼈의 정점을 감싸는 기본 캡슐을 만듭니다.
     * @param[in,out] PhysicsAsset 바디를 추가할 물리 에셋입니다.
     * @param[in] Mesh 정점과 스킨 가중치를 가진 메시입니다.
     * @param[in] Skeleton 메시가 사용하는 스켈레톤입니다.
     * @param[in] BoneIndex 바디를 붙일 뼈 번호입니다.
     * @return 만든 바디 설명서의 번호이며 실패하면 InvalidIndex입니다.
     */
    Index_t AddBodyForBone(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Asset::USkeletalMesh& Mesh, const Asset::USkeleton& Skeleton, Index_t BoneIndex);

    /**
     * @brief 바디가 있는 가장 가까운 조상 뼈를 찾아 자식 뼈와 그 조상을 잇는 관절 설정을 추가합니다.
     * @param[in,out] PhysicsAsset 관절을 추가할 물리 에셋입니다.
     * @param[in] Skeleton 뼈 계층과 기준 자세를 가진 스켈레톤입니다.
     * @param[in] BoneIndex 자식 뼈 번호이며 이 뼈에 바디가 있어야 합니다.
     * @return 새 관절 설정의 번호이며, 이미 있거나 바디가 있는 조상이 없으면 InvalidIndex입니다.
     */
    Index_t AddConstraintForBone(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Asset::USkeleton& Skeleton, Index_t BoneIndex);

    /**
     * @brief 관절 설정 하나를 물리 에셋에서 지웁니다.
     * @param[in,out] PhysicsAsset 관절을 지울 물리 에셋입니다.
     * @param[in] ConstraintIndex 지울 관절 설정의 번호입니다.
     * @return 지웠으면 true입니다.
     */
    bool DeleteConstraint(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, Index_t ConstraintIndex);

    /**
     * @brief 메시가 내장한 스켈레톤이나 같은 폴더의 "<메시 이름>_Skeleton" 에셋을 찾습니다.
     * @param[in,out] Assets 에셋 관리자입니다.
     * @param[in] MeshPath 스켈레탈 메시의 콘텐츠 경로입니다.
     * @param[out] OutSkeletonPath 스켈레톤 에셋을 찾았으면 그 콘텐츠 경로이고 내장 스켈레톤이면 비어 있습니다.
     * @return 찾은 스켈레톤의 비소유 주소이며 없으면 nullptr입니다.
     */
    Asset::USkeleton* FindSkeletonForMesh(Asset::FAssetManager& Assets, const std::filesystem::path& MeshPath, Core::FString& OutSkeletonPath);

    /**
     * @brief 메시 경로에서 기본 물리 에셋 경로("<메시 이름>_PhysicsAsset")를 만듭니다.
     * @param[in] MeshPath 스켈레탈 메시의 콘텐츠 경로입니다.
     * @return 같은 폴더의 물리 에셋 경로입니다.
     */
    std::filesystem::path GetDefaultPhysicsAssetPath(const std::filesystem::path& MeshPath);

    /**
     * @brief 기본 이름 규칙으로 물리 에셋 경로에서 미리보기 메시 경로를 되찾습니다.
     * @param[in] PhysicsAssetPath 물리 에셋의 콘텐츠 경로입니다.
     * @return 메시 경로이며 이름이 "_PhysicsAsset"으로 끝나지 않으면 비어 있습니다.
     */
    std::filesystem::path GetMeshPathForPhysicsAsset(const std::filesystem::path& PhysicsAssetPath);

    /**
     * @brief 스켈레탈 메시로 물리 에셋을 자동 생성해 기본 경로에 저장합니다.
     * @param[in,out] Assets 에셋 관리자입니다.
     * @param[in] MeshPath 스켈레탈 메시의 콘텐츠 경로입니다.
     * @param[out] OutPath 저장한 물리 에셋의 콘텐츠 경로입니다.
     * @param[out] Error 실패 원인입니다.
     * @return 파일을 만들었으면 true이며 같은 이름의 파일이 이미 있으면 덮어쓰지 않고 false입니다.
     */
    bool CreatePhysicsAssetFile(Asset::FAssetManager& Assets, const std::filesystem::path& MeshPath, std::filesystem::path& OutPath, Core::FString& Error);

    /**
     * @brief 시뮬레이션 중인 뼈 바디 하나의 현재 월드 자세입니다.
     */
    struct FBodyPose {
    public:

        /**
         * @brief 물리 에셋 안에서의 바디 설명서 번호입니다.
         */
        Index_t mBodyIndex{InvalidIndex};

        /**
         * @brief 바디의 현재 월드 변환이며 스케일은 바디를 만들 때 쓴 값입니다.
         */
        FTransform mWorld{};

        /**
         * @brief 바디의 현재 선속도(m/s)입니다.
         */
        FVector mLinearVelocity{};
    };

    /**
     * @brief 물리 장면의 뼈 바디를 모두 찾아 현재 월드 자세를 모읍니다.
     * @param[in] Scene 조회할 물리 장면입니다.
     * @param[out] OutPoses 뼈 바디의 번호와 현재 자세입니다.
     */
    void CollectBoneBodyPoses(PhysicsRuntime::FPhysicsScene& Scene, Core::TArray<FBodyPose>& OutPoses);

    /**
     * @brief 지정한 범위 아래에 시뮬레이션용 바닥 상자를 만들어 장면에 넣습니다.
     * @param[in,out] Scene 바닥을 넣을 물리 장면이며 바닥보다 오래 유지되어야 합니다.
     * @param[in] Bounds 바닥을 맞출 메시의 월드 범위입니다.
     * @return 만든 정적 바디이며 실패하면 nullptr입니다.
     */
    std::unique_ptr<PhysicsRuntime::FBodyInstance> CreateFloorBody(PhysicsRuntime::FPhysicsScene& Scene, const FBox& Bounds);

    /**
     * @brief 바닥 상자의 월드 변환과 크기를 반환합니다. 그리기에 사용합니다.
     * @param[in] Bounds 바닥을 맞출 메시의 월드 범위입니다.
     * @param[out] OutHalfExtent 바닥 상자의 반길이입니다.
     * @return 바닥 상자의 월드 위치만 담은 변환입니다.
     */
    FTransform GetFloorTransform(const FBox& Bounds, FVector& OutHalfExtent);

    /**
     * @brief 광선이 처음 맞는 PhysX 바디와 그 안의 Shape 순서를 찾습니다.
     * @param[in] Scene 광선을 쏠 물리 장면입니다.
     * @param[in] Ray 월드 공간의 정규화된 광선입니다.
     * @param[in] MaxDistance 광선이 나아갈 최대 거리(m)입니다.
     * @param[out] OutBodyIndex 맞은 바디의 물리 에셋 안 번호입니다.
     * @param[out] OutShapeOrdinal 맞은 Shape가 Actor의 Shape 목록에서 갖는 순서입니다.
     * @return 뼈 바디를 맞혔으면 true입니다.
     */
    bool PickBody(PhysicsRuntime::FPhysicsScene& Scene, const FRay& Ray, float MaxDistance, Index_t& OutBodyIndex, Index_t& OutShapeOrdinal);

    /**
     * @brief 물리 장면의 뼈 바디 중 현재 자세에서 모양이 서로 겹치는 바디 쌍을 모두 찾습니다.
     * @param[in] Scene 조회할 물리 장면입니다.
     * @param[out] OutPairs 겹치는 바디 번호의 쌍이며 각 쌍은 (작은 번호, 큰 번호)입니다.
     * @note 관절로 이어진 쌍도 겹치면 포함하므로 호출자가 걸러야 합니다.
     */
    void FindOverlappingBodyPairs(PhysicsRuntime::FPhysicsScene& Scene, Core::TArray<std::pair<Index_t, Index_t>>& OutPairs);

    /**
     * @brief 시뮬레이션 중인 바디를 마우스로 잡고 끄는 동작입니다.
     * @details 잡은 점과 마우스 광선 위의 목표점(잡을 때의 카메라 거리) 사이에 스프링-댐퍼 힘을 가합니다.
     *          PhysX 타입은 cpp에 숨깁니다.
     * @note 물리 장면이나 바디가 해제되기 전에 End를 호출해야 합니다. 그렇지 않으면 해제된 Actor를 참조합니다.
     */
    class FBodyGrab {
    public:

        /**
         * @brief 잡고 있는 바디가 없는 상태로 구성합니다.
         */
        FBodyGrab();

        /**
         * @brief 잡고 있던 바디를 놓고 정리합니다.
         */
        ~FBodyGrab();

        FBodyGrab(const FBodyGrab&) = delete;
        FBodyGrab& operator=(const FBodyGrab&) = delete;

        /**
         * @brief 광선이 처음 맞는 시뮬레이션 중인 뼈 바디를 잡습니다.
         * @param[in] Scene 광선을 쏠 물리 장면입니다.
         * @param[in] Ray 월드 공간의 정규화된 광선입니다.
         * @param[in] MaxDistance 광선이 나아갈 최대 거리(m)입니다.
         * @return Dynamic 뼈 바디를 잡았으면 true이며, 바닥이나 Kinematic 바디이면 false입니다.
         */
        bool Begin(PhysicsRuntime::FPhysicsScene& Scene, const FRay& Ray, float MaxDistance);

        /**
         * @brief 마우스 광선에 맞춰 목표점을 옮기고 잡은 점을 목표점으로 끄는 힘을 가합니다. 물리 스텝 전에 호출합니다.
         * @param[in] Ray 현재 마우스의 월드 광선입니다.
         */
        void Update(const FRay& Ray);

        /**
         * @brief 잡고 있던 바디를 놓습니다.
         */
        void End();

        /**
         * @brief 바디를 잡고 있는지 반환합니다.
         * @return 잡고 있으면 true입니다.
         */
        bool IsActive() const;

        /**
         * @brief 잡은 점과 목표점의 월드 위치를 반환합니다.
         * @param[out] OutGrabPoint 바디 위에서 잡은 점의 현재 월드 위치입니다.
         * @param[out] OutTarget 마우스가 가리키는 목표점입니다.
         * @return 잡고 있으면 true입니다.
         */
        bool GetEndpoints(FVector& OutGrabPoint, FVector& OutTarget) const;

    private:

        /**
         * @brief PhysX Actor와 잡은 위치를 담는 구현이며 정의는 cpp에 있습니다.
         */
        struct FImpl;

        /**
         * @brief 잡고 있는 바디의 상태이며 잡고 있지 않으면 비어 있습니다.
         */
        std::unique_ptr<FImpl> mImpl{};
    };

}
