/**
 * @file FDetailsWidget.cpp
 * @brief 선택한 객체의 리플렉션 프로퍼티를 표시하고 편집합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Engine/ALevelSequenceActor.h"
#include "Editor/Widget/FDetailsWidget.h"
#include "Editor/Widget/FEditorQuantityInputWidget.h"
#include "Editor/FEditorContext.h"
#include "Editor/FAssetDragDrop.h"
#include "Editor/FGizmoCore.h"
#include "Engine/AActor.h"
#include "Engine/Component/UActorComponent.h"
#include "Engine/Component/ULuaScriptComponent.h"
#include "Engine/Component/USceneComponent.h"
#include "Engine/Component/UMeshComponent.h"
#include "Engine/Component/ULightComponent.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Engine/Animation/UAnimInstance.h"
#include "Engine/Animation/UAnimGraphInstance.h"
#include "Engine/FDepthOfFieldSettings.h"
#include "RenderCore/IRendererModule.h"
#include "Engine/FProject.h"
#include "Asset/FAssetManager.h"
#include "Asset/FAssetFile.h"
#include "Asset/UStaticMesh.h"
#include "Core/FilePath.h"
#include "Core/Literals/AngleFunctions.h"
#include "Core/Literals/UnitLiterals.h"
#include "CoreUObject/TObjectIterator.h"
#include "Editor/FEditorAssetCatalog.h"
#include "Editor/FAssetThumbnailCache.h"
#include "CoreUObject/UObject.h"
#include "CoreUObject/Reflection/FProperty.h"
#include "CoreUObject/Reflection/UStruct.h"
#include "CoreUObject/Reflection/UClass.h"
#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/FReflectionRegistry.h"
#include "CoreUObject/Reflection/NativeReflection.h"
#include "Serialization/JsonArchive.h"
#include "EditorStyle.h"
#include "Widget/ObjectRow.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

using namespace Core::Literals;

namespace Editor {

    /**
     * @brief Details 패널에서 사용하는 컴포넌트 표시 이름 변환입니다.
     */
    namespace {

        bool DrawLuaScriptSlot(const Editor::FEditorContext& Context, Core::FString& Path) {
            const auto& Style{ImGui::GetStyle()};
            const Core::FString Name{Path.empty() ? "Drop Lua Script" : Core::FilePath::ToUtf8(Core::FilePath::FromUtf8(Path).filename())};
            ImGui::InvisibleButton("##LuaScript", {std::max(1.0f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight()});

            const ImVec2 Minimum{ImGui::GetItemRectMin()};
            const ImVec2 Maximum{ImGui::GetItemRectMax()};
            const ImVec2 TextPosition{Minimum.x + Style.FramePadding.x, Minimum.y + Style.FramePadding.y};
            const ImVec4 Clip{TextPosition.x, Minimum.y, Maximum.x - Style.FramePadding.x, Maximum.y};
            ImGui::RenderFrame(Minimum, Maximum, ImGui::GetColorU32(ImGuiCol_FrameBg), true, Style.FrameRounding);
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), TextPosition, ImGui::GetColorU32(Path.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text), Name.data(), Name.data() + Name.size(), 0.0f, &Clip);

            if (!Path.empty()) {
                ImGui::SetItemTooltip("%s", Path.c_str());
            }

            if ((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0 || !ImGui::BeginDragDropTarget()) {
                return false;
            }

            bool Changed{};

            if (const ImGuiPayload* Payload{ImGui::AcceptDragDropPayload(Editor::EditorAssetDragDrop::LuaScriptPayload)}; Payload != nullptr && Payload->Data != nullptr && Payload->DataSize > 1 && static_cast<const char*>(Payload->Data)[Payload->DataSize - 1] == '\0') {
                const Core::FString Candidate{static_cast<const char*>(Payload->Data), static_cast<std::size_t>(Payload->DataSize - 1)};
                const auto Relative{Core::FilePath::FromUtf8(Candidate)};
                const auto* Project{Context.GetProject()};

                if (Project != nullptr && Project->GetAssets() != nullptr && !Relative.has_root_path() && Core::FilePath::ToLowerExtension(Relative) == L".lua") {
                    Core::FString Error{};
                    const auto Resolved{Project->GetAssets()->ResolvePath(Relative, Error)};
                    std::error_code Code{};

                    if (!Resolved.empty() && std::filesystem::is_regular_file(Resolved, Code) && Path != Candidate) {
                        Path = Candidate;
                        Changed = true;
                    }
                }
            }

            ImGui::EndDragDropTarget();

            return Changed;
        }

        /**
         * @brief 내부 식별자를 사람이 읽기 쉬운 표시 이름으로 변환합니다.
         * @param Name 조회하거나 표시할 이름입니다.
         * @return 공백과 이름 규칙을 반영한 표시 문자열입니다.
         */
        Core::FString DisplayName(std::string_view Name) {
            Core::FString Label{};

            for (Index_t Index{}; Index < Name.size(); ++Index) {
                const auto Character{static_cast<unsigned char>(Name[Index])};

                if (Index > 0 && std::isupper(Character) && (std::islower(static_cast<unsigned char>(Name[Index - 1])) || (Index + 1 < Name.size() && std::islower(static_cast<unsigned char>(Name[Index + 1]))))) {
                    Label.push_back(' ');
                }

                Label.push_back(Name[Index]);
            }

            return Label;
        }

        /**
         * @brief 프로퍼티를 표시할 카테고리를 반환합니다.
         * @param Owner 프로퍼티를 선언한 클래스입니다.
         * @param Property 표시할 프로퍼티 메타데이터입니다.
         * @return 리플렉션에 적은 카테고리이며, 없으면 선언 클래스 이름에서 U·A 접두어를 뗀 이름입니다.
         */
        std::string_view DetailCategory(const CoreUObject::UClass* Owner, const CoreUObject::FProperty* Property) {
            if (!Property->GetCategory().empty()) {
                return Property->GetCategory();
            }

            const std::string_view ClassName{Owner->GetName()};

            return ClassName.size() > 1 && (ClassName.front() == 'U' || ClassName.front() == 'A') && std::isupper(static_cast<unsigned char>(ClassName[1])) ? ClassName.substr(1) : ClassName;
        }

        /**
         * @brief 디테일 패널 섹션 버튼 하나와 그 섹션이 묶는 카테고리입니다.
         */
        struct FDetailSection {
            /**
             * @brief 버튼에 표시할 섹션 이름입니다.
             */
            std::string_view mName;

            /**
             * @brief 섹션에 속한 카테고리 이름입니다. 한 카테고리가 여러 섹션에 속할 수 있습니다.
             */
            Core::TArray<std::string_view> mCategories;
        };

        /**
         * @brief 버튼 순서대로 정렬한 섹션 목록을 반환합니다.
         * @return 정적 섹션 목록의 참조입니다. All은 목록에 없고 항상 마지막에 표시합니다.
         * @details Misc는 적어 둔 카테고리와 함께 다른 섹션에 속하지 않는 카테고리를 모두 받습니다.
         */
        const Core::TArray<FDetailSection>& GetDetailSections() {
            static const Core::TArray<FDetailSection> Sections{
                {"General", {"Transform", "Mesh", "Materials", "Shape", "Light", "Atmosphere", "Camera", "Fog", "Script", "General", "Settings"}},
                {"Actor", {"Actor Tick"}},
                {"Animation", {"Animation"}},
                {"Physics", {"Physics", "Collision"}},
                {"Rendering", {"Rendering", "Materials", "Light", "Atmosphere", "Fog"}},
                {"Misc", {"Movement", "Component Tick"}},
            };

            return Sections;
        }

        /**
         * @brief 카테고리가 섹션에 속하는지 검사합니다.
         * @param Section 섹션 이름이며, All이면 모든 카테고리가 속합니다.
         * @param Category 검사할 카테고리 이름입니다.
         * @return 섹션에 속하면 true입니다.
         */
        bool IsCategoryInSection(std::string_view Section, std::string_view Category) {
            if (Section == "All") {
                return true;
            }

            bool Listed{};

            for (const FDetailSection& Entry : GetDetailSections()) {
                const bool Contains{std::find(Entry.mCategories.begin(), Entry.mCategories.end(), Category) != Entry.mCategories.end()};

                if (Entry.mName == Section && Contains) {
                    return true;
                }

                Listed = Listed || Contains;
            }

            return Section == "Misc" && !Listed;
        }

        /**
         * @brief 리플렉션 메타데이터와 이름 규칙으로 프로퍼티 표시 이름을 만듭니다.
         * @param Property 표시·직렬화·조회할 프로퍼티 메타데이터입니다.
         * @param Struct 구조체 필드를 표시할 때 해당 필드를 선언한 구조체입니다.
         * @return 상세 패널에 표시할 이름입니다.
         */
        Core::FString PropertyLabel(const CoreUObject::FProperty* Property, const CoreUObject::UScriptStruct* Struct = nullptr) {
            const Core::FString& Name{Property->GetName()};

            if (Struct != nullptr && Struct == Engine::FDepthOfFieldSettings::StaticStruct()) {
                if (Name == "MaxBlurRadius") {
                    return "Max Blur Radius (px)";
                }

                if (Name == "FStop") {
                    return "F-stop";
                }

                if (Name == "FocalDistance") {
                    return "Focus Distance";
                }
            }

            if (Name == "UseFieldOfView") {
                return "Use Field of View";
            }

            if (Name == "ShadowBiasPreset") {
                return "Depth Bias Preset";
            }

            if (Name == "ShadowSlopeBiasPreset") {
                return "Slope-Scaled Bias Preset";
            }

            if (Name == "Animation") {
                return "Anim to Play";
            }

            if (Name == "Material") {
                return "All Slots Override";
            }

            return DisplayName(Name);
        }

        /**
         * @brief 프로퍼티 이름에 따른 고급 항목 분류를 확인합니다.
         * @param Name 조회하거나 표시할 이름입니다.
         * @return 고급 표시 필터의 대상이면 true입니다.
         */
        bool IsAdvancedProperty(std::string_view Name) {
            return Name == "EditorTickable" || Name.starts_with("Shadow") || Name == "ReceivesDecals";
        }

        /**
         * @brief 선택 객체가 액터 또는 컴포넌트일 때 소유 액터를 찾습니다.
         * @param Object 외부에서 수명을 보장하는 에셋 객체입니다.
         * @return 비소유 액터 포인터이며 해당하지 않으면 nullptr입니다.
         */
        Engine::AActor* GetOwningActor(CoreUObject::UObject* Object) {
            if (auto* Actor{CoreUObject::Cast<Engine::AActor>(Object)}) {
                return Actor;
            }

            auto* Component{CoreUObject::Cast<Engine::UActorComponent>(Object)};

            return Component != nullptr ? Component->GetOwner() : nullptr;
        }

        /**
         * @brief 프로퍼티 variant의 형식과 실제 값을 비교합니다.
         * @param First 비교할 첫 번째 프로퍼티 값입니다.
         * @param Second 비교할 두 번째 프로퍼티 값입니다.
         * @return 형식과 값이 같으면 true입니다.
         */
        bool EqualValues(const CoreUObject::FPropertyValue& First, const CoreUObject::FPropertyValue& Second) {
            return std::visit([&Second](const auto& Value) {
                using FValue = std::decay_t<decltype(Value)>;
                const auto* Other{std::get_if<FValue>(&Second)};

                if (Other == nullptr) {
                    return false;
                }

                if constexpr (std::is_same_v<FValue, FTransform>) {
                    return Value.Equals(*Other, 0.0f);
                } else if constexpr (std::is_same_v<FValue, CoreUObject::FObjectReference>) {
                    return Value.GetGuid() == Other->GetGuid();
                } else if constexpr (requires { Value == *Other; }) {
                    return Value == *Other;
                } else {
                    return false;
                }
            }, First);
        }

        /**
         * @brief 프로퍼티 variant의 현재 형식에 맞춰 상세 편집 값을 직렬화합니다.
         * @param Archive 저장·로드 방향과 오류 상태를 제공하는 아카이브입니다.
         * @param Value 표시·비교하거나 적용할 값입니다.
         * @return 지원하는 형식의 직렬화가 성공하면 true입니다.
         */
        bool ArchiveDetailValue(Serialization::FJsonArchive& Archive, CoreUObject::FPropertyValue& Value) {
            if (Archive.IsLoading() && !Archive.HasField("Value")) {
                return false;
            }

            return std::visit([&Archive](auto& Field) {
                using FValue = std::decay_t<decltype(Field)>;

                if constexpr (std::is_same_v<FValue, CoreUObject::FQuantityPropertyValue>) {
                    if (!Archive.BeginObject("Value")) {
                        return false;
                    }

                    CoreUObject::FQuantityPropertyValue Quantity{Field};
                    Archive.Serialize("Length", Quantity.mDimension.mLength);
                    Archive.Serialize("Mass", Quantity.mDimension.mMass);
                    Archive.Serialize("Time", Quantity.mDimension.mTime);
                    Archive.Serialize("Angle", Quantity.mDimension.mAngle);
                    Archive.Serialize("BaseValue", Quantity.mBaseValue);
                    Archive.EndObject();

                    if (Archive.HasError() || Quantity.mDimension != Field.mDimension || !std::isfinite(Quantity.mBaseValue)) {
                        return false;
                    }

                    Field = Quantity;

                    return true;
                } else if constexpr (requires { Archive.Serialize("Value", Field); }) {
                    Archive.Serialize("Value", Field);

                    return !Archive.HasError();
                } else {
                    return false;
                }
            }, Value);
        }

        /**
         * @brief 프로퍼티 기본값 복원 버튼을 표시합니다.
         * @return 버튼을 눌렀으면 true입니다.
         */
        bool DrawResetButton() {
            return Editor::EditorStyle::IconButton("Reset to Default", Editor::EditorStyle::EIcon::Reset, false, true, false, nullptr, 0.0f, Editor::EditorStyle::EIconSelectionStyle::Icon);
        }

        /**
         * @brief 이름·값 열을 사용하는 상세 프로퍼티 표를 시작합니다.
         * @return 표가 시작되어 행을 그릴 수 있으면 true입니다.
         */
        bool BeginPropertyTable() {
            constexpr ImGuiTableFlags Flags{ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp};

            if (!ImGui::BeginTable("##Properties", 3, Flags)) {
                return false;
            }

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, ImGui::GetFrameHeight());

            return true;
        }

        /**
         * @brief 클래스 접두사와 Component 접미사를 제거해 컴포넌트 메뉴 이름을 만듭니다.
         * @param[in] Class 생성하거나 이름을 표시할 리플렉션 클래스입니다.
         * @return 계산하거나 조회한 값입니다.
         */
        Core::FString ComponentLabel(const CoreUObject::UClass* Class) {
            Core::FString Name{Class->GetName()};

            if (Name.starts_with("U")) {
                Name.erase(0, 1);
            }

            if (Name.ends_with("Component")) {
                Name.resize(Name.size() - 9);
            }

            return DisplayName(Name);
        }

        /**
         * @brief Details에 표시할 비일시적 편집 가능 또는 읽기 전용 프로퍼티인지 검사합니다.
         * @param[in] Property 유효한 리플렉션 프로퍼티의 비소유 주소입니다.
         * @return Transient가 아니고 Edit 또는 ReadOnly 플래그가 있으면 true입니다.
         */
        bool IsVisibleProperty(const CoreUObject::FProperty* Property) {
            return !Property->HasFlag(Core::EPropertyFlags::Transient) && (Property->HasFlag(Core::EPropertyFlags::Edit) || Property->HasFlag(Core::EPropertyFlags::ReadOnly));
        }

        /**
         * @brief 필드 이름을 사용 가능한 너비에 맞춰 표시합니다.
         * @param Label UI 항목의 표시 이름입니다.
         * @param TrailingWidth 이름 뒤 위젯에 남겨 둘 너비(픽셀)입니다.
         * @return 위젯 구분에 사용할 필드 이름 문자열입니다.
         */
        Core::FString DrawFieldLabel(const char* Label, float TrailingWidth = 0.0f) {
            const float LabelEnd{ImGui::GetCursorPosX() + std::max(1.0f, ImGui::GetContentRegionAvail().x * 0.4f)};

            ImGui::AlignTextToFramePadding();
            ImGui::PushTextWrapPos(LabelEnd);
            ImGui::TextUnformatted(Label);
            ImGui::PopTextWrapPos();

            ImGui::SameLine();
            ImGui::SetCursorPosX(LabelEnd + ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - TrailingWidth));

            return Core::FString{"##"} + Label;
        }

    }

    /**
     * @brief FDetailsWidget의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Editor::FDetailsWidget::FDetailsWidget(Editor::FEditorContext* Context)
		: Editor::FEditorContextWidget{Context, "Details"} {
    }

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Editor::FDetailsWidget::~FDetailsWidget() = default;

    /**
     * @brief 브라우저와 공유할 에셋 표시 서비스를 연결합니다.
     */
    void Editor::FDetailsWidget::SetAssetServices(Editor::FEditorAssetCatalog* Catalog, Editor::FAssetThumbnailCache* Thumbnails) {
        mAssetCatalog = Catalog;
        mThumbnails = Thumbnails;
    }

    void Editor::FDetailsWidget::SetAssetNavigation(std::function<std::filesystem::path()> Selection, std::function<void(const std::filesystem::path&)> Browse) {
        mGetSelectedAsset = std::move(Selection);
        mBrowseAsset = std::move(Browse);
    }

    void Editor::FDetailsWidget::SetAnimGraphOpener(std::function<void()> Opener) {
        mAnimGraphOpener = std::move(Opener);
    }

    void Editor::FDetailsWidget::DrawAnimInstance(const Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets) {
        Editor::FEditorContext* Context{GetEditorContext()};
        Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>> Instances{};

        for (const CoreUObject::TObjectRef<CoreUObject::UObject> Target : Targets) {
            const auto* Component{CoreUObject::Cast<Engine::USkeletalMeshComponent>(Context->ResolveObject(Target))};
            auto* Instance{Component != nullptr ? CoreUObject::Cast<Engine::UAnimGraphInstance>(Component->GetAnimInstance()) : nullptr};

            if (Instance == nullptr) {
                return;
            }

            Instances.push_back(CoreUObject::MakeObjectRef(Instance));
        }

        const auto* GraphProperty{Engine::UAnimGraphInstance::StaticClass()->FindClassProperty("AnimGraph")};

        if (GraphProperty != nullptr && MatchesSearch("Animation", "Anim Graph", "AnimGraph")) {
            DrawPropertyRow({GraphProperty, std::move(Instances), "Animation", "Anim Graph"});
        }

        CoreUObject::UObject* Selected{Context->GetSelectedObject()};

        if (Targets.size() == 1 && Selected != nullptr && CoreUObject::MakeObjectRef(Selected) == Targets.front() && mAnimGraphOpener && !mOnlyModified && MatchesSearch("Animation", "Debug Anim Graph")) {
            BeginPropertyRow("Debug");
            ImGui::TableSetColumnIndex(1);

            if (ImGui::Button("Open Anim Graph")) {
                mAnimGraphOpener();
            }
        }
    }

    /**
     * @brief 패널 고유의 ImGui 위젯과 내용을 그립니다.
     */
    void Editor::FDetailsWidget::DrawContent(float) {
        Editor::FEditorContext* Context{GetEditorContext()};
        Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>> Targets{};

        if (mLockedContext == Context) {
            std::erase_if(mLockedTargets, [Context](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
                return Context->ResolveObject(Target) == nullptr;
            });
        }

        if (mLockedContext != Context || mLockedTargets.empty()) {
            mLockedContext = nullptr;
            mLockedTargets.clear();
            const auto SelectedActors{Context->GetSelectedActors()};
            Targets.assign(SelectedActors.begin(), SelectedActors.end());

            if (Targets.size() < 2) {
                Targets.clear();

                if (CoreUObject::UObject* Selected{Context->GetSelectedObject()}) {
                    Targets.push_back(CoreUObject::MakeObjectRef(Selected));
                }
            }
        } else {
            Targets = mLockedTargets;
        }

        auto* Selected{Targets.size() == 1 ? Context->ResolveObject(Targets.front()) : nullptr};
        auto* Actor{GetOwningActor(Selected)};

        if (Actor != nullptr) {
            DrawComponentMenu(mLockedContext == nullptr && Context->CanEditWorld());
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Actor->GetName().c_str());
            DrawComponentTree(Actor, CoreUObject::MakeObjectRef(Selected));

            if (mLockedContext != nullptr) {
                Targets = mLockedTargets;
            } else if (CoreUObject::UObject* Current{Context->GetSelectedObject()}) {
                Targets.front() = CoreUObject::MakeObjectRef(Current);
            }
        } else if (Targets.size() > 1) {
            ImGui::Text("%zu Actors Selected", Targets.size());
        }

        DrawToolbar(Targets);

        // 섹션 버튼이 이번 프레임의 대상과 검색·옵션을 기준으로 나오도록, 툴바 입력을 반영한 뒤 그리기 전에 행을 모읍니다.
        const FDetailLayout Layout{Targets.empty() ? FDetailLayout{} : BuildDetailLayout(Targets)};

        DrawSectionBar(Layout.mAvailableCategories);
        ImGui::Separator();

        if (ImGui::BeginChild("##DetailProperties")) {
            if (Targets.empty()) {
                ImGui::TextDisabled("Select an actor or component to view its details.");
            } else {
                DrawDetailLayout(Layout, Targets);
            }
        }

        ImGui::EndChild();

        const ImGuiIO& Input{ImGui::GetIO()};

        if (IsEnabled() && mLockedContext == nullptr && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && !Input.WantTextInput && Input.KeyMods == 0 && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            Context->DestroySelectedComponent();
        }

        mExpandCategories.reset();
    }

    void Editor::FDetailsWidget::DrawToolbar(Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets) {
        const bool Locked{mLockedContext != nullptr};
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const float Buttons{ImGui::CalcTextSize("Unlock").x + ImGui::CalcTextSize("Options").x + Style.FramePadding.x * 4.0f + Style.ItemSpacing.x * 2.0f};
        ImGui::SetNextItemWidth(std::max(24.0f, ImGui::GetContentRegionAvail().x - Buttons));
        ImGui::InputTextWithHint("##SearchDetails", "Search Details", mPropertySearch.data(), mPropertySearch.size(), ImGuiInputTextFlags_EscapeClearsAll);
        ImGui::SameLine();
        ImGui::BeginDisabled(Targets.empty());

        if (ImGui::Button(Locked ? "Unlock###LockDetails" : "Lock###LockDetails")) {
            mLockedContext = Locked ? nullptr : GetEditorContext();
            mLockedTargets = Locked ? Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>{} : Targets;
        }

        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Keep this selection in Details");
        ImGui::SameLine();

        if (ImGui::Button("Options")) {
            ImGui::OpenPopup("##DetailOptions");
        }

        if (ImGui::BeginPopup("##DetailOptions")) {
            ImGui::MenuItem("Show Only Modified Properties", nullptr, &mOnlyModified);
            ImGui::MenuItem("Show All Advanced Details", nullptr, &mShowAdvanced);
            ImGui::Separator();

            if (ImGui::MenuItem("Expand All Categories")) {
                mExpandCategories = true;
            }

            if (ImGui::MenuItem("Collapse All Categories")) {
                mExpandCategories = false;
            }

            ImGui::EndPopup();
        }
    }

    void Editor::FDetailsWidget::DrawSectionBar(const Core::TArray<Core::FString>& AvailableCategories) {
        Core::TArray<std::string_view> Visible{};

        for (const FDetailSection& Section : GetDetailSections()) {
            if (std::any_of(AvailableCategories.begin(), AvailableCategories.end(), [&Section](const Core::FString& Category) {
                return IsCategoryInSection(Section.mName, Category);
            })) {
                Visible.push_back(Section.mName);
            }
        }

        if (Visible.empty()) {
            return;
        }

        Visible.push_back("All");

        const std::string_view Effective{GetEffectiveSection(AvailableCategories)};
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const float Right{ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x};

        for (Index_t Index{}; Index < Visible.size(); ++Index) {
            const Core::FString Label{Visible[Index]};
            const float Width{ImGui::CalcTextSize(Label.c_str()).x + Style.FramePadding.x * 2.0f};

            // 남은 폭에 버튼이 들어가지 않으면 다음 줄로 넘깁니다.
            if (Index > 0 && ImGui::GetItemRectMax().x + Style.ItemSpacing.x + Width <= Right) {
                ImGui::SameLine();
            }

            if (Editor::EditorStyle::Button(Label.c_str(), {}, Visible[Index] == Effective)) {
                mSelectedSection = Label;
            }
        }
    }

    std::string_view Editor::FDetailsWidget::GetEffectiveSection(const Core::TArray<Core::FString>& AvailableCategories) const {
        const bool HasCategory{std::any_of(AvailableCategories.begin(), AvailableCategories.end(), [this](const Core::FString& Category) {
            return IsCategoryInSection(mSelectedSection, Category);
        })};

        return HasCategory ? std::string_view{mSelectedSection} : std::string_view{"All"};
    }

    bool Editor::FDetailsWidget::MatchesSearch(std::string_view Category, std::string_view Label, std::string_view Name) const {
        std::string_view Search{mPropertySearch.data()};

        while (!Search.empty()) {
            const std::size_t Start{Search.find_first_not_of(" \t")};

            if (Start == std::string_view::npos) {
                break;
            }

            Search.remove_prefix(Start);
            const std::size_t End{Search.find_first_of(" \t")};
            const std::string_view Term{Search.substr(0, End)};
            const auto Matches{[Term](std::string_view Text) {
                return !Text.empty() && ImStristr(Text.data(), Text.data() + Text.size(), Term.data(), Term.data() + Term.size()) != nullptr;
            }};

            if (!Matches(Category) && !Matches(Label) && !Matches(Name)) {
                return false;
            }

            if (End == std::string_view::npos) {
                break;
            }

            Search.remove_prefix(End);
        }

        return true;
    }

    bool Editor::FDetailsWidget::BeginCategory(const char* Label, bool DefaultOpen) const {
        const bool Searching{mPropertySearch.front() != '\0'};

        if (Searching) {
            ImGui::PushID("Search");
        }

        if (mExpandCategories.has_value()) {
            ImGui::SetNextItemOpen(*mExpandCategories, ImGuiCond_Always);
        }

        const ImGuiTreeNodeFlags Flags{DefaultOpen || Searching ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None};
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));

        const bool Open{ImGui::CollapsingHeader(Label, Flags)};

        ImGui::PopStyleColor(3);

        if (Searching) {
            ImGui::PopID();
        }

        return Open;
    }

    void Editor::FDetailsWidget::BeginPropertyRow(const char* Label) const {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(Label);
        ImGui::PopTextWrapPos();
    }

    void Editor::FDetailsWidget::OnHidden() {
        if (mSceneEditToken != 0) {
            GetEditorContext()->EndSceneEdit(mSceneEditToken);
            mSceneEditToken = 0;
            mSceneEditItem = 0;
        }
    }

    void Editor::FDetailsWidget::FinishInactiveSceneEdit() {
        if (mSceneEditToken == 0 || (IsVisible() && ImGui::GetActiveID() == mSceneEditItem && GetEditorContext()->IsSceneEditActive())) {
            return;
        }

        GetEditorContext()->EndSceneEdit(mSceneEditToken);
        mSceneEditToken = 0;
        mSceneEditItem = 0;
    }

    void Editor::FDetailsWidget::BeginItemSceneEdit() {
        FinishInactiveSceneEdit();
        const ImGuiID Item{ImGui::GetActiveID()};

        if (mSceneEditToken == 0 && Item != 0) {
            mSceneEditToken = GetEditorContext()->BeginSceneEdit();
            mSceneEditItem = Item;
        }
    }

    /**
     * @brief 생성 가능한 컴포넌트의 검색·추가 메뉴를 표시합니다.
     */
    void Editor::FDetailsWidget::DrawComponentMenu(bool CanAdd) {
        Editor::FEditorContext* Context{GetEditorContext()};

        ImGui::BeginDisabled(!CanAdd);

        if (ImGui::Button("+ Add")) {
            mComponentSearch.fill('\0');
            ImGui::OpenPopup("AddComponent");
        }

        ImGui::EndDisabled();

        if (!CanAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("컴포넌트를 추가할 액터 하나를 선택하세요.");
        }

        if (ImGui::BeginPopup("AddComponent")) {
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##ComponentSearch", "컴포넌트 검색", mComponentSearch.data(), mComponentSearch.size());
            ImGui::Separator();

            const ImGuiTextFilter Filter{mComponentSearch.data()};
            bool HasEntries{};

            for (const CoreUObject::UClass* Class : CoreUObject::FReflectionRegistry::GetDerivedClasses(Engine::UActorComponent::StaticClass())) {
                const Core::FString Label{ComponentLabel(Class)};

                if (!Class->CanCreate() || (!Filter.PassFilter(Label.c_str()) && !Filter.PassFilter(Class->GetName().c_str()))) {
                    continue;
                }

                HasEntries = true;

                ImGui::PushID(Class);
                ImGui::BeginDisabled(!Context->CanAddComponent(Class));

                if (ImGui::Selectable(Label.c_str(), false, ImGuiSelectableFlags_DontClosePopups) && Context->AddComponent(Class) != nullptr) {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndDisabled();
                ImGui::PopID();
            }

            if (!HasEntries) {
                ImGui::TextDisabled("검색 결과가 없습니다.");
            }

            ImGui::EndPopup();
        }

        ImGui::Separator();
    }

    /**
     * @brief 선택한 액터 하나와 그 컴포넌트를 고르는 트리를 표시합니다.
     */
    void Editor::FDetailsWidget::DrawComponentTree(Engine::AActor* Actor, CoreUObject::TObjectRef<CoreUObject::UObject> Selected) {
        Editor::FEditorContext* Context{GetEditorContext()};
        const CoreUObject::TObjectRef<CoreUObject::UObject> ActorHandle{CoreUObject::MakeObjectRef(Actor)};
        Engine::USceneComponent* Root{Actor->GetRootComponent()};
        Core::TMap<const Engine::USceneComponent*, Core::TArray<Engine::UActorComponent*>> Children{};

        for (const auto& Component : Actor->GetComponents()) {
            const auto* Scene{CoreUObject::Cast<Engine::USceneComponent>(Component.get())};
            const Engine::USceneComponent* Parent{};

            if (Scene != nullptr && Scene != Root) {
                Parent = Scene->GetAttachParent() != nullptr ? Scene->GetAttachParent() : Root;
            }

            if (Scene != Root || Root == nullptr) {
                Children[Parent].push_back(Component.get());
            }
        }

        constexpr std::size_t MaxVisibleRows{8};
        const float RowHeight{ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f};
        const float Height{static_cast<float>(std::min(Actor->GetComponents().size() + 1, MaxVisibleRows)) * RowHeight + 4.0f};
        constexpr ImGuiTableFlags Flags{ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY};
        bool OpenRootDropMenu{};

        if (ImGui::BeginTable("##Components", 1, Flags, {0.0f, Height})) {
            const auto DrawRow{[this, Context, Selected, &OpenRootDropMenu](CoreUObject::TObjectRef<CoreUObject::UObject> Handle, float Indent) {
                CoreUObject::UObject* Object{Context->ResolveObject(Handle)};

                if (Object == nullptr) {
                    return;
                }

                ImGui::PushID(Object);
                ImGui::TableNextRow();
                ObjectRow::DrawLabel(*Context, Object, Handle == Selected, Indent);

                if (ImGui::IsItemClicked()) {
                    if (mLockedContext != nullptr) {
                        mLockedTargets = {Handle};
                    } else {
                        Context->SelectObject(Handle);
                    }
                }

                const auto* RowActor{CoreUObject::Cast<Engine::AActor>(Object)};

                if (RowActor != nullptr && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    Context->OpenInSequencer(CoreUObject::Cast<Engine::ALevelSequenceActor>(Object));
                }

                ImGui::SetItemTooltip("%s", ObjectRow::GetTypeLabel(Object).c_str());

                auto* Scene{RowActor != nullptr ? RowActor->GetRootComponent() : CoreUObject::Cast<Engine::USceneComponent>(Object)};

                if (Scene != nullptr && Scene->GetAttachParent() != nullptr && ImGui::BeginPopupContextItem("AttachmentMenu")) {
                    if (ImGui::MenuItem("Detach from Parent", nullptr, false, Context->CanAttachComponent(Scene, nullptr))) {
                        Context->AttachComponent(Scene, nullptr);
                    }

                    ImGui::EndPopup();
                }

                if (auto* Candidate{ObjectRow::DrawAttachmentDragDrop(*Context, Object, true)}) {
                    mRootDropComponent = CoreUObject::MakeObjectRef(Candidate);
                    OpenRootDropMenu = true;
                }

                ImGui::PopID();
            }};

            const float IndentStep{ImGui::GetTreeNodeToLabelSpacing()};
            const auto Visit{[&](const auto& VisitChildren, const Engine::USceneComponent* Parent, float Indent) -> void {
                const auto It{Children.find(Parent)};

                if (It == Children.end()) {
                    return;
                }

                for (Engine::UActorComponent* Component : It->second) {
                    DrawRow(CoreUObject::MakeObjectRef(Component), Indent);

                    if (const auto* Scene{CoreUObject::Cast<Engine::USceneComponent>(Component)}) {
                        VisitChildren(VisitChildren, Scene, Indent + IndentStep);
                    }
                }
            }};

            DrawRow(ActorHandle, 0.0f);

            if (Root != nullptr) {
                DrawRow(CoreUObject::MakeObjectRef(Root), IndentStep);
                Visit(Visit, Root, IndentStep * 2.0f);
            }

            Visit(Visit, nullptr, IndentStep);

            ImGui::EndTable();
        }

        if (OpenRootDropMenu) {
            ImGui::OpenPopup("RootComponentDrop");
        }

        if (ImGui::BeginPopup("RootComponentDrop")) {
            auto* Component{CoreUObject::Cast<Engine::USceneComponent>(Context->ResolveObject(mRootDropComponent))};

            if (Component == nullptr || Component->GetOwner() != Actor || !Context->CanPromoteRootComponent(Component)) {
                ImGui::CloseCurrentPopup();
            } else {
                if (ImGui::MenuItem("Make New Root")) {
                    Context->PromoteRootComponent(Component);
                }

                if (ImGui::MenuItem("Attach to Current Root", nullptr, false, Context->CanAttachComponent(Component, nullptr))) {
                    Context->AttachComponent(Component, nullptr);
                }
            }

            ImGui::EndPopup();
        }
    }

    /**
     * @brief 대상들이 공유하는 프로퍼티 행과 표시할 내용이 있는 카테고리를 모읍니다.
     */
    Editor::FDetailsWidget::FDetailLayout Editor::FDetailsWidget::BuildDetailLayout(const Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets) {
        FDetailLayout Layout{};
        Editor::FEditorContext* Context{GetEditorContext()};
        CoreUObject::UObject* Object{Context->ResolveObject(Targets.front())};

        if (Object == nullptr) {
            return Layout;
        }

        const auto* CommonClass{Object->GetClass()};

        for (const CoreUObject::TObjectRef<CoreUObject::UObject> Handle : Targets) {
            const CoreUObject::UObject* Other{Context->ResolveObject(Handle)};

            if (Other == nullptr) {
                return Layout;
            }

            while (!Other->IsA(CommonClass)) {
                CommonClass = CommonClass->GetSuperClass();
            }
        }

        Core::TArray<const CoreUObject::UClass*> Classes{};
        Core::TArray<FDetailRow>& Rows{Layout.mRows};
        Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>> Roots{};

        const bool TransformMatches{MatchesSearch("Transform", "Location") || MatchesSearch("Transform", "Rotation") || MatchesSearch("Transform", "Scale")};

        if (TransformMatches && CommonClass->IsChildOf(Engine::AActor::StaticClass())) {
            for (const CoreUObject::TObjectRef<CoreUObject::UObject> Handle : Targets) {
                const auto* Actor{CoreUObject::Cast<Engine::AActor>(Context->ResolveObject(Handle))};

                if (Actor != nullptr && Actor->GetRootComponent() != nullptr) {
                    Roots.push_back(CoreUObject::MakeObjectRef(Actor->GetRootComponent()));
                }
            }

            if (Roots.size() == Targets.size()) {
                const auto* Property{Engine::USceneComponent::StaticClass()->FindClassProperty("Transform")};
                Rows.push_back({Property, std::move(Roots), "Transform", "Transform"});
            }
        }

        const bool SkeletalMesh{CommonClass->IsChildOf(Engine::USkeletalMeshComponent::StaticClass())};
        const bool LuaScript{CommonClass == Engine::ULuaScriptComponent::StaticClass()};
        const auto IsGraphMode{[Context](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
            const auto* Component{CoreUObject::Cast<Engine::USkeletalMeshComponent>(Context->ResolveObject(Target))};

            return Component != nullptr && !Component->GetAnimClass().empty();
        }};
        const bool AllGraphMode{SkeletalMesh && std::all_of(Targets.begin(), Targets.end(), IsGraphMode)};
        const bool AllAssetMode{SkeletalMesh && std::none_of(Targets.begin(), Targets.end(), IsGraphMode)};

        for (const CoreUObject::UClass* Class{CommonClass}; Class != nullptr; Class = Class->GetSuperClass()) {
            Classes.push_back(Class);
        }

        for (auto Class{Classes.rbegin()}; Class != Classes.rend(); ++Class) {
            for (const CoreUObject::FProperty* Property : (*Class)->GetProperties()) {
                if (!IsVisibleProperty(Property)) {
                    continue;
                }

                const Core::FString& Name{Property->GetName()};

                if (Targets.size() > 1 && Name == "Name") {
                    continue;
                }

                if (LuaScript && Name == "EditorTickable") {
                    continue;
                }

                if (SkeletalMesh && !AllAssetMode && (Name == "Animation" || Name == "Looping" || Name == "PlayRate")) {
                    continue;
                }

                const std::string_view Category{DetailCategory(*Class, Property)};

                // 언리얼처럼 내부 멤버만 보이는 구조체는 멤버마다 행을 만들고, 멤버에 카테고리가 있으면 그 카테고리에 둡니다.
                if (const CoreUObject::UStruct* Struct{Property->GetStruct()}; Struct != nullptr && Property->HasFlag(Core::EPropertyFlags::ShowOnlyInnerProperties)) {
                    for (const CoreUObject::FProperty* Child : Struct->GetProperties()) {
                        if (!IsVisibleProperty(Child)) {
                            continue;
                        }

                        const std::string_view ChildCategory{Child->GetCategory().empty() ? Category : std::string_view{Child->GetCategory()}};
                        const Core::FString ChildLabel{PropertyLabel(Child)};

                        if (MatchesSearch(ChildCategory, ChildLabel, Child->GetName())) {
                            Rows.push_back({Property, Targets, ChildCategory, ChildLabel, IsAdvancedProperty(Child->GetName()), Child});
                        }
                    }

                    continue;
                }

                const Core::FString Label{Name == "AnimClass" ? "Animation Mode" : LuaScript && Name == "ScriptPath" ? "Script" : PropertyLabel(Property)};

                if (Name == "Transform" ? TransformMatches : MatchesSearch(Category, Label, Name)) {
                    Rows.push_back({Property, Targets, Category, Label, IsAdvancedProperty(Name)});
                }
            }
        }

        if (mOnlyModified) {
            std::erase_if(Rows, [this](const FDetailRow& Row) {
                return !IsModified(Row);
            });
        }

        Core::TArray<Core::FString> Categories{"Script", "Transform", "Animation", "Mesh", "Materials", "Physics", "Collision", "Light", "Camera", "Fog", "Shape", "Movement", "Rendering", "General", "Settings", "Actor Tick", "Component Tick"};

        // 정해 둔 순서에 없는 카테고리는 처음 나온 순서대로 General 앞에 둡니다.
        for (const FDetailRow& Row : Rows) {
            if (std::find(Categories.begin(), Categories.end(), Row.mCategory) == Categories.end()) {
                Categories.insert(std::find(Categories.begin(), Categories.end(), "General"), Core::FString{Row.mCategory});
            }
        }

        const auto* ShadowLight{Targets.size() == 1 ? CoreUObject::Cast<Engine::ULightComponent>(Context->ResolveObject(Targets.front())) : nullptr};

        for (const Core::FString& CategoryName : Categories) {
            FDetailCategory Category{CategoryName};
            Category.mGraphRow = CategoryName == "Animation" && AllGraphMode && (MatchesSearch(CategoryName, "Anim Graph", "AnimGraph") || MatchesSearch(CategoryName, "Debug Anim Graph"));
            auto* Mesh{CategoryName == "Materials" && Targets.size() == 1 ? CoreUObject::Cast<Engine::UMeshComponent>(Context->ResolveObject(Targets.front())) : nullptr};
            const auto* Geometry{Mesh != nullptr ? Mesh->GetStaticMesh() : nullptr};

            if (Geometry != nullptr && std::any_of(Geometry->GetMeshData().GetMaterialSlots().begin(), Geometry->GetMeshData().GetMaterialSlots().end(), [this, Mesh, Geometry](const Asset::FMeshMaterialSlot& Slot) {
                const Index_t Index{static_cast<Index_t>(&Slot - Geometry->GetMeshData().GetMaterialSlots().data())};

                return MatchesSearch("Materials", Slot.GetName(), "Material Slots") && (!mOnlyModified || !Mesh->GetSlotMaterialPath(Index).empty());
            })) {
                Category.mMaterialMesh = Mesh;
            }

            Category.mHasRows = std::any_of(Rows.begin(), Rows.end(), [&CategoryName](const FDetailRow& Row) {
                return Row.mCategory == CategoryName;
            });

            if (Category.mHasRows || Category.mGraphRow || Category.mMaterialMesh != nullptr) {
                Layout.mAvailableCategories.push_back(CategoryName);
                Layout.mCategories.push_back(std::move(Category));
            }
        }

        if (ShadowLight != nullptr && !mOnlyModified && MatchesSearch("Rendering", "Shadow Depth Maps")) {
            Layout.mShadowLight = ShadowLight;

            if (std::find(Layout.mAvailableCategories.begin(), Layout.mAvailableCategories.end(), "Rendering") == Layout.mAvailableCategories.end()) {
                Layout.mAvailableCategories.push_back("Rendering");
            }
        }

        return Layout;
    }

    /**
     * @brief 모아 둔 행을 선택한 섹션에 속한 카테고리별로 표시합니다.
     */
    void Editor::FDetailsWidget::DrawDetailLayout(const FDetailLayout& Layout, const Core::TArray<CoreUObject::TObjectRef<CoreUObject::UObject>>& Targets) {
        const Core::TArray<FDetailRow>& Rows{Layout.mRows};
        const bool Searching{mPropertySearch.front() != '\0'};
        const Core::FString Section{GetEffectiveSection(Layout.mAvailableCategories)};
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {5.0f, 2.0f});

        for (const FDetailCategory& Entry : Layout.mCategories) {
            const char* Category{Entry.mName.c_str()};
            const std::string_view CategoryName{Category};

            if (!IsCategoryInSection(Section, CategoryName) || !BeginCategory(Category, CategoryName != "Component Tick" && CategoryName != "Actor Tick")) {
                continue;
            }

            ImGui::PushID(Category);
            const bool HasAdvanced{!Searching && !mShowAdvanced && std::any_of(Rows.begin(), Rows.end(), [CategoryName](const FDetailRow& Row) {
                return Row.mCategory == CategoryName && Row.mAdvanced;
            })};
            const ImGuiID AdvancedId{ImGui::GetID("##Advanced")};
            const bool ShowAdvanced{!HasAdvanced || ImGui::GetStateStorage()->GetBool(AdvancedId)};
            bool DrawAdvancedToggle{};

            if (BeginPropertyTable()) {
                for (const FDetailRow& Row : Rows) {
                    if (Row.mCategory != CategoryName || (!ShowAdvanced && Row.mAdvanced)) {
                        continue;
                    }

                    if (Row.mProperty->GetName() == "Transform") {
                        DrawTransform(Row);
                    } else if (Row.mProperty->GetName() == "AnimClass") {
                        DrawAnimationMode(Row);
                    } else if (Row.mProperty->GetType() == CoreUObject::EPropertyType::Struct) {
                        DrawStructRow(Row);
                    } else {
                        DrawPropertyRow(Row);
                    }
                }

                if (Entry.mGraphRow) {
                    DrawAnimInstance(Targets);
                }

                if (Entry.mMaterialMesh != nullptr) {
                    DrawMaterialSlots(Entry.mMaterialMesh);
                }

                ImGui::EndTable();
                DrawAdvancedToggle = HasAdvanced;
            }

            if (DrawAdvancedToggle && ImGui::SmallButton(ShowAdvanced ? "Hide Advanced" : "Show Advanced")) {
                ImGui::GetStateStorage()->SetBool(AdvancedId, !ShowAdvanced);
            }

            ImGui::PopID();
        }

        ImGui::PopStyleVar(2);

        if (Layout.mShadowLight != nullptr && IsCategoryInSection(Section, "Rendering") && BeginCategory("Shadow Depth Maps", false)) {
            DrawShadowMaps(Layout.mShadowLight);
        }
    }

    void Editor::FDetailsWidget::DrawMaterialSlots(Engine::UMeshComponent* Component) {
        if (Component == nullptr || Component->GetStaticMesh() == nullptr) {
            return;
        }

        Editor::FEditorContext* Context{GetEditorContext()};
        const CoreUObject::TObjectRef<CoreUObject::UObject> Handle{CoreUObject::MakeObjectRef(Component)};
        const Core::FString Override{Component->GetMaterialPath()};
        const Asset::FMeshData& Data{Component->GetStaticMesh()->GetMeshData()};
        const auto& Slots{Data.GetMaterialSlots()};

        for (Index_t Slot{}; Slot < Slots.size(); ++Slot) {
            const auto& SubMeshes{Data.GetSubMeshes()};

            if (std::none_of(SubMeshes.begin(), SubMeshes.end(), [Slot](const Asset::FSubMesh& SubMesh) {
                return SubMesh.GetMaterialIndex() == Slot;
            })) {
                continue;
            }

            const Core::FString& Label{Slots[Slot].GetName()};
            Core::FString Path{Component->GetSlotMaterialPath(Slot)};

            if ((!MatchesSearch("Materials", Label, "Material Slots") || (mOnlyModified && Path.empty()))) {
                continue;
            }

            ImGui::PushID(static_cast<int>(Slot));
            BeginPropertyRow(Label.c_str());
            ImGui::TableSetColumnIndex(1);
            const bool Editable{Context->CanEditWorld() && Override.empty()};
            ImGui::BeginDisabled(!Editable);
            const Core::FString& Inherited{Override.empty() ? Slots[Slot].GetMaterialPath() : Override};
            const bool Changed{DrawAssetPicker("##Slot", Path, Asset::EAssetType::Material, "Use mesh material", Inherited)};

            ImGui::TableSetColumnIndex(2);

            if ((Editable && !Path.empty() && DrawResetButton()) || Changed) {
                Context->SetMaterialOverride(Handle, Slot, Changed ? Path : Core::FString{});
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    void Editor::FDetailsWidget::DrawShadowMaps(const Engine::ULightComponent* Light) {
        const auto* Renderer{GetEditorContext()->GetRenderer()};
        const RenderCore::FShadowAtlasView Atlas{Renderer != nullptr ? Renderer->GetShadowAtlasView() : RenderCore::FShadowAtlasView{}};
        bool Found{};

        for (const RenderCore::FShadowAtlasTile& Tile : Atlas.mTiles) {
            if (Tile.mLightId != Light->GetGuid() || Atlas.mTexture == nullptr) {
                continue;
            }

            Found = true;
            ImGui::Text("Map %u | %u x %u", Tile.mFace, Tile.mResolution, Tile.mResolution);
            const float Size{std::clamp(ImGui::GetContentRegionAvail().x, 64.0f, 320.0f)};
            ImGui::Image(reinterpret_cast<ImTextureID>(Atlas.mTexture), {Size, Size}, {Tile.mRectangle.x, Tile.mRectangle.y}, {Tile.mRectangle.x + Tile.mRectangle.z, Tile.mRectangle.y + Tile.mRectangle.w});
        }

        if (!Found) {
            ImGui::TextDisabled("No active shadow map in the last rendered view.");
        }
    }

    void Editor::FDetailsWidget::DrawAnimationMode(const FDetailRow& Row) {
        Editor::FEditorContext* Context{GetEditorContext()};
        const auto IsGraph{[Context, &Row](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
            return !std::get<Core::FString>(Row.mProperty->ReadValue(Context->ResolveObject(Target))).empty();
        }};
        const bool Graph{IsGraph(Row.mTargets.front())};
        const bool Mixed{std::any_of(Row.mTargets.begin(), Row.mTargets.end(), [&IsGraph, Graph](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
            return IsGraph(Target) != Graph;
        })};
        const bool Editable{std::all_of(Row.mTargets.begin(), Row.mTargets.end(), [Context, &Row](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
            return Context->CanEditProperty(Target, Row.mProperty);
        })};

        if (mOnlyModified && !Graph && !Mixed) {
            return;
        }

        ImGui::PushID(Row.mProperty);
        BeginPropertyRow(Row.mLabel.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::BeginDisabled(!Editable);

        if (ImGui::BeginCombo("##AnimationMode", Mixed ? "Multiple Values" : Graph ? "Use Anim Graph" : "Use Animation Asset")) {
            if (ImGui::Selectable("Use Anim Graph", Graph && !Mixed)) {
                EditProperty(Row, Engine::UAnimGraphInstance::StaticClass()->GetName(), false);
            }

            if (ImGui::Selectable("Use Animation Asset", !Graph && !Mixed)) {
                EditProperty(Row, Core::FString{}, false);
            }

            ImGui::EndCombo();
        }

        ImGui::EndDisabled();
        ImGui::PopID();
    }

    std::optional<CoreUObject::FPropertyValue> Editor::FDetailsWidget::GetDefaultValue(const CoreUObject::UClass* Class, const CoreUObject::FProperty* Property) {
        if (!Class->CanCreate() || Property->GetName() == "Name") {
            return {};
        }

        auto Position{mDefaultValues.find(Class)};

        if (Position == mDefaultValues.end()) {
            auto& Values{mDefaultValues[Class]};
            const auto Defaults{Class->CreateInstance()};

            if (Defaults != nullptr) {
                for (const CoreUObject::UClass* Current{Class}; Current != nullptr; Current = Current->GetSuperClass()) {
                    for (const CoreUObject::FProperty* Field : Current->GetProperties()) {
                        if (IsVisibleProperty(Field)) {
                            auto Value{Field->ReadValue(Defaults.get())};

                            if (!std::holds_alternative<const void*>(Value)) {
                                Values.emplace(Field, std::move(Value));
                            }
                        }
                    }
                }
            }

            Position = mDefaultValues.find(Class);
        }

        const auto Found{Position->second.find(Property)};

        return Found != Position->second.end() ? std::optional<CoreUObject::FPropertyValue>{Found->second} : std::nullopt;
    }

    bool Editor::FDetailsWidget::IsModified(const FDetailRow& Row) {
        Editor::FEditorContext* Context{GetEditorContext()};

        return std::any_of(Row.mTargets.begin(), Row.mTargets.end(), [this, Context, &Row](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
            const CoreUObject::UObject* Object{Context->ResolveObject(Target)};

            if (Object == nullptr) {
                return false;
            }

            const auto Default{GetDefaultValue(Object->GetClass(), Row.mProperty)};

            return Default.has_value() && !EqualValues(Row.mProperty->ReadValue(Object), *Default);
        });
    }

    void Editor::FDetailsWidget::EditProperty(const FDetailRow& Row, const CoreUObject::FPropertyValue& Value, bool Interactive) {
        Editor::FEditorContext* Context{GetEditorContext()};

        if (Interactive) {
            BeginItemSceneEdit();
        }

        const Uint64 Token{Context->BeginSceneEdit()};

        for (CoreUObject::TObjectRef<CoreUObject::UObject> Target : Row.mTargets) {
            CoreUObject::UObject* Object{Context->ResolveObject(Target)};

            if (Object != nullptr && !EqualValues(Row.mProperty->ReadValue(Object), Value)) {
                Context->EditProperty(Target, Row.mProperty, Value, Interactive);
            }
        }

        Context->EndSceneEdit(Token);
    }

    void Editor::FDetailsWidget::DrawPropertyRow(const FDetailRow& Row) {
        Editor::FEditorContext* Context{GetEditorContext()};
        const CoreUObject::TObjectRef<CoreUObject::UObject> First{Row.mTargets.front()};
        const CoreUObject::UObject* Object{Context->ResolveObject(First)};

        if (Object == nullptr) {
            return;
        }

        const bool LuaScript{Row.mProperty->GetName() == "ScriptPath" && Row.mProperty == Engine::ULuaScriptComponent::StaticClass()->FindClassProperty("ScriptPath")};
        CoreUObject::FPropertyValue Value{Row.mProperty->ReadValue(Object)};
        Core::TArray<std::pair<CoreUObject::TObjectRef<CoreUObject::UObject>, CoreUObject::FPropertyValue>> Defaults{};
        bool Mixed{};
        bool Editable{true};

        for (CoreUObject::TObjectRef<CoreUObject::UObject> Target : Row.mTargets) {
            CoreUObject::UObject* Current{Context->ResolveObject(Target)};

            if (Current == nullptr) {
                return;
            }

            const CoreUObject::FPropertyValue CurrentValue{Target == First ? Value : Row.mProperty->ReadValue(Current)};
            Mixed = Mixed || !EqualValues(Value, CurrentValue);
            Editable = Editable && Context->CanEditProperty(Target, Row.mProperty);

            if (auto Default{GetDefaultValue(Current->GetClass(), Row.mProperty)}; Default.has_value() && !EqualValues(CurrentValue, *Default)) {
                Defaults.emplace_back(Target, std::move(*Default));
            }
        }

        if (mOnlyModified && Defaults.empty()) {
            return;
        }

        ImGui::PushID(Row.mProperty);
        ImGui::PushID(Object);
        BeginPropertyRow(Row.mLabel.c_str());
        bool Reset{};
        bool Pasted{};

        if (ImGui::BeginPopupContextItem("##PropertyMenu")) {
            Serialization::FJsonArchive Copy{Core::EArchiveMode::Saving};
            const bool CanCopy{!Mixed && ArchiveDetailValue(Copy, Value)};

            if (ImGui::MenuItem("Copy", nullptr, false, CanCopy)) {
                ImGui::SetClipboardText(Copy.ToString().c_str());
            }

            if (!LuaScript) {
                const char* Clipboard{ImGui::GetClipboardText()};
                auto PastedValue{Value};
                Serialization::FJsonArchive Paste{Core::EArchiveMode::Loading};
                const bool CanPaste{Editable && Clipboard != nullptr && Paste.Parse(Clipboard) && ArchiveDetailValue(Paste, PastedValue)};

                if (ImGui::MenuItem("Paste", nullptr, false, CanPaste)) {
                    Value = std::move(PastedValue);
                    Pasted = true;
                }
            }

            Reset = ImGui::MenuItem("Reset to Default", nullptr, false, Editable && !Defaults.empty());
            ImGui::EndPopup();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(!Editable);

        if (Mixed) {
            ImGui::TextDisabled("Multiple Values");
        }

        ImGui::SetNextItemWidth(-1.0f);

        const bool Changed{LuaScript ? DrawLuaScriptSlot(*Context, std::get<Core::FString>(Value)) : DrawPropertyValue(Row.mProperty, Value, "##Value")};

        if (Changed || Pasted) {
            EditProperty(Row, Value, !LuaScript && !Pasted && ImGui::IsItemActive());
        }

        ImGui::TableSetColumnIndex(2);

        if (Editable && !Defaults.empty() && (DrawResetButton() || Reset)) {
            const Uint64 Token{Context->BeginSceneEdit()};

            for (const auto& [Target, Default] : Defaults) {
                Context->EditProperty(Target, Row.mProperty, Default);
            }

            Context->EndSceneEdit(Token);
        }

        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::PopID();
    }

    void Editor::FDetailsWidget::DrawStructRow(const FDetailRow& Row) {
        Editor::FEditorContext* Context{GetEditorContext()};
        const CoreUObject::UObject* Object{Context->ResolveObject(Row.mTargets.front())};
        const auto* Struct{static_cast<const CoreUObject::UScriptStruct*>(Row.mProperty->GetStruct())};

        if (Object == nullptr || Struct == nullptr) {
            return;
        }

        const auto ReadStruct{[&Row](const CoreUObject::UObject* Target) {
            return std::get<const void*>(Row.mProperty->ReadValue(Target));
        }};
        const bool Editable{std::all_of(Row.mTargets.begin(), Row.mTargets.end(), [Context, &Row](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
            return Context->CanEditProperty(Target, Row.mProperty);
        })};

        // 내부 프로퍼티만 표시하는 구조체는 접힘 노드 없이 멤버를 소유 카테고리의 행으로 바로 그립니다.
        const bool InnerOnly{Row.mProperty->HasFlag(Core::EPropertyFlags::ShowOnlyInnerProperties)};

        ImGui::PushID(Row.mProperty);
        ImGui::PushID(Object);

        if (!InnerOnly) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
        }

        if (InnerOnly || ImGui::TreeNodeEx(Row.mLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns)) {
            for (const CoreUObject::FProperty* Child : Struct->GetProperties()) {
                if (!IsVisibleProperty(Child) || (Row.mInnerProperty != nullptr && Child != Row.mInnerProperty)) {
                    continue;
                }

                ImGui::PushID(Child);
                BeginPropertyRow(PropertyLabel(Child, Struct).c_str());
                ImGui::TableSetColumnIndex(1);

                if (Child->GetType() == CoreUObject::EPropertyType::Struct || Child->GetArrayOps() != nullptr) {
                    ImGui::TextDisabled("Not editable here");
                    ImGui::PopID();

                    continue;
                }

                CoreUObject::FPropertyValue Value{Child->ReadValue(ReadStruct(Object))};
                bool Mixed{};

                for (CoreUObject::TObjectRef<CoreUObject::UObject> Target : Row.mTargets) {
                    if (const CoreUObject::UObject* Current{Context->ResolveObject(Target)}) {
                        Mixed = Mixed || !EqualValues(Value, Child->ReadValue(ReadStruct(Current)));
                    }
                }

                const bool ChildEditable{Editable && !Child->HasFlag(Core::EPropertyFlags::ReadOnly)};
                ImGui::BeginDisabled(!ChildEditable);

                if (Mixed) {
                    ImGui::TextDisabled("Multiple Values");
                }

                ImGui::SetNextItemWidth(-1.0f);

                if (DrawPropertyValue(Child, Value, "##Value") && ChildEditable) {
                    const bool Interactive{ImGui::IsItemActive()};

                    if (Interactive) {
                        BeginItemSceneEdit();
                    }

                    const Uint64 Token{Context->BeginSceneEdit()};

                    for (CoreUObject::TObjectRef<CoreUObject::UObject> Target : Row.mTargets) {
                        const CoreUObject::UObject* Current{Context->ResolveObject(Target)};

                        if (Current == nullptr || EqualValues(Child->ReadValue(ReadStruct(Current)), Value)) {
                            continue;
                        }

                        // 다른 필드는 대상마다 유지하도록 대상의 현재 구조체 사본에 바뀐 필드만 쓴 뒤 전체를 적용합니다.
                        const CoreUObject::UScriptStruct::FValuePtr Working{Struct->CloneValue(ReadStruct(Current))};

                        if (Working != nullptr && Child->WriteValue(Working.get(), Value)) {
                            Context->EditProperty(Target, Row.mProperty, CoreUObject::FPropertyValue{static_cast<const void*>(Working.get())}, Interactive);
                        }
                    }

                    Context->EndSceneEdit(Token);
                }

                ImGui::EndDisabled();
                ImGui::PopID();
            }

            if (!InnerOnly) {
                ImGui::TreePop();
            }
        }

        ImGui::PopID();
        ImGui::PopID();
    }

    void Editor::FDetailsWidget::DrawTransform(const FDetailRow& Row) {
        struct FTransformEdit {
            CoreUObject::TObjectRef<CoreUObject::UObject> mTarget{};
            FTransform mTransform{};
            std::size_t mDepth{};
        };

        Editor::FEditorContext* Context{GetEditorContext()};
        const CoreUObject::UObject* Object{Context->ResolveObject(Row.mTargets.front())};

        if (Object == nullptr) {
            return;
        }

        const bool Editable{std::all_of(Row.mTargets.begin(), Row.mTargets.end(), [Context, &Row](CoreUObject::TObjectRef<CoreUObject::UObject> Target) {
            return Context->CanEditProperty(Target, Row.mProperty);
        })};
        constexpr Core::TStaticArray<const char*, 3> Labels{"Location", "Rotation", "Scale"};
        constexpr Core::TStaticArray<const char*, 3> AxisIds{"X", "Y", "Z"};
        constexpr Core::FQuantityUnit RotationUnit{Core::QuantityUnits::Degree.mAlias, Core::Dim::Angle, Core::QuantityUnits::Degree.mScale};
        const auto& AxisColors{Editor::FGizmoCore::GetAxisColors()};
        const auto ReadPart{[](const FTransform& Transform, Index_t Part) {
            if (Part == 1) {
                const FRotator Rotation{Transform.GetRotator()};

                return Core::TStaticArray<float, 3>{Rotation.roll, Rotation.pitch, Rotation.yaw};
            }

            return (Part == 0 ? Transform.mLocation : Transform.mScale).ToArray();
        }};
        const auto IsDefaultPart{[](const FTransform& Transform, const FTransform& Default, Index_t Part) {
            if (Part == 1) {
                return Transform.mRotation.Equals(Default.mRotation, 0.0f);
            }

            return Part == 0 ? Transform.mLocation == Default.mLocation : Transform.mScale == Default.mScale;
        }};

        ImGui::PushID(Object);
        ImGui::BeginDisabled(!Editable);

        for (Index_t Part{}; Part < Labels.size(); ++Part) {
            if (!MatchesSearch("Transform", Labels[Part])) {
                continue;
            }

            const bool IsRotation{Part == 1};
            const auto First{ReadPart(std::get<FTransform>(Row.mProperty->ReadValue(Object)), Part)};
            Core::TStaticArray<bool, 3> Mixed{};
            bool Modified{};

            for (CoreUObject::TObjectRef<CoreUObject::UObject> Target : Row.mTargets) {
                CoreUObject::UObject* Current{Context->ResolveObject(Target)};

                if (Current == nullptr) {
                    continue;
                }

                const FTransform Transform{std::get<FTransform>(Row.mProperty->ReadValue(Current))};
                const auto Values{ReadPart(Transform, Part)};

                for (Index_t Axis{}; Axis < AxisIds.size(); ++Axis) {
                    Mixed[Axis] = Mixed[Axis] || Values[Axis] != First[Axis];
                }

                if (const auto Default{GetDefaultValue(Current->GetClass(), Row.mProperty)}; Default.has_value()) {
                    Modified = Modified || !IsDefaultPart(Transform, std::get<FTransform>(*Default), Part);
                }
            }

            if (mOnlyModified && !Modified) {
                continue;
            }

            ImGui::PushID(Labels[Part]);
            BeginPropertyRow(Labels[Part]);
            ImGui::TableSetColumnIndex(1);
            auto Values{First};
            Core::TStaticArray<bool, 3> Changed{};
            bool Interactive{};
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2.0f, ImGui::GetStyle().ItemSpacing.y});
            const float Width{std::max(1.0f, (ImGui::GetContentRegionAvail().x - 4.0f) / 3.0f)};

            for (Index_t Axis{}; Axis < AxisIds.size(); ++Axis) {
                if (Axis > 0) {
                    ImGui::SameLine();
                }

                ImGui::PushID(AxisIds[Axis]);
                ImGui::SetNextItemWidth(Width);

                const FVector& Color{AxisColors[Axis]};
                const float BaseValue{IsRotation ? (Values[Axis] * 1_deg).GetBase() : Values[Axis]};
                Editor::FEditorQuantityInputWidget Input{"##Axis", BaseValue, IsRotation ? RotationUnit : Core::QuantityUnits::Scalar};
                Input.SetMixedValue(Mixed[Axis]);
                Input.SetAxisColor(ImGui::ColorConvertFloat4ToU32({Color.x, Color.y, Color.z, 1.0f}));
                Input.SetInlineUnit(IsRotation);
                Input.Draw(0.0f);
                ImGui::SetItemTooltip("%s", AxisIds[Axis]);

                if (const auto Edited{Input.GetEditedValue()}; Edited.has_value()) {
                    const float Value{IsRotation ? Core::Math::ToDegrees(Core::FAngle::FromBase(*Edited)) : *Edited};
                    Changed[Axis] = Mixed[Axis] || Value != Values[Axis];
                    Values[Axis] = Value;
                }

                Interactive = Interactive || (Changed[Axis] && ImGui::IsItemActive());
                ImGui::PopID();
            }

            ImGui::PopStyleVar();
            ImGui::TableSetColumnIndex(2);
            const bool Reset{Editable && Modified && DrawResetButton()};

            if (Reset || std::any_of(Changed.begin(), Changed.end(), [](bool AxisChanged) {
                return AxisChanged;
            })) {
                if (Interactive) {
                    BeginItemSceneEdit();
                }

                const Uint64 Token{Context->BeginSceneEdit()};
                Core::TArray<FTransformEdit> Edits{};
                Edits.reserve(Row.mTargets.size());

                for (CoreUObject::TObjectRef<CoreUObject::UObject> Target : Row.mTargets) {
                    CoreUObject::UObject* Current{Context->ResolveObject(Target)};

                    if (Current == nullptr) {
                        continue;
                    }

                    FTransform Transform{std::get<FTransform>(Row.mProperty->ReadValue(Current))};
                    if (Reset) {
                        const auto Default{GetDefaultValue(Current->GetClass(), Row.mProperty)};

                        if (!Default.has_value() || IsDefaultPart(Transform, std::get<FTransform>(*Default), Part)) {
                            continue;
                        }

                        const FTransform& DefaultTransform{std::get<FTransform>(*Default)};

                        if (Part == 0) {
                            Transform.mLocation = DefaultTransform.mLocation;
                        } else if (IsRotation) {
                            Transform.mRotation = DefaultTransform.mRotation;
                        } else {
                            Transform.mScale = DefaultTransform.mScale;
                        }
                    } else {
                        auto NewPart{ReadPart(Transform, Part)};

                        for (Index_t Axis{}; Axis < AxisIds.size(); ++Axis) {
                            if (Changed[Axis]) {
                                NewPart[Axis] = Values[Axis];
                            }
                        }

                        if (Part == 0) {
                            Transform.mLocation = {NewPart[0], NewPart[1], NewPart[2]};
                        } else if (IsRotation) {
                            Transform.SetRotator({NewPart[1] * 1_deg, NewPart[2] * 1_deg, NewPart[0] * 1_deg});
                        } else {
                            Transform.mScale = {NewPart[0], NewPart[1], NewPart[2]};
                        }
                    }

                    const auto* Scene{CoreUObject::Cast<Engine::USceneComponent>(Current)};
                    const Engine::USceneComponent* Parent{Scene != nullptr ? Scene->GetAttachParent() : nullptr};
                    std::size_t Depth{};

                    while (Parent != nullptr) {
                        const Engine::USceneComponent* Root{Parent->GetOwner()->GetRootComponent()};
                        Parent = Root != nullptr ? Root->GetAttachParent() : nullptr;
                        ++Depth;
                    }

                    Edits.push_back({Target, Transform, Depth});
                }

                std::ranges::stable_sort(Edits, {}, &FTransformEdit::mDepth);

                for (const FTransformEdit& Edit : Edits) {
                    Context->EditProperty(Edit.mTarget, Row.mProperty, Edit.mTransform, Interactive);
                }

                Context->EndSceneEdit(Token);
            }

            ImGui::PopID();
        }

        ImGui::EndDisabled();
        ImGui::PopID();
    }

    /**
     * @brief 프로퍼티 타입에 맞는 편집 위젯을 표시하고 변경한 값을 돌려줍니다.
     */
    bool Editor::FDetailsWidget::DrawPropertyValue(const CoreUObject::FProperty* Property, CoreUObject::FPropertyValue& Value, const char* Label) {

        if (Property->GetType() == CoreUObject::EPropertyType::Enum) {
            const bool ShadowBias{Property->GetNativeType() == CoreUObject::GetNativeType<Engine::EShadowBiasPreset>()};
            const bool SlopeBias{ShadowBias && Property->GetName() == "ShadowSlopeBiasPreset"};
            Int32 Number{std::get<Int32>(Value)};
            const auto& Choices{Property->GetEnumValues()};
            const auto Current{std::find_if(Choices.begin(), Choices.end(), [Number](const CoreUObject::FEnumValue& Choice) {
                return Choice.mValue == Number;
            })};
            const auto FormatChoice{[ShadowBias, SlopeBias](const CoreUObject::FEnumValue& Choice) {
                if (!ShadowBias) {
                    return Choice.mName;
                }

                return SlopeBias ? std::format("Level {} ({:.2f})", Choice.mValue, static_cast<float>(Choice.mValue) * Engine::ShadowSlopeBiasStep) : std::format("Level {} ({})", Choice.mValue, Choice.mValue * Engine::ShadowDepthBiasStep);
            }};
            const Core::FString Preview{Current != Choices.end() ? FormatChoice(*Current) : "Invalid"};

            bool Changed{};

            if (ImGui::BeginCombo(Label, Preview.c_str())) {
                for (const CoreUObject::FEnumValue& Choice : Choices) {
                    const Core::FString Option{FormatChoice(Choice)};

                    if (ImGui::Selectable(Option.c_str(), Number == Choice.mValue)) {
                        Number = Choice.mValue;
                        Changed = true;
                    }
                }

                ImGui::EndCombo();
            }

            Value = Number;

            return Changed;
        }

        if (auto* Vector2{std::get_if<FVector2>(&Value)}) {
            auto Values{Vector2->ToArray()};

            if (ImGui::DragFloat2(Label, Values.data(), 0.025f)) {
                *Vector2 = {Values[0], Values[1]};

                return true;
            }
        } else if (auto* Vector4{std::get_if<FVector4>(&Value)}) {
            auto Values{Vector4->ToArray()};


            const bool Changed{Property->GetName().ends_with("Color") ? ImGui::ColorEdit4(Label, Values.data()) : ImGui::DragFloat4(Label, Values.data(), 0.025f)};

            if (Changed) {
                *Vector4 = {Values[0], Values[1], Values[2], Values[3]};

                return true;
            }
        } else if (auto* Matrix{std::get_if<FMatrix>(&Value)}) {
            bool Changed{};

            if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (Index_t Row{}; Row < 4; ++Row) {
                    Core::TStaticArray<float, 4> Values{(*Matrix)(Row, 0), (*Matrix)(Row, 1), (*Matrix)(Row, 2), (*Matrix)(Row, 3)};

                    if (ImGui::DragFloat4(DrawFieldLabel(std::to_string(Row).c_str()).c_str(), Values.data(), 0.025f)) {
                        for (Index_t Column{}; Column < 4; ++Column) {
                            (*Matrix)(Row, Column) = Values[Column];
                        }

                        Changed = true;
                    }
                }

                ImGui::TreePop();
            }

            return Changed;
        } else if (auto* Vector{std::get_if<FVector>(&Value)}) {
            auto Values{Vector->ToArray()};

            if (ImGui::DragFloat3(Label, Values.data(), 0.025f, 0.0f, 0.0f, "%.3f")) {
                *Vector = {Values[0], Values[1], Values[2]};

                return true;
            }
        } else if (auto* Rotation{std::get_if<FRotator>(&Value)}) {
            auto Values{Rotation->ToArray()};

            if (ImGui::DragFloat3(Label, Values.data(), 0.25f, 0.0f, 0.0f, "%.2f")) {
                *Rotation = {Values[0] * 1_deg, Values[1] * 1_deg, Values[2] * 1_deg};

                return true;
            }
        } else if (auto* String{std::get_if<Core::FString>(&Value)}) {
            if (const Core::FString& AssetTypeName{Property->GetMetaData("AssetType")}; !AssetTypeName.empty()) {
                // AssetType 메타데이터는 EAssetType 값 이름이므로 리플렉션 열거형에서 값을 찾습니다.
                const auto& AssetTypes{Asset::StaticEnumEAssetType()->GetValues()};
                const auto Found{std::find_if(AssetTypes.begin(), AssetTypes.end(), [&AssetTypeName](const CoreUObject::FEnumValue& Entry) {
                    return Entry.mName == AssetTypeName;
                })};

                if (Found != AssetTypes.end()) {
                    const Core::FString& EmptyLabel{Property->GetMetaData("EmptyLabel")};

                    return DrawAssetPicker(Label, *String, static_cast<Asset::EAssetType>(Found->mValue), EmptyLabel.empty() ? "None" : EmptyLabel.c_str());
                }
            }

            if (Property->HasMetaData("MultiLine")) {
                Core::TArray<char> TextBuffer{};
                TextBuffer.resize(std::max<std::size_t>(4096, String->size() + 1024));
                std::copy(String->begin(), String->end(), TextBuffer.begin());

                if (ImGui::InputTextMultiline(Label, TextBuffer.data(), TextBuffer.size(), {0.0f, ImGui::GetTextLineHeight() * 4.0f}, ImGuiInputTextFlags_AllowTabInput)) {
                    *String = TextBuffer.data();

                    return true;
                }

                return false;
            }

            Core::TStaticArray<char, 512> Buffer{};

            std::copy_n(String->begin(), std::min(String->size(), Buffer.size() - 1), Buffer.begin());

            if (ImGui::InputText(Label, Buffer.data(), Buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                *String = Buffer.data();

                return true;
            }
        } else if (auto* Boolean{std::get_if<bool>(&Value)}) {
            return ImGui::Checkbox(Label, Boolean);
        } else if (auto* Quantity{std::get_if<CoreUObject::FQuantityPropertyValue>(&Value)}) {
            const auto Unit{Property->GetQuantityDisplayUnit(Quantity->mDimension)};

            if (!Unit.has_value()) {
                ImGui::TextUnformatted("Invalid quantity unit");

                return false;
            }

            return FEditorQuantityInputWidget::EditValue(Label, Quantity->mBaseValue, *Unit);
        } else if (auto* FloatValue{std::get_if<float>(&Value)}) {
            if (Property->GetMetaData("DisplayUnit") == Core::QuantityUnits::Meter.mSymbol) {
                return FEditorQuantityInputWidget::EditValue(Label, *FloatValue, Core::QuantityUnits::Meter);
            }

            return ImGui::DragFloat(Label, FloatValue, 0.025f);
        } else if (auto* IntegerValue{std::get_if<Int32>(&Value)}) {
            return ImGui::InputInt(Label, IntegerValue);
        } else if (auto* UnsignedValue{std::get_if<Uint32>(&Value)}) {
            return ImGui::InputScalar(Label, ImGuiDataType_U32, UnsignedValue);
        } else if (auto* Unsigned64Value{std::get_if<Uint64>(&Value)}) {
            return ImGui::InputScalar(Label, ImGuiDataType_U64, Unsigned64Value);
        } else if (auto* Guid{std::get_if<Core::FGuid>(&Value)}) {

            ImGui::TextWrapped("%s", Guid->ToString().c_str());
        } else if (auto* Reference{std::get_if<CoreUObject::FObjectReference>(&Value)}) {

            ImGui::TextWrapped("%s", Reference->GetGuid().ToString().c_str());
        } else if (const CoreUObject::UStruct* Struct{Property->GetStruct()}; Struct != nullptr && !std::holds_alternative<const void*>(Value)) {
            // 주소로 전달되는 구조체 멤버는 값이 variant 안에 없으므로 DrawStructRow에서 따로 편집합니다.
            bool Changed{};

            if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const CoreUObject::FProperty* Child : Struct->GetProperties()) {
                    if (Child->HasFlag(Core::EPropertyFlags::Transient)) {
                        continue;
                    }

                    CoreUObject::FPropertyValue ChildValue{Child->ReadValue(CoreUObject::GetPropertyData(Value))};
                    const bool ReadOnly{Child->HasFlag(Core::EPropertyFlags::ReadOnly)};

                    ImGui::BeginDisabled(ReadOnly);

                    const Core::FString ChildLabel{DrawFieldLabel(PropertyLabel(Child).c_str())};

                    if (DrawPropertyValue(Child, ChildValue, ChildLabel.c_str()) && !ReadOnly) {
                        Changed = Child->WriteValue(CoreUObject::GetPropertyData(Value), ChildValue) || Changed;
                    }

                    ImGui::EndDisabled();
                }

                ImGui::TreePop();
            }

            return Changed;
        }

        return false;
    }

    /**
     * @brief 메시 또는 재질 선택과 드래그 앤 드롭을 표시합니다.
     */
    bool Editor::FDetailsWidget::DrawAssetPicker(const char* Label, Core::FString& Path, Asset::EAssetType Type, const char* EmptyLabel, const Core::FString& InheritedPath) {
        if (mAssetCatalog == nullptr || mThumbnails == nullptr) {
            ImGui::TextDisabled("%s", Path.empty() ? EmptyLabel : Path.c_str());

            return false;
        }

        ImGui::PushID(Label);
        bool Changed{mAssetPicker.Draw(*mAssetCatalog, *mThumbnails, "##AssetReference", Path, Type, InheritedPath, EmptyLabel, "Mesh", false)};

        if (ImGui::BeginPopupContextItem("##AssetActions")) {
            const std::filesystem::path Selection{mGetSelectedAsset ? mGetSelectedAsset() : std::filesystem::path{}};
            const auto* Entry{!Selection.empty() ? mAssetCatalog->Find(Selection) : nullptr};
            const Core::FString& Effective{Path.empty() ? InheritedPath : Path};

            if (ImGui::MenuItem("Use Selected Asset from Content Browser", nullptr, false, Entry != nullptr && Entry->GetType() == Type)) {
                Path = Core::FilePath::ToGenericUtf8(Entry->GetPath());
                Changed = true;
            }

            if (ImGui::MenuItem("Browse to Asset in Content Browser", nullptr, false, mBrowseAsset && !Effective.empty())) {
                mBrowseAsset(Core::FilePath::FromUtf8(Effective));
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();

        return Changed;
    }

}
