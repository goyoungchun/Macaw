# Echo / Aperture Relay

Echo가 칼데라 관측소를 복구하는 단일 레벨입니다. 충격 시험에서 강체와 관절 반응을 확인하고, 로버로 세 개의 게이트를 지난 뒤, 관측 렌즈의 세 초점면을 맞춰 신호를 보냅니다.

## 실행

Macaw에서 `PhysicsProj.mproject`를 연 뒤 **File → Open Level**에서 `Content/Maps/ApertureRelay.masset`을 엽니다. **Play**를 누르고 뷰포트를 클릭하면 조작할 수 있습니다.

## 시연 순서

| 구간 | 조작 | 화면에서 확인할 것 |
| --- | --- | --- |
| 충격 시험 | `WASD`와 마우스로 오른쪽의 주황색 시험대에 접근하고 `F` | Echo Witness의 전신 래그돌과 센서 케이스의 강체 운동. 같은 위치에서 `F`를 다시 눌러 시험을 초기화하고 반복할 수 있습니다. |
| 부분 래그돌 | 걷는 동안 `Q`를 눌러 켜고 끕니다 | 플레이어 Echo의 오른팔이 물리 자세와 애니메이션 사이에서 블렌딩됩니다. `Shift`는 달리기, `Space`는 점프입니다. |
| 칼데라 주행 | 주황색 로버 옆에서 `E`, `WASD`로 주행, `Space`로 제동 | Kestrel의 바퀴, 조향, 중앙 게이트의 램프 서스펜션. 게이트 3개를 통과하고 파란 도킹 구역에서 `E`를 누릅니다. `R`은 차량을 최근 게이트 뒤로 복구합니다. |
| 초점 시험 | 도킹 후 관측 벤치에 걸어가 `F`. `1`·`2`·`3`으로 근거리·중거리·원거리, `C`로 f/1.2와 f/11, `V`로 DOF 켜기·끄기 | 같은 구도에서 선명한 대상이 이동하고 조리개 값에 따라 배경 흐림이 달라집니다. |
| 신호 전송 | 세 초점면을 확인한 뒤 `F` | 정면의 관측 배열에서 빛의 고리가 연달아 퍼져 나가고 화면에 **RELAY ONLINE**이 나타납니다. `Backspace`는 전체 미션을 초기화합니다. |

## 에디터 시연

- Content Browser에서 `ApertureRelay/Echo/EchoAperture_PhysicsAsset.masset`을 열거나, `EchoAperture.masset`을 우클릭해 **Physics Asset Editor**를 엽니다. Echo의 바디 18개와 제약 17개를 볼 수 있습니다. **Show Bodies**로 본별 충돌 형상을 표시하고 **Simulate**로 낙하를 재현합니다.
- 에디터 뷰포트의 **Show → Collision**에서 충돌 형상을 켭니다. 에디터 상단의 **Simulate**에서 레벨 강체를 관찰할 때도 이 뷰포트 표시를 사용할 수 있습니다.
- 콘솔에서 `stat physics`, `stat ragdoll`, `stat cloth`를 각각 켜면 시간과 카운터를 표시할 수 있습니다. `ShowFlag.DepthOfField off`와 `ShowFlag.DepthOfField on`으로 에디터 표시 플래그를 비교합니다.
- Echo의 스카프 끝은 NvCloth로 움직입니다. 캐릭터 래그돌 에셋과 별도로 상체 바디 다섯 개를 담은 `EchoScarfCollision.masset`을 스카프 충돌에 사용합니다.

레벨, 메시·재질·물리 에셋과 `Content/Scripts/ApertureRelay.lua`로 구성되어 있습니다.
