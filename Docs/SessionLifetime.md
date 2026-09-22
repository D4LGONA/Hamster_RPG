# 세션 수명 관리 (Session Lifetime)

> 기준일: 2026-09-22  
> 커밋: `5992795` (1단계), `c48b523` (2단계)  
> 목적: 여러 스레드와 운영체제(IOCP)가 세션을 쓰고 있는 동안 세션이 해제되지 않게 한다.

---

## 1. 왜 필요한가

서버는 IOCP 워커 스레드 여러 개와 게임 틱 스레드가 동시에 돈다.
한 세션을 두고 "쓰는 쪽"과 "지우는 쪽"이 동시에 있을 수 있다.
세션을 쓰는 쪽은 두 종류다.

1. **워커 스레드**: 패킷을 처리하는 동안 세션을 들고 있다.
2. **운영체제**: `WSARecv`를 걸어 두면 완료될 때까지 `recvOver`의 주소를 들고 있다.

기존 구조에서는 두 경우 모두 해제된 메모리에 접근할 수 있었다.

### 1.1 워커가 쓰는 중에 지워짐

```text
워커 A: session = FindSession(7)   ← 맨 포인터, 락은 함수 끝에서 풀림
워커 B: DisconnectPlayer(7)
          sessions.erase(7)        ← unique_ptr이라 즉시 delete
워커 A: session->SetSavedBytes()   ← 해제된 메모리 💥
```

### 1.2 Recv가 걸린 채로 지워짐

`recvOver`(EXT_OVER)는 **Session 객체 안에** 멤버로 들어 있다.
세션이 해제되면 `recvOver`도 같이 사라진다.

```text
(7번 Recv가 걸려 있음. 서버는 Recv가 끝나면 바로 다시 걸어서 항상 걸려 있다)

워커 B: 7번 Send 실패 → DisconnectPlayer(7) → Session delete (recvOver도 사라짐)
          ~Session → closesocket
운영체제: 걸려 있던 Recv를 실패로 완료 → 완료 통지 주소 = &recvOver (이미 해제됨)
워커 A: GQCS → extOver->ioType 읽기 💥
```

Recv 실패로 끊을 때는 Recv가 이미 끝난 뒤라 괜찮다.
**Recv가 아닌 이유(Send 실패 등)로 끊을 때** 문제가 된다.

---

## 2. 1단계: `unique_ptr` → `shared_ptr`

1.1을 막는다.

- `sessions` map: `unique_ptr<Session>` → `shared_ptr<Session>`
- `FindSession`: 맨 포인터 대신 `shared_ptr` 복사본을 돌려준다.

```text
워커 A: session = FindSession(7)   → 참조 2개 (map, A)
워커 B: sessions.erase(7)          → 참조 1개 (A) → 아직 해제 안 됨
워커 A: 처리 끝, 변수 소멸           → 참조 0개 → 이때 해제
```

락은 찾을 때만 잠깐 잡고, 쓰는 동안의 안전은 참조 개수로 보장한다.
(찾는 동안 락을 계속 잡고 있으면 다른 스레드가 모두 멈추고,
`ProcessPacket` 안의 `SendPacket`이 `sessionsLock`을 다시 잡으려 해서 데드락도 난다.)

같이 고친 것:
- ACCEPT에서 IOCP 등록 실패 시 `closesocket(clientSocket)`을 부른 뒤 `~Session`이 또 닫았다.
  **두 번 닫기**는 그 사이 다른 스레드가 같은 소켓 번호를 재사용하면 남의 소켓을 닫을 수 있다.
  소켓은 Session이 책임지므로 바깥의 `closesocket`을 지웠다.
- `unique_ptr` 시절의 `sessionPtr`(move 전에 빼 둔 맨 포인터)을 없애고 map에 복사해서 넣는다.

---

## 3. 2단계: 세션은 "Recv 완료 때만" 지운다 (지연 삭제)

1.2를 막는다. 운영체제는 `shared_ptr` 참조 개수에 잡히지 않으므로,
**Recv가 걸려 있는 동안에는 아예 지우지 않는다.**

> 규칙: **Recv를 다시 걸지 못하는 곳에서만 세션을 map에서 지운다.**

세션마다 Recv는 항상 딱 하나만 걸려 있다. 그래서

- Recv가 걸려 있으면 → 완료 통지가 **반드시** 오고, 그걸 받은 워커가 지운다.
- Recv가 안 걸려 있으면 → 올 통지가 없으므로 그 자리에서 지워도 안전하다.

### 3.1 역할 나누기

| 함수 | 하는 일 |
|---|---|
| `DisconnectPlayer` | 플레이어 정리(섹터, 시야, despawn) + **소켓 닫기만.** 세션은 map에 남긴다 |
| `RemoveSession` | 세션을 map에서 지운다. Recv가 걸려 있지 않을 때만 부른다 |

