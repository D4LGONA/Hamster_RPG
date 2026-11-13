// 2026-11-13: 집 안 가구 (Docs/FurnitureDesign.md).
//
// 검사 순서(설계 4장): 권한 -> 연타 -> 좌표 -> 출입구 -> 겹침 -> 한도. 앞에서 걸리면 뒤는 안 본다.
// 성공하면 그 집 안에 있는 사람 전원에게 SPAWN/MOVED/REMOVED를 보낸다(요청자 포함). 실패만 RESULT를 요청자에게.
// 월드에 있는 사람에게는 아무것도 안 보낸다. 가구는 월드에 없다.
// DB가 켜져 있으면 바뀔 때마다 DB 스레드에 저장을 맡긴다(게임은 기다리지 않는다).

#include "stdafx.h"
#include "GameServer.h"

namespace
{
    bool IsFiniteCoord(float v) { return std::isfinite(v) && std::fabs(v) < 1000.0f; }

    SC_FURNITURE_SPAWN_PACKET MakeFurnitureSpawn(const Furniture& f)
    {
        SC_FURNITURE_SPAWN_PACKET p{};
        p.size = sizeof(SC_FURNITURE_SPAWN_PACKET);
        p.type = PACKET_TYPE::SC_FURNITURE_SPAWN;
        p.furnitureId = f.id;
        p.itemType = f.itemType;
        p.rotation = f.rotation;
        p.x = f.x;
        p.y = f.y;
        return p;
    }

    DbFurnitureRow ToRow(const Furniture& f, std::uint32_t furnitureBase, std::uint32_t houseBase)
    {
        return DbFurnitureRow{ f.id - furnitureBase, f.houseId - houseBase, f.itemType, f.x, f.y, f.rotation };
    }
}

House* GameServer::CheckFurnitureAccess(Player& player, std::uint8_t& reason)
{
    // 1) 집 안에 있는가
    if (player.GetZoneId() == 0)
    {
        reason = 1;
        return nullptr;
    }

    // 2) 그 집이 있고, 내 집인가 (남의 집에선 구경만)
    auto houseIter = houses.find(player.GetZoneId());

    if (houseIter == houses.end() || houseIter->second.GetOwnerName() != player.GetName())
    {
        reason = 2;
        return nullptr;
    }

    // 3) 연타 방지: 0.2초에 한 번. 봇이 설치/삭제를 무한 반복해 브로드캐스트를 일으키는 걸 막는다.
    const auto now = std::chrono::steady_clock::now();
    const float sinceLast = std::chrono::duration<float>(now - player.GetLastFurnitureRequest()).count();

    if (sinceLast < FURNITURE_REQUEST_INTERVAL)
    {
        reason = 7;
        return nullptr;
    }

    player.SetLastFurnitureRequest(now);
    return &houseIter->second;
}

bool GameServer::CheckFurnitureFits(const Furniture& candidate, std::uint32_t ignoreId, std::uint8_t& reason)
{
    // 좌표: 숫자가 맞고, 가구 전체가 방 안에 들어가야 한다(벽을 뚫고 나가지 않게)
    if (candidate.itemType >= FURNITURE_TYPE_COUNT || candidate.rotation > 3 ||
        IsFiniteCoord(candidate.x) == false || IsFiniteCoord(candidate.y) == false ||
        std::fabs(candidate.x) + candidate.HalfWidth() > HOUSE_INTERIOR_HALF_X ||
        std::fabs(candidate.y) + candidate.HalfHeight() > HOUSE_INTERIOR_HALF_Y)
    {
        reason = 3;
        return false;
    }

    // 출입구: 들어와서 서는 자리(0, HOUSE_ENTRY_Y) 주변을 막으면 들어오자마자 갇힌다. 러그는 밟고 지나가니 괜찮다.
    if (candidate.itemType != FURNITURE_RUG &&
        std::fabs(candidate.x) < candidate.HalfWidth() + FURNITURE_ENTRY_CLEAR &&
        std::fabs(candidate.y - HOUSE_ENTRY_Y) < candidate.HalfHeight() + FURNITURE_ENTRY_CLEAR)
    {
        reason = 5;
        return false;
    }

    auto listIter = houseFurnitures.find(candidate.houseId);

    if (listIter == houseFurnitures.end())
    {
        return true;   // 첫 가구
    }

    // 겹침: 같은 집 가구끼리만 AABB. 러그는 바닥 장식이라 무엇과도 겹쳐도 된다(러그 위에 의자).
    if (candidate.itemType != FURNITURE_RUG)
    {
        for (std::uint32_t otherId : listIter->second)
        {
            if (otherId == ignoreId)
            {
                continue;
            }

            const Furniture& other = furnitures.at(otherId);

            if (other.itemType != FURNITURE_RUG && candidate.Overlaps(other))
            {
                reason = 4;
                return false;
            }
        }
    }

    // 한도(옮길 때는 개수가 안 늘어나므로 보지 않는다)
    if (ignoreId == 0 && listIter->second.size() >= MAX_FURNITURE_PER_HOUSE)
    {
        reason = 6;
        return false;
    }

    return true;
}

