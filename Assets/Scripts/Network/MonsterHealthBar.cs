using UnityEngine;

// 몬스터 머리 위 체력바. 프리팹을 건드리지 않아도 되게 런타임에 스프라이트 두 장으로 만든다.
// (에디터에서 UI를 얹는 것보다, 몬스터가 서버 통보로 생겼다 사라지는 구조와 맞다)
public class MonsterHealthBar : MonoBehaviour
{
    private const float BarWidth = 0.9f;
    private const float BarHeight = 0.12f;
    private const float BarOffsetY = 0.75f;

    private static Sprite whiteSprite;

    private Transform fillTransform;
    private SpriteRenderer backgroundRenderer;
    private SpriteRenderer fillRenderer;

    private float ratio = 1.0f;

    public void Build(SpriteRenderer ownerRenderer)
    {
        // 몬스터 스프라이트보다 확실히 앞에 그린다. 정렬 레이어가 다르면 order만으로는 안 되므로
        // 레이어까지 같이 맞춰준다.
        int sortingLayerId = 0;
        int sortingOrder = 0;

        if (ownerRenderer != null)
        {
            sortingLayerId = ownerRenderer.sortingLayerID;
            sortingOrder = ownerRenderer.sortingOrder;
        }

        backgroundRenderer = CreatePart("HealthBar_BG", new Color(0.0f, 0.0f, 0.0f, 0.6f),
                                        sortingLayerId, sortingOrder + 10);
        fillRenderer = CreatePart("HealthBar_Fill", new Color(0.85f, 0.2f, 0.2f, 1.0f),
                                  sortingLayerId, sortingOrder + 11);

        fillTransform = fillRenderer.transform;

        backgroundRenderer.transform.localScale = new Vector3(BarWidth, BarHeight, 1.0f);

        SetRatio(1.0f);
    }

    private SpriteRenderer CreatePart(string name, Color color, int sortingLayerId, int sortingOrder)
    {
        GameObject obj = new GameObject(name);
        obj.transform.SetParent(transform, false);
        obj.transform.localPosition = new Vector3(0.0f, BarOffsetY, 0.0f);

        SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
        renderer.sprite = GetWhiteSprite();
        renderer.color = color;
        renderer.sortingLayerID = sortingLayerId;
        renderer.sortingOrder = sortingOrder;

        return renderer;
    }

    public void SetRatio(float newRatio)
    {
        ratio = Mathf.Clamp01(newRatio);

        if (fillTransform == null)
        {
            return;
        }

        float width = BarWidth * ratio;

        fillTransform.localScale = new Vector3(width, BarHeight, 1.0f);

        // 스케일은 가운데를 기준으로 줄어드니, 왼쪽에 붙어 줄어들게 위치를 같이 민다.
        fillTransform.localPosition = new Vector3(-(BarWidth - width) * 0.5f, BarOffsetY, 0.0f);
    }

    public void Hide()
    {
        if (backgroundRenderer != null) backgroundRenderer.gameObject.SetActive(false);
        if (fillRenderer != null) fillRenderer.gameObject.SetActive(false);
    }

    // 1x1 흰색 스프라이트. 색과 스케일만 바꿔 쓰므로 이거 하나면 충분하다.
    private static Sprite GetWhiteSprite()
    {
        if (whiteSprite != null)
        {
            return whiteSprite;
        }

        Texture2D texture = new Texture2D(1, 1);
        texture.SetPixel(0, 0, Color.white);
        texture.Apply();

        whiteSprite = Sprite.Create(texture, new Rect(0.0f, 0.0f, 1.0f, 1.0f), new Vector2(0.5f, 0.5f), 1.0f);

        return whiteSprite;
    }
}
