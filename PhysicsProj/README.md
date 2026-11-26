# Third Person

프로젝트 생성 화면에서 **Third Person**을 선택하고 Play를 누르면 Lua 컨트롤러가 시작됩니다. 시작 맵은 `Content/Maps/Main.masset`이며 바닥, 외벽, 계단, 경사로, 높이가 다른 블록이 배치되어 있습니다.

| 입력 | 동작 |
|---|---|
| W / A / S / D, Shift, Z | 카메라 방향 기준 이동, 달리기, 걷기 |
| 마우스, Space | 카메라 회전, 점프 |
| C, V, 마우스 오른쪽 버튼 | 웅크리기, 수영, 조준 |
| Q, R, E, X, K | 구르기, 장전, 상호작용, 검 공격, 주문 |
| 마우스 왼쪽 버튼, F, H | 펀치 또는 사격, 춤, 피격 |
| Delete, Backspace | 사망, 시작 위치로 초기화 |

`Content/Scripts/ThirdPersonController.lua`의 `Settings`에서 이동 속도, 점프, 중력, 카메라 거리와 감도를 조정할 수 있습니다. 캐릭터의 `Controller` 컴포넌트에서 스크립트를 열 수 있습니다.

애니메이션 그래프와 Blend Space는 `Content/Animation`, 캐릭터와 모션 에셋은 `Content/Characters`, 레벨 재질은 `Content/Materials`에 있습니다.
