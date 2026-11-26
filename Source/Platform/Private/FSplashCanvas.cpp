/**
 * @file FSplashCanvas.cpp
 * @brief 스플래시의 픽셀 배경·브랜드 이미지·텍스트 합성을 구현합니다.
 * @ingroup Platform
 */

#include "Core/Types.h"
#include "Core/Literals/AngleFunctions.h"
#include "FSplashCanvas.h"
#include "Platform/WindowResources.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using namespace Core::Literals;

#pragma comment(lib, "windowscodecs.lib")

namespace Platform {

    /**
     * @brief FSplashCanvas 구현에 한정되는 내부 형식·상수·도우미 함수입니다.
     */
    namespace {
        /**
         * @brief 스플래시 캔버스에서 공유하는 정규화 RGB 색상 형식입니다.
         */
        using FColor = Platform::FSplashCanvas::FColor;

        /**
         * @brief 스플래시 브랜드 구성에 사용하는 빨간색입니다.
         */
        constexpr FColor MacawRed{0.91f, 0.21f, 0.17f};

        /**
         * @brief 스플래시 브랜드 구성에 사용하는 파란색입니다.
         */
        constexpr FColor MacawBlue{0.18f, 0.55f, 0.88f};

        // 레이아웃은 96 DPI 논리 픽셀 기준이며 언리얼 에디터 스플래시의 하단 띠 배치를 따릅니다.

        /**
         * @brief 스플래시 하단 정보 영역의 기준 여백입니다.
         */
        constexpr float Margin{12.0f};

        /**
         * @brief 스플래시 하단 기준 제목 문자열의 세로 오프셋입니다.
         */
        constexpr float TitleTop{70.0f};

        /**
         * @brief 스플래시 하단 기준 부제 문자열의 세로 오프셋입니다.
         */
        constexpr float SubtitleTop{45.0f};

        /**
         * @brief 스플래시 하단 기준 상태 문자열의 세로 오프셋입니다.
         */
        constexpr float StatusTop{26.0f};

        /**
         * @brief 스플래시 하단 로고의 기준 크기입니다.
         */
        constexpr float LogoSize{40.0f};

        /**
         * @brief 스플래시 주 이미지의 기준 크기입니다.
         */
        constexpr float HeroSize{184.0f};

        /**
         * @brief 스플래시 주 이미지의 중심 Y 좌표이며 96 DPI 논리 픽셀 기준입니다.
         */
        constexpr float HeroCenterY{148.0f};

        /**
         * @brief 스플래시 배경 지평선의 기준 Y 좌표입니다.
         */
        constexpr float HorizonY{228.0f};

        /**
         * @brief 두 RGB 색상을 주어진 비율로 선형 보간합니다.
         * @param[in] A 첫 번째 입력 색상입니다.
         * @param[in] B 두 번째 입력 색상입니다.
         * @param[in] T 두 색상의 선형 보간 비율입니다.
         * @return 보간한 RGB 색상입니다.
         */
        FColor Lerp(const FColor& A, const FColor& B, float T) {
            return {A[0] + (B[0] - A[0]) * T, A[1] + (B[1] - A[1]) * T, A[2] + (B[2] - A[2]) * T};
        }

        /**
         * @brief 두 번째 RGB 색상에 배율을 곱해 첫 번째 색상에 더합니다.
         * @param[in] A 첫 번째 입력 색상입니다.
         * @param[in] B 두 번째 입력 색상입니다.
         * @param[in] Scale 색상에 곱할 배율입니다.
         * @return 가산한 RGB 색상입니다.
         */
        FColor Add(const FColor& A, const FColor& B, float Scale) {
            return {A[0] + B[0] * Scale, A[1] + B[1] * Scale, A[2] + B[2] * Scale};
        }

        /**
         * @brief RGB 색상의 모든 채널에 같은 배율을 곱합니다.
         * @param[in] A 첫 번째 입력 색상입니다.
         * @param[in] Scale 색상에 곱할 배율입니다.
         * @return 배율을 적용한 RGB 색상입니다.
         */
        FColor Multiply(const FColor& A, float Scale) {
            return {A[0] * Scale, A[1] * Scale, A[2] * Scale};
        }

