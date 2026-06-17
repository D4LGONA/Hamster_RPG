# 캐릭터 스탯 및 성장 시스템 설계안

> 기준일: 2026-06-16  
> 수정 내용: `attackSpeed`를 초기 스탯에서 제거하고, 공격 주기는 서버의 고정 공격 쿨타임으로 관리하도록 변경했다.  
> 목적: 현재 구현된 C++ 서버 + Unity 클라이언트 멀티플레이 구조 위에 캐릭터 스탯, 전투, 성장, 장비, 강화 시스템을 안정적으로 확장하기 위한 설계안이다.

---

## 1. 설계 목표

캐릭터 스탯 시스템의 목표는 다음과 같다.

```text
서버가 캐릭터의 실제 능력치를 관리한다.
클라이언트는 서버에서 받은 값을 UI에 표시한다.
전투, 레벨업, 장비, 강화, 랭킹은 모두 스탯 시스템을 기반으로 동작한다.
```

즉, 캐릭터 스탯은 이후 게임 기능의 중심 데이터가 된다.

---

## 2. 기본 원칙

### 2.1 서버 권한 구조

캐릭터 스탯은 반드시 서버가 권한을 가진다.

클라이언트가 직접 계산해도 되는 것:

- UI 표시
- 체력바 애니메이션
- 데미지 숫자 이펙트
- 예상 전투력 미리보기

서버가 반드시 계산해야 하는 것:

- 현재 HP
- 데미지
- 사망 여부
- 경험치 획득
- 레벨업
- 장비 스탯 반영
- 강화 성공 여부
- 최종 전투력

### 2.2 기본 스탯과 최종 스탯 분리

스탯은 다음처럼 나누는 것이 좋다.

```text
BaseStats       : 레벨과 직업이 가지는 기본 능력치
EquipmentStats  : 장비에서 오는 능력치
BuffStats       : 버프, 음식, 스킬 효과에서 오는 능력치
FinalStats      : 실제 전투에 사용되는 최종 능력치
```

이렇게 나누면 나중에 장비, 버프, 스킬을 추가해도 구조가 덜 꼬인다.

### 2.3 공격속도는 초기 스탯에서 제외

초기 구현에서는 `attackSpeed`를 캐릭터 스탯으로 두지 않는다.

이유는 다음과 같다.

```text
초기 전투는 서버 권한 데미지 판정과 쿨타임 검증이 더 중요하다.
공격속도를 스탯으로 넣으면 애니메이션, 서버 쿨타임, 클라이언트 입력 타이밍을 함께 맞춰야 한다.
무기 종류, 직업, 스킬이 생기기 전에는 공격속도의 차별성이 크지 않다.
```

따라서 1차 전투 구현에서는 서버에 고정 공격 쿨타임을 둔다.

```cpp
constexpr float DEFAULT_ATTACK_COOLDOWN = 0.7f;
```

이후 무기 타입이나 스킬이 추가되면 `attackSpeed` 대신 다음 중 하나로 확장한다.

```text
attackCooldown      : 공격 간 최소 시간
attackInterval      : 서버가 허용하는 공격 주기
weaponAttackDelay   : 무기별 공격 딜레이
```

초기에는 스탯창에 공격속도를 표시하지 않는다.

---

## 3. 추천 스탯 목록

### 3.1 기본 표시 스탯

| 스탯 | 설명 |
|---|---|
| `level` | 캐릭터 레벨 |
| `exp` | 현재 경험치 |
| `requiredExp` | 다음 레벨까지 필요한 경험치 |
| `gold` | 보유 골드 |
| `hp` | 현재 체력 |
| `maxHp` | 최대 체력 |
| `attack` | 공격력 |
| `defense` | 방어력 |
| `moveSpeed` | 이동속도 |
| `combatPower` | 전투력 |

### 3.2 이후 확장용 스탯

| 스탯 | 설명 |
|---|---|
| `criticalRate` | 치명타 확률 |
| `criticalDamage` | 치명타 데미지 배율 |
| `accuracy` | 명중률 |
| `evasion` | 회피율 |
| `hpRegen` | 체력 회복량 |
| `cooldownReduction` | 스킬 쿨타임 감소 |
| `attackCooldown` | 무기/직업이 생긴 뒤 추가할 공격 주기 스탯 |

