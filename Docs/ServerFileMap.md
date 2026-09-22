# 서버 파일 지도

> 기준일: 2026-09-22 (커밋 `d7adbe4`, GameServer.cpp 분리 직후)  
> 목적: 서버 코드에서 "이 기능은 어느 파일에 있지?"를 바로 찾기 위한 지도.

---

## 1. 한눈에 보기

```text
Server/
├─ main.cpp                  서버 시작점
├─ stdafx.h / .cpp           미리 컴파일된 헤더 (Winsock, STL)
├─ protocol.h                패킷 정의 (클라 Protocol.cs와 짝)
├─ NetworkTypes.h            EXT_OVER (IOCP용 OVERLAPPED 확장), BUFSIZE
├─ Session.h / .cpp          클라이언트 연결 하나 (소켓, recv 버퍼, Send/Recv)
│
├─ GameServer.h              GameServer 클래스 선언 (전부 여기)
├─ GameServer.cpp            네트워크 뼈대
├─ GameServer_Packet.cpp     패킷 처리
├─ GameServer_View.cpp       시야처리(AOI)
├─ GameServer_Monster.cpp    몬스터
├─ GameServer_Combat.cpp     전투
├─ GameServer_House.cpp      하우징
├─ GameServer_Tick.cpp       게임 틱
│
├─ Player.h / .cpp           플레이어 상태 (위치, 뷰리스트, 스탯)
├─ PlayerStats.h             스탯, 경험치, 레벨업
├─ Monster.h                 몬스터 엔티티, 행동 타입
├─ House.h                   집 엔티티
└─ SectorGrid.h              섹터 격자 (AOI 후보 찾기)
```

`GameServer`는 클래스 **하나**이고, 구현만 `GameServer_*.cpp` 여러 파일에 나눠 두었다.
함수를 추가할 때는 선언은 `GameServer.h`에, 구현은 성격에 맞는 파일에 넣는다.

---

## 2. GameServer 구현 파일별 내용

| 파일 | 담당 | 주요 함수 |
|---|---|---|
| `GameServer.cpp` | 네트워크 뼈대, 세션 관리 | `Run`, `Initialize`, `PostAccept`, `WorkerThread`, `SendPacket`, `BroadcastPacket`, `DisconnectPlayer`, `FindSession`, `RemoveSession` |
| `GameServer_Packet.cpp` | 패킷 파싱과 클라 요청 처리 | `ReadPacketInfo`, `ProcessPacket`, `ProcessLogin`, `PickSpawnPosition`, `ProcessInput`, `ProcessChat`, `SendStatUpdate` |
| `GameServer_View.cpp` | 시야처리(AOI) | `InViewRange`, `WithinRadius`, `InRange`, `MakeSpawnPacket`(×3), `GetEntityPosition`, `ClearView`, `PopulateWorldView` |
| `GameServer_Monster.cpp` | 몬스터 스폰, AI, 리스폰 | `SpawnInitialMonsters`, `StepWander`, `StepPatrol`, `StepChase`, `KillMonster`, `UpdateMonsterRespawn`, `UpdateMonsters` |
| `GameServer_Combat.cpp` | 공격 | `ProcessAttack`, `GatherMonstersInArc` |
| `GameServer_House.cpp` | 하우징 | `ProcessHouseAction`, `PlaceHouse`, `EnterHouse`, `ExitHouse`, `IsAtHouseDoor`, `IsBlockedByHouse` |
| `GameServer_Tick.cpp` | 게임 틱, 월드 업데이트 | `GameTickThread`, `UpdateWorld`, `ReportTickProfile` |

---

## 3. 스레드와 락

| 스레드 | 하는 일 | 시작 위치 |
|---|---|---|
| 워커 스레드 (여러 개) | IOCP 완료 처리: Accept, Recv(패킷 처리), Send 정리 | `GameServer.cpp` `WorkerThread` |
| 게임 틱 스레드 (1개) | 이동 적용, 시야 갱신, 몬스터 AI | `GameServer_Tick.cpp` `GameTickThread` |

| 락 | 보호하는 것 |
|---|---|
| `sessionsLock` | `sessions` (연결 목록) |
| `playersLock` | `players`, `monsters`, `houses`, `sectorGrid`, `loginRng` |

- 두 락을 같이 잡아야 하면 **`playersLock` → `sessionsLock`** 순서를 지킨다. (반대로 잡으면 데드락)
- 보통은 `playersLock` 안에서 보낼 패킷을 모아 두고, **락을 푼 뒤에** 전송한다.
- 함수 위 주석에 "playersLock을 잡은 상태에서 호출할 것"이라고 적힌 함수는 안에서 락을 잡지 않는다.

---

## 4. 이런 걸 찾을 때는 여기

| 찾는 것 | 파일 |
|---|---|
| 새 패킷 타입 추가 | `protocol.h` (정의) → `GameServer_Packet.cpp` `ProcessPacket` (switch 분기) |
| 패킷이 쪼개져 올 때 처리 | `GameServer.cpp` `WorkerThread`의 RECV 부분 + `GameServer_Packet.cpp` → [PacketReassembly.md](PacketReassembly.md) |
| 접속 끊기 / 세션 삭제 | `GameServer.cpp` `DisconnectPlayer`, `RemoveSession` → [SessionLifetime.md](SessionLifetime.md) |
| 시야 범위 바꾸기 | `GameServer_View.cpp` `InViewRange` |
| 몬스터 행동 추가 | `Monster.h` (행동 타입) → `GameServer_Monster.cpp` (`Step*` 함수) |
| 데미지 계산 | `GameServer_Combat.cpp` `ProcessAttack`, 스탯은 `PlayerStats.h` |
| 서버 틱 성능 측정 | `GameServer_Tick.cpp` `ReportTickProfile` |

---

## 5. 다른 문서

- [Design.md](Design.md): 캐릭터 스탯 및 성장 시스템 설계
- [Roadmap.md](Roadmap.md): 로드맵
- [PacketReassembly.md](PacketReassembly.md): 패킷 재조립
- [SessionLifetime.md](SessionLifetime.md): 세션 수명 관리

---

## 6. 앞으로 정리할 것

- `UpdateWorld`(`GameServer_Tick.cpp`)가 함수 하나로 약 500줄이다.
  "이동 적용 / 시야 갱신 / 패킷 전송" 같은 작은 함수로 쪼개는 것이 다음 정리 대상이다.
