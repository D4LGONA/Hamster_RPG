// 2026-06-16: 플레이어 스탯 추가
// 2026-07-17: 경험치 획득 / 레벨업 추가.
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

    // 2026-11-13: 장착 무기 공격력. 서버가 장착/해제할 때 넣고 Recalculate가 attack에 더한다. 저장하지 않는다(무기에서 다시 계산).
    int bonusAttack = 0;

    // 경험치를 넣고 필요하면 레벨을 올린다. 한 번에 여러 레벨이 오를 수도 있어서 while로 돈다.
    // 레벨업했으면 true(클라에 알려줄지 판단용).
    bool AddExp(int amount)
    {
        if (amount <= 0)
        {
            return false;
        }

        exp += amount;
        bool leveledUp = false;

        while (exp >= requiredExp)
        {
            exp -= requiredExp;
            level++;
            requiredExp = 100 + (level - 1) * 50;
            leveledUp = true;
        }

        if (leveledUp)
        {
            Recalculate();   // maxHp/attack/defense 재계산
            hp = maxHp;      // 레벨업하면 회복시켜준다
        }

        return leveledUp;
    }

    // 2026-11-12: DB에서 불러온 원본 값을 넣는다. 나머지(maxHp, attack, requiredExp ...)는 레벨로 다시 계산.
    void LoadSaved(int savedLevel, int savedExp, int savedGold, int savedHp)
    {
        level = savedLevel < 1 ? 1 : savedLevel;
        exp = savedExp < 0 ? 0 : savedExp;
        gold = savedGold < 0 ? 0 : savedGold;
        hp = savedHp;
        requiredExp = 100 + (level - 1) * 50;   // AddExp의 레벨업 공식과 같아야 한다
        Recalculate();                          // hp가 0 이하면 여기서 가득 채운다
    }

    void Recalculate()
    {
        maxHp = 100 + (level - 1) * 20;
        attack = 10 + (level - 1) * 3 + bonusAttack;
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