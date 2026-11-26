/**
 * @file FSplashCanvas.h
 * @brief 스플래시 창의 히어로 배경, 로고, 하단 상태 글자를 DIB 프레임에 그립니다.
 * @ingroup Platform
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Types.h"

#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace Platform {

    /**
     * @brief 스플래시 스레드가 소유하는 DIB 프레임과 글꼴, 미리 합성한 배경입니다.
     * @details 언리얼 에디터 스플래시처럼 창 전체를 덮는 배경 그림 위 하단 띠에 제목·부제·상태 줄을,
     *          오른쪽 아래에 로고와 저작권 줄을 둡니다. 진행 막대는 없고 진행률은 상태 줄에 백분율로 붙습니다.
     * @ingroup Platform
     */
    class FSplashCanvas {
    public:
        /**
         * @brief 0~1 범위의 선형 RGB 색입니다.
         */
        using FColor = Core::TStaticArray<float, 3>;

        /**
         * @brief 미리 곱한 알파의 RGBA 실수 픽셀로 읽은 정사각형 아이콘입니다.
         */
        struct FIconImage {
            /**
             * @brief 한 변의 픽셀 수입니다.
             */
            Int32 Size{};

            /**
             * @brief 행 우선 RGBA 픽셀입니다.
             */
            std::vector<float> Pixels{};
        };

    public:
        /**
         * @brief 자원이 없는 빈 캔버스를 구성합니다.
         */
        FSplashCanvas();

        /**
         * @brief DIB와 글꼴을 해제합니다.
         */
        ~FSplashCanvas();

        /**
         * @brief GDI 핸들을 소유하므로 복사를 금지합니다.
         */
        FSplashCanvas(const Platform::FSplashCanvas&) = delete;

        /**
         * @brief GDI 핸들을 소유하므로 대입을 금지합니다.
         * @return 갱신된 자기 자신(*this)의 참조입니다.
         */
        Platform::FSplashCanvas& operator=(const Platform::FSplashCanvas&) = delete;

    public:
        /**
         * @brief 프레임 DIB와 글꼴을 만들고 고정 배경을 합성합니다.
         * @param[in] Width 프레임의 가로 픽셀 수입니다.
         * @param[in] Height 프레임의 세로 픽셀 수입니다.
         * @param[in] Scale 96 DPI 기준 화면 배율입니다.
         * @param[in] ProjectName 제목 줄에 표시할 프로젝트 이름입니다.
         * @param[in] BackgroundImage 기본 배경 대신 쓸 프로젝트 스플래시 이미지입니다. 비었거나 읽지 못하면 기본 배경을 그립니다.
         * @return 자원을 모두 만들면 true입니다.
         * @note 이미지를 WIC로 읽으므로 호출 스레드에서 COM이 초기화되어 있어야 합니다.
         */
        bool Initialize(Int32 Width, Int32 Height, float Scale, const std::wstring& ProjectName, const std::filesystem::path& BackgroundImage);

        /**
         * @brief 배경 위에 상태 줄을 그려 대상 DC로 복사합니다.
         * @param[in] Target 프레임을 복사할 창 DC입니다.
         * @param[in] Stage 현재 로딩 단계 이름입니다.
         * @param[in] Progress 상태 줄 끝에 붙일 진행률이며 0~1 범위입니다.
         */
        void Draw(HDC Target, const std::wstring& Stage, float Progress);

    private:
        /**
         * @brief 논리 픽셀을 현재 배율의 물리 픽셀로 바꿉니다.
         * @param[in] Logical 96 DPI 기준 길이입니다.
         * @return 반올림한 물리 픽셀 길이입니다.
         */
        Int32 Pixels(float Logical) const;

        /**
         * @brief 실행 파일에 포함된 Pretendard 글꼴을 이 프로세스 전용으로 등록합니다.
         * @return 세 굵기를 모두 등록하면 true입니다.
         */
        bool LoadFonts();

        /**
         * @brief 현재 배율의 Pretendard 글꼴을 만들고, 등록하지 못했으면 Segoe UI로 대신합니다.
         * @param[in] Size 96 DPI 기준 글자 높이입니다.
         * @param[in] Weight FW_NORMAL, FW_SEMIBOLD, FW_BOLD 중 하나인 GDI 글꼴 두께입니다.
         * @return 호출자가 해제해야 하는 글꼴 핸들입니다.
         */
        HFONT MakeFont(float Size, Int32 Weight) const;

        /**
         * @brief 현재 선택된 글꼴로 한 줄 문자열을 그립니다.
         * @param[in] Text 그릴 문자열입니다.
         * @param[in] Bounds 문자열을 배치할 영역입니다.
         * @param[in] Color 0xRRGGBB 형식의 글자색입니다.
         * @param[in] Flags 추가 DrawText 플래그입니다.
         */
        void DrawLine(const std::wstring& Text, RECT Bounds, Uint32 Color, UINT Flags);

        /**
         * @brief 실행 파일의 Macaw 아이콘을 지정 크기로 읽습니다.
         * @param[in] Size 읽을 아이콘 크기입니다.
         * @param[out] Image 읽은 아이콘을 받을 이미지입니다.
         * @return 읽기에 성공하면 true입니다.
         */
        static bool LoadMacawIcon(Int32 Size, FIconImage& Image);

        /**
         * @brief 아이콘을 쌍선형으로 표본화합니다.
         * @param[in] Image 표본화할 아이콘입니다.
         * @param[in] U 가로 정규화 좌표입니다.
         * @param[in] V 세로 정규화 좌표입니다.
         * @return 미리 곱한 알파의 RGBA 값입니다.
         */
        static Core::TStaticArray<float, 4> SampleIcon(const FIconImage& Image, float U, float V);

        /**
         * @brief 아이콘을 프레임 버퍼의 정사각형 영역에 알파 합성합니다.
         * @param[in] Image 합성할 아이콘입니다.
         * @param[in] Left 영역 왼쪽 물리 픽셀입니다.
         * @param[in] Top 영역 위쪽 물리 픽셀입니다.
         * @param[in] Size 영역 한 변의 물리 픽셀 수입니다.
         */
        void BlendIcon(const FIconImage& Image, float Left, float Top, float Size);

        /**
         * @brief 이미지 파일을 프레임 비율에 맞게 가운데를 잘라 프레임 크기로 확대·축소해 읽습니다.
         * @param[in] File 읽을 PNG·JPEG·BMP 파일입니다.
         * @param[out] Pixels 프레임 크기의 0xRRGGBB 픽셀을 받을 버퍼입니다.
         * @return 읽기에 성공하면 true입니다.
         */
        bool LoadBackgroundImage(const std::filesystem::path& File, std::vector<Uint32>& Pixels) const;

        /**
         * @brief 배경 그림, 하단 띠와 고정 글자를 배경 버퍼에 합성합니다.
         * @details 프로젝트 이미지가 없으면 황혼 하늘, 능선, 가운데 Macaw 아이콘을 직접 그립니다.
         * @param[in] ProjectName 제목 줄에 표시할 프로젝트 이름입니다.
         * @param[in] BackgroundImage 기본 배경 대신 쓸 프로젝트 스플래시 이미지입니다.
         */
        void ComposeBackground(const std::wstring& ProjectName, const std::filesystem::path& BackgroundImage);

    private:
        /**
         * @brief 프레임 가로 픽셀 수입니다.
         */
        Int32 mWidth{};

        /**
         * @brief 프레임 세로 픽셀 수입니다.
         */
        Int32 mHeight{};

        /**
         * @brief 96 DPI 기준 화면 배율입니다.
         */
        float mScale{1.0f};

        /**
         * @brief 프레임 DIB를 선택한 메모리 DC입니다.
         */
        HDC mDC{};

        /**
         * @brief 32비트 프레임 DIB입니다.
         */
        HBITMAP mBitmap{};

        /**
         * @brief 메모리 DC에 원래 선택되어 있던 비트맵입니다.
         */
        HGDIOBJ mOldBitmap{};

        /**
         * @brief 프레임 DIB의 0xRRGGBB 픽셀 주소입니다.
         */
        Uint32* mBits{};

        /**
         * @brief 매 프레임 복사해 쓰는 고정 배경 픽셀입니다.
         */
        std::vector<Uint32> mBackground{};

        /**
         * @brief "Macaw Editor - 프로젝트" 제목 글꼴입니다.
         */
        HFONT mTitleFont{};

        /**
         * @brief 제목 아래 에디터 이름 글꼴입니다.
         */
        HFONT mSubtitleFont{};

        /**
         * @brief 상태 줄과 저작권 줄 글꼴입니다.
         */
        HFONT mStatusFont{};

        /**
         * @brief AddFontMemResourceEx로 등록한 Pretendard 글꼴 핸들입니다.
         */
        std::vector<HANDLE> mFontResources{};

        /**
         * @brief Pretendard 세 굵기를 모두 등록했는지 여부입니다.
         */
        bool mHasPretendard{};
    };

}
