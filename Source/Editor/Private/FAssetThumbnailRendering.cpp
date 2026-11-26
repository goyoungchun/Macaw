#include "FAssetThumbnailRendering.h"

#include "FPhysicsAssetEditing.h"
#include "FPhysicsAssetViewerContent.h"
#include "Asset/Animation/UAnimGraph.h"
#include "Asset/Animation/UAnimSequence.h"
#include "Asset/Animation/UBlendSpace.h"
#include "Asset/FAssetManager.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Core/FilePath.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Editor/FEditorAssetCatalog.h"
#include "Engine/Animation/FAnimNode_BlendSpacePlayer.h"
#include "Engine/Animation/UAnimGraphInstance.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Viewer/FAnimationViewerContent.h"
#include "Viewer/FAssetViewer.h"
#include "Viewer/FMeshViewerContent.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

using namespace Editor;

namespace {
    struct FPreviewRig {
        Asset::USkeletalMesh* mMesh{};
        Asset::USkeleton* mSkeleton{};
    };

    struct FAnimationTracks {
        std::filesystem::path mSequencePath{};
        std::unordered_set<Core::FString> mBoneNames{};
    };

    Core::TArray<std::filesystem::path> FindNearbyAssets(const FEditorAssetCatalog& Catalog, const std::filesystem::path& Path, Asset::EAssetType Type) {
        const auto Folder{Core::FilePath::ToLower(Path.parent_path())};
        const auto Parent{Folder.parent_path()};
        const auto Stem{Core::FilePath::ToLower(Path.stem()).wstring()};
        Core::TArray<std::pair<int, std::filesystem::path>> Candidates{};

        for (const auto& Entry : Catalog.GetEntries()) {
            if (Entry.GetType() != Type) {
                continue;
            }

            const auto& Candidate{Entry.GetPath()};
            const auto CandidateFolder{Core::FilePath::ToLower(Candidate.parent_path())};
            const auto CandidateStem{Core::FilePath::ToLower(Candidate.stem()).wstring()};
            const bool SharesStem{CandidateStem == Stem || CandidateStem.starts_with(Stem + L"_") || Stem.starts_with(CandidateStem + L"_")};
            const int Distance{CandidateFolder == Folder ? 0 : CandidateFolder == Parent || CandidateFolder.parent_path() == Folder ? 1 : !Parent.empty() && CandidateFolder.parent_path() == Parent ? 2 : 3};

            if (Distance < 3 || SharesStem) {
                Candidates.emplace_back(Distance * 2 + (SharesStem ? 0 : 1), Candidate);
            }
        }

        std::sort(Candidates.begin(), Candidates.end());
        Core::TArray<std::filesystem::path> Result{};

        for (Index_t Index{}; Index < std::min<std::size_t>(Candidates.size(), 16); ++Index) {
            Result.push_back(std::move(Candidates[Index].second));
        }

        return Result;
    }

    bool IsCompatible(const Asset::USkeletalMesh& Mesh, const Asset::USkeleton& Skeleton, const std::unordered_set<Core::FString>& Names) {
        const auto& Bones{Skeleton.GetBones()};

        if (Bones.size() < Mesh.GetSkinMatrixCount() || Bones.size() < Names.size()) {
            return false;
        }

        const auto Matches{std::count_if(Bones.begin(), Bones.end(), [&Names](const Asset::FMeshImportBone& Bone) {
            return Names.contains(Bone.GetName());
        })};

        return static_cast<std::size_t>(Matches) == Names.size();
    }

    Asset::USkeleton* FindAnimationSkeleton(FEditorAssetCatalog& Catalog, const std::filesystem::path& Path, const Asset::USkeletalMesh& Mesh, const std::unordered_set<Core::FString>& Names) {
        Asset::FAssetManager& Assets{*Catalog.GetAssets()};
        Core::FString SkeletonPath{};

        if (auto* Skeleton{PhysicsAssetEditing::FindSkeletonForMesh(Assets, Path, SkeletonPath)}; Skeleton != nullptr && IsCompatible(Mesh, *Skeleton, Names)) {
            return Skeleton;
        }

        Core::FString Error{};

        for (const auto& Candidate : FindNearbyAssets(Catalog, Path, Asset::EAssetType::Skeleton)) {
            auto* Skeleton{Assets.LoadSkeleton(Candidate, Error)};

            if (Skeleton != nullptr && IsCompatible(Mesh, *Skeleton, Names)) {
                return Skeleton;
            }
        }

        return nullptr;
    }

    void CollectTracks(const Asset::UAnimSequence& Sequence, FAnimationTracks& Tracks) {
        if (Tracks.mSequencePath.empty()) {
            Tracks.mSequencePath = Core::FilePath::FromUtf8(Sequence.GetAssetPath());
        }

        for (const auto& Track : Sequence.GetDataModel().GetBoneAnimationTracks()) {
            Tracks.mBoneNames.insert(Track.mName);
        }
    }

