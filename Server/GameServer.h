// 2026-06-11: GameServer.h 파일 제작. IOCP 서버 실행 흐름을 관리하는 클래스.
// 2026-06-16: ProcessChat 추가.
// 2026-06-17: SendStatUpdate 추가.
#pragma once

#include "stdafx.h"
#include "protocol.h"
#include "NetworkTypes.h"
#include "Session.h"
#include "Player.h"
#include "SectorGrid.h"

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
    void ProcessInput(std::uint32_t playerId, const CS_INPUT_PACKET* packet);
    void ProcessChat(std::uint32_t playerId, const CS_CHAT_PACKET* packet);

    // 서버 권위 이동: 고정 주기(15Hz)로 도는 게임 틱 루프.
    void GameTickThread();
    void UpdateWorld(float deltaSeconds);   // 한 틱: 위치 적분 + 시야 diff + 전송

    void SendStatUpdate(std::uint32_t playerId, const PlayerStats& stats);
    void SendPacket(std::uint32_t playerId, const void* packet, int size);
    void BroadcastPacket(const void* packet, int size, std::uint32_t exceptId = 0);

    // 시야처리(AOI) 헬퍼. 아래 두 함수는 playersLock을 잡은 상태에서만 호출한다.
    SC_SPAWN_PACKET MakeSpawnPacket(const Player& player);
    static bool InViewRange(const Player& a, const Player& b);

    void DisconnectPlayer(std::uint32_t playerId);

    Session* FindSession(std::uint32_t playerId);
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
    std::unordered_map<std::uint32_t, std::unique_ptr<Session>> sessions;

    std::mutex playersLock;
    std::unordered_map<std::uint32_t, Player> players;

    // 섹터 격자도 playersLock으로 함께 보호한다(플레이어 위치/뷰리스트와 항상 같이 바뀌므로).
    SectorGrid sectorGrid;
};