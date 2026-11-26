/**
 * @file FAssetFile.cpp
 * @brief 에셋 파일의 종류, 버전, 무결성 검사와 바이너리 입출력에 대한 구현입니다.
 * @ingroup Asset
 */

#include "Asset/FAssetFile.h"
#include "Core/FAtomicFileWriter.h"
#include "Core/LoadingStatus.h"
#include "Core/Types.h"

#include <algorithm>
#include <fstream>

namespace Asset {

    /**
     * @brief 이 구현 파일에서만 사용하는 내부 상태와 보조 함수입니다.
     */
    namespace {
        /**
         * @brief Macaw 에셋 파일을 식별하는 헤더의 매직 바이트입니다.
         */
        constexpr Core::TStaticArray<char, 8> AssetMagic{'M', 'A', 'C', 'A', 'W', 'A', 'S', 'T'};

        /**
         * @brief 현재 지원하는 에셋 파일 형식 버전입니다.
         */
        constexpr Uint32 InitialAssetVersion{1};
        constexpr Uint32 GuidAssetVersion{2};
        constexpr Uint32 ThumbnailAssetVersion{3};

        /**
         * @brief 에셋 파일에서 허용하는 최대 페이로드 바이트 수입니다.
         */
        constexpr Uint64 MaximumPayload{512ULL * 1024 * 1024};
        constexpr Uint32 MaximumThumbnail{1024 * 1024};
        constexpr Uint64 MaximumRedirectorPayload{128 * 1024};

        /**
         * @brief 페이로드 앞에 저장하는 에셋 헤더의 바이트 수입니다.
         */
        constexpr Uint64 HeaderSize{24};
        constexpr Uint64 GuidHeaderSize{HeaderSize + 16};
        constexpr Uint64 ThumbnailHeaderSize{GuidHeaderSize + sizeof(Uint32)};

        /**
         * @brief 에셋 파일을 열고 매직·버전·종류·데이터 크기를 검증합니다.
         * @param[in] Path 열 에셋 파일의 경로입니다.
         * @param[in,out] Input 검증할 에셋 파일을 열고 헤더를 읽을 입력 스트림입니다.
         * @param[out] Type 에셋 파일에 기록하거나 검사할 종류입니다.
         * @param[out] Size 처리할 데이터의 바이트 수입니다.
         * @param[out] Error 실패 원인을 받을 오류 문자열입니다.
         * @return 요청한 처리를 완료하면 true, 실패하면 false입니다.
         */
        bool ReadHeader(const std::filesystem::path& Path, std::ifstream& Input, Asset::EAssetType& Type, Uint64& Size, Core::FGuid& Guid, Uint32& ThumbnailSize, Core::FString& Error, Uint32* FileVersion = nullptr) {
            Error.clear();
            Guid = Core::FGuid::Null();
            ThumbnailSize = 0;

            std::error_code Code{};
            const Uint64 FileSize{std::filesystem::file_size(Path, Code)};

            if (Code || Path.extension() != L".masset" || FileSize < HeaderSize || FileSize > ThumbnailHeaderSize + MaximumPayload + MaximumThumbnail) {
                Error = "Cannot read asset file or invalid asset size";

                return false;
            }

            Input.open(Path, std::ios::binary);

            Core::TStaticArray<char, 8> Magic{};
            Uint32 Version{};
            Uint32 TypeValue{};

            Input.read(Magic.data(), Magic.size());
            Input.read(reinterpret_cast<char*>(&Version), sizeof(Version));
            Input.read(reinterpret_cast<char*>(&TypeValue), sizeof(TypeValue));
            Input.read(reinterpret_cast<char*>(&Size), sizeof(Size));

            if (Version == GuidAssetVersion || Version == ThumbnailAssetVersion) {
                Input.read(reinterpret_cast<char*>(Guid.mBytes.data()), Guid.mBytes.size());
            }

            if (Version == ThumbnailAssetVersion) {
                Input.read(reinterpret_cast<char*>(&ThumbnailSize), sizeof(ThumbnailSize));
            }

            const bool ValidVersion{Version == InitialAssetVersion || Version == GuidAssetVersion || Version == ThumbnailAssetVersion};
            const Uint64 ActualHeaderSize{Version == ThumbnailAssetVersion ? ThumbnailHeaderSize : Version == GuidAssetVersion ? GuidHeaderSize : HeaderSize};
            const bool InvalidRedirector{TypeValue == static_cast<Uint32>(Asset::EAssetType::Redirector) && (Size > MaximumRedirectorPayload || Guid.IsValid() || ThumbnailSize != 0)};

            if (!Input || Magic != AssetMagic || !ValidVersion || TypeValue < static_cast<Uint32>(Asset::EAssetType::Scene) || TypeValue > static_cast<Uint32>(Asset::EAssetType::Redirector) || InvalidRedirector || (Version == GuidAssetVersion && !Guid.IsValid()) || Size == 0 || Size > MaximumPayload || ThumbnailSize > MaximumThumbnail || FileSize != ActualHeaderSize + Size + ThumbnailSize) {
                Error = "Invalid asset header, version or type";
                Guid = Core::FGuid::Null();

                return false;
            }

            Type = static_cast<Asset::EAssetType>(TypeValue);
            if (FileVersion != nullptr) {
                *FileVersion = Version;
            }

            return true;
        }

