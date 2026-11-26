/**
 * @file UStaticMesh.cpp
 * @brief CPU 메시 데이터와 Direct3D 정점·인덱스 버퍼 관리에 대한 구현입니다.
 * @ingroup Asset
 */

#include "Core/Types.h"
#include "Core/Literals/UnitLiterals.h"
#include "Asset/UStaticMesh.h"
#include "Core/FilePath.h"
#include "Asset/Import/IMeshImporter.h"
#include "Asset/Import/FMeshImportResult.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

using namespace Core::Literals;

namespace Asset {

    /**
     * @brief 메시 충돌 형상을 만드는 고정 설정입니다.
     */
    namespace {
        /**
         * @brief 평면처럼 두께가 없는 메시의 충돌 상자가 가질 최소 절반 두께입니다.
         * @note 두께가 0인 상자는 PhysX 형상으로 만들 수 없으므로 얇은 상자로 근사합니다.
         */
        constexpr float MinimumBoxHalfExtent{(0.01_m).GetBase()};
    }

    /**
     * @brief UStaticMesh의 초기 상태를 구성합니다.
     */
    Asset::UStaticMesh::UStaticMesh() = default;

    /**
     * @brief 전달받은 설정과 의존성으로 UStaticMesh의 초기 상태를 구성합니다.
     */
    Asset::UStaticMesh::UStaticMesh(const std::filesystem::path& MeshPath) {
        LoadFromFile(MeshPath);
    }

    /**
     * @brief 객체의 수명을 끝내고 소유한 자원을 정리합니다.
     */
    Asset::UStaticMesh::~UStaticMesh() = default;

    /**
     * @brief 외부 메시 파일을 가져와 정점 속성과 인덱스 및 경계를 구성합니다.
     */
    bool Asset::UStaticMesh::LoadFromFile(const std::filesystem::path& MeshPath) {
        Core::FString Error{};

        return LoadFromFile(MeshPath, Error);
    }

    /**
     * @brief 메시 파일을 읽고 지정된 에셋 관리자로 재질과 텍스처를 해석합니다.
     */
    bool Asset::UStaticMesh::LoadFromFile(const std::filesystem::path& MeshPath, Core::FString& Error) {
        Asset::FMeshImportResult Imported{};

        {
            auto Importer{Asset::IMeshImporter::CreateForSource(MeshPath, Asset::FAssetImportOption{})};

            if (!Importer->Import(Imported, Error)) {
                return false;
            }
        }

        // 기본 옵션은 메시를 병합하므로 결과 메시는 항상 하나입니다.
        if (!SetMeshData(std::move(Imported.GetMeshes().front().GetMeshData()))) {
            Error = "The source contains invalid vertex attributes or mesh sections";

            return false;
        }

        return true;
    }

    /**
     * @brief CPU 메시 데이터의 유효성과 경계를 검사하여 교체하고 GPU 버퍼를 해제합니다.
     */
    bool Asset::UStaticMesh::SetMeshData(Asset::FMeshData MeshData) {
        // 파일 임포트 외에 에셋 로드와 직접 생성한 메시에도 동일한 데이터 규칙을 적용합니다.
        if (!Asset::IMeshImporter::ValidateMeshData(MeshData)) {
            return false;
        }

        mMeshData = std::move(MeshData);
        mConvexElems.clear();
        mSimpleShapes = PhysicsRuntime::FKAggregateGeom{};

        ReleaseResources();
        RebuildBodySetup();

        return true;
    }

    /**
     * @brief 이 메시로 물리 바디를 만들 때 사용할 충돌 형상을 반환합니다.
     */
    const PhysicsRuntime::UBodySetup* Asset::UStaticMesh::GetBodySetup() const {
        return mBodySetup.get();
    }

    /**
     * @brief 임포트할 때 만든 볼록 충돌 형상 목록을 반환합니다.
     */
    const Core::TArray<PhysicsRuntime::FKConvexElem>& Asset::UStaticMesh::GetConvexElems() const {
        return mConvexElems;
    }

