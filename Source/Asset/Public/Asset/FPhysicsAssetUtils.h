/**
 * @file FPhysicsAssetUtils.h
 * @brief 스켈레탈 메시의 뼈와 정점으로 물리 에셋을 자동으로 만드는 함수를 선언합니다.
 * @ingroup Asset
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Core/Types.h"
#include "PhysicsRuntime/FConstraintInstance.h"

namespace PhysicsRuntime {
    class UBodySetup;
    class UPhysicsAsset;
}

namespace Asset {
    class USkeletalMesh;
    class USkeleton;

    /**
     * @brief 뼈마다 만들 충돌 모양의 종류입니다.
     * @ingroup Asset
     */
    enum class EPhysAssetFitGeomType : Uint8 {
        /**
         * @brief 박스입니다.
         */
        Box,

        /**
         * @brief 캡슐입니다.
         */
        Sphyl,

        /**
         * @brief 구입니다.
         */
        Sphere
    };

    /**
     * @brief 정점을 어느 뼈에 배정할지 정하는 방식입니다.
     * @ingroup Asset
     */
    enum class EPhysAssetFitVertWeight : Uint8 {
        /**
         * @brief 가중치가 조금이라도 있는 모든 뼈에 배정합니다.
         */
        AnyWeight,

        /**
         * @brief 가중치가 가장 큰 뼈 하나에만 배정합니다.
         */
        DominantWeight
    };

    /**
     * @brief 물리 에셋 자동 생성 옵션입니다.
     * @ingroup Asset
     */
    struct FPhysAssetCreateParams {
    public:
        /**
         * @brief 바디를 만들 최소 크기(m)입니다.
         * @note 이보다 작은 뼈의 정점은 부모 뼈에 합칩니다.
         */
        float mMinBoneSize{0.2f};

        /**
         * @brief 부모 뼈에 합칠 최소 크기(m)입니다.
         * @note 이보다 작은 뼈는 정점이 없다고 보고 합치지 않습니다.
         */
        float mMinWeldSize{0.0001f};

        /**
         * @brief 만들 충돌 모양의 종류입니다.
         */
        EPhysAssetFitGeomType mGeomType{EPhysAssetFitGeomType::Sphyl};

        /**
         * @brief 정점을 뼈에 배정하는 방식입니다.
         */
        EPhysAssetFitVertWeight mVertWeight{EPhysAssetFitVertWeight::DominantWeight};

        /**
         * @brief 부모 바디와 잇는 관절을 만들지 여부입니다.
         */
        bool mCreateConstraints{true};

        /**
         * @brief 크기와 관계없이 모든 뼈에 바디를 만들지 여부입니다.
         */
        bool mBodyForAll{false};

        /**
         * @brief 새 관절의 회전 3축에 적용할 움직임입니다.
         */
        PhysicsRuntime::EAngularConstraintMotion mAngularConstraintMode{PhysicsRuntime::EAngularConstraintMotion::Limited};
    };

    /**
     * @brief 한 뼈에 배정된 정점 위치 목록입니다.
     * @ingroup Asset
     */
    struct FBoneVertInfo {
    public:
        /**
         * @brief 뼈 기준 좌표로 바꾼 정점 위치입니다.
         */
        Core::TArray<FVector> mPositions{};
    };

    /**
     * @brief 스켈레탈 메시로 물리 에셋을 만들고 고치는 함수 모음입니다.
     * @ingroup Asset
     */
    namespace FPhysicsAssetUtils {
        /**
         * @brief 정점을 뼈별로 나누고 뼈 기준 좌표로 바꿉니다.
         * @param[in] Mesh 정점과 스킨 가중치를 가진 메시입니다.
         * @param[in] Skeleton 메시가 사용하는 스켈레톤입니다.
         * @param[in] VertWeight 정점을 뼈에 배정하는 방식입니다.
         * @param[out] Infos 뼈 순서대로 채운 정점 목록입니다.
         */
        void CalcBoneVertInfos(const USkeletalMesh& Mesh, const USkeleton& Skeleton, EPhysAssetFitVertWeight VertWeight, Core::TArray<FBoneVertInfo>& Infos);

        /**
         * @brief 메시와 스켈레톤으로 바디와 관절을 새로 채웁니다.
         * @param[in,out] PhysicsAsset 채울 물리 에셋이며, 기존 바디와 관절은 지웁니다.
         * @param[in] Mesh 정점과 스킨 가중치를 가진 메시입니다.
         * @param[in] Skeleton 메시가 사용하는 스켈레톤입니다.
         * @param[in] Params 생성 옵션입니다.
         * @return 바디를 하나 이상 만들었으면 true입니다.
         */
        bool CreateFromSkeletalMesh(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const USkeletalMesh& Mesh, const USkeleton& Skeleton, const FPhysAssetCreateParams& Params);

        /**
         * @brief 뼈의 정점을 감싸는 충돌 모양 하나를 설계도에 넣습니다.
         * @param[in,out] BodySetup 모양을 넣을 설계도이며, 기존 모양은 지웁니다.
         * @param[in] Skeleton 뼈의 바인드 포즈를 가진 스켈레톤입니다.
         * @param[in] BoneIndex 모양을 만들 뼈 번호입니다.
         * @param[in] Params 생성 옵션입니다.
         * @param[in] Info 그 뼈에 배정된 정점 목록입니다.
         * @return 모양을 넣었으면 true입니다.
         */
        bool CreateCollisionFromBone(PhysicsRuntime::UBodySetup& BodySetup, const USkeleton& Skeleton, Index_t BoneIndex, const FPhysAssetCreateParams& Params, const FBoneVertInfo& Info);

        /**
         * @brief 뼈 이름으로 빈 바디 설계도를 추가합니다.
         * @param[in,out] PhysicsAsset 설계도를 추가할 물리 에셋입니다.
         * @param[in] BodyName 바디를 붙일 뼈의 이름입니다.
         * @return 설계도 번호이며, 같은 이름이 있으면 그 번호입니다.
         */
        Index_t CreateNewBody(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Core::FString& BodyName);

        /**
         * @brief 바디 설계도 하나와 그 뼈를 쓰는 관절을 지웁니다.
         * @param[in,out] PhysicsAsset 설계도를 지울 물리 에셋입니다.
         * @param[in] BodyIndex 지울 설계도 번호입니다.
         */
        void DestroyBody(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, Index_t BodyIndex);

        /**
         * @brief 관절 이름으로 기본 관절 설정을 추가합니다.
         * @param[in,out] PhysicsAsset 관절을 추가할 물리 에셋입니다.
         * @param[in] ConstraintName 관절의 이름입니다.
         * @return 관절 번호이며, 같은 이름이 있으면 그 번호입니다.
         */
        Index_t CreateNewConstraint(PhysicsRuntime::UPhysicsAsset& PhysicsAsset, const Core::FString& ConstraintName);
    }
}
