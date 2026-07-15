// 2026-06-11 : protocol.h 파일 제작. 현재는 로그인과 이동 동기화만 구현한다.
// 2026-06-16: CS_CHAT, SC_CHAT 추가.

#pragma once

#include <cstdint>

constexpr std::uint16_t PORT_NUM = 4000;

constexpr int NAME_SIZE = 20;
constexpr int MAX_USER = 10000;
constexpr int CHAT_SIZE = 256;


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
    SC_STAT_UPDATE
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