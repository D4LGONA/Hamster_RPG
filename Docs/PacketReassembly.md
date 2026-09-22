# 패킷 재조립 (Packet Reassembly)

> 기준일: 2026-09-22  
> 커밋: `a48ea4b`  
> 목적: TCP 스트림에서 쪼개지거나 붙어서 도착한 패킷을 올바른 경계로 잘라 처리한다.

---

## 1. 왜 필요한가

TCP는 바이트의 **순서**만 보장하고, 보낸 단위(메시지 경계)는 보장하지 않는다.
클라이언트가 10바이트 패킷 3개를 보내도 서버는 이렇게 받을 수 있다.

```text
클라가 보낸 것:  [로그인 10] [이동 10] [채팅 10]

Recv 1번째:  [로그인 10][이동 앞 4]      ← 14바이트
Recv 2번째:  [이동 뒤 6][채팅 10]        ← 16바이트
```

기존 코드는 Recv 때마다 버퍼를 비우고 0번 칸부터 다시 받았다.
그래서 `이동 앞 4`가 사라지고, 다음 Recv의 `이동 뒤 6`을 헤더로 잘못 읽었다.
이후 그 세션의 패킷은 전부 어긋나서 읽혔다.

---

## 2. 해결 방법

> 덜 온 꼬리는 버리지 않고 버퍼 맨 앞으로 당겨 두고, 다음 Recv는 그 뒤에 이어서 받는다.

```text
Recv 1번째 후:  [이동앞4 | _ _ _ ...]      savedBytes = 4
                          ↑ 다음 Recv는 여기부터

Recv 2번째 후:  [이동앞4 | 이동뒤6 | 채팅10]  → total 20, 전부 처리
                                              savedBytes = 0
```

세션마다 수신 버퍼(`recvOver.buffer`, 1024바이트)가 하나씩 있고,
그 버퍼 앞쪽에 남은 미완성 바이트 수를 `savedBytes`로 기억한다.

---

## 3. 구현

### 3.1 파일별 변경

| 파일 | 변경 내용 |
|---|---|
| `Session.h` | `savedBytes`: 처리 못 하고 버퍼 앞에 남은 바이트 수 (getter/setter) |
| `NetworkTypes.h` | `SetupRecv(savedBytes)`: 버퍼를 지우지 않고, 꼬리 뒤부터 남은 칸만큼만 받기 |
| `Session.cpp` | `Recv()`가 `SetupRecv(savedBytes)` 호출 |
| `protocol.h` | `PACKET_HEADER_SIZE` = size(2) + type(2) = 4바이트 |
| `GameServer.h` | `PACKET_CHECK` enum 추가, `ProcessPacket` 반환형 `void` → `int` |
| `GameServer.cpp` | 3가지 판정, 처리한 양 반환, 꼬리 `memmove`, 망가진 패킷이면 끊기 |

### 3.2 수신 위치 잡기 — `SetupRecv`

```cpp
void SetupRecv(int savedBytes)
{
    ZeroMemory(&overlapped, sizeof(overlapped));

    wsabuf.buf = buffer + savedBytes;   // 꼬리 뒤부터
    wsabuf.len = BUFSIZE - savedBytes;  // 남은 칸만큼

    ioType = IO_TYPE::RECV;
}
```

기존에는 `Reset()`을 불렀는데, `Reset()`은 `buffer` 전체를 0으로 지운다.
그러면 남겨 둔 꼬리가 사라지므로 RECV에서는 `overlapped`만 초기화한다.
(Accept/Send는 그대로 `Reset()`을 쓴다.)

### 3.3 패킷 하나 판정 — `ReadPacketInfo`

기존에는 `true`/`false` 두 가지라 "덜 옴"과 "망가짐"을 구분할 수 없었다.
결과를 3가지로 나눴다.

| 결과 | 조건 | 처리 |
|---|---|---|
| `OK` | 패킷 하나가 다 있음 | 처리 |
| `NEED_MORE` | `bytes < PACKET_HEADER_SIZE` (헤더도 덜 옴)<br>`packetSize > bytes` (몸통이 덜 옴) | 멈추고 다음 Recv 기다림 |
| `BROKEN` | `packetSize < PACKET_HEADER_SIZE`<br>`packetSize > BUFSIZE` | 연결 끊기 |

