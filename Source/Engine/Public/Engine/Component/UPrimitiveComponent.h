/**
 * @file UPrimitiveComponent.h
 * @brief 렌더링과 충돌에 쓰이는 기하 형태를 가진 컴포넌트의 기반 클래스입니다.
 * @ingroup Engine
 */

#pragma once

#include "CoreUObject/TObjectRef.h"
#include "CoreUObject/TDelegate.h"

#include "Core/ReflectionMacros.h"
#include "Engine/Component/USceneComponent.h"
#include "Core/Math/FVector2.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "UPrimitiveComponent.generated.h"

namespace Asset {
    struct FMeshData;
    class UStaticMesh;
}

namespace PhysicsRuntime {
    class UBodySetup;
}

struct FBox;

namespace Engine {

    class AActor;
    enum class EPrimitiveType : Uint8;
    class FPrimitiveDrawInterface;
    class UMovementComponent;
    class UVehicleMovementComponent;
    struct FHitResult;
    struct FPrimitiveSceneProxy;

    /**
     * @brief 렌더링과 충돌에 쓰이는 기하 형태를 가진 컴포넌트의 기반 클래스입니다.
     * @details 렌더 장면 프록시의 생성과 지연 갱신, 월드 경계, 그림자·데칼 같은 렌더 속성을 제공합니다.
     *          겹침·충돌 이벤트와 이동 차단 여부도 함께 제공하며, 구체적인 형상은 파생 클래스가 정의합니다.
     *          렌더 프록시가 필요 없는 파생 클래스는 ShouldCreateRenderState()에서 false를 반환합니다.
     *          GetBodySetup()이 충돌 형상을 반환하는 파생 클래스는 월드에 물리 장면이 있을 때 바디 인스턴스로 PhysX 바디를 만들고,
     *          동적 바디의 시뮬레이션 결과를 트랜스폼에 반영합니다.
     * @ingroup Engine
     */
    UCLASS(Abstract)
    class UPrimitiveComponent : public Engine::USceneComponent {
        GENERATED_BODY()

    public:

        /**
         * @brief 겹친 컴포넌트를 전달하는 이벤트 시그니처입니다.
         */
        using FOverlapDelegate = CoreUObject::TDelegate<Engine::UPrimitiveComponent*, Engine::AActor*, Engine::UPrimitiveComponent*>;

        /**
         * @brief 충돌한 액터·컴포넌트·충돌 정보를 전달하는 이벤트 시그니처입니다.
         */
        using FHitDelegate = CoreUObject::TDelegate<Engine::UPrimitiveComponent*, Engine::AActor*, Engine::UPrimitiveComponent*, const Engine::FHitResult&>;

    private:
        enum class EInitialPenetrationMode : Uint8 {
            Allow,
            Stop,
            Resolve
        };

    public:

        /**
         * @brief UPrimitiveComponent의 초기 상태와 전달받은 의존성을 구성합니다.
         */
        UPrimitiveComponent();

        /**
         * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
         */
        ~UPrimitiveComponent() override;

    public:

        /**
         * @brief 컴포넌트를 월드에 연결하고 필요한 렌더 상태를 등록합니다.
         * @param[in,out] World 컴포넌트를 등록할 월드입니다.
         */
        void RegisterComponent(Engine::UWorld* World) override;

        /**
         * @brief 등록된 렌더 상태와 월드 연결을 해제합니다.
         */
        void UnregisterComponent() override;

        /**
         * @brief 소유 액터의 표시 여부 변경을 프록시 재생성으로 반영합니다.
         */
        void OnOwnerVisibilityChanged() override;

        /**
         * @brief 유한하고 유효한 변환을 검증하여 공간 컴포넌트에 적용합니다.
         * @param[in] Transform 적용할 위치, 회전과 스케일입니다.
         * @return 조건을 만족하거나 작업이 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool SetTransform(const FTransform& Transform) override;

        /**
         * @brief 프리미티브 프록시 재생성이 필요함을 표시하고 갱신을 예약합니다.
         */
        void MarkRenderStateDirty();

