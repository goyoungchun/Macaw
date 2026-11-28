#include "FLandscapeLODSolver.h"

#include "FScene.h"
#include "FScenePrimitive.h"

#include <algorithm>
#include <cmath>

namespace Renderer {

	namespace {

		// 점에서 경계 상자까지의 최단 거리 계산
		float DistanceToBox(const FVector& Point, const FBox& Box) {
			const FVector Clamped{Point.Clamped(Box.mMinimum, Box.mMaximum)};

			return Point.Distance(Clamped);
		}

	}

	void FLandscapeLODSolver::Solve(const Renderer::FScene& Scene, const FVector& CameraLocation) {
		mLandscapes.clear();

		for (const Renderer::FScenePrimitive& Primitive : Scene.GetPrimitives()) {
			if (!Primitive.mProxy.mLandscape.has_value() || Primitive.mProxy.mLandscape->mShared == nullptr) {
				continue;
			}

			const Engine::FLandscapeComponentRenderData& Comp{*Primitive.mProxy.mLandscape};
			const Engine::FLandscapeSharedRenderData& Shared{*Comp.mShared};
			const Uint32 SubCountX{Shared.mComponentCountX * Shared.mNumSubsections};
			const Uint32 SubCountY{Shared.mComponentCountY * Shared.mNumSubsections};

			if (SubCountX == 0u || SubCountY == 0u) {
				continue;
			}

			FLandscapeGridState& Grid{mLandscapes[Shared.mLandscapeId]};

			if (Grid.mSections.empty()) {
				Grid.mSubCountX = SubCountX;
				Grid.mSubCountY = SubCountY;
				Grid.mMaxLOD = Shared.mMaxLOD;
				Grid.mSections.resize(static_cast<Index_t>(SubCountX) * SubCountY);
				Grid.mPresent.assign(static_cast<Index_t>(SubCountX) * SubCountY, 0u);
			}

			const float BaseSubWorldSize{static_cast<float>(Shared.mSubsectionSizeQuads) * Shared.mQuadSize};
			const float BaseRange{std::max(BaseSubWorldSize * 1.35f * Shared.mLODDistanceFactor, 1.0f)};

			for (Uint32 SubY{}; SubY < Shared.mNumSubsections; ++SubY) {
				for (Uint32 SubX{}; SubX < Shared.mNumSubsections; ++SubX) {
					const Uint32 GlobalSubX{Comp.mComponentIndexX * Shared.mNumSubsections + SubX};
					const Uint32 GlobalSubY{Comp.mComponentIndexY * Shared.mNumSubsections + SubY};

					if (GlobalSubX >= Grid.mSubCountX || GlobalSubY >= Grid.mSubCountY) {
						continue;
					}

					const Index_t SubLocalIndex{static_cast<Index_t>(SubY) * Shared.mNumSubsections + SubX};
					const FBox SubBounds{SubLocalIndex < Comp.mSubsectionBounds.size() ? Comp.mSubsectionBounds[SubLocalIndex] : Primitive.mProxy.mBounds};
					const float Dist{DistanceToBox(CameraLocation, SubBounds)};

					float ContinuousLOD{};

					if (Dist > BaseRange) {
						ContinuousLOD = std::log2(Dist / BaseRange);
					}

					const float ClampedContinuous{std::clamp(ContinuousLOD, 0.0f, static_cast<float>(Shared.mMaxLOD))};
					const Uint32 TargetLOD{static_cast<Uint32>(std::floor(ClampedContinuous))};
					const float Fraction{ClampedContinuous - static_cast<float>(TargetLOD)};
					// 상위 전환 구간 말단에서만 부드럽게 모핑
					const float Morph{TargetLOD < Shared.mMaxLOD ? std::clamp((Fraction - 0.65f) / 0.35f, 0.0f, 1.0f) : 0.0f};

					const Index_t GridIndex{static_cast<Index_t>(GlobalSubY) * Grid.mSubCountX + GlobalSubX};
					Grid.mSections[GridIndex] = {TargetLOD, Morph, 0u};
					Grid.mPresent[GridIndex] = 1u;
				}
			}
		}

		// 인접 서브섹션 간 LOD 차이를 최대 한 단계로 제한하고 방향 스티칭 마스크 생성
		for (auto& Entry : mLandscapes) {
			FLandscapeGridState& Grid{Entry.second};

			bool Changed{true};

			for (Uint32 Iteration{}; Iteration <= Grid.mMaxLOD && Changed; ++Iteration) {
				Changed = false;

				for (Uint32 Y{}; Y < Grid.mSubCountY; ++Y) {
					for (Uint32 X{}; X < Grid.mSubCountX; ++X) {
						const Index_t Index{static_cast<Index_t>(Y) * Grid.mSubCountX + X};

						if (Grid.mPresent[Index] == 0u) {
							continue;
						}

						Uint32 CurrentLOD{Grid.mSections[Index].mLOD};
						Uint32 MaxAllowedLOD{CurrentLOD};

						const Int32 OffsetsX[4]{-1, 1, 0, 0};
						const Int32 OffsetsY[4]{0, 0, -1, 1};

						for (Index_t Dir{}; Dir < 4u; ++Dir) {
							const Int32 NX{static_cast<Int32>(X) + OffsetsX[Dir]};
							const Int32 NY{static_cast<Int32>(Y) + OffsetsY[Dir]};

							if (NX < 0 || NY < 0 || NX >= static_cast<Int32>(Grid.mSubCountX) || NY >= static_cast<Int32>(Grid.mSubCountY)) {
								continue;
							}

							const Index_t NeighborIndex{static_cast<Index_t>(NY) * Grid.mSubCountX + static_cast<Index_t>(NX)};

							if (Grid.mPresent[NeighborIndex] != 0u) {
								MaxAllowedLOD = std::min(MaxAllowedLOD, Grid.mSections[NeighborIndex].mLOD + 1u);
							}
						}

						if (MaxAllowedLOD < CurrentLOD) {
							Grid.mSections[Index].mLOD = MaxAllowedLOD;
							Grid.mSections[Index].mMorphFactor = 1.0f;
							Changed = true;
						}
					}
				}
			}

			for (Uint32 Y{}; Y < Grid.mSubCountY; ++Y) {
				for (Uint32 X{}; X < Grid.mSubCountX; ++X) {
					const Index_t Index{static_cast<Index_t>(Y) * Grid.mSubCountX + X};

					if (Grid.mPresent[Index] == 0u) {
						continue;
					}

					const Uint32 MyLOD{Grid.mSections[Index].mLOD};
					Uint32 Mask{};

					const Int32 OffsetsX[4]{-1, 1, 0, 0};
					const Int32 OffsetsY[4]{0, 0, -1, 1};

					for (Uint32 Dir{}; Dir < 4u; ++Dir) {
						const Int32 NX{static_cast<Int32>(X) + OffsetsX[Dir]};
						const Int32 NY{static_cast<Int32>(Y) + OffsetsY[Dir]};

						if (NX < 0 || NY < 0 || NX >= static_cast<Int32>(Grid.mSubCountX) || NY >= static_cast<Int32>(Grid.mSubCountY)) {
							continue;
						}

						const Index_t NeighborIndex{static_cast<Index_t>(NY) * Grid.mSubCountX + static_cast<Index_t>(NX)};

						if (Grid.mPresent[NeighborIndex] != 0u && Grid.mSections[NeighborIndex].mLOD > MyLOD) {
							Mask |= (1u << Dir);
						}
					}

					Grid.mSections[Index].mStitchMask = Mask;
				}
			}
		}
	}

	Renderer::FLandscapeSectionLODState FLandscapeLODSolver::GetSectionState(const Core::FGuid& LandscapeId, Uint32 GlobalSubX, Uint32 GlobalSubY) const {
		const Core::TMap<Core::FGuid, FLandscapeGridState, Core::FGuidHash>::const_iterator Found{mLandscapes.find(LandscapeId)};

		if (Found == mLandscapes.end()) {
			return {};
		}

		const FLandscapeGridState& Grid{Found->second};

		if (GlobalSubX >= Grid.mSubCountX || GlobalSubY >= Grid.mSubCountY) {
			return {};
		}

		return Grid.mSections[static_cast<Index_t>(GlobalSubY) * Grid.mSubCountX + GlobalSubX];
	}

	void FLandscapeLODSolver::Clear() {
		mLandscapes.clear();
	}

}
