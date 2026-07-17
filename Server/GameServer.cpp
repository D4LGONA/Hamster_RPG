// 2026-06-11: GameServer.cpp 파일 제작. GameServer 클래스 구현.

#include "stdafx.h"
#include "GameServer.h"

GameServer::~GameServer()
{
    if (listenSocket != INVALID_SOCKET)
    {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }

    if (iocpHandle != nullptr)
    {
        CloseHandle(iocpHandle);
        iocpHandle = nullptr;
    }

    WSACleanup();
}

void GameServer::Run()
{
    if (Initialize() == false)
    {
        return;
    }

    PostAccept();

    // 월드에 몬스터 배치. (아직 워커/틱 스레드가 없어 락 불필요)
    SpawnInitialMonsters();

    // 서버 권위 이동: 게임 틱 루프 스레드. 위치 계산/시야 갱신은 오직 이 스레드에서만 한다.
    std::thread tickThread(&GameServer::GameTickThread, this);

    std::vector<std::thread> workerThreads;
    const unsigned int threadCount = max(1u, std::thread::hardware_concurrency());

    for (unsigned int i = 0; i < threadCount; ++i)
    {
        workerThreads.emplace_back(&GameServer::WorkerThread, this);
    }

    for (auto& worker : workerThreads)
    {
        worker.join();
    }

    running = false;
    tickThread.join();
}

bool GameServer::Initialize()
{
    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        PrintServerError("WSAStartup failed");
        return false;
    }

    listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        PrintServerError("WSASocket failed");
        return false;
    }

    SOCKADDR_IN serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT_NUM);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        PrintServerError("bind failed");
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        PrintServerError("listen failed");
        return false;
    }

    iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

    if (iocpHandle == nullptr)
    {
        PrintServerError("CreateIoCompletionPort failed");
        return false;
    }

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket), iocpHandle, 0, 0) == nullptr)
    {
        PrintServerError("CreateIoCompletionPort listen socket failed");
        return false;
    }

    std::cout << "[SERVER] Start. Port = " << PORT_NUM << std::endl;
    return true;
}

void GameServer::PostAccept()
{
    SOCKET acceptSocket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);

    if (acceptSocket == INVALID_SOCKET)
    {
        PrintServerError("accept socket create failed");
        return;
    }

    EXT_OVER* acceptOver = new EXT_OVER();
    acceptOver->SetupAccept(acceptSocket);

    DWORD bytes = 0;
    BOOL result = AcceptEx(listenSocket, acceptSocket, acceptOver->buffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, &acceptOver->overlapped);

    if (result == FALSE)
    {
        int error = WSAGetLastError();

        if (error != ERROR_IO_PENDING)
        {
            PrintServerError("AcceptEx failed");
            closesocket(acceptSocket);
            delete acceptOver;
        }
    }
}

void GameServer::WorkerThread()
{
    while (true)
    {
        DWORD numBytes = 0;
        ULONG_PTR key = 0;
        WSAOVERLAPPED* overlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(iocpHandle, &numBytes, &key, &overlapped, INFINITE);
        EXT_OVER* extOver = reinterpret_cast<EXT_OVER*>(overlapped);

        if (result == FALSE || extOver == nullptr)
        {
            if (extOver != nullptr)
            {
                IO_TYPE ioType = extOver->ioType;

                if (key != 0)
                {
                    DisconnectPlayer(static_cast<std::uint32_t>(key));
                }

                if (ioType == IO_TYPE::ACCEPT || ioType == IO_TYPE::SEND)
                {
                    delete extOver;
                }
            }
            else if (key != 0)
            {
                DisconnectPlayer(static_cast<std::uint32_t>(key));
            }

            continue;
        }

        if (extOver->ioType == IO_TYPE::ACCEPT)
        {
            SOCKET clientSocket = extOver->acceptSocket;
            setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&listenSocket), sizeof(listenSocket));

            std::uint32_t playerId = IssuePlayerId();

            auto session = std::make_unique<Session>(clientSocket);
            session->SetPlayerId(playerId);

            if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), iocpHandle, playerId, 0) == nullptr)
            {
                PrintServerError("CreateIoCompletionPort client socket failed");
                closesocket(clientSocket);
                delete extOver;
                PostAccept();
                continue;
            }

            Session* sessionPtr = session.get();

            {
                std::lock_guard<std::mutex> lock(sessionsLock);
                sessions.emplace(playerId, std::move(session));
            }

            std::cout << "[ACCEPT] playerId = " << playerId << std::endl;

            if (sessionPtr->Recv() == false)
            {
                DisconnectPlayer(playerId);
            }

            delete extOver;
            PostAccept();
        }
        else if (extOver->ioType == IO_TYPE::RECV)
        {
            std::uint32_t playerId = static_cast<std::uint32_t>(key);

            if (numBytes == 0)
            {
                DisconnectPlayer(playerId);
                continue;
            }

            ProcessPacket(playerId, extOver->buffer, static_cast<int>(numBytes));

            Session* session = FindSession(playerId);

            if (session != nullptr && session->Recv() == false)
            {
                DisconnectPlayer(playerId);
            }

            // RECV extOver는 Session 멤버 recvOver라서 delete 하지 않는다.
        }
        else if (extOver->ioType == IO_TYPE::SEND)
        {
            delete extOver;
        }
    }
}

