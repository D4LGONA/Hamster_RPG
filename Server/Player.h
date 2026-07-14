// 2026-06-11: Player.h 파일 제작. 게임 월드 내 플레이어 상태를 관리하는 클래스.
// explicit : 생성자에 명시적으로 선언하여 암시적 변환을 방지하는 키워드.
// 2026-06-16: PlayerStats 추가.

#pragma once

#include "stdafx.h"
#include "PlayerStats.h"

class Player
{
public:
    Player() = default; // 기본 생성자, Player(){}와 같음.

    Player(std::uint32_t newId, const std::string& newName, float newX, float newY)
        : id(newId), name(newName), x(newX), y(newY), loggedIn(true)
    { }

    bool IsLoggedIn() const { return loggedIn; }
    void RecalculateStats() { stats.Recalculate(); }

    // --------- Getter / Setter ---------
    std::uint32_t GetId() const { return id; }

    const std::string& GetName() const { return name; }
    float GetX() const { return x; }
    float GetY() const { return y; }
    PlayerStats& GetStats() { return stats; }

    const PlayerStats& GetStats() const { return stats; }

    void SetLoggedIn(bool newLoggedIn) { loggedIn = newLoggedIn; }
    void SetPosition(float newX, float newY) { x = newX; y = newY; }
    void SetName(const std::string& newName) { name = newName; }

private:
    std::uint32_t id = 0;
    std::string name;

    float x = 0.0f;
    float y = 0.0f;

    bool loggedIn = false;

    PlayerStats stats;
};