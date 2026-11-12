// 2026-07-14: SectorGrid.h 제작. 오픈월드 시야처리(AOI)를 위한 섹터 격자.
// 넓은 월드를 격자로 나눠, 이동/스폰 시 "주변 섹터에 있는 후보"만 빠르게 찾는다.
// 전체 순회(O(N)) 대신 주변 9칸만 보므로 인원이 늘어도 비용이 크게 안 오른다.
#pragma once

#include "stdafx.h"
#include <unordered_set>

// ---------- 월드 / 섹터 설정 ----------
// 2026-11-12: 무한 월드. 격자를 고정 배열(80x80)에서 해시맵으로 바꿨다.
// 누가 있는 섹터만 메모리를 쓰고, 비면 지운다. 그래서 월드 크기와 메모리가 무관하다.
// WORLD_MIN/MAX는 float 정밀도를 지키려는 안전장치일 뿐이다(±10만에서도 좌표 오차 0.01 미만).
// 끝에서 끝까지 걸어서 11시간이라 사실상 끝이 없다.
constexpr float WORLD_MIN = -100000.0f;
constexpr float WORLD_MAX = 100000.0f;
constexpr float SECTOR_SIZE = 50.0f;   // 섹터 한 칸 크기
constexpr float VIEW_RANGE = 20.0f;    // 이 거리 안의 플레이어만 서로 보임(시야). 화면 20x20 기준 모서리(약14.1)+여유.
                                       // VIEW_RANGE <= SECTOR_SIZE 여야 주변 3x3만 봐도 시야가 정확함

class SectorGrid
{
public:
    // 좌표(한 축) -> 섹터 번호. 음수 쪽도 내림(-0.5 -> -1번 섹터).
    static int ToIndex(float value)
    {
        return static_cast<int>(std::floor(value / SECTOR_SIZE));
    }

    void Add(std::uint32_t id, float x, float y)
    {
        cells[CellKey(x, y)].insert(id);
    }

    void Remove(std::uint32_t id, float x, float y)
    {
        EraseFrom(CellKey(x, y), id);
    }

    // 섹터가 바뀐 경우에만 실제 이동 처리. 같은 칸이면 아무것도 안 함(비용 절약).
    void Move(std::uint32_t id, float oldX, float oldY, float newX, float newY)
    {
        std::int64_t oldCell = CellKey(oldX, oldY);
        std::int64_t newCell = CellKey(newX, newY);

        if (oldCell == newCell)
        {
            return;
        }

        EraseFrom(oldCell, id);
        cells[newCell].insert(id);
    }

    // (x,y) 주변 3x3 섹터에 있는 모든 id를 out에 모은다. (자기 자신 포함될 수 있으니 호출부에서 거른다)
    void GatherNearby(float x, float y, std::vector<std::uint32_t>& out) const
    {
        int col = ToIndex(x);
        int row = ToIndex(y);

        for (int r = row - 1; r <= row + 1; ++r)
        {
            for (int c = col - 1; c <= col + 1; ++c)
            {
                auto iter = cells.find(MakeKey(c, r));

                if (iter == cells.end())
                {
                    continue;
                }

                for (std::uint32_t id : iter->second)
                {
                    out.push_back(id);
                }
            }
        }
    }

    std::size_t GetCellCount() const { return cells.size(); }

private:
    static std::int64_t MakeKey(int col, int row)
    {
        return (static_cast<std::int64_t>(row) << 32) | static_cast<std::uint32_t>(col);
    }

    static std::int64_t CellKey(float x, float y)
    {
        return MakeKey(ToIndex(x), ToIndex(y));
    }

    void EraseFrom(std::int64_t key, std::uint32_t id)
    {
        auto iter = cells.find(key);

        if (iter == cells.end())
        {
            return;
        }

        iter->second.erase(id);

        if (iter->second.empty())
        {
            cells.erase(iter);   // 빈 섹터는 지운다. 안 그러면 돌아다닌 자리마다 빈 칸이 쌓인다
        }
    }

    // 섹터 격자. key = (row << 32) | col, 값 = 그 칸에 있는 id 집합. 누가 있는 칸만 들어 있다.
    std::unordered_map<std::int64_t, std::unordered_set<std::uint32_t>> cells;
};