bool GameServer::ReadPacketInfo(const char* data, int bytes, std::uint16_t& packetSize, PACKET_TYPE& packetType)
{
    if (data == nullptr)
    {
        return false;
    }

    if (bytes < static_cast<int>(sizeof(std::uint16_t) + sizeof(PACKET_TYPE)))
    {
        return false;
    }

    std::memcpy(&packetSize, data, sizeof(std::uint16_t));
    std::memcpy(&packetType, data + sizeof(std::uint16_t), sizeof(PACKET_TYPE));

    if (packetSize < sizeof(std::uint16_t) + sizeof(PACKET_TYPE))
    {
        return false;
    }

    if (packetSize > bytes)
    {
        return false;
    }

    return true;
}

void GameServer::ProcessPacket(std::uint32_t playerId, const char* data, int bytes)
{
    int offset = 0;

    while (offset < bytes)
    {
        std::uint16_t packetSize = 0;
        PACKET_TYPE packetType{};

        if (ReadPacketInfo(data + offset, bytes - offset, packetSize, packetType) == false)
        {
            return;
        }

        const char* packetData = data + offset;

        switch (packetType)
        {
        case PACKET_TYPE::CS_LOGIN:
            if (packetSize == sizeof(CS_LOGIN_PACKET))
            {
                ProcessLogin(playerId, reinterpret_cast<const CS_LOGIN_PACKET*>(packetData));
            }
            break;

        case PACKET_TYPE::CS_CHAT:
            if (packetSize == sizeof(CS_CHAT_PACKET))
            {
                ProcessChat(playerId, reinterpret_cast<const CS_CHAT_PACKET*>(packetData));
            }
            break;

        case PACKET_TYPE::CS_INPUT:
            if (packetSize == sizeof(CS_INPUT_PACKET))
            {
                ProcessInput(playerId, reinterpret_cast<const CS_INPUT_PACKET*>(packetData));
            }
            break;

        case PACKET_TYPE::CS_LOGOUT:
            if (packetSize == sizeof(CS_LOGOUT_PACKET))
            {
                DisconnectPlayer(playerId);
            }
            break;

        default:
            break;
        }

        offset += packetSize;
    }
}

// playersLock을 잡은 상태에서 호출할 것.
SC_SPAWN_PACKET GameServer::MakeSpawnPacket(const Player& player)
{
    SC_SPAWN_PACKET spawn{};
    spawn.size = sizeof(SC_SPAWN_PACKET);
    spawn.type = PACKET_TYPE::SC_SPAWN;
    spawn.id = player.GetId();
    strncpy_s(spawn.name, sizeof(spawn.name), player.GetName().c_str(), _TRUNCATE);
    spawn.x = player.GetX();
    spawn.y = player.GetY();
    spawn.entityType = ENTITY_PLAYER;
    return spawn;
}

SC_SPAWN_PACKET GameServer::MakeSpawnPacket(const Monster& monster)
{
    SC_SPAWN_PACKET spawn{};
    spawn.size = sizeof(SC_SPAWN_PACKET);
    spawn.type = PACKET_TYPE::SC_SPAWN;
    spawn.id = monster.GetId();
    strncpy_s(spawn.name, sizeof(spawn.name), "Monster", _TRUNCATE);
    spawn.x = monster.GetX();
    spawn.y = monster.GetY();
    spawn.entityType = ENTITY_MONSTER;
    return spawn;
}

// 두 점이 주어진 반경 안인지. sqrt 없이 제곱 거리로 비교(비용 절약).
bool GameServer::WithinRadius(float ax, float ay, float bx, float by, float radius)
{
    float dx = ax - bx;
    float dy = ay - by;
    return (dx * dx + dy * dy) <= (radius * radius);
}

// 두 점이 시야 범위 안인지.
bool GameServer::InRange(float ax, float ay, float bx, float by)
{
    return WithinRadius(ax, ay, bx, by, VIEW_RANGE);
}

bool GameServer::InViewRange(const Player& a, const Player& b)
{
    return InRange(a.GetX(), a.GetY(), b.GetX(), b.GetY());
}

// player/monster 공통 위치 조회. id로 종류를 구분한다.
bool GameServer::GetEntityPosition(std::uint32_t id, float& x, float& y)
{
    if (IsMonsterId(id))
    {
        auto iter = monsters.find(id);
        if (iter == monsters.end()) { return false; }
        x = iter->second.GetX();
        y = iter->second.GetY();
        return true;
    }

    auto iter = players.find(id);
    if (iter == players.end()) { return false; }
    x = iter->second.GetX();
    y = iter->second.GetY();
    return true;
}

// 서버 시작 시 월드에 몬스터를 배치한다. 아직 다른 스레드가 없을 때 호출(락 불필요).
// 여기 좌표가 각 몬스터의 '집'이 되고, 배회 반경/순찰 경유지/복귀 지점이 전부 이걸 기준으로 잡힌다.
void GameServer::SpawnInitialMonsters()
{
    struct SpawnEntry
    {
        MonsterBehavior behavior;
        float x;
        float y;
    };

    const SpawnEntry entries[] = {
        { MonsterBehavior::Wander,  6.0f,  0.0f },
        { MonsterBehavior::Wander, -6.0f,  0.0f },
        { MonsterBehavior::Patrol,  0.0f,  6.0f },
        { MonsterBehavior::Chase,   0.0f, -6.0f },
        { MonsterBehavior::Wander,  9.0f,  9.0f },
    };

    for (const SpawnEntry& entry : entries)
    {
        std::uint32_t id = nextMonsterId++;
        monsters.emplace(id, Monster{ id, entry.behavior, entry.x, entry.y, 30 });
        sectorGrid.Add(id, entry.x, entry.y);
    }

    std::cout << "[MONSTER] spawned " << monsters.size() << " monsters" << std::endl;
}