        /**
         * @brief 두 경계 사이의 값을 0~1 범위의 부드러운 전이로 변환합니다.
         * @param[in] Edge0 전이 구간의 시작 경계입니다.
         * @param[in] Edge1 전이 구간의 끝 경계입니다.
         * @param[in] Value 전이 구간에서 정규화할 입력 값입니다.
         * @return 구간 밖은 0 또는 1로 제한한 보간 값입니다.
         */
        float SmoothStep(float Edge0, float Edge1, float Value) {
            const float T{std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f)};

            return T * T * (3.0f - 2.0f * T);
        }

        /**
         * @brief 정규화 RGB에 잡음을 더하고 8비트 채널의 픽셀 값으로 변환합니다.
         * @param[in] Color 그리기 또는 픽셀 변환에 사용할 RGB 색상입니다.
         * @param[in] Noise RGB 채널에 더할 정규화 잡음입니다.
         * @return 0x00RRGGBB 형식의 픽셀입니다.
         */
        Uint32 Pack(const FColor& Color, float Noise = 0.0f) {
            const auto Channel{[Noise](float Value) {
                return static_cast<Uint32>(std::clamp(Value + Noise, 0.0f, 1.0f) * 255.0f + 0.5f);
            }};

            return (Channel(Color[0]) << 16) | (Channel(Color[1]) << 8) | Channel(Color[2]);
        }

        /**
         * @brief 8비트 RGB 픽셀을 정규화 색상으로 변환합니다.
         * @param[in] Pixel 해석할 0x00RRGGBB 픽셀입니다.
         * @return 각 채널이 0~1 범위인 RGB 값입니다.
         */
        FColor Unpack(Uint32 Pixel) {
            return {static_cast<float>((Pixel >> 16) & 0xFF) / 255.0f, static_cast<float>((Pixel >> 8) & 0xFF) / 255.0f, static_cast<float>(Pixel & 0xFF) / 255.0f};
        }

        /**
         * @brief 중심으로부터 거리와 반지름으로 원형 광원의 감쇠를 계산합니다.
         * @param[in] X 픽셀 또는 논리 좌표의 X 값입니다.
         * @param[in] Y 픽셀 또는 논리 좌표의 Y 값입니다.
         * @param[in] CenterX 광원 중심의 X 좌표입니다.
         * @param[in] CenterY 광원 중심의 Y 좌표입니다.
         * @param[in] Radius 광원의 영향 반지름입니다.
         * @return 범위 밖은 0인 제곱 감쇠 값입니다.
         */
        float Glow(float X, float Y, float CenterX, float CenterY, float Radius) {
            const float Falloff{std::max(0.0f, 1.0f - std::hypot(X - CenterX, Y - CenterY) / Radius)};

            return Falloff * Falloff;
        }

        /**
         * @brief 픽셀 좌표로부터 결정적인 의사 난수 해시를 계산합니다.
         * @param[in] X 픽셀 또는 논리 좌표의 X 값입니다.
         * @param[in] Y 픽셀 또는 논리 좌표의 Y 값입니다.
         * @return 좌표에 대응하는 32비트 해시 값입니다.
         */
        Uint32 Hash(Int32 X, Int32 Y) {
            Uint32 Value{static_cast<Uint32>(X) * 73856093u ^ static_cast<Uint32>(Y) * 19349663u};

            Value = (Value ^ (Value >> 13)) * 1274126177u;

            return Value ^ (Value >> 16);
        }

        // 8비트 그라데이션의 띠를 감추는 ±0.5 LSB 크기의 고정 잡음입니다.

        /**
         * @brief 그라데이션 밴딩을 줄일 픽셀별 고정 잡음을 계산합니다.
         * @param[in] X 픽셀 또는 논리 좌표의 X 값입니다.
         * @param[in] Y 픽셀 또는 논리 좌표의 Y 값입니다.
         * @return 8비트 채널 기준 약 ±0.5 LSB의 잡음입니다.
         */
        float Dither(Int32 X, Int32 Y) {
            return (static_cast<float>((Hash(X, Y) >> 8) & 0xFF) / 255.0f - 0.5f) / 255.0f;
        }

        // 먼 능선과 가까운 능선의 윗면 높이를 논리 픽셀 좌표로 돌려줍니다.

