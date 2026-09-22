// 2026-09-22: GameServer.cpp에서 분리. 시야처리(AOI): 시야 범위 판정, spawn 패킷 만들기, 뷰리스트 채우기/비우기.

#include "stdafx.h"
#include "GameServer.h"

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