    void CollectTracks(const Asset::UBlendSpace& BlendSpace, FAnimationTracks& Tracks) {
        for (const auto& Sample : BlendSpace.GetSamples()) {
            if (const auto* Sequence{Sample.mAnimation.Get()}) {
                CollectTracks(*Sequence, Tracks);
            }
        }
    }

    void CollectTracks(Asset::FAssetManager& Assets, const Asset::UAnimGraph& Graph, FAnimationTracks& Tracks) {
        Core::FString Error{};

        for (const auto& Node : Graph.GetNodes()) {
            if (Node.mType == "SequencePlayer") {
                const auto* Path{Node.FindParameter("Sequence")};
                const auto* Sequence{Path != nullptr ? Assets.LoadAnimSequence(Core::FilePath::FromUtf8(*Path), Error) : nullptr};

                if (Sequence != nullptr) {
                    CollectTracks(*Sequence, Tracks);
                }
            } else if (Node.mType == "BlendSpacePlayer") {
                const auto* Path{Node.FindParameter("BlendSpace")};
                const auto* BlendSpace{Path != nullptr ? Assets.LoadBlendSpace(Core::FilePath::FromUtf8(*Path), Error) : nullptr};

                if (BlendSpace != nullptr) {
                    CollectTracks(*BlendSpace, Tracks);
                }
            } else if (const auto* StateMachine{Graph.FindStateMachine(Node.mId)}) {
                for (const auto& State : StateMachine->GetStates()) {
                    if (State.mGraph != nullptr) {
                        CollectTracks(Assets, *State.mGraph, Tracks);
                    }
                }
            }
        }
    }

    FPreviewRig FindAnimationRig(FEditorAssetCatalog& Catalog, const std::filesystem::path& Path, const FAnimationTracks& Tracks, const Core::FString& MeshPath, const Core::FString& SkeletonPath, Core::FString& Error) {
        Asset::FAssetManager& Assets{*Catalog.GetAssets()};

        if (!MeshPath.empty()) {
            const auto ExplicitPath{Core::FilePath::FromUtf8(MeshPath)};
            auto* Mesh{Assets.LoadSkeletalMesh(ExplicitPath, Error)};

            if (Mesh != nullptr) {
                const auto RelativePath{Core::FilePath::FromUtf8(Assets.GetRelativePath(ExplicitPath, Error))};
                auto* Skeleton{SkeletonPath.empty() ? FindAnimationSkeleton(Catalog, RelativePath, *Mesh, Tracks.mBoneNames) : Assets.LoadSkeleton(Core::FilePath::FromUtf8(SkeletonPath), Error)};

                if (Skeleton != nullptr && IsCompatible(*Mesh, *Skeleton, {})) {
                    return {Mesh, Skeleton};
                }
            }
        }

        if (Tracks.mBoneNames.empty()) {
            return {};
        }

        const auto& Origin{Tracks.mSequencePath.empty() ? Path : Tracks.mSequencePath};

        for (const auto& Candidate : FindNearbyAssets(Catalog, Origin, Asset::EAssetType::SkeletalMesh)) {
            auto* Mesh{Assets.LoadSkeletalMesh(Candidate, Error)};

            if (Mesh != nullptr) {
                if (auto* Skeleton{FindAnimationSkeleton(Catalog, Candidate, *Mesh, Tracks.mBoneNames)}) {
                    return {Mesh, Skeleton};
                }
            }
        }

        return {};
    }

    bool ApplyPose(Engine::USkeletalMeshComponent& Component, const Core::TArray<FTransform>& Pose) {
        Core::TArray<FMatrix> Transforms{};
        Transforms.reserve(Pose.size());

        for (const FTransform& Transform : Pose) {
            Transforms.push_back(Transform.ToMatrix());
        }

        return Component.SetLocalPose(Transforms);
    }

    bool SetMeshPreview(Viewer::FAssetViewer& Display, Asset::USkeletalMesh* Mesh, Asset::USkeleton* Skeleton, bool ShowBones) {
        if (Mesh == nullptr || !Mesh->IsValid() || (Skeleton != nullptr && !IsCompatible(*Mesh, *Skeleton, {})) || !Display.SetAsset(Mesh, Asset::EAssetType::SkeletalMesh)) {
            return false;
        }

        auto* Content{static_cast<Viewer::FMeshViewerContent*>(Display.GetContent())};

        return Skeleton == nullptr || Content->SetSkeleton(Skeleton, ShowBones);
    }
}

bool Editor::AssetThumbnailRendering::SetAnimationPreview(Viewer::FAssetViewer& Display, FEditorAssetCatalog& Catalog, const std::filesystem::path& Path, const Asset::UAnimSequence& Sequence, const Core::FString& MeshPath, const Core::FString& SkeletonPath, Core::FString& Error) {
    FAnimationTracks Tracks{};
    CollectTracks(Sequence, Tracks);
    const FPreviewRig Rig{FindAnimationRig(Catalog, Path, Tracks, MeshPath, SkeletonPath, Error)};
    auto* Content{static_cast<Viewer::FAnimationViewerContent*>(Display.GetContent())};

    return Rig.mMesh != nullptr && Content->SetMesh(Rig.mMesh, Rig.mSkeleton, Error);
}