        void WriteHeader(std::ostream& Output, Asset::EAssetType Type, Uint64 Size, const Core::FGuid& Guid, Uint32 ThumbnailSize) {
            const Uint32 TypeValue{static_cast<Uint32>(Type)};
            Output.write(AssetMagic.data(), AssetMagic.size());
            Output.write(reinterpret_cast<const char*>(&ThumbnailAssetVersion), sizeof(ThumbnailAssetVersion));
            Output.write(reinterpret_cast<const char*>(&TypeValue), sizeof(TypeValue));
            Output.write(reinterpret_cast<const char*>(&Size), sizeof(Size));
            Output.write(reinterpret_cast<const char*>(Guid.mBytes.data()), Guid.mBytes.size());
            Output.write(reinterpret_cast<const char*>(&ThumbnailSize), sizeof(ThumbnailSize));
        }

        bool RewriteAssetFile(const std::filesystem::path& Path, std::ifstream& Input, Asset::EAssetType Type, Uint64 Size, const Core::FGuid& Guid, std::span<const Uint8> Thumbnail, std::filesystem::file_time_type SavedTime, Core::FString& Error) {
            Core::FAtomicFileWriter Writer{};
            if (!Writer.Open(Path, Error)) {
                return false;
            }

            std::ostream& Output{Writer.GetStream()};
            WriteHeader(Output, Type, Size, Guid, static_cast<Uint32>(Thumbnail.size()));

            Core::TStaticArray<char, 64 * 1024> Buffer{};
            for (Uint64 Remaining{Size}; Remaining != 0;) {
                const Uint64 Bytes{std::min<Uint64>(Remaining, Buffer.size())};
                const auto Count{static_cast<std::streamsize>(Bytes)};
                if (!Input.read(Buffer.data(), Count) || !Output.write(Buffer.data(), Count)) {
                    Error = "Cannot copy the asset payload";
                    return false;
                }

                Remaining -= Bytes;
            }

            if (!Thumbnail.empty()) {
                Output.write(reinterpret_cast<const char*>(Thumbnail.data()), static_cast<std::streamsize>(Thumbnail.size()));
            }
            Input.close();

            std::error_code Code{};
            if (std::filesystem::last_write_time(Path, Code) != SavedTime || Code) {
                Error = "Asset changed before its file could be updated";
                return false;
            }

            return Writer.Commit(Error);
        }
    }

    /**
     * @brief 에셋 파일 헤더를 검사하여 저장된 에셋 종류를 읽습니다.
     */
    bool Asset::FAssetFile::GetType(const std::filesystem::path& Path, Asset::EAssetType& Type, Core::FString& Error) {
        Core::FGuid Guid{Core::FGuid::Null()};

        return GetIdentity(Path, Type, Guid, Error);
    }

    bool FAssetFile::GetIdentity(const std::filesystem::path& Path, Asset::EAssetType& Type, Core::FGuid& Guid, Core::FString& Error) {
        std::ifstream Input{};
        Uint64 Size{};
        Uint32 ThumbnailSize{};

        return ReadHeader(Path, Input, Type, Size, Guid, ThumbnailSize, Error);
    }

    /**
     * @brief 에셋 종류와 바이트 수를 담은 헤더 및 페이로드를 원자적으로 저장합니다.
     */
    bool Asset::FAssetFile::Save(const std::filesystem::path& Path, Asset::EAssetType Type, std::span<const Uint8> Payload, Core::FString& Error, const Core::FGuid& Guid) {
        Error.clear();

        const bool InvalidRedirector{Type == Asset::EAssetType::Redirector && (Payload.size() > MaximumRedirectorPayload || Guid.IsValid())};
        if (Path.extension() != L".masset" || Payload.empty() || Payload.size() > MaximumPayload || Type < Asset::EAssetType::Scene || Type > Asset::EAssetType::Redirector || InvalidRedirector) {
            Error = "Invalid asset path, type or payload";

            return false;
        }

        std::error_code Code{};
        const std::filesystem::path Target{std::filesystem::absolute(Path, Code)};

        if (Code) {
            Error = "Invalid asset path: " + Code.message();

            return false;
        }

        Asset::EAssetType ExistingType{Type};
        Core::FString ExistingError{};
        Core::FGuid ExistingGuid{Core::FGuid::Null()};

        if (GetIdentity(Target, ExistingType, ExistingGuid, ExistingError) && ExistingType != Type) {
            Error = "Cannot overwrite an asset with a different asset type";

            return false;
        }

        Core::FAtomicFileWriter Writer{};

        if (!Writer.Open(Target, Error)) {
            return false;
        }

        const Uint64 Size{Payload.size()};
        const Core::FGuid Identity{Guid.IsValid() ? Guid : ExistingGuid};
        std::ostream& Output{Writer.GetStream()};
        WriteHeader(Output, Type, Size, Identity, 0);
        Output.write(reinterpret_cast<const char*>(Payload.data()), static_cast<std::streamsize>(Size));

        return Writer.Commit(Error);
    }

