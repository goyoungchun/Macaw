#pragma once

#include "Core/Math/FBox.h"
#include "Core/Math/FVector.h"
#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "CoreUObject/TObjectRef.h"
#include "Engine/Component/UPrimitiveComponent.h"
#include "Engine/FLandscapeRenderData.h"

#include <memory>

#include "ULandscapeComponent.generated.h"

struct FRay;

namespace Asset {
	class UStaticMesh;
	class UTexture2D;
}

namespace Engine {

	class ALandscape;

	// 지형 액터의 컴포넌트 격자 한 칸을 담당하는 프리미티브 컴포넌트
	UCLASS()
	class ULandscapeComponent final : public Engine::UPrimitiveComponent {
		GENERATED_BODY()

	public:
		ULandscapeComponent();
		~ULandscapeComponent() override;

	public:
		void RegisterComponent(Engine::UWorld* World) override;
		void UnregisterComponent() override;

		Engine::EPrimitiveType GetPrimitiveType() const override;
		Asset::UStaticMesh* GetStaticMesh() const override;
		const Asset::FMeshData& GetMeshData() const override;
		Engine::FPrimitiveSceneProxy CreateSceneProxy() const override;
		FBox GetWorldBounds() const override;
		bool ShouldCreateRenderState() const override;
		bool ShouldCreatePhysicsState() const override;

	public:
		// 소유 지형 액터의 컴포넌트 격자 좌표 지정
		void ConfigureSection(Uint32 ComponentIndexX, Uint32 ComponentIndexY, Uint32 SectionBaseX, Uint32 SectionBaseY);

		// 에셋 데이터로부터 서브섹션 경계와 웨이트맵 텍스처 갱신
		void RebuildComponentData();

		// 이 컴포넌트 영역에서 월드 광선 교차 검사
		bool RaycastComponent(const FRay& WorldRay, float& OutDistance, FVector& OutWorldNormal) const;

		// 이 컴포넌트 영역에서 월드 위치의 높이와 법선 조회
		bool SampleComponentHeightAndNormal(const FVector& WorldPosition, float& OutWorldZ, FVector& OutWorldNormal, bool SmoothNormal) const;

		// 컴포넌트 충돌 와이어프레임 수집
		void DrawHeightFieldCollision(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector4& Color, float Thickness) const;

		Engine::ALandscape* GetLandscapeActor() const;
		Uint32 GetComponentIndexX() const;
		Uint32 GetComponentIndexY() const;
		Uint32 GetSectionBaseX() const;
		Uint32 GetSectionBaseY() const;
		bool IsCollisionEnabled() const;
		void SetCollisionEnabled(bool Enabled);
		const Core::TArray<FBox>& GetLocalSubsectionBounds() const;

	protected:
		void OnCreatePhysicsState() override;

	private:
		FBox ComputeComponentLocalBox() const;

	private:
		UPROPERTY(Edit, Serialize, ReadOnly, Category = "Landscape")
		Uint32 mComponentIndexX{};

		UPROPERTY(Edit, Serialize, ReadOnly, Category = "Landscape")
		Uint32 mComponentIndexY{};

		UPROPERTY(Edit, Serialize, ReadOnly, Category = "Landscape")
		Uint32 mSectionBaseX{};

		UPROPERTY(Edit, Serialize, ReadOnly, Category = "Landscape")
		Uint32 mSectionBaseY{};

		UPROPERTY(Edit, Serialize, Category = "Collision", Getter = IsCollisionEnabled, Setter = SetCollisionEnabled)
		bool mCollisionEnabled{true};

		Core::TArray<FBox> mLocalSubsectionBounds{};
		FBox mLocalBounds{};
		Core::TArray<std::unique_ptr<Asset::UTexture2D>> mWeightmapTextures{};
		Core::TArray<Index_t> mActiveLayerIndices{};
	};

}
