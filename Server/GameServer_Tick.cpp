// 2026-09-22: GameServer.cpp에서 분리. 게임 틱 스레드와 월드 업데이트, 틱 프로파일.

#include "stdafx.h"
#include "GameServer.h"

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
