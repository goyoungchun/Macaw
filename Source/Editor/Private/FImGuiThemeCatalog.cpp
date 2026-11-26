/**
 * @file FImGuiThemeCatalog.cpp
 * @brief 기본 테마와 ImThemes TOML에서 읽은 이름·작성자·설명·ImGui 스타일을 관리합니다.
 * @ingroup Editor
 */

#include "Core/Types.h"
#include "FImGuiThemeCatalog.h"
#include "Core/FilePath.h"

#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Editor {

    /**
     * @brief 이 구현 파일에서만 사용하는 검증·변환·설정 보조 함수를 제공합니다.
     */
    namespace {
        /**
         * @brief ImThemes 스타일 표의 수치·방향·RGBA 색상을 검증하여 ImGui 스타일에 반영합니다.
         * @param[in] Table 검증할 ImThemes 스타일의 TOML 표입니다.
         * @param[in,out] Style 읽거나 갱신할 배율 적용 전 ImGui 스타일입니다.
         * @note 잘못된 수치·방향·색상 형식이면 std::runtime_error를 발생시키며 호출자가 오류를 처리합니다.
         */
        void ReadStyle(const toml::table& Table, ImGuiStyle& Style) {
            const std::pair<const char*, float ImGuiStyle::*> Floats[]{
                {"alpha", &ImGuiStyle::Alpha},
                {"disabledAlpha", &ImGuiStyle::DisabledAlpha},
                {"windowRounding", &ImGuiStyle::WindowRounding},
                {"windowBorderSize", &ImGuiStyle::WindowBorderSize},
                {"childRounding", &ImGuiStyle::ChildRounding},
                {"childBorderSize", &ImGuiStyle::ChildBorderSize},
                {"popupRounding", &ImGuiStyle::PopupRounding},
                {"popupBorderSize", &ImGuiStyle::PopupBorderSize},
                {"frameRounding", &ImGuiStyle::FrameRounding},
                {"frameBorderSize", &ImGuiStyle::FrameBorderSize},
                {"indentSpacing", &ImGuiStyle::IndentSpacing},
                {"columnsMinSpacing", &ImGuiStyle::ColumnsMinSpacing},
                {"scrollbarSize", &ImGuiStyle::ScrollbarSize},
                {"scrollbarRounding", &ImGuiStyle::ScrollbarRounding},
                {"grabMinSize", &ImGuiStyle::GrabMinSize},
                {"grabRounding", &ImGuiStyle::GrabRounding},
                {"tabRounding", &ImGuiStyle::TabRounding},
                {"tabBorderSize", &ImGuiStyle::TabBorderSize},
                {"tabMinWidthForCloseButton", &ImGuiStyle::TabCloseButtonMinWidthUnselected}
            };

            for (const auto& [Name, Member] : Floats) {
                if (Table.contains(Name)) {
                    const auto Value{Table[Name].value<double>()};

                    if (!Value || !std::isfinite(*Value) || *Value < 0.0 || *Value > 10000.0) {
                        throw std::runtime_error{Core::FString{"Invalid style value: "} + Name};
                    }

                    Style.*Member = static_cast<float>(*Value);
                }
            }

            if (Style.Alpha < 0.2f || Style.Alpha > 1.0f || Style.DisabledAlpha > 1.0f) {
                throw std::runtime_error{"Theme alpha must keep the editor visible"};
            }

            const std::pair<const char*, ImVec2 ImGuiStyle::*> Vectors[]{
                {"windowPadding", &ImGuiStyle::WindowPadding},
                {"windowMinSize", &ImGuiStyle::WindowMinSize},
                {"windowTitleAlign", &ImGuiStyle::WindowTitleAlign},
                {"framePadding", &ImGuiStyle::FramePadding},
                {"itemSpacing", &ImGuiStyle::ItemSpacing},
                {"itemInnerSpacing", &ImGuiStyle::ItemInnerSpacing},
                {"cellPadding", &ImGuiStyle::CellPadding},
                {"buttonTextAlign", &ImGuiStyle::ButtonTextAlign},
                {"selectableTextAlign", &ImGuiStyle::SelectableTextAlign}
            };

            for (const auto& [Name, Member] : Vectors) {
                if (!Table.contains(Name)) {
                    continue;
                }

                const toml::array* Values{Table[Name].as_array()};

                if (Values == nullptr || Values->size() != 2) {
                    throw std::runtime_error{Core::FString{"Invalid style vector: "} + Name};
                }

                const auto X{(*Values)[0].value<double>()};
                const auto Y{(*Values)[1].value<double>()};

                if (!X || !Y || !std::isfinite(*X) || !std::isfinite(*Y) || *X < 0.0 || *Y < 0.0 || *X > 10000.0 || *Y > 10000.0) {
                    throw std::runtime_error{Core::FString{"Invalid style vector: "} + Name};
                }

                Style.*Member = ImVec2{static_cast<float>(*X), static_cast<float>(*Y)};
            }

            Style.WindowMinSize.x = std::max(1.0f, Style.WindowMinSize.x);
            Style.WindowMinSize.y = std::max(1.0f, Style.WindowMinSize.y);

            for (const auto& [Name, Member] : {std::pair{"windowMenuButtonPosition", &ImGuiStyle::WindowMenuButtonPosition}, std::pair{"colorButtonPosition", &ImGuiStyle::ColorButtonPosition}}) {
                if (Table.contains(Name)) {
                    const Core::FString Value{Table[Name].value_or(Core::FString{})};

                    if (Value != "Left" && Value != "Right" && (Value != "None" || Member == &ImGuiStyle::ColorButtonPosition)) {
                        throw std::runtime_error{Core::FString{"Invalid style direction: "} + Name};
                    }

                    Style.*Member = Value == "Left" ? ImGuiDir_Left : Value == "Right" ? ImGuiDir_Right : ImGuiDir_None;
                }
            }

            const toml::table* Colors{Table["colors"].as_table()};

            if (Colors == nullptr) {
                throw std::runtime_error{"The theme has no colors table"};
            }

            for (const auto& [Key, Node] : *Colors) {
                Core::FString Name{Key.str()};

                if (Name == "TabActive") {
                    Name = "TabSelected";
                } else if (Name == "TabUnfocused") {
                    Name = "TabDimmed";
                } else if (Name == "TabUnfocusedActive") {
                    Name = "TabDimmedSelected";
                } else if (Name == "NavHighlight") {
                    Name = "NavCursor";
                }

                const auto Value{Node.value<Core::FString>()};

                if (!Value || !Value->starts_with("rgba(") || !Value->ends_with(')')) {
                    throw std::runtime_error{"Expected an ImThemes rgba color"};
                }

                Core::FString Components{Value->substr(5, Value->size() - 6)};

                std::replace(Components.begin(), Components.end(), ',', ' ');

                std::istringstream Input{Components};
                Core::TStaticArray<float, 4> Channels{};

                for (float& Channel : Channels) {
                    if (!(Input >> Channel) || !std::isfinite(Channel)) {
                        throw std::runtime_error{"Invalid ImThemes color channel"};
                    }
                }

                Input >> std::ws;

                if (!Input.eof() || Channels[0] < 0.0f || Channels[0] > 255.0f || Channels[1] < 0.0f || Channels[1] > 255.0f || Channels[2] < 0.0f || Channels[2] > 255.0f || Channels[3] < 0.0f || Channels[3] > 1.0f) {
                    throw std::runtime_error{"ImThemes color is out of range"};
                }

                for (Index_t Index{}; Index < ImGuiCol_COUNT; ++Index) {
                    if (Name == ImGui::GetStyleColorName(static_cast<ImGuiCol>(Index))) {
                        Style.Colors[Index] = ImVec4{Channels[0] / 255.0f, Channels[1] / 255.0f, Channels[2] / 255.0f, Channels[3]};

                        break;
                    }
                }
            }

            Style.Colors[ImGuiCol_TabSelectedOverline] = Style.Colors[ImGuiCol_TabSelected];
            Style.Colors[ImGuiCol_TabDimmedSelectedOverline] = Style.Colors[ImGuiCol_TabDimmedSelected];
            Style.Colors[ImGuiCol_DockingPreview] = Style.Colors[ImGuiCol_HeaderActive];
            Style.Colors[ImGuiCol_DockingPreview].w = 0.7f;
            Style.Colors[ImGuiCol_DockingEmptyBg] = Style.Colors[ImGuiCol_WindowBg];
        }
    }

    namespace {
        /**
         * @brief 8비트 명도와 알파로 ImGui 회색 색상을 만듭니다.
         * @param[in] Value 0~255 범위의 회색 명도입니다.
         * @param[in] Alpha 출력 색상의 알파 값입니다.
         * @return 정규화한 회색 RGBA 값입니다.
         */
        ImVec4 Gray(int Value, float Alpha = 1.0f) {
            return ImVec4{static_cast<float>(Value) / 255.0f, static_cast<float>(Value) / 255.0f, static_cast<float>(Value) / 255.0f, Alpha};
        }

        // 언리얼 에디터 5의 기본 색 구성(패널 #242424, 배경 #151515, 입력 #0F0F0F, 강조 #0070E0)을 ImGui 스타일에 대응시킵니다.

        /**
         * @brief 에디터 기본 테마의 간격·모서리·색상을 ImGui 스타일에 적용합니다.
         * @param[in,out] Style 변경할 ImGui 스타일입니다.
         */
        void ApplyUnrealStyle(ImGuiStyle& Style) {
            const ImVec4 Primary{0.0f, 112.0f / 255.0f, 224.0f / 255.0f, 1.0f};
            const ImVec4 Clear{0.0f, 0.0f, 0.0f, 0.0f};
            ImVec4* Colors{Style.Colors};

            Style.WindowPadding = ImVec2{6.0f, 6.0f};
            Style.FramePadding = ImVec2{6.0f, 3.0f};
            Style.ItemSpacing = ImVec2{6.0f, 4.0f};
            Style.ItemInnerSpacing = ImVec2{4.0f, 4.0f};
            Style.CellPadding = ImVec2{4.0f, 2.0f};
            Style.IndentSpacing = 16.0f;
            Style.ScrollbarSize = 12.0f;
            Style.GrabMinSize = 10.0f;
            Style.WindowRounding = 0.0f;
            Style.ChildRounding = 0.0f;
            Style.PopupRounding = 4.0f;
            Style.FrameRounding = 4.0f;
            Style.GrabRounding = 4.0f;
            Style.ScrollbarRounding = 6.0f;
            Style.TabRounding = 4.0f;
            Style.WindowBorderSize = 1.0f;
            Style.ChildBorderSize = 1.0f;
            Style.PopupBorderSize = 1.0f;
            Style.FrameBorderSize = 1.0f;
            Style.TabBorderSize = 0.0f;
            Style.TabBarBorderSize = 1.0f;
            Style.TabBarOverlineSize = 2.0f;
            Style.DockingSeparatorSize = 3.0f;
            Style.SeparatorTextBorderSize = 1.0f;
            Style.WindowMenuButtonPosition = ImGuiDir_None;
            Style.WindowTitleAlign = ImVec2{0.0f, 0.5f};

            Colors[ImGuiCol_Text] = Gray(192);
            Colors[ImGuiCol_TextDisabled] = Gray(110);
            Colors[ImGuiCol_WindowBg] = Gray(36);
            Colors[ImGuiCol_ChildBg] = Clear;
            Colors[ImGuiCol_PopupBg] = Gray(36);
            Colors[ImGuiCol_Border] = Gray(56);
            Colors[ImGuiCol_BorderShadow] = Clear;
            Colors[ImGuiCol_FrameBg] = Gray(15);
            Colors[ImGuiCol_FrameBgHovered] = Gray(26);
            Colors[ImGuiCol_FrameBgActive] = Gray(15);
            Colors[ImGuiCol_TitleBg] = Gray(21);
            Colors[ImGuiCol_TitleBgActive] = Gray(21);
            Colors[ImGuiCol_TitleBgCollapsed] = Gray(21);
            Colors[ImGuiCol_MenuBarBg] = Gray(21);
            Colors[ImGuiCol_ScrollbarBg] = Gray(26, 0.6f);
            Colors[ImGuiCol_ScrollbarGrab] = Gray(87);
            Colors[ImGuiCol_ScrollbarGrabHovered] = Gray(128);
            Colors[ImGuiCol_ScrollbarGrabActive] = Gray(128);
            Colors[ImGuiCol_CheckMark] = Primary;
            Colors[ImGuiCol_SliderGrab] = Gray(87);
            Colors[ImGuiCol_SliderGrabActive] = Primary;
            Colors[ImGuiCol_Button] = Gray(56);
            Colors[ImGuiCol_ButtonHovered] = Gray(87);
            Colors[ImGuiCol_ButtonActive] = Gray(42);
            Colors[ImGuiCol_Header] = Primary;
            Colors[ImGuiCol_HeaderHovered] = ImVec4{Primary.x, Primary.y, Primary.z, 0.75f};
            Colors[ImGuiCol_HeaderActive] = Primary;
            Colors[ImGuiCol_Separator] = Gray(21);
            Colors[ImGuiCol_SeparatorHovered] = Primary;
            Colors[ImGuiCol_SeparatorActive] = Primary;
            Colors[ImGuiCol_ResizeGrip] = Clear;
            Colors[ImGuiCol_ResizeGripHovered] = Gray(87);
            Colors[ImGuiCol_ResizeGripActive] = Primary;
            Colors[ImGuiCol_InputTextCursor] = Gray(255);
            Colors[ImGuiCol_Tab] = Gray(21);
            Colors[ImGuiCol_TabHovered] = Gray(47);
            Colors[ImGuiCol_TabSelected] = Gray(36);
            Colors[ImGuiCol_TabSelectedOverline] = Primary;
            Colors[ImGuiCol_TabDimmed] = Gray(21);
            Colors[ImGuiCol_TabDimmedSelected] = Gray(36);
            Colors[ImGuiCol_TabDimmedSelectedOverline] = Clear;
            Colors[ImGuiCol_DockingPreview] = ImVec4{Primary.x, Primary.y, Primary.z, 0.7f};
            Colors[ImGuiCol_DockingEmptyBg] = Gray(21);
            Colors[ImGuiCol_TableHeaderBg] = Gray(47);
            Colors[ImGuiCol_TableBorderStrong] = Gray(21);
            Colors[ImGuiCol_TableBorderLight] = Gray(26);
            Colors[ImGuiCol_TableRowBg] = Clear;
            Colors[ImGuiCol_TableRowBgAlt] = Gray(255, 0.025f);
            Colors[ImGuiCol_TextLink] = ImVec4{38.0f / 255.0f, 187.0f / 255.0f, 1.0f, 1.0f};
            Colors[ImGuiCol_TextSelectedBg] = ImVec4{Primary.x, Primary.y, Primary.z, 0.5f};
            Colors[ImGuiCol_DragDropTarget] = Primary;
            Colors[ImGuiCol_NavCursor] = Primary;
            Colors[ImGuiCol_NavWindowingHighlight] = Gray(255, 0.7f);
            Colors[ImGuiCol_NavWindowingDimBg] = Gray(0, 0.4f);
            Colors[ImGuiCol_ModalWindowDimBg] = Gray(0, 0.5f);
        }
    }

    /**
     * @brief 기본 설정으로 FImGuiThemeCatalog 객체를 초기화합니다.
     */
    Editor::FImGuiThemeCatalog::FImGuiThemeCatalog() {
        ImGuiStyle Style{};

        ImGui::StyleColorsDark(&Style);
        ApplyUnrealStyle(Style);

        mNames.push_back("Macaw Dark");
        mAuthors.push_back("Macaw");
        mDescriptions.push_back("Default editor theme");
        mStyles.push_back(Style);
    }

    /**
     * @brief ImThemes TOML의 단일 테마 또는 테마 배열을 검증한 뒤 카탈로그에 추가하거나 교체합니다.
     */
    bool Editor::FImGuiThemeCatalog::Load(const std::filesystem::path& Path, Core::FString& Error) {
        Error.clear();

        try {
            std::ifstream Input{Path, std::ios::binary};

            if (!Input) {
                throw std::runtime_error{"Cannot open the theme file"};
            }

            const toml::table Document{toml::parse(Input, Core::FilePath::ToUtf8(Path))};
            const toml::array* Themes{Document["themes"].as_array()};
            Core::TArray<const toml::table*> Tables{};

            if (Themes != nullptr) {
                for (const toml::node& Node : *Themes) {
                    Tables.push_back(Node.as_table());
                }
            } else {
                Tables.push_back(&Document);
            }

            if (Tables.empty()) {
                throw std::runtime_error{"The file contains no ImThemes themes"};
            }

            Editor::FImGuiThemeCatalog Candidate{*this};

            for (const toml::table* Table : Tables) {
                if (Table == nullptr) {
                    throw std::runtime_error{"Expected an ImThemes style table"};
                }

                const toml::table* StyleTable{Table->get_as<toml::table>("style")};

                if (StyleTable == nullptr && Table->contains("colors")) {
                    StyleTable = Table;
                }

                if (StyleTable == nullptr) {
                    throw std::runtime_error{"Expected an ImThemes style table"};
                }

                const Core::FString Name{(*Table)["name"].value_or(Core::FilePath::ToUtf8(Path.stem()))};

                if (Name.empty() || Name.find('\0') != Core::FString::npos || Name == "Macaw Dark") {
                    throw std::runtime_error{"Invalid or reserved theme name"};
                }

                ImGuiStyle Style{};

                ImGui::StyleColorsDark(&Style);
                ReadStyle(*StyleTable, Style);

                const auto Found{std::find(Candidate.mNames.begin(), Candidate.mNames.end(), Name)};
                const Index_t Index{static_cast<Index_t>(static_cast<size_t>(Found - Candidate.mNames.begin()))};

                if (Found == Candidate.mNames.end()) {
                    Candidate.mNames.push_back(Name);
                    Candidate.mAuthors.emplace_back();
                    Candidate.mDescriptions.emplace_back();
                    Candidate.mStyles.emplace_back();
                }

                Candidate.mAuthors[Index] = (*Table)["author"].value_or(Core::FString{});
                Candidate.mDescriptions[Index] = (*Table)["description"].value_or(Core::FString{});
                Candidate.mStyles[Index] = Style;
            }

            *this = std::move(Candidate);

            return true;
        } catch (const std::exception& Exception) {
            Error = "Cannot load ImThemes '" + Core::FilePath::ToUtf8(Path) + "': " + Exception.what();

            return false;
        }
    }

    /**
     * @brief 기본 테마를 포함한 테마 개수를 반환합니다.
     */
    Uint32 Editor::FImGuiThemeCatalog::GetCount() const {
        return static_cast<Uint32>(mNames.size());
    }

    /**
     * @brief 이름이 정확히 일치하는 테마 인덱스를 찾습니다.
     */
    Index_t Editor::FImGuiThemeCatalog::Find(const Core::FString& Name) const {
        const auto Found{std::find(mNames.begin(), mNames.end(), Name)};

        return Found == mNames.end() ? 0 : static_cast<Index_t>(Found - mNames.begin());
    }

    /**
     * @brief 인덱스에 해당하는 테마 이름을 반환합니다.
     */
    const Core::FString& Editor::FImGuiThemeCatalog::GetName(Index_t Index) const {
        return mNames.at(Index);
    }

    /**
     * @brief 인덱스에 해당하는 테마 작성자를 반환합니다.
     */
    const Core::FString& Editor::FImGuiThemeCatalog::GetAuthor(Index_t Index) const {
        return mAuthors.at(Index);
    }

    /**
     * @brief 인덱스에 해당하는 테마 설명을 반환합니다.
     */
    const Core::FString& Editor::FImGuiThemeCatalog::GetDescription(Index_t Index) const {
        return mDescriptions.at(Index);
    }

    /**
     * @brief 인덱스에 해당하는 배율 적용 전 테마 스타일을 반환합니다.
     */
    const ImGuiStyle& Editor::FImGuiThemeCatalog::GetStyle(Index_t Index) const {
        return mStyles.at(Index);
    }

}