소켓을 닫으면 걸려 있던 Recv가 실패로 완료되고, 그 완료를 받은 워커가 `RemoveSession`을 부른다.

### 3.2 `RemoveSession`을 부르는 곳

| 위치 | 상황 |
|---|---|
| GQCS 실패 분기 | **실패한 I/O가 RECV일 때만.** SEND 실패면 Recv가 아직 걸려 있으므로 지우지 않는다 |
| ACCEPT | 첫 `Recv()`가 바로 실패 |
| RECV 완료 | `numBytes == 0` (상대가 끊음) |
| RECV 완료 | 망가진 패킷 (`ProcessPacket`이 -1) |
| RECV 완료 | 다시 건 `Recv()`가 바로 실패 |

GQCS 실패 분기에서 `RemoveSession` 뒤에는 `extOver`를 건드리면 안 된다(세션과 함께 해제될 수 있음).
그래서 `ioType`을 먼저 꺼내 두고 쓴다.

### 3.3 흐름 예시

**Send 실패로 끊길 때**
```text
워커 B: SEND 실패 → DisconnectPlayer(7) → 소켓 닫기 (세션은 map에 있음)
운영체제: 걸려 있던 Recv 실패로 완료
워커 A: GQCS 실패 분기, ioType == RECV → RemoveSession(7) → 이제 해제 ✅
```

**로그아웃**
```text
CS_LOGOUT → DisconnectPlayer → 소켓 닫기
ProcessPacket에서 돌아옴 → session->Recv() → 소켓이 닫혀 있어 실패
→ RemoveSession ✅
```

### 3.4 `DisconnectPlayer`가 두 번 불리는 경우

Send 실패로 한 번, 이어서 Recv 실패로 또 한 번 불릴 수 있다.
두 번째 호출에서는 `players`에 이미 없으므로 아무것도 안 한다.
세션 쪽에서는 `existed`를 세우지 않게 해서 `[DISCONNECT]` 로그가 한 번만 찍힌다.

---

## 4. 검토했다가 버린 대안

**① `EXT_OVER`에 `shared_ptr<Session> owner` 두기**
Recv를 걸 때 `owner = shared_from_this()`로 자기 자신을 붙잡고, 완료가 오면 놓는 방식.
많이 쓰는 방식이지만 `enable_shared_from_this`, `EXT_OVER` 수정, `reset` 누락 시 누수 등
신경 쓸 것이 많다. 지연 삭제는 "Recv 완료 때만 지운다" 한 규칙으로 같은 효과를 내고
건드리는 파일도 적어서 이쪽을 택했다.

**② `closesocket`만 하고 바로 지우기**
`closesocket`은 걸린 Recv를 없애는 게 아니라 **실패로 완료시키는** 것이다.
완료 통지는 나중에 반드시 오므로, 그 전에 지우면 1.2와 같은 문제가 생긴다.
그렇다고 `DisconnectPlayer` 안에서 통지를 기다리면, 통지는 다른 워커가 꺼내므로 멈출 수 있다.

**③ 세션 배열을 미리 만들어 두고 지우지 않기** (`std::array<SESSION, MAX_USER>`)
해제가 없어서 이 문제는 안 생기지만, 끊긴 칸에 새 사람이 들어온 뒤 옛 Recv 완료가 오면
데이터가 섞일 수 있다. 지금 구조(접속 시 생성, 끊기면 해제)와도 맞지 않는다.

---

## 5. 한계와 다음 할 일

- **런타임 테스트 전**: 빌드만 확인했다. 접속/끊김을 반복하는 더미 클라이언트로 검증이 필요하다.
- **`Session::socket` 동시 접근**: 스레드 A가 `Recv()`를 거는 순간 스레드 B가 `Close()`를 하면
  `socket` 변수를 동시에 건드린다. 대부분은 Recv가 실패해서 정상적으로 지워지지만,
  엄밀히 하려면 `socket`을 `std::atomic`으로 바꾸거나 `Close`에 락을 걸어야 한다.
- 끊긴 세션도 Recv 완료가 올 때까지 잠깐 map에 남는다. 그 사이 `SendPacket`은 소켓이 닫혀 있어서 실패만 하고 넘어간다.

---

## 6. 한 줄 요약

> IOCP 워커가 여러 스레드라서 사용 중인 세션이 다른 스레드에서 삭제될 수 있어,
> 세션을 `shared_ptr`로 관리했습니다. 또 Recv의 OVERLAPPED가 세션 안에 있어서
> Send 실패 등으로 끊을 때 걸려 있던 Recv 완료가 해제된 메모리로 올 수 있었습니다.
> 그래서 끊을 때는 소켓만 닫고, 세션 삭제는 Recv 완료를 받은 뒤에만 하도록 바꿨습니다.