void GameServer::CollectHouseFurnitureSpawns(std::uint32_t houseId, std::vector<SC_FURNITURE_SPAWN_PACKET>& out)
{
    auto listIter = houseFurnitures.find(houseId);

    if (listIter == houseFurnitures.end())
    {
        return;
    }

    for (std::uint32_t id : listIter->second)
    {
        out.push_back(MakeFurnitureSpawn(furnitures.at(id)));
    }
}

// 그 집 안에 있는 사람들. 한 집에 들어가는 인원이 적어서 players 전체를 훑는다(설계 8장 결정).
void GameServer::CollectHouseMembers(std::uint32_t houseId, std::vector<std::uint32_t>& out)
{
    for (const auto& [id, player] : players)
    {
        if (player.GetZoneId() == houseId)
        {
            out.push_back(id);
        }
    }
}

void GameServer::SendFurnitureFail(std::uint32_t playerId, std::uint8_t reason)
{
    SC_FURNITURE_RESULT_PACKET fail{};
    fail.size = sizeof(SC_FURNITURE_RESULT_PACKET);
    fail.type = PACKET_TYPE::SC_FURNITURE_RESULT;
    fail.reason = reason;
    SendPacket(playerId, &fail, fail.size);
}

void GameServer::ProcessFurniturePlace(std::uint32_t playerId, const CS_FURNITURE_PLACE_PACKET* packet)
{
    std::uint8_t reason = 0;
    std::vector<std::uint32_t> members;
    SC_FURNITURE_SPAWN_PACKET spawn{};
    DbFurnitureRow row{};

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        House* house = CheckFurnitureAccess(iter->second, reason);

        if (house != nullptr)
        {
            Furniture candidate;
            candidate.houseId = house->GetId();
            candidate.itemType = packet->itemType;
            candidate.rotation = packet->rotation;
            candidate.x = packet->x;
            candidate.y = packet->y;

            if (CheckFurnitureFits(candidate, 0, reason))
            {
                candidate.id = nextFurnitureId++;
                furnitures.emplace(candidate.id, candidate);
                houseFurnitures[candidate.houseId].push_back(candidate.id);

                spawn = MakeFurnitureSpawn(candidate);
                row = ToRow(candidate, FURNITURE_ID_BASE, HOUSE_ID_BASE);
                CollectHouseMembers(candidate.houseId, members);
                reason = 0;
            }
        }
    }

    if (reason != 0)
    {
        SendFurnitureFail(playerId, reason);
        return;
    }

    for (std::uint32_t memberId : members)
    {
        SendPacket(memberId, &spawn, spawn.size);
    }

    if (dbWorker.IsEnabled())
    {
        dbWorker.Post([row](Database& db)
        {
            if (db.SaveFurniture(row) == false)
            {
                std::cout << "[DB] furniture save failed: " << db.GetLastError() << std::endl;
            }
        });
    }
}

void GameServer::ProcessFurnitureMove(std::uint32_t playerId, const CS_FURNITURE_MOVE_PACKET* packet)
{
    std::uint8_t reason = 0;
    std::vector<std::uint32_t> members;
    SC_FURNITURE_MOVED_PACKET moved{};
    DbFurnitureRow row{};

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        House* house = CheckFurnitureAccess(iter->second, reason);

        if (house != nullptr)
        {
            auto furnitureIter = furnitures.find(packet->furnitureId);

            // 그 가구가 이 집 것인가. id만 바꿔 보내서 남의 집 가구를 옮기는 걸 막는다.
            if (furnitureIter == furnitures.end() || furnitureIter->second.houseId != house->GetId())
            {
                reason = 8;
            }
            else
            {
                Furniture candidate = furnitureIter->second;
                candidate.rotation = packet->rotation;
                candidate.x = packet->x;
                candidate.y = packet->y;

                if (CheckFurnitureFits(candidate, candidate.id, reason))
                {
                    furnitureIter->second = candidate;

                    moved.size = sizeof(SC_FURNITURE_MOVED_PACKET);
                    moved.type = PACKET_TYPE::SC_FURNITURE_MOVED;
                    moved.furnitureId = candidate.id;
                    moved.rotation = candidate.rotation;
                    moved.x = candidate.x;
                    moved.y = candidate.y;

                    row = ToRow(candidate, FURNITURE_ID_BASE, HOUSE_ID_BASE);
                    CollectHouseMembers(candidate.houseId, members);
                    reason = 0;
                }
            }
        }
    }

    if (reason != 0)
    {
        SendFurnitureFail(playerId, reason);
        return;
    }

    for (std::uint32_t memberId : members)
    {
        SendPacket(memberId, &moved, moved.size);
    }

    if (dbWorker.IsEnabled())
    {
        dbWorker.Post([row](Database& db)
        {
            if (db.SaveFurniture(row) == false)
            {
                std::cout << "[DB] furniture save failed: " << db.GetLastError() << std::endl;
            }
        });
    }
}

