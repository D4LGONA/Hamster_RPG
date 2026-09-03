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
};

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