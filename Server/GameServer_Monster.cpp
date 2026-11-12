// 2026-09-22: GameServer.cpp에서 분리. 몬스터 스폰과 AI(배회/순찰/추격), 사망과 리스폰.

#include "stdafx.h"
#include "GameServer.h"

// 배회 방향을 새로 뽑는다. 계속 움직이면 부자연스러워서 절반은 그냥 쉰다.
void GameServer::PickWanderDirection(Monster& monster)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    if (chance(wanderRng) < MONSTER_IDLE_CHANCE)
    {
        monster.SetDir(0.0f, 0.0f);
    }
    else
    {
        constexpr float TWO_PI = 6.2831853f;
        std::uniform_real_distribution<float> angleDist(0.0f, TWO_PI);

        float angle = angleDist(wanderRng);
        monster.SetDir(std::cos(angle), std::sin(angle));
    }

    std::uniform_real_distribution<float> durationDist(MONSTER_WANDER_MIN_SEC, MONSTER_WANDER_MAX_SEC);
    monster.SetWanderTimer(durationDist(wanderRng));
}

// 배회: 주기적으로 방향을 새로 뽑고, 스폰 지점에서 너무 멀어지면 되돌아온다.
bool GameServer::StepWander(Monster& monster, float deltaSeconds, float& outX, float& outY)
{
    // 방향 전환 타이머
    float timer = monster.GetWanderTimer() - deltaSeconds;

    if (timer <= 0.0f)
    {
        PickWanderDirection(monster);   // 타이머도 여기서 새로 채워진다
    }
    else
    {
        monster.SetWanderTimer(timer);
    }

    // 배회 반경을 벗어나면 방향을 스폰 지점 쪽으로 강제한다.
    // 타이머 갱신 뒤에 하므로, 방금 '쉬기'가 뽑혔어도 반경 밖이면 이게 이긴다(밖에서 눌러앉지 않게).
    float awayX = monster.GetX() - monster.GetSpawnX();
    float awayY = monster.GetY() - monster.GetSpawnY();
    float awaySqr = awayX * awayX + awayY * awayY;

    if (awaySqr > MONSTER_LEASH_RADIUS * MONSTER_LEASH_RADIUS)
    {
        float dist = std::sqrt(awaySqr);   // 반경 밖이라 0이 될 수 없다
        monster.SetDir(-awayX / dist, -awayY / dist);
    }

    float dirX = monster.GetDirX();
    float dirY = monster.GetDirY();

    if (dirX == 0.0f && dirY == 0.0f)
    {
        return false;   // 쉬는 중
    }

    outX = monster.GetX() + dirX * MONSTER_MOVE_SPEED * deltaSeconds;
    outY = monster.GetY() + dirY * MONSTER_MOVE_SPEED * deltaSeconds;
    return true;
}

// 순찰: 스폰 지점 기준 상대좌표 경로(MONSTER_PATROL_PATH)의 경유지를 순서대로 돈다.
bool GameServer::StepPatrol(Monster& monster, float deltaSeconds, float& outX, float& outY)
{
    std::size_t index = monster.GetPatrolIndex();

    if (index >= MONSTER_PATROL_COUNT)
    {
        index = 0;   // 경로를 줄이는 수정이 들어와도 터지지 않게
    }

    float targetX = monster.GetSpawnX() + MONSTER_PATROL_PATH[index].x;
    float targetY = monster.GetSpawnY() + MONSTER_PATROL_PATH[index].y;

    float toX = targetX - monster.GetX();
    float toY = targetY - monster.GetY();
    float distSqr = toX * toX + toY * toY;

    float step = MONSTER_MOVE_SPEED * deltaSeconds;

    // 도착 판정은 이렇게 '남은 거리'로 직접 한다.
    // 아래 return false(=이번 틱에 움직이지 않았다)를 도착 신호로 재활용하면 안 된다.
    // 지금은 첫 경유지가 스폰 지점이라 시작하자마자 그 상황이 되는데, 둘을 엮어놨으면
    // 인덱스가 안 넘어가서 그 자리에 영영 붙어있게 된다.
    if (distSqr <= step * step)
    {
        monster.SetPatrolIndex((index + 1) % MONSTER_PATROL_COUNT);

        if (distSqr <= MONSTER_ARRIVE_DIST * MONSTER_ARRIVE_DIST)
        {
            return false;   // 이미 경유지 위. 다음 틱부터 새 목표로 움직인다.
        }

        outX = targetX;   // 한 틱 거리 안이면 지나치지 말고 딱 붙인다
        outY = targetY;
        return true;
    }

    float dist = std::sqrt(distSqr);   // 위에서 걸러졌으니 0이 아니다
    outX = monster.GetX() + (toX / dist) * step;
    outY = monster.GetY() + (toY / dist) * step;
    return true;
}