// 배회 방향을 새로 뽑는다. 계속 움직이면 부자연스러워서 절반은 그냥 쉰다.
void GameServer::PickWanderDirection(Monster& monster)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    if (chance(wanderRng) < MONSTER_IDLE_CHANCE)
    {
        monster.SetDir(0.0f, 0.0f);
    }
    else
    {
        constexpr float TWO_PI = 6.2831853f;
        std::uniform_real_distribution<float> angleDist(0.0f, TWO_PI);

        float angle = angleDist(wanderRng);
        monster.SetDir(std::cos(angle), std::sin(angle));
    }

    std::uniform_real_distribution<float> durationDist(MONSTER_WANDER_MIN_SEC, MONSTER_WANDER_MAX_SEC);
    monster.SetWanderTimer(durationDist(wanderRng));
}

// 배회: 주기적으로 방향을 새로 뽑고, 스폰 지점에서 너무 멀어지면 되돌아온다.
bool GameServer::StepWander(Monster& monster, float deltaSeconds, float& outX, float& outY)
{
    // 방향 전환 타이머
    float timer = monster.GetWanderTimer() - deltaSeconds;

    if (timer <= 0.0f)
    {
        PickWanderDirection(monster);   // 타이머도 여기서 새로 채워진다
    }
    else
    {
        monster.SetWanderTimer(timer);
    }

    // 배회 반경을 벗어나면 방향을 스폰 지점 쪽으로 강제한다.
    // 타이머 갱신 뒤에 하므로, 방금 '쉬기'가 뽑혔어도 반경 밖이면 이게 이긴다(밖에서 눌러앉지 않게).
    float awayX = monster.GetX() - monster.GetSpawnX();
    float awayY = monster.GetY() - monster.GetSpawnY();
    float awaySqr = awayX * awayX + awayY * awayY;

    if (awaySqr > MONSTER_LEASH_RADIUS * MONSTER_LEASH_RADIUS)
    {
        float dist = std::sqrt(awaySqr);   // 반경 밖이라 0이 될 수 없다
        monster.SetDir(-awayX / dist, -awayY / dist);
    }

    float dirX = monster.GetDirX();
    float dirY = monster.GetDirY();

    if (dirX == 0.0f && dirY == 0.0f)
    {
        return false;   // 쉬는 중
    }

    outX = monster.GetX() + dirX * MONSTER_MOVE_SPEED * deltaSeconds;
    outY = monster.GetY() + dirY * MONSTER_MOVE_SPEED * deltaSeconds;
    return true;
}

// 순찰: 스폰 지점 기준 상대좌표 경로(MONSTER_PATROL_PATH)의 경유지를 순서대로 돈다.
bool GameServer::StepPatrol(Monster& monster, float deltaSeconds, float& outX, float& outY)
{
    std::size_t index = monster.GetPatrolIndex();

    if (index >= MONSTER_PATROL_COUNT)
    {
        index = 0;   // 경로를 줄이는 수정이 들어와도 터지지 않게
    }

    float targetX = monster.GetSpawnX() + MONSTER_PATROL_PATH[index].x;
    float targetY = monster.GetSpawnY() + MONSTER_PATROL_PATH[index].y;

    float toX = targetX - monster.GetX();
    float toY = targetY - monster.GetY();
    float distSqr = toX * toX + toY * toY;

    float step = MONSTER_MOVE_SPEED * deltaSeconds;

    // 도착 판정은 이렇게 '남은 거리'로 직접 한다.
    // 아래 return false(=이번 틱에 움직이지 않았다)를 도착 신호로 재활용하면 안 된다.
    // 지금은 첫 경유지가 스폰 지점이라 시작하자마자 그 상황이 되는데, 둘을 엮어놨으면
    // 인덱스가 안 넘어가서 그 자리에 영영 붙어있게 된다.
    if (distSqr <= step * step)
    {
        monster.SetPatrolIndex((index + 1) % MONSTER_PATROL_COUNT);

        if (distSqr <= MONSTER_ARRIVE_DIST * MONSTER_ARRIVE_DIST)
        {
            return false;   // 이미 경유지 위. 다음 틱부터 새 목표로 움직인다.
        }

        outX = targetX;   // 한 틱 거리 안이면 지나치지 말고 딱 붙인다
        outY = targetY;
        return true;
    }

    float dist = std::sqrt(distSqr);   // 위에서 걸러졌으니 0이 아니다
    outX = monster.GetX() + (toX / dist) * step;
    outY = monster.GetY() + (toY / dist) * step;
    return true;
}

