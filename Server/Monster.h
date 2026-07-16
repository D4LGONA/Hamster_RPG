// 2026-07-15: Monster.h 제작. 월드에 돌아다니는 몬스터(NPC) 엔티티.
// 플레이어와 별도 맵으로 관리하되, 섹터그리드/시야처리는 함께 태운다.
#pragma once

#include "stdafx.h"
#include <unordered_set>

class Monster
{
public:
    Monster() = default;

    Monster(std::uint32_t newId, float newX, float newY, int newMaxHp)
        : id(newId), x(newX), y(newY), hp(newMaxHp), maxHp(newMaxHp)
    { }

    std::uint32_t GetId() const { return id; }
    float GetX() const { return x; }
    float GetY() const { return y; }

    int GetHp() const { return hp; }
    int GetMaxHp() const { return maxHp; }
    bool IsAlive() const { return hp > 0; }

    void SetPosition(float newX, float newY) { x = newX; y = newY; }
    void TakeDamage(int damage) { hp -= damage; if (hp < 0) { hp = 0; } }

    // 나를 보고 있는 playerId 집합. 이동/피격/죽음을 이들에게만 전송한다.
    std::unordered_set<std::uint32_t>& GetWatchers() { return watchers; }
    const std::unordered_set<std::uint32_t>& GetWatchers() const { return watchers; }

    // 배회 AI 상태(1b에서 사용). 현재 이동 방향과 다음 방향 전환까지 남은 시간.
    float GetDirX() const { return dirX; }
    float GetDirY() const { return dirY; }
    void SetDir(float newDirX, float newDirY) { dirX = newDirX; dirY = newDirY; }

    float GetWanderTimer() const { return wanderTimer; }
    void SetWanderTimer(float value) { wanderTimer = value; }

private:
    std::uint32_t id = 0;

    float x = 0.0f;
    float y = 0.0f;

    int hp = 0;
    int maxHp = 0;

    std::unordered_set<std::uint32_t> watchers;

    float dirX = 0.0f;
    float dirY = 0.0f;
    float wanderTimer = 0.0f;
};
