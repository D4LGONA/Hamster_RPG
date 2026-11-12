// 2026-06-11: GameServer.h 파일 제작. IOCP 서버 실행 흐름을 관리하는 클래스.
// 2026-06-16: ProcessChat 추가.
// 2026-06-17: SendStatUpdate 추가.
#pragma once

#include "stdafx.h"
#include "protocol.h"
#include "NetworkTypes.h"
#include "Session.h"
#include "Player.h"
#include "Monster.h"
#include "House.h"
#include "SectorGrid.h"
#include "WorldGen.h"
#include "Database.h"

enum class PACKET_CHECK
{
    OK,         // 패킷 하나 완성
    NEED_MORE,  // 덜 옴 → 기다리기
    BROKEN      // 망가짐 → 끊기
};

class GameServer
{
public:
    GameServer() = default;
    ~GameServer();

    void Run();

    // 부하 테스트 전용. 로그인 위치를 원점 고정이 아니라 ±halfExtent 안 랜덤으로 흩어놓는다.
    // 0(기본)이면 평소대로 전원 원점 스폰. Run() 전에 호출할 것.
    // 봇을 전부 원점에 겹쳐놓으면 서로가 서로의 시야에 들어와 AOI가 일할 여지가 없어지므로,
    // 시야처리가 실제로 트래픽을 줄여주는지 재려면 이게 필요하다.
    void SetRandomSpawn(float halfExtent) { randomSpawnExtent = halfExtent; }

    // 틱의 각 단계가 몇 ms 먹는지 주기적으로 찍는다. 뭘 워커로 분리할지 정하려면
    // 추측이 아니라 이 숫자를 보고 골라야 한다. Run() 전에 호출할 것.
    void SetProfile(bool on) { profileEnabled = on; }

private:
    bool Initialize();
    void PostAccept();
    void WorkerThread();

    int ProcessPacket(std::uint32_t playerId, const char* data, int bytes);
    PACKET_CHECK ReadPacketInfo(const char* data, int bytes, std::uint16_t& packetSize, PACKET_TYPE& packetType);

    void ProcessLogin(std::uint32_t playerId, const CS_LOGIN_PACKET* packet);

    // 2026-11-12: DB 1단계. 로그인은 두 단계다.
    //   ProcessLogin(워커 스레드) -> DB 스레드에 불러오기 요청 -> CompleteLogin(DB 스레드)에서 월드에 넣는다.
    // DB가 없으면 ProcessLogin이 바로 CompleteLogin을 부른다(saved == nullptr, 예전과 같은 동작).
    void CompleteLogin(std::uint32_t playerId, const std::string& playerName, const CharacterData* saved, bool fromDb);
    void SendLoginFail(std::uint32_t playerId);
    CharacterData MakeSaveData(const Player& player);   // playersLock을 잡은 상태에서
    void SaveAllPlayers(const char* reason);

    // 2026-11-13: 아이템 / 반격
    void ProcessUseItem(std::uint32_t playerId, const CS_USE_ITEM_PACKET* packet);
    void SendInventory(std::uint32_t playerId, const SC_INVENTORY_PACKET& packet);
    // 처치 보상 + 드랍. ProcessAttack이 playersLock 안에서 부른다. 보낼 드랍 패킷은 dropSends에 모은다.
    void GrantKillRewards(Player& killer, const Monster& monster, bool& leveledUp,
                          std::vector<SC_ITEM_DROP_PACKET>& dropSends);
    // 틱: 바닥 아이템 줍기/사라짐, 몬스터가 붙어 있는 플레이어 물기(HP 0이면 광장으로)
    struct TickItemSends
    {
        std::vector<std::pair<std::uint32_t, SC_ITEM_REMOVE_PACKET>> removes;
        std::vector<std::pair<std::uint32_t, SC_ITEM_PICKUP_PACKET>> pickups;
        std::vector<std::pair<std::uint32_t, SC_INVENTORY_PACKET>> inventories;
        std::vector<std::pair<std::uint32_t, SC_DAMAGE_PACKET>> damages;
        std::vector<std::pair<std::uint32_t, PlayerStats>> stats;
        std::vector<std::pair<std::uint32_t, SC_PLAYER_FAINT_PACKET>> faints;
    };
    void UpdateGroundDrops(float deltaSeconds, TickItemSends& sends);
    void ClearDropsOf(std::uint32_t ownerId);   // playersLock 안에서. 집에 들어갈 때
    void UpdateMonsterAttacks(float deltaSeconds, std::vector<std::uint32_t>& playerMovers, TickItemSends& sends);
public:
    // 종료 직전 저장. 남은 DB 작업을 다 끝낼 때까지 기다린다(main.cpp 콘솔 종료 핸들러).
    void ShutdownSave();
private:
    void ProcessInput(std::uint32_t playerId, const CS_INPUT_PACKET* packet);
    void ProcessChat(std::uint32_t playerId, const CS_CHAT_PACKET* packet);
    void ProcessAttack(std::uint32_t playerId, const CS_ATTACK_PACKET* packet);
    void ProcessHouseAction(std::uint32_t playerId);