// 추격: 감지 범위 안에 플레이어가 들어오면 쫓아가고, 놓치면 스폰 지점으로 돌아간다.
// players를 읽으므로 반드시 playersLock을 잡은 채로(=틱 스레드에서) 불러야 한다.
bool GameServer::StepChase(Monster& monster, float deltaSeconds, float& outX, float& outY)
{
    std::uint32_t targetId = monster.GetTargetId();

    // 1) 쫓던 타겟이 아직 유효한가. 접속을 끊었거나, 뿌리쳤거나, 내가 집에서 너무 멀리 왔으면 접는다.
    if (targetId != 0)
    {
        auto targetIter = players.find(targetId);

        bool lost = (targetIter == players.end());

        if (lost == false)
        {
            const Player& target = targetIter->second;

            lost = WithinRadius(monster.GetX(), monster.GetY(), target.GetX(), target.GetY(), MONSTER_CHASE_GIVEUP_RADIUS) == false
                || WithinRadius(monster.GetX(), monster.GetY(), monster.GetSpawnX(), monster.GetSpawnY(), MONSTER_CHASE_LEASH_RADIUS) == false;
        }

        if (lost)
        {
            targetId = 0;
            monster.SetTargetId(0);
        }
    }

    // 2) 타겟이 없으면 감지 범위 안에서 가장 가까운 플레이어를 새로 잡는다.
    //    players 전체 순회 대신 섹터그리드로 주변 후보만 본다(시야처리와 같은 격자 재활용).
    //    단 집에서 이미 리쉬 밖으로 나와 있으면 잡지 않는다. 이 조건이 없으면 1)이 리쉬로 풀어놓은
    //    타겟을 바로 그 자리에서 다시 물어서(플레이어는 아직 감지 범위 안에 있으므로) 리쉬가
    //    아무 일도 못 하고, 플레이어가 몬스터를 매달고 월드 끝까지 끌고 갈 수 있다.
    if (targetId == 0
        && WithinRadius(monster.GetX(), monster.GetY(), monster.GetSpawnX(), monster.GetSpawnY(), MONSTER_CHASE_LEASH_RADIUS))
    {
        std::vector<std::uint32_t> nearby;
        sectorGrid.GatherNearby(monster.GetX(), monster.GetY(), nearby);

        float bestSqr = MONSTER_DETECT_RADIUS * MONSTER_DETECT_RADIUS;

        for (std::uint32_t otherId : nearby)
        {
            if (IsMonsterId(otherId))
            {
                continue;   // 몬스터끼리는 싸우지 않는다
            }

            auto playerIter = players.find(otherId);

            if (playerIter == players.end())
            {
                continue;
            }

            float dx = playerIter->second.GetX() - monster.GetX();
            float dy = playerIter->second.GetY() - monster.GetY();
            float distSqr = dx * dx + dy * dy;

            if (distSqr <= bestSqr)
            {
                bestSqr = distSqr;
                targetId = otherId;
            }
        }

        monster.SetTargetId(targetId);
    }

    // 3) 타겟이 있으면 그쪽으로, 없으면 집으로.
    float goalX = monster.GetSpawnX();
    float goalY = monster.GetSpawnY();
    float stopDist = MONSTER_ARRIVE_DIST;

    if (targetId != 0)
    {
        const Player& target = players.find(targetId)->second;   // 위 1)/2)에서 존재가 보장된다

        goalX = target.GetX();
        goalY = target.GetY();
        stopDist = MONSTER_CHASE_STOP_DIST;   // 몸이 겹치지 않게 조금 앞에서 멈춘다
    }

    float toX = goalX - monster.GetX();
    float toY = goalY - monster.GetY();
    float distSqr = toX * toX + toY * toY;

    if (distSqr <= stopDist * stopDist)
    {
        return false;   // 이미 붙었다(또는 집에 도착했다)
    }

    float dist = std::sqrt(distSqr);   // 위에서 걸러졌으니 0이 아니다
    float travel = dist - stopDist;    // 멈출 지점까지만. 지나쳐서 앞뒤로 떠는 걸 막는다
    float step = MONSTER_MOVE_SPEED * deltaSeconds;

    if (travel > step)
    {
        travel = step;
    }

    outX = monster.GetX() + (toX / dist) * travel;
    outY = monster.GetY() + (toY / dist) * travel;
    return true;
}

// 몬스터 한 틱: 행동 타입별로 이동 -> 실제로 움직인 놈만 movers에 담는다.
// 경계 clamp / 섹터그리드 갱신은 타입과 무관하므로 여기서 공통 처리한다.
// playersLock을 잡은 상태에서 호출할 것.
void GameServer::UpdateMonsters(float deltaSeconds, std::vector<std::uint32_t>& movers)
{
    for (auto& [id, monster] : monsters)
    {
        if (monster.IsAlive() == false)
        {
            continue;
        }

        float newX = 0.0f;
        float newY = 0.0f;
        bool moved = false;

        switch (monster.GetBehavior())
        {
        case MonsterBehavior::Wander: moved = StepWander(monster, deltaSeconds, newX, newY); break;
        case MonsterBehavior::Patrol: moved = StepPatrol(monster, deltaSeconds, newX, newY); break;
        case MonsterBehavior::Chase:  moved = StepChase(monster, deltaSeconds, newX, newY);  break;
        }

        if (moved == false)
        {
            continue;
        }

        // 월드 경계 clamp
        if (newX < WORLD_MIN) { newX = WORLD_MIN; }
        if (newX > WORLD_MAX) { newX = WORLD_MAX; }
        if (newY < WORLD_MIN) { newY = WORLD_MIN; }
        if (newY > WORLD_MAX) { newY = WORLD_MAX; }

        float oldX = monster.GetX();
        float oldY = monster.GetY();

        if (newX == oldX && newY == oldY)
        {
            continue;   // 경계에 눌려 실제 이동 없음
        }

        monster.SetPosition(newX, newY);
        sectorGrid.Move(id, oldX, oldY, newX, newY);
        movers.push_back(id);
    }
}


// 로그인 스폰 위치. 평소엔 원점 고정이고, --random-spawn을 준 부하 테스트 때만 흩어놓는다.
// playersLock을 잡은 상태에서 호출할 것(loginRng를 그 락으로 보호한다).
void GameServer::PickSpawnPosition(float& x, float& y)
{
    if (randomSpawnExtent <= 0.0f)
    {
        x = 0.0f;
        y = 0.0f;
        return;
    }

    float extent = randomSpawnExtent;

    if (extent > WORLD_MAX)
    {
        extent = WORLD_MAX;   // 월드 밖에 스폰시키지 않는다
    }

    std::uniform_real_distribution<float> dist(-extent, extent);

    x = dist(loginRng);
    y = dist(loginRng);
}