        /**
         * @brief 변환 업로드가 필요함을 표시하고 프레임 말 갱신을 예약합니다.
         */
        void MarkRenderTransformDirty();

        /**
         * @brief 누적된 렌더 상태 또는 변환 변경을 연결된 장면에 반영합니다.
         */
        void SendRenderUpdates();

        /**
         * @brief 표시할 프리미티브 메시 종류를 반환합니다.
         * @return 표시할 프리미티브 메시 종류입니다.
         */
        virtual Engine::EPrimitiveType GetPrimitiveType() const = 0;

        /**
         * @brief 프리미티브가 사용하는 정적 메시의 비소유 주소를 반환합니다.
         * @return 대상의 비소유 주소이며, 없거나 유효하지 않으면 nullptr입니다.
         */
        virtual Asset::UStaticMesh* GetStaticMesh() const;

        /**
         * @brief 메시의 CPU 정점 속성·인덱스 데이터 참조를 반환합니다.
         * @return 객체가 보유한 데이터의 참조이며 소유 객체의 수명 동안 유효합니다.
         */
        virtual const Asset::FMeshData& GetMeshData() const;

        /**
         * @brief 컴포넌트의 현재 상태를 복사한 프리미티브 프록시를 만듭니다.
         * @return 현재 상태를 담은 프록시입니다.
         */
        virtual Engine::FPrimitiveSceneProxy CreateSceneProxy() const;

        /**
         * @brief 월드 공간의 축 정렬 경계 상자를 반환합니다.
         * @return 월드 공간의 축 정렬 경계 상자입니다.
         */
        virtual FBox GetWorldBounds() const;

        /**
         * @brief 현재 표시·에셋 상태에서 렌더 프록시를 생성할지 판정합니다.
         * @return 조건을 만족하면 true입니다.
         */
        virtual bool ShouldCreateRenderState() const;

        /**
         * @brief 다른 살아 있는 제어자의 변환을 덮어쓰지 않고 UV 배율과 오프셋을 적용합니다.
         * @param[in] Scale 기본 UV에 곱할 배율입니다.
         * @param[in] Offset UV에 더할 셀 원점입니다.
         * @param[in] Source UV 제어자의 유효한 세대 핸들입니다.
         * @return 값이 유효해 적용했으면 true, 거부했으면 false입니다.
         */
        bool SetUVTransform(const FVector2& Scale, const FVector2& Offset, CoreUObject::TObjectRef<CoreUObject::UObject> Source);

        /**
         * @brief 요청한 제어자가 설정한 UV만 기본 변환으로 복원합니다.
         * @param[in] Source UV 제어자의 유효한 세대 핸들입니다.
         */
        void ClearUVTransform(CoreUObject::TObjectRef<CoreUObject::UObject> Source);

        /**
         * @brief 기본 UV에 곱할 셀 크기를 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const FVector2& GetUVScale() const;

        /**
         * @brief 기본 UV에 더할 셀 원점을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const FVector2& GetUVOffset() const;

        /**
         * @brief 프리미티브의 그림자 생성 여부를 반환합니다.
         * @return 프리미티브의 그림자 생성 여부입니다.
         */
        bool GetCastShadow() const;

        /**
         * @brief 프리미티브의 그림자 생성 여부를 검증하고 변경합니다.
         * @param[in] CastShadow 프리미티브가 그림자를 생성하도록 설정하려면 true입니다.
         * @return 입력값을 수락하고 상태를 갱신하면 true이며 잘못된 값이면 false입니다.
         */
        bool SetCastShadow(bool CastShadow);

        /**
         * @brief 프리미티브의 그림자 수신 여부를 반환합니다.
         * @return 프리미티브의 그림자 수신 여부입니다.
         */
        bool GetReceiveShadow() const;

