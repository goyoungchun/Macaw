#include "Engine/ALandscape.h"
#include "Asset/FAssetManager.h"
#include "Asset/ULandscapeAsset.h"
#include "Asset/UMaterial.h"
#include "Asset/UStaticMesh.h"
#include "Asset/UTexture2D.h"
#include "Core/FilePath.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FRay.h"
#include "Core/Math/FTransform.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/Component/ULandscapeComponent.h"
#include "Engine/Component/USceneComponent.h"
#include "Engine/UWorld.h"
#include "RenderCore/Shader/UShader.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace Engine {

	namespace {

		// 입력 경로의 앞뒤 공백과 큰따옴표 정리
		std::filesystem::path CleanInputPath(const std::filesystem::path& RawPath) {
			Core::FString Text{Core::FilePath::ToUtf8(RawPath)};

			while (!Text.empty() && (Text.front() == ' ' || Text.front() == '\t' || Text.front() == '"' || Text.front() == '\'')) {
				Text.erase(Text.begin());
			}

			while (!Text.empty() && (Text.back() == ' ' || Text.back() == '\t' || Text.back() == '"' || Text.back() == '\'')) {
				Text.pop_back();
			}

			return Core::FilePath::FromUtf8(Text);
		}

		// 외부 이미지 또는 에셋 경로를 프로젝트 내부 텍스처 에셋 경로로 통일
		bool ConvertOrResolveTextureAssetPath(Asset::FAssetManager& Assets, const std::filesystem::path& SourcePath, bool PreserveHighPrecision, Core::FString& OutRelativeAssetPath, Core::FString& Error) {
			OutRelativeAssetPath.clear();
			Error.clear();

			const std::filesystem::path Cleaned{CleanInputPath(SourcePath)};

			if (Cleaned.empty()) {
				Error = "Source path is empty";

				return false;
			}

			const std::wstring Extension{Core::FilePath::ToLowerExtension(Cleaned)};

			if (Extension == L".masset") {
				const std::filesystem::path Resolved{Assets.ResolvePath(Cleaned, Error)};

				if (Resolved.empty()) {
					return false;
				}

				OutRelativeAssetPath = Assets.GetRelativePath(Resolved, Error);

				return !OutRelativeAssetPath.empty();
			}

			std::unique_ptr<Asset::UTexture2D> ConvertedTexture{CoreUObject::NewObject<Asset::UTexture2D>()};

			if (ConvertedTexture == nullptr || !Asset::ULandscapeAsset::ConvertSourceImageToTextureAsset(Cleaned, PreserveHighPrecision, *ConvertedTexture, Error)) {
				return false;
			}

			const std::filesystem::path SubDir{std::filesystem::path{"Textures"} / "Landscape"};
			const std::filesystem::path TargetFolder{Assets.GetContentRoot() / SubDir};
			std::error_code DirError{};
			std::filesystem::create_directories(TargetFolder, DirError);

			if (DirError) {
				Error = "Cannot create landscape texture directory";

				return false;
			}

			Core::FString BaseName{Core::FilePath::ToUtf8(Cleaned.stem())};

			if (BaseName.empty()) {
				BaseName = PreserveHighPrecision ? "Heightmap" : "Weightmap";
			}

			ConvertedTexture->SetName(BaseName);

			const std::filesystem::path UniquePath{Assets.MakeUniqueAssetPath(SubDir, BaseName, Error)};

			if (UniquePath.empty() || !Assets.SaveTexture(UniquePath, *ConvertedTexture, Error)) {
				return false;
			}

			OutRelativeAssetPath = Assets.GetRelativePath(UniquePath, Error);

			return !OutRelativeAssetPath.empty();
		}

	}

	ALandscape::ALandscape() = default;

	ALandscape::~ALandscape() = default;

	bool ALandscape::InitializeDefaultComponents() {
		if (!GetComponents().empty()) {
			return false;
		}

		std::unique_ptr<Engine::USceneComponent> Root{CoreUObject::NewObject<Engine::USceneComponent>()};
		Root->SetName("LandscapeRoot");

		if (!AddComponent(std::move(Root))) {
			return false;
		}

		if (mLayers.empty()) {
			Asset::FLandscapeLayerInfo BaseLayer{};
			BaseLayer.mLayerName = "Base";
			BaseLayer.mUVScale = {0.25f, 0.25f};
			mLayers.push_back(BaseLayer);
		}

		mOwnedAsset = CoreUObject::NewObject<Asset::ULandscapeAsset>();
		mOwnedAsset->SetName("LandscapeData");
		mOwnedAsset->CreateFlat(mSubsectionSizeQuads, mNumSubsections, mComponentCountX, mComponentCountY, mQuadSize, mHeightScale);
		SyncLayersToAsset(*mOwnedAsset);
		mOwnedAsset->GenerateProceduralHills(1337u, 10.0f, 0.025f);

		EnsureRuntimeResources();
		RebuildComponentGrid();

		return true;
	}

	void ALandscape::EnsureRuntimeResources() {
		if (mDefaultLandscapeMaterial == nullptr) {
			mDefaultLandscapeMaterial = CoreUObject::NewObject<Asset::UMaterial>();
			mDefaultLandscapeMaterial->SetName("DefaultLandscapeMaterial");
			mDefaultLandscapeMaterial->SetShader(RenderCore::UShader::GetLandscape());
			mDefaultLandscapeMaterial->SetBaseColor({0.36f, 0.52f, 0.30f, 1.0f});
			mDefaultLandscapeMaterial->SetSpecularStrength(0.15f);
			mDefaultLandscapeMaterial->SetShininess(16.0f);
		}

		if (!mLandscapeAssetPath.empty() && mExternalAsset.Get() == nullptr && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr) {
			Core::FString Error{};
			Asset::ULandscapeAsset* Loaded{GetWorld()->GetAssets()->LoadLandscape(Core::FilePath::FromUtf8(mLandscapeAssetPath), Error)};

			if (Loaded != nullptr) {
				mExternalAsset = Loaded;
				SyncParametersFromAsset(*Loaded);
			}
		}

		if (mExternalAsset.Get() == nullptr && mOwnedAsset == nullptr) {
			mOwnedAsset = CoreUObject::NewObject<Asset::ULandscapeAsset>();
			mOwnedAsset->SetName("LandscapeData");
			mOwnedAsset->CreateFlat(mSubsectionSizeQuads, mNumSubsections, mComponentCountX, mComponentCountY, mQuadSize, mHeightScale);
			SyncLayersToAsset(*mOwnedAsset);

			if (!mHeightmapTexturePath.empty() && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr) {
				Core::FString Error{};

				if (Asset::UTexture2D* Texture{GetWorld()->GetAssets()->LoadTexture(Core::FilePath::FromUtf8(mHeightmapTexturePath), Error)}) {
					mOwnedAsset->ApplyHeightmapFromTexture(*Texture, Error);
				}
			}

			ReapplyAllLayerWeightmaps(*mOwnedAsset);
		}

		ResolveMaterials();

		const Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset != nullptr && ActiveAsset->IsValid()) {
			if (mSharedGrid.mMesh == nullptr || mSharedGrid.mSubsectionSizeQuads != ActiveAsset->GetSubsectionSizeQuads() || mPackedHeightmap == nullptr || mSharedRenderData == nullptr || mBuiltDataRevision != ActiveAsset->GetDataRevision()) {
				RebuildSharedRenderResources();
			}
		}
	}

	bool ALandscape::RebuildLandscape() {
		EnsureRuntimeResources();

		Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr) {
			return false;
		}

		const bool TopologyChanged{
			ActiveAsset->GetSubsectionSizeQuads() != mSubsectionSizeQuads ||
			ActiveAsset->GetNumSubsections() != mNumSubsections ||
			ActiveAsset->GetComponentCountX() != mComponentCountX ||
			ActiveAsset->GetComponentCountY() != mComponentCountY};

		if (TopologyChanged) {
			if (!ActiveAsset->CreateFlat(mSubsectionSizeQuads, mNumSubsections, mComponentCountX, mComponentCountY, mQuadSize, mHeightScale)) {
				return false;
			}

			if (!mHeightmapTexturePath.empty() && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr) {
				Core::FString Error{};

				if (Asset::UTexture2D* Texture{GetWorld()->GetAssets()->LoadTexture(Core::FilePath::FromUtf8(mHeightmapTexturePath), Error)}) {
					ActiveAsset->ApplyHeightmapFromTexture(*Texture, Error);
				}
			}
		} else {
			ActiveAsset->SetQuadSize(mQuadSize);
			ActiveAsset->SetHeightScale(mHeightScale);
		}

		SyncLayersToAsset(*ActiveAsset);

		if (TopologyChanged) {
			ReapplyAllLayerWeightmaps(*ActiveAsset);
		}

		ResolveMaterials();
		RebuildSharedRenderResources();
		RebuildComponentGrid();

		return true;
	}

	bool ALandscape::GenerateProceduralHills(Uint32 Seed, float Amplitude, float Frequency) {
		EnsureRuntimeResources();
		Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr || !ActiveAsset->GenerateProceduralHills(Seed, Amplitude, Frequency)) {
			return false;
		}

		RebuildSharedRenderResources();
		RefreshExistingComponents();

		return true;
	}

	bool ALandscape::ImportHeightmapFile(const std::filesystem::path& SourcePath, Core::FString& Error) {
		EnsureRuntimeResources();

		if (GetWorld() == nullptr || GetWorld()->GetAssets() == nullptr) {
			Error = "No asset manager available";

			return false;
		}

		Core::FString TextureAssetPath{};

		if (!ConvertOrResolveTextureAssetPath(*GetWorld()->GetAssets(), SourcePath, true, TextureAssetPath, Error)) {
			return false;
		}

		return ApplyHeightmapTextureAsset(TextureAssetPath, Error);
	}

	bool ALandscape::ApplyHeightmapTextureAsset(const Core::FString& TextureAssetPath, Core::FString& Error) {
		Error.clear();
		EnsureRuntimeResources();
		Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr) {
			Error = "No active landscape data";

			return false;
		}

		if (TextureAssetPath.empty()) {
			mHeightmapTexturePath.clear();

			return true;
		}

		if (GetWorld() == nullptr || GetWorld()->GetAssets() == nullptr) {
			Error = "No asset manager available";

			return false;
		}

		Asset::UTexture2D* Texture{GetWorld()->GetAssets()->LoadTexture(Core::FilePath::FromUtf8(TextureAssetPath), Error)};

		if (Texture == nullptr) {
			return false;
		}

		if (!ActiveAsset->ApplyHeightmapFromTexture(*Texture, Error)) {
			return false;
		}

		mHeightmapTexturePath = TextureAssetPath;
		RebuildSharedRenderResources();
		RefreshExistingComponents();

		return true;
	}

	bool ALandscape::ImportWeightmapFile(Index_t LayerIndex, const std::filesystem::path& SourcePath, Core::FString& Error) {
		EnsureRuntimeResources();

		if (GetWorld() == nullptr || GetWorld()->GetAssets() == nullptr) {
			Error = "No asset manager available";

			return false;
		}

		Core::FString TextureAssetPath{};

		if (!ConvertOrResolveTextureAssetPath(*GetWorld()->GetAssets(), SourcePath, false, TextureAssetPath, Error)) {
			return false;
		}

		return ApplyWeightmapTextureAsset(LayerIndex, TextureAssetPath, Error);
	}

	bool ALandscape::ApplyWeightmapTextureAsset(Index_t LayerIndex, const Core::FString& TextureAssetPath, Core::FString& Error) {
		Error.clear();
		EnsureRuntimeResources();
		Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr) {
			Error = "No active landscape data";

			return false;
		}

		if (LayerIndex >= ActiveAsset->GetLayers().size()) {
			Error = "Invalid layer index";

			return false;
		}

		if (TextureAssetPath.empty()) {
			ActiveAsset->GetMutableLayers()[LayerIndex].mWeightmapTexturePath.clear();
			ActiveAsset->FillLayerWeights(LayerIndex, 0u);
			ReapplyAllLayerWeightmaps(*ActiveAsset);
			ActiveAsset->NormalizeWeightBlendedLayers();
			ActiveAsset->MarkDataDirty();
			mLayers = ActiveAsset->GetLayers();
			RebuildSharedRenderResources();
			RefreshExistingComponents();

			return true;
		}

		if (GetWorld() == nullptr || GetWorld()->GetAssets() == nullptr) {
			Error = "No asset manager available";

			return false;
		}

		Asset::UTexture2D* Texture{GetWorld()->GetAssets()->LoadTexture(Core::FilePath::FromUtf8(TextureAssetPath), Error)};

		if (Texture == nullptr) {
			return false;
		}

		// 요청한 텍스처를 먼저 검증한 뒤 전체 레이어 웨이트맵을 일괄 재적용
		if (!ActiveAsset->SampleWeightmapTextureToLayerRaw(LayerIndex, *Texture, Error)) {
			return false;
		}

		ActiveAsset->GetMutableLayers()[LayerIndex].mWeightmapTexturePath = TextureAssetPath;
		ReapplyAllLayerWeightmaps(*ActiveAsset);
		mLayers = ActiveAsset->GetLayers();
		RebuildSharedRenderResources();
		RefreshExistingComponents();

		return true;
	}

	bool ALandscape::SaveLandscapeAsset(const std::filesystem::path& TargetPath, Core::FString& Error) {
		if (GetWorld() == nullptr || GetWorld()->GetAssets() == nullptr) {
			Error = "No asset manager available";

			return false;
		}

		Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr || !ActiveAsset->IsValid()) {
			Error = "Invalid landscape asset";

			return false;
		}

		Asset::FAssetManager* Assets{GetWorld()->GetAssets()};
		std::filesystem::path SaveTarget{TargetPath};

		if (SaveTarget.empty()) {
			if (!mLandscapeAssetPath.empty()) {
				SaveTarget = Core::FilePath::FromUtf8(mLandscapeAssetPath);
			} else {
				const std::filesystem::path SubDir{"Landscapes"};
				std::error_code DirError{};
				std::filesystem::create_directories(Assets->GetContentRoot() / SubDir, DirError);

				if (DirError) {
					Error = "Cannot create landscapes directory";

					return false;
				}

				const Core::FString BaseName{GetName().empty() ? Core::FString{"Landscape"} : GetName()};
				SaveTarget = Assets->MakeUniqueAssetPath(SubDir, BaseName, Error);

				if (SaveTarget.empty()) {
					return false;
				}
			}
		}

		if (!Assets->SaveLandscape(SaveTarget, *ActiveAsset, Error)) {
			return false;
		}

		const Core::FString RelativePath{Assets->GetRelativePath(SaveTarget, Error)};

		if (!RelativePath.empty()) {
			mLandscapeAssetPath = RelativePath;
		}

		return true;
	}

	Index_t ALandscape::AddLayer(const Asset::FLandscapeLayerInfo& LayerInfo) {
		EnsureRuntimeResources();
		Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr) {
			return InvalidIndex;
		}

		const Index_t Index{ActiveAsset->AddLayer(LayerInfo)};

		if (Index != InvalidIndex) {
			mLayers = ActiveAsset->GetLayers();
			ResolveMaterials();
			RebuildSharedRenderResources();
			RefreshExistingComponents();
		}

		return Index;
	}

	bool ALandscape::RemoveLayer(Index_t LayerIndex) {
		EnsureRuntimeResources();
		Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr || !ActiveAsset->RemoveLayer(LayerIndex)) {
			return false;
		}

		mLayers = ActiveAsset->GetLayers();
		ResolveMaterials();
		RebuildSharedRenderResources();
		RefreshExistingComponents();

		return true;
	}

	bool ALandscape::SampleHeightAtWorld(const FVector& WorldPosition, float& OutWorldZ) const {
		FVector UnusedNormal{};

		return SampleHeightAndNormalAtWorld(WorldPosition, OutWorldZ, UnusedNormal, false);
	}

	bool ALandscape::SampleHeightAndNormalAtWorld(const FVector& WorldPosition, float& OutWorldZ, FVector& OutWorldNormal, bool SmoothNormal) const {
		const Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr || !ActiveAsset->IsValid()) {
			return false;
		}

		const FMatrix WorldMatrix{GetActorTransform().ToMatrix()};
		FMatrix InverseMatrix{};

		if (!WorldMatrix.TryInverse(InverseMatrix)) {
			return false;
		}

		const FVector LocalPos{InverseMatrix.TransformPosition(WorldPosition)};
		float LocalZ{};
		FVector LocalNormal{};

		if (!ActiveAsset->SampleLocalHeightAndNormal(LocalPos.x, LocalPos.y, LocalZ, LocalNormal, SmoothNormal)) {
			return false;
		}

		const FVector WorldSurface{WorldMatrix.TransformPosition({LocalPos.x, LocalPos.y, LocalZ})};
		OutWorldZ = WorldSurface.z;
		OutWorldNormal = InverseMatrix.Transposed().TransformVector(LocalNormal).Normalized();

		return true;
	}

	bool ALandscape::SampleLayerWeightsAtWorld(const FVector& WorldPosition, Core::TArray<float>& OutWeights) const {
		const Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr || !ActiveAsset->IsValid()) {
			OutWeights.clear();

			return false;
		}

		const FMatrix WorldMatrix{GetActorTransform().ToMatrix()};
		FMatrix InverseMatrix{};

		if (!WorldMatrix.TryInverse(InverseMatrix)) {
			OutWeights.clear();

			return false;
		}

		const FVector LocalPos{InverseMatrix.TransformPosition(WorldPosition)};

		return ActiveAsset->SampleLocalLayerWeights(LocalPos.x, LocalPos.y, OutWeights);
	}

	bool ALandscape::RaycastLandscape(const FRay& WorldRay, float& OutDistance, FVector& OutWorldNormal) const {
		bool Found{};
		float Closest{std::numeric_limits<float>::max()};
		FVector BestNormal{0.0f, 0.0f, 1.0f};

		for (const auto& Component : GetComponents()) {
			const Engine::ULandscapeComponent* LandscapeComp{CoreUObject::Cast<Engine::ULandscapeComponent>(Component.get())};

			if (LandscapeComp == nullptr) {
				continue;
			}

			float CandidateDistance{Closest};
			FVector CandidateNormal{};

			if (LandscapeComp->RaycastComponent(WorldRay, CandidateDistance, CandidateNormal) && CandidateDistance < Closest) {
				Closest = CandidateDistance;
				BestNormal = CandidateNormal;
				Found = true;
			}
		}

		if (Found) {
			OutDistance = Closest;
			OutWorldNormal = BestNormal;
		}

		return Found;
	}

	const Core::FString& ALandscape::GetLandscapeAssetPath() const {
		return mLandscapeAssetPath;
	}

	bool ALandscape::SetLandscapeAssetPath(const Core::FString& Path) {
		if (mLandscapeAssetPath == Path && (Path.empty() || mExternalAsset.Get() != nullptr)) {
			return true;
		}

		mLandscapeAssetPath = Path;
		mExternalAsset = {};

		if (!Path.empty() && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr) {
			Core::FString Error{};
			Asset::ULandscapeAsset* Loaded{GetWorld()->GetAssets()->LoadLandscape(Core::FilePath::FromUtf8(Path), Error)};

			if (Loaded == nullptr) {
				return false;
			}

			mExternalAsset = Loaded;
			SyncParametersFromAsset(*Loaded);
		}

		if (GetRootComponent() != nullptr) {
			RebuildLandscape();
		}

		return true;
	}

	const Core::FString& ALandscape::GetLandscapeMaterialPath() const {
		return mLandscapeMaterialPath;
	}

	bool ALandscape::SetLandscapeMaterialPath(const Core::FString& Path) {
		if (mLandscapeMaterialPath == Path && (Path.empty() || mLandscapeMaterial.Get() != nullptr)) {
			return true;
		}

		mLandscapeMaterialPath = Path;
		mLandscapeMaterial = {};

		ResolveMaterials();
		RefreshExistingComponents();

		return true;
	}

	const Core::FString& ALandscape::GetHeightmapTexturePath() const {
		return mHeightmapTexturePath;
	}

	bool ALandscape::SetHeightmapTexturePath(const Core::FString& Path) {
		if (mHeightmapTexturePath == Path && !Path.empty()) {
			return true;
		}

		if (Path.empty()) {
			mHeightmapTexturePath.clear();

			return true;
		}

		if (GetWorld() == nullptr || GetWorld()->GetAssets() == nullptr) {
			mHeightmapTexturePath = Path;

			return true;
		}

		Core::FString Error{};

		return ApplyHeightmapTextureAsset(Path, Error);
	}

	Uint32 ALandscape::GetSubsectionSizeQuads() const {
		return mSubsectionSizeQuads;
	}

	bool ALandscape::SetSubsectionSizeQuads(Uint32 SizeQuads) {
		if (!Asset::FLandscapeGridBuilder::IsValidSubsectionSizeQuads(SizeQuads)) {
			return false;
		}

		if (mSubsectionSizeQuads != SizeQuads) {
			mSubsectionSizeQuads = SizeQuads;

			if (GetRootComponent() != nullptr) {
				RebuildLandscape();
			}
		}

		return true;
	}

	Uint32 ALandscape::GetNumSubsections() const {
		return mNumSubsections;
	}

	bool ALandscape::SetNumSubsections(Uint32 NumSubsections) {
		if (NumSubsections != 1u && NumSubsections != 2u) {
			return false;
		}

		if (mNumSubsections != NumSubsections) {
			mNumSubsections = NumSubsections;

			if (GetRootComponent() != nullptr) {
				RebuildLandscape();
			}
		}

		return true;
	}

	Uint32 ALandscape::GetComponentCountX() const {
		return mComponentCountX;
	}

	bool ALandscape::SetComponentCountX(Uint32 CountX) {
		if (CountX == 0u || CountX > 32u) {
			return false;
		}

		if (mComponentCountX != CountX) {
			mComponentCountX = CountX;

			if (GetRootComponent() != nullptr) {
				RebuildLandscape();
			}
		}

		return true;
	}

	Uint32 ALandscape::GetComponentCountY() const {
		return mComponentCountY;
	}

	bool ALandscape::SetComponentCountY(Uint32 CountY) {
		if (CountY == 0u || CountY > 32u) {
			return false;
		}

		if (mComponentCountY != CountY) {
			mComponentCountY = CountY;

			if (GetRootComponent() != nullptr) {
				RebuildLandscape();
			}
		}

		return true;
	}

	float ALandscape::GetQuadSize() const {
		return mQuadSize;
	}

	bool ALandscape::SetQuadSize(float QuadSize) {
		if (!std::isfinite(QuadSize) || QuadSize <= 0.0f) {
			return false;
		}

		if (mQuadSize != QuadSize) {
			mQuadSize = QuadSize;

			if (GetRootComponent() != nullptr) {
				RebuildLandscape();
			}
		}

		return true;
	}

	float ALandscape::GetHeightScale() const {
		return mHeightScale;
	}

	bool ALandscape::SetHeightScale(float HeightScale) {
		if (!std::isfinite(HeightScale) || HeightScale <= 0.0f) {
			return false;
		}

		if (mHeightScale != HeightScale) {
			mHeightScale = HeightScale;

			if (GetRootComponent() != nullptr) {
				EnsureRuntimeResources();

				if (Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()}) {
					ActiveAsset->SetHeightScale(mHeightScale);
					RebuildSharedRenderResources();
					RefreshExistingComponents();
				}
			}
		}

		return true;
	}

	float ALandscape::GetLODDistanceFactor() const {
		return mLODDistanceFactor;
	}

	bool ALandscape::SetLODDistanceFactor(float DistanceFactor) {
		if (!std::isfinite(DistanceFactor) || DistanceFactor <= 0.01f) {
			return false;
		}

		if (mLODDistanceFactor != DistanceFactor) {
			mLODDistanceFactor = DistanceFactor;

			if (GetRootComponent() != nullptr) {
				RebuildSharedRenderResources();
				RefreshExistingComponents();
			}
		}

		return true;
	}

	const Core::TArray<Asset::FLandscapeLayerInfo>& ALandscape::GetLayers() const {
		return mLayers;
	}

	bool ALandscape::SetLayers(const Core::TArray<Asset::FLandscapeLayerInfo>& Layers) {
		mLayers = Layers;

		if (Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()}) {
			SyncLayersToAsset(*ActiveAsset);
			ReapplyAllLayerWeightmaps(*ActiveAsset);
			ResolveMaterials();
			RebuildSharedRenderResources();
			RefreshExistingComponents();
		}

		return true;
	}

	Uint32 ALandscape::GetComponentResolution() const {
		return mSubsectionSizeQuads * mNumSubsections + 1u;
	}

	Uint32 ALandscape::GetComponentCount() const {
		return mComponentCountX * mComponentCountY;
	}

	Uint32 ALandscape::GetTotalResolutionX() const {
		return mComponentCountX * mSubsectionSizeQuads * mNumSubsections + 1u;
	}

	Uint32 ALandscape::GetTotalResolutionY() const {
		return mComponentCountY * mSubsectionSizeQuads * mNumSubsections + 1u;
	}

	Uint32 ALandscape::GetMaxLOD() const {
		return Asset::FLandscapeGridBuilder::ComputeMaxLOD(mSubsectionSizeQuads);
	}

	Asset::ULandscapeAsset* ALandscape::GetActiveLandscapeAsset() const {
		if (mExternalAsset.Get() != nullptr) {
			return mExternalAsset.Get();
		}

		return mOwnedAsset.get();
	}

	Asset::UStaticMesh* ALandscape::GetSharedGridMesh() const {
		return mSharedGrid.mMesh.get();
	}

	Asset::UMaterial* ALandscape::GetResolvedLandscapeMaterial() const {
		if (mLandscapeMaterial.Get() != nullptr) {
			return mLandscapeMaterial.Get();
		}

		return mDefaultLandscapeMaterial.get();
	}

	std::shared_ptr<const Engine::FLandscapeSharedRenderData> ALandscape::GetSharedRenderData() const {
		return mSharedRenderData;
	}

	Core::TArray<Engine::ULandscapeComponent*> ALandscape::GetLandscapeComponents() const {
		Core::TArray<Engine::ULandscapeComponent*> Result{};

		for (const auto& Component : GetComponents()) {
			if (Engine::ULandscapeComponent* LandscapeComp{CoreUObject::Cast<Engine::ULandscapeComponent>(Component.get())}) {
				Result.push_back(LandscapeComp);
			}
		}

		return Result;
	}

	void ALandscape::SyncParametersFromAsset(const Asset::ULandscapeAsset& Asset) {
		mSubsectionSizeQuads = Asset.GetSubsectionSizeQuads();
		mNumSubsections = Asset.GetNumSubsections();
		mComponentCountX = Asset.GetComponentCountX();
		mComponentCountY = Asset.GetComponentCountY();
		mQuadSize = Asset.GetQuadSize();
		mHeightScale = Asset.GetHeightScale();
		mLayers = Asset.GetLayers();
	}

	void ALandscape::SyncLayersToAsset(Asset::ULandscapeAsset& Asset) {
		Core::TArray<Asset::FLandscapeLayerInfo>& TargetLayers{Asset.GetMutableLayers()};

		while (TargetLayers.size() > mLayers.size()) {
			Asset.RemoveLayer(TargetLayers.size() - 1u);
		}

		for (Index_t Index{}; Index < mLayers.size(); ++Index) {
			if (Index < TargetLayers.size()) {
				TargetLayers[Index] = mLayers[Index];
			} else {
				Asset.AddLayer(mLayers[Index]);
			}
		}

		Asset.NormalizeWeightBlendedLayers();
	}

	void ALandscape::ReapplyAllLayerWeightmaps(Asset::ULandscapeAsset& Asset) {
		Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

		if (Assets == nullptr) {
			return;
		}

		const Core::TArray<Asset::FLandscapeLayerInfo>& Layers{Asset.GetLayers()};
		bool HasAnyWeightmapTexture{};

		for (const Asset::FLandscapeLayerInfo& Layer : Layers) {
			if (!Layer.mWeightmapTexturePath.empty()) {
				HasAnyWeightmapTexture = true;
				break;
			}
		}

		if (!HasAnyWeightmapTexture) {
			return;
		}

		// 모든 레이어의 원본 웨이트맵 텍스처를 먼저 채운 뒤 일괄 정규화
		for (Index_t Index{}; Index < Layers.size(); ++Index) {
			if (Layers[Index].mWeightmapTexturePath.empty()) {
				Asset.FillLayerWeights(Index, 0u);
				continue;
			}

			Core::FString LoadError{};
			Asset::UTexture2D* Texture{Assets->LoadTexture(Core::FilePath::FromUtf8(Layers[Index].mWeightmapTexturePath), LoadError)};

			if (Texture == nullptr || !Asset.SampleWeightmapTextureToLayerRaw(Index, *Texture, LoadError)) {
				Asset.FillLayerWeights(Index, 0u);
			}
		}

		Asset.NormalizeWeightBlendedLayers();
		Asset.MarkDataDirty();
	}

	void ALandscape::ResolveMaterials() {
		Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

		if (Assets == nullptr) {
			return;
		}

		if (!mLandscapeMaterialPath.empty()) {
			Core::FString Error{};
			Asset::UMaterial* Loaded{Assets->LoadMaterial(Core::FilePath::FromUtf8(mLandscapeMaterialPath), Error)};
			mLandscapeMaterial = Loaded;
		} else {
			mLandscapeMaterial = {};
		}

		if (Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()}) {
			Core::FString Error{};
			ActiveAsset->ResolveLayerMaterials(*Assets, Error);
			mLayers = ActiveAsset->GetLayers();
		}
	}

	void ALandscape::RebuildSharedRenderResources() {
		const Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr || !ActiveAsset->IsValid()) {
			mSharedRenderData.reset();
			mPackedHeightmap.reset();

			return;
		}

		if (mSharedGrid.mMesh == nullptr || mSharedGrid.mSubsectionSizeQuads != ActiveAsset->GetSubsectionSizeQuads()) {
			Asset::FLandscapeGridBuilder::BuildSharedSubsectionGrid(ActiveAsset->GetSubsectionSizeQuads(), mSharedGrid);
		}

		mPackedHeightmap = ActiveAsset->BuildPackedHeightmapTexture();

		std::shared_ptr<Engine::FLandscapeSharedRenderData> Shared{std::make_shared<Engine::FLandscapeSharedRenderData>()};
		Shared->mLandscapeId = GetGuid();
		Shared->mSubsectionSizeQuads = ActiveAsset->GetSubsectionSizeQuads();
		Shared->mNumSubsections = ActiveAsset->GetNumSubsections();
		Shared->mComponentSizeQuads = ActiveAsset->GetComponentSizeQuads();
		Shared->mComponentCountX = ActiveAsset->GetComponentCountX();
		Shared->mComponentCountY = ActiveAsset->GetComponentCountY();
		Shared->mTotalResolutionX = ActiveAsset->GetTotalResolutionX();
		Shared->mTotalResolutionY = ActiveAsset->GetTotalResolutionY();
		Shared->mQuadSize = ActiveAsset->GetQuadSize();
		Shared->mHeightScale = ActiveAsset->GetHeightScale();
		Shared->mMaxLOD = ActiveAsset->GetMaxLOD();
		Shared->mLODDistanceFactor = mLODDistanceFactor;
		Shared->mDataRevision = ActiveAsset->GetDataRevision();
		Shared->mHeightmapTexture = mPackedHeightmap != nullptr ? CoreUObject::MakeObjectRef(mPackedHeightmap.get()) : CoreUObject::TObjectRef<Asset::UTexture2D>{};
		Shared->mLODRanges = mSharedGrid.mLODRanges;

		mSharedRenderData = std::move(Shared);
		mBuiltDataRevision = ActiveAsset->GetDataRevision();
	}

	void ALandscape::RebuildComponentGrid() {
		const Asset::ULandscapeAsset* ActiveAsset{GetActiveLandscapeAsset()};

		if (ActiveAsset == nullptr || !ActiveAsset->IsValid()) {
			return;
		}

		Core::TArray<Engine::ULandscapeComponent*> Existing{GetLandscapeComponents()};
		Engine::USceneComponent* Root{GetRootComponent()};
		const FTransform RootTransform{GetActorTransform()};
		const FMatrix RootMatrix{RootTransform.ToMatrix()};
		const Uint32 CompSizeQuads{ActiveAsset->GetComponentSizeQuads()};
		const float QuadSize{ActiveAsset->GetQuadSize()};
		const Index_t NeededCount{static_cast<Index_t>(ActiveAsset->GetComponentCountX()) * ActiveAsset->GetComponentCountY()};

		Index_t ReuseIndex{};

		// 기존 컴포넌트를 제자리에서 재사용하고 부족한 만큼만 추가
		for (Uint32 CompY{}; CompY < ActiveAsset->GetComponentCountY(); ++CompY) {
			for (Uint32 CompX{}; CompX < ActiveAsset->GetComponentCountX(); ++CompX) {
				const Uint32 BaseX{CompX * CompSizeQuads};
				const Uint32 BaseY{CompY * CompSizeQuads};
				const Core::FString CompName{"LandscapeComponent_" + std::to_string(CompX) + "_" + std::to_string(CompY)};

				FTransform CompTransform{RootTransform};
				const FVector LocalOffset{static_cast<float>(BaseX) * QuadSize, static_cast<float>(BaseY) * QuadSize, 0.0f};
				CompTransform.mLocation = RootMatrix.TransformPosition(LocalOffset);

				Engine::ULandscapeComponent* TargetComp{};

				if (ReuseIndex < Existing.size()) {
					TargetComp = Existing[ReuseIndex];
					TargetComp->SetName(CompName);
					TargetComp->ConfigureSection(CompX, CompY, BaseX, BaseY);
					TargetComp->SetTransform(CompTransform);

					if (Root != nullptr && TargetComp->GetAttachParent() != Root) {
						TargetComp->SetAttachParent(Root);
					}
				} else {
					std::unique_ptr<Engine::ULandscapeComponent> Comp{CoreUObject::NewObject<Engine::ULandscapeComponent>()};
					Comp->SetName(CompName);
					Comp->ConfigureSection(CompX, CompY, BaseX, BaseY);
					Comp->SetTransform(CompTransform);

					Engine::ULandscapeComponent* RawComp{Comp.get()};

					if (AddComponent(std::move(Comp))) {
						if (Root != nullptr) {
							RawComp->SetAttachParent(Root);
						}

						TargetComp = RawComp;
					}
				}

				if (TargetComp != nullptr) {
					TargetComp->RebuildComponentData();
					TargetComp->RecreatePhysicsState();
				}

				++ReuseIndex;
			}
		}

		// 남는 컴포넌트는 제거하거나 디스패치 중이면 비활성화
		for (Index_t ExtraIndex{NeededCount}; ExtraIndex < Existing.size(); ++ExtraIndex) {
			Engine::ULandscapeComponent* ExtraComp{Existing[ExtraIndex]};

			if (!RemoveComponent(ExtraComp)) {
				ExtraComp->ConfigureSection(std::numeric_limits<Uint32>::max(), std::numeric_limits<Uint32>::max(), 0u, 0u);
				ExtraComp->RebuildComponentData();
				ExtraComp->RecreatePhysicsState();
			}
		}
	}

	void ALandscape::RefreshExistingComponents() {
		Core::TArray<Engine::ULandscapeComponent*> Components{GetLandscapeComponents()};

		for (Engine::ULandscapeComponent* Component : Components) {
			Component->RebuildComponentData();
			Component->RecreatePhysicsState();
		}
	}

}
