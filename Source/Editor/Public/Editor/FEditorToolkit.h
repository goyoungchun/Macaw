/**
 * @file FEditorToolkit.h
 * @brief 에디터 기능 단위의 프레임 갱신·그리기 인터페이스입니다.
 * @ingroup Editor
 */

#pragma once

namespace Editor {

    /**
     * @brief 에디터 기능 단위의 프레임 갱신·그리기 인터페이스입니다.
     */
    class FEditorToolkit {
    public:
        /**
         * @brief FEditorToolkit의 초기 상태와 전달받은 의존성을 구성합니다.
         */
        FEditorToolkit();

        /**
         * @brief 객체를 소멸하고 소유 멤버와 연결 상태를 정리합니다.
         */
        virtual ~FEditorToolkit();

    public:
        /**
         * @brief 툴킷의 프레임별 상태를 갱신하는 파생 구현 진입점입니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         */
        virtual void Tick(float DeltaTime) = 0;

        /**
         * @brief 툴킷의 창과 콘텐츠를 그리는 파생 구현 진입점입니다.
         * @param[in] DeltaTime 이전 프레임 이후 경과한 시간이며 단위는 초입니다.
         */
        virtual void Draw(float DeltaTime) = 0;
    };

}