        /**
         * @brief 프리미티브의 그림자 수신 여부를 검증하고 변경합니다.
         * @param[in] ReceiveShadow 프리미티브가 그림자를 받도록 설정하려면 true입니다.
         * @return 입력값을 수락하고 상태를 갱신하면 true이며 잘못된 값이면 false입니다.
         */
        bool SetReceiveShadow(bool ReceiveShadow);

        /**
         * @brief 프리미티브의 데칼 수신 설정을 반환합니다.
         * @return 데칼을 받도록 설정되어 있으면 true입니다.
         */
        bool GetReceivesDecals() const;

        /**
         * @brief 데칼 수신 여부를 설정하고 렌더 상태에 반영합니다.
         * @param ReceivesDecals 데칼 수신 여부입니다.
         */
        void SetReceivesDecals(bool ReceivesDecals);

        /**
         * @brief 게임(Game·PIE) 월드에서 이 프리미티브를 그리지 않는지 반환합니다. 충돌과 물리는 영향받지 않습니다.
         * @return 게임 중 숨김이면 true입니다.
         */
        bool IsHiddenInGame() const;

        /**
         * @brief 게임(Game·PIE) 월드에서 그리기 여부를 바꿉니다. 에디터 월드에서는 계속 보여 편집할 수 있습니다.
         * @param HiddenInGame 게임 중 숨기려면 true입니다.
         */
        void SetHiddenInGame(bool HiddenInGame);

        /**
         * @brief 컴포넌트 겹침 시작 이벤트를 반환합니다.
         * @return 이 컴포넌트가 소유한 이벤트 델리게이트의 참조입니다.
         */
        FOverlapDelegate& GetOnComponentBeginOverlap();

        /**
         * @brief 컴포넌트 겹침 종료 이벤트를 반환합니다.
         * @return 이 컴포넌트가 소유한 이벤트 델리게이트의 참조입니다.
         */
        FOverlapDelegate& GetOnComponentEndOverlap();

        /**
         * @brief 컴포넌트 충돌 이벤트를 반환합니다.
         * @return 이 컴포넌트가 소유한 이벤트 델리게이트의 참조입니다.
         */
        FHitDelegate& GetOnComponentHit();

        /**
         * @brief 다른 컴포넌트와 겹칠 때 겹침 이벤트를 만드는지 반환합니다.
         * @return 겹침 이벤트를 만들면 true입니다.
         */
        bool GetGenerateOverlapEvents() const;

        /**
         * @brief 다른 컴포넌트와 겹칠 때 겹침 이벤트를 만들지 설정합니다.
         * @param[in] Generate 겹침 이벤트를 만들려면 true입니다.
         * @note 두 컴포넌트가 모두 켜져 있어야 이벤트가 오갑니다. 끄면 다음 겹침 갱신에서 겹침 종료 이벤트가 나갑니다.
         */
        void SetGenerateOverlapEvents(bool Generate);

        /**
         * @brief 겹침 시작 델리게이트와 소유 액터의 충돌 이벤트를 호출합니다.
         * @param[in] Other 겹치기 시작한 상대 컴포넌트입니다.
         */
        void OnBeginOverlap(Engine::UPrimitiveComponent* Other);

        /**
         * @brief 겹침 종료 델리게이트와 소유 액터의 충돌 이벤트를 호출합니다.
         * @param[in] Other 겹침이 끝난 상대 컴포넌트입니다.
         */
        void OnEndOverlap(Engine::UPrimitiveComponent* Other);

        /**
         * @brief 적중 액터·컴포넌트와 충돌 정보를 델리게이트와 소유 액터의 충돌 이벤트로 전달합니다.
         * @param[in] OtherActor 충돌한 상대 액터입니다.
         * @param[in] OtherComp 충돌한 상대 컴포넌트입니다.
         * @param[in] Hit 원본 충돌 결과입니다.
         */
        void OnHit(Engine::AActor* OtherActor, Engine::UPrimitiveComponent* OtherComp, const Engine::FHitResult& Hit);

