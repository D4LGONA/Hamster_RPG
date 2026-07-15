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

        case PACKET_TYPE::CS_MOVE:
            if (packetSize == sizeof(CS_MOVE_PACKET))
            {
                ProcessMove(playerId, reinterpret_cast<const CS_MOVE_PACKET*>(packetData));
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
    return spawn;
}

// 두 플레이어가 서로 시야 범위 안인지. sqrt 없이 제곱 거리로 비교(비용 절약).
bool GameServer::InViewRange(const Player& a, const Player& b)
{
    float dx = a.GetX() - b.GetX();
    float dy = a.GetY() - b.GetY();
    return (dx * dx + dy * dy) <= (VIEW_RANGE * VIEW_RANGE);
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

        Player player{ playerId, playerName, 0.0f, 0.0f };
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

void GameServer::ProcessMove(std::uint32_t playerId, const CS_MOVE_PACKET* packet)
{
    SC_MOVE_PACKET movePacket{};
    SC_SPAWN_PACKET selfSpawn{};

    std::vector<std::uint32_t> moveTargets;      // 계속 보이는 상대 -> 내 이동 전송
    std::vector<SC_SPAWN_PACKET> spawnToMe;      // 새로 보이는 상대 -> 나에게 spawn
    std::vector<std::uint32_t> spawnMeTargets;   // 나를 새로 보게 된 상대 -> 그들에게 내 spawn
    std::vector<std::uint32_t> despawnTargets;   // 시야에서 서로 벗어난 상대들 (양방향 despawn)

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        Player& me = iter->second;

        float oldX = me.GetX();
        float oldY = me.GetY();
        float newX = packet->x;
        float newY = packet->y;

        me.SetPosition(newX, newY);
        sectorGrid.Move(playerId, oldX, oldY, newX, newY);

        selfSpawn = MakeSpawnPacket(me);

        movePacket.size = sizeof(SC_MOVE_PACKET);
        movePacket.type = PACKET_TYPE::SC_MOVEOBJECT;
        movePacket.id = playerId;
        movePacket.x = newX;
        movePacket.y = newY;

        // 이동 후 새 시야 계산 (주변 3x3 섹터 후보 -> 거리 판정)
        std::vector<std::uint32_t> nearby;
        sectorGrid.GatherNearby(newX, newY, nearby);

        std::unordered_set<std::uint32_t> newView;

        for (std::uint32_t otherId : nearby)
        {
            if (otherId == playerId)
            {
                continue;
            }

            auto otherIter = players.find(otherId);

            if (otherIter == players.end())
            {
                continue;
            }

            if (InViewRange(me, otherIter->second))
            {
                newView.insert(otherId);
            }
        }

        std::unordered_set<std::uint32_t>& oldView = me.GetViewList();

        // 새 시야 순회: 새로 들어온 사람은 spawn, 계속 있는 사람은 move
        for (std::uint32_t otherId : newView)
        {
            if (oldView.find(otherId) == oldView.end())
            {
                // 새로 시야에 진입 -> 서로 spawn
                auto otherIter = players.find(otherId);

                if (otherIter != players.end())
                {
                    otherIter->second.GetViewList().insert(playerId);
                    spawnToMe.push_back(MakeSpawnPacket(otherIter->second));
                    spawnMeTargets.push_back(otherId);
                }
            }
            else
            {
                // 계속 보이는 중 -> 내 이동을 상대에게
                moveTargets.push_back(otherId);
            }
        }

        // 예전 시야엔 있었는데 새 시야엔 없는 사람 -> 서로 despawn
        for (std::uint32_t otherId : oldView)
        {
            if (newView.find(otherId) != newView.end())
            {
                continue;
            }

            auto otherIter = players.find(otherId);

            if (otherIter != players.end())
            {
                otherIter->second.GetViewList().erase(playerId);
            }

            despawnTargets.push_back(otherId);
        }

        oldView = std::move(newView);   // 내 뷰리스트 갱신
    }

    // ---- playersLock 해제 후 전송 ----
    for (std::uint32_t targetId : moveTargets)
    {
        SendPacket(targetId, &movePacket, movePacket.size);
    }

    for (const SC_SPAWN_PACKET& spawn : spawnToMe)
    {
        SendPacket(playerId, &spawn, spawn.size);
    }

    for (std::uint32_t targetId : spawnMeTargets)
    {
        SendPacket(targetId, &selfSpawn, selfSpawn.size);
    }

    for (std::uint32_t otherId : despawnTargets)
    {
        // 상대에게 "나" despawn
        SC_DESPAWN_PACKET despawnMe{};
        despawnMe.size = sizeof(SC_DESPAWN_PACKET);
        despawnMe.type = PACKET_TYPE::SC_DESPAWN;
        despawnMe.id = playerId;
        SendPacket(otherId, &despawnMe, despawnMe.size);

        // 나에게 "상대" despawn
        SC_DESPAWN_PACKET despawnOther{};
        despawnOther.size = sizeof(SC_DESPAWN_PACKET);
        despawnOther.type = PACKET_TYPE::SC_DESPAWN;
        despawnOther.id = otherId;
        SendPacket(playerId, &despawnOther, despawnOther.size);
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

            // 나를 보던 사람들의 뷰리스트에서 나를 지우고, despawn 대상으로 수집
            for (std::uint32_t otherId : me.GetViewList())
            {
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