// 2026-09-26: Furniture.h 제작. 집 안에 놓는 가구.
// 설계: Docs/FurnitureDesign.md
//
// House와 달리 watchers가 없다. 집 안은 ±4.5짜리 방이라 시야처리(AOI)를 쓰지 않고,
// 같은 집(zoneId)에 있는 사람 전원에게 그냥 보낸다.
//
// 월드에는 존재하지 않으므로 섹터 격자에도 넣지 않는다.
#pragma once

#include "stdafx.h"
#include "protocol.h"

class Furniture
{
public:
    Furniture() = default;

    Furniture(std::uint32_t newId, std::uint32_t newHouseId, std::uint32_t newItemType,
              float newX, float newY, std::uint8_t newRotation)
        : id(newId), houseId(newHouseId), itemType(newItemType)
        , x(newX), y(newY), rotation(newRotation)
    { }

    std::uint32_t GetId() const { return id; }
    std::uint32_t GetHouseId() const { return houseId; }
    std::uint32_t GetItemType() const { return itemType; }

    float GetX() const { return x; }
    float GetY() const { return y; }

    std::uint8_t GetRotation() const { return rotation; }

    // 이동(회전 포함). 검증은 전부 호출부(GameServer)가 하고 여기선 값만 바꾼다.
    void SetPlacement(float newX, float newY, std::uint8_t newRotation)
    {
        x = newX;
        y = newY;
        rotation = newRotation;
    }

    // 회전을 반영한 차지 공간. 90도/270도면 가로세로가 바뀐다.
    // 회전이 네 방향뿐이라 AABB(축 정렬 사각형)로 충돌을 볼 수 있다.
    void GetHalfExtent(float& halfW, float& halfH) const
    {
        const FurnitureSize& size = FURNITURE_SIZE[itemType < FURNITURE_TYPE_COUNT ? itemType : 0];

        if (rotation == 1 || rotation == 3)
        {
            halfW = size.height * 0.5f;
            halfH = size.width * 0.5f;
        }
        else
        {
            halfW = size.width * 0.5f;
            halfH = size.height * 0.5f;
        }
    }

private:
    std::uint32_t id = 0;
    std::uint32_t houseId = 0;      // 어느 집에 있는지 (= 그 집에 들어간 사람의 zoneId)
    std::uint32_t itemType = 0;     // FURNITURE_BED 등. 클라가 프리팹을 고르는 값

    float x = 0.0f;                 // 집 안 좌표 (월드 좌표와 별개)
    float y = 0.0f;

    std::uint8_t rotation = 0;      // 0=0도, 1=90도, 2=180도, 3=270도
};
