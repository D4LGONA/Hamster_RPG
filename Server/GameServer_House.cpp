// 2026-09-22: GameServer.cpp에서 분리. 하우징: 집 설치, 입장/퇴장, 충돌.

#include "stdafx.h"
#include "GameServer.h"

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

    // 나무/덤불/돌 위에는 못 짓는다. 지으면 집이 장애물을 덮어 그림과 충돌이 어긋난다.
    if (WorldGen::IsAreaBlocked(x, y, HOUSE_HALF))
    {
        reason = 4;
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