void GameServer::ProcessLogin(std::uint32_t playerId, const CS_LOGIN_PACKET* packet)
{
    std::string playerName(packet->name, strnlen_s(packet->name, NAME_SIZE));

    PlayerStats selfStats;
    SC_LOGIN_RESULT_PACKET loginResult{};
    SC_SPAWN_PACKET selfSpawn{};

    std::vector<SC_SPAWN_PACKET> spawnToMe;      // 내 화면에 띄울 주변 기존 플레이어들
    std::vector<std::uint32_t> spawnMeTargets;   // 나를 spawn해줄 주변 상대들

    {
        std::lock_guard<std::mutex> lock(playersLock);

        if (players.find(playerId) != players.end())
        {
            return;
        }

        float spawnX = 0.0f;
        float spawnY = 0.0f;
        PickSpawnPosition(spawnX, spawnY);

        Player player{ playerId, playerName, spawnX, spawnY };
        player.RecalculateStats();

        auto result = players.emplace(playerId, std::move(player));

        if (result.second == false)
        {
            return;
        }

        Player& self = result.first->second;
        selfStats = self.GetStats();

        // 섹터 격자에 등록
        sectorGrid.Add(playerId, self.GetX(), self.GetY());

        // 전체 순회 대신 주변 3x3 섹터 후보만 모아 시야 판정
        std::vector<std::uint32_t> nearby;
        sectorGrid.GatherNearby(self.GetX(), self.GetY(), nearby);

        for (std::uint32_t otherId : nearby)
        {
            if (otherId == playerId)
            {
                continue;
            }

            // 몬스터: 내 화면에만 spawn (몬스터는 클라가 없어 reciprocal 없음)
            if (IsMonsterId(otherId))
            {
                auto monsterIter = monsters.find(otherId);

                if (monsterIter == monsters.end())
                {
                    continue;
                }

                Monster& monster = monsterIter->second;

                if (InRange(self.GetX(), self.GetY(), monster.GetX(), monster.GetY()) == false)
                {
                    continue;
                }

                self.GetViewList().insert(otherId);
                monster.GetWatchers().insert(playerId);
                spawnToMe.push_back(MakeSpawnPacket(monster));   // 몬스터 -> 나
                continue;
            }

            auto otherIter = players.find(otherId);

            if (otherIter == players.end())
            {
                continue;
            }

            Player& other = otherIter->second;

            if (InViewRange(self, other) == false)
            {
                continue;
            }

            // 서로 뷰리스트에 추가
            self.GetViewList().insert(otherId);
            other.GetViewList().insert(playerId);

            spawnToMe.push_back(MakeSpawnPacket(other));   // 기존 플레이어 -> 나
            spawnMeTargets.push_back(otherId);             // 나 -> 그 상대
        }

        selfSpawn = MakeSpawnPacket(self);

        loginResult.size = sizeof(SC_LOGIN_RESULT_PACKET);
        loginResult.type = PACKET_TYPE::SC_LOGIN_RESULT;
        loginResult.success = 1;
        loginResult.id = self.GetId();
        loginResult.x = self.GetX();
        loginResult.y = self.GetY();
    }

    // ---- playersLock 해제 후 전송(락 순서: players -> sessions 유지) ----
    SendPacket(playerId, &loginResult, loginResult.size);
    SendStatUpdate(playerId, selfStats);

    for (const SC_SPAWN_PACKET& spawn : spawnToMe)
    {
        SendPacket(playerId, &spawn, spawn.size);
    }

    for (std::uint32_t targetId : spawnMeTargets)
    {
        SendPacket(targetId, &selfSpawn, selfSpawn.size);
    }

    std::cout << "[LOGIN] id = " << loginResult.id
        << ", name = " << playerName
        << ", view = " << spawnMeTargets.size() << std::endl;
}

// 서버 권위 이동: 클라의 입력 방향만 저장한다. 실제 위치 계산은 게임 틱(UpdateWorld)에서.
void GameServer::ProcessInput(std::uint32_t playerId, const CS_INPUT_PACKET* packet)
{
    std::lock_guard<std::mutex> lock(playersLock);

    auto iter = players.find(playerId);

    if (iter == players.end())
    {
        return;
    }

    iter->second.SetInput(packet->dx, packet->dy);
}

// 고정 주기(15Hz)로 도는 게임 틱 루프. 위치/시야 갱신은 오직 이 스레드에서만 한다.
void GameServer::GameTickThread()
{
    using clock = std::chrono::steady_clock;

    constexpr int TICK_HZ = 15;
    const auto tickInterval = std::chrono::nanoseconds(std::chrono::seconds(1)) / TICK_HZ;
    const float dt = 1.0f / static_cast<float>(TICK_HZ);

    auto nextTick = clock::now();
    auto nextReport = clock::now() + std::chrono::seconds(5);

    while (running)
    {
        nextTick += tickInterval;

        UpdateWorld(dt);

        if (profileEnabled && clock::now() >= nextReport)
        {
            ReportTickProfile();
            nextReport += std::chrono::seconds(5);
        }

        std::this_thread::sleep_until(nextTick);
    }
}

