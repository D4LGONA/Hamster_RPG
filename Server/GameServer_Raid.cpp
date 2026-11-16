// 2026-11-16: 월드 보스 레이드 — 왕 고양이.
//
// 흐름: 등장(전원 공지) -> 1초마다 상태 방송(HP, 기여도 상위 5명) -> 5초마다 내려찍기(예고 1.2초 뒤 범위 피해)
//       -> 쓰러짐(막타 순간 bossDown 표시) -> 다음 틱에 기여도대로 보상(정확히 한 번) -> 5분 뒤 다시 등장
//
// 동시성 포인트
//  1) 막타: 수십 명이 같은 틱 안에 때린다. 공격은 IOCP 워커 여러 개가 동시에 처리하지만 전부 playersLock 안에서
//     HP를 깎으므로, "HP가 0 이하로 내려가는 순간"은 한 번뿐이다. 그 한 번만 bossDown = true.
//  2) 보상: 공격 처리 중(워커)에 나누지 않고 틱 스레드가 bossDown을 보고 나눈 다음 bossId를 0으로 돌린다.
//     같은 보스로 보상을 두 번 줄 길이 없다(상태가 '보스 있음 -> 정산 -> 없음' 한 방향으로만 바뀐다).
//  3) 기여도는 이름으로 묶는다. 싸우다 끊겼다 다시 들어와도 이어진다. 보상은 정산 순간 접속 중인 사람에게.
//
// 지금은 전부 playersLock 하나로 보호한다. 부하 테스트(--raid)로 이 락이 얼마나 막히는지 재는 게 다음 단계다.

#include "stdafx.h"
#include "GameServer.h"

namespace
{
    void FillName(char (&dst)[NAME_SIZE], const std::string& src)
    {
        std::memset(dst, 0, sizeof(dst));
        std::memcpy(dst, src.data(), src.size() < sizeof(dst) - 1 ? src.size() : sizeof(dst) - 1);
    }
}

void GameServer::RecordRaidDamage(const std::string& playerName, int damage)
{
    if (damage > 0 && raid.bossId != 0)
    {
        raid.damageByName[playerName] += damage;
    }
}

void GameServer::SpawnRaidBoss(std::vector<std::uint32_t>& monsterMovers, RaidSends& raidSends)
{
    const float x = WorldGen::ARENA_X + 0.5f;
    const float y = WorldGen::ARENA_Y + 0.5f;
    const std::uint32_t id = nextMonsterId++;

    Monster boss{ id, MonsterBehavior::Chase, x, y, raidBossHp, WorldGen::MonsterKind::WorldBoss };
    boss.SetDefense(RAID_BOSS_DEFENSE);
    monsters.emplace(id, boss);
    sectorGrid.Add(id, x, y);
    monsterMovers.push_back(id);   // 근처 사람에게 spawn이 나가도록(부활과 같은 길)

    raid.bossId = id;
    raid.bossDown = false;
    raid.damageByName.clear();
    raid.slamTimer = RAID_SLAM_INTERVAL;
    raid.slamDelay = -1.0f;
    raid.statusTimer = 0.0f;   // 바로 상태 방송

    raidSends.announcements.push_back("왕 고양이가 북쪽 경기장(0, 48)에 나타났어요! 다 같이 잡아요!");
    std::cout << "[RAID] boss spawned id = " << id << std::endl;
}

