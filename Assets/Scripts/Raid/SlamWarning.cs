// 2026-11-16: 왕 고양이 내려찍기 예고. 서버가 SC_RAID_SLAM을 보내면 바닥에 빨간 원이 생기고,
// delay 동안 안쪽부터 차오르다가 꽝(한 번 번쩍) 하고 사라진다. 그 전에 원 밖으로 나가면 안 맞는다.
// 피해 판정은 서버가 한다. 여기서는 보여주기만.

using UnityEngine;

public class SlamWarning : MonoBehaviour
{
    private SpriteRenderer outline;
    private SpriteRenderer fill;
    private float delay;
    private float clock;
    private float radius;

    private static Sprite circleSprite;

    public static void Create(Vector2 position, float radius, float delay)
    {
        var go = new GameObject("SlamWarning");
        go.transform.position = new Vector3(position.x, position.y, 0.0f);
        go.AddComponent<SlamWarning>().Setup(radius, delay);
    }

    private void Setup(float newRadius, float newDelay)
    {
        radius = newRadius;
        delay = Mathf.Max(0.1f, newDelay);

        outline = MakeCircle("Outline", -16);
        outline.transform.localScale = Vector3.one * radius * 2.0f;
        outline.color = new Color(1.0f, 0.25f, 0.25f, 0.25f);

        fill = MakeCircle("Fill", -15);
        fill.transform.localScale = Vector3.zero;
        fill.color = new Color(1.0f, 0.2f, 0.2f, 0.35f);
    }

    private SpriteRenderer MakeCircle(string name, int order)
    {
        var go = new GameObject(name);
        go.transform.SetParent(transform, false);
        SpriteRenderer r = go.AddComponent<SpriteRenderer>();
        r.sprite = GetCircleSprite();
        r.sortingOrder = order;   // 바닥(-100)보다 위, 캐릭터(0)보다 아래
        return r;
    }

    private void Update()
    {
        clock += Time.deltaTime;

        if (clock < delay)
        {
            float t = clock / delay;
            fill.transform.localScale = Vector3.one * radius * 2.0f * t;
            return;
        }

        // 꽝: 0.25초 동안 하얗게 번쩍하며 사라진다
        float after = (clock - delay) / 0.25f;
        fill.transform.localScale = Vector3.one * radius * 2.0f;
        fill.color = new Color(1.0f, 0.9f, 0.8f, 0.6f * (1.0f - after));
        outline.color = new Color(1.0f, 0.25f, 0.25f, 0.25f * (1.0f - after));

        if (after >= 1.0f)
        {
            Destroy(gameObject);
        }
    }

    // 지름 1유닛짜리 원(64px). 코드로 한 번 만들어 같이 쓴다.
    private static Sprite GetCircleSprite()
    {
        if (circleSprite != null)
        {
            return circleSprite;
        }

        const int size = 64;
        var tex = new Texture2D(size, size, TextureFormat.RGBA32, false) { filterMode = FilterMode.Bilinear };
        float r = size * 0.5f;

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float dx = x + 0.5f - r;
                float dy = y + 0.5f - r;
                float d = Mathf.Sqrt(dx * dx + dy * dy);
                float a = d <= r - 1.0f ? 1.0f : (d <= r ? r - d : 0.0f);   // 가장자리만 살짝 부드럽게
                tex.SetPixel(x, y, new Color(1, 1, 1, a));
            }
        }

        tex.Apply();
        circleSprite = Sprite.Create(tex, new Rect(0, 0, size, size), new Vector2(0.5f, 0.5f), size);
        return circleSprite;
    }
}