// 한 틱: (1) 입력 있는 플레이어 위치 적분 (2) 몬스터 배회
//        (3) 이동한 플레이어마다 시야 diff (4) 이동한 몬스터마다 시야 diff -> 락 풀고 전송.
// 위치는 (1)(2)에서 전부 확정한 뒤 diff를 돌린다. 그래야 같은 틱에 플레이어와 몬스터가
// 함께 움직여도 두 diff가 같은 좌표를 보고 같은 결론을 낸다.
void GameServer::UpdateWorld(float deltaSeconds)
{
    using profClock = std::chrono::steady_clock;
    const auto tStart = profClock::now();
    profClock::time_point tLock, t1, t2, t3, t4;

    std::vector<std::pair<std::uint32_t, SC_MOVE_PACKET>> moveSends;
    std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>> spawnSends;
    std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>> despawnSends;

    {
        std::lock_guard<std::mutex> lock(playersLock);
        tLock = profClock::now();

        // 1) 입력이 있는 플레이어의 위치를 적분한다.
        std::vector<std::uint32_t> playerMovers;

        for (auto& [id, player] : players)
        {
            float ix = player.GetInputX();
            float iy = player.GetInputY();

            if (ix == 0.0f && iy == 0.0f)
            {
                continue;
            }

            float speed = player.GetStats().moveSpeed;

            float oldX = player.GetX();
            float oldY = player.GetY();

            float newX = oldX + ix * speed * deltaSeconds;
            float newY = oldY + iy * speed * deltaSeconds;

            // 월드 경계 clamp
            if (newX < WORLD_MIN) { newX = WORLD_MIN; }
            if (newX > WORLD_MAX) { newX = WORLD_MAX; }
            if (newY < WORLD_MIN) { newY = WORLD_MIN; }
            if (newY > WORLD_MAX) { newY = WORLD_MAX; }

            if (newX == oldX && newY == oldY)
            {
                continue;   // 경계에 눌려 실제 이동 없음
            }

            player.SetPosition(newX, newY);
            sectorGrid.Move(id, oldX, oldY, newX, newY);
            playerMovers.push_back(id);
        }

        t1 = profClock::now();

        // 2) 몬스터 배회. 위치를 여기서 다 확정해두고 아래 diff들을 돌린다.
        std::vector<std::uint32_t> monsterMovers;
        UpdateMonsters(deltaSeconds, monsterMovers);

        t2 = profClock::now();

        // 3) 이동한 플레이어마다 시야 diff -> 보낼 패킷 수집.
        for (std::uint32_t moverId : playerMovers)
        {
            auto meIter = players.find(moverId);

            if (meIter == players.end())
            {
                continue;
            }

            Player& me = meIter->second;

            SC_MOVE_PACKET movePacket{};
            movePacket.size = sizeof(SC_MOVE_PACKET);
            movePacket.type = PACKET_TYPE::SC_MOVEOBJECT;
            movePacket.id = moverId;
            movePacket.x = me.GetX();
            movePacket.y = me.GetY();

            SC_SPAWN_PACKET selfSpawn = MakeSpawnPacket(me);

            // 옵션 A: 내 위치도 서버가 소유하므로, 나 자신에게도 내 이동을 보낸다.
            moveSends.emplace_back(moverId, movePacket);

            // 새 시야 계산 (주변 3x3 섹터 후보 -> 거리 판정)
            std::vector<std::uint32_t> nearby;
            sectorGrid.GatherNearby(me.GetX(), me.GetY(), nearby);

            std::unordered_set<std::uint32_t> newView;

            for (std::uint32_t otherId : nearby)
            {
                if (otherId == moverId)
                {
                    continue;
                }

                float ox, oy;
                if (GetEntityPosition(otherId, ox, oy) == false)
                {
                    continue;
                }

                if (InRange(me.GetX(), me.GetY(), ox, oy))
                {
                    newView.insert(otherId);   // 플레이어 + 몬스터 모두 포함
                }
            }

            std::unordered_set<std::uint32_t>& oldView = me.GetViewList();

            // 새로 진입 -> spawn, 계속 보임 -> 내 이동 전송
            for (std::uint32_t otherId : newView)
            {
                bool isNew = (oldView.find(otherId) == oldView.end());

                if (IsMonsterId(otherId))
                {
                    // 상대가 몬스터: 내 화면에만 spawn(reciprocal 없음).
                    // 계속 보이는 몬스터의 이동은 아래 (4) 몬스터 diff가 보내므로 여기선 안 보낸다.
                    if (isNew)
                    {
                        auto monsterIter = monsters.find(otherId);

                        if (monsterIter != monsters.end())
                        {
                            monsterIter->second.GetWatchers().insert(moverId);
                            spawnSends.emplace_back(moverId, MakeSpawnPacket(monsterIter->second)); // 몬스터 -> 나
                        }
                    }
                    continue;
                }

                if (isNew)
                {
                    auto otherIter = players.find(otherId);

                    if (otherIter != players.end())
                    {
                        otherIter->second.GetViewList().insert(moverId);
                        spawnSends.emplace_back(moverId, MakeSpawnPacket(otherIter->second)); // 상대 -> 나
                        spawnSends.emplace_back(otherId, selfSpawn);                          // 나 -> 상대
                    }
                }
                else
                {
                    moveSends.emplace_back(otherId, movePacket);   // 내 이동 -> 계속 보는 상대
                }
            }

            // 시야에서 이탈 -> despawn
            for (std::uint32_t otherId : oldView)
            {
                if (newView.find(otherId) != newView.end())
                {
                    continue;
                }

                SC_DESPAWN_PACKET despawnOther{};
                despawnOther.size = sizeof(SC_DESPAWN_PACKET);
                despawnOther.type = PACKET_TYPE::SC_DESPAWN;
                despawnOther.id = otherId;

                if (IsMonsterId(otherId))
                {
                    // 몬스터: 내 화면에서만 despawn. 몬스터의 watchers에서 나를 제거.
                    auto monsterIter = monsters.find(otherId);

                    if (monsterIter != monsters.end())
                    {
                        monsterIter->second.GetWatchers().erase(moverId);
                    }

                    despawnSends.emplace_back(moverId, despawnOther);  // 나에게 몬스터 despawn
                    continue;
                }

                auto otherIter = players.find(otherId);

                if (otherIter != players.end())
                {
                    otherIter->second.GetViewList().erase(moverId);
                }

                SC_DESPAWN_PACKET despawnMe{};
                despawnMe.size = sizeof(SC_DESPAWN_PACKET);
                despawnMe.type = PACKET_TYPE::SC_DESPAWN;
                despawnMe.id = moverId;
                despawnSends.emplace_back(otherId, despawnMe);     // 상대에게 나 despawn
                despawnSends.emplace_back(moverId, despawnOther);  // 나에게 상대 despawn
            }

            oldView = std::move(newView);   // 내 뷰리스트 갱신
        }

        t3 = profClock::now();

        // 4) 이동한 몬스터마다 시야 diff.
        // 몬스터는 viewList가 없다. 대신 watchers(나를 보는 플레이어)가 그 역할을 하므로
        // watchers를 새로 계산해서 신규 -> spawn, 유지 -> move, 이탈 -> despawn.
        // 플레이어 쪽 viewList에도 몬스터 id가 들어있으니 양쪽을 같이 맞춰준다.
        for (std::uint32_t monsterId : monsterMovers)
        {
            auto monsterIter = monsters.find(monsterId);

            if (monsterIter == monsters.end())
            {
                continue;
            }

            Monster& monster = monsterIter->second;

            SC_MOVE_PACKET movePacket{};
            movePacket.size = sizeof(SC_MOVE_PACKET);
            movePacket.type = PACKET_TYPE::SC_MOVEOBJECT;
            movePacket.id = monsterId;
            movePacket.x = monster.GetX();
            movePacket.y = monster.GetY();

            SC_SPAWN_PACKET monsterSpawn = MakeSpawnPacket(monster);

            std::vector<std::uint32_t> nearby;
            sectorGrid.GatherNearby(monster.GetX(), monster.GetY(), nearby);

            std::unordered_set<std::uint32_t> newWatchers;

            for (std::uint32_t otherId : nearby)
            {
                if (IsMonsterId(otherId))
                {
                    continue;   // 몬스터끼리는 서로 볼 필요 없다
                }

                auto playerIter = players.find(otherId);

                if (playerIter == players.end())
                {
                    continue;
                }

                if (InRange(monster.GetX(), monster.GetY(), playerIter->second.GetX(), playerIter->second.GetY()))
                {
                    newWatchers.insert(otherId);
                }
            }

            std::unordered_set<std::uint32_t>& oldWatchers = monster.GetWatchers();

            // 새로 보게 된 플레이어 -> spawn, 계속 보는 플레이어 -> 몬스터 이동 전송
            for (std::uint32_t watcherId : newWatchers)
            {
                if (oldWatchers.find(watcherId) != oldWatchers.end())
                {
                    moveSends.emplace_back(watcherId, movePacket);
                    continue;
                }

                auto playerIter = players.find(watcherId);

                if (playerIter != players.end())
                {
                    playerIter->second.GetViewList().insert(monsterId);
                    spawnSends.emplace_back(watcherId, monsterSpawn);
                }
            }

            // 시야에서 벗어난 플레이어 -> despawn
            for (std::uint32_t watcherId : oldWatchers)
            {
                if (newWatchers.find(watcherId) != newWatchers.end())
                {
                    continue;
                }

                auto playerIter = players.find(watcherId);

                if (playerIter != players.end())
                {
                    playerIter->second.GetViewList().erase(monsterId);
                }

                SC_DESPAWN_PACKET despawn{};
                despawn.size = sizeof(SC_DESPAWN_PACKET);
                despawn.type = PACKET_TYPE::SC_DESPAWN;
                despawn.id = monsterId;
                despawnSends.emplace_back(watcherId, despawn);
            }

            oldWatchers = std::move(newWatchers);   // 몬스터 watchers 갱신
        }

        t4 = profClock::now();
    }

    // ---- playersLock 해제 후 전송 (spawn -> move -> despawn 순서로 클라 처리 안전하게) ----
    for (auto& [targetId, packet] : spawnSends)
    {
        SendPacket(targetId, &packet, packet.size);
    }

    for (auto& [targetId, packet] : moveSends)
    {
        SendPacket(targetId, &packet, packet.size);
    }

    for (auto& [targetId, packet] : despawnSends)
    {
        SendPacket(targetId, &packet, packet.size);
    }

    if (profileEnabled)
    {
        auto ms = [](profClock::time_point a, profClock::time_point b)
        {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };

        const auto tEnd = profClock::now();
        double total = ms(tStart, tEnd);

        prof.samples++;
        prof.lockWait += ms(tStart, tLock);
        prof.integrate += ms(tLock, t1);
        prof.monsterAI += ms(t1, t2);
        prof.playerDiff += ms(t2, t3);
        prof.monsterDiff += ms(t3, t4);
        prof.send += ms(t4, tEnd);
        prof.total += total;

        if (total > prof.maxTotal)
        {
            prof.maxTotal = total;
        }
    }
}

