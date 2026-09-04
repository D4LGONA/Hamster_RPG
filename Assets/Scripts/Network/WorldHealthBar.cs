using UnityEngine;

// 월드에 떠 있는 체력바. 몬스터와 플레이어가 같이 쓴다.
//
// 프리팹에 스프라이트를 직접 넣고 아래 두 칸에 꽂아두면 그걸 쓴다. 그러면 코드는 채워지는
// 폭만 조절하고 위치 · 크기 · 색은 전부 프리팹에서 정한 대로 간다.
// 안 꽂아두면 런타임에 스프라이트 두 장으로 만들어 쓴다.
public class WorldHealthBar : MonoBehaviour
{
    private const float BarWidth = 0.9f;
    private const float BarHeight = 0.12f;

    // 스프라이트 위쪽에서 이만큼 띄운다. 고정 높이로 두면 스프라이트 크기나 피벗이
    // 바뀔 때마다 바가 몸에 겹치거나 붕 뜬다.
    private const float DefaultGapAboveSprite = 0.15f;

    private static Sprite whiteSprite;

    // 프리팹에 직접 넣은 스프라이트를 꽂아두면 그걸 쓴다. 그러면 코드는 채워지는 폭만 조절하고
    // 위치 · 크기 · 색은 전부 프리팹에서 정한 대로 간다.
    // 비워두면 예전처럼 코드가 만든다.
    [SerializeField] private SpriteRenderer backgroundRenderer;
    [SerializeField] private SpriteRenderer fillRenderer;

    private Transform fillTransform;

    // 프리팹에서 정해둔 '가득 찼을 때'의 폭과 위치. 비율은 여기에 곱한다.
    private float fullWidth;
    private float fullLocalX;
    private bool ready;

    // offsetY 가 0보다 크면 그 높이에 그대로 두고, 아니면 스프라이트 위쪽에 맞춰 계산한다.
    private float barOffsetY;

    public void Build(SpriteRenderer ownerRenderer, float offsetY)
    {
        // 프리팹에 이미 만들어 꽂아둔 경우. 코드가 새로 만들지 않는다.
        if (fillRenderer != null)
        {
            CaptureAuthoredBar();
            return;
        }

        barOffsetY = offsetY > 0.0f
            ? offsetY
            : CalculateOffsetAboveSprite(ownerRenderer);

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
        CaptureAuthoredBar();

        backgroundRenderer.transform.localScale = new Vector3(BarWidth, BarHeight, 1.0f);

        SetRatio(1.0f);
    }

    private SpriteRenderer CreatePart(string name, Color color, int sortingLayerId, int sortingOrder)
    {
        GameObject obj = new GameObject(name);
        obj.transform.SetParent(transform, false);
        obj.transform.localPosition = new Vector3(0.0f, barOffsetY, 0.0f);

        SpriteRenderer renderer = obj.AddComponent<SpriteRenderer>();
        renderer.sprite = GetWhiteSprite();
        renderer.color = color;
        renderer.sortingLayerID = sortingLayerId;
        renderer.sortingOrder = sortingOrder;

        return renderer;
    }

    // 프리팹에서 정해둔 값을 '가득 찬 상태'로 기억해 둔다.
    private void CaptureAuthoredBar()
    {
        if (ready || fillRenderer == null)
        {
            return;
        }

        fillTransform = fillRenderer.transform;
        fullWidth = fillTransform.localScale.x;
        fullLocalX = fillTransform.localPosition.x;
        ready = true;
    }

    public void SetRatio(float newRatio)
    {
        CaptureAuthoredBar();

        if (fillTransform == null)
        {
            return;
        }

        float ratio = Mathf.Clamp01(newRatio);
        float width = fullWidth * ratio;

        Vector3 scale = fillTransform.localScale;
        scale.x = width;
        fillTransform.localScale = scale;

        // 스케일은 가운데를 기준으로 줄어드니, 왼쪽에 붙어 줄어들게 위치를 같이 민다.
        Vector3 position = fillTransform.localPosition;
        position.x = fullLocalX - (fullWidth - width) * 0.5f;
        fillTransform.localPosition = position;
    }

    public void Hide()
    {
        if (backgroundRenderer != null) backgroundRenderer.gameObject.SetActive(false);
        if (fillRenderer != null) fillRenderer.gameObject.SetActive(false);
    }

    // 스프라이트 맨 위 + 여유. 바가 몸에 걸치지 않게.
    private float CalculateOffsetAboveSprite(SpriteRenderer ownerRenderer)
    {
        if (ownerRenderer == null)
        {
            return 0.75f;   // 스프라이트를 못 찾으면 예전 기본값
        }

        // bounds 는 월드 기준이라 내 위치를 빼서 로컬 높이로 바꾼다.
        float top = ownerRenderer.bounds.max.y - transform.position.y;

        return top + DefaultGapAboveSprite;
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