void GameServer::UpdateRaid(float deltaSeconds, std::vector<std::uint32_t>& playerMovers,
                            std::vector<std::uint32_t>& monsterMovers, TickItemSends& itemSends, RaidSends& raidSends)
{
    // 1) 막타가 들어갔으면 정산 (정확히 한 번)
    if (raid.bossId != 0 && raid.bossDown)
    {
        FinishRaid(raidSends);
    }

    // 2) 보스가 없으면 등장 시간을 센다
    if (raid.bossId == 0)
    {
        raid.spawnTimer -= deltaSeconds;

        if (raid.spawnTimer <= 0.0f)
        {
            SpawnRaidBoss(monsterMovers, raidSends);
        }
    }

    auto bossIter = raid.bossId != 0 ? monsters.find(raid.bossId) : monsters.end();
    Monster* boss = (bossIter != monsters.end() && bossIter->second.IsAlive()) ? &bossIter->second : nullptr;

    // 3) 내려찍기: 예고 -> 1.2초 뒤 그 원 안에 남아 있는 사람 전원에게 피해
    if (boss != nullptr)
    {
        if (raid.slamDelay >= 0.0f)
        {
            raid.slamDelay -= deltaSeconds;

            if (raid.slamDelay < 0.0f)
            {
                std::vector<std::uint32_t> nearby;
                sectorGrid.GatherNearby(raid.slamX, raid.slamY, nearby);

                for (std::uint32_t id : nearby)
                {
                    if (IsMonsterId(id) || IsHouseId(id))
                    {
                        continue;
                    }

                    auto playerIter = players.find(id);

                    if (playerIter == players.end() ||
                        WithinRadius(playerIter->second.GetX(), playerIter->second.GetY(), raid.slamX, raid.slamY, RAID_SLAM_RADIUS) == false)
                    {
                        continue;
                    }

                    Player& target = playerIter->second;
                    PlayerStats& s = target.GetStats();
                    int damage = RAID_SLAM_DAMAGE - s.defense / 2;
                    if (damage < 1) { damage = 1; }
                    s.hp -= damage;

                    SC_DAMAGE_PACKET hit{};
                    hit.size = sizeof(SC_DAMAGE_PACKET);
                    hit.type = PACKET_TYPE::SC_DAMAGE;
                    hit.attackerId = raid.bossId;
                    hit.targetId = id;
                    hit.damage = damage;
                    hit.targetHp = s.hp > 0 ? s.hp : 0;
                    hit.targetMaxHp = s.maxHp;
                    itemSends.damages.emplace_back(id, hit);

                    if (s.hp <= 0)
                    {
                        KnockOut(id, target, playerMovers, itemSends);
                    }

                    itemSends.stats.emplace_back(id, target.GetStats());
                }
            }
        }
        else
        {
            raid.slamTimer -= deltaSeconds;

            if (raid.slamTimer <= 0.0f)
            {
                raid.slamTimer = RAID_SLAM_INTERVAL;
                raid.slamDelay = RAID_SLAM_WARNING;
                raid.slamX = boss->GetX();
                raid.slamY = boss->GetY();

                // 예고는 보스를 보고 있는 사람에게만
                raidSends.slam.size = sizeof(SC_RAID_SLAM_PACKET);
                raidSends.slam.type = PACKET_TYPE::SC_RAID_SLAM;
                raidSends.slam.x = raid.slamX;
                raidSends.slam.y = raid.slamY;
                raidSends.slam.radius = RAID_SLAM_RADIUS;
                raidSends.slam.delay = RAID_SLAM_WARNING;

                for (std::uint32_t watcherId : boss->GetWatchers())
                {
                    raidSends.slamTargets.push_back(watcherId);
                }
            }
        }
    }

    // 4) 1초마다 전원에게 상태 방송(HP 막대, 기여도 상위 5명, 다음 등장까지)
    raid.statusTimer -= deltaSeconds;

    if (raid.statusTimer > 0.0f)
    {
        return;
    }

    raid.statusTimer = RAID_STATUS_INTERVAL;

    SC_RAID_STATUS_PACKET& status = raidSends.status;
    status.size = sizeof(SC_RAID_STATUS_PACKET);
    status.type = PACKET_TYPE::SC_RAID_STATUS;
    status.state = boss != nullptr ? 1 : 0;
    status.secondsToSpawn = static_cast<std::uint16_t>(raid.spawnTimer > 0.0f ? raid.spawnTimer : 0.0f);
    status.participants = static_cast<std::uint16_t>(raid.damageByName.size());

    if (boss != nullptr)
    {
        status.bossId = raid.bossId;
        status.hp = boss->GetHp();
        status.maxHp = boss->GetMaxHp();
        status.x = boss->GetX();
        status.y = boss->GetY();

        // 상위 5명: 전체를 정렬하지 않고 partial_sort로 앞 5개만
        std::vector<std::pair<std::string, int>> ranked(raid.damageByName.begin(), raid.damageByName.end());
        const std::size_t count = std::min<std::size_t>(ranked.size(), RAID_TOP_COUNT);
        std::partial_sort(ranked.begin(), ranked.begin() + count, ranked.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });

        status.topCount = static_cast<std::uint8_t>(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            FillName(status.top[i].name, ranked[i].first);
            status.top[i].value = ranked[i].second;
            status.top[i].extra = static_cast<int>(1000LL * ranked[i].second / raidBossHp);
        }
    }

    raidSends.sendStatus = true;
    raidSends.statusTargets.reserve(players.size());

    for (const auto& [id, player] : players)
    {
        raidSends.statusTargets.push_back(id);
    }
}

