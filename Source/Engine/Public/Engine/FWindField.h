/**
 * @file FWindField.h
 * @brief 월드의 바람 원천을 모아 위치별 바람 속도를 계산합니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Math/FVector.h"
#include "Core/Types.h"

namespace Engine {
    class UWindSourceComponent;
}

namespace Engine {

    /**
     * @brief 월드의 바람 원천을 모아 위치별 바람 속도를 계산합니다.
     * @details 천과 머리카락 같은 소비자가 자기 위치를 넘겨 바람 속도(m/s, 월드 좌표)를 받아 갑니다.
     *          원천은 컴포넌트가 월드에 등록될 때 스스로 등록하고, 해제될 때 스스로 뺍니다. 이 객체는 컴포넌트를 소유하지 않습니다.
     * @ingroup Engine
     */
    class FWindField {
    public:

        /**
         * @brief 바람 원천 하나를 등록합니다.
         * @param[in] Source 등록할 원천의 비소유 주소입니다. 이미 등록했거나 nullptr이면 무시합니다.
         */
        void RegisterSource(const Engine::UWindSourceComponent* Source);

        /**
         * @brief 등록한 바람 원천을 뺍니다.
         * @param[in] Source 뺄 원천의 주소입니다. 등록하지 않았으면 무시합니다.
         */
        void UnregisterSource(const Engine::UWindSourceComponent* Source);

        /**
         * @brief 월드 위치의 바람 속도를 모든 원천을 합쳐 계산합니다.
         * @param[in] Position 바람을 알고 싶은 월드 위치입니다.
         * @return 월드 좌표의 바람 속도(m/s)이며, 바람이 없으면 영벡터입니다.
         */
        FVector Sample(const FVector& Position) const;

        /**
         * @brief 등록한 원천의 개수를 반환합니다.
         * @return 원천 개수입니다.
         */
        Index_t GetSourceCount() const;

    private:

        /**
         * @brief 등록한 바람 원천의 비소유 주소 목록입니다.
         */
        Core::TArray<const Engine::UWindSourceComponent*> mSources{};
    };

}