// 추격: 감지 범위 안에 플레이어가 들어오면 쫓아가고, 놓치면 스폰 지점으로 돌아간다.
// players를 읽으므로 반드시 playersLock을 잡은 채로(=틱 스레드에서) 불러야 한다.
bool GameServer::StepChase(Monster& monster, float deltaSeconds, float& outX, float& outY)
{
    std::uint32_t targetId = monster.GetTargetId();

    // 1) 쫓던 타겟이 아직 유효한가. 접속을 끊었거나, 뿌리쳤거나, 내가 집에서 너무 멀리 왔으면 접는다.
    if (targetId != 0)
    {
        auto targetIter = players.find(targetId);

        bool lost = (targetIter == players.end());

        if (lost == false)
        {
            const Player& target = targetIter->second;

            lost = WithinRadius(monster.GetX(), monster.GetY(), target.GetX(), target.GetY(), MONSTER_CHASE_GIVEUP_RADIUS) == false
                || WithinRadius(monster.GetX(), monster.GetY(), monster.GetSpawnX(), monster.GetSpawnY(), MONSTER_CHASE_LEASH_RADIUS) == false;
        }

        if (lost)
        {
            targetId = 0;
            monster.SetTargetId(0);
        }
    }

    // 2) 타겟이 없으면 감지 범위 안에서 가장 가까운 플레이어를 새로 잡는다.
    //    players 전체 순회 대신 섹터그리드로 주변 후보만 본다(시야처리와 같은 격자 재활용).
    //    단 집에서 이미 리쉬 밖으로 나와 있으면 잡지 않는다. 이 조건이 없으면 1)이 리쉬로 풀어놓은
    //    타겟을 바로 그 자리에서 다시 물어서(플레이어는 아직 감지 범위 안에 있으므로) 리쉬가
    //    아무 일도 못 하고, 플레이어가 몬스터를 매달고 월드 끝까지 끌고 갈 수 있다.
    if (targetId == 0
        && WithinRadius(monster.GetX(), monster.GetY(), monster.GetSpawnX(), monster.GetSpawnY(), MONSTER_CHASE_LEASH_RADIUS))
    {
        std::vector<std::uint32_t> nearby;
        sectorGrid.GatherNearby(monster.GetX(), monster.GetY(), nearby);

        float bestSqr = MONSTER_DETECT_RADIUS * MONSTER_DETECT_RADIUS;

        for (std::uint32_t otherId : nearby)
        {
            if (IsMonsterId(otherId))
            {
                continue;   // 몬스터끼리는 싸우지 않는다
            }

            auto playerIter = players.find(otherId);

            if (playerIter == players.end())
            {
                continue;
            }

            float dx = playerIter->second.GetX() - monster.GetX();
            float dy = playerIter->second.GetY() - monster.GetY();
            float distSqr = dx * dx + dy * dy;

            if (distSqr <= bestSqr)
            {
                bestSqr = distSqr;
                targetId = otherId;
            }
        }

        monster.SetTargetId(targetId);
    }

    // 3) 타겟이 있으면 그쪽으로, 없으면 집으로.
    float goalX = monster.GetSpawnX();
    float goalY = monster.GetSpawnY();
    float stopDist = MONSTER_ARRIVE_DIST;

    if (targetId != 0)
    {
        const Player& target = players.find(targetId)->second;   // 위 1)/2)에서 존재가 보장된다

        goalX = target.GetX();
        goalY = target.GetY();
        stopDist = MONSTER_CHASE_STOP_DIST;   // 몸이 겹치지 않게 조금 앞에서 멈춘다
    }

    float toX = goalX - monster.GetX();
    float toY = goalY - monster.GetY();
    float distSqr = toX * toX + toY * toY;

    if (distSqr <= stopDist * stopDist)
    {
        return false;   // 이미 붙었다(또는 집에 도착했다)
    }

    float dist = std::sqrt(distSqr);   // 위에서 걸러졌으니 0이 아니다
    float travel = dist - stopDist;    // 멈출 지점까지만. 지나쳐서 앞뒤로 떠는 걸 막는다
    float step = MONSTER_MOVE_SPEED * deltaSeconds;

    if (travel > step)
    {
        travel = step;
    }

    outX = monster.GetX() + (toX / dist) * travel;
    outY = monster.GetY() + (toY / dist) * travel;
    return true;
}

