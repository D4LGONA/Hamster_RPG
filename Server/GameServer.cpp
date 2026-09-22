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

            Session* session = FindSession(playerId);
            if (session == nullptr) continue;

            // 버퍼 = [지난번에 남은 꼬리 | 이번에 받은 바이트]. 둘을 합친 길이만큼 처리한다.
            int total = session->GetSavedBytes() + static_cast<int>(numBytes);
            int processed = ProcessPacket(playerId, extOver->buffer, total);

            // 망가진 패킷 → 더 기다려도 소용없으니 끊는다.
            if (processed < 0)
            {
                DisconnectPlayer(playerId);
                continue;
            }

            // ProcessPacket 안에서 로그아웃됐을 수 있으니 세션을 다시 찾는다.
            session = FindSession(playerId);
            if (session == nullptr) continue;

            // 덜 온 꼬리는 버퍼 맨 앞으로 당겨 둔다. (앞뒤 구간이 겹칠 수 있어서 memmove)
            int remain = total - processed;
            if (remain > 0)
                std::memmove(extOver->buffer, extOver->buffer + processed, remain);

            // 꼬리 길이를 기억해 두면 다음 Recv가 그 뒤에 이어서 받는다.
            session->SetSavedBytes(remain);

            if (session->Recv() == false)
                DisconnectPlayer(playerId);

            // RECV extOver는 Session 멤버 recvOver라서 delete 하지 않는다.
        }
        else if (extOver->ioType == IO_TYPE::SEND)
        {
            delete extOver;
        }
    }
}

PACKET_CHECK GameServer::ReadPacketInfo(const char* data, int bytes, std::uint16_t& packetSize, PACKET_TYPE& packetType)
{
    if (data == nullptr)
        return PACKET_CHECK::BROKEN;

    // 헤더(size + type)조차 다 안 옴 → 다음 Recv 기다리기
    if (bytes < PACKET_HEADER_SIZE)
        return PACKET_CHECK::NEED_MORE;

    std::memcpy(&packetSize, data, sizeof(std::uint16_t));
    std::memcpy(&packetType, data + sizeof(std::uint16_t), sizeof(PACKET_TYPE));

    // 적힌 크기가 헤더보다 작음 → 말이 안 되는 패킷
    if (packetSize < PACKET_HEADER_SIZE)
        return PACKET_CHECK::BROKEN;

    // 적힌 크기가 너무 큼 → 말이 안 되는 패킷
    if (packetSize > BUFSIZE)
        return PACKET_CHECK::BROKEN;

    // 헤더는 왔지만 몸통이 덜 옴 → 다음 Recv 기다리기
    if (packetSize > bytes)
        return PACKET_CHECK::NEED_MORE;

    return PACKET_CHECK::OK;
}

