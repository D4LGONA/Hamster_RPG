// 2026-11-12: 게임 화면 HUD. 씬에 배치하지 않아도 로그인하면 스스로 만들어진다.
//
//   왼쪽 위   : 초상화 + 이름/레벨 + HP 막대 + 경험치 막대, 그 아래 해바라기씨(골드)
//   오른쪽 위 : 미니맵 (주변 64x64유닛. 장애물/집/몬스터/다른 플레이어) + 좌표
//   가운데 위 : 레벨업 알림
//   왼쪽      : 경험치/골드 획득 알림 (떠오르며 사라짐)
//   가운데    : 상태창 (Tab 또는 C)
//
// 값은 전부 서버가 보내준 것(NetworkClient)이다. HUD가 계산하는 게임 값은 없다.
// 미니맵의 장애물은 WorldGen 공식으로 그린다(서버와 같은 공식이라 실제로 막히는 자리와 같다).

using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public class GameHUD : MonoBehaviour
{
    // ---- 색 ----
    private static readonly Color Brown = new Color32(92, 62, 44, 255);
    private static readonly Color BrownSoft = new Color32(150, 112, 84, 255);
    private static readonly Color HpColor = new Color32(240, 96, 112, 255);
    private static readonly Color HpLowColor = new Color32(230, 60, 60, 255);
    private static readonly Color ExpColor = new Color32(255, 200, 64, 255);
    private static readonly Color Gold = new Color32(255, 214, 90, 255);

    // ---- 미니맵 ----
    private const int MapPixels = 128;          // 텍스처 한 변
    private const float MapUnits = 64.0f;       // 텍스처 한 변이 덮는 월드 크기
    private const float PixelsPerUnit = MapPixels / MapUnits;
    private const float MapRefreshSec = 0.2f;
    private static readonly Color32 MapGround = new Color32(126, 196, 96, 255);
    private static readonly Color32 MapTree = new Color32(52, 120, 64, 255);
    private static readonly Color32 MapBush = new Color32(84, 156, 72, 255);
    private static readonly Color32 MapRock = new Color32(150, 150, 160, 255);
    private static readonly Color32 MapHouse = new Color32(170, 110, 70, 255);
    private static readonly Color32 MapCaterpillar = new Color32(200, 255, 120, 255);
    private static readonly Color32 MapLadybug = new Color32(240, 70, 80, 255);
    private static readonly Color32 MapBoss = new Color32(255, 200, 40, 255);
    private static readonly Color32 MapOther = new Color32(110, 170, 255, 255);
    private static readonly Color32 MapMe = new Color32(255, 150, 60, 255);
    private static readonly Color32 MapEdge = new Color32(30, 24, 20, 255);

    // 9-slice 테두리 굵기. 스프라이트가 32 PPU라 그대로면 원본 1픽셀이 약 3배로 커진다.
    // 1.5625를 곱하면 정확히 2배(1px -> 2px)가 되어 작은 800x800 화면에서도 테두리가 두껍지 않다.
    public const float PixelScaleMultiplier = 1.5625f;

    private static GameHUD instance;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    private static void Create()
    {
        if (instance != null)
        {
            return;
        }

        var go = new GameObject("GameHUD");
        DontDestroyOnLoad(go);   // 월드 <-> 집 씬을 오가도 그대로
        instance = go.AddComponent<GameHUD>();
    }

    // ---- 만든 UI 조각들 ----
    private bool built;
    private TMP_FontAsset font;
    private Sprite panelSprite, panelDarkSprite, barBgSprite, barFillSprite, heartSprite, starSprite, seedSprite, portraitSprite;

    private GameObject root;
    private TextMeshProUGUI nameText, levelText, hpText, expText, goldText;
    private RectTransform hpFill, expFill;
    private Image hpFillImage;

    private GameObject minimapPanel;
    private RawImage minimapImage;
    private Texture2D minimapTexture;
    private Color32[] minimapPixels;
    private TextMeshProUGUI coordText, minimapLabel;
    private float minimapTimer;

    private RectTransform toastRoot;
    private TextMeshProUGUI levelUpText;
    private float levelUpTimer;

    private RectTransform gainRoot;
    private class Gain { public TextMeshProUGUI text; public float age; }
    private readonly List<Gain> gains = new List<Gain>();

    private GameObject statusWindow;
    private readonly Dictionary<string, TextMeshProUGUI> statusValues = new Dictionary<string, TextMeshProUGUI>();
    private TextMeshProUGUI statusTitle;

    private int lastStatVersion = -1;
    private bool hasPrev;
    private int prevLevel, prevExp, prevGold;

    private readonly List<WorldGen.ObstacleSpot> obstacleBuffer = new List<WorldGen.ObstacleSpot>();

    private void Awake()
    {
        SceneManager.sceneLoaded += (scene, mode) => HideEmptySceneStatWindow();
    }

    private void Update()
    {
        NetworkClient net = NetworkClient.Instance;
        bool show = net != null && net.IsLoggedIn();

        if (show && built == false)
        {
            Build();
        }

        if (built == false)
        {
            return;
        }

        root.SetActive(show);

        if (show == false)
        {
            return;
        }

        UpdateStats(net);
        UpdateStatusWindowToggle();
        UpdateMinimap(net);
        UpdateToasts();
    }

    // =====================================================================
    // 만들기
    // =====================================================================

    private void Build()
    {
        built = true;

        font = FindKoreanFont();
        panelSprite = Resources.Load<Sprite>("UI/ui_panel");
        panelDarkSprite = Resources.Load<Sprite>("UI/ui_panel_dark");
        barBgSprite = Resources.Load<Sprite>("UI/ui_bar_bg");
        barFillSprite = Resources.Load<Sprite>("UI/ui_bar_fill");
        heartSprite = Resources.Load<Sprite>("UI/icon_heart");
        starSprite = Resources.Load<Sprite>("UI/icon_star");
        seedSprite = Resources.Load<Sprite>("UI/icon_seed");
        portraitSprite = Resources.Load<Sprite>("UI/portrait_hamster");

        // 캔버스: 게임 창이 800x800(1:1)이라 그 크기를 기준으로 잡는다. 창 크기가 바뀌면 비율대로 커지고 작아진다.
        root = new GameObject("HUDCanvas", typeof(RectTransform));
        root.transform.SetParent(transform, false);
        var canvas = root.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 50;
        var scaler = root.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(800, 800);
        scaler.matchWidthOrHeight = 0.5f;
        root.AddComponent<GraphicRaycaster>();
        RectTransform canvasRect = root.GetComponent<RectTransform>();

        BuildProfile(canvasRect);
        BuildMinimap(canvasRect);
        BuildToasts(canvasRect);
        BuildStatusWindow(canvasRect);

        HideEmptySceneStatWindow();
    }

    private void BuildProfile(RectTransform canvas)
    {
        // 큰 패널
        // 800x800 화면 기준: 왼쪽 위 260x86. 오른쪽 위 미니맵(156)과 사이에 넉넉히 빈 곳이 남는다.
        RectTransform panel = Panel(canvas, "Profile", panelSprite, new Vector2(0, 1), new Vector2(10, -10), new Vector2(260, 86));

        RectTransform frame = Panel(panel, "PortraitFrame", panelDarkSprite, new Vector2(0, 1), new Vector2(9, -9), new Vector2(68, 68));
        Image portrait = Img(frame, "Portrait", portraitSprite, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(56, 56));
        portrait.preserveAspect = true;

        nameText = Text(panel, "Name", "", 17, Brown, new Vector2(0, 1), new Vector2(86, -9), new Vector2(120, 22), TextAlignmentOptions.Left);
        nameText.fontStyle = FontStyles.Bold;
        levelText = Text(panel, "Level", "", 14, BrownSoft, new Vector2(0, 1), new Vector2(194, -11), new Vector2(56, 20), TextAlignmentOptions.Right);
        levelText.fontStyle = FontStyles.Bold;

        Img(panel, "HeartIcon", heartSprite, new Vector2(0, 1), new Vector2(86, -37), new Vector2(16, 16));
        hpFill = Bar(panel, "HpBar", new Vector2(106, -37), new Vector2(144, 16), HpColor, out hpText, out hpFillImage);

        Img(panel, "StarIcon", starSprite, new Vector2(0, 1), new Vector2(86, -59), new Vector2(16, 16));
        expFill = Bar(panel, "ExpBar", new Vector2(106, -59), new Vector2(144, 16), ExpColor, out expText, out _);

        // 골드: 패널 아래 작은 알약 모양
        RectTransform goldPanel = Panel(canvas, "Gold", panelSprite, new Vector2(0, 1), new Vector2(10, -100), new Vector2(124, 32));
        Img(goldPanel, "SeedIcon", seedSprite, new Vector2(0, 0.5f), new Vector2(10, 0), new Vector2(14, 17));
        goldText = Text(goldPanel, "GoldValue", "0", 15, Brown, new Vector2(0, 0.5f), new Vector2(30, 0), new Vector2(90, 24), TextAlignmentOptions.Left);
        goldText.fontStyle = FontStyles.Bold;
    }

    private void BuildMinimap(RectTransform canvas)
    {
        RectTransform panel = Panel(canvas, "Minimap", panelSprite, new Vector2(1, 1), new Vector2(-10, -10), new Vector2(156, 176));
        minimapPanel = panel.gameObject;

        RectTransform frame = Panel(panel, "MapFrame", panelDarkSprite, new Vector2(0.5f, 1), new Vector2(0, -8), new Vector2(142, 142));

        minimapTexture = new Texture2D(MapPixels, MapPixels, TextureFormat.RGBA32, false);
        minimapTexture.filterMode = FilterMode.Point;
        minimapTexture.wrapMode = TextureWrapMode.Clamp;
        minimapPixels = new Color32[MapPixels * MapPixels];

        var imgGo = new GameObject("Map", typeof(RectTransform));
        imgGo.transform.SetParent(frame, false);
        minimapImage = imgGo.AddComponent<RawImage>();
        minimapImage.texture = minimapTexture;
        minimapImage.raycastTarget = false;
        Place(minimapImage.rectTransform, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(128, 128));   // 텍스처 128px = 1:1

        minimapLabel = Text(frame, "InHouse", "집 안", 18, new Color(1, 1, 1, 0.9f), new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(128, 40), TextAlignmentOptions.Center);
        minimapLabel.gameObject.SetActive(false);

        coordText = Text(panel, "Coord", "", 12, BrownSoft, new Vector2(0.5f, 0), new Vector2(0, 6), new Vector2(140, 18), TextAlignmentOptions.Center);
    }

    private void BuildToasts(RectTransform canvas)
    {
        toastRoot = new GameObject("LevelUp", typeof(RectTransform)).GetComponent<RectTransform>();
        toastRoot.SetParent(canvas, false);
        Place(toastRoot, new Vector2(0.5f, 1), new Vector2(0, -200), new Vector2(440, 70));   // 패널들 아래, 캐릭터 위

        levelUpText = Text(toastRoot, "Text", "", 38, Gold, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(440, 70), TextAlignmentOptions.Center);
        levelUpText.fontStyle = FontStyles.Bold;
        levelUpText.outlineWidth = 0.25f;
        levelUpText.outlineColor = new Color32(90, 50, 20, 255);
        toastRoot.gameObject.SetActive(false);

        gainRoot = new GameObject("Gains", typeof(RectTransform)).GetComponent<RectTransform>();
        gainRoot.SetParent(canvas, false);
        Place(gainRoot, new Vector2(0, 1), new Vector2(142, -100), new Vector2(200, 32));   // 골드 알약 오른쪽
    }

    private void BuildStatusWindow(RectTransform canvas)
    {
        RectTransform panel = Panel(canvas, "StatusWindow", panelSprite, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(340, 420));
        statusWindow = panel.gameObject;
        panel.GetComponent<Image>().raycastTarget = true;   // 창 위를 클릭해도 공격이 나가지 않게(PlayerAttackInput이 UI 위 클릭은 무시)

        statusTitle = Text(panel, "Title", "상태", 22, Brown, new Vector2(0.5f, 1), new Vector2(0, -16), new Vector2(300, 30), TextAlignmentOptions.Center);
        statusTitle.fontStyle = FontStyles.Bold;

        // 닫기 버튼
        RectTransform close = Panel(panel, "Close", panelDarkSprite, new Vector2(1, 1), new Vector2(-12, -12), new Vector2(34, 34));
        Text(close, "X", "X", 18, Color.white, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(34, 34), TextAlignmentOptions.Center).fontStyle = FontStyles.Bold;
        close.GetComponent<Image>().raycastTarget = true;
        close.gameObject.AddComponent<Button>().onClick.AddListener(() => statusWindow.SetActive(false));

        Img(panel, "Portrait", portraitSprite, new Vector2(0.5f, 1), new Vector2(0, -54), new Vector2(72, 72)).preserveAspect = true;

        string[] rows = { "레벨", "경험치", "HP", "공격력", "방어력", "이동 속도", "전투력", "해바라기씨" };
        float y = -140;

        foreach (string label in rows)
        {
            Text(panel, label, label, 17, BrownSoft, new Vector2(0, 1), new Vector2(36, y), new Vector2(130, 26), TextAlignmentOptions.Left);
            TextMeshProUGUI value = Text(panel, label + "Value", "-", 17, Brown, new Vector2(0, 1), new Vector2(160, y), new Vector2(150, 26), TextAlignmentOptions.Right);
            value.fontStyle = FontStyles.Bold;
            statusValues[label] = value;
            y -= 30;
        }

        Text(panel, "Hint", "Tab / C 로 닫기", 13, BrownSoft, new Vector2(0.5f, 0), new Vector2(0, 12), new Vector2(300, 20), TextAlignmentOptions.Center);
        statusWindow.SetActive(false);
    }

    // =====================================================================
    // 갱신
    // =====================================================================

    private void UpdateStats(NetworkClient net)
    {
        int version = net.GetStatVersion();

        if (version == lastStatVersion || net.HasMyStats() == false)
        {
            return;   // 바뀐 때에만 다시 그린다
        }

        lastStatVersion = version;
        Protocol.PlayerStatsData s = net.GetMyStats();
        string playerName = net.GetPlayerName();

        nameText.text = playerName;
        levelText.text = "Lv." + s.level;

        float hpRatio = s.maxHp > 0 ? (float)s.hp / s.maxHp : 0.0f;
        SetBar(hpFill, hpRatio);
        hpFillImage.color = hpRatio < 0.3f ? HpLowColor : HpColor;
        hpText.text = s.hp + " / " + s.maxHp;

        SetBar(expFill, s.requiredExp > 0 ? (float)s.exp / s.requiredExp : 0.0f);
        expText.text = s.exp + " / " + s.requiredExp;

        goldText.text = s.gold.ToString("N0");

        statusTitle.text = playerName + "의 상태";
        statusValues["레벨"].text = s.level.ToString();
        statusValues["경험치"].text = s.exp + " / " + s.requiredExp;
        statusValues["HP"].text = s.hp + " / " + s.maxHp;
        statusValues["공격력"].text = s.attack.ToString();
        statusValues["방어력"].text = s.defense.ToString();
        statusValues["이동 속도"].text = s.moveSpeed.ToString("0.0");
        statusValues["전투력"].text = s.combatPower.ToString("N0");
        statusValues["해바라기씨"].text = s.gold.ToString("N0");

        // 무엇이 얼마나 늘었나 (처음 받은 스탯은 비교할 게 없어서 건너뛴다)
        if (hasPrev)
        {
            if (s.level > prevLevel)
            {
                ShowLevelUp(s.level);
            }
            else if (s.exp > prevExp)
            {
                AddGain("+" + (s.exp - prevExp) + " 경험치", ExpColor);
            }

            if (s.gold > prevGold)
            {
                AddGain("+" + (s.gold - prevGold) + " 해바라기씨", new Color32(250, 240, 220, 255));
            }
        }

        hasPrev = true;
        prevLevel = s.level;
        prevExp = s.exp;
        prevGold = s.gold;
    }

    private void UpdateStatusWindowToggle()
    {
        bool chatting = ChatUI.Instance != null && ChatUI.Instance.IsChatting();   // 채팅 중엔 Tab / C 가 글자 입력

        if (chatting == false && (Input.GetKeyDown(KeyCode.Tab) || Input.GetKeyDown(KeyCode.C)))
        {
            statusWindow.SetActive(!statusWindow.activeSelf);
        }
    }

    private void UpdateMinimap(NetworkClient net)
    {
        GameObject me = net.GetLocalPlayerObject();
        bool inWorld = net.GetCurrentZoneId() == 0;

        minimapImage.gameObject.SetActive(inWorld);
        minimapLabel.gameObject.SetActive(inWorld == false);

        if (me == null)
        {
            return;
        }

        Vector2 center = me.transform.position;
        coordText.text = inWorld ? $"X {Mathf.RoundToInt(center.x)}   Y {Mathf.RoundToInt(center.y)}" : "";

        minimapTimer -= Time.deltaTime;

        if (inWorld == false || minimapTimer > 0.0f)
        {
            return;
        }

        minimapTimer = MapRefreshSec;
        DrawMinimap(net, center);
    }

    private void DrawMinimap(NetworkClient net, Vector2 center)
    {
        for (int i = 0; i < minimapPixels.Length; ++i)
        {
            minimapPixels[i] = MapGround;
        }

        float half = MapUnits * 0.5f;
        float left = center.x - half;
        float bottom = center.y - half;

        // 장애물: 화면을 덮는 청크들만 계산한다
        int c0x = WorldGen.ChunkOf(WorldGen.TileOf(left));
        int c1x = WorldGen.ChunkOf(WorldGen.TileOf(left + MapUnits));
        int c0y = WorldGen.ChunkOf(WorldGen.TileOf(bottom));
        int c1y = WorldGen.ChunkOf(WorldGen.TileOf(bottom + MapUnits));

        for (int cy = c0y; cy <= c1y; ++cy)
        {
            for (int cx = c0x; cx <= c1x; ++cx)
            {
                obstacleBuffer.Clear();
                WorldGen.ObstaclesInChunk(cx, cy, obstacleBuffer);

                foreach (WorldGen.ObstacleSpot o in obstacleBuffer)
                {
                    Color32 c = o.Kind == WorldGen.Obstacle.Tree ? MapTree : (o.Kind == WorldGen.Obstacle.Bush ? MapBush : MapRock);
                    Dot(o.TileX + 0.5f - left, o.TileY + 0.5f - bottom, 2, c, false);
                }
            }
        }

        foreach (HouseEntity house in net.GetHouses().Values)
        {
            if (house != null)
            {
                Vector2 p = house.transform.position;
                Dot(p.x - left, p.y - bottom, 6, MapHouse, true);
            }
        }

        foreach (MonsterEntity monster in net.GetMonsters().Values)
        {
            if (monster == null || monster.IsDead())
            {
                continue;
            }

            Vector2 p = monster.transform.position;

            switch (monster.Kind)
            {
                case "CatBoss": Dot(p.x - left, p.y - bottom, 6, MapBoss, true); break;
                case "Ladybug": Dot(p.x - left, p.y - bottom, 3, MapLadybug, false); break;
                default:        Dot(p.x - left, p.y - bottom, 3, MapCaterpillar, false); break;
            }
        }

        foreach (RemotePlayer other in net.GetRemotePlayers().Values)
        {
            if (other != null)
            {
                Vector2 p = other.transform.position;
                Dot(p.x - left, p.y - bottom, 4, MapOther, true);
            }
        }

        Dot(half, half, 5, MapMe, true);   // 나는 항상 가운데

        minimapTexture.SetPixels32(minimapPixels);
        minimapTexture.Apply(false);
    }

    // 월드 좌표(미니맵 왼쪽 아래 기준)에 size 픽셀 정사각형 점을 찍는다. edge면 테두리를 검게.
    private void Dot(float localX, float localY, int size, Color32 color, bool edge)
    {
        int px = Mathf.FloorToInt(localX * PixelsPerUnit) - size / 2;
        int py = Mathf.FloorToInt(localY * PixelsPerUnit) - size / 2;

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                int tx = px + x;
                int ty = py + y;

                if (tx < 0 || ty < 0 || tx >= MapPixels || ty >= MapPixels)
                {
                    continue;
                }

                bool border = edge && (x == 0 || y == 0 || x == size - 1 || y == size - 1);
                minimapPixels[ty * MapPixels + tx] = border ? MapEdge : color;
            }
        }
    }

    private void ShowLevelUp(int level)
    {
        levelUpText.text = "LEVEL UP!  Lv." + level;
        levelUpTimer = 2.0f;
        toastRoot.gameObject.SetActive(true);
    }

    private void AddGain(string message, Color color)
    {
        TextMeshProUGUI text = Text(gainRoot, "Gain", message, 14, color, new Vector2(0, 0.5f), Vector2.zero, new Vector2(220, 22), TextAlignmentOptions.Left);
        text.fontStyle = FontStyles.Bold;
        text.outlineWidth = 0.22f;
        text.outlineColor = new Color32(60, 40, 30, 255);
        gains.Add(new Gain { text = text, age = 0.0f });
    }

    private void UpdateToasts()
    {
        if (levelUpTimer > 0.0f)
        {
            levelUpTimer -= Time.deltaTime;

            // 처음 0.2초 동안 커졌다가 제자리로, 마지막 0.5초 동안 흐려진다
            float t = 2.0f - levelUpTimer;
            float scale = t < 0.2f ? Mathf.Lerp(0.6f, 1.15f, t / 0.2f) : (t < 0.35f ? Mathf.Lerp(1.15f, 1.0f, (t - 0.2f) / 0.15f) : 1.0f);
            toastRoot.localScale = new Vector3(scale, scale, 1.0f);
            levelUpText.alpha = Mathf.Clamp01(levelUpTimer / 0.5f);

            if (levelUpTimer <= 0.0f)
            {
                toastRoot.gameObject.SetActive(false);
            }
        }

        // 획득 알림: 골드 옆에서 아래로 흘러내리며 1.2초 뒤 사라진다. 여러 개면 아래로 쌓인다.
        // (위로 띄우면 프로필 패널과 겹친다)
        for (int i = gains.Count - 1; i >= 0; --i)
        {
            Gain gain = gains[i];
            gain.age += Time.deltaTime;

            if (gain.age >= 1.2f || gain.text == null)
            {
                if (gain.text != null)
                {
                    Destroy(gain.text.gameObject);
                }
                gains.RemoveAt(i);
                continue;
            }

            int stack = gains.Count - 1 - i;
            gain.text.rectTransform.anchoredPosition = new Vector2(0, -gain.age * 18.0f - stack * 20.0f);
            gain.text.alpha = 1.0f - Mathf.Clamp01((gain.age - 0.7f) / 0.5f);
        }
    }

    // 씬에 만들다 만 빈 상태창(StatWindow)이 있으면 HUD 상태창과 겹치므로 숨긴다.
    // StatusPanelUI가 붙어 있으면(직접 연결해서 쓰는 중이면) 건드리지 않는다.
    private void HideEmptySceneStatWindow()
    {
        if (built == false)
        {
            return;
        }

        GameObject sceneWindow = GameObject.Find("StatWindow");

        if (sceneWindow != null && sceneWindow.GetComponentInChildren<StatusPanelUI>(true) == null)
        {
            sceneWindow.SetActive(false);
        }
    }

    // =====================================================================
    // 도우미
    // =====================================================================

    // 한글 글꼴: 채팅 등 씬이 이미 쓰고 있는 Pretendard를 찾아 쓴다. 없으면 TMP 기본 글꼴.
    private static TMP_FontAsset FindKoreanFont()
    {
        foreach (TMP_FontAsset asset in Resources.FindObjectsOfTypeAll<TMP_FontAsset>())
        {
            if (asset != null && asset.name.Contains("Pretendard"))
            {
                return asset;
            }
        }

        return TMP_Settings.defaultFontAsset;
    }

    private static void Place(RectTransform rect, Vector2 anchor, Vector2 position, Vector2 size)
    {
        rect.anchorMin = anchor;
        rect.anchorMax = anchor;
        rect.pivot = anchor;
        rect.anchoredPosition = position;
        rect.sizeDelta = size;
    }

    private RectTransform Panel(RectTransform parent, string name, Sprite sprite, Vector2 anchor, Vector2 position, Vector2 size)
    {
        Image image = Img(parent, name, sprite, anchor, position, size);
        image.type = Image.Type.Sliced;
        image.pixelsPerUnitMultiplier = PixelScaleMultiplier;
        image.raycastTarget = false;
        return image.rectTransform;
    }

    private Image Img(RectTransform parent, string name, Sprite sprite, Vector2 anchor, Vector2 position, Vector2 size)
    {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        Image image = go.AddComponent<Image>();
        image.sprite = sprite;
        image.raycastTarget = false;
        Place(image.rectTransform, anchor, position, size);
        return image;
    }

    private TextMeshProUGUI Text(RectTransform parent, string name, string value, float size, Color color,
                                 Vector2 anchor, Vector2 position, Vector2 box, TextAlignmentOptions align)
    {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        TextMeshProUGUI text = go.AddComponent<TextMeshProUGUI>();

        if (font != null)
        {
            text.font = font;
        }

        text.text = value;
        text.fontSize = size;
        text.color = color;
        text.alignment = align;
        text.enableWordWrapping = false;
        text.overflowMode = TextOverflowModes.Overflow;
        text.raycastTarget = false;
        Place(text.rectTransform, anchor, position, box);
        return text;
    }

    // 막대: 배경 + 채움(왼쪽부터 ratio만큼 늘어남) + 가운데 숫자
    private RectTransform Bar(RectTransform parent, string name, Vector2 position, Vector2 size, Color color,
                              out TextMeshProUGUI label, out Image fillImage)
    {
        Image bg = Img(parent, name, barBgSprite, new Vector2(0, 1), position, size);
        bg.type = Image.Type.Sliced;
        bg.pixelsPerUnitMultiplier = PixelScaleMultiplier;

        var fillGo = new GameObject("Fill", typeof(RectTransform));
        fillGo.transform.SetParent(bg.rectTransform, false);
        fillImage = fillGo.AddComponent<Image>();
        fillImage.sprite = barFillSprite;
        fillImage.type = Image.Type.Sliced;
        fillImage.pixelsPerUnitMultiplier = PixelScaleMultiplier;
        fillImage.color = color;
        fillImage.raycastTarget = false;

        RectTransform fill = fillImage.rectTransform;
        fill.anchorMin = new Vector2(0, 0);
        fill.anchorMax = new Vector2(1, 1);
        fill.pivot = new Vector2(0, 0.5f);
        fill.offsetMin = new Vector2(2, 2);
        fill.offsetMax = new Vector2(-2, -2);

        label = Text(bg.rectTransform, "Value", "", 11, Color.white, new Vector2(0.5f, 0.5f), Vector2.zero, size, TextAlignmentOptions.Center);
        label.fontStyle = FontStyles.Bold;
        label.outlineWidth = 0.25f;
        label.outlineColor = new Color32(40, 24, 18, 255);

        return fill;
    }

    private static void SetBar(RectTransform fill, float ratio)
    {
        Vector2 max = fill.anchorMax;
        max.x = Mathf.Clamp01(ratio);
        fill.anchorMax = max;
        fill.gameObject.SetActive(ratio > 0.001f);   // 0일 때 테두리 조각만 남지 않게
    }
}
