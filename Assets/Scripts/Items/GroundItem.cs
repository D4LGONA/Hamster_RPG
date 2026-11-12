// 2026-11-13: 바닥에 떨어진 아이템 그림. 서버가 SC_ITEM_DROP을 보내면 생기고, SC_ITEM_REMOVE면 사라진다.
// 줍는 판정은 서버가 한다(주인이 가까이 오면 서버가 가방에 넣고 REMOVE를 보낸다). 여기서는 보여주기만.

using UnityEngine;

public class GroundItem : MonoBehaviour
{
    private SpriteRenderer icon;
    private SpriteRenderer shadow;
    private Vector3 basePosition;
    private float clock;
    private float popTime = 0.25f;     // 떨어질 때 톡 튀어 오르는 시간
    private Transform collector;       // 줍는 중이면 이쪽으로 빨려 들어간다
    private float collectTime;

    private static Sprite shadowSprite;

    public static GroundItem Create(ushort itemId, ushort count, Vector2 position)
    {
        var go = new GameObject("GroundItem_" + itemId);
        go.transform.position = new Vector3(position.x, position.y, 0.0f);
        GroundItem item = go.AddComponent<GroundItem>();
        item.Setup(itemId);
        return item;
    }

    private void Setup(ushort itemId)
    {
        basePosition = transform.position;

        // 그림자: 작은 반투명 타원
        var shadowGo = new GameObject("Shadow");
        shadowGo.transform.SetParent(transform, false);
        shadowGo.transform.localPosition = new Vector3(0.0f, -0.22f, 0.0f);
        shadow = shadowGo.AddComponent<SpriteRenderer>();
        shadow.sprite = GetShadowSprite();
        shadow.color = new Color(0.0f, 0.0f, 0.0f, 0.25f);
        shadow.sortingOrder = -50;

        var iconGo = new GameObject("Icon");
        iconGo.transform.SetParent(transform, false);
        icon = iconGo.AddComponent<SpriteRenderer>();
        ItemTable.Def def = ItemTable.Find(itemId);
        icon.sprite = def != null ? def.Icon : null;
        icon.sortingOrder = 5;
    }

    // 주웠다: 플레이어 쪽으로 0.2초 동안 빨려 들어가며 작아지고 사라진다. 주인이 없으면 바로 사라진다.
    public void Collect(Transform target)
    {
        if (target == null)
        {
            Destroy(gameObject);
            return;
        }

        collector = target;
        collectTime = 0.0f;
    }

    private void Update()
    {
        clock += Time.deltaTime;

        if (collector != null)
        {
            collectTime += Time.deltaTime;
            float t = Mathf.Clamp01(collectTime / 0.2f);
            transform.position = Vector3.Lerp(transform.position, collector.position, t);
            transform.localScale = Vector3.one * (1.0f - t);

            if (t >= 1.0f)
            {
                Destroy(gameObject);
            }
            return;
        }

        // 처음엔 포물선으로 톡, 그다음엔 둥실둥실
        float height;
        if (clock < popTime)
        {
            float u = clock / popTime;
            height = 0.6f * 4.0f * u * (1.0f - u);
        }
        else
        {
            height = 0.06f + Mathf.Sin((clock - popTime) * 3.0f) * 0.05f;
        }

        icon.transform.localPosition = new Vector3(0.0f, height, 0.0f);
        shadow.transform.localScale = Vector3.one * (1.0f - height * 0.6f);
        transform.position = basePosition;
    }

    private static Sprite GetShadowSprite()
    {
        if (shadowSprite != null)
        {
            return shadowSprite;
        }

        // 12x6 타원을 코드로 그린다(점 하나 그림 파일을 따로 두지 않으려고)
        const int w = 12, h = 6;
        var tex = new Texture2D(w, h, TextureFormat.RGBA32, false) { filterMode = FilterMode.Point };
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float dx = (x + 0.5f - w / 2.0f) / (w / 2.0f);
                float dy = (y + 0.5f - h / 2.0f) / (h / 2.0f);
                tex.SetPixel(x, y, dx * dx + dy * dy <= 1.0f ? Color.white : Color.clear);
            }
        }
        tex.Apply();
        shadowSprite = Sprite.Create(tex, new Rect(0, 0, w, h), new Vector2(0.5f, 0.5f), 32.0f);
        return shadowSprite;
    }
}