    // 방향 공격에 걸린 살아있는 몬스터 id들. playersLock을 잡은 상태에서만 호출한다.
    void GatherMonstersInArc(float originX, float originY, float dirX, float dirY,
                             std::vector<std::uint32_t>& out);

    // 서버 권위 이동: 고정 주기(15Hz)로 도는 게임 틱 루프.
    void GameTickThread();
    void UpdateWorld(float deltaSeconds);   // 한 틱: 위치 적분 + 시야 diff + 전송

    void SendStatUpdate(std::uint32_t playerId, const PlayerStats& stats);
    void SendPacket(std::uint32_t playerId, const void* packet, int size);
    void BroadcastPacket(const void* packet, int size, std::uint32_t exceptId = 0);

    // 시야처리(AOI) 헬퍼. 아래 함수들은 playersLock을 잡은 상태에서만 호출한다.
    SC_SPAWN_PACKET MakeSpawnPacket(const Player& player);
    SC_SPAWN_PACKET MakeSpawnPacket(const Monster& monster);
    static bool InViewRange(const Player& a, const Player& b);
    static bool InRange(float ax, float ay, float bx, float by);
    static bool WithinRadius(float ax, float ay, float bx, float by, float radius);   // 반경 지정 거리 비교(sqrt 없이)

    // 몬스터: 별도 맵으로 관리하되 섹터그리드/시야엔 함께 태운다.
    static bool IsMonsterId(std::uint32_t id) { return id >= MONSTER_ID_BASE; }
    bool GetEntityPosition(std::uint32_t id, float& x, float& y);   // player/monster 공통 위치 조회

    // 몬스터 AI. 아래 전부 게임 틱 스레드에서 playersLock을 잡은 상태로만 호출한다.
    void UpdateMonsters(float deltaSeconds, std::vector<std::uint32_t>& movers);

    // 무한 월드 청크. 플레이어 주변 청크를 켜서 몬스터를 만들고, 오래 아무도 없던 청크는 몬스터째 지운다.
    // 게임 틱 스레드에서 playersLock을 잡은 상태로만 호출한다.
    void UpdateChunks(float deltaSeconds, std::vector<std::uint32_t>& spawned,
                      std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends);
    void ActivateChunk(int chunkX, int chunkY, std::vector<std::uint32_t>& spawned);
    void UnloadChunk(std::int64_t key, std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends);   // 이동 -> 움직인 몬스터 수집

    // 행동 타입별 이동. 이번 틱의 목표 위치를 outX/outY에 넣고, 실제로 움직였으면 true.
    // 경계 clamp / 섹터그리드 갱신 / movers 수집은 호출부(UpdateMonsters)가 공통으로 처리하므로
    // 여기선 '어디로 갈지'만 정하면 된다.
    bool StepWander(Monster& monster, float deltaSeconds, float& outX, float& outY);
    bool StepPatrol(Monster& monster, float deltaSeconds, float& outX, float& outY);
    bool StepChase(Monster& monster, float deltaSeconds, float& outX, float& outY);

    void PickWanderDirection(Monster& monster);   // 새 방향(또는 정지) + 다음 전환 시간

    // 죽은 몬스터의 부활 타이머를 돌리고, 다 된 놈은 되살려 respawned에 담는다.
    void UpdateMonsterRespawn(float deltaSeconds, std::vector<std::uint32_t>& respawned);

    // 몬스터를 죽은 상태로 만든다(격자에서 빼고 보던 사람들의 viewList도 정리).
    // 죽음 통지 대상은 deathTargets로 돌려주므로, 호출부가 락을 푼 뒤 보낸다.
    void KillMonster(Monster& monster, std::vector<std::uint32_t>& deathTargets);

    // ---- 하우징. 아래 전부 playersLock을 잡은 상태에서만 호출한다. ----

    static bool IsHouseId(std::uint32_t id) { return id >= HOUSE_ID_BASE && id < MONSTER_ID_BASE; }