    /**
     * @brief 에셋 종류와 데이터 무결성을 검사하여 페이로드를 읽습니다.
     */
    bool Asset::FAssetFile::Load(const std::filesystem::path& Path, Asset::EAssetType Type, Core::TArray<Uint8>& Payload, Core::FString& Error) {
        Core::LoadingStatus::ReportFile("Loading", Path);

        std::ifstream Input{};
        Asset::EAssetType ActualType{Type};
        Uint64 Size{};
        Core::FGuid Guid{Core::FGuid::Null()};
        Uint32 ThumbnailSize{};

        if (!ReadHeader(Path, Input, ActualType, Size, Guid, ThumbnailSize, Error)) {
            return false;
        }

        if (ActualType != Type) {
            Error = "The asset type does not match the requested type";

            return false;
        }

        Core::TArray<Uint8> Candidate{};
        Candidate.resize(static_cast<std::size_t>(Size));
        Input.read(reinterpret_cast<char*>(Candidate.data()), static_cast<std::streamsize>(Size));

        if (!Input) {
            Error = "Truncated asset payload";

            return false;
        }

        Payload = std::move(Candidate);

        return true;
    }

    bool FAssetFile::Migrate(const std::filesystem::path& Path, Asset::EAssetType& Type, bool& Migrated, Core::FString& Error) {
        Migrated = false;

        std::error_code Code{};
        const auto SavedTime{std::filesystem::last_write_time(Path, Code)};
        if (Code) {
            Error = "Cannot read asset modification time: " + Code.message();
            return false;
        }

        std::ifstream Input{};
        Uint64 Size{};
        Core::FGuid Guid{Core::FGuid::Null()};
        Uint32 ThumbnailSize{};
        Uint32 Version{};

        if (!ReadHeader(Path, Input, Type, Size, Guid, ThumbnailSize, Error, &Version)) {
            return false;
        }

        if (Version == ThumbnailAssetVersion) {
            return true;
        }

        Migrated = RewriteAssetFile(Path, Input, Type, Size, Guid, {}, SavedTime, Error);
        return Migrated;
    }

    bool FAssetFile::LoadThumbnail(const std::filesystem::path& Path, Asset::EAssetType Type, Core::TArray<Uint8>& Thumbnail) {
        std::ifstream Input{};
        Asset::EAssetType ActualType{};
        Uint64 Size{};
        Core::FGuid Guid{Core::FGuid::Null()};
        Uint32 ThumbnailSize{};
        Core::FString Error{};

        if (!ReadHeader(Path, Input, ActualType, Size, Guid, ThumbnailSize, Error) || ActualType != Type || ThumbnailSize == 0) {
            return false;
        }

        Core::TArray<Uint8> Candidate{};
        Candidate.resize(ThumbnailSize);
        Input.seekg(static_cast<std::streamoff>(Size), std::ios::cur);
        Input.read(reinterpret_cast<char*>(Candidate.data()), static_cast<std::streamsize>(ThumbnailSize));

        if (!Input) {
            return false;
        }

        Thumbnail = std::move(Candidate);
        return true;
    }

    bool FAssetFile::SaveThumbnail(const std::filesystem::path& Path, Asset::EAssetType Type, std::span<const Uint8> Thumbnail, std::filesystem::file_time_type SavedTime, Core::FString& Error) {
        if (Type == Asset::EAssetType::Redirector) {
            Error = "Cannot save a thumbnail for an asset redirector";
            return false;
        }

        if (Thumbnail.empty() || Thumbnail.size() > MaximumThumbnail) {
            Error = "Invalid asset thumbnail size";
            return false;
        }

        std::error_code Code{};
        if (std::filesystem::last_write_time(Path, Code) != SavedTime || Code) {
            Error = "Asset changed before saving its thumbnail";
            return false;
        }

        std::ifstream Input{};
        Asset::EAssetType ActualType{};
        Uint64 Size{};
        Core::FGuid Guid{Core::FGuid::Null()};
        Uint32 ExistingThumbnailSize{};

        if (!ReadHeader(Path, Input, ActualType, Size, Guid, ExistingThumbnailSize, Error)) {
            return false;
        }

        if (ActualType != Type) {
            Error = "The asset type does not match the thumbnail";
            return false;
        }

        return RewriteAssetFile(Path, Input, Type, Size, Guid, Thumbnail, SavedTime, Error);
    }

}