처음부터 너무 많은 스탯을 구현할 필요는 없다.  
초기에는 `HP`, `Attack`, `Defense`, `MoveSpeed`, `CombatPower` 정도만 구현하는 것이 좋다.

---

## 4. 서버 클래스 설계

### 4.1 PlayerStats 구조체

서버에 다음 구조를 추가하는 것을 추천한다.

```cpp
struct PlayerStats
{
    int level = 1;
    int exp = 0;
    int requiredExp = 100;
    int gold = 0;

    int hp = 100;
    int maxHp = 100;
    int attack = 10;
    int defense = 3;

    float moveSpeed = 5.0f;

    int combatPower = 0;
};
```

### 4.2 Player 클래스에 추가

```cpp
class Player
{
private:
    std::uint32_t id;
    std::string name;
    float x;
    float y;
    bool loggedIn;

    PlayerStats stats;

    double lastAttackTime = 0.0;

public:
    PlayerStats& GetStats();
    const PlayerStats& GetStats() const;

    double GetLastAttackTime() const;
    void SetLastAttackTime(double time);
};
```

단순 구현 단계에서는 `Player`가 직접 `PlayerStats`를 들고 있어도 충분하다.  
공격 쿨타임 검증용 시간은 스탯이 아니라 전투 상태 데이터로 분리한다.

---

## 5. 스탯 계산 구조

### 5.1 추천 계산 흐름

최종 스탯은 아래 순서로 계산한다.

```text
1. 레벨 기반 기본 스탯 계산
2. 장비 스탯 더하기
3. 강화 수치 반영
4. 버프 스탯 더하기
5. 최종 스탯 확정
6. 전투력 계산
```

### 5.2 기본 스탯 예시

레벨업 시 다음처럼 증가시킬 수 있다.

```cpp
maxHp = 100 + (level - 1) * 20;
attack = 10 + (level - 1) * 3;
defense = 3 + (level - 1) * 1;
moveSpeed = 5.0f;
```

처음에는 복잡한 공식보다 단순한 선형 증가가 좋다.

### 5.3 전투력 계산식

초기 전투력은 단순 가중치 합산 방식이 좋다.

```cpp
combatPower = attack * 10
            + defense * 6
            + maxHp * 1
            + level * 30;
```

이 공식은 밸런스용으로 언제든 수정 가능하다.  
처음에는 랭킹 표시와 성장 체감용으로만 사용하면 된다.

---

## 6. 클라이언트 표시용 구조

Unity 클라이언트에도 서버 패킷을 읽기 위한 구조를 둔다.

```csharp
public struct PlayerStatsData
{
    public int level;
    public int exp;
    public int requiredExp;
    public int gold;

    public int hp;
    public int maxHp;
    public int attack;
    public int defense;

    public float moveSpeed;

    public int combatPower;
}
```

클라이언트에서는 이 값을 직접 조작하지 않고, 서버에서 받은 값을 UI에 표시하는 용도로만 사용한다.

---

## 7. 패킷 설계

### 7.1 필요한 패킷

| 패킷 | 방향 | 역할 |
|---|---|---|
| `SC_STAT_UPDATE` | Server → Client | 내 캐릭터 스탯 갱신 |
| `SC_REMOTE_STAT_UPDATE` | Server → Client | 다른 플레이어의 일부 정보 갱신 |
| `CS_STAT_REQUEST` | Client → Server | 상태창 열 때 최신 스탯 요청 |

처음에는 `SC_STAT_UPDATE` 하나만 있어도 충분하다.

### 7.2 SC_STAT_UPDATE 예시

```cpp
struct SC_STAT_UPDATE_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t id;

    int level;
    int exp;
    int requiredExp;
    int gold;

    int hp;
    int maxHp;
    int attack;
    int defense;

    float moveSpeed;

    int combatPower;
};
```

이 패킷은 다음 시점에 보내면 된다.

```text
로그인 성공 직후
레벨업 직후
피격 후 HP 변경
장비 착용/해제 후
강화 성공 후
골드 변경 후
```

---

## 8. 상태창 UI 설계

### 8.1 하이어라키 예시

```text
Canvas
└── StatusPanel
    ├── Background
    ├── NameText
    ├── LevelText
    ├── ExpText
    ├── HpText
    ├── AttackText
    ├── DefenseText
    ├── MoveSpeedText
    ├── CombatPowerText
    └── CloseButton
```

