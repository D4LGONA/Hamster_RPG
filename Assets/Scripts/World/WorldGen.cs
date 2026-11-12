// 2026-11-12: 무한 월드 생성 공식. 서버 Server/WorldGen.h 와 한 줄 한 줄 같아야 한다.
// 고치면 둘 다 고치고 Tools/worldgen_check/ 로 결과가 같은지 확인할 것.
// UnityEngine을 쓰지 않는다. 나중에 C# 서버가 이 파일을 그대로 가져다 쓰게 하려는 것.

using System;
using System.Collections.Generic;

public static class WorldGen
{
    public const uint Seed = 20261112u;
    public const int ChunkSize = 16;
    public const int SafeRadiusTiles = 8;
    public const int BossMinDistance = 48;
    public const uint BossChance = 30;

    public enum Obstacle : byte { None = 0, Tree = 1, Bush = 2, Rock = 3 }
    public enum MonsterKind : byte { Caterpillar = 0, Ladybug = 1, CatBoss = 2 }

    public struct ObstacleSpot { public int TileX; public int TileY; public Obstacle Kind; }
    public struct MonsterSpot { public float X; public float Y; public MonsterKind Kind; }

    public static uint Hash(int a, int b, int c)
    {
        unchecked
        {
            uint h = Seed;
            h ^= (uint)a * 0x8DA6B343u;
            h ^= (uint)b * 0xD8163841u;
            h ^= (uint)c * 0xCB1AB31Fu;
            h ^= h >> 16; h *= 0x85EBCA6Bu;
            h ^= h >> 13; h *= 0xC2B2AE35u;
            h ^= h >> 16;
            return h;
        }
    }

    public static int FloorDiv(int value, int divisor)
    {
        int q = value / divisor;
        if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) { --q; }
        return q;
    }

    public static int TileOf(float worldPos) { return (int)Math.Floor(worldPos); }
    public static int ChunkOf(int tile) { return FloorDiv(tile, ChunkSize); }

    public static bool InSafeZone(int tileX, int tileY)
    {
        return Math.Abs(tileX) <= SafeRadiusTiles && Math.Abs(tileY) <= SafeRadiusTiles;
    }

    public static void ObstaclesInChunk(int chunkX, int chunkY, List<ObstacleSpot> output)
    {
        uint count = 2 + Hash(chunkX, chunkY, 0) % 4;

        for (uint i = 0; i < count; ++i)
        {
            uint h = Hash(chunkX, chunkY, 1 + (int)i);
            int tileX = chunkX * ChunkSize + 1 + (int)(h % 14);
            int tileY = chunkY * ChunkSize + 1 + (int)((h >> 8) % 14);

            if (InSafeZone(tileX, tileY))
            {
                continue;
            }

            uint roll = (h >> 16) % 10;
            Obstacle kind = roll < 5 ? Obstacle.Tree : (roll < 8 ? Obstacle.Bush : Obstacle.Rock);
            output.Add(new ObstacleSpot { TileX = tileX, TileY = tileY, Kind = kind });
        }
    }

    [ThreadStatic] private static List<ObstacleSpot> scratch;

    public static Obstacle ObstacleAt(int tileX, int tileY)
    {
        if (scratch == null) { scratch = new List<ObstacleSpot>(8); }
        scratch.Clear();
        ObstaclesInChunk(ChunkOf(tileX), ChunkOf(tileY), scratch);

        foreach (ObstacleSpot s in scratch)
        {
            if (s.TileX == tileX && s.TileY == tileY)
            {
                return s.Kind;
            }
        }

        return Obstacle.None;
    }

    public static bool IsBlocked(float x, float y, float radius)
    {
        return ObstacleAt(TileOf(x - radius), TileOf(y - radius)) != Obstacle.None ||
               ObstacleAt(TileOf(x + radius), TileOf(y - radius)) != Obstacle.None ||
               ObstacleAt(TileOf(x - radius), TileOf(y + radius)) != Obstacle.None ||
               ObstacleAt(TileOf(x + radius), TileOf(y + radius)) != Obstacle.None;
    }

    public static void MonstersInChunk(int chunkX, int chunkY, List<MonsterSpot> output)
    {
        long centerX = (long)chunkX * ChunkSize + ChunkSize / 2;
        long centerY = (long)chunkY * ChunkSize + ChunkSize / 2;
        long distSq = centerX * centerX + centerY * centerY;

        int ladybugPercent = 10;
        for (long ring = 64; ring * ring <= distSq && ladybugPercent < 70; ring += 64)
        {
            ladybugPercent += 15;
        }
        if (ladybugPercent > 70) { ladybugPercent = 70; }

        uint count = 1 + Hash(chunkX, chunkY, 50) % 3;

        for (uint i = 0; i < count; ++i)
        {
            uint h = Hash(chunkX, chunkY, 51 + (int)i);
            int tileX = chunkX * ChunkSize + 2 + (int)(h % 12);
            int tileY = chunkY * ChunkSize + 2 + (int)((h >> 8) % 12);

            if (InSafeZone(tileX, tileY) || ObstacleAt(tileX, tileY) != Obstacle.None)
            {
                continue;
            }

            MonsterKind kind = (int)((h >> 16) % 100) < ladybugPercent ? MonsterKind.Ladybug : MonsterKind.Caterpillar;
            output.Add(new MonsterSpot { X = tileX + 0.5f, Y = tileY + 0.5f, Kind = kind });
        }

        if (distSq >= (long)BossMinDistance * BossMinDistance && Hash(chunkX, chunkY, 77) % BossChance == 0)
        {
            int tileX = chunkX * ChunkSize + 8;
            int tileY = chunkY * ChunkSize + 8;

            if (ObstacleAt(tileX, tileY) == Obstacle.None)
            {
                output.Add(new MonsterSpot { X = tileX + 0.5f, Y = tileY + 0.5f, Kind = MonsterKind.CatBoss });
            }
        }
    }

    // 바닥 무늬. 서버는 안 쓴다(바닥은 막지 않으므로). 0 = 풀1, 1 = 풀2, 2/3 = 꽃 조금
    public static int GroundAt(int tileX, int tileY)
    {
        uint h = Hash(tileX, tileY, 99) % 100;
        if (h < 2) { return 2 + (int)(h & 1); }
        return h < 30 ? 1 : 0;
    }
}
