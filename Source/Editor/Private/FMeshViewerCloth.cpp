/**
 * @file FMeshViewerCloth.cpp
 * @brief 메시 뷰어의 천 칸(섹션 표, 설정, 칠하기, 충돌, 저장)과 미리 보기 천 시뮬레이션을 구현합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "Editor/FMeshViewer.h"
#include "Viewer/FMeshViewerContent.h"
#include "Core/FilePath.h"
#include "Core/Math/FRay.h"
#include "Core/Math/FVector4.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/FSceneView.h"
#include "Asset/FAssetManager.h"
#include "Asset/FAssetFile.h"
#include "Asset/FClothingSectionData.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"

#include "ClothEditing.h"
#include "PhysicsRuntime/FClothingSystemRuntimeModuleNv.h"
#include "PhysicsRuntime/IClothingSimulation.h"
#include "PhysicsRuntime/UClothConfigNv.h"
#include "PhysicsRuntime/UClothingAssetCommon.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace Editor {

    namespace {
        /**
         * @brief 결과 문장을 보여 주는 시간(초)입니다.
         */
        constexpr double ClothMessageSeconds{6.0};

        /**
         * @brief 기본값과 다른 설정 칸의 이름 색입니다.
         */
        constexpr ImU32 ChangedLabelColor{IM_COL32(255, 200, 90, 255)};

        /**
         * @brief 설정 칸이 기본값과 다른지 검사합니다.
         * @param[in] Field 설정 항목입니다.
         * @param[in] Config 값을 가진 설정입니다.
         * @return 기본값과 다르면 true입니다.
         */
        bool IsChangedFromDefault(const ClothEditing::FClothConfigField& Field, PhysicsRuntime::UClothConfigNv& Config) {
            constexpr float Tolerance{1.e-6f};

            if (Field.HasScalarValue()) {
                return std::abs(Field.GetScalarValue(Config) - Field.mDefault.x) > Tolerance;
            }

            const FVector& Value{*Field.mGetVector(Config)};

            return std::abs(Value.x - Field.mDefault.x) > Tolerance || std::abs(Value.y - Field.mDefault.y) > Tolerance || std::abs(Value.z - Field.mDefault.z) > Tolerance;
        }

        /**
         * @brief 설정 칸을 기본값으로 되돌립니다.
         * @param[in] Field 설정 항목입니다.
         * @param[in,out] Config 값을 바꿀 설정입니다.
         */
        void ResetToDefault(const ClothEditing::FClothConfigField& Field, PhysicsRuntime::UClothConfigNv& Config) {
            if (Field.HasScalarValue()) {
                Field.SetScalarValue(Config, Field.mDefault.x);
            } else {
                *Field.mGetVector(Config) = Field.mDefault;
            }
        }

        /**
         * @brief 설정 항목의 설명에 허용 범위를 붙여 툴팁으로 보여 줍니다.
         * @param[in] Field 설정 항목입니다.
         */
        void ShowFieldTooltip(const ClothEditing::FClothConfigField& Field) {
            if (!ImGui::BeginItemTooltip()) {
                return;
            }

            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(Field.mLabel);
            ImGui::Separator();
            ImGui::TextUnformatted(Field.mTooltip);

            // 기본값과 범위는 표의 값에서 자동으로 만들어 설명 문장을 짧게 둡니다.
            const float Scale{Field.mDisplayScale};

            if (Field.HasScalarValue()) {
                ImGui::TextDisabled("기본 %g  |  범위 %g ~ %g", Field.mDefault.x * Scale, Field.mMin * Scale, Field.mMax * Scale);
            } else {
                ImGui::TextDisabled("기본 (%g, %g, %g)  |  범위 %g ~ %g", Field.mDefault.x * Scale, Field.mDefault.y * Scale, Field.mDefault.z * Scale, Field.mMin * Scale, Field.mMax * Scale);
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        /**
         * @brief 이름과 설명을 툴팁으로 보여 줍니다.
         * @param[in] Label 이름과 설명입니다.
         */
        void ShowLabelTooltip(const ClothEditing::FClothLabel& Label) {
            if (!ImGui::BeginItemTooltip()) {
                return;
            }

            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(Label.mLabel);
            ImGui::Separator();
            ImGui::TextUnformatted(Label.mTooltip);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        /**
         * @brief 긴 설명 문장을 툴팁으로 보여 줍니다.
         * @param[in] Text 설명입니다.
         */
        void ShowTextTooltip(const char* Text) {
            if (!ImGui::BeginItemTooltip()) {
                return;
            }

            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(Text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        /**
         * @brief 설정 칸 하나의 값 위젯을 그립니다.
         * @details 0~1처럼 좁은 범위는 슬라이더, 넓은 범위는 끌기 칸을 쓰고, 기본값과 다르면 칸을 물들입니다.
         * @param[in] Field 설정 항목입니다.
         * @param[in,out] Config 값을 가진 설정입니다.
         * @return 값 편집을 마쳐 미리 보기에 반영해야 하면 true입니다.
         */
        bool DrawFieldWidget(const ClothEditing::FClothConfigField& Field, PhysicsRuntime::UClothConfigNv& Config) {
            const bool Changed{IsChangedFromDefault(Field, Config)};
            const float Scale{Field.mDisplayScale};
            const float Min{Field.mMin * Scale};
            const float Max{Field.mMax * Scale};
            // 0~1처럼 좁은 범위는 슬라이더로 위치를 보여 주고, 아주 작은 값을 다루는 칸은 끌기 칸으로 둡니다.
            const bool UseSlider{Field.mMin >= 0.0f && (Field.mMax - Field.mMin) * Scale <= 1.0f && Field.mSpeed >= 0.001f};
            const ImGuiSliderFlags Flags{ImGuiSliderFlags_AlwaysClamp};
            bool Committed{};

            if (Changed) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(90, 70, 30, 255));
            }

            ImGui::PushID(Field.mKey);
            ImGui::SetNextItemWidth(-FLT_MIN);

            if (Field.HasScalarValue()) {
                float Display{Field.GetScalarValue(Config) * Scale};
                const bool Edited{UseSlider ? ImGui::SliderFloat("##Value", &Display, Min, Max, Field.mFormat, Flags) : ImGui::DragFloat("##Value", &Display, Field.mSpeed, Min, Max, Field.mFormat, Flags)};

                if (Edited) {
                    Field.SetScalarValue(Config, std::clamp(Display / Scale, Field.mMin, Field.mMax));
                }
            } else {
                FVector* const Value{Field.mGetVector(Config)};
                float Display[3]{Value->x * Scale, Value->y * Scale, Value->z * Scale};
                const bool Edited{UseSlider ? ImGui::SliderFloat3("##Value", Display, Min, Max, Field.mFormat, Flags) : ImGui::DragFloat3("##Value", Display, Field.mSpeed, Min, Max, Field.mFormat, Flags)};

                if (Edited) {
                    *Value = FVector{std::clamp(Display[0] / Scale, Field.mMin, Field.mMax), std::clamp(Display[1] / Scale, Field.mMin, Field.mMax), std::clamp(Display[2] / Scale, Field.mMin, Field.mMax)};
                }
            }

            // 끄는 동안 매 프레임 천을 다시 만들면 천이 계속 처음 모양으로 돌아가므로 손을 뗄 때 한 번 반영합니다.
            Committed = ImGui::IsItemDeactivatedAfterEdit();
            ShowFieldTooltip(Field);

            if (ImGui::BeginPopupContextItem("Reset")) {
                if (ImGui::MenuItem("Reset to Default", nullptr, false, Changed)) {
                    ResetToDefault(Field, Config);
                    Committed = true;
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();

            if (Changed) {
                ImGui::PopStyleColor();
            }

            return Committed;
        }

        /**
         * @brief 이름, 값, 되돌리기 버튼을 표의 한 행으로 그립니다.
         * @param[in] Field 설정 항목입니다.
         * @param[in,out] Config 값을 가진 설정입니다.
         * @param[in] Disabled 다른 설정 때문에 쓰지 않는 칸이면 true입니다.
         * @return 값 편집을 마쳐 미리 보기에 반영해야 하면 true입니다.
         */
        bool DrawFieldRow(const ClothEditing::FClothConfigField& Field, PhysicsRuntime::UClothConfigNv& Config, bool Disabled) {
            const bool Changed{IsChangedFromDefault(Field, Config)};
            bool Committed{};

            ImGui::TableNextRow();
            ImGui::BeginDisabled(Disabled);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            if (Changed) {
                ImGui::PushStyleColor(ImGuiCol_Text, ChangedLabelColor);
            }

            ImGui::TextUnformatted(Field.mLabel);

            if (Changed) {
                ImGui::PopStyleColor();
            }

            ShowFieldTooltip(Field);

            ImGui::TableSetColumnIndex(1);
            Committed = DrawFieldWidget(Field, Config);

            ImGui::TableSetColumnIndex(2);

            if (Changed) {
                ImGui::PushID(Field.mKey);

                if (ImGui::SmallButton("Reset")) {
                    ResetToDefault(Field, Config);
                    Committed = true;
                }

                ImGui::SetItemTooltip("기본값으로 되돌립니다.");
                ImGui::PopID();
            }

            ImGui::EndDisabled();

            return Committed;
        }

        /**
         * @brief 광선이 삼각형과 만나는지 검사하고 만나는 거리를 구합니다. 삼각형의 앞뒤를 가리지 않습니다.
         * @param[in] Origin 광선 시작점입니다.
         * @param[in] Direction 광선 방향입니다.
         * @param[in] A 삼각형 첫 꼭짓점입니다.
         * @param[in] B 삼각형 둘째 꼭짓점입니다.
         * @param[in] C 삼각형 셋째 꼭짓점입니다.
         * @param[out] Distance 시작점에서 만나는 점까지 방향 길이 단위의 거리입니다.
         * @return 시작점 앞쪽에서 만나면 true입니다.
         */
        bool IntersectRayTriangle(const FVector& Origin, const FVector& Direction, const FVector& A, const FVector& B, const FVector& C, float& Distance) {
            // 삼각형 평면과의 교점을 무게중심 좌표로 바로 구하는 방법(Moller-Trumbore)입니다.
            constexpr float Epsilon{1.e-8f};
            const FVector EdgeAB{B - A};
            const FVector EdgeAC{C - A};
            const FVector P{Direction.Cross(EdgeAC)};
            const float Determinant{EdgeAB.Dot(P)};

            // 광선이 삼각형 평면과 나란하면 만나지 않습니다.
            if (std::abs(Determinant) < Epsilon) {
                return false;
            }

            const float InverseDeterminant{1.0f / Determinant};
            const FVector ToOrigin{Origin - A};
            const float U{ToOrigin.Dot(P) * InverseDeterminant};

            if (U < 0.0f || U > 1.0f) {
                return false;
            }

            const FVector Q{ToOrigin.Cross(EdgeAB)};
            const float V{Direction.Dot(Q) * InverseDeterminant};

            if (V < 0.0f || U + V > 1.0f) {
                return false;
            }

            Distance = EdgeAC.Dot(Q) * InverseDeterminant;

            return Distance > Epsilon;
        }

        /**
         * @brief 값 하나를 칠하기 대상의 단위 형식으로 문자열로 만듭니다.
         * @param[in] Format 단위를 포함한 printf 형식입니다.
         * @param[in] Value 값입니다.
         * @return 형식에 맞춘 문자열입니다.
         */
        Core::FString FormatValue(const char* Format, float Value) {
            char Buffer[64]{};
            std::snprintf(Buffer, sizeof(Buffer), Format, Value);

            return Buffer;
        }
    }

    /**
     * @brief 미리 보기에서 천을 움직일 천 시스템을 연결합니다.
     */
    void Editor::FMeshViewer::SetClothingSystem(PhysicsRuntime::FClothingSystemRuntimeModuleNv* ClothingSystem) {
        ReleaseClothPreview();
        mClothingSystem = ClothingSystem;
        mClothPreviewStale = true;
    }

    /**
     * @brief 고른 천 에셋을 반환합니다.
     */
    PhysicsRuntime::UClothingAssetCommon* Editor::FMeshViewer::GetSelectedClothingAsset() const {
        if (mSkeletalMesh == nullptr) {
            return nullptr;
        }

        const auto& ClothingAssets{mSkeletalMesh->GetMeshClothingAssets()};

        return mClothSelectedAsset < ClothingAssets.size() ? ClothingAssets[mClothSelectedAsset].get() : nullptr;
    }

    /**
     * @brief 천 칠하기를 쓸 수 있는 상태인지 검사합니다.
     */
    bool Editor::FMeshViewer::IsClothPainting() const {
        return mClothPaintEnabled && GetSelectedClothingAsset() != nullptr;
    }

    /**
     * @brief 천 칸 아래에 결과 문장을 잠시 보여 줍니다.
     */
    void Editor::FMeshViewer::ShowClothMessage(const Core::FString& Message) {
        mClothMessage = Message;
        mClothMessageTime = ImGui::GetTime();
    }

    /**
     * @brief 천 에셋이 바뀌었음을 기록하고 미리 보기 천을 다시 만들게 합니다.
     */
    void Editor::FMeshViewer::MarkClothChanged(const Core::FString& Message) {
        mClothUnsaved = true;
        mClothPreviewStale = true;
        ShowClothMessage(Message);
    }

    /**
     * @brief 천 칸 전체를 그립니다.
     */
    void Editor::FMeshViewer::DrawClothing() {
        Asset::USkeletalMesh& Mesh{*mSkeletalMesh.Get()};

        if (mSkeleton == nullptr) {
            ImGui::TextColored(ImVec4{1.0f, 0.75f, 0.3f, 1.0f}, "Pick a Skeleton above first.");
            ShowTextTooltip("천은 뼈를 따라 움직이므로 스켈레톤이 필요함");
        }

        DrawClothSections();

        const auto& ClothingAssets{Mesh.GetMeshClothingAssets()};

        if (ClothingAssets.empty()) {
            ImGui::TextDisabled("No cloth yet. Press Create Cloth on the section that should move like cloth.");
        } else {
            mClothSelectedAsset = std::min(mClothSelectedAsset, ClothingAssets.size() - 1);

            // 미리 보기에서 천을 움직일지 고르고, 처음 모양에서 다시 시작하게 합니다.
            ImGui::BeginDisabled(mClothingSystem == nullptr);

            if (ImGui::Checkbox("Simulate in Preview", &mClothSimulate)) {
                mClothPreviewStale = true;
            }

            ShowTextTooltip("미리 보기에서 천을 움직임. 값을 바꾸면 바로 반영");
            ImGui::SameLine();

            if (ImGui::Button("Restart")) {
                mClothPreviewStale = true;
            }

            ShowTextTooltip("천을 처음 모양으로 되돌려 다시 시작");
            ImGui::EndDisabled();

            if (mClothingSystem == nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("(not available in this window)");
            } else if (IsClothPainting()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(paused while painting)");
            }

            const PhysicsRuntime::UClothingAssetCommon& Selected{*ClothingAssets[mClothSelectedAsset]};
            const Index_t NumParticles{Selected.mLodData.empty() ? 0 : Selected.mLodData[0].mPhysicalMeshData.mVertices.size()};
            const Core::FString Title{"Cloth " + std::to_string(mClothSelectedAsset) + "  (" + std::to_string(NumParticles) + " particles)"};

            ImGui::SeparatorText(Title.c_str());

            if (ImGui::BeginTabBar("ClothTabs")) {
                bool PaintTabOpen{};

                // 탭 설명은 탭 내용을 그리기 전에 붙여야 탭 머리에 마우스를 올렸을 때 나옵니다.
                const bool SettingsOpen{ImGui::BeginTabItem("Settings")};
                ShowTextTooltip("늘어남, 처짐, 흔들림, 바람");

                if (SettingsOpen) {
                    if (PhysicsRuntime::UClothConfigNv* Config{ClothingAssets[mClothSelectedAsset]->GetClothConfig()}) {
                        DrawClothSettings(*Config);
                    }

                    ImGui::EndTabItem();
                }

                PaintTabOpen = ImGui::BeginTabItem("Paint");
                ShowTextTooltip("입자별 값(움직일 수 있는 거리 등)을 붓으로 칠함");

                if (PaintTabOpen) {
                    DrawClothPaintSettings(*ClothingAssets[mClothSelectedAsset]);
                    ImGui::EndTabItem();
                }

                const bool CollisionOpen{ImGui::BeginTabItem("Collision")};
                ShowTextTooltip("천이 부딪힐 물리 에셋");

                if (CollisionOpen) {
                    DrawClothCollision(*ClothingAssets[mClothSelectedAsset]);
                    ImGui::EndTabItem();
                }

                // 칠하기 탭을 떠나면 왼쪽 버튼을 다시 카메라와 뼈 선택에 돌려줍니다.
                if (!PaintTabOpen) {
                    mClothPaintEnabled = false;
                }

                ImGui::EndTabBar();
            }
        }

        ImGui::Separator();

        // 바꾼 것이 있을 때만 저장할 수 있게 해 저장 상태를 한눈에 알 수 있게 합니다.
        ImGui::BeginDisabled(!mClothUnsaved);

        if (ImGui::Button("Save Mesh")) {
            Core::FString Error{};

            // 뼈를 파일 안에 넣은 예전 메시는 새 형식으로 저장하면 뼈가 사라지므로 막습니다.
            if (mAssets->FindLegacySkeleton(Core::FilePath::FromUtf8(mPath)) != nullptr) {
                ShowClothMessage("This mesh uses an embedded skeleton, so its file cannot be saved safely.");
            } else if (mAssets->SaveSkeletalMesh(Core::FilePath::FromUtf8(mPath), Mesh, Error)) {
                mClothUnsaved = false;
                ShowClothMessage("Saved " + mPath + ".");
            } else {
                ShowClothMessage("Cannot save the skeletal mesh. " + Error);
            }
        }

        ImGui::EndDisabled();
        ShowTextTooltip("천과 설정, 칠한 값을 메시 파일에 저장");
        ImGui::SameLine();

        if (mClothUnsaved) {
            ImGui::TextColored(ImVec4{1.0f, 0.78f, 0.35f, 1.0f}, "Unsaved changes");
        } else {
            ImGui::TextDisabled("No unsaved changes");
        }

        if (!mClothMessage.empty() && ImGui::GetTime() - mClothMessageTime < ClothMessageSeconds) {
            ImGui::TextWrapped("%s", mClothMessage.c_str());
        }
    }

    /**
     * @brief 섹션마다 머티리얼, 천 상태, 천 만들기 버튼을 표로 그리고 천을 고르게 합니다.
     */
    void Editor::FMeshViewer::DrawClothSections() {
        Asset::USkeletalMesh& Mesh{*mSkeletalMesh.Get()};
        const auto& SubMeshes{Mesh.GetMeshData().GetSubMeshes()};
        const auto& Slots{Mesh.GetMeshData().GetMaterialSlots()};
        const auto& ClothingAssets{Mesh.GetMeshClothingAssets()};
        const ImGuiTableFlags Flags{ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp};

        if (!ImGui::BeginTable("ClothSections", 4, Flags)) {
            return;
        }

        ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Material");
        ImGui::TableSetupColumn("Cloth");
        ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (Index_t SubMeshIndex{}; SubMeshIndex < SubMeshes.size(); ++SubMeshIndex) {
            const Asset::FClothingSectionData* SectionData{Mesh.GetSectionClothingData(SubMeshIndex)};
            const bool HasCloth{SectionData != nullptr && SectionData->IsValid() && SectionData->mCorrespondClothAssetIndex < ClothingAssets.size()};
            const Index_t MaterialIndex{SubMeshes[SubMeshIndex].GetMaterialIndex()};

            ImGui::PushID(static_cast<int>(SubMeshIndex));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu", SubMeshIndex);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(MaterialIndex < Slots.size() ? Slots[MaterialIndex].GetName().c_str() : "-");

            ImGui::TableSetColumnIndex(2);

            if (HasCloth) {
                const Index_t AssetIndex{SectionData->mCorrespondClothAssetIndex};
                const PhysicsRuntime::UClothingAssetCommon& ClothingAsset{*ClothingAssets[AssetIndex]};
                const Index_t NumParticles{ClothingAsset.mLodData.empty() ? 0 : ClothingAsset.mLodData[0].mPhysicalMeshData.mVertices.size()};
                const Core::FString Label{"Cloth " + std::to_string(AssetIndex) + "  (" + std::to_string(NumParticles) + " particles)"};

                if (ImGui::Selectable(Label.c_str(), AssetIndex == mClothSelectedAsset)) {
                    mClothSelectedAsset = AssetIndex;
                }

                ShowTextTooltip("이 천을 아래 탭에서 편집");
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableSetColumnIndex(3);

            if (HasCloth) {
                if (ImGui::SmallButton("Rebuild")) {
                    Core::FString Error{};

                    if (Editor::ClothEditing::RebuildSectionMapping(Mesh, SubMeshIndex, Error)) {
                        MarkClothChanged("Rebuilt the render mapping of section " + std::to_string(SubMeshIndex) + ". Save the mesh to keep it.");
                    } else {
                        ShowClothMessage(Error);
                    }
                }

                ShowTextTooltip("렌더 정점과 천 입자의 연결을 다시 만듦.\n칠한 값과 설정은 그대로");
            } else {
                ImGui::BeginDisabled(mSkeleton == nullptr);

                if (ImGui::SmallButton("Create Cloth")) {
                    // 메시가 쓰는 물리 에셋과 부딪히게 만들고, 저장한 물리 에셋이면 경로도 남깁니다.
                    Core::FString Error{};
                    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{Mesh.GetPhysicsAssetPath().empty() ? Mesh.GetGeneratedPhysicsAsset(*mSkeleton.Get()) : mAssets->LoadPhysicsAsset(Core::FilePath::FromUtf8(Mesh.GetPhysicsAssetPath()), Error)};

                    if (Editor::ClothEditing::CreateClothFromSection(Mesh, *mSkeleton.Get(), SubMeshIndex, PhysicsAsset, Mesh.GetPhysicsAssetPath(), Error) != nullptr) {
                        const Asset::FClothingSectionData* Created{Mesh.GetSectionClothingData(SubMeshIndex)};
                        mClothSelectedAsset = Created != nullptr ? Created->mCorrespondClothAssetIndex : 0;
                        MarkClothChanged("Created cloth on section " + std::to_string(SubMeshIndex) + ". Paint Max Distance to choose what moves.");
                    } else {
                        ShowClothMessage(Error);
                    }
                }

                ImGui::EndDisabled();
                ShowTextTooltip("이 섹션을 천으로 만듦.\n처음엔 위쪽 20%만 고정, Paint 탭에서 고침");
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    /**
     * @brief 고른 천 에셋의 설정을 묶음별 표로 그리고 바꾼 값을 미리 보기에 반영합니다.
     */
    void Editor::FMeshViewer::DrawClothSettings(PhysicsRuntime::UClothConfigNv& Config) {
        const auto& Fields{ClothEditing::GetConfigFields()};
        const float Scale{ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi};
        bool Committed{};

        ImGui::TextDisabled("Hover for help | Orange = changed | Right-click: reset | Ctrl+click: type");

        // 강성은 네 종류 연결마다 같은 네 값이 있으므로 행과 열로 된 표로 보여 줍니다.
        if (ImGui::CollapsingHeader(ClothEditing::StiffnessGroup, ImGuiTreeNodeFlags_DefaultOpen)) {
            ShowTextTooltip("네 종류 연결의 세기. 행·열 이름에 설명 있음");

            const auto& Rows{ClothEditing::GetStiffnessRows()};
            const auto& Columns{ClothEditing::GetStiffnessColumns()};

            if (ImGui::BeginTable("StiffnessTable", 1 + static_cast<int>(Columns.size()), ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("##Row", ImGuiTableColumnFlags_WidthFixed, 80.0f * Scale);

                for (const ClothEditing::FClothLabel& Column : Columns) {
                    ImGui::TableSetupColumn(Column.mLabel);
                }

                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

                for (Index_t ColumnIndex{}; ColumnIndex < Columns.size(); ++ColumnIndex) {
                    ImGui::TableSetColumnIndex(static_cast<int>(ColumnIndex) + 1);
                    ImGui::TableHeader(Columns[ColumnIndex].mLabel);
                    ShowLabelTooltip(Columns[ColumnIndex]);
                }

                for (Index_t RowIndex{}; RowIndex < Rows.size(); ++RowIndex) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(Rows[RowIndex].mLabel);
                    ShowLabelTooltip(Rows[RowIndex]);

                    for (Index_t ColumnIndex{}; ColumnIndex < Columns.size(); ++ColumnIndex) {
                        const Index_t FieldIndex{RowIndex * Columns.size() + ColumnIndex};

                        ImGui::TableSetColumnIndex(static_cast<int>(ColumnIndex) + 1);

                        if (FieldIndex < Fields.size() && Core::FString{Fields[FieldIndex].mGroup} == ClothEditing::StiffnessGroup) {
                            Committed = DrawFieldWidget(Fields[FieldIndex], Config) || Committed;
                        }
                    }
                }

                ImGui::EndTable();
            }
        }

        for (const ClothEditing::FClothLabel& Group : ClothEditing::GetConfigGroups()) {
            const Core::FString GroupName{Group.mLabel};
            const ImGuiTreeNodeFlags HeaderFlags{GroupName == "Simulation" ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None};

            if (!ImGui::CollapsingHeader(Group.mLabel, HeaderFlags)) {
                ShowTextTooltip(Group.mTooltip);

                continue;
            }

            ShowTextTooltip(Group.mTooltip);

            // 바람 방식은 실수 값이 아니므로 표 위에 따로 고르게 합니다.
            if (GroupName == "Wind") {
                const char* const Methods[]{"Legacy", "Accurate"};
                int Method{Config.mClothingWindMethod == PhysicsRuntime::EClothingWindMethodNv::Accurate ? 1 : 0};

                if (ImGui::Combo("Wind Method", &Method, Methods, 2)) {
                    Config.mClothingWindMethod = Method == 1 ? PhysicsRuntime::EClothingWindMethodNv::Accurate : PhysicsRuntime::EClothingWindMethodNv::Legacy;
                    Committed = true;
                }

                ShowTextTooltip(ClothEditing::GetWindMethodTooltip());
            }

            if (!ImGui::BeginTable(Group.mLabel, 3, ImGuiTableFlags_SizingStretchProp)) {
                continue;
            }

            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f * Scale);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, 50.0f * Scale);

            // 중력 덮어쓰기는 켜고 끄는 값이므로 중력 칸 바로 앞에 체크 상자로 둡니다.
            if (GroupName == "Simulation") {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Override Gravity");
                ShowTextTooltip("켜면 아래 Gravity 값을 중력으로 씀");
                ImGui::TableSetColumnIndex(1);

                if (ImGui::Checkbox("##OverrideGravity", &Config.mUseGravityOverride)) {
                    Committed = true;
                }
            }

            for (const ClothEditing::FClothConfigField& Field : Fields) {
                if (GroupName != Field.mGroup) {
                    continue;
                }

                // 다른 설정 때문에 쓰지 않는 칸은 잠가서 바꿔도 아무 일이 없는 칸을 만지지 않게 합니다.
                const Core::FString Key{Field.mKey};
                const bool UsesOverride{Config.mUseGravityOverride};
                const bool Legacy{Config.mClothingWindMethod == PhysicsRuntime::EClothingWindMethodNv::Legacy};
                const bool Disabled{(Key == "gravityscale" && UsesOverride) || (Key == "gravityoverride" && !UsesOverride) || ((Key == "winddrag" || Key == "windlift") && Legacy)};

                Committed = DrawFieldRow(Field, Config, Disabled) || Committed;
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        if (ImGui::Button("Reset All to Defaults")) {
            for (const ClothEditing::FClothConfigField& Field : Fields) {
                ResetToDefault(Field, Config);
            }

            Config.mUseGravityOverride = false;
            Config.mClothingWindMethod = PhysicsRuntime::EClothingWindMethodNv::Legacy;
            Committed = true;
        }

        ShowTextTooltip("모든 설정을 기본값으로");

        if (Committed) {
            // 천끼리 충돌 거리가 바뀌었을 수 있으므로 부딪힐 입자를 다시 고릅니다.
            if (PhysicsRuntime::UClothingAssetCommon* ClothingAsset{GetSelectedClothingAsset()}) {
                ClothingAsset->BuildSelfCollisionData();
            }

            MarkClothChanged("Settings applied to the preview. Save the mesh to keep them.");
        }
    }

    /**
     * @brief 칠하기 켜기, 대상, 도구, 값, 붓 크기, 색 범례를 그립니다.
     */
    void Editor::FMeshViewer::DrawClothPaintSettings(PhysicsRuntime::UClothingAssetCommon&) {
        ImGui::Checkbox("Paint in Viewport", &mClothPaintEnabled);
        ShowTextTooltip("켜면 왼쪽 버튼으로 칠함. 회전은 오른쪽 버튼");

        ImGui::Checkbox("Cull Back Faces", &mClothCullBackFaces);
        ShowTextTooltip("뒤쪽 면 와이어프레임을 숨김");
        ImGui::SameLine();
        ImGui::Checkbox("Front Facing Only", &mClothFrontFacingOnly);
        ShowTextTooltip("카메라를 향한 입자만 칠함");

        const ClothEditing::FPaintTargetInfo& Info{ClothEditing::GetPaintTargetInfo(mClothPaintTarget)};

        if (ImGui::BeginCombo("Target", Info.mName)) {
            for (int TargetIndex{}; TargetIndex < ClothEditing::NumPaintTargets; ++TargetIndex) {
                const ClothEditing::FPaintTargetInfo& Candidate{ClothEditing::GetPaintTargetInfo(TargetIndex)};

                if (ImGui::Selectable(Candidate.mName, TargetIndex == mClothPaintTarget)) {
                    mClothPaintTarget = TargetIndex;
                }

                ShowTextTooltip(Candidate.mTooltip);
            }

            ImGui::EndCombo();
        }

        ShowTextTooltip(Info.mTooltip);

        // 도구는 한 줄에 나란히 놓고 각각 무엇을 하는지 툴팁으로 알려 줍니다.
        const auto& Tools{ClothEditing::GetPaintTools()};

        for (Index_t ToolIndex{}; ToolIndex < Tools.size(); ++ToolIndex) {
            if (ToolIndex > 0) {
                ImGui::SameLine();
            }

            ImGui::RadioButton(Tools[ToolIndex].mLabel, &mClothPaintTool, static_cast<int>(ToolIndex));
            ShowLabelTooltip(Tools[ToolIndex]);
        }

        const auto Tool{static_cast<ClothEditing::EClothPaintTool>(mClothPaintTool)};

        if (Tool != ClothEditing::EClothPaintTool::Smooth) {
            ImGui::DragFloat("Value", &mClothPaintValue, Info.mSpeed, 0.0f, Info.mMax, Info.mFormat, ImGuiSliderFlags_AlwaysClamp);
            ShowTextTooltip("칠할 값. 단위는 Target 설명 참고");
        } else {
            ImGui::SliderFloat("Strength", &mClothSmoothStrength, 0.0f, 1.0f, "%.2f");
            ShowTextTooltip("다듬는 세기");
        }

        if (Tool == ClothEditing::EClothPaintTool::Fill) {
            ImGui::DragFloat("Threshold", &mClothFillThreshold, Info.mSpeed, 0.0f, Info.mMax, Info.mFormat, ImGuiSliderFlags_AlwaysClamp);
            ShowTextTooltip("시작 값과 이만큼 차이 나는 입자까지 채움");
        } else {
            float Radius{mClothPaintRadius.GetBase()};

            if (ImGui::SliderFloat("Brush Radius", &Radius, 0.001f, 0.5f, "%.3f m", ImGuiSliderFlags_Logarithmic)) {
                mClothPaintRadius = Core::FLength::FromBase(Radius);
            }
            ShowTextTooltip("붓 반지름(m). 가리킨 표면 둘레만 칠함\n아주 작아도 가장 가까운 입자 하나는 칠함");
        }

        // 색 범례: 자홍색 칸이 0이고, 그 옆 막대는 0보다 큰 값이 검정에서 흰색(가장 큰 값)으로 밝아지는 모습입니다.
        ImGui::Spacing();

        const float BarHeight{ImGui::GetTextLineHeight()};
        const ImVec2 ZeroMin{ImGui::GetCursorScreenPos()};

        ImGui::GetWindowDrawList()->AddRectFilled(ZeroMin, ImVec2{ZeroMin.x + BarHeight, ZeroMin.y + BarHeight}, IM_COL32(255, 0, 255, 255));
        ImGui::Dummy(ImVec2{BarHeight, BarHeight});
        ShowTextTooltip("값 0");
        ImGui::SameLine();
        ImGui::TextUnformatted("0");
        ImGui::SameLine();

        const ImVec2 BarMin{ImGui::GetCursorScreenPos()};
        const float BarWidth{std::max(ImGui::GetContentRegionAvail().x - 90.0f, 40.0f)};
        const ImU32 Black{IM_COL32(0, 0, 0, 255)};
        const ImU32 White{IM_COL32(255, 255, 255, 255)};

        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(BarMin, ImVec2{BarMin.x + BarWidth, BarMin.y + BarHeight}, Black, White, White, Black);
        ImGui::Dummy(ImVec2{BarWidth, BarHeight});
        ShowTextTooltip("0보다 큰 값. 밝을수록 큼");
        ImGui::SameLine();
        ImGui::TextUnformatted(FormatValue(Info.mFormat, mClothPaintMaxValue).c_str());

        if (IsClothPainting() && mClothHoveredParticle != InvalidIndex) {
            ImGui::Text("Particle %zu: %s", mClothHoveredParticle, FormatValue(Info.mFormat, mClothHoveredValue).c_str());
        } else {
            ImGui::TextDisabled("Point at the cloth in the preview to read a particle value.");
        }

        ImGui::TextDisabled("Left drag: paint | Shift + drag: paint 0 | Right drag: orbit | Middle: pan");
    }

    /**
     * @brief 천이 부딪힐 물리 에셋을 고르는 칸을 그립니다.
     */
    void Editor::FMeshViewer::DrawClothCollision(PhysicsRuntime::UClothingAssetCommon& ClothingAsset) {
        // 천 전용 물리 에셋을 고릅니다. 메시가 쓰는 물리 에셋과 따로 정할 수 있습니다.
        const Core::FString Current{ClothingAsset.mPhysicsAssetPath};

        ImGui::BeginDisabled(mSkeleton == nullptr);

        if (ImGui::BeginCombo("Physics Asset##Cloth", Current.empty() ? "None" : Current.c_str())) {
            Core::FString ListError{};

            if (ImGui::Selectable("None", Current.empty())) {
                ClothingAsset.SetPhysicsAsset(nullptr, Editor::ClothEditing::GetSkeletonBoneNames(*mSkeleton.Get()));
                ClothingAsset.mPhysicsAssetPath.clear();
                MarkClothChanged("The cloth no longer collides with a physics asset.");
            }

            for (const auto& Candidate : mAssets->GetAssetPaths(ListError)) {
                Asset::EAssetType Type{};

                if (!Asset::FAssetFile::GetType(mAssets->ResolvePath(Candidate, ListError), Type, ListError) || Type != Asset::EAssetType::PhysicsAsset) {
                    continue;
                }

                const Core::FString Relative{Core::FilePath::ToGenericUtf8(Candidate)};

                if (ImGui::Selectable(Relative.c_str(), Relative == Current)) {
                    Core::FString LoadError{};
                    PhysicsRuntime::UPhysicsAsset* PhysicsAsset{mAssets->LoadPhysicsAsset(Candidate, LoadError)};

                    if (PhysicsAsset != nullptr) {
                        ClothingAsset.SetPhysicsAsset(PhysicsAsset, Editor::ClothEditing::GetSkeletonBoneNames(*mSkeleton.Get()));
                        ClothingAsset.mPhysicsAssetPath = Relative;
                        MarkClothChanged("The cloth now collides with " + Relative + ".");
                    } else {
                        ShowClothMessage(LoadError);
                    }
                }
            }

            ImGui::EndCombo();
        }

        ImGui::EndDisabled();
        ShowTextTooltip("이 물리 에셋의 구·캡슐이 천을 밀어냄 (각 최대 32개)");

        ImGui::TextWrapped("Spheres and capsules of this physics asset push the cloth away. Up to 32 spheres and 32 capsules are used.");
        ImGui::TextDisabled("Thickness and Friction are in Settings > Collision.");
    }

    /**
     * @brief 칠할 천 메시를 가중치 색 와이어프레임으로 그리고, 왼쪽 버튼으로 고른 도구를 적용합니다.
     */
    void Editor::FMeshViewer::DrawClothPaint(const Engine::FSceneView& View, const FVector2& Origin, const FVector2& Mouse, bool Hovered, const Core::FInput& Input, Engine::FPrimitiveDrawInterface& DrawInterface) {
        using Core::EKey;

        PhysicsRuntime::UClothingAssetCommon* const ClothingAsset{GetSelectedClothingAsset()};
        mClothHoveredParticle = InvalidIndex;

        if (ClothingAsset == nullptr || ClothingAsset->mLodData.empty()) {
            return;
        }

        PhysicsRuntime::FClothPhysicalMeshData& PhysMesh{ClothingAsset->mLodData[0].mPhysicalMeshData};
        PhysicsRuntime::FPointWeightMap& Map{PhysMesh.AddWeightMap(Editor::ClothEditing::GetPaintTarget(mClothPaintTarget))};
        const Core::TArray<FVector>& Vertices{PhysMesh.mVertices};
        const Core::TArray<Uint32>& Indices{PhysMesh.mIndices};
        const Index_t NumParticles{Vertices.size()};

        // 처음 칠하는 맵은 입자 수만큼 0으로 채웁니다.
        if (Map.mValues.size() != NumParticles) {
            Map.mValues.resize(NumParticles, 0.0f);
        }

        // 마우스 위치에서 화면 안쪽으로 광선을 만듭니다. 광선의 시작점이 카메라 위치입니다.
        FRay Ray{};
        const bool HasRay{View.Deproject(Mouse, Ray)};
        const FVector CameraPosition{HasRay ? Ray.mOrigin : FVector{}};

        // 삼각형 면 방향은 감는 순서에 따라 뒤집힐 수 있으므로, 세 입자 법선의 합과 같은 쪽을 보게 맞춥니다.
        const auto GetFaceNormal{[&](Uint32 IndexA, Uint32 IndexB, Uint32 IndexC) {
            FVector Normal{(Vertices[IndexB] - Vertices[IndexA]).Cross(Vertices[IndexC] - Vertices[IndexA])};

            if (PhysMesh.mNormals.size() == NumParticles && Normal.Dot(PhysMesh.mNormals[IndexA] + PhysMesh.mNormals[IndexB] + PhysMesh.mNormals[IndexC]) < 0.0f) {
                Normal = -Normal;
            }

            return Normal;
        }};

        // 입자가 카메라를 향하는지는 입자 법선과 입자에서 카메라로 가는 방향의 내적으로 봅니다.
        const auto FacesCamera{[&](Index_t Particle) {
            return PhysMesh.mNormals.size() != NumParticles || PhysMesh.mNormals[Particle].Dot(CameraPosition - Vertices[Particle]) > 0.0f;
        }};

        // 광선이 천 표면에 처음 맞은 점을 찾습니다. 앞자락을 가리키면 그 뒤의 뒷자락은 맞지 않습니다.
        float ClosestHit{std::numeric_limits<float>::max()};
        FVector HitPoint{};
        FVector HitNormal{};
        bool HasHit{};

        for (Index_t BaseIndex{}; HasRay && Hovered && BaseIndex + 2 < Indices.size(); BaseIndex += 3) {
            const Uint32 IndexA{Indices[BaseIndex]};
            const Uint32 IndexB{Indices[BaseIndex + 1]};
            const Uint32 IndexC{Indices[BaseIndex + 2]};

            if (IndexA >= NumParticles || IndexB >= NumParticles || IndexC >= NumParticles) {
                continue;
            }

            float Distance{};

            if (IntersectRayTriangle(Ray.mOrigin, Ray.mDirection, Vertices[IndexA], Vertices[IndexB], Vertices[IndexC], Distance) && Distance < ClosestHit) {
                ClosestHit = Distance;
                HitPoint = Ray.mOrigin + Ray.mDirection * Distance;
                HitNormal = GetFaceNormal(IndexA, IndexB, IndexC).Normalized();
                HasHit = true;
            }
        }

        // 맞은 점에서 3D 반지름 안의 입자를 붓 안으로 보고, 그중 맞은 점에 가장 가까운 입자를 마우스 아래 입자로 둡니다.
        Core::TArray<Index_t> InBrush{};
        const float RadiusSquared{(mClothPaintRadius * mClothPaintRadius).GetBase()};
        float ClosestParticle{std::numeric_limits<float>::max()};

        for (Index_t Particle{}; HasHit && Particle < NumParticles; ++Particle) {
            if (mClothFrontFacingOnly && !FacesCamera(Particle)) {
                continue;
            }

            const float DistanceSquared{(Vertices[Particle] - HitPoint).LengthSquared()};

            if (DistanceSquared <= RadiusSquared) {
                InBrush.push_back(Particle);
            }

            if (DistanceSquared < ClosestParticle) {
                ClosestParticle = DistanceSquared;
                mClothHoveredParticle = Particle;
            }
        }

        // 붓이 입자 사이 간격보다 작으면 공 안에 입자가 하나도 없을 수 있으므로, 맞은 점에 가장 가까운 입자는 항상 붓에 넣습니다.
        if (mClothHoveredParticle != InvalidIndex && std::find(InBrush.begin(), InBrush.end(), mClothHoveredParticle) == InBrush.end()) {
            InBrush.push_back(mClothHoveredParticle);
        }

        // 고른 도구를 적용합니다. 다듬기와 채우기는 삼각형 변으로 이어진 이웃이 필요합니다.
        const auto Tool{static_cast<ClothEditing::EClothPaintTool>(mClothPaintTool)};
        const bool Dragging{HasHit && Input.IsDown(EKey::MouseLeft) && !Input.IsDown(EKey::MouseRight) && !Input.IsDown(EKey::MouseMiddle)};
        const bool Pressed{HasHit && Input.WasPressed(EKey::MouseLeft) && !Input.IsDown(EKey::MouseRight) && !Input.IsDown(EKey::MouseMiddle)};
        const float PaintValue{Input.IsDown(EKey::Shift) ? 0.0f : mClothPaintValue};

        if (Tool == ClothEditing::EClothPaintTool::Brush && Dragging && !InBrush.empty()) {
            for (const Index_t Particle : InBrush) {
                Map.mValues[Particle] = PaintValue;
            }

            mClothPaintDirty = true;
        } else if (Tool == ClothEditing::EClothPaintTool::Smooth && Dragging && !InBrush.empty()) {
            ClothEditing::SmoothWeights(Map.mValues, ClothEditing::BuildParticleNeighbors(PhysMesh), InBrush, mClothSmoothStrength);
            mClothPaintDirty = true;
        } else if (Tool == ClothEditing::EClothPaintTool::Fill && Pressed && mClothHoveredParticle != InvalidIndex) {
            const Index_t NumFilled{ClothEditing::FillWeights(Map.mValues, ClothEditing::BuildParticleNeighbors(PhysMesh), mClothHoveredParticle, PaintValue, mClothFillThreshold)};
            mClothPaintDirty = NumFilled > 0;
        }

        float MaxValue{1.e-4f};

        for (const float Value : Map.mValues) {
            MaxValue = std::max(MaxValue, Value);
        }

        mClothPaintMaxValue = MaxValue;

        // 값 색: 0은 자홍색, 0보다 크면 검정에서 흰색으로 밝아지며 가장 큰 값이 흰색입니다.
        const auto GetWeightColor{[&](Index_t Particle) {
            const float Value{Map.mValues[Particle]};

            if (Value <= 0.0f) {
                return FVector{1.0f, 0.0f, 1.0f};
            }

            const float Ratio{std::clamp(Value / MaxValue, 0.0f, 1.0f)};

            return FVector{Ratio, Ratio, Ratio};
        }};

        // 삼각형마다 세 변을 그립니다. 변 하나를 반으로 나눠 각 절반을 가까운 끝 입자의 색으로 칠해 값이 바뀌는 곳이 보이게 합니다.
        for (Index_t BaseIndex{}; BaseIndex + 2 < Indices.size(); BaseIndex += 3) {
            const Uint32 Corners[3]{Indices[BaseIndex], Indices[BaseIndex + 1], Indices[BaseIndex + 2]};

            if (Corners[0] >= NumParticles || Corners[1] >= NumParticles || Corners[2] >= NumParticles) {
                continue;
            }

            // 카메라 반대쪽을 향한 삼각형은 그리지 않아 반대쪽 면이 비쳐 보이지 않게 합니다.
            if (mClothCullBackFaces && HasRay) {
                const FVector Center{(Vertices[Corners[0]] + Vertices[Corners[1]] + Vertices[Corners[2]]) * (1.0f / 3.0f)};

                if (GetFaceNormal(Corners[0], Corners[1], Corners[2]).Dot(CameraPosition - Center) <= 0.0f) {
                    continue;
                }
            }

            for (Index_t Corner{}; Corner < 3; ++Corner) {
                const Uint32 Start{Corners[Corner]};
                const Uint32 End{Corners[(Corner + 1) % 3]};
                const FVector Middle{(Vertices[Start] + Vertices[End]) * 0.5f};

                DrawInterface.DrawLine(Vertices[Start], Middle, GetWeightColor(Start));
                DrawInterface.DrawLine(Middle, Vertices[End], GetWeightColor(End));
            }
        }

        // 고정 입자와 역질량 계산은 무거우므로 버튼을 놓을 때 한 번 반영합니다.
        if (mClothPaintDirty && !Input.IsDown(EKey::MouseLeft)) {
            Editor::ClothEditing::ApplyPaintedWeights(*mSkeletalMesh.Get(), *ClothingAsset);
            mClothPaintDirty = false;
            MarkClothChanged("Painted " + Core::FString{ClothEditing::GetPaintTargetInfo(mClothPaintTarget).mName} + ". Turn off Paint in Viewport to see the cloth move.");
        }

        if (!HasHit) {
            return;
        }

        // 붓은 맞은 점에서 표면에 붙은 원으로 그립니다. 채우기는 입자 하나를 고르므로 원 대신 그 입자만 표시합니다.
        const FVector BrushColor{0.2f, 0.85f, 1.0f};

        if (Tool != ClothEditing::EClothPaintTool::Fill) {
            const FVector Helper{std::abs(HitNormal.z) < 0.9f ? FVector{0.0f, 0.0f, 1.0f} : FVector{1.0f, 0.0f, 0.0f}};
            const FVector AxisU{HitNormal.Cross(Helper).Normalized()};
            const FVector AxisV{HitNormal.Cross(AxisU)};
            constexpr Index_t NumSegments{48};
            constexpr float TwoPi{6.28318530718f};

            for (Index_t Segment{}; Segment < NumSegments; ++Segment) {
                const float AngleA{TwoPi * static_cast<float>(Segment) / static_cast<float>(NumSegments)};
                const float AngleB{TwoPi * static_cast<float>(Segment + 1) / static_cast<float>(NumSegments)};
                const FVector PointA{HitPoint + (AxisU * std::cos(AngleA) + AxisV * std::sin(AngleA)) * mClothPaintRadius.GetBase()};
                const FVector PointB{HitPoint + (AxisU * std::cos(AngleB) + AxisV * std::sin(AngleB)) * mClothPaintRadius.GetBase()};

                DrawInterface.DrawLine(PointA, PointB, BrushColor, false, 2.0f);
            }
        }

        // 마우스 아래 입자는 화면에 크게 표시하고, 마우스 옆에 그 값을 보여 줍니다.
        if (mClothHoveredParticle != InvalidIndex) {
            mClothHoveredValue = Map.mValues[mClothHoveredParticle];

            const FVector4 Clip{FVector4{Vertices[mClothHoveredParticle], 1.0f} * View.mViewProjection};
            ImDrawList* const DrawList{ImGui::GetWindowDrawList()};

            if (Clip.w > 1.e-6f) {
                const ImVec2 Center{Origin.x + (Clip.x / Clip.w * 0.5f + 0.5f) * View.mSize.x, Origin.y + (0.5f - Clip.y / Clip.w * 0.5f) * View.mSize.y};

                DrawList->AddCircle(Center, 6.0f, IM_COL32(50, 220, 255, 255), 0, 2.0f);
            }

            const Core::FString Text{FormatValue(ClothEditing::GetPaintTargetInfo(mClothPaintTarget).mFormat, mClothHoveredValue)};
            const ImVec2 TextPosition{Origin.x + Mouse.x + 14.0f, Origin.y + Mouse.y + 14.0f};
            const ImVec2 TextSize{ImGui::CalcTextSize(Text.c_str())};

            DrawList->AddRectFilled(ImVec2{TextPosition.x - 3.0f, TextPosition.y - 2.0f}, ImVec2{TextPosition.x + TextSize.x + 3.0f, TextPosition.y + TextSize.y + 2.0f}, IM_COL32(0, 0, 0, 180), 3.0f);
            DrawList->AddText(TextPosition, IM_COL32(255, 255, 255, 255), Text.c_str());
        }

    }

    /**
     * @brief 미리 보기 포즈로 천 시뮬레이션 입력을 채웁니다.
     */
    void Editor::FMeshViewer::UpdateClothPreviewContext(float DeltaTime) {
        // 한 프레임이 너무 길면 천이 튀므로 물리 한 번에 진행할 최대 시간으로 자릅니다.
        constexpr Core::FTime MaxPhysicsDelta{1.0f / Core::FFrequency::FromBase(30.0f)};
        const Engine::FSkeletalMeshPose& Pose{mContent->GetPose()};

        // 미리 보기 메시는 원점에 놓이므로 컴포넌트 변환은 단위 행렬입니다.
        mClothPreviewContext.mBoneTransforms = Pose.GetGlobalTransforms();
        mClothPreviewContext.mRefToLocals = Pose.GetSkinMatrices();
        mClothPreviewContext.mComponentToWorld = FMatrix::Identity();
        mClothPreviewContext.mWorldGravity = FVector{0.0f, 0.0f, -9.81f};
        mClothPreviewContext.mDeltaTime = std::min(Core::FTime::FromBase(DeltaTime), MaxPhysicsDelta);
        mClothPreviewContext.mPredictedLod = 0;
        mClothPreviewContext.mMaxDistanceScale = 1.0f;
        // 미리 보기에는 컴포넌트 바람이 없으므로, 게임에서 컴포넌트 바람이 0일 때처럼 천 에셋의 기본 바람을 씁니다.
        mClothPreviewContext.mWindVelocity = FVector{};
        mClothPreviewContext.mWindAdaption = 2.0f;
        mClothPreviewContext.mTeleportMode = PhysicsRuntime::EClothingTeleportMode::None;
    }

    /**
     * @brief 미리 보기 천 시뮬레이션을 해제하고 천 섹션을 스키닝 위치로 그리게 합니다.
     */
    void Editor::FMeshViewer::ReleaseClothPreview() {
        if (mClothPreview != nullptr) {
            mClothPreview->Shutdown();
            mClothPreview.reset();
        }

        if (mContent != nullptr) {
            mContent->ClearClothSimulationData();
        }
    }

    /**
     * @brief 미리 보기 천 시뮬레이션을 지금 천 에셋과 포즈로 다시 만듭니다.
     */
    void Editor::FMeshViewer::RecreateClothPreview() {
        ReleaseClothPreview();
        mClothPreviewStale = false;

        if (!mClothSimulate || mClothingSystem == nullptr || mContent == nullptr || mSkeletalMesh == nullptr || mSkeleton == nullptr || !mContent->GetPose().GetRevision().IsValid()) {
            return;
        }

        const auto& ClothingAssets{mSkeletalMesh->GetMeshClothingAssets()};

        if (ClothingAssets.empty()) {
            return;
        }

        // 천 에셋의 사용 뼈를 지금 고른 스켈레톤의 뼈 번호로 다시 찾습니다.
        const Core::TArray<Core::FString> SkeletonBoneNames{Editor::ClothEditing::GetSkeletonBoneNames(*mSkeleton.Get())};

        for (const auto& ClothingAsset : ClothingAssets) {
            ClothingAsset->RefreshBoneMapping(SkeletonBoneNames);
        }

        mClothPreview = mClothingSystem->CreateSimulation();

        if (mClothPreview == nullptr) {
            return;
        }

        // 섹션에 연결된 천 에셋만 천으로 만듭니다.
        Core::TArray<const PhysicsRuntime::UClothingAssetCommon*> AssetsInUse{};
        mSkeletalMesh->GetClothingAssetsInUse(AssetsInUse);
        UpdateClothPreviewContext(0.0f);

        for (Index_t AssetIndex{}; AssetIndex < ClothingAssets.size(); ++AssetIndex) {
            const PhysicsRuntime::UClothingAssetCommon* const ClothingAsset{ClothingAssets[AssetIndex].get()};

            if (std::find(AssetsInUse.begin(), AssetsInUse.end(), ClothingAsset) != AssetsInUse.end()) {
                mClothPreview->CreateActor(*ClothingAsset, AssetIndex, mClothPreviewContext);
            }
        }

        Core::TMap<Index_t, PhysicsRuntime::FClothSimulData> Data{};
        mClothPreview->GetSimulationData(Data, mClothPreviewContext);
        mContent->SetClothSimulationData(std::move(Data));
    }

    /**
     * @brief 미리 보기 천을 한 프레임 진행하고 결과를 미리 보기 장면에 넣습니다.
     */
    void Editor::FMeshViewer::TickClothPreview(float DeltaTime) {
        if (mContent == nullptr || mSkeletalMesh == nullptr) {
            return;
        }

        // 칠하는 동안에는 점이 처음 모양 위에 있으므로 천도 처음 모양으로 멈춰 둡니다.
        if (IsClothPainting()) {
            if (mClothPreview != nullptr) {
                ReleaseClothPreview();
            }

            mClothPreviewStale = true;

            return;
        }

        if (mClothPreviewStale) {
            RecreateClothPreview();
        }

        // 시간이 0이면 솔버가 속도를 구할 수 없으므로 진행하지 않습니다.
        if (mClothPreview == nullptr || DeltaTime <= 0.0f) {
            return;
        }

        UpdateClothPreviewContext(DeltaTime);
        mClothPreview->Simulate(mClothPreviewContext);

        Core::TMap<Index_t, PhysicsRuntime::FClothSimulData> Data{};
        mClothPreview->GetSimulationData(Data, mClothPreviewContext);
        mContent->SetClothSimulationData(std::move(Data));
    }
}