### 8.2 동작 방식

- 기본 상태에서는 `StatusPanel` 비활성화
- `C` 키 또는 UI 버튼을 누르면 열기
- 열릴 때 `CS_STAT_REQUEST` 전송 가능
- 서버가 `SC_STAT_UPDATE` 전송
- 클라이언트가 UI 갱신

초기 구현에서는 `SC_STAT_UPDATE`를 로그인 직후 한 번만 받아도 된다.

---

## 9. HP 처리

### 9.1 서버 처리

서버는 플레이어의 현재 HP를 관리한다.

```cpp
void Player::TakeDamage(int damage)
{
    int finalDamage = damage - stats.defense;

    if (finalDamage < 1)
    {
        finalDamage = 1;
    }

    stats.hp -= finalDamage;

    if (stats.hp < 0)
    {
        stats.hp = 0;
    }
}
```

### 9.2 클라이언트 처리

클라이언트는 서버에서 받은 HP를 UI에 표시한다.

```text
HP Text: 80 / 100
HP Bar : 0.8
```

클라이언트가 임의로 HP를 깎으면 안 된다.

---

## 10. 경험치 및 레벨업

### 10.1 경험치 지급 흐름

```text
1. 몬스터 사망
2. 서버가 처치한 플레이어 확인
3. 경험치 지급
4. 필요 경험치 이상이면 레벨업
5. 기본 스탯 재계산
6. HP 회복 여부 결정
7. SC_STAT_UPDATE 전송
```

### 10.2 레벨업 예시

```cpp
void Player::AddExp(int amount)
{
    stats.exp += amount;

    while (stats.exp >= stats.requiredExp)
    {
        stats.exp -= stats.requiredExp;
        stats.level += 1;
        stats.requiredExp = 100 + (stats.level - 1) * 50;

        RecalculateStats();
        stats.hp = stats.maxHp;
    }
}
```

---

## 11. 장비 스탯

### 11.1 EquipmentStats 구조

```cpp
struct EquipmentStats
{
    int attack = 0;
    int defense = 0;
    int maxHp = 0;
    float moveSpeed = 0.0f;
};
```

### 11.2 장비 예시

```text
낡은 검
- 공격력 +3
- 강화당 공격력 +3
```

### 11.3 장비 반영 방식

```cpp
finalAttack = baseAttack + weaponAttack + enhanceBonus;
```

초기에는 무기 1개만 구현해도 충분하다.

---

## 12. 강화 시스템 설계

### 12.1 강화 규칙

| 항목 | 값 |
|---|---|
| 시작 비용 | 50 Gold |
| 비용 증가 | 강화 시도마다 2배 |
| 성공률 | `max(10%, 100% - 강화수치 × 10%)` |
| 성공 보상 | 무기 공격력 +3 |
| 실패 패널티 | 골드만 소모 |
| 파괴/하락 | 없음 |

### 12.2 서버 처리 흐름

```text
1. 클라이언트가 CS_ENHANCE_REQUEST 전송
2. 서버가 골드 확인
3. 서버가 성공률 계산
4. 서버가 랜덤 판정
5. 골드 차감
6. 성공 시 강화 수치 증가
7. 장비 스탯 재계산
8. SC_ENHANCE_RESULT 전송
9. SC_STAT_UPDATE 전송
```

### 12.3 패킷 예시

```cpp
struct SC_ENHANCE_RESULT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint8_t success;
    int enhanceLevel;
    int weaponAttack;
    int cost;
    int successRate;
};
```

---

## 13. 전투 시스템과 스탯 연결

### 13.1 공격 요청

```text
Client → Server: CS_ATTACK
```

클라이언트는 공격 버튼을 눌렀다는 사실만 보낸다.

### 13.2 서버 판정

서버는 다음을 검사한다.

```text
대상이 존재하는가?
대상이 공격 범위 안에 있는가?
공격 쿨타임이 지났는가?
플레이어가 살아 있는가?
몬스터가 살아 있는가?
```

### 13.3 공격 쿨타임 검증

초기 전투는 공격속도 스탯 없이 고정 쿨타임으로 처리한다.

