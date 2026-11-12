// 2026-07-15: Monster.h 제작. 월드에 돌아다니는 몬스터(NPC) 엔티티.
// 플레이어와 별도 맵으로 관리하되, 섹터그리드/시야처리는 함께 태운다.
// 2026-07-16: 행동 타입(MonsterBehavior) 추가. 타입별 이동은 GameServer의 Step* 함수가 담당.
#pragma once

#include "stdafx.h"
#include <unordered_set>
#include "WorldGen.h"

// 몬스터 행동 타입. 이 값에 따라 GameServer가 매 틱 어떤 이동 함수를 부를지 고른다.
enum class MonsterBehavior : std::uint8_t
{
    Wander,   // 스폰 지점 주변을 어슬렁거린다
    Patrol,   // 정해진 경로를 돈다
    Chase,    // 범위 안에 플레이어가 들어오면 쫓아온다
};

class Monster
{
public:
    Monster() = default;

    Monster(std::uint32_t newId, MonsterBehavior newBehavior, float newX, float newY, int newMaxHp,
            WorldGen::MonsterKind newKind = WorldGen::MonsterKind::Caterpillar)
        : id(newId), behavior(newBehavior), kind(newKind)
        , x(newX), y(newY)
        , spawnX(newX), spawnY(newY)
        , hp(newMaxHp), maxHp(newMaxHp)
    { }

    std::uint32_t GetId() const { return id; }
    MonsterBehavior GetBehavior() const { return behavior; }
    WorldGen::MonsterKind GetKind() const { return kind; }   // 외형/체력 구분. 클라에는 스폰 패킷 name으로 전한다

    float GetX() const { return x; }
    float GetY() const { return y; }
    void SetPosition(float newX, float newY) { x = newX; y = newY; }

    // 스폰 지점. 배회 반경/순찰 경로/복귀 지점의 기준이 된다.
    float GetSpawnX() const { return spawnX; }
    float GetSpawnY() const { return spawnY; }

    int GetHp() const { return hp; }
    int GetMaxHp() const { return maxHp; }
    bool IsAlive() const { return hp > 0; }
    void TakeDamage(int damage) { hp -= damage; if (hp < 0) { hp = 0; } }

    int GetAttack() const { return attack; }
    int GetDefense() const { return defense; }
    void SetCombatStats(int newAttack, int newDefense) { attack = newAttack; defense = newDefense; }

    // 죽으면 리스폰 타이머가 돌고, 다 되면 집에서 hp를 채워 되살아난다.
    float GetRespawnTimer() const { return respawnTimer; }
    void SetRespawnTimer(float value) { respawnTimer = value; }

    void Respawn()
    {
        hp = maxHp;
        x = spawnX;
        y = spawnY;
        respawnTimer = 0.0f;

        // 되살아날 땐 AI 상태도 처음으로. 안 그러면 죽기 직전 쫓던 타겟을 그대로 물고 시작한다.
        dirX = 0.0f;
        dirY = 0.0f;
        wanderTimer = 0.0f;
        patrolIndex = 0;
        targetId = 0;
        aggroTimer = 0.0f;
        attackCooldown = 0.0f;
    }

    // 나를 보고 있는 playerId 집합. 이동/피격/죽음을 이들에게만 전송한다.
    std::unordered_set<std::uint32_t>& GetWatchers() { return watchers; }
    const std::unordered_set<std::uint32_t>& GetWatchers() const { return watchers; }

    // --- Wander 상태값. dir이 (0,0)이면 쉬는 중. ---
    float GetDirX() const { return dirX; }
    float GetDirY() const { return dirY; }
    void SetDir(float newDirX, float newDirY) { dirX = newDirX; dirY = newDirY; }

    float GetWanderTimer() const { return wanderTimer; }
    void SetWanderTimer(float value) { wanderTimer = value; }

    // --- Patrol 상태값. 지금 향하고 있는 경유지 번호. ---
    std::size_t GetPatrolIndex() const { return patrolIndex; }
    void SetPatrolIndex(std::size_t value) { patrolIndex = value; }

    // --- Chase 상태값. 추격 중인 playerId. 0이면 없음. ---
    // 2026-11-13: 반격. 맞으면 aggroTimer 동안 때린 사람을 쫓아와 문다(배회/순찰 몬스터도).
    float GetAggroTimer() const { return aggroTimer; }
    void SetAggroTimer(float value) { aggroTimer = value; }
    float GetAttackCooldown() const { return attackCooldown; }
    void SetAttackCooldown(float value) { attackCooldown = value; }

    std::uint32_t GetTargetId() const { return targetId; }
    void SetTargetId(std::uint32_t value) { targetId = value; }

private:
    std::uint32_t id = 0;
    MonsterBehavior behavior = MonsterBehavior::Wander;
    WorldGen::MonsterKind kind = WorldGen::MonsterKind::Caterpillar;

    float x = 0.0f;
    float y = 0.0f;

    float spawnX = 0.0f;
    float spawnY = 0.0f;

    int hp = 0;
    int maxHp = 0;
    int attack = 5;
    int defense = 1;

    float respawnTimer = 0.0f;   // 0보다 크면 죽어서 부활을 기다리는 중

    std::unordered_set<std::uint32_t> watchers;

    float dirX = 0.0f;
    float dirY = 0.0f;
    float wanderTimer = 0.0f;

    std::size_t patrolIndex = 0;

    std::uint32_t targetId = 0;
    float aggroTimer = 0.0f;
    float attackCooldown = 0.0f;
};