// 틱의 각 단계에 쓴 시간을 평균내서 찍는다. 어디가 진짜 비싼지 봐야 뭘 분리할지 정할 수 있다.
// 예산은 한 틱당 66.7ms(15Hz)이고, avg가 여기 근접하면 틱이 밀리기 시작한다.
void GameServer::ReportTickProfile()
{
    if (prof.samples == 0)
    {
        return;
    }

    std::size_t playerCount = 0;

    {
        std::lock_guard<std::mutex> lock(playersLock);
        playerCount = players.size();
    }

    const double n = static_cast<double>(prof.samples);

    std::cout << std::fixed << std::setprecision(2)
              << "[TICK] players=" << playerCount
              << " ticks=" << prof.samples
              << " | avg " << (prof.total / n) << "ms (max " << prof.maxTotal << ")"
              << " | lock " << (prof.lockWait / n)
              << " | 1.integrate " << (prof.integrate / n)
              << " | 2.monsterAI " << (prof.monsterAI / n)
              << " | 3.playerDiff " << (prof.playerDiff / n)
              << " | 4.monsterDiff " << (prof.monsterDiff / n)
              << " | 5.send " << (prof.send / n)
              << std::endl;

    prof = TickProfile{};
}

void GameServer::ProcessChat(std::uint32_t playerId, const CS_CHAT_PACKET* packet)
{
    std::string senderName;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        senderName = iter->second.GetName();
    }

    std::string message(packet->message, strnlen_s(packet->message, CHAT_SIZE));

    if (message.empty())
    {
        return;
    }

    SC_CHAT_PACKET chatPacket{};
    chatPacket.size = sizeof(SC_CHAT_PACKET);
    chatPacket.type = PACKET_TYPE::SC_CHAT;
    chatPacket.id = playerId;

    strncpy_s(chatPacket.name, sizeof(chatPacket.name), senderName.c_str(), _TRUNCATE);
    strncpy_s(chatPacket.message, sizeof(chatPacket.message), message.c_str(), _TRUNCATE);

    BroadcastPacket(&chatPacket, chatPacket.size, 0);

    std::cout << "[CHAT] id = " << playerId
        << ", name = " << senderName
        << ", message = " << message << std::endl;
}

