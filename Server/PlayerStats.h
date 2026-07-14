// 2026-06-16: 플레이어 스탯 추가
#pragma once

struct PlayerStats
{
    int level = 1;
    int exp = 0;
    int requiredExp = 100;
    int gold = 0;

    int hp = 100;
    int maxHp = 100;
    int attack = 10;
    int defense = 3;

    float moveSpeed = 5.0f;

    int combatPower = 0;

    void Recalculate()
    {
        maxHp = 100 + (level - 1) * 20;
        attack = 10 + (level - 1) * 3;
        defense = 3 + (level - 1);

        if (hp > maxHp)
        {
            hp = maxHp;
        }

        if (hp <= 0)
        {
            hp = maxHp;
        }

        moveSpeed = 5.0f;

        combatPower = attack * 10 + defense * 6 + maxHp + level * 30;
    }
};