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
        renderer.sprite = GetSquareSprite();
        renderer.color = new Color(0.38f, 0.30f, 0.24f);
        renderer.sortingOrder = -20;   // 캐릭터보다 뒤
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
