#include "Engine/Component/ULandscapeComponent.h"
#include "Asset/FMeshData.h"
#include "Asset/ULandscapeAsset.h"
#include "Asset/UMaterial.h"
#include "Asset/UStaticMesh.h"
#include "Asset/UTexture2D.h"
#include "Core/Math.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FRay.h"
#include "Core/Math/FTransform.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Engine/ALandscape.h"
#include "Engine/EPrimitiveType.h"
#include "Engine/FDrawLine.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Engine/UWorld.h"
#include "PhysicsRuntime/FHeightFieldDesc.h"
#include "PhysicsRuntime/PhysicsConversion.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Engine {

	ULandscapeComponent::ULandscapeComponent() = default;

	ULandscapeComponent::~ULandscapeComponent() = default;

	void ULandscapeComponent::RegisterComponent(Engine::UWorld* World) {
		Engine::UPrimitiveComponent::RegisterComponent(World);

		if (Engine::ALandscape* Landscape{GetLandscapeActor()}) {
			Landscape->EnsureRuntimeResources();
			RebuildComponentData();
		}
	}

	void ULandscapeComponent::UnregisterComponent() {
		Engine::UPrimitiveComponent::UnregisterComponent();
	}

	Engine::EPrimitiveType ULandscapeComponent::GetPrimitiveType() const {
		return Engine::EPrimitiveType::Plane;
	}

	Asset::UStaticMesh* ULandscapeComponent::GetStaticMesh() const {
		const Engine::ALandscape* Landscape{GetLandscapeActor()};

		return Landscape != nullptr ? Landscape->GetSharedGridMesh() : nullptr;
	}

	const Asset::FMeshData& ULandscapeComponent::GetMeshData() const {
		static const Asset::FMeshData EmptyMesh{};

		return EmptyMesh;
	}

	Engine::FPrimitiveSceneProxy ULandscapeComponent::CreateSceneProxy() const {
		Engine::FPrimitiveSceneProxy Proxy{Engine::UPrimitiveComponent::CreateSceneProxy()};
		const Engine::ALandscape* Landscape{GetLandscapeActor()};

		if (Landscape == nullptr) {
			return Proxy;
		}

		const Asset::ULandscapeAsset* Asset{Landscape->GetActiveLandscapeAsset()};
		const std::shared_ptr<const Engine::FLandscapeSharedRenderData> Shared{Landscape->GetSharedRenderData()};

		if (Asset == nullptr || !Asset->IsValid() || Shared == nullptr) {
			return Proxy;
		}

		Proxy.mMesh = Landscape->GetSharedGridMesh();
		Proxy.mMaterial = Landscape->GetResolvedLandscapeMaterial();

		Engine::FLandscapeComponentRenderData RenderData{};
		RenderData.mShared = Shared;
		RenderData.mComponentIndexX = mComponentIndexX;
		RenderData.mComponentIndexY = mComponentIndexY;
		RenderData.mSectionBaseX = mSectionBaseX;
		RenderData.mSectionBaseY = mSectionBaseY;

		const FMatrix WorldMatrix{GetWorldMatrix()};
		RenderData.mLocalSubsectionBounds = mLocalSubsectionBounds;
		RenderData.mSubsectionBounds.reserve(mLocalSubsectionBounds.size());

		for (const FBox& LocalSubBox : mLocalSubsectionBounds) {
			RenderData.mSubsectionBounds.push_back(LocalSubBox.Transformed(WorldMatrix));
		}

		RenderData.mWeightmapTextures.reserve(mWeightmapTextures.size());

		for (const auto& Weightmap : mWeightmapTextures) {
			if (Weightmap != nullptr) {
				RenderData.mWeightmapTextures.push_back(CoreUObject::MakeObjectRef(Weightmap.get()));
			}
		}

		const Core::TArray<Asset::FLandscapeLayerInfo>& Layers{Asset->GetLayers()};
		RenderData.mLayerAllocations.reserve(mActiveLayerIndices.size());

		for (Index_t SlotIndex{}; SlotIndex < mActiveLayerIndices.size(); ++SlotIndex) {
			const Index_t LayerIndex{mActiveLayerIndices[SlotIndex]};

			if (LayerIndex >= Layers.size()) {
				continue;
			}

			const Asset::FLandscapeLayerInfo& Layer{Layers[LayerIndex]};
			Engine::FLandscapeLayerAllocation Allocation{};
			Allocation.mMaterial = Layer.mMaterial;
			Allocation.mUVScale = Layer.mUVScale;
			Allocation.mWeightmapIndex = static_cast<Uint8>(SlotIndex / Asset::ULandscapeAsset::LayersPerWeightmap);
			Allocation.mWeightmapChannel = static_cast<Uint8>(SlotIndex % Asset::ULandscapeAsset::LayersPerWeightmap);
			Allocation.mWeightBlended = Layer.mWeightBlended;
			RenderData.mLayerAllocations.push_back(Allocation);
		}

		Proxy.mLandscape = std::move(RenderData);

		return Proxy;
	}

	FBox ULandscapeComponent::GetWorldBounds() const {
		return mLocalBounds.Transformed(GetWorldMatrix());
	}

	bool ULandscapeComponent::ShouldCreateRenderState() const {
		const Engine::ALandscape* Landscape{GetLandscapeActor()};

		return Landscape != nullptr && Landscape->GetActiveLandscapeAsset() != nullptr && Landscape->GetActiveLandscapeAsset()->IsValid() && Landscape->GetSharedGridMesh() != nullptr;
	}

	bool ULandscapeComponent::ShouldCreatePhysicsState() const {
		if (!mCollisionEnabled) {
			return false;
		}

		const Engine::ALandscape* Landscape{GetLandscapeActor()};

		return Landscape != nullptr && Landscape->GetActiveLandscapeAsset() != nullptr && Landscape->GetActiveLandscapeAsset()->IsValid();
	}

	void ULandscapeComponent::ConfigureSection(Uint32 ComponentIndexX, Uint32 ComponentIndexY, Uint32 SectionBaseX, Uint32 SectionBaseY) {
		mComponentIndexX = ComponentIndexX;
		mComponentIndexY = ComponentIndexY;
		mSectionBaseX = SectionBaseX;
		mSectionBaseY = SectionBaseY;
	}

	void ULandscapeComponent::RebuildComponentData() {
		const Engine::ALandscape* Landscape{GetLandscapeActor()};
		const Asset::ULandscapeAsset* Asset{Landscape != nullptr ? Landscape->GetActiveLandscapeAsset() : nullptr};

		mLocalSubsectionBounds.clear();
		mWeightmapTextures.clear();
		mActiveLayerIndices.clear();
		mLocalBounds = {};

		if (Asset == nullptr || !Asset->IsValid()) {
			return;
		}

		if (mComponentIndexX >= Asset->GetComponentCountX() || mComponentIndexY >= Asset->GetComponentCountY()) {
			return;
		}

		const float QuadSize{Asset->GetQuadSize()};
		const FVector ComponentOriginOffset{static_cast<float>(mSectionBaseX) * QuadSize, static_cast<float>(mSectionBaseY) * QuadSize, 0.0f};
		const Uint32 NumSubsections{Asset->GetNumSubsections()};

		mLocalSubsectionBounds.reserve(static_cast<Index_t>(NumSubsections) * NumSubsections);

		for (Uint32 SubY{}; SubY < NumSubsections; ++SubY) {
			for (Uint32 SubX{}; SubX < NumSubsections; ++SubX) {
				FBox LocalSubBox{Asset->ComputeSubsectionLocalBounds(mComponentIndexX, mComponentIndexY, SubX, SubY)};
				LocalSubBox.mMinimum -= ComponentOriginOffset;
				LocalSubBox.mMaximum -= ComponentOriginOffset;
				mLocalSubsectionBounds.push_back(LocalSubBox);
			}
		}

		mLocalBounds = ComputeComponentLocalBox();
		Asset->BuildComponentWeightmapTextures(mComponentIndexX, mComponentIndexY, mWeightmapTextures, mActiveLayerIndices);

		MarkRenderStateDirty();
	}

	bool ULandscapeComponent::RaycastComponent(const FRay& WorldRay, float& OutDistance, FVector& OutWorldNormal) const {
		const Engine::ALandscape* Landscape{GetLandscapeActor()};
		const Asset::ULandscapeAsset* Asset{Landscape != nullptr ? Landscape->GetActiveLandscapeAsset() : nullptr};

		if (Asset == nullptr || !Asset->IsValid()) {
			return false;
		}

		float BoxHitDistance{};

		if (!GetWorldBounds().Intersects(WorldRay, BoxHitDistance) || BoxHitDistance >= OutDistance) {
			return false;
		}

		const FMatrix WorldMatrix{GetWorldMatrix()};
		FMatrix InverseMatrix{};

		if (!WorldMatrix.TryInverse(InverseMatrix)) {
			return false;
		}

		const FRay LocalRay{InverseMatrix.TransformPosition(WorldRay.mOrigin), InverseMatrix.TransformVector(WorldRay.mDirection)};
		const Uint32 CompQuads{Asset->GetComponentSizeQuads()};
		const float QuadSize{Asset->GetQuadSize()};
		const float Extent{static_cast<float>(CompQuads) * QuadSize};

		// 로컬 경계 상자와의 교차 구간 계산
		float TMin{0.0f};
		float TMax{OutDistance};

		const float MinCoords[3]{mLocalBounds.mMinimum.x, mLocalBounds.mMinimum.y, mLocalBounds.mMinimum.z};
		const float MaxCoords[3]{mLocalBounds.mMaximum.x, mLocalBounds.mMaximum.y, mLocalBounds.mMaximum.z};
		const float Origins[3]{LocalRay.mOrigin.x, LocalRay.mOrigin.y, LocalRay.mOrigin.z};
		const float Dirs[3]{LocalRay.mDirection.x, LocalRay.mDirection.y, LocalRay.mDirection.z};

		for (Index_t Axis{}; Axis < 3u; ++Axis) {
			if (std::abs(Dirs[Axis]) < 1e-6f) {
				if (Origins[Axis] < MinCoords[Axis] || Origins[Axis] > MaxCoords[Axis]) {
					return false;
				}
			} else {
				const float InvD{1.0f / Dirs[Axis]};
				float T0{(MinCoords[Axis] - Origins[Axis]) * InvD};
				float T1{(MaxCoords[Axis] - Origins[Axis]) * InvD};

				if (T0 > T1) {
					std::swap(T0, T1);
				}

				TMin = std::max(TMin, T0);
				TMax = std::min(TMax, T1);

				if (TMin > TMax) {
					return false;
				}
			}
		}

		const FVector StartPoint{LocalRay.mOrigin + LocalRay.mDirection * TMin};
		const float StartGridX{std::clamp(StartPoint.x / QuadSize, 0.0f, static_cast<float>(CompQuads) - 0.0001f)};
		const float StartGridY{std::clamp(StartPoint.y / QuadSize, 0.0f, static_cast<float>(CompQuads) - 0.0001f)};

		Int32 CellX{static_cast<Int32>(std::floor(StartGridX))};
		Int32 CellY{static_cast<Int32>(std::floor(StartGridY))};
		CellX = std::clamp(CellX, 0, static_cast<Int32>(CompQuads) - 1);
		CellY = std::clamp(CellY, 0, static_cast<Int32>(CompQuads) - 1);

		const float DirGridX{LocalRay.mDirection.x / QuadSize};
		const float DirGridY{LocalRay.mDirection.y / QuadSize};
		const Int32 StepX{DirGridX > 0.0f ? 1 : (DirGridX < 0.0f ? -1 : 0)};
		const Int32 StepY{DirGridY > 0.0f ? 1 : (DirGridY < 0.0f ? -1 : 0)};

		const float TDeltaX{StepX != 0 ? std::abs(1.0f / DirGridX) : std::numeric_limits<float>::max()};
		const float TDeltaY{StepY != 0 ? std::abs(1.0f / DirGridY) : std::numeric_limits<float>::max()};

		float TMaxX{std::numeric_limits<float>::max()};
		float TMaxY{std::numeric_limits<float>::max()};

		if (StepX > 0) {
			TMaxX = TMin + (static_cast<float>(CellX + 1) - StartGridX) / DirGridX;
		} else if (StepX < 0) {
			TMaxX = TMin + (static_cast<float>(CellX) - StartGridX) / DirGridX;
		}

		if (StepY > 0) {
			TMaxY = TMin + (static_cast<float>(CellY + 1) - StartGridY) / DirGridY;
		} else if (StepY < 0) {
			TMaxY = TMin + (static_cast<float>(CellY) - StartGridY) / DirGridY;
		}

		const Uint32 MaxSteps{CompQuads * 2u + 4u};
		bool Found{};
		float ClosestDistance{OutDistance};
		FVector ClosestLocalNormal{0.0f, 0.0f, 1.0f};

		// 격자 순회로 교차 삼각형 탐색
		for (Uint32 Step{}; Step < MaxSteps; ++Step) {
			if (CellX < 0 || CellY < 0 || CellX >= static_cast<Int32>(CompQuads) || CellY >= static_cast<Int32>(CompQuads)) {
				break;
			}

			const Uint32 GlobalQuadX{mSectionBaseX + static_cast<Uint32>(CellX)};
			const Uint32 GlobalQuadY{mSectionBaseY + static_cast<Uint32>(CellY)};

			if (!Asset->IsQuadHole(GlobalQuadX, GlobalQuadY)) {
				const float X0{static_cast<float>(CellX) * QuadSize};
				const float Y0{static_cast<float>(CellY) * QuadSize};
				const float X1{std::min(X0 + QuadSize, Extent)};
				const float Y1{std::min(Y0 + QuadSize, Extent)};

				const FVector V00{X0, Y0, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(GlobalQuadX, GlobalQuadY))};
				const FVector V10{X1, Y0, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(GlobalQuadX + 1u, GlobalQuadY))};
				const FVector V01{X0, Y1, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(GlobalQuadX, GlobalQuadY + 1u))};
				const FVector V11{X1, Y1, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(GlobalQuadX + 1u, GlobalQuadY + 1u))};

				float HitDistance{};

				if (Core::Math::IntersectTriangle(LocalRay, V00, V10, V11, HitDistance) && HitDistance < ClosestDistance) {
					ClosestDistance = HitDistance;
					ClosestLocalNormal = (V10 - V00).Cross(V11 - V00).Normalized();
					Found = true;
				}

				if (Core::Math::IntersectTriangle(LocalRay, V00, V11, V01, HitDistance) && HitDistance < ClosestDistance) {
					ClosestDistance = HitDistance;
					ClosestLocalNormal = (V11 - V00).Cross(V01 - V00).Normalized();
					Found = true;
				}

				if (Found) {
					break;
				}
			}

			const float NextT{std::min(TMaxX, TMaxY)};

			if (NextT > TMax || NextT >= ClosestDistance) {
				break;
			}

			if (TMaxX < TMaxY) {
				CellX += StepX;
				TMaxX += TDeltaX;
			} else {
				CellY += StepY;
				TMaxY += TDeltaY;
			}
		}

		if (!Found) {
			return false;
		}

		OutDistance = ClosestDistance;
		OutWorldNormal = InverseMatrix.Transposed().TransformVector(ClosestLocalNormal).Normalized();

		return true;
	}

	bool ULandscapeComponent::SampleComponentHeightAndNormal(const FVector& WorldPosition, float& OutWorldZ, FVector& OutWorldNormal, bool SmoothNormal) const {
		const Engine::ALandscape* Landscape{GetLandscapeActor()};
		const Asset::ULandscapeAsset* Asset{Landscape != nullptr ? Landscape->GetActiveLandscapeAsset() : nullptr};

		if (Asset == nullptr || !Asset->IsValid()) {
			return false;
		}

		const FMatrix WorldMatrix{GetWorldMatrix()};
		FMatrix InverseMatrix{};

		if (!WorldMatrix.TryInverse(InverseMatrix)) {
			return false;
		}

		const FVector LocalPos{InverseMatrix.TransformPosition(WorldPosition)};
		const float QuadSize{Asset->GetQuadSize()};
		const float Extent{static_cast<float>(Asset->GetComponentSizeQuads()) * QuadSize};

		if (LocalPos.x < 0.0f || LocalPos.y < 0.0f || LocalPos.x > Extent || LocalPos.y > Extent) {
			return false;
		}

		const float GlobalLocalX{static_cast<float>(mSectionBaseX) * QuadSize + LocalPos.x};
		const float GlobalLocalY{static_cast<float>(mSectionBaseY) * QuadSize + LocalPos.y};
		float LocalZ{};
		FVector LocalNormal{};

		if (!Asset->SampleLocalHeightAndNormal(GlobalLocalX, GlobalLocalY, LocalZ, LocalNormal, SmoothNormal)) {
			return false;
		}

		const FVector SurfaceWorld{WorldMatrix.TransformPosition({LocalPos.x, LocalPos.y, LocalZ})};
		OutWorldZ = SurfaceWorld.z;
		OutWorldNormal = InverseMatrix.Transposed().TransformVector(LocalNormal).Normalized();

		return true;
	}

	void ULandscapeComponent::DrawHeightFieldCollision(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector4& Color, float Thickness) const {
		const Engine::ALandscape* Landscape{GetLandscapeActor()};
		const Asset::ULandscapeAsset* Asset{Landscape != nullptr ? Landscape->GetActiveLandscapeAsset() : nullptr};

		if (Asset == nullptr || !Asset->IsValid()) {
			return;
		}

		const FMatrix WorldMatrix{GetWorldMatrix()};
		const Uint32 CompQuads{Asset->GetComponentSizeQuads()};
		const float QuadSize{Asset->GetQuadSize()};
		const Uint32 Step{std::max<Uint32>(1u, CompQuads / 16u)};

		const FVector LineColor{Color.x, Color.y, Color.z};

		for (Uint32 Y{}; Y <= CompQuads; Y += Step) {
			for (Uint32 X{}; X < CompQuads; X += Step) {
				const Uint32 NextX{std::min(X + Step, CompQuads)};
				const FVector A{WorldMatrix.TransformPosition({static_cast<float>(X) * QuadSize, static_cast<float>(Y) * QuadSize, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(mSectionBaseX + X, mSectionBaseY + Y))})};
				const FVector B{WorldMatrix.TransformPosition({static_cast<float>(NextX) * QuadSize, static_cast<float>(Y) * QuadSize, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(mSectionBaseX + NextX, mSectionBaseY + Y))})};
				DrawInterface.DrawLine(A, B, LineColor, true, Thickness, Color.w);
			}
		}

		for (Uint32 X{}; X <= CompQuads; X += Step) {
			for (Uint32 Y{}; Y < CompQuads; Y += Step) {
				const Uint32 NextY{std::min(Y + Step, CompQuads)};
				const FVector A{WorldMatrix.TransformPosition({static_cast<float>(X) * QuadSize, static_cast<float>(Y) * QuadSize, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(mSectionBaseX + X, mSectionBaseY + Y))})};
				const FVector B{WorldMatrix.TransformPosition({static_cast<float>(X) * QuadSize, static_cast<float>(NextY) * QuadSize, Asset->HeightSampleToLocalZ(Asset->GetHeightSample(mSectionBaseX + X, mSectionBaseY + NextY))})};
				DrawInterface.DrawLine(A, B, LineColor, true, Thickness, Color.w);
			}
		}
	}

	Engine::ALandscape* ULandscapeComponent::GetLandscapeActor() const {
		return CoreUObject::Cast<Engine::ALandscape>(GetOwner());
	}

	Uint32 ULandscapeComponent::GetComponentIndexX() const {
		return mComponentIndexX;
	}

	Uint32 ULandscapeComponent::GetComponentIndexY() const {
		return mComponentIndexY;
	}

	Uint32 ULandscapeComponent::GetSectionBaseX() const {
		return mSectionBaseX;
	}

	Uint32 ULandscapeComponent::GetSectionBaseY() const {
		return mSectionBaseY;
	}

	bool ULandscapeComponent::IsCollisionEnabled() const {
		return mCollisionEnabled;
	}

	void ULandscapeComponent::SetCollisionEnabled(bool Enabled) {
		if (mCollisionEnabled != Enabled) {
			mCollisionEnabled = Enabled;
			RecreatePhysicsState();
		}
	}

	const Core::TArray<FBox>& ULandscapeComponent::GetLocalSubsectionBounds() const {
		return mLocalSubsectionBounds;
	}

	void ULandscapeComponent::OnCreatePhysicsState() {
		if (!mCollisionEnabled) {
			return;
		}

		const Engine::ALandscape* Landscape{GetLandscapeActor()};
		const Asset::ULandscapeAsset* Asset{Landscape != nullptr ? Landscape->GetActiveLandscapeAsset() : nullptr};

		if (Asset == nullptr || !Asset->IsValid()) {
			return;
		}

		const Uint32 CompQuads{Asset->GetComponentSizeQuads()};
		const Uint32 CompRes{CompQuads + 1u};
		const float QuadSize{Asset->GetQuadSize()};
		const FVector Scale{GetTransform().mScale};

		const float ScaleX{std::max(std::abs(Scale.x), 0.001f)};
		const float ScaleY{std::max(std::abs(Scale.y), 0.001f)};
		const float ScaleZ{std::max(std::abs(Scale.z), 0.001f)};

		PhysicsRuntime::FHeightFieldDesc Desc{};
		Desc.mRows = CompRes;
		Desc.mColumns = CompRes;
		Desc.mRowScale = QuadSize * ScaleX;
		Desc.mColumnScale = QuadSize * ScaleY;
		Desc.mHeightScale = (Asset->GetHeightScale() * ScaleZ) / 65535.0f;
		Desc.mLocalPosition = FVector{0.0f, static_cast<float>(CompQuads) * Desc.mColumnScale, 0.5f * Desc.mHeightScale};
		Desc.mLocalRotation = PhysicsRuntime::ToFQuat(PhysicsRuntime::GetHeightFieldZUpBasis());
		Desc.mDrivableSurface = true;

		const Core::TArray<Asset::FLandscapeLayerInfo>& Layers{Asset->GetLayers()};
		Desc.mMaterials.reserve(std::max<Index_t>(1u, Layers.size()));

		if (Layers.empty()) {
			Desc.mMaterials.push_back({0.5f, 0.5f, 0.2f});
		} else {
			for (const Asset::FLandscapeLayerInfo& Layer : Layers) {
				Desc.mMaterials.push_back({Layer.mStaticFriction, Layer.mDynamicFriction, Layer.mRestitution});
			}
		}

		const Uint32 MaterialCount{static_cast<Uint32>(Desc.mMaterials.size())};
		Desc.mSamples.resize(static_cast<Index_t>(CompRes) * CompRes);

		// 엔진 상향 축과 PhysX 높이 축을 정렬하고 공통 대각선 규약 유지
		for (Uint32 Row{}; Row < CompRes; ++Row) {
			for (Uint32 Col{}; Col < CompRes; ++Col) {
				const Uint32 LocalX{Row};
				const Uint32 LocalY{CompQuads - Col};
				const Uint32 GlobalX{mSectionBaseX + LocalX};
				const Uint32 GlobalY{mSectionBaseY + LocalY};
				const std::uint16_t HeightU16{Asset->GetHeightSample(GlobalX, GlobalY)};

				physx::PxHeightFieldSample Sample{};
				Sample.height = static_cast<physx::PxI16>(static_cast<Int32>(HeightU16) - static_cast<Int32>(Asset::ULandscapeAsset::MidHeightValue));
				Sample.clearTessFlag();

				if (Row < CompQuads && Col < CompQuads) {
					const Uint32 QuadX{mSectionBaseX + Row};
					const Uint32 QuadY{mSectionBaseY + (CompQuads - 1u - Col)};

					if (Asset->IsQuadHole(QuadX, QuadY)) {
						Sample.materialIndex0 = physx::PxHeightFieldMaterial::eHOLE;
						Sample.materialIndex1 = physx::PxHeightFieldMaterial::eHOLE;
					} else {
						const Index_t DominantLayer{Asset->GetDominantLayerAtQuad(QuadX, QuadY)};
						const physx::PxU8 MaterialSlot{DominantLayer != InvalidIndex && DominantLayer < MaterialCount ? static_cast<physx::PxU8>(DominantLayer) : static_cast<physx::PxU8>(0u)};
						Sample.materialIndex0 = MaterialSlot;
						Sample.materialIndex1 = MaterialSlot;
					}
				} else {
					Sample.materialIndex0 = 0u;
					Sample.materialIndex1 = 0u;
				}

				Desc.mSamples[static_cast<Index_t>(Row) * CompRes + Col] = Sample;
			}
		}

		InitHeightFieldPhysicsState(Desc);
	}

	FBox ULandscapeComponent::ComputeComponentLocalBox() const {
		if (mLocalSubsectionBounds.empty()) {
			return {};
		}

		FBox Combined{mLocalSubsectionBounds.front()};

		for (Index_t Index{1u}; Index < mLocalSubsectionBounds.size(); ++Index) {
			Combined.mMinimum = Combined.mMinimum.Min(mLocalSubsectionBounds[Index].mMinimum);
			Combined.mMaximum = Combined.mMaximum.Max(mLocalSubsectionBounds[Index].mMaximum);
		}

		return Combined;
	}

}