void GameServer::SendStatUpdate(std::uint32_t playerId, const PlayerStats& stats)
{
    SC_STAT_UPDATE_PACKET statPacket{};
    statPacket.size = sizeof(SC_STAT_UPDATE_PACKET);
    statPacket.type = PACKET_TYPE::SC_STAT_UPDATE;

    statPacket.id = playerId;

    statPacket.level = stats.level;
    statPacket.exp = stats.exp;
    statPacket.requiredExp = stats.requiredExp;
    statPacket.gold = stats.gold;

    statPacket.hp = stats.hp;
    statPacket.maxHp = stats.maxHp;
    statPacket.attack = stats.attack;
    statPacket.defense = stats.defense;

    statPacket.moveSpeed = stats.moveSpeed;

    statPacket.combatPower = stats.combatPower;

    SendPacket(playerId, &statPacket, statPacket.size);

    std::cout << "[STAT SEND] id = " << playerId
        << ", level = " << stats.level
        << ", hp = " << stats.hp << "/" << stats.maxHp
        << ", attack = " << stats.attack
        << ", defense = " << stats.defense
        << ", combatPower = " << stats.combatPower
        << std::endl;
}

void GameServer::SendPacket(std::uint32_t playerId, const void* packet, int size)
{
    std::lock_guard<std::mutex> lock(sessionsLock);

    auto iter = sessions.find(playerId);

    if (iter == sessions.end())
    {
        return;
    }

    if (iter->second->Send(packet, size) == false)
    {
        std::cout << "[SEND FAIL] playerId = " << playerId << std::endl;
    }
}

void GameServer::BroadcastPacket(const void* packet, int size, std::uint32_t exceptId)
{
    std::lock_guard<std::mutex> lock(sessionsLock);

    for (auto& [playerId, session] : sessions)
    {
        if (playerId == exceptId)
        {
            continue;
        }

        session->Send(packet, size);
    }
}

void GameServer::DisconnectPlayer(std::uint32_t playerId)
{
    bool existed = false;
    std::vector<std::uint32_t> viewers;   // 나를 시야에 두고 있던 사람들

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter != players.end())
        {
            Player& me = iter->second;

            // 섹터 격자에서 제거
            sectorGrid.Remove(playerId, me.GetX(), me.GetY());

            // 내가 보던 것들 정리. 몬스터면 그 watchers에서 나를 제거(despawn 불필요, 내가 나가니까).
            // 플레이어면 그쪽 뷰리스트에서 나를 지우고 despawn 대상으로 수집.
            for (std::uint32_t otherId : me.GetViewList())
            {
                if (IsMonsterId(otherId))
                {
                    auto monsterIter = monsters.find(otherId);

                    if (monsterIter != monsters.end())
                    {
                        monsterIter->second.GetWatchers().erase(playerId);
                    }

                    continue;
                }

                auto otherIter = players.find(otherId);

                if (otherIter != players.end())
                {
                    otherIter->second.GetViewList().erase(playerId);
                }

                viewers.push_back(otherId);
            }

            players.erase(iter);
            existed = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(sessionsLock);

        auto iter = sessions.find(playerId);

        if (iter != sessions.end())
        {
            sessions.erase(iter);
            existed = true;
        }
    }

    if (existed == false)
    {
        return;
    }

    SC_DESPAWN_PACKET despawn{};
    despawn.size = sizeof(SC_DESPAWN_PACKET);
    despawn.type = PACKET_TYPE::SC_DESPAWN;
    despawn.id = playerId;

    for (std::uint32_t targetId : viewers)
    {
        SendPacket(targetId, &despawn, despawn.size);
    }

    std::cout << "[DISCONNECT] id = " << playerId << ", despawn to " << viewers.size() << std::endl;
}

Session* GameServer::FindSession(std::uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(sessionsLock);

    auto iter = sessions.find(playerId);

    if (iter == sessions.end())
    {
        return nullptr;
    }

    return iter->second.get();
}

std::uint32_t GameServer::IssuePlayerId()
{
    return nextPlayerId.fetch_add(1);
}

void GameServer::PrintServerError(const char* message)
{
    std::cout << "[SERVER ERROR] " << message << ", error = " << WSAGetLastError() << std::endl;
}