    /**
     * @brief 볼록 충돌 형상 목록을 바꾸고 바디 셋업을 다시 만듭니다.
     */
    bool Asset::UStaticMesh::SetConvexElems(Core::TArray<PhysicsRuntime::FKConvexElem> ConvexElems) {
        if (!std::all_of(ConvexElems.begin(), ConvexElems.end(), [](const PhysicsRuntime::FKConvexElem& Element) {
            return Element.IsValid();
        })) {
            return false;
        }

        mConvexElems = std::move(ConvexElems);

        RebuildBodySetup();

        return true;
    }

    /**
     * @brief 사용자가 편집한 단순 충돌 형상을 반환합니다.
     */
    const PhysicsRuntime::FKAggregateGeom& Asset::UStaticMesh::GetSimpleShapes() const {
        return mSimpleShapes;
    }

    /**
     * @brief 단순 충돌 형상을 바꾸고 바디 셋업을 다시 만듭니다.
     */
    bool Asset::UStaticMesh::SetSimpleShapes(PhysicsRuntime::FKAggregateGeom Shapes) {
        Shapes.mConvexElems.clear();

        // 값이 NaN이거나 크기가 0 이하이면 PhysX 형상을 만들 수 없으므로 거부한다.
        const bool Valid{std::all_of(Shapes.mSphereElems.begin(), Shapes.mSphereElems.end(), [](const PhysicsRuntime::FKSphereElem& Elem) {
                             return Elem.mCenter.IsFinite() && Elem.mRadius.IsFinite() && Elem.mRadius > Core::FLength{};
                         }) &&
                         std::all_of(Shapes.mBoxElems.begin(), Shapes.mBoxElems.end(), [](const PhysicsRuntime::FKBoxElem& Elem) {
                             return Elem.mCenter.IsFinite() && Elem.mRotation.IsFinite() && Elem.mHalfExtent.IsFinite() && Elem.mHalfExtent.x > 0.0f && Elem.mHalfExtent.y > 0.0f && Elem.mHalfExtent.z > 0.0f;
                         }) &&
                         std::all_of(Shapes.mSphylElems.begin(), Shapes.mSphylElems.end(), [](const PhysicsRuntime::FKSphylElem& Elem) {
                             return Elem.mCenter.IsFinite() && Elem.mRotation.IsFinite() && Elem.mRadius.IsFinite() && Elem.mRadius > Core::FLength{} && Elem.mLength.IsFinite() && Elem.mLength >= Core::FLength{};
                         })};

        if (!Valid) {
            return false;
        }

        mSimpleShapes = std::move(Shapes);

        RebuildBodySetup();

        return true;
    }

    /**
     * @brief 단순 형상이 있으면 그것으로, 없으면 볼록 형상, 그것도 없으면 메시 경계 상자로 바디 셋업을 다시 만듭니다.
     */
    void Asset::UStaticMesh::RebuildBodySetup() {
        if (!IsValid()) {
            mBodySetup.reset();

            return;
        }

        if (mBodySetup == nullptr) {
            mBodySetup = CoreUObject::NewObject<PhysicsRuntime::UBodySetup>();
            mBodySetup->SetName("BodySetup");
            mBodySetup->SetOuter(this);
        }

        PhysicsRuntime::FKAggregateGeom& Geometry{mBodySetup->GetAggGeom()};
        Geometry = PhysicsRuntime::FKAggregateGeom{};

        if (!mSimpleShapes.IsEmpty()) {
            Geometry.mSphereElems = mSimpleShapes.mSphereElems;
            Geometry.mBoxElems = mSimpleShapes.mBoxElems;
            Geometry.mSphylElems = mSimpleShapes.mSphylElems;

            return;
        }

        if (!mConvexElems.empty()) {
            Geometry.mConvexElems = mConvexElems;

            return;
        }

        // 볼록 형상이 없으면 메시 경계를 감싸는 상자 하나를 단순 충돌 형상으로 사용합니다.

        const FBox& Bounds{GetBounds()};
        const FVector HalfExtent{(Bounds.mMaximum - Bounds.mMinimum) * 0.5f};
        PhysicsRuntime::FKBoxElem& Box{Geometry.mBoxElems.emplace_back()};
        Box.mCenter = (Bounds.mMinimum + Bounds.mMaximum) * 0.5f;
        Box.mHalfExtent = FVector{std::max(HalfExtent.x, MinimumBoxHalfExtent), std::max(HalfExtent.y, MinimumBoxHalfExtent), std::max(HalfExtent.z, MinimumBoxHalfExtent)};
    }

