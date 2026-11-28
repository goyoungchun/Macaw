#include "Editor/Landscape/FLandscapeWidget.h"
#include "Editor/FEditorContext.h"
#include <imgui.h>

namespace Editor {
    FLandscapeWidget::FLandscapeWidget(Editor::FEditorContext* Context)
		: Editor::FEditorContextWidget{Context, "Landscape"} {
    }

    FLandscapeWidget::~FLandscapeWidget() = default;

    void FLandscapeWidget::DrawContent(float) {
        Editor::FEditorContext* Context{GetEditorContext()};

        ImGui::SeparatorText("New Landscape");
        ImGui::BeginDisabled(!Context->CanEditWorld());

        if (ImGui::BeginTable("LandscapeSettings", 2)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            const auto BeginSetting{[](const char* Name) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Name);
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
            }};

            float Location[]{mSettings.mLocation.x, mSettings.mLocation.y, mSettings.mLocation.z};
            BeginSetting("Location (m)");
            if (ImGui::InputFloat3("##Location", Location)) {
                mSettings.mLocation = {Location[0], Location[1], Location[2]};
            }

            BeginSetting("Component Count X");
            ImGui::InputInt("##ComponentCountX", &mSettings.mComponentCountX, 0);
            BeginSetting("Component Count Y");
            ImGui::InputInt("##ComponentCountY", &mSettings.mComponentCountY, 0);
            BeginSetting("Proxy Size In Components");
            ImGui::InputInt("##ProxySizeInComponents", &mSettings.mProxySizeInComponents, 0);

            Int32 SectionLayout{mSettings.mSectionsPerComponent == Engine::ELandscapeSectionsPerComponent::Four ? 1 : 0};
            BeginSetting("Sections Per Component");
            if (ImGui::Combo("##SectionsPerComponent", &SectionLayout, "1 x 1\0" "2 x 2\0")) {
                mSettings.mSectionsPerComponent = SectionLayout == 0 ? Engine::ELandscapeSectionsPerComponent::One : Engine::ELandscapeSectionsPerComponent::Four;
            }

            BeginSetting("Quads Per Section");
            ImGui::InputInt("##QuadsPerSection", &mSettings.mQuadsPerSection, 0);
            ImGui::EndTable();
        }

        if (ImGui::Button("Create")) {
            Context->CreateLandscape(mSettings, mError);
        }

        ImGui::EndDisabled();

        if (!mError.empty()) {
            ImGui::TextWrapped("%s", mError.c_str());
        }
    }
}
