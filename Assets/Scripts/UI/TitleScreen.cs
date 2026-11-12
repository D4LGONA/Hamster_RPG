// 2026-11-12: 타이틀 화면 꾸미기. TitleScene이 로드되면 스스로 만들어진다(씬 편집 불필요).
//
// 씬에 원래 있던 이름 입력칸(TMP_InputField)과 입장 버튼은 그대로 쓴다(TitleConnectUI가 연결해 둔 동작 유지).
// 이 스크립트는 그 둘을 새 화면으로 옮겨와 모양만 바꾸고, 배경/로고/움직이는 몬스터/조작 안내를 더한다.

using TMPro;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

public class TitleScreen : MonoBehaviour
{
    private const string TargetScene = "TitleScene";

    private static readonly Color Brown = new Color32(92, 58, 36, 255);
    private static readonly Color BrownSoft = new Color32(150, 112, 84, 255);

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    private static void Hook()
    {
        SceneManager.sceneLoaded -= OnSceneLoaded;
        SceneManager.sceneLoaded += OnSceneLoaded;
        OnSceneLoaded(SceneManager.GetActiveScene(), LoadSceneMode.Single);
    }

    private static void OnSceneLoaded(Scene scene, LoadSceneMode mode)
    {
        if (scene.name != TargetScene || FindObjectOfType<TitleScreen>() != null)
        {
            return;
        }

        var go = new GameObject("TitleScreen");
        SceneManager.MoveGameObjectToScene(go, scene);
        go.AddComponent<TitleScreen>().Build();
    }

    // 걸어다니는 장식 몬스터
    private class Walker
    {
        public RectTransform rect;
        public Image image;
        public Sprite[] frames;
        public int row;          // 1 오른쪽, 2 왼쪽
        public float speed;      // px/s (음수면 왼쪽)
        public float clock;
    }

    private Walker[] walkers;
    private RectTransform hamster;
    private RectTransform cat;
    private Image catImage;
    private Sprite[] catFrames;
    private TMP_InputField input;
    private Button enterButton;
    private TextMeshProUGUI statusText;
    private float time;

    private void Build()
    {
        TMP_FontAsset font = FindKoreanFont();

        var canvasGo = new GameObject("TitleCanvas", typeof(RectTransform));
        canvasGo.transform.SetParent(transform, false);
        var canvas = canvasGo.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 20;
        var scaler = canvasGo.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(800, 800);
        scaler.matchWidthOrHeight = 0.5f;
        canvasGo.AddComponent<GraphicRaycaster>();
        RectTransform root = canvasGo.GetComponent<RectTransform>();

        // 배경: 화면 꽉 채움
        Image bg = NewImage(root, "Background", Resources.Load<Sprite>("UI/title_bg"));
        bg.rectTransform.anchorMin = Vector2.zero;
        bg.rectTransform.anchorMax = Vector2.one;
        bg.rectTransform.offsetMin = Vector2.zero;
        bg.rectTransform.offsetMax = Vector2.zero;

        // 길 위를 오가는 애벌레/무당벌레 (배경 그림의 흙길 높이)
        Sprite[] caterpillar = LoadSheet("Monsters/Monster_Caterpillar");
        Sprite[] ladybug = LoadSheet("Monsters/Monster_Ladybug");
        walkers = new[]
        {
            NewWalker(root, caterpillar, -330, -262, 46),
            NewWalker(root, ladybug, 260, -286, -60),
            NewWalker(root, caterpillar, 120, -300, 34),
        };

        // 오른쪽 아래에서 고개를 내민 고양이 대장
        catFrames = LoadSheet("Monsters/Boss_Cat");
        if (catFrames != null)
        {
            catImage = NewImage(root, "Cat", catFrames[0]);
            cat = catImage.rectTransform;
            Place(cat, new Vector2(1, 0), new Vector2(-40, 70), new Vector2(160, 160));
        }

        // 가운데 햄스터 (숨쉬듯 통통)
        Image ham = NewImage(root, "Hamster", Resources.Load<Sprite>("UI/portrait_hamster"));
        hamster = ham.rectTransform;
        Place(hamster, new Vector2(0.5f, 0.5f), new Vector2(0, -2), new Vector2(132, 132));   // 아랫부분은 입장 패널 뒤로 숨는다(빼꼼)

        // 로고
        Image logo = NewImage(root, "Logo", Resources.Load<Sprite>("UI/title_logo"));
        logo.preserveAspect = true;
        Place(logo.rectTransform, new Vector2(0.5f, 1), new Vector2(0, -70), new Vector2(620, 188));

        // 입장 패널
        Image panel = NewImage(root, "LoginPanel", Resources.Load<Sprite>("UI/ui_panel"));
        panel.type = Image.Type.Sliced;
        panel.pixelsPerUnitMultiplier = GameHUD.PixelScaleMultiplier;
        RectTransform panelRect = panel.rectTransform;
        Place(panelRect, new Vector2(0.5f, 0.5f), new Vector2(0, -150), new Vector2(360, 176));

        NewText(panelRect, "Label", "햄스터 이름", 16, BrownSoft, font, new Vector2(0.5f, 1), new Vector2(0, -14), new Vector2(300, 22));

        AdoptSceneControls(panelRect, font);

        statusText = NewText(panelRect, "Status", "", 13, BrownSoft, font, new Vector2(0.5f, 0), new Vector2(0, -26), new Vector2(340, 20));

        // 조작 안내
        TextMeshProUGUI hint = NewText(root, "Hint", "WASD 이동   ·   클릭 공격   ·   T 집 짓기   ·   Tab 상태창",
                                       14, Color.white, font, new Vector2(0.5f, 0), new Vector2(0, 16), new Vector2(760, 24));
        hint.outlineWidth = 0.25f;
        hint.outlineColor = new Color32(60, 40, 30, 255);
    }