        /**
         * @brief 바디를 만들 충돌 형상이 있으면 물리 상태가 필요하다고 반환합니다.
         * @return GetBodySetup()이 바디 셋업을 반환하면 true입니다.
         */
        bool ShouldCreatePhysicsState() const override;

        /**
         * @brief 물리 바디를 만들 때 사용할 충돌 형상을 반환하는 확장 지점입니다.
         * @return 조회한 값의 비소유 주소이며, 충돌 형상이 없으면 nullptr입니다. 기본 구현은 nullptr입니다.
         * @note 반환한 바디 셋업은 물리 상태가 유지되는 동안 살아 있어야 합니다.
         */
        virtual const PhysicsRuntime::UBodySetup* GetBodySetup() const;

        /**
         * @brief 이 컴포넌트의 바디가 현재 자세에서 겹치는 다른 바디를 장면 쿼리로 찾습니다.
         * @param[out] OutOverlaps 겹친 바디 인스턴스의 비소유 주소 목록이며, 중복 없이 채웁니다.
         * @return 쿼리할 바디가 있어 쿼리를 수행했으면 true입니다.
         * @note 기본 구현은 컴포넌트 대표 바디 하나로 쿼리합니다. 바디를 여러 개 가진 컴포넌트는 재정의합니다.
         *       결과에는 이 컴포넌트가 가진 다른 바디가 들어 있을 수 있습니다.
         */
        virtual bool ComponentOverlapMulti(Core::TArray<PhysicsRuntime::FBodyInstance*>& OutOverlaps) const;

        /**
         * @brief 충돌 프리셋을 적용합니다.
         * @param[in] Preset 적용할 프리셋입니다.
         */
        void SetCollisionPreset(PhysicsRuntime::ECollisionPreset Preset);

        /**
         * @brief 쿼리·시뮬레이션 참여 방식을 반환합니다.
         * @return 참여 방식입니다.
         */
        PhysicsRuntime::ECollisionEnabled GetCollisionEnabled() const;

        /**
         * @brief 쿼리·시뮬레이션 참여 방식을 바꿉니다.
         * @param[in] CollisionEnabled 새 참여 방식입니다.
         */
        void SetCollisionEnabled(PhysicsRuntime::ECollisionEnabled CollisionEnabled);

        /**
         * @brief 다른 모든 바디에 대한 응답을 바꿉니다.
         * @param[in] Response 새 응답입니다.
         */
        void SetCollisionResponseToAllChannels(PhysicsRuntime::ECollisionResponse Response);

    protected:
        /**
         * @brief 바디가 있으면 이동 경로를 쓸어 응답이 Block인 바디에 막히는 지점에서 멈추고 양쪽에 Hit 이벤트를 보냅니다.
         * @param[in] Delta 이동할 월드 변위(m)입니다.
         * @param[in] Sweep 이동 경로를 쓸지 여부입니다.
         * @param[out] OutHit 막힌 지점의 정보입니다.
         * @return 이동을 적용했으면 true입니다.
         * @note 같은 액터의 바디에는 막히지 않습니다. 바디가 없거나 쿼리에 참여하지 않으면 쓸지 않고 옮깁니다.
         */
        bool MoveComponentImpl(const FVector& Delta, bool Sweep, Engine::FHitResult& OutHit) override;

    public:

        /**
         * @brief 물리 시뮬레이션 설정과 실제 바디를 가진 바디 인스턴스를 반환합니다.
         * @return 이 객체가 보관하는 값의 참조입니다.
         */
        const PhysicsRuntime::FBodyInstance& GetBodyInstance() const;

        /**
         * @brief 바디 인스턴스 설정을 바꾸고 물리 상태를 다시 만듭니다.
         * @param[in] BodyInstance 설정을 복사할 바디 인스턴스이며, 실제 바디는 복사하지 않습니다.
         * @return 항상 true입니다.
         */
        bool SetBodyInstance(const PhysicsRuntime::FBodyInstance& BodyInstance);

        /**
         * @brief 실제 바디가 동적 강체로 시뮬레이션되고 있는지 반환합니다.
         * @return 동적 강체가 있으면 true입니다.
         */
        bool IsSimulatingPhysics() const;

