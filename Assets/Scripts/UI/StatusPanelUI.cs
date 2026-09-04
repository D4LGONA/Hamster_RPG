using TMPro;
using UnityEngine;
using UnityEngine.UI;

// 캐릭터 상태창. Tab 또는 C로 연다.
//
// 값은 전부 서버가 SC_STAT_UPDATE로 보내준 것을 그대로 표시한다. 클라가 계산하는 값은 없다.
// 프리팹 없이 런타임에 만든다(몬스터 체력바 · 데미지 숫자와 같은 방식).
public class StatusPanelUI : MonoBehaviour
{
    // 씬을 옮겨도 열려 있던 상태를 유지한다. 씬마다 새로 만들어지기 때문.
    private static bool openedLastTime;

    private const float PanelWidth = 320.0f;
    private const float PanelHeight = 392.0f;
    private const float RowHeight = 30.0f;
    private const float SidePadding = 18.0f;

    private static Sprite whiteSprite;

    // 에디터 메뉴(Tools > Hamster > 상태창 만들기)로 한 번 구워두면 여기 채워져 씬에 저장된다.
    // 그러면 인스펙터에서 색 · 위치 · 폰트를 직접 만질 수 있다.
    // 비어 있으면 실행할 때 코드가 똑같은 구조를 만들어 쓴다.
    [SerializeField] private GameObject panel;

    [SerializeField] private TextMeshProUGUI nameText;
    [SerializeField] private TextMeshProUGUI levelText;
    [SerializeField] private TextMeshProUGUI expText;
    [SerializeField] private TextMeshProUGUI hpText;
    [SerializeField] private TextMeshProUGUI attackText;
    [SerializeField] private TextMeshProUGUI defenseText;
    [SerializeField] private TextMeshProUGUI moveSpeedText;
    [SerializeField] private TextMeshProUGUI combatPowerText;
    [SerializeField] private TextMeshProUGUI goldText;

    [SerializeField] private RectTransform hpFill;
    [SerializeField] private RectTransform expFill;

    private int lastStatVersion = -1;

    // 찾는 순서: 씬에 이미 있는 것 -> 프리팹 -> 코드로 만들기.
    // 어느 쪽이든 결과는 같은 구조라 아래 표시 코드는 신경 쓸 필요가 없다.
    public static StatusPanelUI Create(GameObject prefab)
    {
        StatusPanelUI existing = FindObjectOfType<StatusPanelUI>();

        if (existing != null)
        {
            return existing;
        }

        if (prefab != null)
        {
            GameObject obj = Instantiate(prefab);
            obj.name = "StatusPanelUI";

            StatusPanelUI fromPrefab = obj.GetComponent<StatusPanelUI>();

            if (fromPrefab != null)
            {
                return fromPrefab;
            }

            Debug.LogWarning("[상태창] 프리팹에 StatusPanelUI 컴포넌트가 없습니다. 코드로 만듭니다.");
            Destroy(obj);
        }

        GameObject root = new GameObject("StatusPanelUI");
        StatusPanelUI ui = root.AddComponent<StatusPanelUI>();
        ui.Build();
        return ui;
    }

    // 에디터 메뉴에서도 이걸 부른다. 만드는 코드가 한 군데만 있어야 둘이 어긋나지 않는다.
    public void Build()
    {
        if (panel != null || gameObject.GetComponent<Canvas>() != null)
        {
            return;   // 이미 만들어져 있다(에디터에서 구워둔 경우)
        }

        Canvas canvas = gameObject.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 100;   // 채팅창보다 위

        CanvasScaler scaler = gameObject.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(800.0f, 800.0f);   // 채팅 UI와 같은 기준
        scaler.matchWidthOrHeight = 0.5f;

        panel = CreatePanel();
        panel.SetActive(openedLastTime);
    }

    private void Awake()
    {
        // 에디터에서 구워둔 경우엔 Build()가 안 불리므로 여기서 초기 표시 상태만 맞춘다.
        if (panel != null)
        {
            panel.SetActive(openedLastTime);
        }
    }

