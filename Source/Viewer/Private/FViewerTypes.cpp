/**
 * @file FViewerTypes.cpp
 * @brief 뷰어 촬영의 준비 결과와 재현 가능한 촬영 상태를 정의합니다.
 * @ingroup Viewer
 */

#include "Viewer/FViewerTypes.h"
#include "Core/Literals/UnitLiterals.h"

using namespace Viewer;
using namespace Core::Literals;

constinit const float Viewer::FViewerCaptureState::mDefaultFixedStep{(1.0f / 60_Hz).GetBase()};

FViewerCaptureState::FViewerCaptureState(double Time, Uint32 Seed, float FixedStep)
	: mTime{Time},
	  mSeed{Seed},
	  mFixedStep{FixedStep} {
}

double FViewerCaptureState::GetTime() const {
    return mTime;
}

Uint32 FViewerCaptureState::GetSeed() const {
    return mSeed;
}

float FViewerCaptureState::GetFixedStep() const {
    return mFixedStep;
}
