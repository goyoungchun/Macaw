/**
 * @file FAssetManager.cpp
 * @brief 프로젝트 콘텐츠 경로와 에셋 로드·저장·캐시를 구현합니다.
 * @ingroup Asset
 */

#include "Core/Types.h"
#include "Asset/FAssetManager.h"
#include "Asset/AssetStats.h"
#include "Core/FilePath.h"
#include "Asset/FAssetFile.h"
#include "Asset/UStaticMesh.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Asset/UTexture2D.h"
#include "Asset/UMaterial.h"
#include "RenderCore/Shader/UShader.h"
#include "Asset/Sequence/ULevelSequence.h"
#include "Asset/Animation/UAnimSequence.h"
#include "Asset/Animation/UBlendSpace1D.h"
#include "Asset/Animation/UAnimGraph.h"
#include "PhysicsRuntime/UPhysicsAsset.h"
#include "Core/Base/ErrorHandler.h"
#include "FMaterialFile.h"
#include "FMaterialAssetData.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "CoreUObject/Reflection/UClass.h"
#include "Serialization/JsonArchive.h"
#include "Serialization/MemoryArchive.h"
#include "FAssetSerialization.h"
#include "FTextureAssetData.h"
#include "FAssetImportScope.h"
#include "Asset/Import/IMeshImporter.h"
#include "Asset/Import/FMeshImportResult.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Asset {

    /**
     * @brief 이 구현 파일에서만 사용하는 내부 상태와 보조 함수입니다.
     */
    namespace {

        constexpr wchar_t DirectoryRedirectorName[]{L".redirector.masset"};

        bool IsAssetRedirector(const std::filesystem::path& Path) {
            Asset::EAssetType Type{};
            Core::FString Error{};

            return Asset::FAssetFile::GetType(Path, Type, Error) && Type == Asset::EAssetType::Redirector;
        }

        bool RestoreAssetIdentity(const std::filesystem::path& Path, CoreUObject::UObject& Object, Core::FString& Error) {
            Asset::EAssetType Type{};
            Core::FGuid Guid{Core::FGuid::Null()};

            return Asset::FAssetFile::GetIdentity(Path, Type, Guid, Error) && (!Guid.IsValid() || Object.RestoreGuid(Guid));
        }

        /**
         * @brief 에셋 상대 경로를 대소문자 구분 없이 조회할 캐시 키로 변환합니다.
         * @param[in] Relative 캐시 키로 변환할 프로젝트 상대 에셋 경로입니다.
         * @return 입력 값을 변환한 문자열입니다.
         */
        Core::FString CacheKey(const Core::FString& Relative) {
            return Core::FilePath::ToGenericUtf8(Core::FilePath::ToLower(Core::FilePath::FromUtf8(Relative)));
        }

        // 캐시에 올라간 에셋 객체는 바꾸지 않으므로, 같은 경로에 다른 객체가 이미 있으면 저장을 거부합니다.
        template <typename TCache, typename TAsset>

        /**
         * @brief 동일 경로의 캐시가 저장하려는 에셋과 충돌하는지 검사합니다.
         * @param Cache 동일 종류 에셋의 경로별 캐시입니다.
         * @param Key 조회하거나 저장할 캐시 키입니다.
         * @param Asset 캐시 충돌 여부를 비교할 에셋 포인터입니다.
         * @param Error 실패 원인을 기록할 출력 문자열입니다.
         * @return 슬롯이 비었거나 같은 에셋이면 true이며 충돌 시 Error에 원인을 기록합니다.
         */
        bool CheckCacheSlot(const TCache& Cache, const Core::FString& Key, const TAsset* Asset, Core::FString& Error) {
            const auto Existing{Cache.find(Key)};

            if (Existing != Cache.end() && Existing->second.get() != Asset) {
                Error = "Another asset is already loaded at this path";

                return false;
            }

            return true;
        }

        /**
         * @brief 파일명 금지 문자·끝 공백과 점·Windows 예약 이름을 정리합니다.
         * @param[in] Name 조회하거나 지정할 이름입니다.
         * @param[in] Fallback 정리한 이름이 비었을 때 사용할 대체 파일 이름입니다.
         * @return 안전하게 정리한 이름이며 입력이 비어 있으면 대체 이름을 사용합니다.
         */
        Core::FString AssetFileName(Core::FString Name, const Core::FString& Fallback) {
            for (char& Character : Name) {
                if (static_cast<unsigned char>(Character) < 32 || Core::FString{"<>:\"/\\|?*"}.find(Character) != Core::FString::npos) {
                    Character = '_';
                }
            }

            while (!Name.empty() && (Name.back() == '.' || Name.back() == ' ')) {
                Name.pop_back();
            }

            if (Name.empty()) {
                Name = Fallback;
            }

            Core::FString Base{Name.substr(0, Name.find('.'))};

            for (char& Character : Base) {
                if (Character >= 'a' && Character <= 'z') {
                    Character = static_cast<char>(Character - 'a' + 'A');
                }
            }

            if (Base == "CON" || Base == "PRN" || Base == "AUX" || Base == "NUL" || (Base.size() == 4 && (Base.starts_with("COM") || Base.starts_with("LPT")) && Base.back() >= '1' && Base.back() <= '9')) {
                Name.insert(Name.begin(), '_');
            }

            return Name;
        }

        /**
         * @brief 기존 파일과 겹치지 않도록 숫자 접미사를 붙인 .masset 경로를 선택합니다.
         * @param[in] Directory 에셋 파일을 만들 폴더입니다.
         * @param[in] Name 조회하거나 지정할 이름입니다.
         * @param[out] Error 실패 원인을 기록할 문자열입니다.
         * @return 선택한 파일 경로이며 파일 시스템 검사 실패 시 빈 경로입니다.
         */
        std::filesystem::path UniqueAssetPath(const std::filesystem::path& Directory, const Core::FString& Name, Core::FString& Error) {
            const auto Base{Core::FilePath::FromUtf8(Name)};
            auto Target{Directory / Base};

            Target += L".masset";

            std::error_code Code{};

            for (Index_t Index{1}; std::filesystem::exists(Target, Code) && !Code; ++Index) {
                Target = Directory / (Base.wstring() + L"_" + std::to_wstring(Index) + L".masset");
            }

            if (Code) {
                Error = "Cannot choose an asset filename: " + Code.message();

                return {};
            }

            return Target;
        }

        /**
         * @brief 재질 텍스처를 중복 경로 캐시와 공유하여 가져오고 프로젝트 상대 참조를 반환합니다.
         * @param[in,out] Assets 경로 경계를 확인하고 에셋을 가져올 프로젝트 에셋 관리자입니다.
         * @param[in] Texture 가져올 원본 텍스처 참조입니다.
         * @param[in] Directory 가져온 텍스처 에셋을 저장할 폴더입니다.
         * @param[in,out] Textures 원본 경로별로 이미 가져온 텍스처의 프로젝트 참조를 보관하는 캐시입니다.
         * @param[out] ImportedPath 가져온 텍스처의 프로젝트 상대 경로를 기록할 문자열입니다.
         * @param[out] Error 실패 원인을 기록할 문자열입니다.
         * @return 텍스처가 없거나 가져오기에 성공하면 true이며 오류이면 false입니다.
         */
        bool ImportMeshTexture(Asset::FAssetManager& Assets, const Asset::FMeshImportTexture& Texture, const std::filesystem::path& Directory, Core::TMap<Core::FString, Core::FString>& Textures, Core::FString& ImportedPath, Core::FString& Error) {
            ImportedPath.clear();

            if (Texture.GetSource() == Asset::EMeshImportTextureSource::None) {
                return true;
            }

            const bool Embedded{Texture.GetSource() == Asset::EMeshImportTextureSource::Embedded};
            const auto Source{Embedded ? Core::FilePath::FromUtf8(Texture.GetEmbeddedReference()) : Texture.GetFilePath()};
            const Core::FString Key{Core::FString{Embedded ? "Embedded:" : "File:"} + CacheKey(Core::FilePath::ToUtf8(Source))};
            auto Existing{Textures.find(Key)};

            if (Existing == Textures.end()) {
                const auto Target{UniqueAssetPath(Directory, AssetFileName(Core::FilePath::ToUtf8(Source.stem()), "Texture"), Error)};

                if (Target.empty()) {
                    return false;
                }

                bool Imported{};

                if (Embedded) {
                    auto Image{CoreUObject::NewObject<Asset::UTexture2D>()};

                    Imported = Image->LoadFromMemory(Texture.GetEmbeddedData(), Source) && Assets.SaveTexture(Target, *Image, Error);

                    if (!Imported && Error.empty()) {
                        Error = "Cannot decode the embedded texture";
                    }
                } else {
                    Imported = Assets.ImportTexture(Source, Target, Error);
                }

                if (!Imported) {
                    Error = "Cannot import material texture '" + Core::FilePath::ToUtf8(Source) + "': " + Error;

                    return false;
                }

                const Core::FString RelativePath{Assets.GetRelativePath(Target, Error)};

                if (RelativePath.empty()) {
                    return false;
                }

                Existing = Textures.emplace(Key, RelativePath).first;
            }

            ImportedPath = Existing->second;

            return true;
        }

        /**
         * @brief 메시 정점 속성과 UV 채널 및 인덱스를 바이너리로 직렬화합니다.
         * @param[in,out] Archive 모드에 따라 값을 읽거나 쓸 아카이브입니다.
         * @param[in,out] Mesh 읽거나 직렬화할 메시 데이터입니다.
         */
        void SerializeMesh(Serialization::FMemoryArchive& Archive, Asset::FMeshData& Mesh) {
            Asset::FAssetSerialization::SerializeValues(Archive, Mesh.GetPositions());
            Asset::FAssetSerialization::SerializeValues(Archive, Mesh.GetNormals());
            Asset::FAssetSerialization::SerializeValues(Archive, Mesh.GetTangents());
            Asset::FAssetSerialization::SerializeValues(Archive, Mesh.GetColors());

            Uint32 Channels{Archive.IsLoading() ? 0 : static_cast<Uint32>(Mesh.GetUVChannels().size())};

            Archive.Serialize("UVChannels", Channels);

            if (Archive.HasError() || Channels > 8) {
                Archive.SetError("Invalid mesh UV channel count");

                return;
            }

            if (Archive.IsLoading()) {
                Mesh.GetUVChannels().resize(Channels);
            }

            for (auto& Channel : Mesh.GetUVChannels()) {
                Asset::FAssetSerialization::SerializeValues(Archive, Channel);
            }

            Asset::FAssetSerialization::SerializeValues(Archive, Mesh.GetIndices());

            if (Archive.HasError() || (Archive.IsLoading() && Archive.GetRemainingBytes() == 0)) {
                return;
            }

            Uint32 ExtensionVersion{1};

            Archive.Serialize("SectionsVersion", ExtensionVersion);

            if (ExtensionVersion != 1) {
                Archive.SetError("Unsupported mesh section version");

                return;
            }

            Uint32 SlotCount{Archive.IsLoading() ? 0 : static_cast<Uint32>(Mesh.GetMaterialSlots().size())};

            if (!Archive.BeginArray("MaterialSlots", SlotCount) || (Archive.IsLoading() && SlotCount > Archive.GetRemainingBytes() / 8)) {
                Archive.SetError("Invalid mesh material slot count");

                return;
            }

            if (Archive.IsLoading()) {
                Mesh.GetMaterialSlots().clear();
            }

            for (Index_t Index{}; Index < SlotCount && !Archive.HasError(); ++Index) {
                Core::FString Name{Archive.IsLoading() ? Core::FString{} : Mesh.GetMaterialSlots()[Index].GetName()};
                Core::FString Path{Archive.IsLoading() ? Core::FString{} : Mesh.GetMaterialSlots()[Index].GetMaterialPath()};

                Archive.Serialize("Name", Name);
                Archive.Serialize("Material", Path);

                if (Archive.IsLoading()) {
                    Mesh.GetMaterialSlots().emplace_back(std::move(Name), std::move(Path));
                }
            }

            Archive.EndArray();

            Uint32 SectionCount{Archive.IsLoading() ? 0 : static_cast<Uint32>(Mesh.GetSubMeshes().size())};

            if (!Archive.BeginArray("SubMeshes", SectionCount) || (Archive.IsLoading() && SectionCount > Archive.GetRemainingBytes() / 12)) {
                Archive.SetError("Invalid submesh count");

                return;
            }

            if (Archive.IsLoading()) {
                Mesh.GetSubMeshes().clear();
            }

            for (Index_t Index{}; Index < SectionCount && !Archive.HasError(); ++Index) {
                Uint32 First{static_cast<Uint32>(Archive.IsLoading() ? 0 : Mesh.GetSubMeshes()[Index].GetFirstIndex())};
                Uint32 Count{Archive.IsLoading() ? 0 : Mesh.GetSubMeshes()[Index].GetIndexCount()};
                Uint32 Slot{static_cast<Uint32>(Archive.IsLoading() ? 0 : Mesh.GetSubMeshes()[Index].GetMaterialIndex())};

                Archive.Serialize("FirstIndex", First);
                Archive.Serialize("IndexCount", Count);
                Archive.Serialize("MaterialIndex", Slot);

                if (Archive.IsLoading()) {
                    Mesh.GetSubMeshes().emplace_back(First, Count, Slot);
                }
            }

            Archive.EndArray();
        }

        /**
         * @brief 정적 메시의 볼록 충돌 형상을 바이너리로 직렬화합니다.
         * @details 정적 메시 에셋에서 메시 데이터 뒤에 붙는 섹션입니다. 이 섹션이 없는 이전 에셋은 경계 상자를 사용합니다.
         *          스켈레탈 메시는 메시 데이터 뒤에 본 데이터가 이어지므로 이 섹션을 쓰지 않습니다.
         * @param[in,out] Archive 모드에 따라 값을 읽거나 쓸 아카이브입니다.
         * @param[in,out] ConvexElems 읽거나 직렬화할 볼록 충돌 형상 목록입니다.
         * @param[in,out] SimpleShapes 읽거나 직렬화할 단순 충돌 형상(구, 상자, 캡슐)입니다. 볼록 형상 목록은 쓰지 않습니다.
         */
        void SerializeCollision(Serialization::FMemoryArchive& Archive, Core::TArray<PhysicsRuntime::FKConvexElem>& ConvexElems, PhysicsRuntime::FKAggregateGeom& SimpleShapes) {
            // 2: 볼록 형상 뒤에 단순 형상(구, 상자, 캡슐)을 저장한다. 1은 볼록 형상만 있다.
            Uint32 Version{2};

            Archive.Serialize("CollisionVersion", Version);

            if (Archive.HasError() || Version < 1 || Version > 2) {
                Archive.SetError("Unsupported mesh collision version");

                return;
            }

            Uint32 Count{Archive.IsLoading() ? 0 : static_cast<Uint32>(ConvexElems.size())};

            if (!Archive.BeginArray("ConvexElems", Count) || (Archive.IsLoading() && Count > Archive.GetRemainingBytes() / 8)) {
                Archive.SetError("Invalid convex collision count");

                return;
            }

            if (Archive.IsLoading()) {
                ConvexElems.clear();
                ConvexElems.resize(Count);
            }

            for (Index_t Index{}; Index < Count && !Archive.HasError(); ++Index) {
                Asset::FAssetSerialization::SerializeValues(Archive, ConvexElems[Index].mVertices);
                Asset::FAssetSerialization::SerializeValues(Archive, ConvexElems[Index].mIndices);
            }

            Archive.EndArray();

            if (Version < 2 || Archive.HasError()) {
                return;
            }

            // 단순 형상 목록 하나를 읽거나 씁니다. 개수는 남은 바이트로 검증합니다.
            const auto SerializeList{[&Archive](std::string_view Name, auto& Elems, std::size_t MinimumBytes, auto&& SerializeElement) {
                Uint32 Size{Archive.IsLoading() ? 0 : static_cast<Uint32>(Elems.size())};

                if (Archive.HasError() || !Archive.BeginArray(Name, Size) || (Archive.IsLoading() && Size > Archive.GetRemainingBytes() / MinimumBytes)) {
                    Archive.SetError("Invalid simple collision count");

                    return;
                }

                if (Archive.IsLoading()) {
                    Elems.clear();
                    Elems.resize(Size);
                }

                for (auto& Elem : Elems) {
                    SerializeElement(Elem);

                    if (Archive.HasError()) {
                        return;
                    }
                }

                Archive.EndArray();
            }};

            SerializeList("Spheres", SimpleShapes.mSphereElems, 16, [&Archive](PhysicsRuntime::FKSphereElem& Elem) {
                Archive.Serialize("Center", Elem.mCenter);
                Archive.Serialize("Radius", Elem.mRadius);
            });

            SerializeList("Boxes", SimpleShapes.mBoxElems, 36, [&Archive](PhysicsRuntime::FKBoxElem& Elem) {
                Archive.Serialize("Center", Elem.mCenter);
                Archive.Serialize("Rotation", Elem.mRotation);
                Archive.Serialize("HalfExtent", Elem.mHalfExtent);
            });

            SerializeList("Capsules", SimpleShapes.mSphylElems, 32, [&Archive](PhysicsRuntime::FKSphylElem& Elem) {
                Archive.Serialize("Center", Elem.mCenter);
                Archive.Serialize("Rotation", Elem.mRotation);
                Archive.Serialize("Radius", Elem.mRadius);
                Archive.Serialize("Length", Elem.mLength);
            });
        }

        /**
         * @brief 본 이름·부모·바인드 변환을 바이너리 아카이브에 직렬화합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param Bones 부모 인덱스와 바인드 변환을 가진 본 배열입니다.
         */
        void SerializeBones(Serialization::FMemoryArchive& Archive, Core::TArray<Asset::FMeshImportBone>& Bones) {
            Uint32 BoneCount{Archive.IsLoading() ? 0 : static_cast<Uint32>(Bones.size())};

            if (Archive.HasError() || !Archive.BeginArray("Bones", BoneCount) || BoneCount == 0 || BoneCount > Asset::MaxSkeletonBones || (Archive.IsLoading() && BoneCount > Archive.GetRemainingBytes() / 72)) {
                Archive.SetError("Invalid skeleton bone count");

                return;
            }

            if (Archive.IsLoading()) {
                Bones.clear();
                Bones.reserve(BoneCount);
            }

            for (Index_t Index{}; Index < BoneCount && !Archive.HasError(); ++Index) {
                Core::FString Name{Archive.IsLoading() ? Core::FString{} : Bones[Index].GetName()};
                Int32 Parent{static_cast<Int32>(Archive.IsLoading() ? -1 : Bones[Index].GetParentIndex())};
                FMatrix BindPose{Archive.IsLoading() ? FMatrix{} : Bones[Index].GetBindPose()};

                Archive.Serialize("Name", Name);
                Archive.Serialize("ParentIndex", Parent);
                Archive.Serialize("BindPose", BindPose);

                if (Archive.IsLoading() && !Archive.HasError()) {
                    Bones.emplace_back(std::move(Name), Parent, BindPose);
                }
            }

            Archive.EndArray();
        }

        /**
         * @brief 스켈레톤 본 배열을 바이너리 아카이브에 직렬화합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param Bones 부모 인덱스와 바인드 변환을 가진 본 배열입니다.
         */
        void SerializeSkeleton(Serialization::FMemoryArchive& Archive, Core::TArray<Asset::FMeshImportBone>& Bones) {
            Uint32 Version{1};

            Archive.Serialize("SkeletonVersion", Version);

            if (Archive.HasError() || Version != 1) {
                Archive.SetError("Unsupported skeleton version");

                return;
            }

            SerializeBones(Archive, Bones);
        }

        /**
         * @brief 시퀀스 모델과 루트 모션 추출·고정 설정을 바이너리 아카이브에 직렬화합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param Model 시퀀스 키프레임과 트랙 데이터입니다.
         * @param EnableRootMotion 루트 모션 추출 허용 설정입니다.
         * @param RootLock 시퀀스의 루트 본 고정 기준입니다.
         * @param ForceRootLock 추출 여부와 별개인 루트 고정 설정입니다.
         * @param RootBone 루트 모션 추출 대상 본 이름입니다.
         */
        void SerializeAnimSequence(Serialization::FMemoryArchive& Archive, Asset::FAnimDataModel& Model, bool& EnableRootMotion, Asset::ERootMotionRootLock& RootLock, bool& ForceRootLock, Core::FString& RootBone) {
            Uint32 Version{2};

            Archive.Serialize("AnimSequenceVersion", Version);

            if (Archive.HasError() || (Version != 1 && Version != 2)) {
                Archive.SetError("Unsupported animation sequence version");

                return;
            }

            Asset::FFrameRate FrameRate{Model.GetFrameRate()};
            Uint32 Frames{Model.GetNumberOfFrames()};

            Archive.Serialize("FrameRateNumerator", FrameRate.mNumerator);
            Archive.Serialize("FrameRateDenominator", FrameRate.mDenominator);
            Archive.Serialize("NumberOfFrames", Frames);

            if (Archive.HasError() || !FrameRate.IsValid() || Frames == 0 || Frames > Asset::FAnimDataModel::MaxFrames) {
                Archive.SetError("Invalid animation sequence timing");

                return;
            }

            const std::size_t Keys{static_cast<std::size_t>(Frames) + 1};
            auto& Tracks{Model.GetBoneAnimationTracks()};
            Uint32 TrackCount{Archive.IsLoading() ? 0 : static_cast<Uint32>(Tracks.size())};

            // 트랙 하나는 최소한 이름 길이와 세 채널의 키 수, 키 하나씩(12 + 16 + 12바이트)을 가집니다.
            constexpr std::size_t MinimumTrackBytes{sizeof(Uint32) * 4 + 40};

            if (!Archive.BeginArray("Tracks", TrackCount) || TrackCount == 0 || TrackCount > Asset::FAnimDataModel::MaxTracks || (Archive.IsLoading() && TrackCount > Archive.GetRemainingBytes() / MinimumTrackBytes)) {
                Archive.SetError("Invalid animation sequence track count");

                return;
            }

            if (Archive.IsLoading()) {
                Tracks.assign(TrackCount, {});
            }

            // 채널마다 키 수를 먼저 기록하며, 키 수는 1 또는 프레임 수 + 1만 허용합니다.
            const auto SerializeChannel{[&Archive, Keys](std::string_view Name, auto& Channel, std::size_t KeyBytes) {
                Uint32 Count{Archive.IsLoading() ? 0 : static_cast<Uint32>(Channel.size())};

                if (!Archive.BeginArray(Name, Count) || (Count != 1 && Count != Keys) || (Archive.IsLoading() && Count > Archive.GetRemainingBytes() / KeyBytes)) {
                    Archive.SetError("Invalid animation sequence key count");

                    return;
                }

                if (Archive.IsLoading()) {
                    Channel.resize(Count);
                }

                for (auto& Key : Channel) {
                    if (Archive.HasError()) {
                        break;
                    }

                    Archive.Serialize("Key", Key);
                }

                Archive.EndArray();
            }};

            for (Asset::FBoneAnimationTrack& Track : Tracks) {
                if (Archive.HasError()) {
                    break;
                }

                Archive.Serialize("Name", Track.mName);
                SerializeChannel("PosKeys", Track.mInternalTrack.mPosKeys, sizeof(float) * 3);
                SerializeChannel("RotKeys", Track.mInternalTrack.mRotKeys, sizeof(float) * 4);
                SerializeChannel("ScaleKeys", Track.mInternalTrack.mScaleKeys, sizeof(float) * 3);
            }

            Archive.EndArray();

            if (Version >= 2) {
                Uint32 Lock{static_cast<Uint32>(RootLock)};

                Archive.Serialize("EnableRootMotion", EnableRootMotion);
                Archive.Serialize("RootMotionRootLock", Lock);
                Archive.Serialize("ForceRootLock", ForceRootLock);
                Archive.Serialize("RootMotionBone", RootBone);

                if (Lock > static_cast<Uint32>(Asset::ERootMotionRootLock::Zero) || RootBone.find('\0') != Core::FString::npos) {
                    Archive.SetError("Invalid root motion settings");

                    return;
                }

                RootLock = static_cast<Asset::ERootMotionRootLock>(Lock);
            }

            if (Archive.IsLoading()) {
                Model.SetFrameRate(FrameRate);
                Model.SetNumberOfFrames(Frames);
            }
        }

        /**
         * @brief 메시와 스키닝 가중치를 직렬화하고 구형 본 데이터 읽기를 지원합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param Mesh 호출자가 수명을 보장하는 미리보기 메시입니다.
         * @param SkinWeights 정점별 본 인덱스·가중치 배열입니다.
         * @param PhysicsAssetPath 메시가 참조하는 물리 에셋 경로입니다.
         * @param LegacyBones 구형 파일의 본 데이터를 받을 선택 출력 배열입니다.
         */
        void SerializeSkeletalMesh(Serialization::FMemoryArchive& Archive, Asset::FMeshData& Mesh, Core::TArray<Asset::FMeshImportSkinWeight>& SkinWeights, Core::FString& PhysicsAssetPath, Core::TArray<Asset::FMeshImportBone>* LegacyBones = nullptr, Uint32* OutVersion = nullptr) {
            // 4: 물리 에셋 경로 뒤에 천 에셋과 섹션 연결 데이터가 이어집니다. 그 부분은 메시 객체가 직접 읽고 씁니다.
            Uint32 Version{4};

            Archive.Serialize("SkeletalMeshVersion", Version);

            if (OutVersion != nullptr) {
                *OutVersion = Version;
            }

            if (Archive.HasError() || (Version != 4 && Version != 3 && Version != 2 && (Version != 1 || LegacyBones == nullptr))) {
                Archive.SetError("Unsupported skeletal mesh version");

                return;
            }

            SerializeMesh(Archive, Mesh);

            if (Version == 1) {
                SerializeBones(Archive, *LegacyBones);
            }

            Uint32 VertexCount{Archive.IsLoading() ? 0 : static_cast<Uint32>(SkinWeights.size())};

            Archive.Serialize("SkinWeightCount", VertexCount);

            constexpr std::size_t WeightBytes{Asset::FMeshImportSkinWeight::MaxInfluences * (sizeof(Uint32) + sizeof(float))};

            if (Archive.HasError() || VertexCount != Mesh.GetPositions().size() || VertexCount > 16000000 || (Archive.IsLoading() && VertexCount > Archive.GetRemainingBytes() / WeightBytes)) {
                Archive.SetError("Invalid skeletal mesh skin weight count");

                return;
            }

            if (Archive.IsLoading()) {
                SkinWeights.clear();
                SkinWeights.reserve(VertexCount);
            }

            for (Index_t Vertex{}; Vertex < VertexCount && !Archive.HasError(); ++Vertex) {
                Core::TStaticArray<Uint32, Asset::FMeshImportSkinWeight::MaxInfluences> BoneIndices{Archive.IsLoading() ? Core::TStaticArray<Uint32, Asset::FMeshImportSkinWeight::MaxInfluences>{} : SkinWeights[Vertex].GetBoneIndices()};
                Core::TStaticArray<float, Asset::FMeshImportSkinWeight::MaxInfluences> Weights{Archive.IsLoading() ? Core::TStaticArray<float, Asset::FMeshImportSkinWeight::MaxInfluences>{} : SkinWeights[Vertex].GetWeights()};

                for (Index_t Influence{}; Influence < Asset::FMeshImportSkinWeight::MaxInfluences; ++Influence) {
                    Archive.Serialize("BoneIndex", BoneIndices[Influence]);
                    Archive.Serialize("Weight", Weights[Influence]);
                }

                if (Archive.IsLoading() && !Archive.HasError()) {
                    SkinWeights.emplace_back(BoneIndices, Weights);
                }
            }

            // 버전 3부터 메시가 참조하는 물리 에셋 경로를 함께 저장합니다.
            if (Version >= 3) {
                Archive.Serialize("PhysicsAsset", PhysicsAssetPath);
            }
        }

        /**
         * @brief 배열 개수를 검증하고 원소마다 직렬화 함수를 호출합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param Name 배열의 이름입니다.
         * @param Values 직렬화할 배열이며, 로드 시 개수만큼 기본값으로 채웁니다.
         * @param MinimumBytes 원소 하나가 차지하는 최소 바이트 수입니다.
         * @param SerializeElement 원소 하나를 직렬화하는 함수입니다.
         */
        template <typename TElement, typename TFunction>
        void SerializePhysicsArray(Serialization::FMemoryArchive& Archive, std::string_view Name, Core::TArray<TElement>& Values, std::size_t MinimumBytes, TFunction&& SerializeElement) {
            Uint32 Count{Archive.IsLoading() ? 0 : static_cast<Uint32>(Values.size())};

            if (Archive.HasError() || !Archive.BeginArray(Name, Count) || (Archive.IsLoading() && Count > Archive.GetRemainingBytes() / MinimumBytes)) {
                Archive.SetError("Invalid physics asset array count");

                return;
            }

            if (Archive.IsLoading()) {
                Values.clear();
                Values.resize(Count);
            }

            for (TElement& Value : Values) {
                SerializeElement(Value);

                if (Archive.HasError()) {
                    return;
                }
            }

            Archive.EndArray();
        }

        /**
         * @brief 관절 움직임 열거값을 정수로 직렬화합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param Name 값의 이름입니다.
         * @param Motion 직렬화할 움직임 값입니다.
         */
        template <typename TMotion>
        void SerializeMotion(Serialization::FMemoryArchive& Archive, std::string_view Name, TMotion& Motion) {
            Uint32 Value{static_cast<Uint32>(Motion)};

            Archive.Serialize(Name, Value);

            if (Value > static_cast<Uint32>(TMotion::Locked)) {
                Archive.SetError("Invalid constraint motion");

                return;
            }

            Motion = static_cast<TMotion>(Value);
        }

        /**
         * @brief 물리 에셋의 바디 설계도와 관절 설정을 바이너리 아카이브에 직렬화합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param PhysicsAsset 직렬화할 물리 에셋입니다.
         */
        void SerializePhysicsAsset(Serialization::FMemoryArchive& Archive, PhysicsRuntime::UPhysicsAsset& PhysicsAsset) {
            // 2: 바디마다 SimulatePhysics(Dynamic 여부)를 저장한다. 1은 모든 바디가 Dynamic이다.
            // 3: 바디마다 CollisionEnabled와 충돌 비활성 쌍 목록을 저장한다. 2 이하는 모든 바디가 충돌하고 비활성 쌍이 없다.
            // 4: 에디터 미리보기 스켈레탈 메시 경로를 저장한다. 3 이하는 에셋 이름 규칙으로 메시를 찾는다.
            // 5: 바디마다 선형 감쇠, 회전 감쇠, 질량, 관성 배율을 저장한다. 4 이하는 바디 셋업의 기본값을 쓴다.
            // 6: 바디마다 바람 영향(WindInfluence)을 저장한다. 5 이하는 0(바람을 받지 않음)이다.
            Uint32 Version{6};

            Archive.Serialize("PhysicsAssetVersion", Version);

            if (Archive.HasError() || Version < 1 || Version > 6) {
                Archive.SetError("Unsupported physics asset version");

                return;
            }

            SerializePhysicsArray(Archive, "Bodies", PhysicsAsset.mSkeletalBodySetups, 16, [&Archive, Version](std::unique_ptr<PhysicsRuntime::UBodySetup>& BodySetup) {
                if (Archive.IsLoading()) {
                    BodySetup = CoreUObject::NewObject<PhysicsRuntime::UBodySetup>();
                }

                PhysicsRuntime::FKAggregateGeom& AggGeom{BodySetup->GetAggGeom()};
                Core::FString BoneName{BodySetup->GetBoneName()};

                Archive.Serialize("BoneName", BoneName);

                if (Archive.IsLoading()) {
                    BodySetup->SetBoneName(BoneName);
                }

                if (Version >= 2) {
                    Uint32 Simulate{BodySetup->ShouldSimulatePhysics() ? 1u : 0u};

                    Archive.Serialize("SimulatePhysics", Simulate);

                    if (Archive.IsLoading()) {
                        BodySetup->SetSimulatePhysics(Simulate != 0);
                    }
                }

                if (Version >= 3) {
                    Uint32 Collision{BodySetup->IsCollisionEnabled() ? 1u : 0u};

                    Archive.Serialize("CollisionEnabled", Collision);

                    if (Archive.IsLoading()) {
                        BodySetup->SetCollisionEnabled(Collision != 0);
                    }
                }

                if (Version >= 5) {
                    float LinearDamping{BodySetup->GetLinearDamping().GetBase()};
                    float AngularDamping{BodySetup->GetAngularDamping().GetBase()};
                    float Mass{BodySetup->GetMassInKg().GetBase()};
                    float InertiaScale{BodySetup->GetInertiaScale()};

                    Archive.Serialize("LinearDamping", LinearDamping);
                    Archive.Serialize("AngularDamping", AngularDamping);
                    Archive.Serialize("MassInKg", Mass);
                    Archive.Serialize("InertiaScale", InertiaScale);

                    if (Archive.IsLoading()) {
                        // 잘못된 값은 Setter가 무시하므로 기본값이 남는다.
                        BodySetup->SetLinearDamping(Core::FFrequency::FromBase(LinearDamping));
                        BodySetup->SetAngularDamping(Core::FFrequency::FromBase(AngularDamping));
                        BodySetup->SetMassInKg(Core::FMass::FromBase(Mass));
                        BodySetup->SetInertiaScale(InertiaScale);
                    }
                }

                if (Version >= 6) {
                    float WindInfluence{BodySetup->GetWindInfluence()};

                    Archive.Serialize("WindInfluence", WindInfluence);

                    if (Archive.IsLoading()) {
                        BodySetup->SetWindInfluence(WindInfluence);
                    }
                }

                SerializePhysicsArray(Archive, "Spheres", AggGeom.mSphereElems, 16, [&Archive](PhysicsRuntime::FKSphereElem& Elem) {
                    Archive.Serialize("Center", Elem.mCenter);
                    Archive.Serialize("Radius", Elem.mRadius);
                });

                SerializePhysicsArray(Archive, "Boxes", AggGeom.mBoxElems, 36, [&Archive](PhysicsRuntime::FKBoxElem& Elem) {
                    Archive.Serialize("Center", Elem.mCenter);
                    Archive.Serialize("Rotation", Elem.mRotation);
                    Archive.Serialize("HalfExtent", Elem.mHalfExtent);
                });

                SerializePhysicsArray(Archive, "Capsules", AggGeom.mSphylElems, 32, [&Archive](PhysicsRuntime::FKSphylElem& Elem) {
                    Archive.Serialize("Center", Elem.mCenter);
                    Archive.Serialize("Rotation", Elem.mRotation);
                    Archive.Serialize("Radius", Elem.mRadius);
                    Archive.Serialize("Length", Elem.mLength);
                });
            });

            SerializePhysicsArray(Archive, "Constraints", PhysicsAsset.mConstraintSetup, 64, [&Archive](std::unique_ptr<PhysicsRuntime::UPhysicsConstraintTemplate>& ConstraintSetup) {
                if (Archive.IsLoading()) {
                    ConstraintSetup = CoreUObject::NewObject<PhysicsRuntime::UPhysicsConstraintTemplate>();
                }

                PhysicsRuntime::FConstraintInstance& Instance{ConstraintSetup->mDefaultInstance};

                Archive.Serialize("JointName", Instance.mJointName);
                Archive.Serialize("ConstraintBone1", Instance.mConstraintBone1);
                Archive.Serialize("ConstraintBone2", Instance.mConstraintBone2);
                Archive.Serialize("Pos1", Instance.mPos1);
                Archive.Serialize("PriAxis1", Instance.mPriAxis1);
                Archive.Serialize("SecAxis1", Instance.mSecAxis1);
                Archive.Serialize("Pos2", Instance.mPos2);
                Archive.Serialize("PriAxis2", Instance.mPriAxis2);
                Archive.Serialize("SecAxis2", Instance.mSecAxis2);
                SerializeMotion(Archive, "LinearXMotion", Instance.mLinearXMotion);
                SerializeMotion(Archive, "LinearYMotion", Instance.mLinearYMotion);
                SerializeMotion(Archive, "LinearZMotion", Instance.mLinearZMotion);
                SerializeMotion(Archive, "Swing1Motion", Instance.mSwing1Motion);
                SerializeMotion(Archive, "Swing2Motion", Instance.mSwing2Motion);
                SerializeMotion(Archive, "TwistMotion", Instance.mTwistMotion);
                Archive.Serialize("Swing1LimitDegrees", Instance.mSwing1Limit, Core::QuantityUnits::Degree);
                Archive.Serialize("Swing2LimitDegrees", Instance.mSwing2Limit, Core::QuantityUnits::Degree);
                Archive.Serialize("TwistLimitDegrees", Instance.mTwistLimit, Core::QuantityUnits::Degree);
            });

            if (Version >= 3) {
                SerializePhysicsArray(Archive, "CollisionDisabledPairs", PhysicsAsset.mCollisionDisabledPairs, 8, [&Archive](std::pair<Core::FString, Core::FString>& Pair) {
                    Archive.Serialize("BoneA", Pair.first);
                    Archive.Serialize("BoneB", Pair.second);
                });
            }

            if (Version >= 4) {
                Archive.Serialize("PreviewMesh", PhysicsAsset.mPreviewMeshPath);
            }

            if (Archive.IsLoading() && !Archive.HasError()) {
                PhysicsAsset.UpdateBodySetupIndexMap();
            }
        }

    }

    /**
     * @brief 전달받은 설정과 의존성으로 FAssetManager의 초기 상태를 구성합니다.
     */
    Asset::FAssetManager::FAssetManager(std::filesystem::path ContentRoot)
		: mContentRoot{std::filesystem::absolute(ContentRoot).lexically_normal()},
		  mEngineContentRoot{Core::FilePath::GetEngineContentRoot()},
		  mEngineShaderRoot{Core::FilePath::GetEngineShaderRoot()} {
    }

    /**
     * @brief 객체의 수명을 끝내고 소유한 자원을 정리합니다.
     */
    Asset::FAssetManager::~FAssetManager() = default;

    /**
     * @brief 에셋 경로의 기준이 되는 콘텐츠 폴더를 반환합니다.
     */
    const std::filesystem::path& Asset::FAssetManager::GetContentRoot() const {
        return mContentRoot;
    }

    const std::filesystem::path& Asset::FAssetManager::GetEngineContentRoot() const {
        return mEngineContentRoot;
    }

    const std::filesystem::path& Asset::FAssetManager::GetEngineShaderRoot() const {
        return mEngineShaderRoot;
    }

    /**
     * @brief 메시를 임포트할 때 볼록 충돌 형상을 계산할 PhysX 런타임을 설정합니다.
     */
    void Asset::FAssetManager::SetPhysicsRuntime(PhysicsRuntime::FPhysicsRuntime* Runtime) {
        mPhysicsRuntime = Runtime;
    }

    void FAssetManager::SetAssetSavedHandler(std::function<void(const std::filesystem::path&, Asset::EAssetType)> Handler) {
        mAssetSaved = std::move(Handler);
    }

    bool FAssetManager::MigrateAsset(const std::filesystem::path& Path, Core::FString& Error) {
        std::filesystem::path ReferencePath{};
        const auto ResolvedPath{ResolveAssetPath(Path, ReferencePath, Error)};
        if (ResolvedPath.empty()) {
            return false;
        }

        if (ReferencePath.has_root_directory() || ResolvedPath.extension() != L".masset") {
            return true;
        }

        Asset::EAssetType Type{};
        bool Migrated{};
        if (!Asset::FAssetFile::Migrate(ResolvedPath, Type, Migrated, Error)) {
            return false;
        }

        if (Migrated && mAssetSaved) {
            mAssetSaved(ResolvedPath, Type);
        }

        return true;
    }

    bool FAssetManager::SaveAssetFile(const std::filesystem::path& Path, Asset::EAssetType Type, std::span<const Uint8> Payload, Core::FString& Error, const Core::FGuid& Guid) {
        if (!Asset::FAssetFile::Save(Path, Type, Payload, Error, Guid)) {
            return false;
        }

        if (mAssetSaved) {
            mAssetSaved(Path, Type);
        }

        return true;
    }

    /**
     * @brief 이 관리자가 보유한 메시의 경로를 조회합니다.
     */
    Core::FString Asset::FAssetManager::GetMeshPath(const Asset::UStaticMesh* Mesh) const {
        for (const auto& Entry : mMeshes) {
            if (Entry.second.get() == Mesh) {
                return Entry.first;
            }
        }

        return {};
    }

    /**
     * @brief 캐시에 등록된 스켈레탈 메시의 프로젝트 상대 경로를 반환합니다.
     */
    Core::FString Asset::FAssetManager::GetSkeletalMeshPath(const Asset::USkeletalMesh* Mesh) const {
        for (const auto& Entry : mSkeletalMeshes) {
            if (Entry.second.get() == Mesh) {
                return Entry.first;
            }
        }

        return {};
    }

    /**
     * @brief 이 관리자의 캐시에서 텍스처에 대응하는 에셋 경로를 조회합니다.
     */
    Core::FString Asset::FAssetManager::GetTexturePath(const Asset::UTexture2D* Texture) const {
        for (const auto& Entry : mTextures) {
            if (Entry.second.get() == Texture) {
                return Entry.first;
            }
        }

        return {};
    }

    Core::FString Asset::FAssetManager::GetMaterialPath(const Asset::UMaterial* Material) const {
        for (const auto& Entry : mMaterials) {
            if (Entry.second.get() == Material) {
                return Entry.first;
            }
        }

        return {};
    }

    std::filesystem::path Asset::FAssetManager::MakeUniqueAssetPath(const std::filesystem::path& Directory, const Core::FString& Name, Core::FString& Error) const {
        Error.clear();

        const auto Folder{Directory.empty() || Directory == L"." ? mContentRoot : ResolveWritablePath(Directory, Error)};

        return Folder.empty() ? std::filesystem::path{} : UniqueAssetPath(Folder, AssetFileName(Name, "Asset"), Error);
    }

    /**
     * @brief 콘텐츠 폴더를 벗어나는 경로를 거부하고 정규화된 절대 경로를 반환합니다.
     */
    std::filesystem::path Asset::FAssetManager::ResolvePath(const std::filesystem::path& Path, Core::FString& Error) const {
        std::filesystem::path ReferencePath{};

        return ResolveAssetPath(Path, ReferencePath, Error);
    }

    std::filesystem::path Asset::FAssetManager::ResolveRedirectedPath(const std::filesystem::path& Path, Core::TArray<std::filesystem::path>& Redirectors, Core::FString& Error) const {
        std::filesystem::path ReferencePath{};
        Redirectors.clear();

        return ResolveAssetPath(Path, ReferencePath, Error, &Redirectors);
    }

    bool Asset::FAssetManager::RewriteBinaryAssetReferences(Asset::EAssetType Type, Core::TArray<Uint8>& Payload, const std::function<Core::FString(const Core::FString&, bool)>& Remap, bool& Changed, Core::FString& Error) const {
        struct FStringReplacement {
            std::size_t mOffset{};
            std::size_t mLength{};
            Core::FString mValue{};
        };

        Changed = false;
        Core::TArray<FStringReplacement> Replacements{};
        Serialization::FMemoryArchive Archive{std::as_const(Payload)};
        Archive.SetStringReader([&](std::string_view Name, const Core::FString& Value, std::size_t Offset) {
            const bool IsMaterialPath{Type == Asset::EAssetType::Material && (Name == "Shader" || Name == "BaseColorTexture" || Name == "NormalMap" || Name == "SpecularTexture" || Name == "EmissiveTexture" || Name == "ReflectionTexture")};
            const bool IsMeshPath{(Type == Asset::EAssetType::Mesh || Type == Asset::EAssetType::SkeletalMesh) && (Name == "Material" || Name == "PhysicsAsset" || Name == "PhysicsAssetPath")};
            const bool IsShaderPath{Type == Asset::EAssetType::Shader && Name == "Source"};
            auto Replacement{Remap(Value, IsMaterialPath || IsMeshPath || IsShaderPath)};
            if (!Replacement.empty()) {
                Replacements.push_back({Offset, Value.size(), std::move(Replacement)});
            }
        });

        if (Type == Asset::EAssetType::Material) {
            Asset::FMaterialAssetData Material{};
            Material.Serialize(Archive);
        } else if (Type == Asset::EAssetType::Mesh) {
            Asset::FMeshData Mesh{};
            SerializeMesh(Archive, Mesh);
            if (Archive.GetRemainingBytes() != 0 && !Archive.HasError()) {
                Core::TArray<PhysicsRuntime::FKConvexElem> Collision{};
                PhysicsRuntime::FKAggregateGeom SimpleCollision{};
                SerializeCollision(Archive, Collision, SimpleCollision);
            }
        } else if (Type == Asset::EAssetType::SkeletalMesh) {
            Asset::FMeshData MeshData{};
            Core::TArray<Asset::FMeshImportBone> Bones{};
            Core::TArray<Asset::FMeshImportSkinWeight> Weights{};
            Core::FString PhysicsAsset{};
            Uint32 Version{};
            SerializeSkeletalMesh(Archive, MeshData, Weights, PhysicsAsset, &Bones, &Version);
            if (Version >= 4 && !Archive.HasError()) {
                auto Mesh{CoreUObject::NewObject<Asset::USkeletalMesh>()};
                if (!Mesh->SetMeshData(std::move(MeshData), std::move(Weights))) {
                    Error = "Invalid skeletal mesh geometry";
                    return false;
                }
                Mesh->SerializeClothingData(Archive);
            }
        } else if (Type == Asset::EAssetType::Shader) {
            Uint32 Version{};
            Archive.Serialize("Version", Version);
            if (Version != 1) {
                Error = "Unsupported shader asset version";
                return false;
            }
            auto Shader{CoreUObject::NewObject<RenderCore::UShader>()};
            Shader->Serialize(Archive);
        } else {
            return true;
        }

        if (Archive.HasError() || Archive.GetRemainingBytes() != 0) {
            Error = Archive.HasError() ? Archive.GetError() : "Unexpected binary asset data";
            return false;
        }

        if (!Replacements.empty()) {
            Core::TArray<Uint8> Result{};
            Result.reserve(Payload.size());
            std::size_t Previous{};
            for (const auto& Replacement : Replacements) {
                Result.insert(Result.end(), Payload.data() + Previous, Payload.data() + Replacement.mOffset);
                const Uint32 Length{static_cast<Uint32>(Replacement.mValue.size())};
                const auto* Bytes{reinterpret_cast<const Uint8*>(&Length)};
                Result.insert(Result.end(), Bytes, Bytes + sizeof(Length));
                Result.insert(Result.end(), Replacement.mValue.begin(), Replacement.mValue.end());
                Previous = Replacement.mOffset + sizeof(Length) + Replacement.mLength;
            }
            Result.insert(Result.end(), Payload.data() + Previous, Payload.data() + Payload.size());
            Payload = std::move(Result);
            Changed = true;
        }

        return true;
    }

    std::filesystem::path Asset::FAssetManager::ResolveWritablePath(const std::filesystem::path& Path, Core::FString& Error) const {
        std::filesystem::path ReferencePath{};
        const auto Resolved{ResolveContentPath(Path, ReferencePath, Error)};

        if (ReferencePath.has_root_directory()) {
            Error = "Engine content is read-only";

            return {};
        }

        return Resolved;
    }

    bool Asset::FAssetManager::IsRedirectorDirectory(const std::filesystem::path& Path) const {
        if (!IsAssetRedirector(Path / DirectoryRedirectorName)) {
            return false;
        }

        std::error_code Code{};
        std::filesystem::directory_iterator Iterator{Path, Code};
        const std::filesystem::directory_iterator End{};
        if (Code || Iterator == End || Iterator->path().filename() != DirectoryRedirectorName) {
            return false;
        }

        return Iterator.increment(Code) == End && !Code;
    }

    std::filesystem::path Asset::FAssetManager::ResolveAssetPath(const std::filesystem::path& Path, std::filesystem::path& ReferencePath, Core::FString& Error, Core::TArray<std::filesystem::path>* Redirectors) const {
        auto Resolved{ResolveContentPath(Path, ReferencePath, Error)};
        Core::TArray<std::filesystem::path> Visited{};

        while (!Resolved.empty() && !ReferencePath.has_root_directory()) {
            std::filesystem::path Suffix{};
            const auto Redirector{FindContentRedirector(Resolved, ReferencePath, Suffix)};
            if (Redirector.empty()) {
                std::error_code Code{};
                if (Visited.empty() || std::filesystem::exists(Resolved, Code)) {
                    return Resolved;
                }

                Error = "The redirector target does not exist";
                break;
            }

            const auto Key{Core::FilePath::ToLower(Resolved)};
            if (Visited.size() == 64 || std::ranges::find(Visited, Key) != Visited.end()) {
                Error = "Circular or excessive asset redirection";
                break;
            }
            Visited.push_back(Key);
            if (Redirectors != nullptr) {
                Redirectors->push_back(Redirector);
            }

            Core::TArray<Uint8> Data{};
            if (!Asset::FAssetFile::Load(Redirector, Asset::EAssetType::Redirector, Data, Error)) {
                break;
            }

            std::filesystem::path Target{};
            try {
                Target = Core::FilePath::FromUtf8(std::string_view{reinterpret_cast<const char*>(Data.data()), Data.size()});
            } catch (const std::filesystem::filesystem_error&) {
                Error = "Invalid asset redirector target";
                break;
            }

            if (Target.has_root_path() || Core::FilePath::GetContentMount(Target) != Core::FilePath::EContentMount::Game || Target == L".") {
                Error = "Invalid asset redirector target";
                break;
            }

            Resolved = ResolveContentPath(Suffix.empty() ? Target : Target / Suffix, ReferencePath, Error);
        }

        if (!Visited.empty() || Resolved.empty()) {
            ReferencePath.clear();
            return {};
        }

        return Resolved;
    }

    std::filesystem::path Asset::FAssetManager::ResolveAssetLoadPath(const std::filesystem::path& Path, Core::FString& Key, Core::FString& Error) const {
        std::filesystem::path ReferencePath{};
        const auto Resolved{ResolveAssetPath(Path, ReferencePath, Error)};
        Key = CacheKey(Core::FilePath::ToGenericUtf8(ReferencePath));

        return Resolved;
    }

    std::filesystem::path Asset::FAssetManager::FindContentRedirector(const std::filesystem::path& Resolved, const std::filesystem::path& ReferencePath, std::filesystem::path& Suffix) const {
        if (Resolved.extension() == L".masset") {
            Asset::EAssetType Type{};
            Core::FString Error{};
            if (Asset::FAssetFile::GetType(Resolved, Type, Error)) {
                return Type == Asset::EAssetType::Redirector ? Resolved : std::filesystem::path{};
            }
        }

        std::error_code Code{};
        const auto Status{std::filesystem::status(Resolved, Code)};
        if (std::filesystem::is_directory(Status) && IsRedirectorDirectory(Resolved)) {
            return Resolved / DirectoryRedirectorName;
        }

        if (std::filesystem::exists(Status) || !std::filesystem::status_known(Status)) {
            return {};
        }

        if (IsTextureSource(Resolved)) {
            auto Candidate{Resolved};
            Candidate += L".masset";
            if (IsAssetRedirector(Candidate)) {
                return Candidate;
            }
        }

        for (auto Parent{ReferencePath.parent_path()}; !Parent.empty(); Parent = Parent.parent_path()) {
            const auto Candidate{mContentRoot / Parent / DirectoryRedirectorName};
            if (IsAssetRedirector(Candidate)) {
                Suffix = ReferencePath.lexically_relative(Parent);
                return Candidate;
            }
        }

        return {};
    }

    std::filesystem::path Asset::FAssetManager::ResolveContentPath(const std::filesystem::path& Path, std::filesystem::path& ReferencePath, Core::FString& Error) const {
        Error.clear();
        ReferencePath.clear();

        const bool Absolute{Path.is_absolute()};
        std::filesystem::path Relative{};
        const auto Mount{Absolute ? Core::FilePath::EContentMount::Invalid : Core::FilePath::GetContentMount(Path, &Relative)};

        if (Path.native().find(L'\0') != std::wstring::npos || (!Absolute && Mount == Core::FilePath::EContentMount::Invalid)) {
            Error = "Invalid content path";

            return {};
        }

        const bool EngineMount{Mount == Core::FilePath::EContentMount::Engine};
        const std::filesystem::path ShaderFolder{L"Shaders"};
        const auto MountedShaderPath{EngineMount ? GetPathWithinRoot(ShaderFolder, Relative.lexically_normal()) : std::filesystem::path{}};
        const bool EngineShaderMount{!MountedShaderPath.empty()};
        std::error_code Code{};
        const auto ProjectRoot{std::filesystem::weakly_canonical(mContentRoot, Code)};
        const auto EngineRoot{mEngineContentRoot.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(mEngineContentRoot, Code)};
        const auto ShaderRoot{mEngineShaderRoot.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(mEngineShaderRoot, Code)};
        const auto& Root{EngineShaderMount ? ShaderRoot : EngineMount ? EngineRoot : ProjectRoot};

        if (!Absolute && Root.empty()) {
            Error = "Cannot locate the content directory";

            return {};
        }

        Code.clear();
        const auto Resolved{std::filesystem::weakly_canonical(Absolute ? Path : Root / (EngineShaderMount ? MountedShaderPath : Relative), Code)};

        if (Code || Resolved.empty()) {
            Error = "Cannot resolve the content path";

            return {};
        }

        const auto ShaderRelative{GetPathWithinRoot(ShaderRoot, Resolved)};

        if (!ShaderRelative.empty() && (Absolute || EngineShaderMount)) {
            ReferencePath = L"/Engine/Shaders";

            if (ShaderRelative != L".") {
                ReferencePath /= ShaderRelative;
            }

            return Resolved;
        }

        const auto EngineRelative{GetPathWithinRoot(EngineRoot, Resolved)};

        if (!EngineRelative.empty() && ShaderRelative.empty() && (Absolute || (EngineMount && !EngineShaderMount)) && GetPathWithinRoot(ShaderFolder, EngineRelative).empty()) {
            ReferencePath = L"/Engine";

            if (EngineRelative != L".") {
                ReferencePath /= EngineRelative;
            }

            return Resolved;
        }

        if (EngineRelative.empty() && ShaderRelative.empty() && !EngineMount) {
            ReferencePath = GetPathWithinRoot(ProjectRoot, Resolved);

            if (!ReferencePath.empty()) {
                return Resolved;
            }
        }

        Error = "The path is outside its content directory";

        return {};
    }

    std::filesystem::path Asset::FAssetManager::GetPathWithinRoot(const std::filesystem::path& Root, const std::filesystem::path& Path) const {
        if (Root.empty()) {
            return {};
        }

        auto Part{Path.begin()};

        for (const auto& RootPart : Root) {
            if (Part == Path.end() || _wcsicmp(RootPart.c_str(), Part->c_str()) != 0) {
                return {};
            }

            ++Part;
        }

        std::filesystem::path Relative{};

        for (; Part != Path.end(); ++Part) {
            Relative /= *Part;
        }

        return Relative.empty() ? std::filesystem::path{L"."} : Relative;
    }

    /**
     * @brief 경로가 콘텐츠 폴더 안에 있는지 검증하고 UTF-8 상대 경로를 반환합니다.
     */
    Core::FString Asset::FAssetManager::GetRelativePath(const std::filesystem::path& Path, Core::FString& Error) const {
        std::filesystem::path ReferencePath{};
        ResolveContentPath(Path, ReferencePath, Error);

        return Core::FilePath::ToGenericUtf8(ReferencePath);
    }

    /**
     * @brief 콘텐츠 폴더 아래의 .masset 파일을 찾아 상대 경로순으로 반환합니다.
     */
    Core::TArray<std::filesystem::path> Asset::FAssetManager::GetAssetPaths(Core::FString& Error) const {
        Error.clear();

        Core::TArray<std::filesystem::path> Paths{};
        for (const auto* Root : {&mContentRoot, &mEngineContentRoot}) {
            std::error_code Code{};
            if (Root->empty() || (!std::filesystem::exists(*Root, Code) && !Code)) {
                continue;
            }

            std::filesystem::recursive_directory_iterator Iterator{*Root, Code};
            const std::filesystem::recursive_directory_iterator End{};

            while (!Code && Iterator != End) {
                if (Iterator->is_symlink(Code)) {
                    Iterator.disable_recursion_pending();
                } else if (!Code && Iterator->is_regular_file(Code) && Core::FilePath::ToLowerExtension(Iterator->path()) == L".masset") {
                    const auto Reference{GetRelativePath(Iterator->path(), Error)};

                    if (Reference.empty()) {
                        return {};
                    }

                    if (!IsAssetRedirector(Iterator->path())) {
                        Paths.push_back(Core::FilePath::FromUtf8(Reference));
                    }
                }

                if (!Code) {
                    Iterator.increment(Code);
                }
            }

            if (Code) {
                Error = "Cannot enumerate content assets: " + Code.message();

                return {};
            }
        }

        std::sort(Paths.begin(), Paths.end());

        return Paths;
    }

    /**
     * @brief 메시 에셋을 캐시에서 조회하거나 파일에서 로드하여 보관합니다.
     */
    Asset::UStaticMesh* Asset::FAssetManager::LoadMesh(const std::filesystem::path& Path, Core::FString& Error) {
        const Core::FScopedStatTimer StatTimer{Asset::AssetStats::MeshLoad};
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mMeshes.find(Key)};

        if (Existing != mMeshes.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::Mesh, Payload, Error)) {
            return nullptr;
        }

        Serialization::FMemoryArchive Archive{static_cast<const Core::TArray<Uint8>&>(Payload)};
        Asset::FMeshData Data{};
        Core::TArray<PhysicsRuntime::FKConvexElem> ConvexElems{};
        PhysicsRuntime::FKAggregateGeom SimpleShapes{};

        SerializeMesh(Archive, Data);

        // 충돌 섹션은 이후 버전에서 추가했으므로 없으면 경계 상자를 사용합니다.
        if (!Archive.HasError() && Archive.GetRemainingBytes() != 0) {
            SerializeCollision(Archive, ConvexElems, SimpleShapes);
        }

        auto Mesh{CoreUObject::NewObject<Asset::UStaticMesh>()};

        // 메시 데이터를 설정하면 충돌 형상이 지워지므로 충돌 형상은 그 뒤에 설정합니다.
        if (Archive.HasError() || Archive.GetRemainingBytes() != 0 || !Mesh->SetMeshData(std::move(Data)) || !Mesh->SetConvexElems(std::move(ConvexElems)) || !Mesh->SetSimpleShapes(std::move(SimpleShapes))) {
            Error = Archive.HasError() ? Archive.GetError() : "Invalid mesh asset data";

            return nullptr;
        }

        if (!RestoreAssetIdentity(ResolvedPath, *Mesh, Error)) {
            return nullptr;
        }

        Asset::UStaticMesh* Result{Mesh.get()};

        Mesh->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));
        mMeshes.emplace(Key, std::move(Mesh));

        return Result;
    }

    /**
     * @brief 스켈레탈 메시 파일에서 본·스킨 가중치·기하 데이터를 읽어 캐시에 등록합니다.
     */
    Asset::USkeletalMesh* Asset::FAssetManager::LoadSkeletalMesh(const std::filesystem::path& Path, Core::FString& Error) {
        const Core::FScopedStatTimer StatTimer{Asset::AssetStats::MeshLoad};
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mSkeletalMeshes.find(Key)};

        if (Existing != mSkeletalMeshes.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::SkeletalMesh, Payload, Error)) {
            return nullptr;
        }

        Serialization::FMemoryArchive Archive{static_cast<const Core::TArray<Uint8>&>(Payload)};
        Asset::FMeshData Data{};
        Core::TArray<Asset::FMeshImportBone> LegacyBones{};
        Core::TArray<Asset::FMeshImportSkinWeight> SkinWeights{};
        Core::FString PhysicsAssetPath{};

        Uint32 SkeletalMeshVersion{};

        SerializeSkeletalMesh(Archive, Data, SkinWeights, PhysicsAssetPath, &LegacyBones, &SkeletalMeshVersion);

        auto Mesh{CoreUObject::NewObject<Asset::USkeletalMesh>()};

        if (Archive.HasError() || !Mesh->SetMeshData(std::move(Data), std::move(SkinWeights))) {
            Error = Archive.HasError() ? Archive.GetError() : "Invalid skeletal mesh asset data";

            return nullptr;
        }

        // 버전 4부터 천 데이터가 이어지며, 섹션 수를 검사하려고 형상을 넣은 뒤에 읽습니다.
        if (SkeletalMeshVersion >= 4) {
            Mesh->SerializeClothingData(Archive);
        }

        if (Archive.HasError() || Archive.GetRemainingBytes() != 0) {
            Error = Archive.HasError() ? Archive.GetError() : "Invalid skeletal mesh asset data";

            return nullptr;
        }

        Mesh->SetPhysicsAssetPath(std::move(PhysicsAssetPath));

        // 천 에셋은 물리 에셋을 경로로만 저장하므로 여기서 찾아 연결합니다.
        for (const std::unique_ptr<PhysicsRuntime::UClothingAssetCommon>& ClothingAsset : Mesh->GetMeshClothingAssets()) {
            if (ClothingAsset->mPhysicsAssetPath.empty()) {
                continue;
            }

            Core::FString PhysicsAssetError{};

            if (PhysicsRuntime::UPhysicsAsset* ClothPhysicsAsset{LoadPhysicsAsset(Core::FilePath::FromUtf8(ClothingAsset->mPhysicsAssetPath), PhysicsAssetError)}) {
                ClothingAsset->mPhysicsAsset = CoreUObject::MakeObjectRef(ClothPhysicsAsset);
            } else {
                Core::ErrorHandler::Report("Cloth", "Cannot load the cloth physics asset '" + ClothingAsset->mPhysicsAssetPath + "': " + PhysicsAssetError, Core::ErrorHandler::EErrorLevel::Warning);
            }
        }

        if (!RestoreAssetIdentity(ResolvedPath, *Mesh, Error)) {
            return nullptr;
        }

        Asset::USkeletalMesh* Result{Mesh.get()};

        Mesh->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));

        if (!LegacyBones.empty()) {
            auto Skeleton{CoreUObject::NewObject<Asset::USkeleton>()};

            if (!Skeleton->SetBones(std::move(LegacyBones))) {
                Error = "Invalid embedded skeleton data";

                return nullptr;
            }

            Skeleton->SetName(Mesh->GetName());
            mLegacySkeletons.emplace(Key, std::move(Skeleton));
        }

        mSkeletalMeshes.emplace(Key, std::move(Mesh));

        return Result;
    }

    Asset::USkeleton* Asset::FAssetManager::LoadSkeleton(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mSkeletons.find(Key)};

        if (Existing != mSkeletons.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::Skeleton, Payload, Error)) {
            return nullptr;
        }

        Serialization::FMemoryArchive Archive{static_cast<const Core::TArray<Uint8>&>(Payload)};
        Core::TArray<Asset::FMeshImportBone> Bones{};

        SerializeSkeleton(Archive, Bones);

        auto Skeleton{CoreUObject::NewObject<Asset::USkeleton>()};

        if (Archive.HasError() || Archive.GetRemainingBytes() != 0 || !Skeleton->SetBones(std::move(Bones))) {
            Error = Archive.HasError() ? Archive.GetError() : "Invalid skeleton asset data";

            return nullptr;
        }

        if (!RestoreAssetIdentity(ResolvedPath, *Skeleton, Error)) {
            return nullptr;
        }

        Asset::USkeleton* Result{Skeleton.get()};

        Skeleton->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));
        mSkeletons.emplace(Key, std::move(Skeleton));

        return Result;
    }

    Asset::USkeleton* Asset::FAssetManager::FindLegacySkeleton(const std::filesystem::path& MeshPath) const {
        Core::FString Error{};
        Core::FString Key{};
        ResolveAssetLoadPath(MeshPath, Key, Error);
        const auto Existing{mLegacySkeletons.find(Key)};

        return Existing != mLegacySkeletons.end() ? Existing->second.get() : nullptr;
    }

    /**
     * @brief 물리 에셋을 캐시에서 찾거나 파일에서 읽어 캐시에 보관합니다.
     */
    PhysicsRuntime::UPhysicsAsset* Asset::FAssetManager::LoadPhysicsAsset(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mPhysicsAssets.find(Key)};

        if (Existing != mPhysicsAssets.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::PhysicsAsset, Payload, Error)) {
            return nullptr;
        }

        Serialization::FMemoryArchive Archive{static_cast<const Core::TArray<Uint8>&>(Payload)};
        auto PhysicsAsset{CoreUObject::NewObject<PhysicsRuntime::UPhysicsAsset>()};

        SerializePhysicsAsset(Archive, *PhysicsAsset);

        if (Archive.HasError() || Archive.GetRemainingBytes() != 0) {
            Error = Archive.HasError() ? Archive.GetError() : "Invalid physics asset data";

            return nullptr;
        }

        if (!RestoreAssetIdentity(ResolvedPath, *PhysicsAsset, Error)) {
            return nullptr;
        }

        PhysicsRuntime::UPhysicsAsset* Result{PhysicsAsset.get()};

        PhysicsAsset->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));
        mPhysicsAssets.emplace(Key, std::move(PhysicsAsset));

        return Result;
    }

    Asset::UAnimSequence* Asset::FAssetManager::LoadAnimSequence(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mAnimSequences.find(Key)};

        if (Existing != mAnimSequences.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::AnimSequence, Payload, Error)) {
            return nullptr;
        }

        Serialization::FMemoryArchive Archive{static_cast<const Core::TArray<Uint8>&>(Payload)};
        Asset::FAnimDataModel Model{};
        bool EnableRootMotion{};
        Asset::ERootMotionRootLock RootLock{Asset::ERootMotionRootLock::AnimFirstFrame};
        bool ForceRootLock{};
        Core::FString RootBone{};

        SerializeAnimSequence(Archive, Model, EnableRootMotion, RootLock, ForceRootLock, RootBone);

        if (Archive.HasError() || Archive.GetRemainingBytes() != 0) {
            Error = Archive.HasError() ? Archive.GetError() : "Invalid animation sequence asset data";

            return nullptr;
        }

        auto Sequence{CoreUObject::NewObject<Asset::UAnimSequence>()};

        if (!Sequence->SetDataModel(std::move(Model), Error)) {
            return nullptr;
        }

        if (!RestoreAssetIdentity(ResolvedPath, *Sequence, Error)) {
            return nullptr;
        }

        Asset::UAnimSequence* Result{Sequence.get()};

        Sequence->SetRootMotionEnabled(EnableRootMotion);
        Sequence->SetRootMotionRootLock(RootLock);
        Sequence->SetForceRootLock(ForceRootLock);
        Sequence->SetRootMotionBone(RootBone);
        Sequence->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));
        Sequence->mAssetPath = GetRelativePath(ResolvedPath, Error);
        mAnimSequences.emplace(Key, std::move(Sequence));

        return Result;
    }

    /**
     * @brief 텍스처 에셋을 캐시에서 조회하거나 파일에서 로드하여 보관합니다.
     */
    Asset::UTexture2D* Asset::FAssetManager::LoadTexture(const std::filesystem::path& Path, Core::FString& Error) {
        const Core::FScopedStatTimer StatTimer{Asset::AssetStats::TextureLoad};
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mTextures.find(Key)};

        if (Existing != mTextures.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        if (ResolvedPath.extension() != L".masset") {
            auto Texture{CoreUObject::NewObject<Asset::UTexture2D>()};

            if (!Texture->LoadFromFile(ResolvedPath)) {
                Error = "Cannot load the material texture";

                return nullptr;
            }

            Asset::UTexture2D* Result{Texture.get()};

            mTextures.emplace(Key, std::move(Texture));

            return Result;
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::Texture, Payload, Error)) {
            return nullptr;
        }

        Serialization::FMemoryArchive Archive{static_cast<const Core::TArray<Uint8>&>(Payload)};
        Asset::FTextureAssetData Data{};
        Data.Serialize(Archive);

        auto Texture{CoreUObject::NewObject<Asset::UTexture2D>()};

        if (Archive.HasError() || Archive.GetRemainingBytes() != 0) {
            Error = Archive.HasError() ? Archive.GetError() : "Invalid texture asset data";

            return nullptr;
        }

        if (!RestoreAssetIdentity(ResolvedPath, *Texture, Error)) {
            return nullptr;
        }

        if (!Data.ApplyTo(*Texture, Error)) {
            return nullptr;
        }

        Asset::UTexture2D* Result{Texture.get()};

        mTextures.emplace(Key, std::move(Texture));

        return Result;
    }

    /**
     * @brief 검증한 CPU 메시 데이터를 바이너리 에셋 파일로 저장합니다.
     */
    bool Asset::FAssetManager::SaveMesh(const std::filesystem::path& Path, const Asset::UStaticMesh& Mesh, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, Mesh.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty() || !Mesh.IsValid()) {
            if (Error.empty()) {
                Error = "Cannot save an invalid mesh";
            }

            return false;
        }

        Core::TArray<Uint8> Payload{};
        Serialization::FMemoryArchive Archive{Payload};
        Asset::FMeshData Data{Mesh.GetMeshData()};
        Core::TArray<PhysicsRuntime::FKConvexElem> ConvexElems{Mesh.GetConvexElems()};
        PhysicsRuntime::FKAggregateGeom SimpleShapes{Mesh.GetSimpleShapes()};

        SerializeMesh(Archive, Data);
        SerializeCollision(Archive, ConvexElems, SimpleShapes);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        if (!CheckCacheSlot(mMeshes, CacheKey(GetRelativePath(Path, Error)), &Mesh, Error)) {
            return false;
        }

        if (!SaveAssetFile(Target, Asset::EAssetType::Mesh, Payload, Error, GetSaveIdentity(Target, Mesh.GetGuid()))) {
            return false;
        }

        return true;
    }

    /**
     * @brief 스켈레탈 메시의 기하·본 계층·스킨 가중치를 에셋 파일에 저장합니다.
     */
    bool Asset::FAssetManager::SaveSkeletalMesh(const std::filesystem::path& Path, const Asset::USkeletalMesh& Mesh, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, Mesh.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty() || !Mesh.IsValid()) {
            if (Error.empty()) {
                Error = "Cannot save an invalid skeletal mesh";
            }

            return false;
        }

        Core::TArray<Uint8> Payload{};
        Serialization::FMemoryArchive Archive{Payload};
        Asset::FMeshData Data{Mesh.GetMeshData()};
        Core::TArray<Asset::FMeshImportSkinWeight> SkinWeights{Mesh.GetSkinWeights()};
        Core::FString PhysicsAssetPath{Mesh.GetPhysicsAssetPath()};

        SerializeSkeletalMesh(Archive, Data, SkinWeights, PhysicsAssetPath);

        // 저장은 메시를 바꾸지 않지만 직렬화 함수가 읽기와 쓰기를 함께 하므로 const를 풉니다.
        const_cast<Asset::USkeletalMesh&>(Mesh).SerializeClothingData(Archive);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        if (!CheckCacheSlot(mSkeletalMeshes, CacheKey(GetRelativePath(Path, Error)), &Mesh, Error)) {
            return false;
        }

        if (!SaveAssetFile(Target, Asset::EAssetType::SkeletalMesh, Payload, Error, GetSaveIdentity(Target, Mesh.GetGuid()))) {
            return false;
        }

        return true;
    }

    bool Asset::FAssetManager::SaveSkeleton(const std::filesystem::path& Path, const Asset::USkeleton& Skeleton, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, Skeleton.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty() || !Skeleton.IsValid()) {
            if (Error.empty()) {
                Error = "Cannot save an invalid skeleton";
            }

            return false;
        }

        Core::TArray<Uint8> Payload{};
        Serialization::FMemoryArchive Archive{Payload};
        Core::TArray<Asset::FMeshImportBone> Bones{Skeleton.GetBones()};

        SerializeSkeleton(Archive, Bones);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        if (!CheckCacheSlot(mSkeletons, CacheKey(GetRelativePath(Path, Error)), &Skeleton, Error)) {
            return false;
        }

        if (!SaveAssetFile(Target, Asset::EAssetType::Skeleton, Payload, Error, GetSaveIdentity(Target, Skeleton.GetGuid()))) {
            return false;
        }

        return true;
    }

    /**
     * @brief 물리 에셋의 바디 설계도와 관절 설정을 지정한 에셋 파일에 저장합니다.
     */
    bool Asset::FAssetManager::SavePhysicsAsset(const std::filesystem::path& Path, const PhysicsRuntime::UPhysicsAsset& PhysicsAsset, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, PhysicsAsset.GetGuid(), Error)) {
            return false;
        }

        const auto IsNull{[](const auto& Setup) { return Setup == nullptr; }};

        if (Target.empty() || std::ranges::any_of(PhysicsAsset.mSkeletalBodySetups, IsNull) || std::ranges::any_of(PhysicsAsset.mConstraintSetup, IsNull)) {
            if (Error.empty()) {
                Error = "Cannot save an invalid physics asset";
            }

            return false;
        }

        Core::TArray<Uint8> Payload{};
        Serialization::FMemoryArchive Archive{Payload};

        // 저장 방향의 직렬화는 값을 읽기만 하므로 에셋을 바꾸지 않습니다.
        SerializePhysicsAsset(Archive, const_cast<PhysicsRuntime::UPhysicsAsset&>(PhysicsAsset));

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        if (!CheckCacheSlot(mPhysicsAssets, CacheKey(GetRelativePath(Path, Error)), &PhysicsAsset, Error)) {
            return false;
        }

        if (!SaveAssetFile(Target, Asset::EAssetType::PhysicsAsset, Payload, Error, GetSaveIdentity(Target, PhysicsAsset.GetGuid()))) {
            return false;
        }

        return true;
    }

    bool Asset::FAssetManager::SaveAnimSequence(const std::filesystem::path& Path, const Asset::UAnimSequence& Sequence, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, Sequence.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty() || !Sequence.IsValid()) {
            if (Error.empty()) {
                Error = "Cannot save an invalid animation sequence";
            }

            return false;
        }

        Core::TArray<Uint8> Payload{};
        Serialization::FMemoryArchive Archive{Payload};
        Asset::FAnimDataModel Model{Sequence.GetDataModel()};
        bool EnableRootMotion{Sequence.IsRootMotionEnabled()};
        Asset::ERootMotionRootLock RootLock{Sequence.GetRootMotionRootLock()};
        bool ForceRootLock{Sequence.IsRootLockForced()};
        Core::FString RootBone{Sequence.GetRootMotionBone()};

        SerializeAnimSequence(Archive, Model, EnableRootMotion, RootLock, ForceRootLock, RootBone);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        if (!CheckCacheSlot(mAnimSequences, CacheKey(GetRelativePath(Path, Error)), &Sequence, Error)) {
            return false;
        }

        if (!SaveAssetFile(Target, Asset::EAssetType::AnimSequence, Payload, Error, GetSaveIdentity(Target, Sequence.GetGuid()))) {
            return false;
        }

        return true;
    }

    /**
     * @brief 텍스처의 크기·형식·색 공간과 이미지 데이터를 에셋 파일로 저장합니다.
     */
    bool Asset::FAssetManager::SaveTexture(const std::filesystem::path& Path, const Asset::UTexture2D& Texture, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, Texture.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty() || !Texture.IsValid() || (Texture.GetPixels().empty() && Texture.GetDDSData().empty())) {
            if (Error.empty()) {
                Error = "Only textures with stored pixel data can be saved";
            }

            return false;
        }

        Core::TArray<Uint8> Payload{};
        Serialization::FMemoryArchive Archive{Payload};
        Asset::FTextureAssetData Data{Texture};
        Data.Serialize(Archive);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        if (!CheckCacheSlot(mTextures, CacheKey(GetRelativePath(Path, Error)), &Texture, Error)) {
            return false;
        }

        if (!SaveAssetFile(Target, Asset::EAssetType::Texture, Payload, Error, GetSaveIdentity(Target, Texture.GetGuid()))) {
            return false;
        }

        return true;
    }

    bool FAssetManager::IsTextureSource(const std::filesystem::path& Path) {
        const auto Extension{Core::FilePath::ToLowerExtension(Path)};

        return Extension == L".png" || Extension == L".jpg" || Extension == L".jpeg" || Extension == L".bmp" || Extension == L".tga" || Extension == L".dds" || Extension == L".hdr";
    }

    /**
     * @brief 기본 임포트 옵션으로 외부 에셋을 새 폴더에 가져오고 생성한 에셋 경로를 반환합니다.
     */
    bool Asset::FAssetManager::ImportAsset(const std::filesystem::path& Source, const std::filesystem::path& Directory, std::filesystem::path& ImportedPath, Core::FString& Error) {
        return ImportAsset(Source, Directory, Asset::FAssetImportOption{}, ImportedPath, Error);
    }

    /**
     * @brief 임포트 옵션을 따라 외부 에셋을 새 폴더에 가져오고 생성한 주 에셋 경로를 반환합니다.
     */
    bool Asset::FAssetManager::ImportAsset(const std::filesystem::path& Source, const std::filesystem::path& Directory, const Asset::FAssetImportOption& Option, std::filesystem::path& ImportedPath, Core::FString& Error) {
        Error.clear();
        ImportedPath.clear();

        const std::wstring Extension{Core::FilePath::ToLowerExtension(Source)};
        if (Extension == L".hlsl") {
            return ImportShader(Source, Directory, ImportedPath, Error);
        }

        const bool Texture{IsTextureSource(Source)};

        if (!Texture && !Asset::IMeshImporter::IsSupported(Source)) {
            Error = "Unsupported import file type: " + Core::FilePath::ToUtf8(Source.filename());

            return false;
        }

        const auto Parent{Directory.empty() ? mContentRoot : ResolveWritablePath(Directory, Error)};

        if (Parent.empty()) {
            return false;
        }

        std::error_code Code{};

        if (!std::filesystem::is_regular_file(Source, Code) || Code) {
            Error = "Cannot read the import source";

            return false;
        }

        std::filesystem::create_directories(Parent, Code);

        if (Code) {
            Error = "Cannot create the import destination: " + Code.message();

            return false;
        }

        const Core::FString Name{AssetFileName(Core::FilePath::ToUtf8(Source.stem()), "Asset")};
        const auto Base{Core::FilePath::FromUtf8(Name)};

        if (!Texture && !Option.mImportMesh && !Option.mImportSkeleton && Option.mImportAnimations) {
            auto Target{Parent / Base};

            Target += L".masset";

            if (!ImportMesh(Source, Target, Option, Error)) {
                return false;
            }

            ImportedPath = Target.lexically_relative(mContentRoot);

            return true;
        }

        auto Folder{Parent / Base};

        for (Index_t Index{}; !std::filesystem::create_directory(Folder, Code); ++Index) {
            if (Code && Code != std::errc::file_exists) {
                Error = "Cannot create the import folder: " + Code.message();

                return false;
            }

            Code.clear();

            Folder = Parent / (Base.wstring() + L"_" + std::to_wstring(Index + 1));
        }

        // 새로 만든 폴더라도 지운 에셋의 캐시가 같은 경로에 남아 있을 수 있으므로 먼저 은퇴시킵니다.
        RetireAssets(Folder);

        Asset::FAssetImportScope Scope{Folder};
        auto Target{Folder / Base};

        Target += L".masset";

        if (!(Texture ? ImportTexture(Source, Target, Error) : ImportMesh(Source, Target, Option, Error))) {
            RetireAssets(Folder);

            return false;
        }

        ImportedPath = Target.lexically_relative(mContentRoot);

        Scope.Commit();

        return true;
    }

    /**
     * @brief 원본 메시와 재질·외부 텍스처를 임포트 옵션에 따라 프로젝트 에셋으로 가져와 메시 참조를 저장합니다.
     */
    bool Asset::FAssetManager::ImportMesh(const std::filesystem::path& Source, std::filesystem::path& Target, const Asset::FAssetImportOption& Option, Core::FString& Error) {
        const Core::FScopedStatTimer StatTimer{Asset::AssetStats::MeshImport};
        const auto Destination{ResolveWritablePath(Target, Error)};

        if (Destination.empty()) {
            return false;
        }

        Asset::FMeshImportResult Imported{};

        {
            auto Importer{Asset::IMeshImporter::CreateForSource(Source, Option)};

            if (!Importer->Import(Imported, Error)) {
                Error = "Cannot import mesh source '" + Core::FilePath::ToUtf8(Source) + "': " + Error;

                return false;
            }
        }

        // 메시 옵션을 끄면 원본에 메시가 있어도 스켈레톤과 애니메이션만 가져옵니다.
        if (!Option.mImportMesh) {
            Imported.GetMeshes().clear();
        }

        const bool HasSkeleton{Option.mImportSkeleton && !Imported.GetBones().empty()};
        std::filesystem::path SkeletonTarget{};

        if (HasSkeleton) {
            auto Skeleton{CoreUObject::NewObject<Asset::USkeleton>()};

            if (!Skeleton->SetBones(std::move(Imported.GetBones()))) {
                Error = "Invalid imported skeleton data";

                return false;
            }

            SkeletonTarget = UniqueAssetPath(Destination.parent_path(), Core::FilePath::ToUtf8(Destination.stem()) + "_Skeleton", Error);

            if (SkeletonTarget.empty() || !SaveSkeleton(SkeletonTarget, *Skeleton, Error)) {
                return false;
            }
        }

        std::filesystem::path FirstAnimationTarget{};

        if (Option.mImportAnimations) {
            const bool AnimationOnly{!Option.mImportMesh && !Option.mImportSkeleton};
            const auto AnimationDirectory{AnimationOnly ? Destination.parent_path() : Destination.parent_path() / "Animations"};
            Asset::FAssetImportScope AnimationScope{};

            for (Asset::FMeshImportAnimation& Animation : Imported.GetAnimations()) {
                auto Sequence{CoreUObject::NewObject<Asset::UAnimSequence>()};

                if (!Sequence->SetDataModel(std::move(Animation.GetDataModel()), Error)) {
                    Error = "Invalid imported animation '" + Animation.GetName() + "': " + Error;

                    return false;
                }

                const auto AnimationTarget{UniqueAssetPath(AnimationDirectory, AssetFileName(Animation.GetName(), "Animation"), Error)};

                if (AnimationTarget.empty()) {
                    return false;
                }

                if (AnimationOnly) {
                    RetireAssets(AnimationTarget);
                    AnimationScope.TrackFile(AnimationTarget);
                }

                if (!SaveAnimSequence(AnimationTarget, *Sequence, Error)) {
                    return false;
                }

                if (FirstAnimationTarget.empty()) {
                    FirstAnimationTarget = AnimationTarget;
                }
            }

            AnimationScope.Commit();
        }

        // 메시가 없으면 스켈레톤을, 스켈레톤도 없으면 첫 애니메이션을 대표 에셋으로 선택합니다.
        if (Imported.GetMeshes().empty()) {
            Target = !SkeletonTarget.empty() ? SkeletonTarget : FirstAnimationTarget;

            if (Target.empty()) {
                Error = "The source contains nothing to import with the selected options";

                return false;
            }

            return true;
        }

        const auto& Materials{Imported.GetMaterials()};
        const auto MaterialDirectory{Destination.parent_path() / "Materials"};
        const auto TextureDirectory{Destination.parent_path() / "Textures"};
        Core::TMap<Core::FString, Core::FString> Textures{};

        // 결과 재질별로 저장한 머티리얼 에셋 경로입니다. 여러 메시가 같은 재질을 쓰면 한 번만 저장하고, 쓰이지 않는 재질은 저장하지 않습니다.
        Core::TMap<Index_t, Core::FString> MaterialPaths{};

        const auto ImportMaterial{[&](Index_t Index, Core::FString& MaterialPath) {
            const Asset::FMeshImportMaterial& SourceMaterial{Materials[Index]};
            auto Material{CoreUObject::NewObject<Asset::UMaterial>()};
            Material->SetName(SourceMaterial.GetName());

            if (!Material->SetBaseColor(SourceMaterial.GetBaseColor())) {
                Error = "Invalid imported material color: " + SourceMaterial.GetName();

                return false;
            }

            Material->SetTwoSided(SourceMaterial.IsTwoSided());

            if (!Material->SetSpecularColor(SourceMaterial.GetSpecularColor()) || !Material->SetSpecularStrength(SourceMaterial.GetSpecularStrength()) || !Material->SetShininess(SourceMaterial.GetShininess()) || !Material->SetAmbientColor(SourceMaterial.GetAmbientColor()) || !Material->SetEmissiveColor(SourceMaterial.GetEmissiveColor()) || !Material->SetReflectionColor(SourceMaterial.GetReflectionColor()) || !Material->SetReflectionStrength(SourceMaterial.GetReflectionStrength())) {
                Error = "Invalid imported material lighting: " + SourceMaterial.GetName();

                return false;
            }

            Core::FString BaseColorPath{};
            Core::FString NormalMapPath{};
            Core::FString SpecularPath{};
            Core::FString EmissivePath{};
            Core::FString ReflectionPath{};

            // 텍스처 임포트 옵션을 끄면 텍스처 경로를 비워 둔 채 머티리얼의 색상·조명 값만 저장합니다.
            const auto ReadTexture{[&](const Asset::FMeshImportTexture& Texture, Core::FString& TexturePath) {
                return !Option.mImportTextures || ImportMeshTexture(*this, Texture, TextureDirectory, Textures, TexturePath, Error);
            }};

            if (!ReadTexture(SourceMaterial.GetBaseColorTexture(), BaseColorPath) || !ReadTexture(SourceMaterial.GetNormalMapTexture(), NormalMapPath)) {
                return false;
            }

            if (!Material->SetBaseColorTexturePath(BaseColorPath) || !Material->SetNormalMapTexturePath(NormalMapPath)) {
                Error = "Invalid imported material texture path: " + SourceMaterial.GetName();

                return false;
            }

            if (!ReadTexture(SourceMaterial.GetSpecularTexture(), SpecularPath) || !ReadTexture(SourceMaterial.GetEmissiveTexture(), EmissivePath) || !ReadTexture(SourceMaterial.GetReflectionTexture(), ReflectionPath)) {
                return false;
            }

            if (!Material->SetSpecularTexturePath(SpecularPath) || !Material->SetEmissiveTexturePath(EmissivePath) || !Material->SetReflectionTexturePath(ReflectionPath)) {
                Error = "Invalid imported material texture path: " + SourceMaterial.GetName();

                return false;
            }

            const auto MaterialTarget{UniqueAssetPath(MaterialDirectory, AssetFileName(Material->GetName(), "Material_" + std::to_string(Index)), Error)};

            if (MaterialTarget.empty() || !SaveMaterial(MaterialTarget, *Material, Error)) {
                return false;
            }

            MaterialPath = GetRelativePath(MaterialTarget, Error);

            return !MaterialPath.empty();
        }};

        auto& Meshes{Imported.GetMeshes()};

        for (Index_t MeshIndex{}; MeshIndex < Meshes.size(); ++MeshIndex) {
            Asset::FMeshImportMesh& ImportedMesh{Meshes[MeshIndex]};
            Asset::FMeshData& Data{ImportedMesh.GetMeshData()};
            const auto& MaterialIndices{ImportedMesh.GetMaterialIndices()};

            if (MaterialIndices.size() != Data.GetMaterialSlots().size()) {
                Error = "Imported materials do not match the mesh material slots";

                return false;
            }

            for (Index_t Slot{}; Slot < MaterialIndices.size(); ++Slot) {
                const Index_t MaterialIndex{static_cast<Index_t>(MaterialIndices[Slot])};

                if (MaterialIndex >= Materials.size()) {
                    Error = "An imported mesh slot references an invalid material";

                    return false;
                }

                // 재질 임포트 옵션을 끄면 슬롯의 머티리얼 경로를 비워 기본 머티리얼을 사용하게 합니다.
                Core::FString MaterialPath{};

                if (Option.mImportMaterials) {
                    auto Existing{MaterialPaths.find(MaterialIndex)};

                    if (Existing == MaterialPaths.end()) {
                        if (!ImportMaterial(MaterialIndex, MaterialPath)) {
                            return false;
                        }

                        Existing = MaterialPaths.emplace(MaterialIndex, std::move(MaterialPath)).first;
                    }

                    MaterialPath = Existing->second;
                }

                Data.GetMaterialSlots()[Slot] = Asset::FMeshMaterialSlot{Materials[MaterialIndex].GetName(), std::move(MaterialPath)};
            }

            // 메시가 하나이면 요청한 경로에 저장하고, 여러 개이면 같은 폴더에 메시 이름으로 저장합니다. 첫 메시가 대표 경로가 됩니다.
            const auto MeshTarget{Meshes.size() == 1 ? Destination : UniqueAssetPath(Destination.parent_path(), AssetFileName(ImportedMesh.GetName(), "Mesh_" + std::to_string(MeshIndex)), Error)};

            if (MeshTarget.empty()) {
                return false;
            }

            if (HasSkeleton) {
                auto Mesh{CoreUObject::NewObject<Asset::USkeletalMesh>()};
                Core::TArray<Asset::FMeshImportSkinWeight> SkinWeights{ImportedMesh.GetSkinWeights()};

                if (SkinWeights.empty()) {
                    SkinWeights.resize(Data.GetPositions().size());
                }

                if (!Mesh->SetMeshData(std::move(Data), std::move(SkinWeights))) {
                    Error = "Invalid imported skeletal mesh data: " + ImportedMesh.GetName();

                    return false;
                }

                if (!SaveSkeletalMesh(MeshTarget, *Mesh, Error)) {
                    return false;
                }
            } else {
                auto Mesh{CoreUObject::NewObject<Asset::UStaticMesh>()};

                if (!Mesh->SetMeshData(std::move(Data))) {
                    Error = "Invalid imported mesh sections: " + ImportedMesh.GetName();

                    return false;
                }

                // 메시 형태를 감싸는 볼록 외곽을 충돌 형상으로 저장합니다. 만들 수 없으면 경계 상자를 사용합니다.
                if (Option.mGenerateConvexCollision) {
                    PhysicsRuntime::FKConvexElem Hull{};

                    if (mPhysicsRuntime != nullptr && mPhysicsRuntime->ComputeConvexHull(Mesh->GetMeshData().GetPositions(), Hull)) {
                        Mesh->SetConvexElems({std::move(Hull)});
                    } else {
                        Core::ErrorHandler::Report("Asset", "Cannot generate convex collision for '" + ImportedMesh.GetName() + "'; using the mesh bounds box", Core::ErrorHandler::EErrorLevel::Warning);
                    }
                }

                if (!SaveMesh(MeshTarget, *Mesh, Error)) {
                    return false;
                }
            }

            if (MeshIndex == 0) {
                Target = MeshTarget;
            }
        }

        return true;
    }

    /**
     * @brief 외부 이미지를 읽어 프로젝트의 텍스처 에셋 파일로 저장합니다.
     */
    bool Asset::FAssetManager::ImportTexture(const std::filesystem::path& Source, const std::filesystem::path& Target, Core::FString& Error) {
        const Core::FScopedStatTimer StatTimer{Asset::AssetStats::TextureImport};
        auto Texture{CoreUObject::NewObject<Asset::UTexture2D>()};

        if (!Texture->LoadFromFile(Source)) {
            Error = "Cannot import texture source";

            return false;
        }

        return SaveTexture(Target, *Texture, Error);
    }

    bool Asset::FAssetManager::RenameContentPath(const std::filesystem::path& Path, const std::filesystem::path& Destination, Core::FString& Error) {
        const auto From{ResolveWritablePath(Path, Error)};
        if (From.empty()) {
            return false;
        }

        const auto DestinationParent{ResolveWritablePath(Destination.parent_path().empty() ? std::filesystem::path{L"."} : Destination.parent_path(), Error)};
        if (DestinationParent.empty()) {
            return false;
        }

        const auto Name{Destination.filename()};
        const auto To{DestinationParent / Name};
        if (Name.empty() || Name == L"." || Name == L".." || Name.native().find_first_of(L"\\/:*?\"<>|") != std::wstring::npos || Name.native().find(L'\0') != std::wstring::npos || GetRelativePath(From, Error) == ".") {
            Error = "Invalid content destination";
            return false;
        }

        if (From == To) {
            return true;
        }

        std::error_code Code{};
        if (!std::filesystem::is_directory(DestinationParent, Code) || Code || IsRedirectorDirectory(DestinationParent)) {
            Error = "The destination must be an existing content folder";
            return false;
        }

        if (!GetPathWithinRoot(From, DestinationParent).empty()) {
            Error = "Cannot move a folder into itself";
            return false;
        }

        const bool SamePath{Core::FilePath::ToLower(From) == Core::FilePath::ToLower(To)};
        const auto DestinationStatus{std::filesystem::symlink_status(To, Code)};
        if (std::filesystem::is_symlink(DestinationStatus) || !std::filesystem::status_known(DestinationStatus) || (std::filesystem::exists(DestinationStatus) && (!SamePath || !std::filesystem::equivalent(From, To, Code)))) {
            Error = "The destination name is already in use";
            return false;
        }

        const bool Directory{std::filesystem::is_directory(From, Code)};
        if (Code) {
            Error = "Cannot read the rename source: " + Code.message();
            return false;
        }

        std::filesystem::path Stage{};
        if (!SamePath && (Directory || From.extension() == L".masset")) {
            const Core::FGuid StagingId{};
            Stage = From.parent_path() / Core::FilePath::FromUtf8(".macaw-rename-" + StagingId.ToString());
            if (!StagingId.IsValid() || !std::filesystem::create_directory(Stage, Code)) {
                Error = "Cannot prepare the asset redirector";
                return false;
            }
        }

        Asset::FAssetImportScope Scope{Stage};
        const auto Marker{Stage / DirectoryRedirectorName};
        if (!Stage.empty()) {
            const auto Target{GetRelativePath(To, Error)};
            if (Target.empty() || !Asset::FAssetFile::Save(Marker, Asset::EAssetType::Redirector, std::span<const Uint8>{reinterpret_cast<const Uint8*>(Target.data()), Target.size()}, Error)) {
                return false;
            }
        }

        std::filesystem::rename(From, To, Code);
        if (Code) {
            Error = "Cannot rename the content: " + Code.message();
            return false;
        }

        if (!Stage.empty()) {
            std::filesystem::rename(Directory ? Stage : Marker, From, Code);
            if (Code) {
                Error = "Cannot install the asset redirector: " + Code.message();
                std::filesystem::rename(To, From, Code);
                if (Code) {
                    Error += " Cannot restore the original path: " + Code.message();
                }
                return false;
            }

            if (Directory) {
                Scope.Commit();
            }
        }

        RetireAssets(From);
        return true;
    }

    /**
     * @brief 경로와 하위 폴더에 해당하는 에셋을 캐시에서 은퇴시켜 기존 참조의 수명을 유지합니다.
     * @details 각 캐시의 소유권을 mRetired로 이전하며 파일은 변경하지 않습니다.
     */
    void Asset::FAssetManager::RetireAssets(const std::filesystem::path& Path) {
        Core::FString Error{};
        const Core::FString Relative{GetRelativePath(Path, Error)};

        if (Relative.empty()) {
            return;
        }

        // 콘텐츠 루트 자체를 가리키면 모든 항목이 대상입니다.
        const Core::FString Key{Relative == "." ? Core::FString{} : CacheKey(Relative)};
        const auto Retire{[this, &Key](auto& Cache) {
            std::erase_if(Cache, [this, &Key](auto& Entry) {
                if (!Key.empty() && Entry.first != Key && !Entry.first.starts_with(Key + "/")) {
                    return false;
                }

                mRetired.push_back(std::move(Entry.second));

                return true;
            });
        }};

        Retire(mMeshes);
        Retire(mSkeletalMeshes);
        Retire(mSkeletons);
        Retire(mLegacySkeletons);
        Retire(mPhysicsAssets);
        Retire(mAnimSequences);
        Retire(mTextures);
        Retire(mNormalMaps);
        Retire(mMaterials);
        Retire(mShaders);
        Retire(mSequences);
        Retire(mBlendSpaces);
        Retire(mAnimGraphs);
        Retire(mRegisteredAssets);
    }

    /**
     * @brief 재질 에셋과 텍스처 참조를 로드하여 캐시에 보관합니다.
     */
    Asset::UMaterial* Asset::FAssetManager::LoadMaterial(const std::filesystem::path& Path, Core::FString& Error) {
        const Core::FScopedStatTimer StatTimer{Asset::AssetStats::MaterialLoad};
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mMaterials.find(Key)};

        if (Existing != mMaterials.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        auto Material{CoreUObject::NewObject<Asset::UMaterial>()};

        if (!Asset::FMaterialFile::Load(ResolvedPath, *this, *Material, Error)) {
            return nullptr;
        }

        Asset::UMaterial* Result{Material.get()};

        mMaterials.emplace(Key, std::move(Material));

        return Result;
    }

    /**
     * @brief 시퀀스 에셋을 로드하여 캐시에 보관합니다.
     */
    Asset::ULevelSequence* Asset::FAssetManager::LoadSequence(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mSequences.find(Key)};

        if (Existing != mSequences.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::Sequence, Payload, Error)) {
            return nullptr;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Loading};

        if (!Archive.Parse(Core::FString{reinterpret_cast<const char*>(Payload.data()), Payload.size()})) {
            Error = Archive.GetError();

            return nullptr;
        }

        auto Sequence{CoreUObject::NewObject<Asset::ULevelSequence>()};
        Sequence->Serialize(Archive);

        if (!RestoreAssetIdentity(ResolvedPath, *Sequence, Error)) {
            return nullptr;
        }

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return nullptr;
        }

        Asset::ULevelSequence* Result{Sequence.get()};

        mSequences.emplace(Key, std::move(Sequence));

        return Result;
    }

    /**
     * @brief Sequences 폴더에 빈 시퀀스 에셋을 만들고 캐시에 보관합니다.
     */
    Asset::ULevelSequence* Asset::FAssetManager::CreateSequence(const Core::FString& Name, std::filesystem::path& Path, Core::FString& Error) {
        const auto Directory{mContentRoot / "Sequences"};
        std::error_code Code{};

        std::filesystem::create_directories(Directory, Code);

        if (Code) {
            Error = "Cannot create the sequence folder: " + Code.message();

            return nullptr;
        }

        const auto Target{UniqueAssetPath(Directory, AssetFileName(Name, "Sequence"), Error)};
        auto Sequence{CoreUObject::NewObject<Asset::ULevelSequence>()};

        if (Target.empty() || !SaveSequence(Target, *Sequence, Error)) {
            return nullptr;
        }

        Path = Target.lexically_relative(mContentRoot);

        Asset::ULevelSequence* Result{Sequence.get()};

        mSequences.emplace(CacheKey(GetRelativePath(Target, Error)), std::move(Sequence));

        return Result;
    }

    /**
     * @brief 시퀀스를 에셋 파일로 저장합니다.
     */
    bool Asset::FAssetManager::SaveSequence(const std::filesystem::path& Path, Asset::ULevelSequence& Sequence, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, Sequence.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty()) {
            return false;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Saving};

        Sequence.Serialize(Archive);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        const Core::FString Payload{Archive.ToString()};

        return Asset::FAssetFile::Save(Target, Asset::EAssetType::Sequence, std::span<const Uint8>{reinterpret_cast<const Uint8*>(Payload.data()), Payload.size()}, Error, GetSaveIdentity(Target, Sequence.GetGuid()));
    }

    Asset::UBlendSpace* Asset::FAssetManager::LoadBlendSpace(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mBlendSpaces.find(Key)};

        if (Existing != mBlendSpaces.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::BlendSpace, Payload, Error)) {
            return nullptr;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Loading};

        if (!Archive.Parse(Core::FString{reinterpret_cast<const char*>(Payload.data()), Payload.size()})) {
            Error = Archive.GetError();

            return nullptr;
        }

        // 파일에 기록한 클래스 이름으로 1D·2D 같은 파생 타입을 고릅니다. 지금은 1D만 있습니다.
        Core::FString ClassName{};

        Archive.Serialize("Class", ClassName);

        if (Archive.HasError() || ClassName != "BlendSpace1D") {
            Error = Archive.HasError() ? Archive.GetError() : "Unsupported blend space class: " + ClassName;

            return nullptr;
        }

        std::unique_ptr<Asset::UBlendSpace> BlendSpace{CoreUObject::NewObject<Asset::UBlendSpace1D>()};
        BlendSpace->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));

        BlendSpace->Serialize(Archive, *this);

        if (!RestoreAssetIdentity(ResolvedPath, *BlendSpace, Error)) {
            return nullptr;
        }

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return nullptr;
        }

        Asset::UBlendSpace* Result{BlendSpace.get()};

        mBlendSpaces.emplace(Key, std::move(BlendSpace));

        return Result;
    }

    Asset::UBlendSpace* Asset::FAssetManager::CreateBlendSpace1D(const std::filesystem::path& Directory, const Core::FString& Name, std::filesystem::path& Path, Core::FString& Error) {
        // 콘텐츠 루트 자체는 ResolvePath가 에셋 경로로 받지 않으므로 직접 씁니다.
        const auto Folder{Directory.empty() || Directory == L"." ? mContentRoot : ResolveWritablePath(Directory, Error)};

        if (Folder.empty()) {
            return nullptr;
        }

        const auto Target{UniqueAssetPath(Folder, AssetFileName(Name, "BlendSpace"), Error)};

        if (Target.empty()) {
            return nullptr;
        }

        std::unique_ptr<Asset::UBlendSpace> BlendSpace{CoreUObject::NewObject<Asset::UBlendSpace1D>()};

        BlendSpace->SetName(Core::FilePath::ToUtf8(Target.stem()));

        if (!SaveBlendSpace(Target, *BlendSpace, Error)) {
            return nullptr;
        }

        const Core::FString Relative{GetRelativePath(Target, Error)};

        if (Relative.empty()) {
            return nullptr;
        }

        Path = Core::FilePath::FromUtf8(Relative);

        Asset::UBlendSpace* Result{BlendSpace.get()};

        mBlendSpaces.emplace(CacheKey(Relative), std::move(BlendSpace));

        return Result;
    }

    bool Asset::FAssetManager::SaveBlendSpace(const std::filesystem::path& Path, Asset::UBlendSpace& BlendSpace, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, BlendSpace.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty()) {
            return false;
        }

        const Core::FString Key{CacheKey(GetRelativePath(Target, Error))};

        if (Key.empty()) {
            return false;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Saving};
        Core::FString ClassName{"BlendSpace1D"};

        Archive.Serialize("Class", ClassName);
        BlendSpace.Serialize(Archive, *this);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        const Core::FString Payload{Archive.ToString()};

        if (!SaveAssetFile(Target, Asset::EAssetType::BlendSpace, std::span<const Uint8>{reinterpret_cast<const Uint8*>(Payload.data()), Payload.size()}, Error, GetSaveIdentity(Target, BlendSpace.GetGuid()))) {
            return false;
        }

        if (const auto Existing{mBlendSpaces.find(Key)}; Existing != mBlendSpaces.end()) {
            Existing->second->CopyFrom(BlendSpace);
        }

        return true;
    }

    Asset::UAnimGraph* Asset::FAssetManager::LoadAnimGraph(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mAnimGraphs.find(Key)};

        if (Existing != mAnimGraphs.end()) {
            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Asset::EAssetType::AnimGraph, Payload, Error)) {
            return nullptr;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Loading};

        if (!Archive.Parse(Core::FString{reinterpret_cast<const char*>(Payload.data()), Payload.size()})) {
            Error = Archive.GetError();

            return nullptr;
        }

        auto Graph{CoreUObject::NewObject<Asset::UAnimGraph>()};

        Graph->Serialize(Archive);

        if (!RestoreAssetIdentity(ResolvedPath, *Graph, Error)) {
            return nullptr;
        }

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return nullptr;
        }

        Graph->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));

        Asset::UAnimGraph* Result{Graph.get()};

        mAnimGraphs.emplace(Key, std::move(Graph));

        return Result;
    }

    Asset::UAnimGraph* Asset::FAssetManager::CreateAnimGraph(const std::filesystem::path& Directory, const Core::FString& Name, std::filesystem::path& Path, Core::FString& Error) {
        // 콘텐츠 루트 자체는 ResolvePath가 에셋 경로로 받지 않으므로 직접 씁니다.
        const auto Folder{Directory.empty() || Directory == L"." ? mContentRoot : ResolveWritablePath(Directory, Error)};

        if (Folder.empty()) {
            return nullptr;
        }

        const auto Target{UniqueAssetPath(Folder, AssetFileName(Name, "AnimGraph"), Error)};

        if (Target.empty()) {
            return nullptr;
        }

        auto Graph{CoreUObject::NewObject<Asset::UAnimGraph>()};

        Graph->SetName(Core::FilePath::ToUtf8(Target.stem()));

        if (!SaveAnimGraph(Target, *Graph, Error)) {
            return nullptr;
        }

        const Core::FString Relative{GetRelativePath(Target, Error)};

        if (Relative.empty()) {
            return nullptr;
        }

        Path = Core::FilePath::FromUtf8(Relative);

        Asset::UAnimGraph* Result{Graph.get()};

        mAnimGraphs.emplace(CacheKey(Relative), std::move(Graph));

        return Result;
    }

    bool Asset::FAssetManager::SaveAnimGraph(const std::filesystem::path& Path, Asset::UAnimGraph& Graph, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (!ValidateAssetIdentity(Target, Graph.GetGuid(), Error)) {
            return false;
        }

        if (Target.empty()) {
            return false;
        }

        const Core::FString Key{CacheKey(GetRelativePath(Target, Error))};

        if (Key.empty()) {
            return false;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Saving};

        Graph.Serialize(Archive);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        const Core::FString Payload{Archive.ToString()};

        if (!SaveAssetFile(Target, Asset::EAssetType::AnimGraph, std::span<const Uint8>{reinterpret_cast<const Uint8*>(Payload.data()), Payload.size()}, Error, GetSaveIdentity(Target, Graph.GetGuid()))) {
            return false;
        }

        if (const auto Existing{mAnimGraphs.find(Key)}; Existing != mAnimGraphs.end()) {
            Existing->second->CopyFrom(Graph);
        }

        return true;
    }


    bool Asset::FAssetManager::RegisterAssetType(Asset::EAssetType Type, Asset::FAssetTypeHandler Handler) {
        if (Handler.mClass == nullptr || !Handler.mClass->CanCreate() || mTypeHandlers.contains(Type)) {
            return false;
        }

        mTypeHandlers.emplace(Type, std::move(Handler));

        return true;
    }

    const Asset::FAssetTypeHandler* Asset::FAssetManager::FindAssetTypeHandler(Asset::EAssetType Type) const {
        const auto Found{mTypeHandlers.find(Type)};

        return Found != mTypeHandlers.end() ? &Found->second : nullptr;
    }

    CoreUObject::UObject* Asset::FAssetManager::LoadRegisteredAsset(const std::filesystem::path& Path, Asset::EAssetType Type, Core::FString& Error) {
        const Asset::FAssetTypeHandler* Handler{FindAssetTypeHandler(Type)};

        if (Handler == nullptr) {
            Error = "This asset type is not registered";

            return nullptr;
        }

        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return nullptr;
        }

        if (const auto Existing{mRegisteredAssets.find(Key)}; Existing != mRegisteredAssets.end()) {
            if (!Existing->second->IsA(Handler->mClass)) {
                Error = "The asset type does not match the object reference";

                return nullptr;
            }

            Asset::AssetStats::AssetCacheHits.Add();

            return Existing->second.get();
        }

        Core::TArray<Uint8> Payload{};

        if (!Asset::FAssetFile::Load(ResolvedPath, Type, Payload, Error)) {
            return nullptr;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Loading};

        if (!Archive.Parse(Core::FString{reinterpret_cast<const char*>(Payload.data()), Payload.size()})) {
            Error = Archive.GetError();

            return nullptr;
        }

        auto Object{Handler->mClass->CreateInstance()};

        if (Object == nullptr) {
            Error = "Cannot create the asset object";

            return nullptr;
        }

        Object->Serialize(Archive);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return nullptr;
        }

        if (!RestoreAssetIdentity(ResolvedPath, *Object, Error)) {
            return nullptr;
        }

        Object->SetName(Core::FilePath::ToUtf8(ResolvedPath.stem()));

        if (Handler->mPostLoad && !Handler->mPostLoad(*Object, Error)) {
            return nullptr;
        }

        CoreUObject::UObject* Result{Object.get()};

        mRegisteredAssets.emplace(Key, std::move(Object));

        return Result;
    }

    CoreUObject::UObject* Asset::FAssetManager::CreateRegisteredAsset(const std::filesystem::path& Directory, const Core::FString& Name, Asset::EAssetType Type, std::filesystem::path& Path, Core::FString& Error) {
        const Asset::FAssetTypeHandler* Handler{FindAssetTypeHandler(Type)};

        if (Handler == nullptr) {
            Error = "This asset type is not registered";

            return nullptr;
        }

        // 콘텐츠 루트 자체는 ResolvePath가 에셋 경로로 받지 않으므로 직접 씁니다.
        const auto Folder{Directory.empty() || Directory == L"." ? mContentRoot : ResolveWritablePath(Directory, Error)};

        if (Folder.empty()) {
            return nullptr;
        }

        const auto Target{UniqueAssetPath(Folder, AssetFileName(Name, Handler->mDefaultName), Error)};

        if (Target.empty()) {
            return nullptr;
        }

        auto Object{Handler->mClass->CreateInstance()};

        if (Object == nullptr) {
            Error = "Cannot create the asset object";

            return nullptr;
        }

        Object->SetName(Core::FilePath::ToUtf8(Target.stem()));

        if (Handler->mInitialize) {
            Handler->mInitialize(*Object);
        }

        if (!SaveRegisteredAsset(Target, Type, *Object, Error)) {
            return nullptr;
        }

        if (Handler->mPostLoad && !Handler->mPostLoad(*Object, Error)) {
            return nullptr;
        }

        const Core::FString Relative{GetRelativePath(Target, Error)};

        if (Relative.empty()) {
            return nullptr;
        }

        Path = Core::FilePath::FromUtf8(Relative);

        CoreUObject::UObject* Result{Object.get()};

        mRegisteredAssets.emplace(CacheKey(Relative), std::move(Object));

        return Result;
    }

    bool Asset::FAssetManager::SaveRegisteredAsset(const std::filesystem::path& Path, Asset::EAssetType Type, CoreUObject::UObject& Object, Core::FString& Error, bool ReplaceCached) {
        const Asset::FAssetTypeHandler* Handler{FindAssetTypeHandler(Type)};

        if (Handler == nullptr || !Object.IsA(Handler->mClass)) {
            Error = Handler == nullptr ? "This asset type is not registered" : "The object does not match the asset type";

            return false;
        }

        const auto Target{ResolveWritablePath(Path, Error)};

        if (Target.empty()) {
            return false;
        }

        const Core::FString Key{CacheKey(GetRelativePath(Target, Error))};

        if (Key.empty() || (!ReplaceCached && !CheckCacheSlot(mRegisteredAssets, Key, &Object, Error))) {
            return false;
        }

        const auto Existing{mRegisteredAssets.find(Key)};
        if (ReplaceCached) {
            Asset::EAssetType ExistingType{};

            if (Existing == mRegisteredAssets.end() || !Existing->second->IsA(Handler->mClass) || !Asset::FAssetFile::GetType(Target, ExistingType, Error) || ExistingType != Type) {
                Error = "The asset to replace is no longer available at this path";

                return false;
            }
        }

        const Core::FGuid Identity{GetSaveIdentity(Target, ReplaceCached ? Existing->second->GetGuid() : Object.GetGuid())};

        if (!ValidateAssetIdentity(Target, Identity, Error)) {
            return false;
        }

        Serialization::FJsonArchive Archive{Core::EArchiveMode::Saving};

        Object.Serialize(Archive);

        if (Archive.HasError()) {
            Error = Archive.GetError();

            return false;
        }

        const Core::FString Payload{Archive.ToString()};

        std::unique_ptr<CoreUObject::UObject> Replacement{};

        if (ReplaceCached) {
            Replacement = Handler->mClass->CreateInstance();
            Serialization::FJsonArchive Reader{Core::EArchiveMode::Loading};

            if (Replacement == nullptr) {
                Error = "Cannot create the replacement asset object";

                return false;
            }

            if (Reader.Parse(Payload)) {
                Replacement->Serialize(Reader);
            }

            if (Reader.HasError()) {
                Error = Reader.GetError();

                return false;
            }

            Replacement->RestoreGuid(Identity);
            Replacement->SetName(Core::FilePath::ToUtf8(Target.stem()));

            if (Handler->mPostLoad && !Handler->mPostLoad(*Replacement, Error)) {
                return false;
            }
        }

        if (!Asset::FAssetFile::Save(Target, Type, std::span<const Uint8>{reinterpret_cast<const Uint8*>(Payload.data()), Payload.size()}, Error, Identity)) {
            return false;
        }

        if (Replacement != nullptr) {
            mRetired.push_back(std::move(Existing->second));
            Existing->second = std::move(Replacement);
        }

        return true;
    }

    /**
     * @brief 머티리얼을 안전하게 저장하고 같은 경로의 캐시 객체를 갱신합니다.
     */
    bool Asset::FAssetManager::SaveMaterial(const std::filesystem::path& Path, const Asset::UMaterial& Material, Core::FString& Error) {
        const auto Target{ResolveWritablePath(Path, Error)};

        if (Target.empty()) {
            return false;
        }

        auto Candidate{CoreUObject::NewObject<Asset::UMaterial>()};
        Asset::EAssetType ExistingType{};
        Core::FGuid ExistingGuid{Core::FGuid::Null()};
        Core::FString HeaderError{};
        Asset::FAssetFile::GetIdentity(Target, ExistingType, ExistingGuid, HeaderError);
        Candidate->RestoreGuid(ExistingGuid.IsValid() ? ExistingGuid : Material.GetGuid());

        if (LoadAsset(Material.GetShader().GetGuid(), Error, RenderCore::UShader::StaticClass()) == nullptr || !ValidateAssetIdentity(Target, Candidate->GetGuid(), Error)) {
            return false;
        }

        if (!ApplyMaterialSettings(*Candidate, Material, Error) || !Asset::FMaterialFile::Save(Target, *this, *Candidate, Error)) {
            return false;
        }

        if (mAssetSaved) {
            mAssetSaved(Target, Asset::EAssetType::Material);
        }

        const auto Existing{mMaterials.find(CacheKey(GetRelativePath(Path, Error)))};

        if (Existing != mMaterials.end() && Existing->second.get() != &Material) {
            return ApplyMaterialSettings(*Existing->second, Material, Error);
        }

        return true;
    }

    /**
     * @brief 캐시 객체의 주소를 유지하면서 파일의 최신 설정을 다시 적용합니다.
     */
    bool Asset::FAssetManager::ReloadMaterial(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        const auto ResolvedPath{ResolveAssetLoadPath(Path, Key, Error)};

        if (Key.empty()) {
            return false;
        }

        const auto Existing{mMaterials.find(Key)};

        if (Existing == mMaterials.end()) {
            return LoadMaterial(ResolvedPath, Error) != nullptr;
        }

        auto Candidate{CoreUObject::NewObject<Asset::UMaterial>()};

        if (!Asset::FMaterialFile::Load(ResolvedPath, *this, *Candidate, Error)) {
            return false;
        }

        return ApplyMaterialSettings(*Existing->second, *Candidate, Error);
    }

    /**
     * @brief 캐시 객체의 주소를 유지하면서 머티리얼 설정과 텍스처 참조를 갱신합니다.
     */
    bool Asset::FAssetManager::ApplyMaterialSettings(Asset::UMaterial& Target, const Asset::UMaterial& Source, Core::FString& Error) {
        Target.SetName(Source.GetName());
        Target.SetShader(Source.GetShader());
        Target.SetBaseColor(Source.GetBaseColor());
        Target.SetBlendMode(Source.GetBlendMode());
        Target.SetBaseColorTexturePath(Source.GetBaseColorTexturePath());
        Target.SetNormalMapTexturePath(Source.GetNormalMapTexturePath());
        Target.SetSpecularStrength(Source.GetSpecularStrength());
        Target.SetShininess(Source.GetShininess());
        Target.SetNormalStrength(Source.GetNormalStrength());
        Target.SetTwoSided(Source.IsTwoSided());
        Target.SetSpecularColor(Source.GetSpecularColor());
        Target.SetAmbientColor(Source.GetAmbientColor());
        Target.SetEmissiveColor(Source.GetEmissiveColor());
        Target.SetReflectionColor(Source.GetReflectionColor());
        Target.SetReflectionStrength(Source.GetReflectionStrength());
        Target.SetSpecularTexturePath(Source.GetSpecularTexturePath());
        Target.SetEmissiveTexturePath(Source.GetEmissiveTexturePath());
        Target.SetReflectionTexturePath(Source.GetReflectionTexturePath());

        return Target.ResolveShader(*this, Error) && Target.ResolveTexture(*this, Error);
    }

    /**
     * @brief 노멀 맵을 선형 색상으로 로드하고 전용 캐시에 보관합니다.
     */
    Asset::UTexture2D* Asset::FAssetManager::LoadNormalMap(const std::filesystem::path& Path, Core::FString& Error) {
        Core::FString Key{};
        ResolveAssetLoadPath(Path, Key, Error);

        if (Key.empty()) {
            return nullptr;
        }

        const auto Existing{mNormalMaps.find(Key)};

        if (Existing != mNormalMaps.end()) {
            return Existing->second.get();
        }

        Asset::UTexture2D* Source{LoadTexture(Path, Error)};

        if (Source == nullptr) {
            return nullptr;
        }

        Asset::FTextureAssetData Data{*Source};
        auto Texture{CoreUObject::NewObject<Asset::UTexture2D>()};

        if (!Data.ApplyTo(*Texture, Error) || !Texture->SetSRGB(false)) {
            Error = "Cannot create a linear normal map: " + Error;

            return nullptr;
        }

        Asset::UTexture2D* Result{Texture.get()};

        mNormalMaps.emplace(Key, std::move(Texture));

        return Result;
    }

}