    private GameObject CreatePanel()
    {
        GameObject panelObj = CreateUIObject("Panel", transform);

        RectTransform rect = panelObj.GetComponent<RectTransform>();
        // 오른쪽 위. 왼쪽 아래는 채팅창이 쓴다.
        rect.anchorMin = new Vector2(1.0f, 1.0f);
        rect.anchorMax = new Vector2(1.0f, 1.0f);
        rect.pivot = new Vector2(1.0f, 1.0f);
        rect.anchoredPosition = new Vector2(-16.0f, -16.0f);
        rect.sizeDelta = new Vector2(PanelWidth, PanelHeight);

        Image background = panelObj.AddComponent<Image>();
        background.sprite = GetWhiteSprite();
        background.color = new Color(0.07f, 0.08f, 0.11f, 0.92f);

        float y = -14.0f;

        CreateTitle(panelObj.transform, ref y);
        nameText = CreateRow(panelObj.transform, "이름", ref y);
        levelText = CreateRow(panelObj.transform, "레벨", ref y);

        expText = CreateRow(panelObj.transform, "경험치", ref y);
        expFill = CreateBar(panelObj.transform, ref y, new Color(0.45f, 0.75f, 1.0f));

        hpText = CreateRow(panelObj.transform, "체력", ref y);
        hpFill = CreateBar(panelObj.transform, ref y, new Color(0.9f, 0.35f, 0.35f));

        attackText = CreateRow(panelObj.transform, "공격력", ref y);
        defenseText = CreateRow(panelObj.transform, "방어력", ref y);
        moveSpeedText = CreateRow(panelObj.transform, "이동속도", ref y);
        combatPowerText = CreateRow(panelObj.transform, "전투력", ref y);
        goldText = CreateRow(panelObj.transform, "골드", ref y);

        CreateHint(panelObj.transform);

        return panelObj;
    }

    private void CreateTitle(Transform parent, ref float y)
    {
        TextMeshProUGUI title = CreateText(parent, "Title", 20.0f, TextAlignmentOptions.Center);
        title.color = new Color(1.0f, 0.93f, 0.72f);
        title.text = "캐릭터 정보";

        RectTransform rect = title.rectTransform;
        rect.anchoredPosition = new Vector2(0.0f, y);
        rect.sizeDelta = new Vector2(PanelWidth - SidePadding * 2.0f, RowHeight);

        y -= RowHeight + 6.0f;
    }

    // 왼쪽에 항목 이름, 오른쪽에 값. 값 쪽 텍스트를 돌려준다.
    private TextMeshProUGUI CreateRow(Transform parent, string label, ref float y)
    {
        TextMeshProUGUI labelText = CreateText(parent, label + "_label", 16.0f, TextAlignmentOptions.Left);
        labelText.color = new Color(0.62f, 0.66f, 0.76f);
        labelText.text = label;
        labelText.rectTransform.anchoredPosition = new Vector2(SidePadding, y);
        labelText.rectTransform.sizeDelta = new Vector2(120.0f, RowHeight);

        TextMeshProUGUI valueText = CreateText(parent, label + "_value", 16.0f, TextAlignmentOptions.Right);
        valueText.color = Color.white;
        valueText.text = "-";
        valueText.rectTransform.anchoredPosition = new Vector2(-SidePadding, y);
        valueText.rectTransform.anchorMin = new Vector2(1.0f, 1.0f);
        valueText.rectTransform.anchorMax = new Vector2(1.0f, 1.0f);
        valueText.rectTransform.pivot = new Vector2(1.0f, 1.0f);
        valueText.rectTransform.sizeDelta = new Vector2(170.0f, RowHeight);

        y -= RowHeight;

        return valueText;
    }

    // 가로 막대. 채워지는 쪽 RectTransform을 돌려준다(폭을 비율로 조절해 쓴다).
    private RectTransform CreateBar(Transform parent, ref float y, Color fillColor)
    {
        float width = PanelWidth - SidePadding * 2.0f;

        GameObject barObj = CreateUIObject("Bar", parent);
        RectTransform barRect = barObj.GetComponent<RectTransform>();
        barRect.anchoredPosition = new Vector2(SidePadding, y + 8.0f);
        barRect.sizeDelta = new Vector2(width, 8.0f);

        Image barBackground = barObj.AddComponent<Image>();
        barBackground.sprite = GetWhiteSprite();
        barBackground.color = new Color(0.0f, 0.0f, 0.0f, 0.5f);

        GameObject fillObj = CreateUIObject("Fill", barObj.transform);
        RectTransform fillRect = fillObj.GetComponent<RectTransform>();
        fillRect.anchoredPosition = Vector2.zero;
        fillRect.sizeDelta = new Vector2(width, 8.0f);

        Image fill = fillObj.AddComponent<Image>();
        fill.sprite = GetWhiteSprite();
        fill.color = fillColor;

        y -= 16.0f;

        return fillRect;
    }

