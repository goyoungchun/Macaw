/**
 * @file EditorStyle.h
 * @brief 에디터에서 공유하는 벡터 아이콘과 툴바 표시 함수를 선언합니다.
 * @ingroup Editor
 */

#pragma once

#include <imgui.h>
#include "Editor/EEditorIcon.h"

#include <filesystem>
#include <functional>

namespace Editor {

    /**
     * @brief 에디터 UI의 아이콘, 버튼과 툴바 스타일을 제공합니다.
     * @ingroup Editor
     * @note 함수는 현재 ImGui 컨텍스트와 창을 사용하며 뷰포트 명령이나 편집 상태를 소유하지 않습니다.
     */
    namespace EditorStyle {

        /**
         * @brief 최상위 탭 표시줄에서 브랜드 영역에 확보할 기준 너비입니다.
         */
        inline constexpr float BrandAreaWidth{72.0f};

        /**
         * @brief 에디터 주 메뉴 표시줄의 기준 높이입니다.
         */
        inline constexpr float MenuBarHeight{30.0f};

        /**
         * @brief 에디터 최상위 탭끼리 도킹하도록 제한하는 창 클래스 식별자입니다.
         */
        inline constexpr ImGuiID TopLevelTabClassId{0x4D414A52};

        /**
         * @brief 현재 글꼴과 배율에 맞는 최상위 탭 높이를 반환합니다.
         * @return 최상위 탭의 표시 높이입니다.
         */
        float GetTopLevelTabHeight();

        /**
         * @brief 현재 테마의 최상위 탭 배경색을 반환합니다.
         * @return 최상위 탭에 사용할 색상입니다.
         */
        ImVec4 GetTopLevelTabColor();

        /**
         * @brief 최상위 탭 표시줄에 필요한 ImGui 스타일을 스택에 넣습니다.
         */
        void PushTopLevelTabStyle();

        /**
         * @brief PushTopLevelTabStyle()이 넣은 ImGui 스타일을 복원합니다.
         */
        void PopTopLevelTabStyle();

        /**
         * @brief 최상위 탭 여부에 맞는 스타일을 적용해 에디터 창을 시작합니다.
         * @param[in] Name 표시 제목과 고유 ImGui 식별자를 담는 이름입니다.
         * @param[in] Open 창 표시 상태이며 nullptr이면 닫기 버튼을 제공하지 않습니다.
         * @param[in] Flags 적용할 ImGui 창 또는 선택 항목 플래그입니다.
         * @return 창 콘텐츠를 그릴 수 있으면 true이며 항상 ImGui::End()로 마쳐야 합니다.
         */
        bool BeginEditorWindow(const char* Name, bool* Open = nullptr, ImGuiWindowFlags Flags = 0);

        /**
         * @brief 명령 정의와 UI가 공유하는 아이콘 식별자의 스타일 네임스페이스 별칭입니다.
         */
        using EIcon = Editor::EEditorIcon;

        /**
         * @brief 선택 상태와 등장 효과를 적용한 에디터 버튼을 그립니다.
         * @param[in] Label 메뉴 또는 컨트롤에 표시할 레이블입니다.
         * @param[in] Size 항목의 표시 크기이며 0인 축은 ImGui 기본 크기를 사용합니다.
         * @param[in] Selected 항목이 선택된 상태인지 지정합니다.
         * @return 버튼을 누르면 true입니다.
         */
        bool Button(const char* Label, const ImVec2& Size = {}, bool Selected = false);

        /**
         * @brief 에디터 스타일의 작은 버튼을 그립니다.
         * @param[in] Label 메뉴 또는 컨트롤에 표시할 레이블입니다.
         * @return 버튼을 누르면 true입니다.
         */
        bool SmallButton(const char* Label);

        /**
         * @brief 선택·호버 배경 효과를 적용한 항목을 그립니다.
         * @param[in] Label 메뉴 또는 컨트롤에 표시할 레이블입니다.
         * @param[in] Selected 항목이 선택된 상태인지 지정합니다.
         * @param[in] Flags 적용할 ImGui 창 또는 선택 항목 플래그입니다.
         * @param[in] Size 항목의 표시 크기이며 0인 축은 ImGui 기본 크기를 사용합니다.
         * @return 항목을 누르면 true입니다.
         */
        bool Selectable(const char* Label, bool Selected = false, ImGuiSelectableFlags Flags = 0, const ImVec2& Size = {});