    /**
     * @brief 메시의 CPU 정점 속성·인덱스 데이터 참조를 반환합니다.
     */
    const Asset::FMeshData& Asset::UStaticMesh::GetMeshData() const {
        return mMeshData;
    }

    /**
     * @brief 메시의 로컬 공간 경계 상자를 반환합니다.
     */
    const FBox& Asset::UStaticMesh::GetBounds() const {
        return mMeshData.GetBounds();
    }

    /**
     * @brief 메시의 정점 개수를 반환합니다.
     */
    Uint32 Asset::UStaticMesh::GetVertexCount() const {
        return static_cast<Uint32>(mMeshData.GetPositions().size());
    }

    /**
     * @brief 메시의 인덱스 개수를 반환합니다.
     */
    Uint32 Asset::UStaticMesh::GetIndexCount() const {
        return static_cast<Uint32>(mMeshData.GetIndices().size());
    }

    /**
     * @brief 메시에 포함된 UV 채널 수를 반환합니다.
     */
    Uint32 Asset::UStaticMesh::GetUVChannelCount() const {
        return static_cast<Uint32>(mMeshData.GetUVChannels().size());
    }

    /**
     * @brief CPU 메시의 정점·인덱스·속성 배열이 렌더링 조건을 만족하는지 검사합니다.
     */
    bool Asset::UStaticMesh::IsValid() const {
        return !mMeshData.GetPositions().empty() && !mMeshData.GetIndices().empty();
    }

    /**
     * @brief 현재 CPU 데이터를 사용하여 Direct3D 리소스를 생성합니다.
     */
    bool Asset::UStaticMesh::CreateResources(ID3D11Device* Device) {
        if (Device == nullptr || !IsValid()) {
            return false;
        }

        if (HasResources() && mPositionBuffer.IsCreatedOn(Device)) {
            return true;
        }

        const std::size_t VertexCount{mMeshData.GetPositions().size()};
        const std::size_t ChannelCount{std::max<std::size_t>(1, GetUVChannelCount())};
        Core::TArray<Core::TStaticArray<float, 3>> Positions{};
        Core::TArray<Core::TStaticArray<float, 7>> Tangents{};
        Core::TArray<Core::TStaticArray<float, 2>> UVs{};
        Core::TArray<Core::TStaticArray<float, 4>> Colors{};

        Positions.reserve(VertexCount);
        Tangents.reserve(VertexCount);
        UVs.reserve(VertexCount * ChannelCount);
        Colors.reserve(VertexCount);

        for (Index_t Index{}; Index < VertexCount; ++Index) {
            Positions.push_back(mMeshData.GetPositions()[Index].ToArray());

            const FVector Normal{mMeshData.GetNormals().empty() ? FVector{0.0f, 0.0f, 1.0f} : mMeshData.GetNormals()[Index]};
            const FVector Axis{std::abs(Normal.z) < 0.99f ? FVector{0.0f, 0.0f, 1.0f} : FVector{0.0f, 1.0f, 0.0f}};
            const FVector DefaultTangent{Axis.Cross(Normal).Normalized()};
            const FVector4 Tangent{mMeshData.GetTangents().empty() ? FVector4{DefaultTangent, 1.0f} : mMeshData.GetTangents()[Index]};

            Tangents.push_back({Normal.x, Normal.y, Normal.z, Tangent.x, Tangent.y, Tangent.z, Tangent.w});

            const FVector4 Color{mMeshData.GetColors().empty() ? FVector4{1.0f, 1.0f, 1.0f, 1.0f} : mMeshData.GetColors()[Index]};

            Colors.push_back({Color.x, Color.y, Color.z, Color.w});

            for (Index_t Channel{}; Channel < ChannelCount; ++Channel) {
                const FVector2 UV{GetUVChannelCount() == 0 ? FVector2{} : mMeshData.GetUVChannels()[Channel][Index]};

                UVs.push_back({UV.x, UV.y});
            }
        }

        // 다섯 버퍼를 모두 만든 뒤에 교체하여 실패해도 기존 자원을 유지합니다.
        TMeshVertexBuffer<Core::TStaticArray<float, 3>> PositionBuffer{};
        TMeshVertexBuffer<Core::TStaticArray<float, 7>> TangentBuffer{};
        TMeshVertexBuffer<Core::TStaticArray<float, 2>> UVBuffer{};
        TMeshVertexBuffer<Core::TStaticArray<float, 4>> ColorBuffer{};
        RenderCore::TIndexBuffer<Uint32> IndexBuffer{};
        Core::FString Error{};

        if (!PositionBuffer.Create(Device, Positions, Error) || !TangentBuffer.Create(Device, Tangents, Error) || !UVBuffer.Create(Device, UVs, Error) || !ColorBuffer.Create(Device, Colors, Error) || !IndexBuffer.Create(Device, mMeshData.GetIndices(), Error)) {
            return false;
        }

        mPositionBuffer = std::move(PositionBuffer);
        mTangentBuffer = std::move(TangentBuffer);
        mUVBuffer = std::move(UVBuffer);
        mColorBuffer = std::move(ColorBuffer);
        mIndexBuffer = std::move(IndexBuffer);

        return true;
    }

