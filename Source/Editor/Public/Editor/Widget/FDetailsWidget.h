/**
 * @file FDetailsWidget.h
 * @brief 선택한 객체의 리플렉션 프로퍼티를 표시하고 편집합니다.
 * @ingroup Editor
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Editor/Widget/FEditorContextWidget.h"
#include "CoreUObject/Reflection/FPropertyValue.h"
#include "CoreUObject/TObjectRef.h"
#include "Editor/FAssetPicker.h"
#include "Core/Types.h"

#include <functional>
#include <optional>
#include <string_view>

namespace CoreUObject {
    class FProperty;
    class UClass;
    class UObject;
}

namespace Engine {
    class AActor;
    class ULightComponent;
    class UMeshComponent;
    class USceneComponent;
    class USkeletalMeshComponent;
}

namespace Editor {

    class FEditorAssetCatalog;
    class FAssetThumbnailCache;

    /**
     * @brief 선택한 객체의 리플렉션 프로퍼티를 표시하고 편집합니다.
     * @ingroup Editor
     */
    class FDetailsWidget final : public Editor::FEditorContextWidget {
    private:

        /**
         * @brief 여러 선택 객체가 공유하는 프로퍼티와 표시 분류·이름입니다.
         * @ingroup Editor
         */
        struct FDetailRow {

            /**
             * @brief 리플렉션 레지스트리가 소유하는 프로퍼티 메타데이터입니다.
             */
            const CoreUObject::FProperty* mProperty{};

            /**
             * @brief 편집 대상 객체의 세대 검증 핸들 목록입니다.
             */
            Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>> mTargets{};

            /**
             * @brief 프로퍼티의 표시 분류입니다.
             */
            std::string_view mCategory{};

            /**
             * @brief UI에 표시할 이름입니다.
             */
            Core::FString mLabel{};

            /**
             * @brief 고급 프로퍼티로 분류할지 여부입니다.
             */
            bool mAdvanced{};

            /**
             * @brief ShowOnlyInnerProperties 구조체에서 이 행이 그리는 멤버이며, 구조체 전체를 그리면 nullptr입니다.
             */
            const CoreUObject::FProperty* mInnerProperty{};
        };

        /**
         * @brief 표시할 내용이 있는 카테고리 하나와 그 안에 그릴 특수 행입니다.
         * @ingroup Editor
         */
        struct FDetailCategory {

            /**
             * @brief 카테고리 이름입니다.
             */
            Core::FString mName{};

            /**
             * @brief 이 카테고리에 속한 프로퍼티 행이 있는지 여부입니다.
             */
            bool mHasRows{};

            /**
             * @brief Animation 카테고리에 애님 그래프 행을 그릴지 여부입니다.
             */
            bool mGraphRow{};

            /**
             * @brief Materials 카테고리에 머티리얼 슬롯을 그릴 메시 컴포넌트이며, 그리지 않으면 nullptr입니다.
             */
            Engine::UMeshComponent* mMaterialMesh{};
        };

        /**
         * @brief 한 프레임에 표시할 행과 카테고리를 그리기 전에 모은 결과입니다.
         * @details 섹션 버튼이 같은 프레임의 대상을 기준으로 나오도록 툴바보다 먼저 만듭니다.
         * @ingroup Editor
         */
        struct FDetailLayout {

            /**
             * @brief 검색·변경 필터를 통과한 프로퍼티 행입니다.
             */
            Core::TArray<FDetailRow> mRows{};

            /**
             * @brief 표시할 내용이 있는 카테고리를 표시 순서대로 담습니다.
             */
            Core::TArray<FDetailCategory> mCategories{};

            /**
             * @brief Shadow Depth Maps를 그릴 라이트이며, 그리지 않으면 nullptr입니다.
             */
            const Engine::ULightComponent* mShadowLight{};

            /**
             * @brief 섹션 버튼과 섹션 필터가 쓰는, 표시할 내용이 있는 카테고리 이름입니다.
             */
            Core::TArray<Core::FString> mAvailableCategories{};
        };

    public:

        /**
         * @brief FDetailsWidget의 초기 상태와 전달받은 의존성을 구성합니다.
         * @param[in,out] Context 패널이 조회하고 편집할 에디터 컨텍스트입니다.
         */
        explicit FDetailsWidget(Editor::FEditorContext* Context);

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~FDetailsWidget() override;

    public:

        /**
         * @brief 브라우저와 공유할 에셋 표시 서비스를 연결합니다.
         * @param Catalog 프로젝트 표시 목록입니다.
         * @param Thumbnails 썸네일 캐시입니다.
         */
        void SetAssetServices(Editor::FEditorAssetCatalog* Catalog, Editor::FAssetThumbnailCache* Thumbnails);

        /**
         * @brief 선택한 에셋 조회와 브라우저 위치 표시 콜백을 연결합니다.
         * @param Selection 브라우저에서 현재 선택한 에셋 경로를 얻는 콜백입니다.
         * @param Browse 지정한 에셋을 브라우저에 표시하는 콜백입니다.
         */
        void SetAssetNavigation(std::function<std::filesystem::path()> Selection, std::function<void(const std::filesystem::path&)> Browse);

        /**
         * @brief Anim Instance 항목의 "Anim Graph 열기" 버튼이 호출할 함수를 연결합니다.
         * @param Opener Anim Graph 패널을 여는 함수입니다.
         */
        void SetAnimGraphOpener(std::function<void()> Opener);

        /**
         * @brief 편집 중인 ImGui 항목이 비활성화되면 장면 편집 범위를 종료합니다.
         */
        void FinishInactiveSceneEdit();

    private:

        /**
         * @brief 활성화된 프로퍼티 항목의 변경을 하나의 장면 편집 범위로 시작합니다.
         */
        void BeginItemSceneEdit();

        /**
         * @brief 대상 고정, 검색, 고급·변경 프로퍼티 필터를 표시합니다.
         * @param Targets 표시·편집할 객체 핸들 목록입니다.
         */
        void DrawToolbar(Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets);

        /**
         * @brief 표시할 카테고리가 있는 섹션만 필터 버튼으로 표시하고 선택을 바꿉니다.
         * @param AvailableCategories 이번 프레임에 표시할 내용이 있는 카테고리 목록이며, 비어 있으면 아무것도 그리지 않습니다.
         */
        void DrawSectionBar(const Core::TArray<Core::FString>& AvailableCategories);

        /**
         * @brief 선택한 섹션이 현재 대상에서 표시할 카테고리를 가지면 그 섹션을, 아니면 All을 반환합니다.
         * @param AvailableCategories 표시할 내용이 있는 카테고리 목록입니다.
         * @return 실제로 적용할 섹션 이름입니다.
         */
        std::string_view GetEffectiveSection(const Core::TArray<Core::FString>& AvailableCategories) const;

        /**
         * @brief 프로퍼티 분류·표시 이름·내부 이름에 검색어가 일치하는지 확인합니다.
         * @param Category 검색하거나 표시할 프로퍼티 분류입니다.
         * @param Label UI 항목의 표시 이름입니다.
         * @param Name 조회하거나 표시할 이름입니다.
         * @return 검색 필터를 통과하면 true입니다.
         */
        bool MatchesSearch(std::string_view Category, std::string_view Label, std::string_view Name = {}) const;

        /**
         * @brief 검색·전체 펼침 상태를 반영하여 프로퍼티 분류를 시작합니다.
         * @param Label UI 항목의 표시 이름입니다.
         * @param DefaultOpen 분류를 처음 표시할 때 펼쳐 둘지 여부입니다.
         * @return 분류 내용을 그려야 하면 true입니다.
         */
        bool BeginCategory(const char* Label, bool DefaultOpen = true) const;

        /**
         * @brief 프로퍼티 이름과 값 위젯을 배치할 표 행을 시작합니다.
         * @param Label UI 항목의 표시 이름입니다.
         */
        void BeginPropertyRow(const char* Label) const;

        /**
         * @brief 여러 객체의 공통 프로퍼티 값과 혼합 상태·초기화 UI를 표시합니다.
         * @param Row 공통 프로퍼티와 대상 객체들을 보관한 행입니다.
         */
        void DrawPropertyRow(const FDetailRow& Row);

        /**
         * @brief 구조체 프로퍼티를 펼쳐 하위 필드를 공통 편집합니다.
         * @details 필드를 바꾸면 대상마다 현재 구조체의 사본에 필드를 쓴 뒤 구조체 전체를 프로퍼티 편집 경로로 적용합니다.
         * @param Row 구조체 프로퍼티와 대상 객체들을 보관한 행입니다.
         * @note 중첩 구조체와 배열 필드는 아직 편집하지 않습니다.
         */
        void DrawStructRow(const FDetailRow& Row);

        /**
         * @brief 여러 선택 객체의 위치·회전·크기를 공통 편집합니다.
         * @param Row 공통 프로퍼티와 대상 객체들을 보관한 행입니다.
         */
        void DrawTransform(const FDetailRow& Row);

        /**
         * @brief 선택한 스켈레탈 컴포넌트들의 애니메이션 재생 모드를 편집합니다.
         * @param Row 공통 프로퍼티와 대상 객체들을 보관한 행입니다.
         */
        void DrawAnimationMode(const FDetailRow& Row);

        /**
         * @brief 선택 객체들에 프로퍼티 값을 적용하고 장면 편집 이력을 기록합니다.
         * @param Row 공통 프로퍼티와 대상 객체들을 보관한 행입니다.
         * @param Value 표시·비교하거나 적용할 값입니다.
         * @param Interactive 드래그 등 연속 편집 중인 변경인지 여부입니다.
         */
        void EditProperty(const FDetailRow& Row, const CoreUObject::FPropertyValue& Value, bool Interactive);

        /**
         * @brief 행의 선택 객체 중 기본값과 다른 프로퍼티가 있는지 확인합니다.
         * @param Row 공통 프로퍼티와 대상 객체들을 보관한 행입니다.
         * @return 한 객체라도 기본값에서 변경되었으면 true입니다.
         */
        bool IsModified(const FDetailRow& Row);

        /**
         * @brief 클래스 기본 객체에서 프로퍼티 초기값을 얻어 캐시합니다.
         * @param Class 기본값을 조회할 리플렉션 클래스입니다.
         * @param Property 표시·직렬화·조회할 프로퍼티 메타데이터입니다.
         * @return 조회 가능한 기본값이며 없으면 빈 optional입니다.
         */
        std::optional<CoreUObject::FPropertyValue> GetDefaultValue(const CoreUObject::UClass* Class, const CoreUObject::FProperty* Property);

        /**
         * @brief 메시 컴포넌트의 재질 슬롯과 에셋 선택기를 표시합니다.
         * @param Component 설정을 표시하거나 조회할 컴포넌트입니다.
         */
        void DrawMaterialSlots(Engine::UMeshComponent* Component);

        /**
         * @brief 선택한 조명의 그림자 맵 미리보기와 관련 정보를 표시합니다.
         * @param Light 그림자 맵을 표시할 조명 컴포넌트입니다.
         */
        void DrawShadowMaps(const Engine::ULightComponent* Light);

        /**
         * @brief 스켈레탈 메시 컴포넌트가 가진 AnimClass 인스턴스의 설정을 표시하고 편집합니다.
         * @param Targets 설정을 함께 표시할 선택 객체 핸들입니다.
         */
        void DrawAnimInstance(const Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets);

        /**
         * @brief 패널 고유의 ImGui 위젯과 내용을 그립니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간(초)입니다.
         */
        void DrawContent(float DeltaTime) override;
        void OnHidden() override;

        /**
         * @brief 생성 가능한 컴포넌트의 검색·추가 메뉴를 표시합니다.
         * @param CanAdd 현재 선택·실행 상태에서 컴포넌트 추가를 허용하는지 여부입니다.
         */
        void DrawComponentMenu(bool CanAdd);

        /**
         * @brief 선택한 액터 하나와 그 컴포넌트를 고르는 트리를 표시합니다.
         * @param Actor 액터와 컴포넌트 계층을 표시할 대상입니다.
         * @param Selected 현재 상세 선택 객체의 핸들입니다.
         */
        void DrawComponentTree(Engine::AActor* Actor, CoreUObject::TObjectRef<CoreUObject::UObject> Selected);

        /**
         * @brief 대상들이 공유하는 프로퍼티 행과 표시할 내용이 있는 카테고리를 모읍니다.
         * @param Targets 공통 프로퍼티를 표시하고 편집할 객체 핸들입니다.
         * @return 검색·변경 필터를 반영한 행과 카테고리이며, 대상을 해석하지 못하면 빈 결과입니다.
         */
        FDetailLayout BuildDetailLayout(const Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets);

        /**
         * @brief 모아 둔 행을 선택한 섹션에 속한 카테고리별로 표시합니다.
         * @param Layout 이번 프레임에 BuildDetailLayout으로 모은 결과입니다.
         * @param Targets 표시·편집할 객체 핸들 목록입니다.
         */
        void DrawDetailLayout(const FDetailLayout& Layout, const Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets);

        /**
         * @brief 프로퍼티 타입에 맞는 편집 위젯을 표시하고 변경한 값을 돌려줍니다.
         * @param[in] Property 그릴 값의 프로퍼티 메타데이터입니다.
         * @param[in,out] Value 프로퍼티 타입에 대응하는 variant 값입니다.
         * @param Label 값 위젯을 구분하는 ImGui 이름입니다.
         * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool DrawPropertyValue(const CoreUObject::FProperty* Property, CoreUObject::FPropertyValue& Value, const char* Label);

        /**
         * @brief 에셋 종류에 맞는 선택기와 드래그 앤 드롭을 표시합니다.
         * @param Label 위젯 이름입니다.
         * @param Path 선택할 Content 기준 경로입니다.
         * @param Type 선택을 허용할 에셋 종류입니다.
         * @param EmptyLabel 경로가 없을 때 표시할 이름입니다.
         * @param InheritedPath 명시적 선택이 없을 때 사용할 상속 에셋 경로입니다.
         * @return 선택 경로가 변경되면 true입니다.
         */
        bool DrawAssetPicker(const char* Label, Core::FString& Path, Asset::EAssetType Type, const char* EmptyLabel, const Core::FString& InheritedPath = {});

    private:

        /**
         * @brief 클래스·프로퍼티별 기본값 캐시입니다.
         */
        Core::TMap<const CoreUObject::UClass*, Core::TMap<const CoreUObject::FProperty*, CoreUObject::FPropertyValue>> mDefaultValues{};

        /**
         * @brief 선택 변경과 무관하게 표시할 고정 객체 핸들입니다.
         */
        Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>> mLockedTargets{};

        /**
         * @brief 고정한 대상이 속한 에디터 컨텍스트의 비소유 포인터입니다.
         */
        Editor::FEditorContext* mLockedContext{};

        /**
         * @brief 프로퍼티 검색용 UTF-8 입력 버퍼입니다.
         */
        Core::TStaticArray<char, 192> mPropertySearch{};

        /**
         * @brief 모든 분류의 펼침 상태를 변경할 일회성 요청입니다.
         */
        std::optional<bool> mExpandCategories{};

        /**
         * @brief 기본값에서 바뀐 프로퍼티만 표시할지 여부입니다.
         */
        bool mOnlyModified{};

        /**
         * @brief 고급 프로퍼티 표시 여부입니다.
         */
        bool mShowAdvanced{};

        /**
         * @brief 사용자가 고른 섹션 이름이며, 대상이 바뀌어도 유지합니다.
         */
        Core::FString mSelectedSection{"All"};

        /**
         * @brief 브라우저의 현재 선택 경로를 얻는 콜백입니다.
         */
        std::function<std::filesystem::path()> mGetSelectedAsset{};

        /**
         * @brief 브라우저에서 에셋 위치를 표시하는 콜백입니다.
         */
        std::function<void(const std::filesystem::path&)> mBrowseAsset{};

        /**
         * @brief Details 컴포넌트 검색 입력 버퍼입니다.
         */
        Core::TStaticArray<char, 128> mComponentSearch{};

        CoreUObject::TObjectRef<Engine::USceneComponent> mRootDropComponent{};

        /**
         * @brief 브라우저와 동일한 에셋 목록입니다.
         */
        Editor::FEditorAssetCatalog* mAssetCatalog{};

        /**
         * @brief 브라우저와 동일한 썸네일 캐시입니다.
         */
        Editor::FAssetThumbnailCache* mThumbnails{};

        /**
         * @brief 썸네일과 드롭다운을 표시하는 선택기입니다.
         */
        Editor::FAssetPicker mAssetPicker{};

        /**
         * @brief Anim Graph 패널을 여는 함수입니다.
         */
        std::function<void()> mAnimGraphOpener{};

        /**
         * @brief 현재 장면 편집 범위를 종료할 토큰이며 비활성 시 0입니다.
         */
        Uint64 mSceneEditToken{};

        /**
         * @brief 장면 편집을 시작한 ImGui 항목 ID입니다.
         */
        Uint32 mSceneEditItem{};
    };

}