        /**
         * @brief 선택과 호버 상태에 맞는 애니메이션 배경을 지정한 사각형에 그립니다.
         * @param[in] Id 등록 또는 그리기 항목을 구분할 식별자입니다.
         * @param[in] Minimum 대상 사각형의 왼쪽 위 화면 좌표입니다.
         * @param[in] Maximum 대상 사각형의 오른쪽 아래 화면 좌표입니다.
         * @param[in] Selected 항목이 선택된 상태인지 지정합니다.
         * @param[in] Hovered 항목 위에 마우스가 있는지 지정합니다.
         */
        void SelectionBackground(ImGuiID Id, const ImVec2& Minimum, const ImVec2& Maximum, bool Selected, bool Hovered);

        /**
         * @brief 등장 효과를 적용한 에디터 팝업을 시작합니다.
         * @param[in] Id 등록 또는 그리기 항목을 구분할 식별자입니다.
         * @param[in] Flags 적용할 ImGui 창 또는 선택 항목 플래그입니다.
         * @return 팝업이 열렸으면 true이며 이때 EndPopup()을 호출해야 합니다.
         */
        bool BeginPopup(const char* Id, ImGuiWindowFlags Flags = 0);

        /**
         * @brief BeginPopup()에서 시작한 팝업과 스타일 범위를 종료합니다.
         */
        void EndPopup();

        /**
         * @brief 현재 ImGui 스타일 스택에 뷰포트 툴바의 간격, 모서리와 버튼 색상을 적용합니다.
         * @note 같은 컨텍스트에서 PopViewportToolbar()와 짝지어 호출해야 합니다.
         */
        void PushViewportToolbar();

        /**
         * @brief PushViewportToolbar()로 적용한 스타일을 복원합니다.
         * @pre 대응하는 PushViewportToolbar() 호출이 있어야 합니다.
         */
        void PopViewportToolbar();

        /**
         * @brief 현재 창의 드로 목록에 지정한 벡터 아이콘을 그립니다.
         * @param[in] Icon 표시할 공통 아이콘입니다.
         * @param[in] Origin 아이콘 정사각형의 화면 기준 왼쪽 위 픽셀 좌표입니다.
         * @param[in] Size 아이콘 정사각형의 한 변 길이이며 양수여야 합니다.
         * @param[in] Color 알파를 포함하는 ImGui 형식의 패킹된 표시 색상입니다.
         * @pre 현재 ImGui 프레임과 창이 유효해야 합니다.
         */
        void DrawIcon(EIcon Icon, const ImVec2& Origin, float Size, ImU32 Color);

        /**
         * @brief 지정한 드로 목록에 벡터 아이콘을 그립니다.
         * @param[in,out] DrawList 아이콘을 추가할 드로 목록입니다.
         * @param[in] Icon 그릴 아이콘입니다.
         * @param[in] Origin 아이콘 왼쪽 위의 화면 좌표입니다.
         * @param[in] Size 아이콘 한 변의 픽셀 크기입니다.
         * @param[in] Color 아이콘 색상입니다.
         */
        void DrawIcon(ImDrawList* DrawList, EIcon Icon, const ImVec2& Origin, float Size, ImU32 Color);

        /**
         * @brief 탭 아이콘이 들어갈 자리를 비우기 위해 창 제목 앞에 붙이는 공백입니다.
         */
        inline constexpr const char* TabIconPadding{"      "};

        /**
         * @brief 방금 Begin()한 창이 도킹되어 있으면 그 탭의 왼쪽에 아이콘을 그립니다.
         * @param[in] Icon 탭에 그릴 아이콘입니다.
         * @param[in] Color 아이콘 색상이며 0이면 텍스트 색상을 사용합니다.
         * @note 창 제목은 TabIconPadding으로 시작해야 아이콘과 글자가 겹치지 않습니다.
         */
        void TabIcon(EIcon Icon, ImU32 Color = 0);