    House* FindHouseByOwner(const std::string& ownerName);

    // 집을 짓는다. 실패하면 reason에 사유를 담아 false.
    bool PlaceHouse(Player& owner, std::uint8_t& reason,
                    std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends);

    void EnterHouse(Player& player, House& house,
                    std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends,
                    std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends);

    void ExitHouse(Player& player,
                   std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends,
                   std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends);

    // 지금 보고 있는 것들과의 관계를 전부 끊는다(존을 옮기기 직전에 부른다).
    // 섹터 격자에서 빼는 것은 호출부가 상황에 맞게 한다.
    void ClearView(Player& player, std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends);

    // 그 자리에 있는 주변 것들을 서로 보이게 만든다(존에 막 들어왔을 때).
    void PopulateWorldView(Player& player, std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends);

    // (x, y)에 플레이어가 서 있을 때 집에 파묻히는가. 이동을 막는 데 쓴다.
    bool IsBlockedByHouse(float x, float y);
    // 집 + 월드 장애물(나무/덤불/돌)을 같이 본다. 플레이어와 몬스터 이동은 이걸 쓴다.
    bool IsBlocked(float x, float y) { return IsBlockedByHouse(x, y) || WorldGen::IsBlocked(x, y, PLAYER_RADIUS); }

    // (x, y)가 어느 집 문에 닿아 있는가. 닿아 있으면 그 집 id를 outHouseId에 넣고 true.
    bool IsAtHouseDoor(float x, float y, std::uint32_t& outHouseId);

    SC_SPAWN_PACKET MakeSpawnPacket(const House& house);

    void PickSpawnPosition(float& x, float& y);   // 로그인 스폰 위치. playersLock을 잡은 상태에서 호출할 것.

    void ReportTickProfile();   // 누적된 틱 프로파일을 찍고 리셋. 틱 스레드에서만 호출.

    void DisconnectPlayer(std::uint32_t playerId);   // 플레이어 정리 + 소켓 닫기 (세션은 안 지움)
    void RemoveSession(std::uint32_t playerId);      // 세션을 map에서 지우기. Recv가 걸려 있지 않을 때만

    std::shared_ptr<Session> FindSession(std::uint32_t playerId);
    std::uint32_t IssuePlayerId();
    void PrintServerError(const char* message);

private:
    HANDLE iocpHandle = nullptr;
    SOCKET listenSocket = INVALID_SOCKET;

    std::atomic<std::uint32_t> nextPlayerId{ 1 };

    // 게임 틱 루프 종료 플래그.
    std::atomic<bool> running{ true };


    // 추후 concurrency로 바꿀지 고민.
    std::mutex sessionsLock;
    std::unordered_map<std::uint32_t, std::shared_ptr<Session>> sessions;

    std::mutex playersLock;
    std::unordered_map<std::uint32_t, Player> players;

    // 몬스터. playersLock으로 players/sectorGrid와 함께 보호한다.
    // id 대역으로 종류를 구분한다(HOUSE_ID_BASE 주석의 표 참고).
    // 집이 1,000,000대를 쓰므로 몬스터는 2,000,000부터.
    static constexpr std::uint32_t MONSTER_ID_BASE = 2000000;
    std::uint32_t nextMonsterId = MONSTER_ID_BASE;
    std::unordered_map<std::uint32_t, Monster> monsters;

    // 집. 몬스터와 마찬가지로 playersLock으로 players/sectorGrid와 함께 보호한다.
    // id 대역을 나눠 놓아서 id만 보고 무엇인지 구분한다.
    //   1 ~ 999,999          플레이어
    //   1,000,000 ~ 1,999,999 집
    //   2,000,000 이상        몬스터
    static constexpr std::uint32_t HOUSE_ID_BASE = 1000000;
    std::uint32_t nextHouseId = HOUSE_ID_BASE;
    std::unordered_map<std::uint32_t, House> houses;

    static constexpr float HOUSE_HALF = HOUSE_SIZE * 0.5f;

    // 집 중심끼리 이만큼은 떨어져야 지을 수 있다. 집 크기(3) + 통로(2).
    // 단순히 '안 겹치게'만 하면 집을 딱 붙여 지어서 지나갈 틈이 없는 벽이 생긴다.
    static constexpr float HOUSE_MIN_CENTER_DISTANCE = 5.0f;

    // 충돌 판정에서 플레이어/몬스터를 점이 아니라 이만큼의 반지름으로 본다.
    static constexpr float PLAYER_RADIUS = 0.4f;

