// 2026-06-11: Player.h 파일 제작. 게임 월드 내 플레이어 상태를 관리하는 클래스.
// explicit : 생성자에 명시적으로 선언하여 암시적 변환을 방지하는 키워드.
// 2026-06-16: PlayerStats 추가.

#pragma once

#include "stdafx.h"
#include <unordered_set>
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

    // 시야처리(AOI)용: 지금 내 눈에 보이는 상대 playerId 집합. playersLock으로 보호됨.
    std::unordered_set<std::uint32_t>& GetViewList() { return viewList; }
    const std::unordered_set<std::uint32_t>& GetViewList() const { return viewList; }

    // 서버 권위 이동용: 클라가 보낸 최신 입력 방향. 게임 틱 루프가 매 틱 읽어 위치를 적분한다.
    float GetInputX() const { return inputX; }
    float GetInputY() const { return inputY; }
    void SetInput(float newInputX, float newInputY) { inputX = newInputX; inputY = newInputY; }

    // 공격 쿨다운: 마지막으로 때린 시각. 서버가 들고 있어야 클라가 연타로 속일 수 없다.
    std::chrono::steady_clock::time_point GetLastAttackAt() const { return lastAttackAt; }
    void SetLastAttackAt(std::chrono::steady_clock::time_point value) { lastAttackAt = value; }

    // 지금 있는 존. 0이면 월드(바깥), 그 외에는 그 집의 id.
    // 존이 다르면 서로 보이지 않는다. 집 안에 있는 동안은 섹터 격자에서 빠져 있다.
    std::uint32_t GetZoneId() const { return zoneId; }
    void SetZoneId(std::uint32_t value) { zoneId = value; }

    // 문에 닿아 있는 상태인가. 닿는 '순간'에만 들어가게 하려고 둔다.
    // 이게 없으면 문 앞에 서 있는 동안 매 틱 입장 처리가 돌아버린다.
    bool IsInDoorZone() const { return inDoorZone; }
    void SetInDoorZone(bool value) { inDoorZone = value; }

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

    std::unordered_set<std::uint32_t> viewList;

    // 최신 입력 방향(정규화). 서버 틱이 pos += input * moveSpeed * dt 로 적분.
    float inputX = 0.0f;
    float inputY = 0.0f;

    std::chrono::steady_clock::time_point lastAttackAt{};

    std::uint32_t zoneId = 0;
    bool inDoorZone = false;
};