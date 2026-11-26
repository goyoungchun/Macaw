/**
 * @file FWindField.cpp
 * @brief 월드의 바람 원천을 모아 위치별 바람 속도를 계산합니다.
 * @ingroup Engine
 */

#include "Engine/FWindField.h"
#include "Engine/Component/UWindSourceComponent.h"

#include <algorithm>

namespace Engine {

    /**
     * @brief 바람 원천 하나를 등록합니다.
     */
    void Engine::FWindField::RegisterSource(const Engine::UWindSourceComponent* Source) {
        if (Source != nullptr && std::find(mSources.begin(), mSources.end(), Source) == mSources.end()) {
            mSources.push_back(Source);
        }
    }

    /**
     * @brief 등록한 바람 원천을 뺍니다.
     */
    void Engine::FWindField::UnregisterSource(const Engine::UWindSourceComponent* Source) {
        mSources.erase(std::remove(mSources.begin(), mSources.end(), Source), mSources.end());
    }

    /**
     * @brief 월드 위치의 바람 속도를 모든 원천을 합쳐 계산합니다.
     */
    FVector Engine::FWindField::Sample(const FVector& Position) const {
        FVector Wind{};

        for (const Engine::UWindSourceComponent* Source : mSources) {
            Wind += Source->SampleWind(Position);
        }

        return Wind;
    }

    /**
     * @brief 등록한 원천의 개수를 반환합니다.
     */
    Index_t Engine::FWindField::GetSourceCount() const {
        return mSources.size();
    }

}
