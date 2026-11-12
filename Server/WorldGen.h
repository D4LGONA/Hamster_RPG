// 2026-11-12: 무한 월드 생성 공식. 맵 파일이 없다.
//
// 월드를 16x16칸 청크로 나누고, 청크 안의 장애물/몬스터 자리를 (시드, 청크 좌표)의 해시로 정한다.
// 같은 입력이면 언제 어디서 계산해도 같은 결과가 나오므로
//   - 서버는 충돌 판정과 몬스터 배치에,
//   - 클라(Assets/Scripts/World/WorldGen.cs)는 그리기에
// 이 공식을 따로 돌린다. 둘이 맵 데이터를 주고받을 필요가 없다.
//
// !!! 이 파일을 고치면 WorldGen.cs 도 똑같이 고쳐야 한다. 한쪽만 바뀌면 보이는 나무와 막히는 나무가 어긋난다.
//     Tools/worldgen_check/ 가 두 구현의 결과를 비교한다.
//
// 표준 라이브러리 난수(std::mt19937 등)는 쓰지 않는다. C#과 결과를 맞출 수 없어서다.
// 대신 정수 해시(murmur3 finalizer)만 쓰고, 부호 없는 32비트 곱셈의 오버플로(mod 2^32)는 두 언어에서 같다.

#pragma once

#include <cstdint>
#include <cmath>
#include <vector>

namespace WorldGen
{
    constexpr std::uint32_t SEED = 20261112u;
    constexpr int CHUNK_SIZE = 16;              // 한 청크 = 16x16칸 = 16x16유닛
    constexpr int SAFE_RADIUS_TILES = 8;        // 원점 스폰 광장. 이 안에는 장애물/몬스터를 두지 않는다
    constexpr int BOSS_MIN_DISTANCE = 48;      // 보스는 원점에서 이만큼은 떨어진 곳에만
    constexpr std::uint32_t BOSS_CHANCE = 30;   // 조건을 만족하는 청크 중 1/30

    enum class Obstacle : std::uint8_t { None = 0, Tree = 1, Bush = 2, Rock = 3 };
    enum class MonsterKind : std::uint8_t { Caterpillar = 0, Ladybug = 1, CatBoss = 2 };

    struct ObstacleSpot { int tileX; int tileY; Obstacle kind; };
    struct MonsterSpot { float x; float y; MonsterKind kind; };

    inline std::uint32_t Hash(int a, int b, int c)
    {
        std::uint32_t h = SEED;
        h ^= static_cast<std::uint32_t>(a) * 0x8DA6B343u;
        h ^= static_cast<std::uint32_t>(b) * 0xD8163841u;
        h ^= static_cast<std::uint32_t>(c) * 0xCB1AB31Fu;
        h ^= h >> 16; h *= 0x85EBCA6Bu;
        h ^= h >> 13; h *= 0xC2B2AE35u;
        h ^= h >> 16;
        return h;
    }