bool Editor::AssetThumbnailRendering::SetSkeletalAssetPreview(Viewer::FAssetViewer& Display, FEditorAssetCatalog& Catalog, const std::filesystem::path& Path, Asset::EAssetType Type, CoreUObject::UObject* Object, Core::FString& Error) {
    Asset::FAssetManager& Assets{*Catalog.GetAssets()};

    if (Type == Asset::EAssetType::Skeleton) {
        auto* Skeleton{CoreUObject::Cast<Asset::USkeleton>(Object)};

        for (const auto& Candidate : FindNearbyAssets(Catalog, Path, Asset::EAssetType::SkeletalMesh)) {
            Core::FString SkeletonPath{};

            if (PhysicsAssetEditing::FindSkeletonForMesh(Assets, Candidate, SkeletonPath) == Skeleton && SetMeshPreview(Display, Assets.LoadSkeletalMesh(Candidate, Error), Skeleton, true)) {
                return true;
            }
        }

        return false;
    }

    const auto NamedMesh{PhysicsAssetEditing::GetMeshPathForPhysicsAsset(Path)};
    const auto Target{Core::FilePath::ToLower(Path.lexically_normal())};

    for (const auto& Candidate : FindNearbyAssets(Catalog, Path, Asset::EAssetType::SkeletalMesh)) {
        auto* Mesh{Assets.LoadSkeletalMesh(Candidate, Error)};

        if (Mesh == nullptr) {
            continue;
        }

        const auto Assigned{Core::FilePath::ToLower(Core::FilePath::FromUtf8(Mesh->GetPhysicsAssetPath()).lexically_normal())};

        if (Assigned == Target || (Assigned.empty() && Core::FilePath::ToLower(Candidate) == Core::FilePath::ToLower(NamedMesh))) {
            Core::FString SkeletonPath{};
            auto* Skeleton{PhysicsAssetEditing::FindSkeletonForMesh(Assets, Candidate, SkeletonPath)};

            if (SetMeshPreview(Display, Mesh, Skeleton, false)) {
                return true;
            }
        }
    }

    return false;
}

bool Editor::AssetThumbnailRendering::SetAnimatedAssetPreview(Viewer::FAssetViewer& Display, FEditorAssetCatalog& Catalog, RenderCore::IRendererModule* Renderer, const std::filesystem::path& Path, CoreUObject::UObject* Object, Core::FString& Error) {
    auto* BlendSpace{CoreUObject::Cast<Asset::UBlendSpace>(Object)};
    auto* Graph{CoreUObject::Cast<Asset::UAnimGraph>(Object)};
    Asset::FAssetManager& Assets{*Catalog.GetAssets()};
    FAnimationTracks Tracks{};

    if (BlendSpace != nullptr) {
        CollectTracks(*BlendSpace, Tracks);
    } else if (Graph != nullptr) {
        CollectTracks(Assets, *Graph, Tracks);
    } else {
        return false;
    }

    const FPreviewRig Rig{FindAnimationRig(Catalog, Path, Tracks, {}, {}, Error)};
    auto Content{std::make_unique<FPhysicsAssetViewerContent>(Renderer, &Assets)};

    if (Rig.mMesh == nullptr || !Content->SetMesh(Rig.mMesh, Rig.mSkeleton, nullptr, Error)) {
        return false;
    }

    auto& Component{*Content->GetComponent()};

    if (BlendSpace != nullptr) {
        Engine::UAnimInstance Instance{};
        Instance.InitializeAnimation(&Component);
        Engine::FAnimNode_BlendSpacePlayer Player{};
        Player.mBlendSpace = BlendSpace;
        const auto& Axis{BlendSpace->GetBlendParameter(Asset::EBlendSpaceAxis::X)};
        Player.mX = std::midpoint(Axis.mMin, Axis.mMax);
        Player.Initialize(Instance);
        Player.Update(Engine::FAnimationUpdateContext{Instance, 0.0f});

        if (Player.GetSampleWeights().empty()) {
            return false;
        }

        Engine::FPoseContext Pose{Instance};
        Player.Evaluate(Pose);

        if (!ApplyPose(Component, Pose.mPose)) {
            return false;
        }
    } else {
        Engine::UAnimGraphInstance Instance{};

        if (!Instance.SetAnimGraph(Core::FilePath::ToGenericUtf8(Path))) {
            return false;
        }

        Instance.InitializeAnimation(&Component);

        if (Instance.GetRootNode() == nullptr) {
            return false;
        }

        Instance.UpdateAnimation(0.0f);

        if (!ApplyPose(Component, Instance.GetPose())) {
            return false;
        }
    }

    return Display.SetContent(std::move(Content));
}