    /**
     * @brief 소유한 GPU 리소스와 뷰를 해제하고 메모리 집계를 초기화합니다.
     */
    void Asset::UStaticMesh::ReleaseResources() {
        mPositionBuffer.Reset();
        mTangentBuffer.Reset();
        mUVBuffer.Reset();
        mColorBuffer.Reset();
        mIndexBuffer.Reset();
    }

    /**
     * @brief 사용할 수 있는 GPU 리소스를 보유하는지 검사합니다.
     */
    bool Asset::UStaticMesh::HasResources() const {
        return mPositionBuffer.IsCreated() && mTangentBuffer.IsCreated() && mUVBuffer.IsCreated() && mColorBuffer.IsCreated() && mIndexBuffer.IsCreated();
    }

    /**
     * @brief 위치·법선·UV·색상 버퍼를 입력 슬롯 0~3에, 인덱스 버퍼를 함께 연결합니다.
     */
    void Asset::UStaticMesh::Bind(ID3D11DeviceContext* Context) const {
        mPositionBuffer.Bind(Context, 0);
        mTangentBuffer.Bind(Context, 1);
        mUVBuffer.Bind(Context, 2, std::max<Uint32>(1, GetUVChannelCount()));
        mColorBuffer.Bind(Context, 3);
        mIndexBuffer.Bind(Context);
    }

    /**
     * @brief 위치 버퍼만 입력 슬롯 0에, 인덱스 버퍼를 함께 연결합니다.
     */
    void Asset::UStaticMesh::BindPositions(ID3D11DeviceContext* Context) const {
        mPositionBuffer.Bind(Context, 0);
        mIndexBuffer.Bind(Context);
    }

    /**
     * @brief 현재 보유한 GPU 리소스의 추정 바이트 수를 반환합니다.
     */
    std::size_t Asset::UStaticMesh::GetResourceBytes() const {
        return mPositionBuffer.GetResourceBytes() + mTangentBuffer.GetResourceBytes() + mUVBuffer.GetResourceBytes() + mColorBuffer.GetResourceBytes() + mIndexBuffer.GetResourceBytes();
    }

}