    // 씬의 입력칸/버튼을 새 패널로 옮기고 모양을 바꾼다. 원래 캔버스는 그리지 않게 끈다.
    private void AdoptSceneControls(RectTransform panel, TMP_FontAsset font)
    {
        input = FindObjectOfType<TMP_InputField>(true);
        TitleConnectUI connect = FindObjectOfType<TitleConnectUI>(true);
        enterButton = connect != null ? connect.GetComponentInChildren<Button>(true) : FindObjectOfType<Button>(true);

        Canvas sceneCanvas = input != null ? input.GetComponentInParent<Canvas>() : null;

        if (input != null)
        {
            input.transform.SetParent(panel, false);
            Place((RectTransform)input.transform, new Vector2(0.5f, 1), new Vector2(0, -42), new Vector2(300, 46));

            Image box = input.GetComponent<Image>();
            if (box != null)
            {
                box.sprite = Resources.Load<Sprite>("UI/ui_panel_dark");
                box.type = Image.Type.Sliced;
                box.pixelsPerUnitMultiplier = GameHUD.PixelScaleMultiplier;
                box.color = Color.white;
            }

            StyleText(input.textComponent, font, 20, Color.white);
            if (input.placeholder is TMP_Text placeholder)
            {
                StyleText(placeholder, font, 18, new Color(1, 1, 1, 0.45f));
                placeholder.text = "이름을 입력하세요";
                placeholder.fontStyle = FontStyles.Normal;
            }
            input.characterLimit = 6;   // 한글 6자 = UTF-8 18바이트 < 서버 NAME_SIZE(20)
            input.onSubmit.AddListener(_ => PressEnter());
            input.ActivateInputField();
        }

        if (enterButton != null)
        {
            enterButton.transform.SetParent(panel, false);
            Place((RectTransform)enterButton.transform, new Vector2(0.5f, 1), new Vector2(0, -100), new Vector2(300, 50));

            Image face = enterButton.GetComponent<Image>();
            if (face != null)
            {
                face.sprite = Resources.Load<Sprite>("UI/ui_button");
                face.type = Image.Type.Sliced;
                face.pixelsPerUnitMultiplier = GameHUD.PixelScaleMultiplier;
                face.color = Color.white;
            }

            TMP_Text label = enterButton.GetComponentInChildren<TMP_Text>(true);
            if (label != null)
            {
                StyleText(label, font, 22, Color.white);
                label.text = "입장하기";
                label.fontStyle = FontStyles.Bold;
                label.outlineWidth = 0.22f;
                label.outlineColor = new Color32(120, 60, 20, 255);
            }
        }

        // 옮기고 남은 원래 화면(파란 배경 등)은 그리지 않는다. 오브젝트와 TitleConnectUI는 그대로 살아 있다.
        if (sceneCanvas != null)
        {
            sceneCanvas.enabled = false;
        }
    }

