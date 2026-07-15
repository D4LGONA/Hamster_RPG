// 2026-07-14: SectorGrid.h 제작. 오픈월드 시야처리(AOI)를 위한 섹터 격자.
// 넓은 월드를 격자로 나눠, 이동/스폰 시 "주변 섹터에 있는 후보"만 빠르게 찾는다.
// 전체 순회(O(N)) 대신 주변 9칸만 보므로 인원이 늘어도 비용이 크게 안 오른다.
#pragma once

#include "stdafx.h"
#include <unordered_set>

// ---------- 월드 / 섹터 설정 ----------
constexpr float WORLD_MIN = -2000.0f;
constexpr float WORLD_MAX = 2000.0f;   // 가로/세로 4000 유닛
constexpr float SECTOR_SIZE = 50.0f;   // 섹터 한 칸 크기
constexpr float VIEW_RANGE = 20.0f;    // 이 거리 안의 플레이어만 서로 보임(시야). 화면 20x20 기준 모서리(약14.1)+여유.

// 한 축당 섹터 개수. (VIEW_RANGE <= SECTOR_SIZE 여야 주변 3x3만 봐도 시야가 정확함)
constexpr int SECTOR_COUNT = static_cast<int>((WORLD_MAX - WORLD_MIN) / SECTOR_SIZE); // 80

class SectorGrid
{
public:
    SectorGrid()
        : cells(static_cast<std::size_t>(SECTOR_COUNT) * SECTOR_COUNT)
    {
    }

    // 좌표(한 축) -> 섹터 인덱스. 월드 밖이면 가장자리 칸으로 clamp.
    static int ToIndex(float value)
    {
        int index = static_cast<int>((value - WORLD_MIN) / SECTOR_SIZE);

        if (index < 0)
        {
            index = 0;
        }

        if (index >= SECTOR_COUNT)
        {
            index = SECTOR_COUNT - 1;
        }

        return index;
    }

    void Add(std::uint32_t id, float x, float y)
    {
        cells[CellIndex(x, y)].insert(id);
    }

    void Remove(std::uint32_t id, float x, float y)
    {
        cells[CellIndex(x, y)].erase(id);
    }

    // 섹터가 바뀐 경우에만 실제 이동 처리. 같은 칸이면 아무것도 안 함(비용 절약).
    void Move(std::uint32_t id, float oldX, float oldY, float newX, float newY)
    {
        int oldCell = CellIndex(oldX, oldY);
        int newCell = CellIndex(newX, newY);

        if (oldCell == newCell)
        {
            return;
        }

        cells[oldCell].erase(id);
        cells[newCell].insert(id);
    }

    // (x,y) 주변 3x3 섹터에 있는 모든 id를 out에 모은다. (자기 자신 포함될 수 있으니 호출부에서 거른다)
    void GatherNearby(float x, float y, std::vector<std::uint32_t>& out) const
    {
        int col = ToIndex(x);
        int row = ToIndex(y);

        for (int r = row - 1; r <= row + 1; ++r)
        {
            if (r < 0 || r >= SECTOR_COUNT)
            {
                continue;
            }

            for (int c = col - 1; c <= col + 1; ++c)
            {
                if (c < 0 || c >= SECTOR_COUNT)
                {
                    continue;
                }

                const auto& cell = cells[static_cast<std::size_t>(r) * SECTOR_COUNT + c];

                for (std::uint32_t id : cell)
                {
                    out.push_back(id);
                }
            }
        }
    }

private:
    int CellIndex(float x, float y) const
    {
        int col = ToIndex(x);
        int row = ToIndex(y);
        return row * SECTOR_COUNT + col;
    }

    // 섹터 격자. cells[row * SECTOR_COUNT + col] = 그 칸에 있는 playerId 집합.
    std::vector<std::unordered_set<std::uint32_t>> cells;
};
