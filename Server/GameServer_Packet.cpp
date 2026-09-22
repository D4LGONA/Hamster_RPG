// 2026-09-22: GameServer.cpp에서 분리. 패킷 파싱과 로그인/이동 입력/채팅 등 클라 패킷 처리.

#include "stdafx.h"
#include "GameServer.h"

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
