// 2026-06-11: GameServer.cpp 파일 제작. GameServer 클래스 구현.
// 2026-06-12: 위치 동기화 작동 안함, 클라 문제로 추정.


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

void GameServer::ProcessLogin(std::uint32_t playerId, const CS_LOGIN_PACKET* packet)
{
    std::string playerName(packet->name, strnlen_s(packet->name, NAME_SIZE));
    Player self{ playerId, playerName, 0.0f, 0.0f };

    {
        std::lock_guard<std::mutex> lock(playersLock);
        players[playerId] = self;
    }

    SC_LOGIN_RESULT_PACKET loginResult{};
    loginResult.size = sizeof(SC_LOGIN_RESULT_PACKET);
    loginResult.type = PACKET_TYPE::SC_LOGIN_RESULT;
    loginResult.success = 1;
    loginResult.id = self.GetId();
    loginResult.x = self.GetX();
    loginResult.y = self.GetY();

    SendPacket(playerId, &loginResult, loginResult.size);

    {
        std::lock_guard<std::mutex> lock(playersLock);

        for (const auto& [otherId, other] : players) // 기존에 있는 유저들 스폰
        {
            if (otherId == playerId) continue;

            SC_SPAWN_PACKET spawn{};
            spawn.size = sizeof(SC_SPAWN_PACKET);
            spawn.type = PACKET_TYPE::SC_SPAWN;
            spawn.id = other.GetId();
            strncpy_s(spawn.name, sizeof(spawn.name), other.GetName().c_str(), _TRUNCATE);
            spawn.x = other.GetX();
            spawn.y = other.GetY();

            SendPacket(playerId, &spawn, spawn.size);
        }
    }

    SC_SPAWN_PACKET newPlayerSpawn{};
    newPlayerSpawn.size = sizeof(SC_SPAWN_PACKET);
    newPlayerSpawn.type = PACKET_TYPE::SC_SPAWN;
    newPlayerSpawn.id = self.GetId();
    strncpy_s(newPlayerSpawn.name, sizeof(newPlayerSpawn.name), self.GetName().c_str(), _TRUNCATE);
    newPlayerSpawn.x = self.GetX();
    newPlayerSpawn.y = self.GetY();

    BroadcastPacket(&newPlayerSpawn, newPlayerSpawn.size, playerId);

    std::cout << "[LOGIN] id = " << self.GetId() << ", name = " << self.GetName() << std::endl;
}

void GameServer::ProcessMove(std::uint32_t playerId, const CS_MOVE_PACKET* packet)
{
    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        iter->second.SetPosition(packet->x, packet->y);
    }

    SC_MOVE_PACKET movePacket{};
    movePacket.size = sizeof(SC_MOVE_PACKET);
    movePacket.type = PACKET_TYPE::SC_MOVEOBJECT;
    movePacket.id = playerId;
    movePacket.x = packet->x;
    movePacket.y = packet->y;

    BroadcastPacket(&movePacket, movePacket.size, playerId);
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

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter != players.end())
        {
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

    BroadcastPacket(&despawn, despawn.size, playerId);

    std::cout << "[DISCONNECT] id = " << playerId << std::endl;
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