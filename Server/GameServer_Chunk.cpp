// 2026-11-12: 무한 월드 청크 관리.
//
// 월드가 끝이 없으니 몬스터를 미리 다 깔 수 없다. 그래서
//   - 플레이어 주변 5x5 청크를 '켜고', 처음 켜질 때 그 청크의 몬스터를 만든다.
//   - 주변에 아무도 없는 상태가 30초 이어진 청크는 '끄고', 그 청크가 낳은 몬스터를 지운다.
// 몬스터 자리와 종류는 WorldGen 공식으로 정해지므로, 다시 켜면 같은 자리에 같은 몬스터가 생긴다.
// (잡아둔 몬스터가 되살아나는 건 부활 규칙과 같으니 문제없다)
//
// 지울 때 시야 문제: 끄는 청크는 반경 2청크(32유닛) 안에 플레이어가 없다. 몬스터는 집에서 12유닛 넘게
// 안 벗어나므로 플레이어와 최소 20유닛 = 시야(VIEW_RANGE) 끝이다. 그래도 경계에서 누가 보고 있을 수 있어서
// watchers에게는 despawn을 꼭 보낸다.

#include "stdafx.h"
#include "GameServer.h"

namespace
{
    std::int64_t ChunkKey(int chunkX, int chunkY)
    {
        return (static_cast<std::int64_t>(chunkY) << 32) | static_cast<std::uint32_t>(chunkX);
    }
}

void GameServer::UpdateChunks(float deltaSeconds, std::vector<std::uint32_t>& spawned,
                              std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends)
{
    chunkScanTimer += deltaSeconds;

    if (chunkScanTimer < CHUNK_SCAN_INTERVAL)
    {
        return;
    }

    const float elapsed = chunkScanTimer;
    chunkScanTimer = 0.0f;

    // 1) 이번 스캔에서 누군가 근처에 있는 청크를 모은다. 없던 청크는 켠다.
    std::unordered_set<std::int64_t> touched;

    for (const auto& [id, player] : players)
    {
        if (player.GetZoneId() != 0)
        {
            continue;   // 집 안 좌표는 월드 좌표가 아니다
        }

        const int centerX = WorldGen::ChunkOf(WorldGen::TileOf(player.GetX()));
        const int centerY = WorldGen::ChunkOf(WorldGen::TileOf(player.GetY()));

        for (int dy = -CHUNK_ACTIVE_RADIUS; dy <= CHUNK_ACTIVE_RADIUS; ++dy)
        {
            for (int dx = -CHUNK_ACTIVE_RADIUS; dx <= CHUNK_ACTIVE_RADIUS; ++dx)
            {
                const int cx = centerX + dx;
                const int cy = centerY + dy;
                const std::int64_t key = ChunkKey(cx, cy);

                if (touched.insert(key).second == false)
                {
                    continue;   // 다른 플레이어가 이미 켰다
                }

                auto iter = chunks.find(key);

                if (iter == chunks.end())
                {
                    ActivateChunk(cx, cy, spawned);
                }
                else
                {
                    iter->second.idleSeconds = 0.0f;
                }
            }
        }
    }

    // 2) 아무도 안 온 청크는 빈 시간을 늘리고, 오래 비었으면 끈다.
    std::vector<std::int64_t> toUnload;

    for (auto& [key, state] : chunks)
    {
        if (touched.count(key) != 0)
        {
            continue;
        }

        state.idleSeconds += elapsed;

        if (state.idleSeconds >= CHUNK_UNLOAD_IDLE_SEC)
        {
            toUnload.push_back(key);
        }
    }

    for (std::int64_t key : toUnload)
    {
        UnloadChunk(key, despawnSends);
    }
}

void GameServer::ActivateChunk(int chunkX, int chunkY, std::vector<std::uint32_t>& spawned)
{
    ChunkState& state = chunks[ChunkKey(chunkX, chunkY)];

    std::vector<WorldGen::MonsterSpot> spots;
    WorldGen::MonstersInChunk(chunkX, chunkY, spots);

    for (const WorldGen::MonsterSpot& spot : spots)
    {
        // 종류별 행동/체력. 애벌레는 느긋하게 배회, 무당벌레는 순찰, 고양이 보스는 쫓아온다.
        MonsterBehavior behavior = MonsterBehavior::Wander;
        int maxHp = 30;

        switch (spot.kind)
        {
        case WorldGen::MonsterKind::Caterpillar: behavior = MonsterBehavior::Wander; maxHp = 30;  break;
        case WorldGen::MonsterKind::Ladybug:     behavior = MonsterBehavior::Patrol; maxHp = 50;  break;
        case WorldGen::MonsterKind::CatBoss:     behavior = MonsterBehavior::Chase;  maxHp = 500; break;
        }

        const std::uint32_t id = nextMonsterId++;
        monsters.emplace(id, Monster{ id, behavior, spot.x, spot.y, maxHp, spot.kind });
        sectorGrid.Add(id, spot.x, spot.y);

        state.monsterIds.push_back(id);
        spawned.push_back(id);
    }
}

void GameServer::UnloadChunk(std::int64_t key, std::vector<std::pair<std::uint32_t, SC_DESPAWN_PACKET>>& despawnSends)
{
    auto chunkIter = chunks.find(key);

    if (chunkIter == chunks.end())
    {
        return;
    }

    for (std::uint32_t monsterId : chunkIter->second.monsterIds)
    {
        auto monsterIter = monsters.find(monsterId);

        if (monsterIter == monsters.end())
        {
            continue;
        }

        Monster& monster = monsterIter->second;

        // 죽어서 부활 대기 중인 몬스터는 이미 격자에서 빠져 있다(KillMonster 참고).
        if (monster.GetRespawnTimer() <= 0.0f)
        {
            sectorGrid.Remove(monsterId, monster.GetX(), monster.GetY());
        }

        // 보고 있던 사람이 있으면 화면에서도 지운다(watchers <-> viewList 불변식 유지).
        for (std::uint32_t watcherId : monster.GetWatchers())
        {
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

        monsters.erase(monsterIter);
    }

    chunks.erase(chunkIter);
}