        /**
         * @brief 물리 시뮬레이션 여부를 바꾸고 물리 상태를 다시 만듭니다.
         * @param[in] Simulate 동적 강체로 시뮬레이션할지 여부입니다.
         */
        void SetSimulatePhysics(bool Simulate);

        /**
         * @brief 동적 강체의 질량 중심에 힘을 더합니다.
         * @param[in] Force 더할 힘(N)이며, AccelChange가 true이면 가속도(m/s²)입니다.
         * @param[in] AccelChange 질량과 무관하게 가속도로 적용할지 여부입니다.
         * @return 동적 강체가 있어 적용했으면 true입니다.
         */
        bool AddForce(const FVector& Force, bool AccelChange = false);

        /**
         * @brief 동적 강체의 질량 중심에 충격량을 더합니다.
         * @param[in] Impulse 더할 충격량(N·s)이며, VelocityChange가 true이면 속도 변화량(m/s)입니다.
         * @param[in] VelocityChange 질량과 무관하게 속도 변화량으로 적용할지 여부입니다.
         * @return 동적 강체가 있어 적용했으면 true입니다.
         */
        bool AddImpulse(const FVector& Impulse, bool VelocityChange = false);

        /**
         * @brief 동적 강체의 선속도를 반환합니다.
         * @return 선속도(m/s)이며, 동적 강체가 없으면 영벡터입니다.
         */
        FVector GetPhysicsLinearVelocity() const;

        /**
         * @brief 동적 강체의 선속도를 설정합니다.
         * @param[in] Velocity 새 선속도(m/s)입니다.
         * @return 동적 강체가 있어 적용했으면 true입니다.
         */
        bool SetPhysicsLinearVelocity(const FVector& Velocity);

        /**
         * @brief 동적 강체의 각속도를 반환합니다.
         * @return 각속도(rad/s)이며, 동적 강체가 없으면 영벡터입니다.
         */
        FVector GetPhysicsAngularVelocity() const;

        /**
         * @brief 동적 강체의 각속도를 설정합니다.
         * @param[in] Velocity 새 각속도(rad/s)입니다.
         * @return 동적 강체가 있어 적용했으면 true입니다.
         */
        bool SetPhysicsAngularVelocity(const FVector& Velocity);

        /**
         * @brief 시뮬레이션한 동적 강체의 자세를 컴포넌트 트랜스폼에 반영합니다.
         * @details 액터의 루트 컴포넌트이면 액터 전체를 옮기고, 아니면 이 컴포넌트만 옮깁니다.
         *          반영하는 동안에는 바디를 다시 순간 이동하지 않습니다.
         */
        void SyncComponentToBody();

        /**
         * @brief 바디 셋업의 충돌 형상을 현재 트랜스폼에 맞춰 선분으로 수집합니다.
         * @details 물리 바디와 같은 스케일 규칙을 쓰므로 편집 월드에서도 시뮬레이션할 때의 형상을 그대로 보여 줍니다.
         *          선택하면 주황색, 시뮬레이션 설정이면 초록색, 그 밖에는 파란색으로 그립니다.
         * @param[in,out] DrawInterface 선분을 추가할 프레임 수집기입니다.
         * @param[in] Highlight 선택 강조 색상으로 그릴지 여부입니다.
         * @param[in] Thickness 픽셀 단위 선 두께입니다.
         * @note 바디 셋업이 없으면 아무것도 그리지 않습니다.
         */
        void DrawCollision(Engine::FPrimitiveDrawInterface& DrawInterface, bool Highlight, float Thickness = 1.0f) const;
        void DrawCollision(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector4& Color, float Thickness) const;

    private:

        /**
         * @brief 같은 컴포넌트를 중복 예약하지 않고 월드의 갱신 큐에 넣습니다.
         */
        void QueueUpdate();

    protected:

        /**
         * @brief 바디 셋업의 형상으로 월드 물리 장면에 바디를 만듭니다.
         * @note 바디를 여러 개 만드는 하위 클래스는 재정의하고, 바디 하나로 충분할 때 이 구현을 호출합니다.
         */
        void OnCreatePhysicsState() override;

        /**
         * @brief 바디를 물리 장면에서 해제합니다.
         */
        void OnDestroyPhysicsState() override;

    private:

        friend class UMovementComponent;
        friend class UVehicleMovementComponent;

        bool MoveComponentSwept(const FVector& Delta, const FQuat& NewRotation, EInitialPenetrationMode PenetrationMode, Engine::FHitResult& OutHit);

    private:
        /**
         * @brief 프록시 전체 재생성이 필요한지 여부입니다.
         */
        bool mRenderStateDirty{true};

        /**
         * @brief 프리미티브의 그림자 생성 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Rendering", Getter = GetCastShadow, Setter = SetCastShadow)
        bool mCastShadow{true};

        /**
         * @brief 프리미티브의 그림자 수신 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Rendering", Getter = GetReceiveShadow, Setter = SetReceiveShadow)
        bool mReceiveShadow{true};

        /**
         * @brief 이 프리미티브가 데칼을 받는지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Rendering", Getter = GetReceivesDecals, Setter = SetReceivesDecals)
        bool mReceivesDecals{true};

        /**
         * @brief 게임(Game·PIE) 월드에서 그리지 않는지 여부입니다. 차량 충돌 박스처럼 물리 전용 프리미티브에 씁니다.
         */
        UPROPERTY(Edit, Serialize, Getter = IsHiddenInGame, Setter = SetHiddenInGame)
        bool mHiddenInGame{};

        /**
         * @brief 변환 업로드가 필요한지 여부입니다.
         */
        bool mRenderTransformDirty{};

        /**
         * @brief 월드 갱신 큐에 이미 등록되었는지 여부입니다.
         */
        bool mQueued{};

        /**
         * @brief 렌더 장면에 프록시가 생성되었는지 여부입니다.
         */
        bool mRenderStateCreated{};

        /**
         * @brief 기본 UV에 곱할 셀 크기입니다.
         */
        FVector2 mUVScale{1.0f, 1.0f};

        /**
         * @brief 기본 UV에 더할 셀 원점입니다.
         */
        FVector2 mUVOffset{};

        /**
         * @brief 현재 UV 변환을 제어하는 컴포넌트 핸들입니다.
         */
        CoreUObject::TObjectRef<CoreUObject::UObject> mUVSource{};

        /**
         * @brief 다른 컴포넌트와 겹칠 때 겹침 이벤트를 만드는지 여부입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Collision", Getter = GetGenerateOverlapEvents, Setter = SetGenerateOverlapEvents)
        bool mGenerateOverlapEvents{};

        /**
         * @brief 물리 시뮬레이션 설정과 실제 바디입니다.
         */
        UPROPERTY(Edit, Serialize, ShowOnlyInnerProperties, Category = "Physics", Getter = GetBodyInstance, Setter = SetBodyInstance)
        PhysicsRuntime::FBodyInstance mBodyInstance{};

        /** @brief 단일 바디 생성 직후와 해제 직전에 알립니다. */
        CoreUObject::TDelegate<Engine::UPrimitiveComponent*, bool> mOnBodyStateChanged{};

        /**
         * @brief 시뮬레이션 결과를 트랜스폼에 반영하는 중이라 바디를 순간 이동하지 않아야 하는지 여부입니다.
         */
        bool mSyncingFromPhysics{};

        /**
         * @brief 컴포넌트 겹침 시작 이벤트입니다.
         */
        FOverlapDelegate mOnComponentBeginOverlap{};

        /**
         * @brief 컴포넌트 겹침 종료 이벤트입니다.
         */
        FOverlapDelegate mOnComponentEndOverlap{};

        /**
         * @brief 컴포넌트 충돌 이벤트입니다.
         */
        FHitDelegate mOnComponentHit{};
    };

}
