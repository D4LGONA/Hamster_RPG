using UnityEngine;

// 집 안(HouseScene)의 바닥. 씬에는 카메라만 있고 나머지는 여기서 그린다.
//
// 벽은 그리지 않는다. 서버는 플레이어의 '중심 좌표'를 막는데 스프라이트는 그보다
// 반 칸 더 나오므로, 벽을 그려두면 몸이 벽을 뚫고 나온 것처럼 보인다.
// 화면 전체를 바닥으로 깔고 이동 한계를 화면 끝에 맞추는 편이 어긋날 일이 없다.
public class HouseRoom : MonoBehaviour
{
    // 화면보다 넉넉히 크게. 어떤 화면 비율에서도 바닥 밖이 보이지 않게 한다.
    private const float FloorSize = 40.0f;

    private static Sprite squareSprite;

    public static void Build()
    {
        GameObject root = new GameObject("HouseRoom");
        root.AddComponent<HouseRoom>().Construct();
    }

    private void Construct()
    {
        GameObject floor = new GameObject("Floor");
        floor.transform.SetParent(transform, false);
        floor.transform.localPosition = Vector3.zero;
        floor.transform.localScale = new Vector3(FloorSize, FloorSize, 1.0f);

        SpriteRenderer renderer = floor.AddComponent<SpriteRenderer>();
        renderer.sortingOrder = -20;   // 캐릭터보다 뒤

        // 2026-11-13: 나무 마루 타일을 깐다. 그림이 없으면 예전처럼 단색.
        Sprite wood = Resources.Load<Sprite>("Furniture/floor_wood");
        if (wood != null)
        {
            floor.transform.localScale = Vector3.one;
            renderer.sprite = wood;
            renderer.drawMode = SpriteDrawMode.Tiled;
            renderer.size = new Vector2(FloorSize, FloorSize);
        }
        else
        {
            renderer.sprite = GetSquareSprite();
            renderer.color = new Color(0.38f, 0.30f, 0.24f);
        }

        // 서버가 막는 방 테두리(±4.5)를 옅은 선으로 보여준다. 가구를 어디까지 놓을 수 있는지 알 수 있게.
        BuildBorder();

        // 가구: 서버가 보낸 목록을 그리는 관리자 + 꾸미기 모드(B)
        gameObject.AddComponent<HouseFurnitureView>();
        gameObject.AddComponent<HouseBuildMode>();
    }

    private void BuildBorder()
    {
        const float half = 4.5f;   // protocol.h HOUSE_INTERIOR_HALF_X/Y
        const float thickness = 0.08f;
        Vector2[] centers = { new Vector2(0, half), new Vector2(0, -half), new Vector2(half, 0), new Vector2(-half, 0) };
        Vector2[] sizes = { new Vector2(half * 2, thickness), new Vector2(half * 2, thickness), new Vector2(thickness, half * 2), new Vector2(thickness, half * 2) };

        for (int i = 0; i < 4; ++i)
        {
            var line = new GameObject("Border");
            line.transform.SetParent(transform, false);
            line.transform.localPosition = centers[i];
            line.transform.localScale = new Vector3(sizes[i].x, sizes[i].y, 1.0f);
            SpriteRenderer r = line.AddComponent<SpriteRenderer>();
            r.sprite = GetSquareSprite();
            r.color = new Color(1.0f, 0.95f, 0.85f, 0.35f);
            r.sortingOrder = -19;
        }
    }

    private static Sprite GetSquareSprite()
    {
        if (squareSprite != null)
        {
            return squareSprite;
        }

        Texture2D texture = new Texture2D(1, 1);
        texture.SetPixel(0, 0, Color.white);
        texture.Apply();

        // pixelsPerUnit을 1로 두면 스케일 값이 곧 월드 크기가 된다.
        squareSprite = Sprite.Create(texture, new Rect(0.0f, 0.0f, 1.0f, 1.0f), new Vector2(0.5f, 0.5f), 1.0f);

        return squareSprite;
    }
}