```cpp
constexpr double DEFAULT_ATTACK_COOLDOWN = 0.7;

void GameServer::ProcessAttack(std::uint32_t playerId, const CS_ATTACK_PACKET* packet)
{
    double now = GetServerTimeSeconds();

    Player* player = FindPlayer(playerId);

    if (player == nullptr)
    {
        return;
    }

    if (now - player->GetLastAttackTime() < DEFAULT_ATTACK_COOLDOWN)
    {
        return;
    }

    player->SetLastAttackTime(now);

    // 이후 공격 대상 검증 및 데미지 계산
}
```

이 구조가 있으면 클라이언트가 공격 패킷을 과하게 보내도 서버가 무시할 수 있다.

### 13.4 데미지 계산

초기에는 단순하게 간다.

```cpp
int damage = player.attack - monster.defense;

if (damage < 1)
{
    damage = 1;
}
```

이후 치명타, 스킬 계수, 속성 데미지를 추가할 수 있다.

---

## 14. DB 저장 설계

### 14.1 저장할 데이터

| 데이터 | 설명 |
|---|---|
| playerId | 플레이어 고유 ID |
| name | 캐릭터 이름 |
| level | 레벨 |
| exp | 경험치 |
| gold | 골드 |
| hp | 현재 HP |
| x, y | 마지막 위치 |
| weaponId | 착용 무기 |
| enhanceLevel | 무기 강화 수치 |

### 14.2 저장 시점

```text
로그아웃 시
일정 시간마다 자동 저장
레벨업 직후
강화 성공/실패 직후
아이템 획득 직후
```

초기에는 로그아웃 저장만 구현하고, 이후 주기적 저장을 추가하면 된다.

---

## 15. 추천 구현 순서

### 1단계: 스탯 데이터만 추가

```text
서버 PlayerStats 추가
로그인 시 기본 스탯 생성
SC_STAT_UPDATE 전송
Unity 상태창 UI에 표시
```

### 2단계: HP와 전투력 표시

```text
HP / MaxHP 표시
Attack / Defense 표시
CombatPower 표시
```

### 3단계: 몬스터와 데미지

```text
Monster 클래스 추가
몬스터 HP 추가
CS_ATTACK 추가
서버 공격 쿨타임 검증
서버 데미지 계산
SC_MONSTER_HIT 전송
```

### 4단계: 경험치와 레벨업

```text
몬스터 사망 시 경험치 지급
레벨업 처리
스탯 재계산
SC_STAT_UPDATE 전송
```

### 5단계: 장비와 강화

```text
무기 데이터 추가
장비 공격력 반영
강화 요청/결과 패킷 추가
강화 후 스탯 재계산
```

### 6단계: DB 저장

```text
캐릭터 정보 저장
로그인 시 로드
로그아웃 시 저장
```

### 7단계: 공격속도 확장

무기 종류, 직업, 스킬이 생긴 뒤에 공격속도 개념을 추가한다.

```text
단검: 공격 쿨타임 짧음
검: 기본 쿨타임
도끼: 공격 쿨타임 김
스태프: 스킬 쿨타임 중심
```

이때도 `attackSpeed`라는 표시 스탯보다는 `attackCooldown` 또는 `weaponAttackDelay`처럼 서버 판정에 바로 쓰이는 값으로 관리하는 편이 좋다.

---

## 16. 최종 방향

이 프로젝트에서 캐릭터 스탯 시스템은 단순히 숫자를 보여주는 기능이 아니라, 이후 모든 RPG 기능의 중심이 된다.

따라서 처음부터 너무 복잡하게 만들기보다는 다음 방향을 추천한다.

```text
작게 시작한다.
서버 권한을 유지한다.
UI는 표시 전용으로 둔다.
공격속도는 초기 스탯에서 제외한다.
공격 주기는 서버의 고정 쿨타임으로 검증한다.
레벨업, 장비, 강화는 모두 RecalculateStats()로 연결한다.
패킷은 SC_STAT_UPDATE 하나로 먼저 단순화한다.
```

초기 목표는 다음 정도면 충분하다.

```text
레벨 1 캐릭터 생성
HP 100
공격력 10
방어력 3
이동속도 5
전투력 계산
상태창 표시
```

그 다음 몬스터와 전투를 붙이면 자연스럽게 게임 구조가 확장된다.
