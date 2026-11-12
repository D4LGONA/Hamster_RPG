// 2026-06-11 : protocol.h 파일 제작. 현재는 로그인과 이동 동기화만 구현한다.
// 2026-06-16: CS_CHAT, SC_CHAT 추가.

#pragma once

#include <cstdint>

constexpr std::uint16_t PORT_NUM = 4000;

constexpr int NAME_SIZE = 20;
constexpr int MAX_USER = 10000;
constexpr int CHAT_SIZE = 256;

// spawn 패킷의 엔티티 종류. 클라가 어떤 프리팹으로 스폰할지 구분한다.
constexpr std::uint8_t ENTITY_PLAYER = 0;
constexpr std::uint8_t ENTITY_MONSTER = 1;
constexpr std::uint8_t ENTITY_HOUSE = 2;

// 집 한 변의 길이. 화면에 세로 10유닛이 보이고 플레이어가 1x1이라, 3이면
// 플레이어의 3배 크기로 건물답게 보이면서 주변 시야도 남는다.
constexpr float HOUSE_SIZE = 3.0f;

// 문은 집 남쪽 벽 한가운데. 이 폭 안에서 벽에 닿으면 들어간다.
// 그림에서도 여기에 문을 그려야 보이는 것과 실제가 맞는다.
constexpr float HOUSE_DOOR_HALF_WIDTH = 0.6f;   // 문 폭 1.2

// 집을 나갔을 때 서는 자리. 문 바로 앞이 아니라 옆으로 비켜 세운다.
// 문 앞에 세우면 나가자마자 위로 걷고 있던 입력 때문에 곧바로 다시 들어가 버린다.
constexpr float HOUSE_EXIT_OFFSET = 2.4f;                 // 집 중심에서 아래로
constexpr float HOUSE_EXIT_SIDE_OFFSET = 1.0f;            // 문 기둥에서 옆으로 (문 폭 밖)

// 집 안(인테리어) 공간의 크기. 월드와 좌표계가 분리돼 있고 여기 안에서만 움직인다.
// 카메라 orthographic size가 5라 세로로 10유닛이 보인다. 플레이어 스프라이트가 1x1이므로
// 중심을 4.5에서 막으면 몸 끝이 화면 끝에 딱 닿는다. 바닥은 이보다 넓게 깔아서
// 어느 화면 비율에서도 빈 곳이 안 보이게 한다.
constexpr float HOUSE_INTERIOR_HALF_X = 4.5f;
constexpr float HOUSE_INTERIOR_HALF_Y = 4.5f;
constexpr float HOUSE_ENTRY_Y = -3.0f;   // 들어갔을 때 서는 자리(아래쪽)


enum class PACKET_TYPE : std::uint16_t
{
    // Client -> Server
    CS_LOGIN = 1,
    CS_INPUT,     // 서버 권위 이동: 위치가 아니라 '입력 방향'을 보낸다.
    CS_LOGOUT,
    CS_CHAT,

    // Server -> Client
    SC_LOGIN_RESULT,
    SC_SPAWN,
    SC_DESPAWN,
	SC_MOVEOBJECT,
    SC_CHAT,
    SC_STAT_UPDATE,

    // 2026-07-17: 전투 추가. 값이 클라(Protocol.cs)와 물려 있으므로 중간에 끼워넣지 말고
    // 반드시 맨 뒤에만 붙일 것. CS/SC가 섞이는 건 감수한다.
    CS_ATTACK,      // 11
    SC_DAMAGE,      // 12
    SC_DEATH,       // 13

    // 2026-07-18: 하우징. 마찬가지로 맨 뒤에만 붙일 것.
    CS_HOUSE_ACTION,   // 14  T키. 설치/입장/퇴장은 서버가 상황 보고 정한다
    SC_ZONE_CHANGE,    // 15  존이 바뀌었다(월드 <-> 집 안). 클라는 씬을 바꾼다
    SC_HOUSE_RESULT,   // 16  설치 시도 결과
};

// 모든 패킷 맨 앞에 붙는 헤더(size + type)의 크기. 4바이트.
constexpr int PACKET_HEADER_SIZE = sizeof(std::uint16_t) + sizeof(PACKET_TYPE);

#pragma pack(push, 1)
// ==============================
// Client -> Server
// ==============================

struct CS_LOGIN_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    char name[NAME_SIZE];
};

// 서버 권위 이동: 클라는 자기 위치가 아니라 '이동 방향'만 보낸다(정규화, 정지=0,0).
// 실제 위치 계산은 서버의 게임 틱 루프가 담당한다.
struct CS_INPUT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    float dx;
    float dy;
};

// T키 한 번. 지금 집 안이면 나가고, 밖인데 집이 있으면 들어가고, 없으면 그 자리에 짓는다.
// 무엇을 할지는 서버가 정하므로 클라가 보낼 내용이 없다.
struct CS_HOUSE_ACTION_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;
};

struct CS_LOGOUT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;
};

struct CS_CHAT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;
    char message[CHAT_SIZE];
};

// 공격 요청. 방향 공격이라 대상을 클라가 고르지 않는다.
// 클라는 '어느 쪽을 휘둘렀는지'만 보내고, 그 부채꼴에 누가 걸렸는지 · 사거리 · 쿨다운 ·
// 데미지는 전부 서버가 판정한다. dx/dy는 정규화된 방향.
struct CS_ATTACK_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    float dx;
    float dy;
};

// ==============================
// Server -> Client
// ==============================

struct SC_LOGIN_RESULT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint8_t success;
    std::uint32_t id;

    float x;
    float y;
};

struct SC_SPAWN_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t id;
    char name[NAME_SIZE];

    float x;
    float y;

    std::uint8_t entityType;   // ENTITY_PLAYER / ENTITY_MONSTER
};

struct SC_DESPAWN_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t id;
};

struct SC_MOVE_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t id;

    float x;
    float y;
};

struct SC_CHAT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;
    std::uint32_t id;
    char name[NAME_SIZE];
    char message[CHAT_SIZE];
};

// 누가 누구를 때려서 얼마가 깎였는지. 맞은 쪽을 보고 있는 사람들에게 보낸다.
// 남은 hp를 같이 실어보내서 클라가 hp를 따로 계산하지 않게 한다(서버가 유일한 정답).
struct SC_DAMAGE_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t attackerId;
    std::uint32_t targetId;

    int damage;
    int targetHp;
    int targetMaxHp;
};

struct SC_DEATH_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t id;
};

// 존 이동. zoneId가 0이면 월드, 그 외에는 그 집의 id.
// x, y는 옮겨간 존에서의 시작 좌표다(월드 좌표와 집 안 좌표는 별개의 좌표계).
struct SC_ZONE_CHANGE_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t zoneId;

    float x;
    float y;
};

// 집 설치 시도 결과. 실패 사유를 클라가 그대로 문구로 보여준다.
struct SC_HOUSE_RESULT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint8_t success;
    std::uint8_t reason;   // 0 성공 / 1 다른 집과 너무 가까움 / 2 월드 밖 / 3 몬스터 자리 / 4 장애물(나무 등) 위
};

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

#pragma pack(pop)