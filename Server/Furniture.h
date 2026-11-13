// 2026-11-13: 집 안 가구 (Docs/FurnitureDesign.md).
//
// 가구는 월드에 없다. 집 안(zoneId == houseId)에 있는 사람에게만 보내므로 시야(AOI) 목록과 섹터 격자에 넣지 않는다.
// 그래서 가구 id는 플레이어/집/몬스터 id와 섞이는 자리가 없다. FURNITURE_ID_BASE는 로그에서 구분하기 쉽게 주는 값일 뿐이다.
// (설계 문서는 IsMonsterId를 고치라고 했지만, 무한 월드에서 몬스터 id가 300만을 넘을 수 있어서
//  대역을 나누는 대신 '가구는 다른 컨테이너에만 있다'로 해결했다)

#pragma once

#include <cstdint>
#include <cmath>
#include "protocol.h"

constexpr std::uint8_t FURNITURE_TYPE_COUNT = 5;
constexpr std::uint8_t FURNITURE_RUG = 4;

// 가로, 세로 (집 안 좌표 기준, 회전 0일 때). 클라가 보낸 크기는 믿지 않고 이 표를 쓴다.
struct FurnitureSize { float width; float height; };

constexpr FurnitureSize FURNITURE_SIZE[FURNITURE_TYPE_COUNT] = {
    { 2.0f, 1.2f },   // 0 침대
    { 1.6f, 0.8f },   // 1 책상
    { 0.7f, 0.7f },   // 2 의자
    { 0.6f, 0.6f },   // 3 화분
    { 2.0f, 1.5f },   // 4 러그 (바닥 장식. 다른 가구와 겹쳐도 된다)
};

struct Furniture
{
    std::uint32_t id = 0;
    std::uint32_t houseId = 0;
    std::uint8_t itemType = 0;
    std::uint8_t rotation = 0;
    float x = 0.0f;
    float y = 0.0f;

    // 90/270도면 가로세로가 바뀐다. 회전이 4방향뿐이라 AABB로 충분하다.
    float HalfWidth() const
    {
        const FurnitureSize& s = FURNITURE_SIZE[itemType];
        return ((rotation & 1) ? s.height : s.width) * 0.5f;
    }

    float HalfHeight() const
    {
        const FurnitureSize& s = FURNITURE_SIZE[itemType];
        return ((rotation & 1) ? s.width : s.height) * 0.5f;
    }

    bool Overlaps(const Furniture& other) const
    {
        return std::fabs(x - other.x) < HalfWidth() + other.HalfWidth() &&
               std::fabs(y - other.y) < HalfHeight() + other.HalfHeight();
    }
};