        /**
         * @brief 아이콘 버튼의 선택 상태를 표시하는 방식입니다.
         */
        enum class EIconSelectionStyle {
            /**
             * @brief 버튼 배경을 선택 색상으로 채웁니다.
             */
            Background,
            /**
             * @brief 배경 없이 아이콘을 체크 표시 색상으로 그립니다.
             */
            Icon
        };

        /**
         * @brief 선택 및 비활성 상태를 반영하는 아이콘 버튼과 툴팁을 표시합니다.
         * @param[in] Label 현재 ID 범위에서 버튼을 식별하며 기본 툴팁에도 사용할 문자열입니다.
         * @param[in] Icon 버튼에 표시할 아이콘입니다.
         * @param[in] Selected 선택 색상을 적용할지 지정하며 외부 상태를 변경하지 않습니다.
         * @param[in] Enabled false이면 버튼 클릭을 차단하며 툴팁은 계속 표시할 수 있습니다.
         * @param[in] Dropdown true이면 드롭다운 화살표와 그 공간을 추가합니다.
         * @param[in] Tooltip 대체 툴팁이며 nullptr이면 Label을 사용합니다.
         * @return 활성 버튼을 눌렀으면 true이며 비활성 상태에서는 false입니다.
         * @note 팝업 열기와 명령 실행은 호출자가 처리합니다. 문자열은 호출 중 유효해야 합니다.
         * @param[in] MinimumWidth 버튼에 확보할 최소 너비입니다.
         * @param[in] SelectionStyle Selected가 true일 때 배경과 아이콘 중 어느 쪽으로 선택 상태를 표시할지 지정합니다.
         */
        bool IconButton(const char* Label, EIcon Icon, bool Selected = false, bool Enabled = true, bool Dropdown = false, const char* Tooltip = nullptr, float MinimumWidth = 0.0f, EIconSelectionStyle SelectionStyle = EIconSelectionStyle::Background);

        /**
         * @brief 분할 아이콘 버튼에서 이번 프레임에 눌린 영역입니다.
         */
        enum class ESplitIconPress {
            /**
             * @brief 어느 영역도 눌리지 않았습니다.
             */
            None,
            /**
             * @brief 아이콘 동작 영역이 눌렸습니다.
             */
            Action,
            /**
             * @brief 메뉴 영역이 눌렸습니다.
             */
            Menu
        };

        /**
         * @brief 분할 아이콘 버튼의 메뉴 표시와 동작 상태를 지정합니다.
         */
        struct FSplitIconButtonOptions {
            /**
             * @brief 메뉴 영역에 표시할 현재 값 문자열이며 nullptr이면 더 보기 아이콘을 그립니다.
             * @note 값 문자열이 있으면 개별 영역 배경 대신 버튼 전체에 마우스 오버 배경을 그립니다.
             */
            const char* mMenuLabel{};
            /**
             * @brief false이면 동작 영역을 비활성으로 표시하며 메뉴 영역은 계속 누를 수 있습니다.
             */
            bool mActionEnabled{true};
            /**
             * @brief true이면 메뉴 영역을 왼쪽, 동작 영역을 오른쪽에 배치합니다.
             */
            bool mMenuFirst{};
            /**
             * @brief true이면 동작 아이콘을 체크 표시 색상으로 그려 켜진 상태를 나타냅니다.
             */
            bool mActionChecked{};
        };

        /**
         * @brief 분할 아이콘 버튼이 차지할 전체 너비를 계산합니다.
         * @param[in] MenuLabel 메뉴 영역에 표시할 값 문자열이며 nullptr이면 더 보기 아이콘 너비를 사용합니다.
         * @return 동작 영역과 메뉴 영역을 합친 픽셀 너비입니다.
         * @pre 현재 ImGui 프레임이 유효해야 합니다.
         */
        float GetSplitIconButtonWidth(const char* MenuLabel = nullptr);