// 버퍼 앞에서부터 완성된 패킷만 처리하고, 처리한 바이트 수를 돌려준다.
// 망가진 패킷을 만나면 -1.
int GameServer::ProcessPacket(std::uint32_t playerId, const char* data, int bytes)
{
    int offset = 0;

    while (offset < bytes)
    {
        std::uint16_t packetSize = 0;
        PACKET_TYPE packetType{};

        PACKET_CHECK check = ReadPacketInfo(data + offset, bytes - offset, packetSize, packetType);
        if (check == PACKET_CHECK::BROKEN)
            return -1;
        if (check == PACKET_CHECK::NEED_MORE)
            break; // 여기까지 처리한 양(offset)을 돌려주고, 나머지는 꼬리로 남긴다.

        const char* packetData = data + offset;

        switch (packetType)
        {
        case PACKET_TYPE::CS_LOGIN:
            if (packetSize == sizeof(CS_LOGIN_PACKET))
                ProcessLogin(playerId, reinterpret_cast<const CS_LOGIN_PACKET*>(packetData));
            break;

        case PACKET_TYPE::CS_CHAT:
            if (packetSize == sizeof(CS_CHAT_PACKET))
                ProcessChat(playerId, reinterpret_cast<const CS_CHAT_PACKET*>(packetData));
            break;

        case PACKET_TYPE::CS_INPUT:
            if (packetSize == sizeof(CS_INPUT_PACKET))
                ProcessInput(playerId, reinterpret_cast<const CS_INPUT_PACKET*>(packetData));
            break;

        case PACKET_TYPE::CS_ATTACK:
            if (packetSize == sizeof(CS_ATTACK_PACKET))
                ProcessAttack(playerId, reinterpret_cast<const CS_ATTACK_PACKET*>(packetData));
            break;

        case PACKET_TYPE::CS_HOUSE_ACTION:
            if (packetSize == sizeof(CS_HOUSE_ACTION_PACKET))
                ProcessHouseAction(playerId);
            break;

        case PACKET_TYPE::CS_LOGOUT:
            if (packetSize == sizeof(CS_LOGOUT_PACKET))
                DisconnectPlayer(playerId);
            break;

        default:
            break;
        }

        offset += packetSize;
    }

    return offset;
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
SC_SPAWN_PACKET GameServer::MakeSpawnPacket(const House& house)
{
    SC_SPAWN_PACKET spawn{};
    spawn.size = sizeof(SC_SPAWN_PACKET);
    spawn.type = PACKET_TYPE::SC_SPAWN;
    spawn.id = house.GetId();
    // 이름 칸에 주인 이름을 실어 보낸다. 클라가 "OO의 집"으로 띄운다.
    strncpy_s(spawn.name, sizeof(spawn.name), house.GetOwnerName().c_str(), _TRUNCATE);
    spawn.x = house.GetX();
    spawn.y = house.GetY();
    spawn.entityType = ENTITY_HOUSE;
    return spawn;
}

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

    if (IsHouseId(id))
    {
        auto iter = houses.find(id);
        if (iter == houses.end()) { return false; }
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

// 몬스터를 죽은 상태로 만든다. 격자에서 빼서 더는 시야 계산에 안 잡히게 하고,
// 보고 있던 사람들의 viewList도 같이 정리한다(watchers <-> viewList 불변식).
// 전송은 락 밖에서 해야 하므로 통지 대상만 모아서 돌려준다.
// playersLock을 잡은 상태에서 호출할 것.
void GameServer::KillMonster(Monster& monster, std::vector<std::uint32_t>& deathTargets)
{
    std::uint32_t monsterId = monster.GetId();

    sectorGrid.Remove(monsterId, monster.GetX(), monster.GetY());

    for (std::uint32_t watcherId : monster.GetWatchers())
    {
        auto playerIter = players.find(watcherId);

        if (playerIter != players.end())
        {
            playerIter->second.GetViewList().erase(monsterId);
        }

        deathTargets.push_back(watcherId);
    }

    monster.GetWatchers().clear();
    monster.SetRespawnTimer(MONSTER_RESPAWN_SEC);
}

// 죽은 몬스터의 부활 타이머. 다 된 놈은 집에서 hp를 채워 되살리고 격자에 다시 넣는다.
// 되살아난 id는 respawned에 담아 호출부(UpdateWorld)의 몬스터 시야 diff로 넘긴다.
// watchers가 비어 있으므로 그 diff가 주변 플레이어 전원을 '신규'로 보고 spawn을 쏴준다.
// playersLock을 잡은 상태에서 호출할 것.
void GameServer::UpdateMonsterRespawn(float deltaSeconds, std::vector<std::uint32_t>& respawned)
{
    for (auto& [id, monster] : monsters)
    {
        if (monster.IsAlive())
        {
            continue;
        }

        float timer = monster.GetRespawnTimer() - deltaSeconds;

        if (timer > 0.0f)
        {
            monster.SetRespawnTimer(timer);
            continue;
        }

        monster.Respawn();
        sectorGrid.Add(id, monster.GetX(), monster.GetY());
        respawned.push_back(id);
    }
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

        float oldX = monster.GetX();
        float oldY = monster.GetY();

        // 집을 통과하지 못하게 한다. 플레이어는 집에 못 들어가는데 몬스터만 들어갈 수 있으면
        // 집 안에 들어앉은 몬스터를 아무도 잡을 수 없게 된다.
        // 이미 안에 있다면(집이 나중에 지어진 경우) 충돌을 꺼서 걸어 나올 수 있게 둔다.
        const bool monsterStuckInside = IsBlockedByHouse(oldX, oldY);

        if (monsterStuckInside == false)
        {
            // 플레이어와 같은 방식으로 축을 따로 판정해 벽을 따라 미끄러지게 한다.
            if (IsBlockedByHouse(newX, oldY))
            {
                newX = oldX;
            }

            if (IsBlockedByHouse(newX, newY))
            {
                newY = oldY;
            }
        }

        // 월드 경계 clamp
        if (newX < WORLD_MIN) { newX = WORLD_MIN; }
        if (newX > WORLD_MAX) { newX = WORLD_MAX; }
        if (newY < WORLD_MIN) { newY = WORLD_MIN; }
        if (newY > WORLD_MAX) { newY = WORLD_MAX; }

        if (newX == oldX && newY == oldY)
        {
            continue;   // 경계나 집에 막혀 실제 이동 없음
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

    // (받을 사람, 보낼 spawn). 주변을 서로 보이게 만드는 일은 PopulateWorldView가 한다.
    std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>> spawnSends;
    std::size_t viewCount = 0;

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

        // 주변 3x3 섹터만 보고 서로 보이게 만든다(플레이어 / 몬스터 / 집).
        // 집에서 나올 때(ExitHouse)도 똑같은 일을 하므로 함수로 묶어 두었다.
        PopulateWorldView(self, spawnSends);

        viewCount = self.GetViewList().size();

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

    for (auto& [targetId, spawn] : spawnSends)
    {
        SendPacket(targetId, &spawn, spawn.size);
    }

    std::cout << "[LOGIN] id = " << loginResult.id
        << ", name = " << playerName
        << ", view = " << viewCount << std::endl;
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
    std::vector<std::pair<std::uint32_t, SC_ZONE_CHANGE_PACKET>> zoneChangeSends;

    {
        std::lock_guard<std::mutex> lock(playersLock);
        tLock = profClock::now();

        // 1) 입력이 있는 플레이어의 위치를 적분한다.
        std::vector<std::uint32_t> playerMovers;
        std::vector<std::uint32_t> houseZoneMovers;   // 집 안에서 움직인 사람

        // 문 판정 대상. 움직였는지가 아니라 '움직이려 했는지'로 모은다.
        // 문 앞에서는 벽에 막혀 위치가 안 바뀌는데, movers로 모으면 바로 그 순간이 빠진다.
        std::vector<std::uint32_t> doorCandidates;

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

            // 집 안은 좌표계가 따로다. 섹터 격자에 들어있지 않고 집과 부딪히지도 않는다.
            if (player.GetZoneId() != 0)
            {
                float insideX = std::clamp(oldX + ix * speed * deltaSeconds,
                                           -HOUSE_INTERIOR_HALF_X, HOUSE_INTERIOR_HALF_X);
                float insideY = std::clamp(oldY + iy * speed * deltaSeconds,
                                           -HOUSE_INTERIOR_HALF_Y, HOUSE_INTERIOR_HALF_Y);

                if (insideX == oldX && insideY == oldY)
                {
                    continue;
                }

                player.SetPosition(insideX, insideY);
                houseZoneMovers.push_back(id);
                continue;
            }

            doorCandidates.push_back(id);

            // 이미 집 안에 파묻힌 상태라면 충돌을 끈다. 안 그러면 양쪽 축이 다 막혀
            // 영영 못 빠져나온다(집이 생기는 순간 겹쳤다면 그럴 수 있다).
            const bool stuckInside = IsBlockedByHouse(oldX, oldY);

            // 축을 따로 판정한다. 한 축이 막혀도 다른 축으로는 미끄러지게 하려는 것이다.
            // 한 번에 판정해 막아버리면 벽에 비스듬히 붙었을 때 아예 못 움직인다.
            float newX = oldX + ix * speed * deltaSeconds;

            if (stuckInside == false && IsBlockedByHouse(newX, oldY))
            {
                newX = oldX;
            }

            float newY = oldY + iy * speed * deltaSeconds;

            if (stuckInside == false && IsBlockedByHouse(newX, newY))
            {
                newY = oldY;
            }

            // 월드 경계 clamp
            if (newX < WORLD_MIN) { newX = WORLD_MIN; }
            if (newX > WORLD_MAX) { newX = WORLD_MAX; }
            if (newY < WORLD_MIN) { newY = WORLD_MIN; }
            if (newY > WORLD_MAX) { newY = WORLD_MAX; }

            if (newX == oldX && newY == oldY)
            {
                continue;   // 경계나 집에 막혀 실제 이동 없음
            }

            player.SetPosition(newX, newY);
            sectorGrid.Move(id, oldX, oldY, newX, newY);
            playerMovers.push_back(id);
        }

        // 1-b) 문에 닿았으면 그 집으로 들어간다.
        // 닿아 있는 '순간'에만 발동시킨다(inDoorZone). 안 그러면 문 앞에 서 있는 동안
        // 매 틱 입장 처리가 돌아 존을 계속 갈아끼운다.
        for (std::uint32_t moverId : doorCandidates)
        {
            auto meIter = players.find(moverId);

            if (meIter == players.end())
            {
                continue;
            }

            Player& me = meIter->second;

            if (me.GetZoneId() != 0)
            {
                continue;
            }

            std::uint32_t doorHouseId = 0;

            if (IsAtHouseDoor(me.GetX(), me.GetY(), doorHouseId) == false)
            {
                me.SetInDoorZone(false);   // 문에서 떨어졌다. 다음에 닿으면 다시 발동
                continue;
            }

            if (me.IsInDoorZone())
            {
                continue;   // 이미 닿아 있던 상태
            }

            me.SetInDoorZone(true);

            auto doorHouseIter = houses.find(doorHouseId);

            if (doorHouseIter == houses.end())
            {
                continue;
            }

            // 남의 집 문이어도 들어간다. 같은 집에 있는 사람끼리는 서로 보이므로 방문이 된다.
            EnterHouse(me, doorHouseIter->second, spawnSends, despawnSends);

            SC_ZONE_CHANGE_PACKET zoneChange{};
            zoneChange.size = sizeof(SC_ZONE_CHANGE_PACKET);
            zoneChange.type = PACKET_TYPE::SC_ZONE_CHANGE;
            zoneChange.zoneId = doorHouseId;
            zoneChange.x = me.GetX();
            zoneChange.y = me.GetY();

            zoneChangeSends.emplace_back(moverId, zoneChange);

            std::cout << "[ZONE] id = " << moverId << " -> house " << doorHouseId
                      << " (door)" << std::endl;
        }

        t1 = profClock::now();

        // 2) 죽은 몬스터 부활 + 살아있는 몬스터 배회. 위치를 여기서 다 확정해두고 아래 diff들을 돌린다.
        // 되살아난 놈은 watchers가 비어 있어서, 아래 (4)가 주변 플레이어 전원을 신규로 보고 spawn을 쏜다.
        std::vector<std::uint32_t> monsterMovers;
        UpdateMonsterRespawn(deltaSeconds, monsterMovers);
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

            if (me.GetZoneId() != 0)
            {
                continue;   // 이번 틱에 문으로 들어간 사람. 월드 시야 처리 대상이 아니다
            }

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
                    // 계속 보이는 몬스터의 이동은 아래 (4) 몬스터 diff가 보내므로 여긴 안 보낸다.
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

                if (IsHouseId(otherId))
                {
                    // 집도 몬스터와 같다. 다만 움직이지 않으니 이동 전송이 영영 없다.
                    if (isNew)
                    {
                        auto houseIter = houses.find(otherId);

                        if (houseIter != houses.end())
                        {
                            houseIter->second.GetWatchers().insert(moverId);
                            spawnSends.emplace_back(moverId, MakeSpawnPacket(houseIter->second));   // 집 -> 나
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

                if (IsHouseId(otherId))
                {
                    auto houseIter = houses.find(otherId);

                    if (houseIter != houses.end())
                    {
                        houseIter->second.GetWatchers().erase(moverId);
                    }

                    despawnSends.emplace_back(moverId, despawnOther);  // 나에게 집 despawn
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

        // 3-b) 집 안에서 움직인 사람. 같은 집에 있는 사람끼리는 입장/퇴장 때 이미 서로
        // 보게 만들어 두었으므로, 여기서는 시야 diff 없이 이동만 뿌리면 된다.
        for (std::uint32_t moverId : houseZoneMovers)
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

            moveSends.emplace_back(moverId, movePacket);   // 내 위치도 서버가 소유한다

            for (std::uint32_t otherId : me.GetViewList())
            {
                moveSends.emplace_back(otherId, movePacket);
            }
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

    // ---- playersLock 해제 후 전송 ----
    // 존 변경이 제일 먼저다. 클라가 이걸 받고 씬을 갈아끼우므로, 새 존의 spawn이 먼저 도착하면
    // 아직 옛 씬에 오브젝트를 만들었다가 씬 로드와 함께 날려버린다.
    for (auto& [targetId, packet] : zoneChangeSends)
    {
        SendPacket(targetId, &packet, packet.size);
    }

    // 그다음 spawn -> move -> despawn 순서로 클라 처리 안전하게.
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

// 공격. 클라는 '누구를' 때릴지만 보내고 사거리/쿨다운/데미지는 전부 여기서 판정한다.
// (클라 말을 믿으면 사거리 무시하고 아무나 때리거나 연타로 순삭시킬 수 있다)
// 방향 공격. 클라는 휘두른 방향만 보내고, 누가 맞았는지는 여기서 정한다.
// 부채꼴에 걸린 몬스터는 전부 맞으므로 한 번 휘둘러 여러 마리를 잡을 수도 있다.
void GameServer::ProcessAttack(std::uint32_t playerId, const CS_ATTACK_PACKET* packet)
{
    std::vector<std::pair<std::uint32_t, SC_DAMAGE_PACKET>> damageSends;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> deathSends;   // (받을 사람, 죽은 몬스터 id)

    int killCount = 0;
    bool leveledUp = false;
    PlayerStats attackerStats;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto attackerIter = players.find(playerId);

        if (attackerIter == players.end())
        {
            return;
        }

        Player& attacker = attackerIter->second;

        // 클라가 정규화해서 보내지만 그대로 믿지 않는다. 길이가 0이면 어디를 휘둘렀는지 알 수 없다.
        float dirX = packet->dx;
        float dirY = packet->dy;

        const float length = std::sqrt(dirX * dirX + dirY * dirY);

        if (length < 0.0001f)
        {
            return;
        }

        dirX /= length;
        dirY /= length;

        // 쿨다운. 타겟팅과 달리 빗나가도 휘두른 것이므로, 통과하면 맞든 안 맞든 소모시킨다.
        const auto now = std::chrono::steady_clock::now();
        const auto sinceLast = std::chrono::duration<float>(now - attacker.GetLastAttackAt()).count();

        if (sinceLast < ATTACK_COOLDOWN_SEC)
        {
            return;
        }

        attacker.SetLastAttackAt(now);

        std::vector<std::uint32_t> hitIds;
        GatherMonstersInArc(attacker.GetX(), attacker.GetY(), dirX, dirY, hitIds);

        for (std::uint32_t hitId : hitIds)
        {
            auto monsterIter = monsters.find(hitId);

            if (monsterIter == monsters.end())
            {
                continue;
            }

            Monster& monster = monsterIter->second;

            int damage = attacker.GetStats().attack - monster.GetDefense();

            if (damage < 1)
            {
                damage = 1;   // 방어력이 높아도 최소 1은 들어가게(안 그러면 영영 못 잡는다)
            }

            monster.TakeDamage(damage);

            SC_DAMAGE_PACKET damagePacket{};
            damagePacket.size = sizeof(SC_DAMAGE_PACKET);
            damagePacket.type = PACKET_TYPE::SC_DAMAGE;
            damagePacket.attackerId = playerId;
            damagePacket.targetId = hitId;
            damagePacket.damage = damage;
            damagePacket.targetHp = monster.GetHp();
            damagePacket.targetMaxHp = monster.GetMaxHp();

            // 그 몬스터를 보고 있는 사람들에게. 때린 나도 watchers에 들어있다.
            for (std::uint32_t watcherId : monster.GetWatchers())
            {
                damageSends.emplace_back(watcherId, damagePacket);
            }

            if (monster.IsAlive() == false)
            {
                std::vector<std::uint32_t> deathTargets;
                KillMonster(monster, deathTargets);   // watchers를 비우므로 위 수집 뒤에 호출해야 한다

                for (std::uint32_t watcherId : deathTargets)
                {
                    deathSends.emplace_back(watcherId, hitId);
                }

                ++killCount;

                PlayerStats& stats = attacker.GetStats();
                stats.gold += MONSTER_GOLD_REWARD;

                if (stats.AddExp(MONSTER_EXP_REWARD))
                {
                    leveledUp = true;   // 한 번 휘둘러 여러 마리를 잡으면 레벨이 두 번 오를 수도 있다
                }
            }
        }

        if (killCount > 0)
        {
            attackerStats = attacker.GetStats();
        }
    }

    // ---- 락 해제 후 전송 ----
    for (auto& [targetPlayerId, damagePacket] : damageSends)
    {
        SendPacket(targetPlayerId, &damagePacket, damagePacket.size);
    }

    for (auto& [targetPlayerId, monsterId] : deathSends)
    {
        SC_DEATH_PACKET death{};
        death.size = sizeof(SC_DEATH_PACKET);
        death.type = PACKET_TYPE::SC_DEATH;
        death.id = monsterId;

        SendPacket(targetPlayerId, &death, death.size);
    }

    if (killCount > 0)
    {
        SendStatUpdate(playerId, attackerStats);   // 경험치/골드(레벨업했으면 스탯까지) 갱신

        if (leveledUp)
        {
            std::cout << "[LEVEL UP] playerId = " << playerId
                      << ", level = " << attackerStats.level << std::endl;
        }
    }
}

// ============================================================
//  하우징
// ============================================================

// T키 한 번. 지금 상황을 보고 설치 / 입장 / 퇴장 중 하나를 고른다.
// 무엇을 할지 클라가 정하지 않는 이유는, 집을 가졌는지 아는 쪽이 서버이기 때문이다.
void GameServer::ProcessHouseAction(std::uint32_t playerId)
{
    std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>> spawnSends;
    std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>> despawnSends;
    std::vector<std::pair<std::uint32_t, SC_MOVE_PACKET>> moveSends;

    bool sendZone = false;
    SC_ZONE_CHANGE_PACKET zoneChange{};

    bool sendResult = false;
    SC_HOUSE_RESULT_PACKET houseResult{};

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        Player& player = iter->second;

        if (player.GetZoneId() != 0)
        {
            // 집 안 -> 밖
            ExitHouse(player, spawnSends, despawnSends);

            sendZone = true;
            zoneChange.zoneId = 0;
            zoneChange.x = player.GetX();
            zoneChange.y = player.GetY();
        }
        else
        {
            House* house = FindHouseByOwner(player.GetName());

            if (house != nullptr)
            {
                // 밖 + 집 있음 -> 집으로 귀환
                std::uint32_t houseId = house->GetId();

                EnterHouse(player, *house, spawnSends, despawnSends);

                sendZone = true;
                zoneChange.zoneId = houseId;
                zoneChange.x = player.GetX();
                zoneChange.y = player.GetY();
            }
            else
            {
                // 밖 + 집 없음 -> 그 자리에 짓는다
                std::uint8_t reason = 0;
                bool placed = PlaceHouse(player, reason, spawnSends);

                sendResult = true;
                houseResult.success = placed ? 1 : 0;
                houseResult.reason = reason;

                if (placed)
                {
                    // 집에 파묻히지 않게 밖으로 밀어냈으므로 새 위치를 알려준다.
                    SC_MOVE_PACKET movePacket{};
                    movePacket.size = sizeof(SC_MOVE_PACKET);
                    movePacket.type = PACKET_TYPE::SC_MOVEOBJECT;
                    movePacket.id = playerId;
                    movePacket.x = player.GetX();
                    movePacket.y = player.GetY();

                    moveSends.emplace_back(playerId, movePacket);

                    for (std::uint32_t viewerId : player.GetViewList())
                    {
                        if (IsMonsterId(viewerId) || IsHouseId(viewerId))
                        {
                            continue;
                        }

                        moveSends.emplace_back(viewerId, movePacket);
                    }
                }
            }
        }
    }

    // ---- 락 해제 후 전송 ----

    // 존 변경을 제일 먼저 보낸다. 클라는 이걸 받고 씬을 갈아끼우는데, 새 존의 spawn이
    // 먼저 도착해 버리면 아직 옛 씬에 오브젝트를 만들었다가 씬 로드와 함께 다 날려버린다.
    // 순서를 지켜야 클라가 "지금부터 오는 것은 새 존 것"으로 받아둘 수 있다.
    if (sendZone)
    {
        zoneChange.size = sizeof(SC_ZONE_CHANGE_PACKET);
        zoneChange.type = PACKET_TYPE::SC_ZONE_CHANGE;
        SendPacket(playerId, &zoneChange, zoneChange.size);

        std::cout << "[ZONE] id = " << playerId << " -> zone " << zoneChange.zoneId << std::endl;
    }

    for (auto& [targetId, despawn] : despawnSends)
    {
        SendPacket(targetId, &despawn, despawn.size);
    }

    for (auto& [targetId, spawn] : spawnSends)
    {
        SendPacket(targetId, &spawn, spawn.size);
    }

    for (auto& [targetId, move] : moveSends)
    {
        SendPacket(targetId, &move, move.size);
    }

    if (sendResult)
    {
        houseResult.size = sizeof(SC_HOUSE_RESULT_PACKET);
        houseResult.type = PACKET_TYPE::SC_HOUSE_RESULT;
        SendPacket(playerId, &houseResult, houseResult.size);

        std::cout << "[HOUSE] id = " << playerId << ", success = " << static_cast<int>(houseResult.success)
                  << ", reason = " << static_cast<int>(houseResult.reason) << std::endl;
    }
}

// 이름으로 집을 찾는다. 집이 몇 채 안 되는 동안은 선형 탐색으로 충분하다.
// 많아지면 이름 -> houseId 맵을 따로 두면 된다.
House* GameServer::FindHouseByOwner(const std::string& ownerName)
{
    for (auto& [id, house] : houses)
    {
        if (house.GetOwnerName() == ownerName)
        {
            return &house;
        }
    }

    return nullptr;
}

// 서 있는 자리에 집을 짓는다.
bool GameServer::PlaceHouse(Player& owner, std::uint8_t& reason,
                            std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends)
{
    const float x = owner.GetX();
    const float y = owner.GetY();

    // 집이 월드 밖으로 삐져나오면 안 된다.
    if (x - HOUSE_HALF < WORLD_MIN || x + HOUSE_HALF > WORLD_MAX ||
        y - HOUSE_HALF < WORLD_MIN || y + HOUSE_HALF > WORLD_MAX)
    {
        reason = 2;
        return false;
    }

    // 집을 추가하기 전에 주변을 훑는다. 거리 검사와 spawn 대상 찾기에 같이 쓴다.
    std::vector<std::uint32_t> nearby;
    sectorGrid.GatherNearby(x, y, nearby);

    for (std::uint32_t id : nearby)
    {
        if (IsHouseId(id) == false)
        {
            continue;
        }

        auto houseIter = houses.find(id);

        if (houseIter == houses.end())
        {
            continue;
        }

        // 겹치지만 않으면 되는 게 아니라, 통로가 남을 만큼 떨어져야 한다.
        // 딱 붙여 지으면 사람이 지나갈 틈이 없는 벽이 생긴다.
        if (WithinRadius(x, y, houseIter->second.GetX(), houseIter->second.GetY(),
                         HOUSE_MIN_CENTER_DISTANCE))
        {
            reason = 1;
            return false;
        }
    }

    // 몬스터 자리인지 확인. 지금 서 있는 위치와 스폰 지점을 둘 다 본다.
    // 스폰 지점이 더 중요하다. 몬스터는 죽으면 거기서 되살아나고 멀어지면 거기로 돌아오므로,
    // 스폰 지점을 집으로 덮으면 그 몬스터는 영영 안에 갇힌다.
    for (std::uint32_t id : nearby)
    {
        if (IsMonsterId(id) == false)
        {
            continue;
        }

        auto monsterIter = monsters.find(id);

        if (monsterIter == monsters.end())
        {
            continue;
        }

        const Monster& monster = monsterIter->second;

        if (WithinRadius(x, y, monster.GetSpawnX(), monster.GetSpawnY(), HOUSE_MONSTER_CLEARANCE) ||
            (monster.IsAlive() &&
             WithinRadius(x, y, monster.GetX(), monster.GetY(), HOUSE_MONSTER_CLEARANCE)))
        {
            reason = 3;
            return false;
        }
    }

    std::uint32_t houseId = nextHouseId++;

    houses.emplace(houseId, House{ houseId, owner.GetName(), x, y });
    sectorGrid.Add(houseId, x, y);

    // 지은 사람이 자기 집에 파묻혀 못 움직이게 되지 않도록 남쪽으로 밀어낸다.
    // 문 기둥은 피해서 옆에 세운다. 문 앞에 세우면 걷기 시작하자마자 자기 집에 빨려 들어간다.
    float ownerX = x + HOUSE_EXIT_SIDE_OFFSET;
    float ownerY = y - HOUSE_EXIT_OFFSET;

    if (ownerX > WORLD_MAX) { ownerX = WORLD_MAX; }
    if (ownerY < WORLD_MIN) { ownerY = WORLD_MIN; }

    sectorGrid.Move(owner.GetId(), owner.GetX(), owner.GetY(), ownerX, ownerY);
    owner.SetPosition(ownerX, ownerY);
    owner.SetInput(0.0f, 0.0f);

    House& house = houses.find(houseId)->second;
    SC_SPAWN_PACKET houseSpawn = MakeSpawnPacket(house);

    // 주변 사람들 화면에 집을 즉시 띄운다.
    // 시야 diff는 '움직인 사람'에 대해서만 도는데, 가만히 서 있는 사람 눈앞에 집이 생기는
    // 경우는 그 diff가 돌지 않으므로 여기서 직접 보내야 한다.
    for (std::uint32_t id : nearby)
    {
        if (IsMonsterId(id) || IsHouseId(id))
        {
            continue;
        }

        auto playerIter = players.find(id);

        if (playerIter == players.end())
        {
            continue;
        }

        Player& other = playerIter->second;

        if (other.GetZoneId() != 0)
        {
            continue;
        }

        if (InRange(x, y, other.GetX(), other.GetY()) == false)
        {
            continue;
        }

        other.GetViewList().insert(houseId);
        house.GetWatchers().insert(id);
        spawnSends.emplace_back(id, houseSpawn);
    }

    reason = 0;
    return true;
}

// 월드에서 빠져 집 안 존으로 들어간다.
void GameServer::EnterHouse(Player& player, House& house,
                            std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends,
                            std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends)
{
    const std::uint32_t myId = player.GetId();

    sectorGrid.Remove(myId, player.GetX(), player.GetY());
    ClearView(player, despawnSends);

    player.SetZoneId(house.GetId());
    player.SetPosition(0.0f, HOUSE_ENTRY_Y);   // 집 안은 월드와 다른 좌표계다
    player.SetInput(0.0f, 0.0f);

    SC_SPAWN_PACKET selfSpawn = MakeSpawnPacket(player);

    // 같은 집에 있는 사람끼리는 거리를 따지지 않고 전부 보인다.
    // 방 하나가 시야 범위보다 좁아서 공간을 나눌 이유가 없다.
    for (auto& [otherId, other] : players)
    {
        if (otherId == myId || other.GetZoneId() != house.GetId())
        {
            continue;
        }

        player.GetViewList().insert(otherId);
        other.GetViewList().insert(myId);

        spawnSends.emplace_back(myId, MakeSpawnPacket(other));
        spawnSends.emplace_back(otherId, selfSpawn);
    }
}

// 집 안에서 나와 월드로 돌아간다.
void GameServer::ExitHouse(Player& player,
                           std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends,
                           std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends)
{
    ClearView(player, despawnSends);

    // 자기 집 남쪽으로 나온다.
    float x = 0.0f;
    float y = 0.0f;

    auto houseIter = houses.find(player.GetZoneId());

    if (houseIter != houses.end())
    {
        // 문 바로 앞이 아니라 옆으로 비켜 세운다. 문 앞에 세우면 위로 걷던 입력이 남아 있을 때
        // 나가자마자 다시 문에 닿아 도로 들어가 버린다.
        x = houseIter->second.GetX() + HOUSE_EXIT_SIDE_OFFSET;
        y = houseIter->second.GetY() - HOUSE_EXIT_OFFSET;
    }

    player.SetZoneId(0);
    player.SetInDoorZone(false);
    player.SetPosition(x, y);
    player.SetInput(0.0f, 0.0f);

    sectorGrid.Add(player.GetId(), x, y);
    PopulateWorldView(player, spawnSends);
}

// 지금 보고 있는 것들과의 관계를 전부 끊는다. 존을 옮기기 직전에 부른다.
void GameServer::ClearView(Player& player,
                           std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends)
{
    const std::uint32_t myId = player.GetId();

    SC_DESPAWN_PACKET despawnMe{};
    despawnMe.size = sizeof(SC_DESPAWN_PACKET);
    despawnMe.type = PACKET_TYPE::SC_DESPAWN;
    despawnMe.id = myId;

    for (std::uint32_t otherId : player.GetViewList())
    {
        SC_DESPAWN_PACKET despawnOther{};
        despawnOther.size = sizeof(SC_DESPAWN_PACKET);
        despawnOther.type = PACKET_TYPE::SC_DESPAWN;
        despawnOther.id = otherId;

        despawnSends.emplace_back(myId, despawnOther);   // 나에게: 상대를 지운다

        if (IsMonsterId(otherId))
        {
            auto monsterIter = monsters.find(otherId);

            if (monsterIter != monsters.end())
            {
                monsterIter->second.GetWatchers().erase(myId);
            }

            continue;
        }

        if (IsHouseId(otherId))
        {
            auto houseIter = houses.find(otherId);

            if (houseIter != houses.end())
            {
                houseIter->second.GetWatchers().erase(myId);
            }

            continue;
        }

        auto otherIter = players.find(otherId);

        if (otherIter != players.end())
        {
            otherIter->second.GetViewList().erase(myId);
            despawnSends.emplace_back(otherId, despawnMe);   // 상대에게: 나를 지운다
        }
    }

    player.GetViewList().clear();
}

// 지금 서 있는 자리 주변을 서로 보이게 만든다. 로그인 직후의 시야 구성과 같은 일이다.
void GameServer::PopulateWorldView(Player& self,
                                   std::vector<std::pair<std::uint32_t, SC_SPAWN_PACKET>>& spawnSends)
{
    const std::uint32_t myId = self.GetId();

    SC_SPAWN_PACKET selfSpawn = MakeSpawnPacket(self);

    std::vector<std::uint32_t> nearby;
    sectorGrid.GatherNearby(self.GetX(), self.GetY(), nearby);

    for (std::uint32_t otherId : nearby)
    {
        if (otherId == myId)
        {
            continue;
        }

        if (IsMonsterId(otherId))
        {
            auto monsterIter = monsters.find(otherId);

            if (monsterIter == monsters.end()) { continue; }

            Monster& monster = monsterIter->second;

            if (InRange(self.GetX(), self.GetY(), monster.GetX(), monster.GetY()) == false) { continue; }

            self.GetViewList().insert(otherId);
            monster.GetWatchers().insert(myId);
            spawnSends.emplace_back(myId, MakeSpawnPacket(monster));
            continue;
        }

        if (IsHouseId(otherId))
        {
            auto houseIter = houses.find(otherId);

            if (houseIter == houses.end()) { continue; }

            House& house = houseIter->second;

            if (InRange(self.GetX(), self.GetY(), house.GetX(), house.GetY()) == false) { continue; }

            self.GetViewList().insert(otherId);
            house.GetWatchers().insert(myId);
            spawnSends.emplace_back(myId, MakeSpawnPacket(house));
            continue;
        }

        auto otherIter = players.find(otherId);

        if (otherIter == players.end()) { continue; }

        Player& other = otherIter->second;

        if (other.GetZoneId() != 0) { continue; }   // 집 안에 있는 사람은 월드에서 안 보인다
        if (InViewRange(self, other) == false) { continue; }

        self.GetViewList().insert(otherId);
        other.GetViewList().insert(myId);

        spawnSends.emplace_back(myId, MakeSpawnPacket(other));
        spawnSends.emplace_back(otherId, selfSpawn);
    }
}

// (x, y)가 어느 집 문 앞에 닿아 있는가.
// 문은 집 남쪽 벽 한가운데다. 집이 이동을 막으므로 플레이어는 벽에서 PLAYER_RADIUS만큼
// 떨어진 선까지만 올 수 있고, 그 선에 문 폭 안에서 닿으면 들어간 것으로 본다.
bool GameServer::IsAtHouseDoor(float x, float y, std::uint32_t& outHouseId)
{
    std::vector<std::uint32_t> nearby;
    sectorGrid.GatherNearby(x, y, nearby);

    for (std::uint32_t id : nearby)
    {
        if (IsHouseId(id) == false)
        {
            continue;
        }

        auto houseIter = houses.find(id);

        if (houseIter == houses.end())
        {
            continue;
        }

        const House& house = houseIter->second;

        // 문 폭 안인가
        if (std::fabs(x - house.GetX()) > HOUSE_DOOR_HALF_WIDTH)
        {
            continue;
        }

        // 집의 남쪽에서 접근했는가. 위/옆에서 벽을 밀고 있는 건 문이 아니다.
        const float gap = house.GetY() - y;

        if (gap <= 0.0f || gap > HOUSE_DOOR_REACH)
        {
            continue;
        }

        outHouseId = id;
        return true;
    }

    return false;
}

// (x, y)에 서면 집에 파묻히는가. 집을 실체로 만들어 이동을 막는 데 쓴다.
bool GameServer::IsBlockedByHouse(float x, float y)
{
    std::vector<std::uint32_t> nearby;
    sectorGrid.GatherNearby(x, y, nearby);

    // 플레이어를 점이 아니라 반지름을 가진 것으로 본다. 안 그러면 몸이 벽에 반쯤 박힌다.
    const float reach = HOUSE_HALF + PLAYER_RADIUS;

    for (std::uint32_t id : nearby)
    {
        if (IsHouseId(id) == false)
        {
            continue;
        }

        auto houseIter = houses.find(id);

        if (houseIter == houses.end())
        {
            continue;
        }

        if (std::fabs(x - houseIter->second.GetX()) < reach &&
            std::fabs(y - houseIter->second.GetY()) < reach)
        {
            return true;
        }
    }

    return false;
}

// 휘두른 부채꼴에 든 살아있는 몬스터를 모은다.
// 주변 3x3 섹터만 훑으므로 월드에 몬스터가 많아져도 비용이 늘지 않는다.
void GameServer::GatherMonstersInArc(float originX, float originY, float dirX, float dirY,
                                     std::vector<std::uint32_t>& out)
{
    std::vector<std::uint32_t> nearby;
    sectorGrid.GatherNearby(originX, originY, nearby);

    for (std::uint32_t id : nearby)
    {
        if (IsMonsterId(id) == false)
        {
            continue;   // 플레이어는 아직 때릴 수 없다(PvP 별도)
        }

        auto iter = monsters.find(id);

        if (iter == monsters.end())
        {
            continue;
        }

        Monster& monster = iter->second;

        if (monster.IsAlive() == false)
        {
            continue;
        }

        const float toX = monster.GetX() - originX;
        const float toY = monster.GetY() - originY;

        const float distanceSq = toX * toX + toY * toY;

        if (distanceSq > ATTACK_RANGE * ATTACK_RANGE)
        {
            continue;
        }

        // 몸이 겹칠 만큼 붙어 있으면 방향을 따지지 않는다(0으로 나누는 것도 막힌다).
        if (distanceSq < 0.0001f)
        {
            out.push_back(id);
            continue;
        }

        // 휘두른 방향과 몬스터가 있는 방향 사이의 각. 내적이 cos(반각) 이상이면 부채꼴 안이다.
        const float dot = (toX * dirX + toY * dirY) / std::sqrt(distanceSq);

        if (dot < ATTACK_ARC_COS)
        {
            continue;
        }

        out.push_back(id);
    }
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

                if (IsHouseId(otherId))
                {
                    auto houseIter = houses.find(otherId);

                    if (houseIter != houses.end())
                    {
                        houseIter->second.GetWatchers().erase(playerId);
                    }

                    continue;   // 집은 그대로 둔다. 주인이 나가도 월드에 남는다
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