// 몬스터를 죽은 상태로 만든다. 격자에서 빼서 더는 시야 계산에 안 잡히게 하고,
// 보고 있던 사람들의 viewList도 같이 정리한다(watchers <-> viewList 불변식).
// 전송은 락 밖에서 해야 하므로 통지 대상만 모아서 돌려준다.
// playersLock을 잡은 상태에서 호출할 것.
void GameServer::KillMonster(Monster& monster, std::vector<std::uint32_t>& deathTargets)
{
    std::uint32_t monsterId = monster.GetId();

    sectorGrid.Remove(monsterId, monster.GetX(), monster.GetY());

    for (std::uint32_t watcherId : monster.GetWatchers())
    {
        auto playerIter = players.find(watcherId);

        if (playerIter != players.end())
        {
            playerIter->second.GetViewList().erase(monsterId);
        }

        deathTargets.push_back(watcherId);
    }

    monster.GetWatchers().clear();
    monster.SetRespawnTimer(MONSTER_RESPAWN_SEC);
}

// 죽은 몬스터의 부활 타이머. 다 된 놈은 집에서 hp를 채워 되살리고 격자에 다시 넣는다.
// 되살아난 id는 respawned에 담아 호출부(UpdateWorld)의 몬스터 시야 diff로 넘긴다.
// watchers가 비어 있으므로 그 diff가 주변 플레이어 전원을 '신규'로 보고 spawn을 쏴준다.
// playersLock을 잡은 상태에서 호출할 것.
void GameServer::UpdateMonsterRespawn(float deltaSeconds, std::vector<std::uint32_t>& respawned)
{
    for (auto& [id, monster] : monsters)
    {
        if (monster.IsAlive())
        {
            continue;
        }

        float timer = monster.GetRespawnTimer() - deltaSeconds;

        if (timer > 0.0f)
        {
            monster.SetRespawnTimer(timer);
            continue;
        }

        monster.Respawn();
        sectorGrid.Add(id, monster.GetX(), monster.GetY());
        respawned.push_back(id);
    }
}

// 몬스터 한 틱: 행동 타입별로 이동 -> 실제로 움직인 놈만 movers에 담는다.
// 경계 clamp / 섹터그리드 갱신은 타입과 무관하므로 여기서 공통 처리한다.
// playersLock을 잡은 상태에서 호출할 것.
void GameServer::UpdateMonsters(float deltaSeconds, std::vector<std::uint32_t>& movers)
{
    for (auto& [id, monster] : monsters)
    {
        if (monster.IsAlive() == false)
        {
            continue;
        }

        float newX = 0.0f;
        float newY = 0.0f;
        bool moved = false;

        switch (monster.GetBehavior())
        {
        case MonsterBehavior::Wander: moved = StepWander(monster, deltaSeconds, newX, newY); break;
        case MonsterBehavior::Patrol: moved = StepPatrol(monster, deltaSeconds, newX, newY); break;
        case MonsterBehavior::Chase:  moved = StepChase(monster, deltaSeconds, newX, newY);  break;
        }

        if (moved == false)
        {
            continue;
        }

        float oldX = monster.GetX();
        float oldY = monster.GetY();

        // 집을 통과하지 못하게 한다. 플레이어는 집에 못 들어가는데 몬스터만 들어갈 수 있으면
        // 집 안에 들어앉은 몬스터를 아무도 잡을 수 없게 된다.
        // 이미 안에 있다면(집이 나중에 지어진 경우) 충돌을 꺼서 걸어 나올 수 있게 둔다.
        const bool monsterStuckInside = IsBlocked(oldX, oldY);

        if (monsterStuckInside == false)
        {
            // 플레이어와 같은 방식으로 축을 따로 판정해 벽을 따라 미끄러지게 한다.
            if (IsBlocked(newX, oldY))
            {
                newX = oldX;
            }

            if (IsBlocked(newX, newY))
            {
                newY = oldY;
            }
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

        monster.SetPosition(newX, newY);
        sectorGrid.Move(id, oldX, oldY, newX, newY);
        movers.push_back(id);
    }
}