        /**
         * @brief 아이콘 동작 영역과 메뉴 영역이 붙은 분할 버튼과 영역별 툴팁을 표시합니다.
         * @param[in] Label 현재 ID 범위에서 버튼을 식별할 문자열입니다.
         * @param[in] Icon 동작 영역에 표시할 아이콘입니다.
         * @param[in] ActionTooltip 동작 영역에 마우스를 올렸을 때 보여 줄 설명입니다.
         * @param[in] MenuTooltip 메뉴 영역에 마우스를 올렸을 때 보여 줄 설명입니다.
         * @param[in] Options 메뉴 표시 문자열, 배치 순서와 동작 상태입니다.
         * @return 이번 프레임에 눌린 영역이며 누르지 않았으면 ESplitIconPress::None입니다.
         * @note 메뉴 팝업 열기와 동작 실행은 호출자가 처리합니다. 문자열은 호출 중 유효해야 합니다.
         */
        ESplitIconPress SplitIconButton(const char* Label, EIcon Icon, const char* ActionTooltip, const char* MenuTooltip, const FSplitIconButtonOptions& Options = {});

        /**
         * @brief 선택적인 아이콘과 드롭다운 표시를 붙인 텍스트 버튼을 표시합니다.
         * @param[in] Label 버튼에 표시할 문자열이며 ## 뒤는 식별자로만 사용합니다.
         * @param[in] Icon 텍스트 앞에 그릴 아이콘이며 없으면 nullptr입니다.
         * @param[in] Dropdown true이면 오른쪽에 드롭다운 삼각형을 그립니다.
         * @param[in] Enabled false이면 비활성 상태로 표시합니다.
         * @param[in] Tooltip 마우스를 올렸을 때 보여 줄 설명이며 없으면 nullptr입니다.
         * @param[in] HoverOnly true이면 평소에는 배경 없이 그리고 마우스를 올렸을 때만 배경을 채웁니다.
         * @return 버튼을 눌렀으면 true입니다.
         */
        bool LabelButton(const char* Label, const EIcon* Icon = nullptr, bool Dropdown = false, bool Enabled = true, const char* Tooltip = nullptr, bool HoverOnly = false);

        /**
         * @brief LabelButton이 같은 인수로 차지할 너비를 계산합니다.
         * @param[in] Label 버튼에 표시할 문자열이며 ## 뒤는 너비 계산에서 제외합니다.
         * @param[in] Icon 텍스트 앞에 그릴 아이콘이며 없으면 nullptr입니다.
         * @param[in] Dropdown true이면 드롭다운 삼각형 공간을 포함합니다.
         * @return 프레임 여백을 포함한 픽셀 너비입니다.
         * @pre 현재 ImGui 프레임이 유효해야 합니다.
         */
        float GetLabelButtonWidth(const char* Label, const EIcon* Icon = nullptr, bool Dropdown = false);

        void DrawFieldLabel(const char* Label, float LabelWidth = -1.0f, float FieldWidth = -1.0f);

        /**
         * @brief 에셋 에디터 툴바의 찾아보기 버튼이 콘텐츠 브라우저에서 에셋을 선택할 때 호출할 처리기를 지정합니다.
         * @param[in] Handler 에셋 경로를 받아 콘텐츠 브라우저를 이동시키는 함수이며 비우면 버튼을 비활성화합니다.
         */
        void SetAssetBrowseHandler(std::function<void(const std::filesystem::path&)> Handler);

        /**
         * @brief 등록한 찾아보기 처리기에 에셋 경로를 전달해 콘텐츠 브라우저에서 선택합니다.
         * @param[in] AssetPath 콘텐츠 브라우저에서 선택할 에셋 경로입니다.
         */
        void BrowseAsset(const std::filesystem::path& AssetPath);

        /**
         * @brief 메뉴의 아이콘 열을 확보하고 선택 및 비활성 상태를 반영한 항목을 표시합니다.
         * @param[in] Label 표시할 메뉴 항목 이름이며 호출 중 유효해야 합니다.
         * @param[in] Icon 항목 왼쪽에 표시할 아이콘입니다.
         * @param[in] Selected 현재 선택을 나타내는 체크 표시 여부입니다.
         * @param[in] Enabled false이면 항목 선택을 차단합니다.
         * @return 활성 항목을 선택했으면 true이며 비활성 상태에서는 false입니다.
         * @pre 현재 ImGui 메뉴 또는 팝업 안에서 호출해야 합니다.
         */
        bool IconMenuItem(const char* Label, EIcon Icon, bool Selected, bool Enabled = true);
    }

}