// 정산. 기여도 순으로 줄 세워 보상을 준다. 상위 3명은 고양이 발톱.
//   해바라기씨 = 300 + 기여도 x 3000,  경험치 = 200 + 기여도 x 2000   (기여도 1% 이상만)
void GameServer::FinishRaid(RaidSends& raidSends)
{
    std::vector<std::pair<std::string, int>> ranked(raid.damageByName.begin(), raid.damageByName.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    // 이름 -> 접속 중인 플레이어
    std::unordered_map<std::string, std::uint32_t> online;
    for (auto& [id, player] : players)
    {
        online[player.GetName()] = id;
    }

    raid.lastResult.clear();
    const std::uint16_t participants = static_cast<std::uint16_t>(ranked.size());

    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        const std::string& name = ranked[i].first;
        const int damage = ranked[i].second;
        const float share = static_cast<float>(damage) / raidBossHp;

        if (raid.lastResult.size() < RANKING_COUNT)
        {
            RankEntry entry{};
            FillName(entry.name, name);
            entry.value = damage;
            entry.extra = static_cast<int>(share * 1000.0f);
            raid.lastResult.push_back(entry);
        }

        auto onlineIter = online.find(name);

        if (onlineIter == online.end() || share < RAID_MIN_SHARE)
        {
            continue;   // 나갔거나 1% 미만
        }

        Player& player = players.at(onlineIter->second);
        PlayerStats& s = player.GetStats();
        const int gold = 300 + static_cast<int>(share * 3000.0f);
        const int exp = 200 + static_cast<int>(share * 2000.0f);
        s.gold += gold;
        s.AddExp(exp);

        bool gotClaw = false;
        if (i < 3 && player.GetInventory().TryAdd(Items::CAT_CLAW, 1))
        {
            gotClaw = true;
        }
        player.GetInventory().TryAdd(Items::BLUEBERRY, 2);   // 가방이 꽉 찼으면 못 받는다

        SC_RAID_RESULT_PACKET result{};
        result.size = sizeof(SC_RAID_RESULT_PACKET);
        result.type = PACKET_TYPE::SC_RAID_RESULT;
        result.rank = static_cast<std::uint16_t>(i + 1);
        result.participants = participants;
        result.sharePermille = static_cast<std::uint16_t>(share * 1000.0f);
        result.gotClaw = gotClaw ? 1 : 0;
        result.damage = damage;
        result.gold = gold;
        result.exp = exp;
        raidSends.results.emplace_back(onlineIter->second, result);

        SC_INVENTORY_PACKET inventory{};
        player.GetInventory().FillPacket(inventory);
        raidSends.inventories.emplace_back(onlineIter->second, inventory);
        raidSends.stats.emplace_back(onlineIter->second, s);
    }

    // 랭킹 창의 '지난 레이드' 목록을 바꿔 끼운다(읽는 쪽은 락 없이 가져간다)
    raidRanking.store(std::make_shared<const RankingList>(raid.lastResult));

    // 보스를 지운다. 쓰러질 때 KillMonster가 격자/시야에서 이미 뺐다(부활 대기 상태). 부활하지 않게 아예 없앤다.
    monsters.erase(raid.bossId);
    raid.bossId = 0;
    raid.bossDown = false;
    raid.slamDelay = -1.0f;
    raid.spawnTimer = RAID_RESPAWN_SEC;

    std::string top = ranked.empty() ? "" : ranked.front().first;
    raidSends.announcements.push_back("왕 고양이를 물리쳤어요! 참가 " + std::to_string(participants) + "명" +
                                      (top.empty() ? "" : ", 1등 " + top));
    std::cout << "[RAID] boss down. participants = " << participants << std::endl;
}

// 이름이 "알림"인 채팅으로 전원에게. 락 밖에서 부른다.
void GameServer::BroadcastAnnouncement(const std::string& message)
{
    SC_CHAT_PACKET chat{};
    chat.size = sizeof(SC_CHAT_PACKET);
    chat.type = PACKET_TYPE::SC_CHAT;
    chat.id = 0;
    strncpy_s(chat.name, sizeof(chat.name), "알림", _TRUNCATE);
    strncpy_s(chat.message, sizeof(chat.message), message.c_str(), _TRUNCATE);
    BroadcastPacket(&chat, chat.size, 0);
}
