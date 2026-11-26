#pragma once

#include <initializer_list>
#include <memory>
#include <vector>
#include "Editor/View/FEditorViewportTypes.h"
#include "Editor/View/SSplitter.h"
#include "Core/Types.h"

enum class EViewportLayoutPreset : Uint8 {
    Single,
    TwoTopBottom,
    TwoLeftRight,
    ThreeLeftOneRightTwo,
    ThreeLeftTwoRightOne,
    ThreeTopOneBottomTwo,
    ThreeTopTwoBottomOne,
    FourGrid,
    FourLeftOneRightThree,
    FourLeftThreeRightOne,
    FourTopOneBottomThree,
    FourTopThreeBottomOne,
    Count
};

class FViewportPresetLayout {
public:
    static constexpr Uint32 MaximumViewportCount{4};
    static constexpr Uint32 MaximumSplitterCount{3};

    explicit FViewportPresetLayout(EViewportLayoutPreset InPreset = EViewportLayoutPreset::FourGrid);

    void SetPreset(EViewportLayoutPreset InPreset);
    EViewportLayoutPreset GetPreset() const;
    static const char* GetPresetName(EViewportLayoutPreset InPreset);
    const char* GetPresetName() const;
    Uint32 GetViewportCount() const;

    void SetRect(const FRect& InRect);
    void RefreshLayout();
    const FRect& GetViewportRect(FViewportId ViewportId) const;
    void CollectSplitters(std::vector<SSplitter*>& OutSplitters) const;

    void GetSplitterRatios(Core::TStaticArray<float, MaximumSplitterCount>& OutRatios, Uint32& OutCount) const;
    bool RestoreSplitterRatios(const Core::TStaticArray<float, MaximumSplitterCount>& Ratios, Uint32 RatioCount);

private:
    SSplitter* AddTopBottomSplitter(SWindow* First, SWindow* Second, float Ratio = 0.5f, const FSplitterRatio* SharedRatio = nullptr);
    SSplitter* AddLeftRightSplitter(SWindow* First, SWindow* Second, float Ratio = 0.5f, const FSplitterRatio* SharedRatio = nullptr);
    SSplitter* AddSplitter(std::unique_ptr<SSplitter> Splitter, SWindow* First, SWindow* Second, float Ratio, const FSplitterRatio* SharedRatio);
    SWindow* BuildTopBottomThree(FViewportId FirstId, FViewportId SecondId, FViewportId ThirdId);
    SWindow* BuildLeftRightThree(FViewportId FirstId, FViewportId SecondId, FViewportId ThirdId);
    void Reset();
    void SetDrawOrder(std::initializer_list<SSplitter*> InSplitters);

    EViewportLayoutPreset mPreset{EViewportLayoutPreset::FourGrid};
    Uint32 mViewportCount{MaximumViewportCount};
    FRect mRect{};
    Core::TStaticArray<SWindow, MaximumViewportCount> mViewportRegions{};
    Core::TStaticArray<std::unique_ptr<SSplitter>, MaximumSplitterCount> mSplitters{};
    Core::TStaticArray<SSplitter*, MaximumSplitterCount> mSplitterDrawOrder{};
    Uint32 mSplitterCount{0};
    SWindow* mRoot{nullptr};
    FSplitterRatio mSharedGridRatio{};
};