    // 문 판정 깊이.
    // 이동은 틱 단위(15Hz)라 한 번에 moveSpeed/15 = 0.33씩 건너뛴다. 그래서 벽에 막혀 멈추는
    // 지점이 딱 벽면(1.9)이 아니라 1.9 ~ 2.23 사이 아무 데나 된다. 여유를 그보다 크게 잡아야
    // "분명히 문에 붙었는데 안 들어가지는" 일이 안 생긴다.
    static constexpr float HOUSE_DOOR_REACH = HOUSE_HALF + PLAYER_RADIUS + 0.5f;   // 2.4

    // 몬스터 자리에는 집을 못 짓는다.
    // 몬스터는 죽어도 스폰 지점에서 되살아나고 멀어지면 거기로 돌아오므로,
    // 스폰 지점을 덮어버리면 그 몬스터는 영영 집 안에 갇혀 산다(플레이어는 못 들어가니 잡지도 못한다).
    static constexpr float HOUSE_MONSTER_CLEARANCE = 3.0f;

    // 몬스터 AI 설정.
    static constexpr float MONSTER_MOVE_SPEED = 2.0f;      // 플레이어(5.0)보다 느리게
    static constexpr float MONSTER_LEASH_RADIUS = 5.0f;    // 스폰 지점에서 이 거리 벗어나면 되돌아온다
    static constexpr float MONSTER_IDLE_CHANCE = 0.5f;     // 방향 전환 시 이 확률로 잠깐 쉰다
    static constexpr float MONSTER_WANDER_MIN_SEC = 1.0f;  // 한 방향(또는 정지)을 유지하는 시간
    static constexpr float MONSTER_WANDER_MAX_SEC = 3.0f;

    // 목표 지점에 이만큼 붙으면 도착으로 친다. 0으로 두면 부동소수점 오차 때문에
    // 도착 지점에서 미세하게 떨면서 매 틱 move 패킷을 쏘게 된다.
    static constexpr float MONSTER_ARRIVE_DIST = 0.1f;

    // 순찰 경로: 스폰 지점 기준 상대좌표. 순서대로 돌고 마지막에서 처음으로 순환한다.
    // 첫 점이 (0,0)이라 스폰 지점 자체가 경로의 한 모서리가 된다.
    struct PatrolPoint { float x; float y; };
    static constexpr PatrolPoint MONSTER_PATROL_PATH[] = {
        { 0.0f, 0.0f },
        { 6.0f, 0.0f },
        { 6.0f, 6.0f },
        { 0.0f, 6.0f },
    };
    static constexpr std::size_t MONSTER_PATROL_COUNT = std::size(MONSTER_PATROL_PATH);

    // 추격 설정.
    static constexpr float MONSTER_DETECT_RADIUS = 8.0f;         // 이 안에 들어온 플레이어를 쫓기 시작한다.
                                                                 // VIEW_RANGE(20)보다 작아야 플레이어가 몬스터를 먼저 보고, 다가오는 걸 눈으로 확인할 수 있다.
    static constexpr float MONSTER_CHASE_GIVEUP_RADIUS = 12.0f;  // 타겟이 이보다 멀어지면 포기.
                                                                 // 감지 반경보다 커야 경계에서 붙었다 떨어졌다 하지 않는다.
    static constexpr float MONSTER_CHASE_LEASH_RADIUS = 12.0f;   // 집에서 이보다 멀어지면 추격을 접고 복귀한다(월드 끝까지 따라가지 않게).
    static constexpr float MONSTER_CHASE_STOP_DIST = 1.0f;       // 타겟과 이 거리까지만 붙는다(몸이 겹쳐 보이지 않게).

    // 전투 설정.
    static constexpr float ATTACK_RANGE = 2.5f;          // 이 안에 있어야 때릴 수 있다. 몬스터가 붙는 거리(1.0)보다 넉넉하게.

    // 방향 공격의 부채꼴 반각. 휘두른 방향에서 좌우로 이만큼 안에 든 몬스터가 맞는다.
    // 50도면 전체 100도. 마우스로 대충 찍어도 맞을 만큼은 넓고, 등 뒤가 맞지는 않을 만큼은 좁다.
    // 비교는 매번 각도를 구하지 않고 내적과 cos값으로 한다.
    static constexpr float ATTACK_ARC_HALF_DEGREES = 50.0f;
    static constexpr float ATTACK_ARC_COS = 0.6428f;     // cos(50도)

