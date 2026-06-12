// 2026-06-11: GameServer.h 파일 제작. IOCP 서버 실행 흐름을 관리하는 클래스.
#pragma once

#include "stdafx.h"
#include "protocol.h"
#include "NetworkTypes.h"
#include "Session.h"
#include "Player.h"

class GameServer
{
public:
    GameServer() = default;
    ~GameServer();

    void Run();

private:
    bool Initialize();
    void PostAccept();
    void WorkerThread();

    void ProcessPacket(std::uint32_t playerId, const char* data, int bytes);
    bool ReadPacketInfo(const char* data, int bytes, std::uint16_t& packetSize, PACKET_TYPE& packetType);

    void ProcessLogin(std::uint32_t playerId, const CS_LOGIN_PACKET* packet);
    void ProcessMove(std::uint32_t playerId, const CS_MOVE_PACKET* packet);

    void SendPacket(std::uint32_t playerId, const void* packet, int size);
    void BroadcastPacket(const void* packet, int size, std::uint32_t exceptId = 0);

    void DisconnectPlayer(std::uint32_t playerId);

    Session* FindSession(std::uint32_t playerId);
    std::uint32_t IssuePlayerId();
    void PrintServerError(const char* message);

private:
    HANDLE iocpHandle = nullptr;
    SOCKET listenSocket = INVALID_SOCKET;

    std::atomic<std::uint32_t> nextPlayerId{ 1 };


    // 추후 concurrency로 바꿀지 고민.
    std::mutex sessionsLock;
    std::unordered_map<std::uint32_t, std::unique_ptr<Session>> sessions;

    std::mutex playersLock;
    std::unordered_map<std::uint32_t, Player> players;
};