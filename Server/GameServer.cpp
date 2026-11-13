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

    // DB 1단계: 캐릭터 저장. 설정 파일이 없거나 연결이 안 되면 DB 없이 돈다(저장만 안 될 뿐).
    dbWorker.Start(LoadDbConnectionString());

    // 2026-11-13: 지어 둔 집과 가구를 DB에서 불러온다(서버를 껐다 켜도 그대로)
    LoadWorldFromDb();

    // 몬스터는 미리 깔지 않는다. 플레이어가 다가가는 청크마다 게임 틱이 만든다(GameServer_Chunk.cpp).

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

    ShutdownSave();
}

void GameServer::ShutdownSave()
{
    if (dbWorker.IsEnabled() == false)
    {
        return;
    }

    SaveAllPlayers("shutdown");
    dbWorker.Stop();   // 큐에 남은 저장을 다 처리하고 끝난다
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

                    // 실패한 게 Recv면 이게 그 세션의 마지막 Recv 완료다. 이제 지워도 안전하다.
                    // (SEND 실패면 Recv가 아직 걸려 있으니 지우지 않는다. 그 Recv 완료 때 지워진다.)
                    // 지운 뒤에는 extOver(= 세션 안의 recvOver)를 건드리면 안 된다.
                    if (ioType == IO_TYPE::RECV)
                        RemoveSession(static_cast<std::uint32_t>(key));
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

            auto session = std::make_shared<Session>(clientSocket);
            session->SetPlayerId(playerId);

            if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), iocpHandle, playerId, 0) == nullptr)
            {
                PrintServerError("CreateIoCompletionPort client socket failed");
                delete extOver;
                PostAccept();
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(sessionsLock);
                sessions.emplace(playerId, session);
            }

            std::cout << "[ACCEPT] playerId = " << playerId << std::endl;

            // 첫 Recv부터 실패 → 걸린 Recv가 없으니 바로 지운다.
            if (session->Recv() == false)
            {
                DisconnectPlayer(playerId);
                RemoveSession(playerId);
            }

            delete extOver;
            PostAccept();
        }
        else if (extOver->ioType == IO_TYPE::RECV)
        {
            std::uint32_t playerId = static_cast<std::uint32_t>(key);

            // 상대가 연결을 끊음 → Recv를 다시 걸지 않으니 여기서 지운다.
            if (numBytes == 0)
            {
                DisconnectPlayer(playerId);
                RemoveSession(playerId);
                continue;
            }

            std::shared_ptr<Session> session = FindSession(playerId);
            if (session == nullptr) continue;

            // 버퍼 = [지난번에 남은 꼬리 | 이번에 받은 바이트]. 둘을 합친 길이만큼 처리한다.
            int total = session->GetSavedBytes() + static_cast<int>(numBytes);
            int processed = ProcessPacket(playerId, extOver->buffer, total);

            // 망가진 패킷 → 더 기다려도 소용없으니 끊는다.
            if (processed < 0)
            {
                DisconnectPlayer(playerId);
                RemoveSession(playerId);
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

            // 다시 건 Recv가 바로 실패 → 걸린 Recv가 없으니 바로 지운다.
            // (로그아웃도 여기로 온다: DisconnectPlayer가 소켓을 닫아 둬서 Recv가 실패함)
            if (session->Recv() == false)
            {
                DisconnectPlayer(playerId);
                RemoveSession(playerId);
            }

            // RECV extOver는 Session 멤버 recvOver라서 delete 하지 않는다.
        }
        else if (extOver->ioType == IO_TYPE::SEND)
        {
            delete extOver;
        }
    }
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
    bool needSave = false;
    CharacterData saveData;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        // DB에서 불러오는 중에 끊겼다. CompleteLogin이 이걸 보고 월드에 넣지 않는다.
        pendingLogins.erase(playerId);

        auto iter = players.find(playerId);

        if (iter != players.end())
        {
            Player& me = iter->second;

            // 지우기 전에 저장할 값을 떠 둔다. 실제 저장은 DB 스레드가 한다(락 밖에서 Post).
            if (me.IsPersistent())
            {
                saveData = MakeSaveData(me);
                needSave = true;
            }

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
            // 여기서는 소켓만 닫고 map에서 지우지 않는다.
            // 걸려 있던 Recv가 실패로 완료되면 워커가 RemoveSession으로 지운다.
            // (지금 지우면 그 Recv의 recvOver가 세션과 함께 사라져서, 완료 통지가 지워진 메모리로 온다)
            // existed는 건드리지 않는다. 한 사람에게 두 번 불려도 로그가 한 번만 찍히게.
            iter->second->Close();
        }
    }

    if (needSave && dbWorker.IsEnabled())
    {
        dbWorker.Post([saveData](Database& db)
        {
            std::vector<CharacterData> one{ saveData };

            if (db.SaveCharacters(one) == false)
            {
                std::cout << "[DB] logout save failed for " << saveData.name << ": " << db.GetLastError() << std::endl;
            }
        });
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

std::shared_ptr<Session> GameServer::FindSession(std::uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(sessionsLock);

    auto iter = sessions.find(playerId);

    if (iter == sessions.end()) return nullptr;

    return iter->second;
}

std::uint32_t GameServer::IssuePlayerId()
{
    return nextPlayerId.fetch_add(1);
}

void GameServer::PrintServerError(const char* message)
{
    std::cout << "[SERVER ERROR] " << message << ", error = " << WSAGetLastError() << std::endl;
}

// 세션을 map에서 지운다. Recv가 걸려 있지 않을 때만 불러야 한다.
// (= Recv 완료를 처리하면서 다시 걸지 않을 때, 또는 Recv를 거는 데 실패했을 때)
void GameServer::RemoveSession(std::uint32_t playerId)
{
    std::lock_guard<std::mutex> lock(sessionsLock);

    auto iter = sessions.find(playerId);
    if (iter != sessions.end())
    {
        sessions.erase(iter);
        std::cout << "[SESSION REMOVED] playerId = " << playerId << std::endl;
    }
}
