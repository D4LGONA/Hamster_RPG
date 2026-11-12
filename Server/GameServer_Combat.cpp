// 2026-09-22: GameServer.cpp에서 분리. 공격 처리와 공격 범위 판정.

#include "stdafx.h"
#include "GameServer.h"

// 공격. 클라는 '누구를' 때릴지만 보내고 사거리/쿨다운/데미지는 전부 여기서 판정한다.
// (클라 말을 믿으면 사거리 무시하고 아무나 때리거나 연타로 순삭시킬 수 있다)
// 방향 공격. 클라는 휘두른 방향만 보내고, 누가 맞았는지는 여기서 정한다.
// 부채꼴에 걸린 몬스터는 전부 맞으므로 한 번 휘둘러 여러 마리를 잡을 수도 있다.
void GameServer::ProcessAttack(std::uint32_t playerId, const CS_ATTACK_PACKET* packet)
{
    std::vector<std::pair<std::uint32_t, SC_DAMAGE_PACKET>> damageSends;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> deathSends;   // (받을 사람, 죽은 몬스터 id)

    std::vector<SC_ITEM_DROP_PACKET> dropSends;   // 때린 사람에게만(개인 드랍)

    int killCount = 0;
    bool leveledUp = false;
    PlayerStats attackerStats;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto attackerIter = players.find(playerId);

        if (attackerIter == players.end())
        {
            return;
        }

        Player& attacker = attackerIter->second;

        // 클라가 정규화해서 보내지만 그대로 믿지 않는다. 길이가 0이면 어디를 휘둘렀는지 알 수 없다.
        float dirX = packet->dx;
        float dirY = packet->dy;

        const float length = std::sqrt(dirX * dirX + dirY * dirY);

        if (length < 0.0001f)
        {
            return;
        }

        dirX /= length;
        dirY /= length;

        // 쿨다운. 타겟팅과 달리 빗나가도 휘두른 것이므로, 통과하면 맞든 안 맞든 소모시킨다.
        const auto now = std::chrono::steady_clock::now();
        const auto sinceLast = std::chrono::duration<float>(now - attacker.GetLastAttackAt()).count();

        if (sinceLast < ATTACK_COOLDOWN_SEC)
        {
            return;
        }

        attacker.SetLastAttackAt(now);

        std::vector<std::uint32_t> hitIds;
        GatherMonstersInArc(attacker.GetX(), attacker.GetY(), dirX, dirY, hitIds);

        for (std::uint32_t hitId : hitIds)
        {
            auto monsterIter = monsters.find(hitId);

            if (monsterIter == monsters.end())
            {
                continue;
            }

            Monster& monster = monsterIter->second;

            int damage = attacker.GetStats().attack - monster.GetDefense();

            if (damage < 1)
            {
                damage = 1;   // 방어력이 높아도 최소 1은 들어가게(안 그러면 영영 못 잡는다)
            }

            monster.TakeDamage(damage);

            SC_DAMAGE_PACKET damagePacket{};
            damagePacket.size = sizeof(SC_DAMAGE_PACKET);
            damagePacket.type = PACKET_TYPE::SC_DAMAGE;
            damagePacket.attackerId = playerId;
            damagePacket.targetId = hitId;
            damagePacket.damage = damage;
            damagePacket.targetHp = monster.GetHp();
            damagePacket.targetMaxHp = monster.GetMaxHp();

            // 그 몬스터를 보고 있는 사람들에게. 때린 나도 watchers에 들어있다.
            for (std::uint32_t watcherId : monster.GetWatchers())
            {
                damageSends.emplace_back(watcherId, damagePacket);
            }

            if (monster.IsAlive())
            {
                // 2026-11-13: 적대 몬스터는 때린 사람을 쫓아와 문다. 순둥한 애벌레는 반대쪽으로 도망간다.
                if (monster.IsHostile())
                {
                    monster.SetTargetId(playerId);
                    monster.SetAggroTimer(MONSTER_AGGRO_SEC);
                }
                else
                {
                    monster.StartFlee(attacker.GetX(), attacker.GetY(), MONSTER_FLEE_SEC);
                }
            }
            else
            {
                std::vector<std::uint32_t> deathTargets;
                KillMonster(monster, deathTargets);   // watchers를 비우므로 위 수집 뒤에 호출해야 한다

                for (std::uint32_t watcherId : deathTargets)
                {
                    deathSends.emplace_back(watcherId, hitId);
                }

                ++killCount;

                // 경험치/골드/드랍. 한 번 휘둘러 여러 마리를 잡으면 레벨이 두 번 오를 수도 있다
                GrantKillRewards(attacker, monster, leveledUp, dropSends);
            }
        }

        if (killCount > 0)
        {
            attackerStats = attacker.GetStats();
        }
    }

    // ---- 락 해제 후 전송 ----
    for (auto& [targetPlayerId, damagePacket] : damageSends)
    {
        SendPacket(targetPlayerId, &damagePacket, damagePacket.size);
    }

    for (auto& [targetPlayerId, monsterId] : deathSends)
    {
        SC_DEATH_PACKET death{};
        death.size = sizeof(SC_DEATH_PACKET);
        death.type = PACKET_TYPE::SC_DEATH;
        death.id = monsterId;

        SendPacket(targetPlayerId, &death, death.size);
    }

    for (SC_ITEM_DROP_PACKET& drop : dropSends)
    {
        SendPacket(playerId, &drop, drop.size);
    }

    if (killCount > 0)
    {
        SendStatUpdate(playerId, attackerStats);   // 경험치/골드(레벨업했으면 스탯까지) 갱신

        if (leveledUp)
        {
            std::cout << "[LEVEL UP] playerId = " << playerId
                      << ", level = " << attackerStats.level << std::endl;
        }
    }
}

// 휘두른 부채꼴에 든 살아있는 몬스터를 모은다.
// 주변 3x3 섹터만 훑으므로 월드에 몬스터가 많아져도 비용이 늘지 않는다.
void GameServer::GatherMonstersInArc(float originX, float originY, float dirX, float dirY,
                                     std::vector<std::uint32_t>& out)
{
    std::vector<std::uint32_t> nearby;
    sectorGrid.GatherNearby(originX, originY, nearby);

    for (std::uint32_t id : nearby)
    {
        if (IsMonsterId(id) == false)
        {
            continue;   // 플레이어는 아직 때릴 수 없다(PvP 별도)
        }

        auto iter = monsters.find(id);

        if (iter == monsters.end())
        {
            continue;
        }

        Monster& monster = iter->second;

        if (monster.IsAlive() == false)
        {
            continue;
        }

        const float toX = monster.GetX() - originX;
        const float toY = monster.GetY() - originY;

        const float distanceSq = toX * toX + toY * toY;

        if (distanceSq > ATTACK_RANGE * ATTACK_RANGE)
        {
            continue;
        }

        // 몸이 겹칠 만큼 붙어 있으면 방향을 따지지 않는다(0으로 나누는 것도 막힌다).
        if (distanceSq < 0.0001f)
        {
            out.push_back(id);
            continue;
        }

        // 휘두른 방향과 몬스터가 있는 방향 사이의 각. 내적이 cos(반각) 이상이면 부채꼴 안이다.
        const float dot = (toX * dirX + toY * dirY) / std::sqrt(distanceSq);

        if (dot < ATTACK_ARC_COS)
        {
            continue;
        }

        out.push_back(id);
    }
}