        /**
         * @brief 스플래시 배경의 먼 산 능선 높이를 계산합니다.
         * @param[in] X 픽셀 또는 논리 좌표의 X 값입니다.
         * @return 논리 픽셀 기준 능선 Y 좌표입니다.
         */
        float FarRidge(float X) {
            return HorizonY + 6.0f - 16.0f * Core::Math::Sin(X * 0.011_rad + 1.3_rad) - 8.0f * Core::Math::Sin(X * 0.029_rad + 0.4_rad) - 3.0f * Core::Math::Sin(X * 0.083_rad + 2.2_rad);
        }

        /**
         * @brief 스플래시 배경의 가까운 산 능선 높이를 계산합니다.
         * @param[in] X 픽셀 또는 논리 좌표의 X 값입니다.
         * @return 논리 픽셀 기준 능선 Y 좌표입니다.
         */
        float NearRidge(float X) {
            return HorizonY + 40.0f - 24.0f * Core::Math::Sin(X * 0.0072_rad + 2.1_rad) - 11.0f * Core::Math::Sin(X * 0.024_rad + 0.7_rad) - 5.0f * Core::Math::Sin(X * 0.061_rad + 1.9_rad) - 2.0f * Core::Math::Sin(X * 0.19_rad);
        }

        /**
         * @brief 상단 행부터 저장하는 32비트 RGB DIB 형식을 만듭니다.
         * @param[in] Width 대상 영역의 너비이며 단위는 픽셀입니다.
         * @param[in] Height 대상 영역의 높이이며 단위는 픽셀입니다.
         * @return 지정한 크기의 BITMAPINFO입니다.
         */
        BITMAPINFO MakeFormat(Int32 Width, Int32 Height) {
            BITMAPINFO Format{};
            Format.bmiHeader.biSize = sizeof(Format.bmiHeader);
            Format.bmiHeader.biWidth = Width;
            Format.bmiHeader.biHeight = -Height;
            Format.bmiHeader.biPlanes = 1;
            Format.bmiHeader.biBitCount = 32;
            Format.bmiHeader.biCompression = BI_RGB;

            return Format;
        }
    }

    Platform::FSplashCanvas::FSplashCanvas() = default;

    Platform::FSplashCanvas::~FSplashCanvas() {
        if (mDC != nullptr) {
            SelectObject(mDC, mOldBitmap);
            DeleteDC(mDC);
        }

        if (mBitmap != nullptr) {
            DeleteObject(mBitmap);
        }

        for (HFONT Font : {mTitleFont, mSubtitleFont, mStatusFont}) {
            if (Font != nullptr) {
                DeleteObject(Font);
            }
        }

        for (HANDLE Resource : mFontResources) {
            RemoveFontMemResourceEx(Resource);
        }
    }

    bool Platform::FSplashCanvas::Initialize(Int32 Width, Int32 Height, float Scale, const std::wstring& ProjectName, const std::filesystem::path& BackgroundImage) {
        mWidth = Width;
        mHeight = Height;
        mScale = Scale;

        const BITMAPINFO Format{MakeFormat(Width, Height)};
        void* Bits{};

        mDC = CreateCompatibleDC(nullptr);
        mBitmap = mDC != nullptr ? CreateDIBSection(mDC, &Format, DIB_RGB_COLORS, &Bits, nullptr, 0) : nullptr;

        if (mBitmap == nullptr) {
            return false;
        }

        mBits = static_cast<Uint32*>(Bits);
        mOldBitmap = SelectObject(mDC, mBitmap);

        SetBkMode(mDC, TRANSPARENT);

        mHasPretendard = LoadFonts();
        mTitleFont = MakeFont(20.0f, FW_SEMIBOLD);
        mSubtitleFont = MakeFont(12.5f, FW_NORMAL);
        mStatusFont = MakeFont(12.0f, FW_NORMAL);

        ComposeBackground(ProjectName, BackgroundImage);

        return true;
    }

    void Platform::FSplashCanvas::Draw(HDC Target, const std::wstring& Stage, float Progress) {
        GdiFlush();
        std::copy(mBackground.begin(), mBackground.end(), mBits);

        const std::wstring Status{Stage + L" (" + std::to_wstring(static_cast<Int32>(std::clamp(Progress, 0.0f, 1.0f) * 100.0f + 0.5f)) + L"%)"};
        const Int32 Left{Pixels(Margin)};
        const Int32 Top{mHeight - Pixels(StatusTop)};

        SelectObject(mDC, mStatusFont);
        DrawLine(Status, {Left, Top, mWidth / 2, Top + Pixels(16.0f)}, 0x9C9C9C, DT_END_ELLIPSIS);

        GdiFlush();
        BitBlt(Target, 0, 0, mWidth, mHeight, mDC, 0, 0, SRCCOPY);
    }

    Int32 Platform::FSplashCanvas::Pixels(float Logical) const {
        return static_cast<Int32>(std::lround(Logical * mScale));
    }

    bool Platform::FSplashCanvas::LoadFonts() {
        const HMODULE Module{GetModuleHandleW(nullptr)};

        for (Int32 Identifier : {MacawSplashRegularFontResource, MacawSplashSemiBoldFontResource, MacawSplashBoldFontResource}) {
            const HRSRC Resource{FindResourceW(Module, MAKEINTRESOURCEW(Identifier), RT_RCDATA)};
            const HGLOBAL Data{Resource != nullptr ? LoadResource(Module, Resource) : nullptr};
            void* Bytes{Data != nullptr ? LockResource(Data) : nullptr};
            DWORD Count{};
            const HANDLE Font{Bytes != nullptr ? AddFontMemResourceEx(Bytes, SizeofResource(Module, Resource), nullptr, &Count) : nullptr};

            if (Font == nullptr) {
                return false;
            }

            mFontResources.push_back(Font);
        }

        return true;
    }

    HFONT Platform::FSplashCanvas::MakeFont(float Size, Int32 Weight) const {
        // 정적 Pretendard SemiBold는 GDI에서 별도 패밀리로 등록되므로 이름으로 고릅니다.
        const bool SemiBold{mHasPretendard && Weight == FW_SEMIBOLD};
        const wchar_t* Face{!mHasPretendard ? L"Segoe UI" : (SemiBold ? L"Pretendard SemiBold" : L"Pretendard")};

        return CreateFontW(-Pixels(Size), 0, 0, 0, SemiBold ? FW_NORMAL : Weight, false, false, false, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, Face);
    }

    void Platform::FSplashCanvas::DrawLine(const std::wstring& Text, RECT Bounds, Uint32 Color, UINT Flags) {
        SetTextColor(mDC, RGB((Color >> 16) & 0xFF, (Color >> 8) & 0xFF, Color & 0xFF));
        DrawTextW(mDC, Text.c_str(), static_cast<Int32>(Text.size()), &Bounds, Flags | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
    }

    bool Platform::FSplashCanvas::LoadMacawIcon(Int32 Size, FIconImage& Image) {
        const HICON Icon{static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(MacawIconResource), IMAGE_ICON, Size, Size, LR_DEFAULTCOLOR))};

        if (Icon == nullptr) {
            return false;
        }

        ICONINFO Info{};
        std::vector<Uint32> Raw{};
        Raw.resize(static_cast<std::size_t>(Size * Size));
        bool Loaded{};

        if (GetIconInfo(Icon, &Info)) {
            BITMAPINFO Format{MakeFormat(Size, Size)};
            const HDC Screen{GetDC(nullptr)};

            Loaded = Info.hbmColor != nullptr && GetDIBits(Screen, Info.hbmColor, 0, static_cast<UINT>(Size), Raw.data(), &Format, DIB_RGB_COLORS) == Size;

            ReleaseDC(nullptr, Screen);
            DeleteObject(Info.hbmColor);
            DeleteObject(Info.hbmMask);
        }

        DestroyIcon(Icon);

        if (!Loaded) {
            return false;
        }

        Image.Size = Size;
        Image.Pixels.resize(Raw.size() * 4);

        for (Index_t Index{}; Index < Raw.size(); ++Index) {
            const FColor Color{Unpack(Raw[Index])};
            const float Alpha{static_cast<float>(Raw[Index] >> 24) / 255.0f};

            Image.Pixels[Index * 4 + 0] = Color[0] * Alpha;
            Image.Pixels[Index * 4 + 1] = Color[1] * Alpha;
            Image.Pixels[Index * 4 + 2] = Color[2] * Alpha;
            Image.Pixels[Index * 4 + 3] = Alpha;
        }

        return true;
    }

    Core::TStaticArray<float, 4> Platform::FSplashCanvas::SampleIcon(const FIconImage& Image, float U, float V) {
        const float X{std::clamp(U * static_cast<float>(Image.Size) - 0.5f, 0.0f, static_cast<float>(Image.Size - 1))};
        const float Y{std::clamp(V * static_cast<float>(Image.Size) - 0.5f, 0.0f, static_cast<float>(Image.Size - 1))};
        const Int32 X0{static_cast<Int32>(X)};
        const Int32 Y0{static_cast<Int32>(Y)};
        const Int32 X1{std::min(X0 + 1, Image.Size - 1)};
        const Int32 Y1{std::min(Y0 + 1, Image.Size - 1)};
        const float FX{X - static_cast<float>(X0)};
        const float FY{Y - static_cast<float>(Y0)};
        Core::TStaticArray<float, 4> Result{};

        for (Index_t Channel{}; Channel < 4; ++Channel) {
            const auto At{[&Image, Channel](Int32 PX, Int32 PY) {
                return Image.Pixels[static_cast<Index_t>(PY * Image.Size + PX) * 4 + Channel];
            }};
            const float Top{At(X0, Y0) + (At(X1, Y0) - At(X0, Y0)) * FX};
            const float Bottom{At(X0, Y1) + (At(X1, Y1) - At(X0, Y1)) * FX};

            Result[Channel] = Top + (Bottom - Top) * FY;
        }

        return Result;
    }

    void Platform::FSplashCanvas::BlendIcon(const FIconImage& Image, float Left, float Top, float Size) {
        const Int32 X0{std::max(0, static_cast<Int32>(std::floor(Left)))};
        const Int32 Y0{std::max(0, static_cast<Int32>(std::floor(Top)))};
        const Int32 X1{std::min(mWidth, static_cast<Int32>(std::ceil(Left + Size)))};
        const Int32 Y1{std::min(mHeight, static_cast<Int32>(std::ceil(Top + Size)))};

        for (Int32 Y{Y0}; Y < Y1; ++Y) {
            for (Int32 X{X0}; X < X1; ++X) {
                const Core::TStaticArray<float, 4> Sample{SampleIcon(Image, (static_cast<float>(X) + 0.5f - Left) / Size, (static_cast<float>(Y) + 0.5f - Top) / Size)};
                Uint32& Pixel{mBits[static_cast<std::size_t>(Y) * mWidth + X]};
                const FColor Under{Unpack(Pixel)};
                const float Inverse{1.0f - Sample[3]};

                Pixel = Pack({Sample[0] + Under[0] * Inverse, Sample[1] + Under[1] * Inverse, Sample[2] + Under[2] * Inverse});
            }
        }
    }

    bool Platform::FSplashCanvas::LoadBackgroundImage(const std::filesystem::path& File, std::vector<Uint32>& Pixels) const {
        using Microsoft::WRL::ComPtr;

        ComPtr<IWICImagingFactory> Factory{};
        ComPtr<IWICBitmapDecoder> Decoder{};
        ComPtr<IWICBitmapFrameDecode> Frame{};
        ComPtr<IWICFormatConverter> Converter{};
        ComPtr<IWICBitmapClipper> Clipper{};
        ComPtr<IWICBitmapScaler> Scaler{};
        UINT SourceWidth{};
        UINT SourceHeight{};

        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory)))
            || FAILED(Factory->CreateDecoderFromFilename(File.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &Decoder))
            || FAILED(Decoder->GetFrame(0, &Frame))
            || FAILED(Factory->CreateFormatConverter(&Converter))
            || FAILED(Converter->Initialize(Frame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))
            || FAILED(Converter->GetSize(&SourceWidth, &SourceHeight)) || SourceWidth == 0 || SourceHeight == 0) {
            return false;
        }

        // 언리얼 스플래시처럼 비율이 다르면 늘리지 않고 가운데를 잘라 창을 꽉 채웁니다.
        const double FrameAspect{static_cast<double>(mWidth) / static_cast<double>(mHeight)};
        WICRect Crop{0, 0, static_cast<INT>(SourceWidth), static_cast<INT>(SourceHeight)};

        if (static_cast<double>(SourceWidth) / static_cast<double>(SourceHeight) > FrameAspect) {
            Crop.Width = std::max(1, static_cast<INT>(std::lround(SourceHeight * FrameAspect)));
            Crop.X = (static_cast<INT>(SourceWidth) - Crop.Width) / 2;
        } else {
            Crop.Height = std::max(1, static_cast<INT>(std::lround(SourceWidth / FrameAspect)));
            Crop.Y = (static_cast<INT>(SourceHeight) - Crop.Height) / 2;
        }

        Pixels.resize(static_cast<std::size_t>(mWidth) * static_cast<std::size_t>(mHeight));

        return SUCCEEDED(Factory->CreateBitmapClipper(&Clipper))
            && SUCCEEDED(Clipper->Initialize(Converter.Get(), &Crop))
            && SUCCEEDED(Factory->CreateBitmapScaler(&Scaler))
            && SUCCEEDED(Scaler->Initialize(Clipper.Get(), static_cast<UINT>(mWidth), static_cast<UINT>(mHeight), WICBitmapInterpolationModeHighQualityCubic))
            && SUCCEEDED(Scaler->CopyPixels(nullptr, static_cast<UINT>(mWidth) * 4, static_cast<UINT>(Pixels.size() * 4), reinterpret_cast<BYTE*>(Pixels.data())));
    }

    void Platform::FSplashCanvas::ComposeBackground(const std::wstring& ProjectName, const std::filesystem::path& BackgroundImage) {
        std::vector<Uint32> Custom{};
        const bool HasCustom{!BackgroundImage.empty() && LoadBackgroundImage(BackgroundImage, Custom)};
        const float Width{static_cast<float>(mWidth)};
        const float Height{static_cast<float>(mHeight)};
        const float LogicalHeight{Height / mScale};
        const float HeroX{Width * 0.5f};
        const float HeroY{HeroCenterY * mScale};
        const FColor SkyTop{0.012f, 0.016f, 0.050f};
        const FColor SkyMiddle{0.070f, 0.055f, 0.165f};
        const FColor SkyHorizon{0.400f, 0.130f, 0.170f};
        const FColor Dusk{0.600f, 0.260f, 0.170f};
        const FColor FarRock{0.050f, 0.045f, 0.095f};
        const FColor NearRock{0.012f, 0.012f, 0.022f};

        for (Int32 Y{}; Y < mHeight; ++Y) {
            const float FY{static_cast<float>(Y) + 0.5f};
            const float LogicalY{FY / mScale};

            for (Int32 X{}; X < mWidth; ++X) {
                const float FX{static_cast<float>(X) + 0.5f};

                if (HasCustom) {
                    const FColor Color{Multiply(Unpack(Custom[static_cast<std::size_t>(Y) * mWidth + X]), 1.0f - 0.85f * SmoothStep(LogicalHeight - 130.0f, LogicalHeight - 62.0f, LogicalY))};
                    const bool Border{X == 0 || Y == 0 || X == mWidth - 1 || Y == mHeight - 1};

                    mBits[static_cast<std::size_t>(Y) * mWidth + X] = Pack(Border ? FColor{0.10f, 0.10f, 0.12f} : Color, Dither(X, Y));

                    continue;
                }

                const float LogicalX{FX / mScale};
                const float HeroGlow{Glow(FX, FY, HeroX, HeroY, 250.0f * mScale)};
                const float SkyT{std::clamp(LogicalY / HorizonY, 0.0f, 1.0f)};
                FColor Color{SkyT < 0.55f ? Lerp(SkyTop, SkyMiddle, SkyT / 0.55f) : Lerp(SkyMiddle, SkyHorizon, SmoothStep(0.55f, 1.0f, SkyT))};

                Color = Add(Color, Dusk, 0.30f * Glow(FX, FY, Width * 0.18f, HorizonY * mScale, 300.0f * mScale));
                Color = Add(Color, MacawBlue, 0.42f * HeroGlow);
                Color = Add(Color, MacawRed, 0.10f * Glow(FX, FY, HeroX, HeroY + 30.0f * mScale, 170.0f * mScale));

                // 위쪽 하늘에만 드문드문 별을 찍습니다. 배율이 커져도 같은 위치에 남도록 논리 좌표로 고릅니다.
                const Uint32 Star{Hash(static_cast<Int32>(LogicalX), static_cast<Int32>(LogicalY) + 7919)};

                if ((Star & 0x1FF) == 0) {
                    const float Twinkle{static_cast<float>((Star >> 12) & 0xFF) / 255.0f};

                    Color = Add(Color, {0.85f, 0.88f, 1.0f}, 0.55f * Twinkle * std::pow(1.0f - SkyT, 1.5f));
                }

                const float Far{FarRidge(LogicalX)};
                const float Near{NearRidge(LogicalX)};

                if (LogicalY >= Far) {
                    // 먼 능선은 지평선 빛이 남은 대기 원근으로 윗면을 밝게 둡니다.
                    const float Haze{std::exp(-(LogicalY - Far) / 18.0f)};

                    Color = Lerp(FarRock, Lerp(SkyHorizon, Dusk, 0.25f), 0.45f * Haze);
                    Color = Add(Color, MacawBlue, 0.18f * HeroGlow);
                    Color = Add(Color, MacawBlue, 0.55f * HeroGlow * std::max(0.0f, 1.0f - (LogicalY - Far) / 1.5f));
                }

                if (LogicalY >= Near) {
                    Color = Add(NearRock, MacawBlue, 0.06f * HeroGlow);
                    Color = Add(Color, MacawBlue, 0.45f * Glow(FX, FY, HeroX, HeroY, 420.0f * mScale) * std::max(0.0f, 1.0f - (LogicalY - Near) / 1.2f));
                }

                // 하단 글자가 읽히도록 언리얼 스플래시처럼 아래쪽을 검게 덮습니다.
                Color = Multiply(Color, 1.0f - 0.9f * SmoothStep(LogicalHeight - 130.0f, LogicalHeight - 62.0f, LogicalY));

                if (X == 0 || Y == 0 || X == mWidth - 1 || Y == mHeight - 1) {
                    Color = {0.10f, 0.10f, 0.12f};
                }

                mBits[static_cast<std::size_t>(Y) * mWidth + X] = Pack(Color, Dither(X, Y));
            }
        }

        FIconImage Hero{};

        if (!HasCustom && (LoadMacawIcon(256, Hero) || LoadMacawIcon(128, Hero))) {
            const float Size{HeroSize * mScale};

            BlendIcon(Hero, HeroX - Size * 0.5f, HeroY - Size * 0.5f, Size);
        }

        FIconImage Logo{};

        if (LoadMacawIcon(Pixels(LogoSize), Logo)) {
            const float Size{static_cast<float>(Logo.Size)};

            BlendIcon(Logo, static_cast<float>(mWidth - Pixels(Margin)) - Size, static_cast<float>(mHeight - Pixels(TitleTop)), Size);
        }

        GdiFlush();

        const Int32 Left{Pixels(Margin)};
        const Int32 Right{mWidth - Pixels(Margin)};
        const Int32 TextRight{mWidth - Pixels(Margin + LogoSize + 24.0f)};
        const Int32 Title{mHeight - Pixels(TitleTop)};
        const Int32 Subtitle{mHeight - Pixels(SubtitleTop)};
        const Int32 Status{mHeight - Pixels(StatusTop)};

        SelectObject(mDC, mTitleFont);
        DrawLine(L"Macaw Editor - " + ProjectName, {Left, Title, TextRight, Title + Pixels(26.0f)}, 0xF2F2F2, DT_END_ELLIPSIS);
        SelectObject(mDC, mSubtitleFont);
        DrawLine(L"Macaw Editor", {Left, Subtitle, TextRight, Subtitle + Pixels(17.0f)}, 0xB8B8B8, 0);
        SelectObject(mDC, mStatusFont);
        DrawLine(L"Macaw Engine", {mWidth / 2, Status, Right, Status + Pixels(16.0f)}, 0x9C9C9C, DT_RIGHT);
        GdiFlush();
        mBackground.assign(mBits, mBits + static_cast<std::size_t>(mWidth) * static_cast<std::size_t>(mHeight));
    }
}
