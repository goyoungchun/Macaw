#pragma once

#include "Core/Math/FVector2.h"
#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "CoreUObject/TObjectRef.h"

#include "FLandscapeLayerInfo.generated.h"

namespace Asset {

	class UMaterial;

	// 지형 레이어 하나의 머티리얼과 물리 속성
	USTRUCT()
	struct FLandscapeLayerInfo {
		GENERATED_BODY()

	public:
		// 레이어 식별 이름
		UPROPERTY(Edit, Serialize, Category = "Layer")
		Core::FString mLayerName{"Layer"};

		// 레이어가 참조할 머티리얼 에셋 경로
		UPROPERTY(Edit, Serialize, Category = "Layer", Meta = (AssetType = "Material"))
		Core::FString mMaterialPath{};

		// 레이어가 참조할 웨이트맵 텍스처 에셋 경로
		UPROPERTY(Edit, Serialize, Category = "Layer", Meta = (AssetType = "Texture"))
		Core::FString mWeightmapTexturePath{};

		// 월드 미터당 텍스처 반복 배율
		UPROPERTY(Edit, Serialize, Category = "Layer")
		FVector2 mUVScale{0.25f, 0.25f};

		// 가중치 정규화 블렌딩 참여 여부
		UPROPERTY(Edit, Serialize, Category = "Layer")
		bool mWeightBlended{true};

		// 정지 마찰 계수
		UPROPERTY(Edit, Serialize, Category = "Physics")
		float mStaticFriction{0.5f};

		// 운동 마찰 계수
		UPROPERTY(Edit, Serialize, Category = "Physics")
		float mDynamicFriction{0.5f};

		// 반발 계수
		UPROPERTY(Edit, Serialize, Category = "Physics")
		float mRestitution{0.2f};

		// 런타임 해석된 머티리얼 참조
		CoreUObject::TObjectRef<Asset::UMaterial> mMaterial{};
	};

}