검사 순서가 중요하다. `packetSize > BUFSIZE` 검사가 `packetSize > bytes`보다 먼저 와야
너무 큰 패킷을 "덜 옴"으로 잘못 판정해서 영원히 기다리지 않는다.

### 3.4 버퍼 처리 — `ProcessPacket`

버퍼 앞에서부터 완성된 패킷만 처리하고, **처리한 바이트 수**를 돌려준다.

```cpp
while (offset < bytes)
{
    PACKET_CHECK check = ReadPacketInfo(data + offset, bytes - offset, ...);
    if (check == PACKET_CHECK::BROKEN)
        return -1;
    if (check == PACKET_CHECK::NEED_MORE)
        break;

    // switch로 패킷 타입별 처리
    offset += packetSize;
}
return offset;
```

`NEED_MORE`일 때 `return 0`이 아니라 `break`인 것이 핵심이다.
`[로그인 10][이동 앞 4]`에서 `return 0`을 하면 이미 처리한 로그인까지 꼬리로 남아
다음 Recv에서 **한 번 더 처리된다.** (공격 패킷이면 데미지가 두 번 들어간다.)

### 3.5 RECV 완료 처리 — `WorkerThread`

```text
1. total = savedBytes + numBytes         (남은 꼬리 + 이번에 받은 것)
2. processed = ProcessPacket(buffer, total)
3. processed < 0  → 망가진 패킷, 연결 끊기
4. FindSession으로 세션 다시 찾기          (ProcessPacket 안에서 로그아웃됐을 수 있음)
5. remain = total - processed
6. remain > 0이면 memmove(buffer, buffer + processed, remain)
7. savedBytes = remain
8. Recv() 다시 걸기                       (꼬리 뒤부터 받음)
```

- 6번에서 `memcpy`가 아니라 `memmove`를 쓴다. 옮기기 전 구간과 옮긴 뒤 구간이
  겹칠 수 있고(예: `processed = 3`, `remain = 8`), `memcpy`는 겹칠 때 결과가 보장되지 않는다.
- 4번은 `CS_LOGOUT` 처리 중 `DisconnectPlayer`로 세션이 지워진 경우를 막기 위한 것이다.

---

## 4. 망가진 패킷을 끊는 이유

서버는 헤더의 `size` 하나만 보고 다음 패킷의 시작 위치를 정한다.
`size`를 믿을 수 없으면 이후 바이트의 경계를 영영 알 수 없으므로 복구하지 않고 끊는다.

- `size`가 헤더보다 작음: 특히 `size = 0`이면 `offset`이 안 늘어나 **무한 루프**
- `size`가 버퍼보다 큼: 버퍼에 절대 다 담기지 않아 **무한 대기**.
  방치하면 버퍼가 꽉 차서 `wsabuf.len = 0`인 Recv가 0바이트로 끝나고,
  정상 종료로 잘못 처리된다.

원인은 클라이언트 버그, 서버/클라 구조체 불일치, 악의적인 클라이언트 등이다.
해당 세션만 끊어서 다른 플레이어에게 영향이 가지 않게 한다.

---

## 5. 한계와 다음 할 일

- **런타임 테스트 전**: 빌드만 확인했다. 패킷을 일부러 쪼개 보내는 테스트 클라이언트로 검증이 필요하다.
- **최대 패킷 크기 = 1024바이트(`BUFSIZE`)**: 이보다 큰 패킷은 망가진 것으로 보고 끊는다.
  지금 가장 큰 패킷(채팅)보다 충분히 크지만, 나중에 큰 패킷이 생기면 버퍼를 키우거나 링 버퍼로 바꿔야 한다.
- **세션 수명**: 세션이 `unique_ptr`라서 처리 도중 지워질 수 있고, 그래서 `FindSession`을 다시 부른다.
  `shared_ptr`로 바꾸면 이 부분을 더 안전하게 만들 수 있다.

---

## 6. 한 줄 요약

> TCP는 메시지 경계가 없어서 패킷이 쪼개지거나 붙어서 올 수 있다.
> 세션마다 수신 버퍼에 미완성 바이트를 남겨 두고, 다음 수신 데이터와 이어 붙인 뒤
> 헤더의 size로 패킷을 잘라 처리했다. size가 비정상이면 스트림 동기화를 잃은 것이므로 세션을 끊는다.