    private void PressEnter()
    {
        if (enterButton != null && enterButton.interactable)
        {
            enterButton.onClick.Invoke();
        }
    }

    private void Update()
    {
        time += Time.deltaTime;

        if (hamster != null)
        {
            float squash = Mathf.Sin(time * 3.0f) * 0.04f;
            hamster.localScale = new Vector3(1.0f + squash, 1.0f - squash, 1.0f);
        }

        if (cat != null && catFrames != null)
        {
            cat.anchoredPosition = new Vector2(-40, 70 + Mathf.Round(Mathf.Sin(time * 1.5f) * 4.0f));
        }

        if (walkers != null)
        {
            foreach (Walker w in walkers)
            {
                if (w == null)
                {
                    continue;
                }

                Vector2 p = w.rect.anchoredPosition;
                p.x += w.speed * Time.deltaTime;
                if (p.x > 460) { p.x = -460; }
                if (p.x < -460) { p.x = 460; }
                w.rect.anchoredPosition = p;

                w.clock += Time.deltaTime * 8.0f;
                w.image.sprite = w.frames[w.row * 4 + ((int)w.clock % 4)];
            }
        }

        if (statusText != null && enterButton != null)
        {
            statusText.text = enterButton.interactable ? "" : "서버에 접속하는 중...";
        }
    }

    // ---------------------------------------------------------------- 도우미

    private Walker NewWalker(RectTransform root, Sprite[] frames, float x, float y, float speed)
    {
        if (frames == null)
        {
            return null;
        }

        Image image = NewImage(root, "Walker", frames[0]);
        Place(image.rectTransform, new Vector2(0.5f, 0.5f), new Vector2(x, y), new Vector2(80, 80));
        return new Walker { rect = image.rectTransform, image = image, frames = frames, row = speed > 0 ? 1 : 2, speed = speed };
    }

    private static Sprite[] LoadSheet(string path)
    {
        Sprite[] sliced = Resources.LoadAll<Sprite>(path);
        return sliced != null && sliced.Length >= 16 ? WorldMapView.SortByIndex(sliced, 16) : null;
    }

    private static Image NewImage(RectTransform parent, string name, Sprite sprite)
    {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        Image image = go.AddComponent<Image>();
        image.sprite = sprite;
        image.raycastTarget = false;
        return image;
    }

    private static TextMeshProUGUI NewText(RectTransform parent, string name, string value, float size, Color color,
                                           TMP_FontAsset font, Vector2 anchor, Vector2 position, Vector2 box)
    {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        TextMeshProUGUI text = go.AddComponent<TextMeshProUGUI>();
        StyleText(text, font, size, color);
        text.text = value;
        text.alignment = TextAlignmentOptions.Center;
        text.enableWordWrapping = false;
        text.raycastTarget = false;
        Place(text.rectTransform, anchor, position, box);
        return text;
    }

    private static void StyleText(TMP_Text text, TMP_FontAsset font, float size, Color color)
    {
        if (text == null)
        {
            return;
        }

        if (font != null)
        {
            text.font = font;
        }
        text.fontSize = size;
        text.color = color;
    }

    private static void Place(RectTransform rect, Vector2 anchor, Vector2 position, Vector2 size)
    {
        rect.anchorMin = anchor;
        rect.anchorMax = anchor;
        rect.pivot = anchor;
        rect.anchoredPosition = position;
        rect.sizeDelta = size;
    }

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
}