    // 음수에서도 내림이 되는 나눗셈. -1 / 16 은 0이 아니라 -1 청크여야 한다.
    inline int FloorDiv(int value, int divisor)
    {
        int q = value / divisor;
        if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) { --q; }
        return q;
    }

    inline int TileOf(float worldPos) { return static_cast<int>(std::floor(worldPos)); }
    inline int ChunkOf(int tile) { return FloorDiv(tile, CHUNK_SIZE); }

    inline bool InSafeZone(int tileX, int tileY)
    {
        return std::abs(tileX) <= SAFE_RADIUS_TILES && std::abs(tileY) <= SAFE_RADIUS_TILES;
    }

    // 청크 하나의 장애물. 2~5개. 청크 가장자리 한 줄은 비워서 이웃 청크와 붙어 벽이 되지 않게 한다.
    inline void ObstaclesInChunk(int chunkX, int chunkY, std::vector<ObstacleSpot>& out)
    {
        const std::uint32_t count = 2 + Hash(chunkX, chunkY, 0) % 4;

        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::uint32_t h = Hash(chunkX, chunkY, 1 + static_cast<int>(i));
            const int tileX = chunkX * CHUNK_SIZE + 1 + static_cast<int>(h % 14);
            const int tileY = chunkY * CHUNK_SIZE + 1 + static_cast<int>((h >> 8) % 14);

            if (InSafeZone(tileX, tileY))
            {
                continue;
            }

            const std::uint32_t roll = (h >> 16) % 10;
            const Obstacle kind = roll < 5 ? Obstacle::Tree : (roll < 8 ? Obstacle::Bush : Obstacle::Rock);
            out.push_back({ tileX, tileY, kind });
        }
    }

    inline Obstacle ObstacleAt(int tileX, int tileY)
    {
        std::vector<ObstacleSpot> spots;
        ObstaclesInChunk(ChunkOf(tileX), ChunkOf(tileY), spots);

        for (const ObstacleSpot& s : spots)
        {
            if (s.tileX == tileX && s.tileY == tileY)
            {
                return s.kind;
            }
        }

        return Obstacle::None;
    }

    // 반지름 radius인 몸이 (x, y)에 서면 장애물에 걸치는가. 몸을 정사각형으로 보고 네 모서리 칸을 본다.
    inline bool IsBlocked(float x, float y, float radius)
    {
        return ObstacleAt(TileOf(x - radius), TileOf(y - radius)) != Obstacle::None ||
               ObstacleAt(TileOf(x + radius), TileOf(y - radius)) != Obstacle::None ||
               ObstacleAt(TileOf(x - radius), TileOf(y + radius)) != Obstacle::None ||
               ObstacleAt(TileOf(x + radius), TileOf(y + radius)) != Obstacle::None;
    }

    // 한 변이 2*half인 정사각형이 덮는 칸 중 장애물이 하나라도 있는가. 집(3x3) 설치 검사용.
    inline bool IsAreaBlocked(float x, float y, float half)
    {
        for (int ty = TileOf(y - half); ty <= TileOf(y + half); ++ty)
        {
            for (int tx = TileOf(x - half); tx <= TileOf(x + half); ++tx)
            {
                if (ObstacleAt(tx, ty) != Obstacle::None)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // 청크 하나의 몬스터 자리. 원점에서 멀수록 무당벌레 비율이 오르고, 멀리서 가끔 고양이 보스가 나온다.
    inline void MonstersInChunk(int chunkX, int chunkY, std::vector<MonsterSpot>& out)
    {
        // 거리는 정수로만 계산한다. float sqrt는 C#과 마지막 자리가 다를 수 있어 경계에서 결과가 갈린다.
        const std::int64_t centerX = static_cast<std::int64_t>(chunkX) * CHUNK_SIZE + CHUNK_SIZE / 2;
        const std::int64_t centerY = static_cast<std::int64_t>(chunkY) * CHUNK_SIZE + CHUNK_SIZE / 2;
        const std::int64_t distSq = centerX * centerX + centerY * centerY;

        // 무당벌레 확률(%): 원점 근처 10% -> 64칸 멀어질 때마다 +15%, 최대 70%
        int ladybugPercent = 10;
        for (std::int64_t ring = 64; ring * ring <= distSq && ladybugPercent < 70; ring += 64)
        {
            ladybugPercent += 15;
        }
        if (ladybugPercent > 70) { ladybugPercent = 70; }

        const std::uint32_t count = 1 + Hash(chunkX, chunkY, 50) % 3;

        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::uint32_t h = Hash(chunkX, chunkY, 51 + static_cast<int>(i));
            const int tileX = chunkX * CHUNK_SIZE + 2 + static_cast<int>(h % 12);
            const int tileY = chunkY * CHUNK_SIZE + 2 + static_cast<int>((h >> 8) % 12);

            if (InSafeZone(tileX, tileY) || ObstacleAt(tileX, tileY) != Obstacle::None)
            {
                continue;
            }

            const MonsterKind kind = static_cast<int>((h >> 16) % 100) < ladybugPercent
                ? MonsterKind::Ladybug : MonsterKind::Caterpillar;
            out.push_back({ tileX + 0.5f, tileY + 0.5f, kind });
        }

        if (distSq >= static_cast<std::int64_t>(BOSS_MIN_DISTANCE) * BOSS_MIN_DISTANCE && Hash(chunkX, chunkY, 77) % BOSS_CHANCE == 0)
        {
            const int tileX = chunkX * CHUNK_SIZE + 8;
            const int tileY = chunkY * CHUNK_SIZE + 8;

            if (ObstacleAt(tileX, tileY) == Obstacle::None)
            {
                out.push_back({ tileX + 0.5f, tileY + 0.5f, MonsterKind::CatBoss });
            }
        }
    }
}
