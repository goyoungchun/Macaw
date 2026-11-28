/**
 * @file FTransform.h
 * @brief 위치, 쿼터니언 회전과 축별 스케일을 묶은 공간 변환입니다.
 * @ingroup Core
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FQuat.h"
#include "FTransform.generated.h"

/**
 * @brief 위치, 쿼터니언 회전과 축별 스케일을 묶은 공간 변환입니다.
 * @ingroup Core
 */
USTRUCT()
struct alignas(16) FTransform {
    GENERATED_BODY()

public:
    /**
     * @brief FTransform의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    FTransform() = default;

    /**
     * @brief FTransform의 초기 상태와 전달받은 의존성을 구성합니다.
     * @param[in] Location 오류가 발생한 원래 호출 위치입니다.
     * @param[in] Rotation 적용할 단위 쿼터니언 회전입니다.
     * @param[in] Scale 축별 스케일입니다.
     */
    FTransform(const FVector& Location, const FQuat& Rotation, const FVector& Scale);

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    ~FTransform() = default;

    /**
     * @brief 상대 값의 상태로 객체를 복사하거나 이동 생성합니다.
     * @param[in] Other 복사할 원본 값입니다.
     */
    FTransform(const FTransform& Other) = default;

    /**
     * @brief 상대 값의 상태를 대입합니다.
     * @param[in] Other 복사할 원본 값입니다.
     * @return 갱신된 자기 자신(*this)의 참조입니다.
     */
    FTransform& operator=(const FTransform& Other) = default;

    /**
     * @brief 상대 값의 상태로 객체를 복사하거나 이동 생성합니다.
     * @param[in] Other 이동할 원본 값입니다.
     */
    FTransform(FTransform&& Other) noexcept = default;

    /**
     * @brief 상대 값의 상태를 대입합니다.
     * @param[in] Other 이동할 원본 값입니다.
     * @return 갱신된 자기 자신(*this)의 참조입니다.
     */
    FTransform& operator=(FTransform&& Other) noexcept = default;

public:
    /**
     * @brief 현재 변환 뒤에 상대 변환을 적용하도록 위치, 회전과 축별 스케일을 합성합니다.
     * @param[in] Other 현재 변환 다음에 적용할 변환입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FTransform operator*(const FTransform& Other) const;

    /**
     * @brief 현재 변환 뒤에 상대 변환을 적용하도록 위치, 회전과 축별 스케일을 합성합니다.
     * @param[in] Other 현재 변환 다음에 적용할 변환입니다.
     * @return 갱신된 자기 자신(*this)의 참조입니다.
     */
    FTransform& operator*=(const FTransform& Other);

    FTransform GetRelativeTransform(const FTransform& Parent) const;

    /**
     * @brief 위치에 스케일, 회전과 이동을 적용합니다.
     * @param[in] Position 변환하거나 투영할 위치입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector TransformPosition(const FVector& Position) const;

    /**
     * @brief 방향 벡터에 스케일과 회전을 적용합니다.
     * @param[in] Vector 연산에 사용할 벡터입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector TransformVector(const FVector& Vector) const;

    /**
     * @brief 월드 위치를 이 변환의 로컬 공간 위치로 되돌립니다.
     * @param[in] Position 변환하거나 투영할 위치입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector InverseTransformPosition(const FVector& Position) const;

    /**
     * @brief 방향 벡터에 역회전과 역스케일을 적용하며 이동은 무시합니다.
     * @param[in] Vector 연산에 사용할 벡터입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector InverseTransformVector(const FVector& Vector) const;

    /**
     * @brief 위치에 스케일을 제외한 회전과 이동을 적용합니다.
     * @param[in] Position 변환하거나 투영할 위치입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector TransformPositionNoScale(const FVector& Position) const;

    /**
     * @brief 벡터에 스케일을 제외한 회전만 적용합니다.
     * @param[in] Vector 연산에 사용할 벡터입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector TransformVectorNoScale(const FVector& Vector) const;

    /**
     * @brief 위치에 이동과 회전의 역변환을 적용합니다.
     * @param[in] Position 변환하거나 투영할 위치입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector InverseTransformPositionNoScale(const FVector& Position) const;

    /**
     * @brief 벡터에 역회전을 적용합니다.
     * @param[in] Vector 연산에 사용할 벡터입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FVector InverseTransformVectorNoScale(const FVector& Vector) const;

    /**
     * @brief 위치·스케일 선형 보간과 회전 보간을 결합한 변환을 구합니다.
     * @param[in] Other Alpha가 1일 때의 목표 값입니다.
     * @param[in] Alpha 보간 비율입니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FTransform Blend(const FTransform& Other, float Alpha) const;

    /**
     * @brief 변환의 쿼터니언 회전을 제자리에서 정규화합니다.
     * @return 정규화할 수 있으면 true이며, 퇴화하거나 유효하지 않은 값이면 false입니다.
     */
    bool NormalizeRotation();

    /**
     * @brief 현재 변환이 허용 오차 이내에서 항등 변환인지 검사합니다.
     * @param[in] Tolerance 항등 행렬과 비교할 성분별 허용 차이입니다.
     * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
     */
    bool IsIdentity(float Tolerance = 0.0001f) const;

    /**
     * @brief 각 성분의 차이가 지정한 허용 오차 이내인지 검사합니다.
     * @param[in] Other 비교할 상대 값입니다.
     * @param[in] Tolerance 같다고 볼 성분별 최대 차이입니다.
     * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
     */
    bool Equals(const FTransform& Other, float Tolerance = 0.0001f) const;

    /**
     * @brief 현재 쿼터니언 회전을 도 단위 오일러 각으로 변환한 값을 반환합니다.
     * @return 현재 쿼터니언 회전을 도 단위 오일러 각으로 변환한 값입니다.
     */
    FRotator GetRotator() const;

    /**
     * @brief 유효한 도 단위 오일러 회전을 쿼터니언으로 변환하여 적용합니다.
     * @param[in] Rotation 적용할 오일러 회전(도 단위)입니다.
     * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
     */
    bool SetRotator(const FRotator& Rotation);

    /**
     * @brief 위치, 회전과 스케일을 결합한 변환 행렬을 반환합니다.
     * @return 연산을 적용한 새 값입니다.
     */
    FMatrix ToMatrix() const;

    /**
     * @brief 성분이 유한하고 회전과 스케일이 사용 가능한 공간 변환인지 검사합니다.
     * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
     */
    bool IsValid() const;

public:
    /**
     * @brief 공간상의 위치입니다.
     */
    UPROPERTY(Edit, Serialize)
    FVector mLocation{};

    /**
     * @brief 회전 값입니다.
     */
    UPROPERTY(Edit, Serialize, Getter = GetRotator, Setter = SetRotator)
    FQuat mRotation{};

    /**
     * @brief 축별 스케일입니다.
     */
    UPROPERTY(Edit, Serialize)
    FVector mScale{1.0f, 1.0f, 1.0f};
};