    private void CreateHint(Transform parent)
    {
        TextMeshProUGUI hint = CreateText(parent, "Hint", 13.0f, TextAlignmentOptions.Center);
        hint.color = new Color(0.5f, 0.54f, 0.62f);
        hint.text = "Tab / C : 닫기";

        RectTransform rect = hint.rectTransform;
        rect.anchorMin = new Vector2(0.5f, 0.0f);
        rect.anchorMax = new Vector2(0.5f, 0.0f);
        rect.pivot = new Vector2(0.5f, 0.0f);
        rect.anchoredPosition = new Vector2(0.0f, 8.0f);
        rect.sizeDelta = new Vector2(PanelWidth - SidePadding * 2.0f, 20.0f);
    }

    private void Update()
    {
        if (NetworkClient.Instance == null || NetworkClient.Instance.IsLoggedIn() == false)
        {
            if (panel.activeSelf)
            {
                panel.SetActive(false);
            }

            return;
        }

        // 채팅 중엔 Tab / C 가 글자 입력이다.
        bool chatting = ChatUI.Instance != null && ChatUI.Instance.IsChatting();

        if (chatting == false && (Input.GetKeyDown(KeyCode.Tab) || Input.GetKeyDown(KeyCode.C)))
        {
            openedLastTime = !panel.activeSelf;
            panel.SetActive(openedLastTime);
        }

        if (panel.activeSelf == false)
        {
            return;
        }

        // 값이 바뀐 틱에만 문자열을 새로 만든다. 매 프레임 만들면 쓸데없이 쓰레기가 쌓인다.
        int version = NetworkClient.Instance.GetStatVersion();

        if (version != lastStatVersion)
        {
            lastStatVersion = version;
            Refresh();
        }
    }

    private void Refresh()
    {
        nameText.text = NetworkClient.Instance.GetPlayerName();

        if (NetworkClient.Instance.HasMyStats() == false)
        {
            return;   // 아직 서버에서 스탯이 안 왔다
        }

        Protocol.PlayerStatsData stats = NetworkClient.Instance.GetMyStats();

        levelText.text = stats.level.ToString();
        expText.text = stats.exp + " / " + stats.requiredExp;
        hpText.text = stats.hp + " / " + stats.maxHp;
        attackText.text = stats.attack.ToString();
        defenseText.text = stats.defense.ToString();
        moveSpeedText.text = stats.moveSpeed.ToString("0.0");
        combatPowerText.text = stats.combatPower.ToString();
        goldText.text = stats.gold.ToString();

        SetBar(expFill, stats.requiredExp > 0 ? (float)stats.exp / stats.requiredExp : 0.0f);
        SetBar(hpFill, stats.maxHp > 0 ? (float)stats.hp / stats.maxHp : 0.0f);
    }

    private void SetBar(RectTransform fill, float ratio)
    {
        float full = PanelWidth - SidePadding * 2.0f;
        fill.sizeDelta = new Vector2(full * Mathf.Clamp01(ratio), fill.sizeDelta.y);
    }

    // --- 만들기 도우미 ---

    private static GameObject CreateUIObject(string name, Transform parent)
    {
        GameObject obj = new GameObject(name, typeof(RectTransform));
        obj.transform.SetParent(parent, false);

        RectTransform rect = obj.GetComponent<RectTransform>();
        // 왼쪽 위를 기준점으로 두면 y를 음수로 내려가며 줄을 쌓기 편하다.
        rect.anchorMin = new Vector2(0.0f, 1.0f);
        rect.anchorMax = new Vector2(0.0f, 1.0f);
        rect.pivot = new Vector2(0.0f, 1.0f);

        return obj;
    }

    private static TextMeshProUGUI CreateText(Transform parent, string name, float fontSize,
                                              TextAlignmentOptions alignment)
    {
        GameObject obj = CreateUIObject(name, parent);

        TextMeshProUGUI text = obj.AddComponent<TextMeshProUGUI>();
        text.fontSize = fontSize;
        text.alignment = alignment;
        text.enableWordWrapping = false;
        text.raycastTarget = false;

        return text;
    }

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
