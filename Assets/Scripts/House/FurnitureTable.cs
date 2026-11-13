// 2026-11-13: 가구 종류 표(클라). 크기는 서버 Server/Furniture.h FURNITURE_SIZE와 같아야 한다.
// 클라는 미리보기(놓을 수 있나 초록/빨강)에만 크기를 쓰고, 진짜 판정은 서버가 한다.

using UnityEngine;

public static class FurnitureTable
{
    public const int Count = 5;
    public const int Rug = 4;
    public const float RoomHalf = 4.5f;      // protocol.h HOUSE_INTERIOR_HALF_X/Y
    public const float EntryY = -3.0f;       // protocol.h HOUSE_ENTRY_Y
    public const float EntryClear = 1.0f;    // GameServer.h FURNITURE_ENTRY_CLEAR

    public static readonly string[] Names = { "침대", "책상", "의자", "화분", "러그" };
    private static readonly Vector2[] Sizes =
    {
        new Vector2(2.0f, 1.2f),
        new Vector2(1.6f, 0.8f),
        new Vector2(0.7f, 0.7f),
        new Vector2(0.6f, 0.6f),
        new Vector2(2.0f, 1.5f),
    };
    private static readonly string[] SpritePaths =
    {
        "Furniture/furniture_bed", "Furniture/furniture_desk", "Furniture/furniture_chair", "Furniture/furniture_pot", "Furniture/furniture_rug",
    };
    private static readonly Sprite[] sprites = new Sprite[Count];

    public static Sprite GetSprite(int type)
    {
        if (type < 0 || type >= Count) return null;
        if (sprites[type] == null) sprites[type] = Resources.Load<Sprite>(SpritePaths[type]);
        return sprites[type];
    }

    // 회전(0~3)을 반영한 반쪽 크기. 90/270도면 가로세로가 바뀐다.
    public static Vector2 HalfExtents(int type, int rotation)
    {
        Vector2 size = Sizes[Mathf.Clamp(type, 0, Count - 1)];
        return (rotation & 1) == 1 ? new Vector2(size.y, size.x) * 0.5f : size * 0.5f;
    }

    // 서버 CheckFurnitureFits와 같은 규칙(좌표 → 출입구 → 겹침). 미리보기 색에만 쓴다.
    public static bool Fits(int type, int rotation, Vector2 pos, uint ignoreId,
                            System.Collections.Generic.IReadOnlyDictionary<uint, NetworkClient.FurnitureData> others)
    {
        Vector2 half = HalfExtents(type, rotation);

        if (Mathf.Abs(pos.x) + half.x > RoomHalf || Mathf.Abs(pos.y) + half.y > RoomHalf)
        {
            return false;
        }

        if (type != Rug && Mathf.Abs(pos.x) < half.x + EntryClear && Mathf.Abs(pos.y - EntryY) < half.y + EntryClear)
        {
            return false;
        }

        if (type == Rug || others == null)
        {
            return true;
        }

        foreach (var pair in others)
        {
            if (pair.Key == ignoreId || pair.Value.itemType == Rug) continue;

            Vector2 otherHalf = HalfExtents(pair.Value.itemType, pair.Value.rotation);
            if (Mathf.Abs(pos.x - pair.Value.x) < half.x + otherHalf.x && Mathf.Abs(pos.y - pair.Value.y) < half.y + otherHalf.y)
            {
                return false;
            }
        }

        return true;
    }
}