    static constexpr float ATTACK_COOLDOWN_SEC = 0.8f;   // 공격 간격. 서버가 재서 연타를 막는다.
    static constexpr float MONSTER_RESPAWN_SEC = 10.0f;  // 죽은 뒤 부활까지
    // 처치 보상은 몬스터 종류별(GameServer_Item.cpp GrantKillRewards)

    // 배회용 난수. 게임 틱 스레드에서만 쓴다.
    std::mt19937 wanderRng{ std::random_device{}() };

    // 부하 테스트용 랜덤 스폰 범위(0이면 비활성).
    // loginRng는 워커 스레드들이 함께 쓰므로 playersLock을 잡은 채로만 건드린다.
    float randomSpawnExtent = 0.0f;
    std::mt19937 loginRng{ std::random_device{}() };

    // 틱 프로파일. 한 틱의 각 단계에 쓴 시간을 누적한다.
    // 틱 스레드만 건드리므로 락이 필요 없다.
    struct TickProfile
    {
        std::uint64_t samples = 0;
        double lockWait = 0.0;      // playersLock을 잡으려고 기다린 시간
        double integrate = 0.0;     // (1) 플레이어 위치 적분
        double monsterAI = 0.0;     // (2) 몬스터 배회
        double playerDiff = 0.0;    // (3) 플레이어 시야 diff
        double monsterDiff = 0.0;   // (4) 몬스터 시야 diff
        double send = 0.0;          // (5) 락 밖 전송
        double total = 0.0;
        double maxTotal = 0.0;
    };

    bool profileEnabled = false;
    TickProfile prof;

    // 섹터 격자도 playersLock으로 함께 보호한다(위치/뷰리스트와 항상 같이 바뀌므로). 몬스터도 여기 들어간다.
    SectorGrid sectorGrid;

    DbWorker dbWorker;
    // DB에서 캐릭터를 불러오는 중인 playerId. 그 사이에 끊기면 여기서 빠지고, CompleteLogin이 이걸 보고 버린다.
    // playersLock으로 보호.
    std::unordered_set<std::uint32_t> pendingLogins;
    float autosaveTimer = 0.0f;

    // 바닥 아이템(개인 드랍). 잡은 사람에게만 보이고 그 사람만 줍는다. playersLock으로 보호.
    struct GroundDrop
    {
        std::uint32_t ownerId;
        std::uint16_t itemId;
        std::uint16_t count;
        float x;
        float y;
        float ttl;   // 남은 시간
    };
    std::unordered_map<std::uint32_t, GroundDrop> groundDrops;
    std::uint32_t nextDropId = 1;
    std::mt19937 dropRng{ std::random_device{}() };

    static constexpr float DROP_TTL_SEC = 60.0f;          // 안 주우면 1분 뒤 사라진다
    static constexpr float DROP_PICKUP_RADIUS = 1.0f;     // 이만큼 다가가면 자동으로 줍는다
    static constexpr float MONSTER_AGGRO_SEC = 8.0f;      // 맞은 몬스터가 쫓아오는 시간
    static constexpr float MONSTER_ATTACK_RANGE = 1.3f;   // 추격은 1.0에서 멈추므로 붙으면 닿는다
    static constexpr float MONSTER_ATTACK_COOLDOWN = 1.2f;
    static constexpr float FAINT_GOLD_LOSS = 0.1f;        // 쓰러지면 골드 10%를 잃는다
    static constexpr float AUTOSAVE_INTERVAL_SEC = 30.0f;   // 서버가 갑자기 꺼져도 최대 30초만 잃는다

    // 켜진 청크. key = (chunkY << 32) | chunkX. playersLock으로 보호.
    struct ChunkState
    {
        std::vector<std::uint32_t> monsterIds;   // 이 청크가 낳은 몬스터. 돌아다니다 청크를 벗어나도 여기 소속
        float idleSeconds = 0.0f;                // 주변에 아무도 없던 시간
    };
    std::unordered_map<std::int64_t, ChunkState> chunks;
    float chunkScanTimer = 0.0f;

    static constexpr int CHUNK_ACTIVE_RADIUS = 2;            // 플레이어 청크 기준 5x5를 켠다(시야 20 + 이동 여유)
    static constexpr float CHUNK_SCAN_INTERVAL = 0.5f;       // 매 틱 할 필요 없다. 0.5초에 2.5유닛밖에 못 간다
    static constexpr float CHUNK_UNLOAD_IDLE_SEC = 30.0f;    // 이만큼 비어 있으면 몬스터째 지운다(금방 돌아오면 그대로)
};