void GameServer::ProcessFurnitureRemove(std::uint32_t playerId, const CS_FURNITURE_REMOVE_PACKET* packet)
{
    std::uint8_t reason = 0;
    std::vector<std::uint32_t> members;
    SC_FURNITURE_REMOVED_PACKET removed{};
    std::uint64_t dbId = 0;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        House* house = CheckFurnitureAccess(iter->second, reason);

        if (house != nullptr)
        {
            auto furnitureIter = furnitures.find(packet->furnitureId);

            if (furnitureIter == furnitures.end() || furnitureIter->second.houseId != house->GetId())
            {
                reason = 8;
            }
            else
            {
                const std::uint32_t id = furnitureIter->first;
                std::vector<std::uint32_t>& list = houseFurnitures[house->GetId()];
                list.erase(std::remove(list.begin(), list.end(), id), list.end());
                furnitures.erase(furnitureIter);

                removed.size = sizeof(SC_FURNITURE_REMOVED_PACKET);
                removed.type = PACKET_TYPE::SC_FURNITURE_REMOVED;
                removed.furnitureId = id;

                dbId = id - FURNITURE_ID_BASE;
                CollectHouseMembers(house->GetId(), members);
                reason = 0;
            }
        }
    }

    if (reason != 0)
    {
        SendFurnitureFail(playerId, reason);
        return;
    }

    for (std::uint32_t memberId : members)
    {
        SendPacket(memberId, &removed, removed.size);
    }

    if (dbWorker.IsEnabled())
    {
        dbWorker.Post([dbId](Database& db)
        {
            if (db.DeleteFurniture(dbId) == false)
            {
                std::cout << "[DB] furniture delete failed: " << db.GetLastError() << std::endl;
            }
        });
    }
}

// 서버 시작 때 한 번. 아직 워커/틱 스레드가 없어서 락이 필요 없지만, 습관대로 잡는다.
// 메모리 id = 대역 + DB id. 다음 id는 DB에 있던 가장 큰 값 다음부터.
void GameServer::LoadWorldFromDb()
{
    if (dbWorker.IsEnabled() == false)
    {
        return;
    }

    std::vector<DbHouseRow> houseRows;
    std::vector<DbFurnitureRow> furnitureRows;
    bool ok = false;
    std::string error;

    dbWorker.RunSync([&](Database& db)
    {
        ok = db.LoadWorld(houseRows, furnitureRows);
        error = db.GetLastError();
    });

    if (ok == false)
    {
        std::cout << "[DB] world load failed: " << error << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(playersLock);

    for (const DbHouseRow& row : houseRows)
    {
        const std::uint32_t id = HOUSE_ID_BASE + static_cast<std::uint32_t>(row.houseId);
        houses.emplace(id, House{ id, row.ownerName, row.x, row.y });
        sectorGrid.Add(id, row.x, row.y);

        if (id >= nextHouseId)
        {
            nextHouseId = id + 1;
        }
    }

    for (const DbFurnitureRow& row : furnitureRows)
    {
        Furniture f;
        f.id = FURNITURE_ID_BASE + static_cast<std::uint32_t>(row.furnitureId);
        f.houseId = HOUSE_ID_BASE + static_cast<std::uint32_t>(row.houseId);
        f.itemType = static_cast<std::uint8_t>(row.itemType);
        f.rotation = static_cast<std::uint8_t>(row.rotation & 3);
        f.x = row.x;
        f.y = row.y;

        if (f.itemType >= FURNITURE_TYPE_COUNT || houses.count(f.houseId) == 0)
        {
            continue;   // 표에서 지운 가구 종류이거나 집이 없다
        }

        furnitures.emplace(f.id, f);
        houseFurnitures[f.houseId].push_back(f.id);

        if (f.id >= nextFurnitureId)
        {
            nextFurnitureId = f.id + 1;
        }
    }

    std::cout << "[DB] loaded " << houseRows.size() << " houses, " << furnitures.size() << " furniture" << std::endl;
}
