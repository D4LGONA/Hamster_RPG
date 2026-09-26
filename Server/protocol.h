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

// 2026-09-26: 건축(가구). 설계는 Docs/FurnitureDesign.md.
// 가구는 집 안에만 존재하고 월드에는 나가지 않는다.

// 가구 종류. 클라는 프리팹을 고르는 값으로, 서버는 크기 표의 인덱스로 쓴다.
constexpr std::uint32_t FURNITURE_BED = 0;
constexpr std::uint32_t FURNITURE_DESK = 1;
constexpr std::uint32_t FURNITURE_CHAIR = 2;
constexpr std::uint32_t FURNITURE_POT = 3;
constexpr std::uint32_t FURNITURE_RUG = 4;
constexpr std::uint32_t FURNITURE_TYPE_COUNT = 5;

struct FurnitureSize
{
    float width;
    float height;
};

// 가구가 차지하는 크기. 회전이 90/270도면 가로세로를 바꿔 본다(Furniture::GetHalfExtent).
// 충돌/범위 판정은 서버만 하고, 클라는 배치 미리보기를 그리는 데 쓴다.
// 방이 ±4.5(가로세로 9)이므로 침대가 방의 약 1/4 폭이다.
constexpr FurnitureSize FURNITURE_SIZE[FURNITURE_TYPE_COUNT] =
{
    { 2.0f, 1.2f },   // 0 침대
    { 1.6f, 0.8f },   // 1 책상
    { 0.7f, 0.7f },   // 2 의자
    { 0.6f, 0.6f },   // 3 화분
    { 2.0f, 1.5f },   // 4 러그
};

// 집 하나당 가구 개수 한도.
constexpr int MAX_FURNITURE_PER_HOUSE = 30;

// 들어왔을 때 서는 자리(HOUSE_ENTRY_Y) 주변 이 반경에는 못 놓는다.
// 안 막으면 자기 집에 들어가자마자 가구에 끼어서 못 움직이는 상황이 생긴다.
constexpr float FURNITURE_ENTRY_CLEAR_RADIUS = 1.0f;

// 가구 요청 연타 방지. 같은 사람의 설치/이동/삭제 요청은 이 간격보다 촘촘하면 무시한다.
constexpr float FURNITURE_ACTION_COOLDOWN = 0.2f;

// SC_FURNITURE_RESULT의 reason. 실패했을 때만 보낸다(성공은 SPAWN/MOVED/REMOVED가 곧 성공 신호).
constexpr std::uint8_t FURNITURE_FAIL_NOT_IN_HOUSE = 1;   // 집 안이 아님
constexpr std::uint8_t FURNITURE_FAIL_NOT_OWNER = 2;      // 내 집이 아님
constexpr std::uint8_t FURNITURE_FAIL_OUT_OF_ROOM = 3;    // 방 밖으로 나감
constexpr std::uint8_t FURNITURE_FAIL_OVERLAP = 4;        // 다른 가구와 겹침
constexpr std::uint8_t FURNITURE_FAIL_BLOCKS_ENTRY = 5;   // 출입구를 막음
constexpr std::uint8_t FURNITURE_FAIL_LIMIT = 6;          // 개수 한도 초과
constexpr std::uint8_t FURNITURE_FAIL_BAD_TYPE = 7;       // 알 수 없는 가구 종류
constexpr std::uint8_t FURNITURE_FAIL_NO_SUCH = 8;        // 그런 가구 없음(id 오류)


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

    // 2026-09-26: 건축(가구). 마찬가지로 맨 뒤에만 붙일 것.
    CS_FURNITURE_PLACE,    // 17  설치 요청
    CS_FURNITURE_REMOVE,   // 18  삭제 요청
    CS_FURNITURE_MOVE,     // 19  이동(회전 포함) 요청
    SC_FURNITURE_SPAWN,    // 20  가구가 생겼다 / 집에 들어와서 받는 목록
    SC_FURNITURE_MOVED,    // 21  가구가 움직였다
    SC_FURNITURE_REMOVED,  // 22  가구가 사라졌다
    SC_FURNITURE_RESULT,   // 23  요청 실패 사유 (요청자에게만)
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

// 가구 요청 3종. houseId는 받지 않는다.
// 서버가 요청자의 zoneId를 보면 어느 집인지 알 수 있고,
// 클라가 보낸 houseId를 믿으면 남의 집에 설치하는 요청을 막을 수 없다.
struct CS_FURNITURE_PLACE_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t itemType;

    float x;
    float y;

    std::uint8_t rotation;   // 0~3
};

struct CS_FURNITURE_REMOVE_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t furnitureId;
};

struct CS_FURNITURE_MOVE_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t furnitureId;

    float x;
    float y;

    std::uint8_t rotation;   // 0~3
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
    std::uint8_t reason;   // 0 성공 / 1 다른 집과 너무 가까움 / 2 월드 밖 / 3 몬스터 자리
};

// 가구 통지 3종. 같은 집(zoneId)에 있는 사람 전원에게 보낸다(요청자 포함).
// 성공했을 때 따로 RESULT를 보내지 않고 이것들이 곧 성공 신호다.
struct SC_FURNITURE_SPAWN_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t furnitureId;
    std::uint32_t itemType;

    float x;
    float y;

    std::uint8_t rotation;
};

struct SC_FURNITURE_MOVED_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t furnitureId;

    float x;
    float y;

    std::uint8_t rotation;
};

struct SC_FURNITURE_REMOVED_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint32_t furnitureId;
};

// 요청이 실패했을 때 요청자에게만. reason은 FURNITURE_FAIL_* 참고.
struct SC_FURNITURE_RESULT_PACKET
{
    std::uint16_t size;
    PACKET_TYPE type;

    std::uint8_t reason;
};

// 클라(Protocol.cs)가 같은 바이트 수로 읽고 쓰는지 컴파일 때 못 박는다.
// 여기서 에러가 나면 Protocol.cs의 Make*/Read*도 같이 어긋난 것이다.
static_assert(sizeof(CS_FURNITURE_PLACE_PACKET) == PACKET_HEADER_SIZE + 13, "CS_FURNITURE_PLACE 크기 불일치");
static_assert(sizeof(CS_FURNITURE_REMOVE_PACKET) == PACKET_HEADER_SIZE + 4, "CS_FURNITURE_REMOVE 크기 불일치");
static_assert(sizeof(CS_FURNITURE_MOVE_PACKET) == PACKET_HEADER_SIZE + 13, "CS_FURNITURE_MOVE 크기 불일치");
static_assert(sizeof(SC_FURNITURE_SPAWN_PACKET) == PACKET_HEADER_SIZE + 17, "SC_FURNITURE_SPAWN 크기 불일치");
static_assert(sizeof(SC_FURNITURE_MOVED_PACKET) == PACKET_HEADER_SIZE + 13, "SC_FURNITURE_MOVED 크기 불일치");
static_assert(sizeof(SC_FURNITURE_REMOVED_PACKET) == PACKET_HEADER_SIZE + 4, "SC_FURNITURE_REMOVED 크기 불일치");
static_assert(sizeof(SC_FURNITURE_RESULT_PACKET) == PACKET_HEADER_SIZE + 1, "SC_FURNITURE_RESULT 크기 불일치");

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