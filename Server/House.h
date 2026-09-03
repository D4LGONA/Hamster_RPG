// 2026-07-18: House.h 제작. 플레이어가 월드에 설치하는 집.
// 몬스터와 마찬가지로 별도 맵으로 관리하되 섹터 격자/시야처리에는 함께 태운다.
// 움직이지 않으므로 이동 처리는 없고, 시야에 들어올 때 spawn만 나간다.
//
// 소유권을 id가 아니라 '이름'으로 건다. DB가 없어서 playerId는 재접속마다 바뀌는데,
// id로 걸어두면 잠깐 나갔다 들어온 사람이 자기 집을 잃어버리기 때문이다.
// (DB가 붙으면 계정 id로 바꾸면 된다)
#pragma once

#include "stdafx.h"
#include <unordered_set>

class House
{
public:
    House() = default;

    House(std::uint32_t newId, const std::string& newOwnerName, float newX, float newY)
        : id(newId), ownerName(newOwnerName), x(newX), y(newY)
    { }

    std::uint32_t GetId() const { return id; }
    const std::string& GetOwnerName() const { return ownerName; }

    float GetX() const { return x; }
    float GetY() const { return y; }

    // 나를 보고 있는 playerId 집합. 몬스터의 watchers와 같은 역할.
    std::unordered_set<std::uint32_t>& GetWatchers() { return watchers; }
    const std::unordered_set<std::uint32_t>& GetWatchers() const { return watchers; }

private:
    std::uint32_t id = 0;
    std::string ownerName;

    float x = 0.0f;
    float y = 0.0f;

    std::unordered_set<std::uint32_t> watchers;
};
