// 2026-11-13: 집 꾸미기 모드 (B키).
//
//   아래 가구 버튼을 누르면 → 마우스를 따라다니는 미리보기(놓을 수 있으면 초록, 안 되면 빨강)
//   R: 90도 돌리기 / 왼쪽 클릭: 놓기 / 오른쪽 클릭·Esc: 취소
//   (아무것도 안 들고) 놓인 가구 클릭 → 들어서 옮기기, 들고 있을 때 Delete·X → 치우기
//
// 미리보기 색은 서버와 같은 규칙(FurnitureTable.Fits)으로 미리 계산할 뿐이고, 진짜 판정은 서버가 한다.
// 서버가 거절하면 이유를 화면에 띄운다.

using TMPro;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

public class HouseBuildMode : MonoBehaviour
{
    public static bool IsActive { get; private set; }   // 켜져 있는 동안 클릭이 공격으로 나가지 않게(PlayerAttackInput)

    private const float Snap = 0.25f;   // 0.25 칸 단위로 맞춘다

    private bool active;
    private int heldType = -1;           // 들고 있는 가구 종류(-1이면 없음)
    private uint movingId;               // 옮기는 중인 가구(0이면 새로 놓는 중)
    private int rotation;
    private SpriteRenderer ghost;

    private GameObject paletteRoot;
    private TextMeshProUGUI hintText, messageText;
    private float messageTimer;

    private void Start()
    {
        var ghostGo = new GameObject("FurnitureGhost");
        ghostGo.transform.SetParent(transform, false);
        ghost = ghostGo.AddComponent<SpriteRenderer>();
        ghost.sortingOrder = 500;
        ghost.enabled = false;

        BuildPalette();

        if (NetworkClient.Instance != null)
        {
            NetworkClient.Instance.OnFurnitureFailed += ShowFailReason;
        }
    }

    private void OnDestroy()
    {
        IsActive = false;
        if (NetworkClient.Instance != null)
        {
            NetworkClient.Instance.OnFurnitureFailed -= ShowFailReason;
        }
    }

    private void Update()
    {
        bool chatting = ChatUI.Instance != null && ChatUI.Instance.IsChatting();

        if (chatting == false && Input.GetKeyDown(KeyCode.B))
        {
            SetActive(!active);
        }

        if (messageTimer > 0.0f)
        {
            messageTimer -= Time.deltaTime;
            if (messageTimer <= 0.0f && messageText != null) messageText.text = "";
        }

        if (active == false)
        {
            return;
        }

        Camera cam = Camera.main;
        if (cam == null) return;

        Vector3 mouse = cam.ScreenToWorldPoint(Input.mousePosition);
        Vector2 snapped = new Vector2(Mathf.Round(mouse.x / Snap) * Snap, Mathf.Round(mouse.y / Snap) * Snap);
        bool overUI = EventSystem.current != null && EventSystem.current.IsPointerOverGameObject();

        if (heldType < 0)
        {
            ghost.enabled = false;

            // 아무것도 안 들고 놓인 가구를 누르면 들어 올린다
            if (Input.GetMouseButtonDown(0) && overUI == false && HouseFurnitureView.Instance != null &&
                HouseFurnitureView.Instance.TryPick(mouse, out uint picked) &&
                NetworkClient.Instance.GetFurniture().TryGetValue(picked, out NetworkClient.FurnitureData data))
            {
                heldType = data.itemType;
                rotation = data.rotation;
                movingId = picked;
                HouseFurnitureView.Instance.SetHidden(picked, true);
            }
            return;
        }

        if (chatting == false && Input.GetKeyDown(KeyCode.R))
        {
            rotation = (rotation + 1) % 4;
        }

        if (Input.GetMouseButtonDown(1) || Input.GetKeyDown(KeyCode.Escape))
        {
            Drop();
            return;
        }

        if (movingId != 0 && chatting == false && (Input.GetKeyDown(KeyCode.Delete) || Input.GetKeyDown(KeyCode.X)))
        {
            NetworkClient.Instance.SendFurnitureRemove(movingId);
            Drop();
            return;
        }

        bool fits = FurnitureTable.Fits(heldType, rotation, snapped, movingId, NetworkClient.Instance.GetFurniture());

        ghost.enabled = true;
        ghost.sprite = FurnitureTable.GetSprite(heldType);
        ghost.transform.position = new Vector3(snapped.x, snapped.y, 0.0f);
        ghost.transform.rotation = Quaternion.Euler(0.0f, 0.0f, rotation * 90.0f);
        ghost.color = fits ? new Color(0.7f, 1.0f, 0.7f, 0.75f) : new Color(1.0f, 0.5f, 0.5f, 0.6f);

        if (Input.GetMouseButtonDown(0) && overUI == false)
        {
            if (fits == false)
            {
                ShowMessage("여기엔 놓을 수 없어요");
                return;
            }

            if (movingId != 0)
            {
                NetworkClient.Instance.SendFurnitureMove(movingId, rotation, snapped);
            }
            else
            {
                NetworkClient.Instance.SendFurniturePlace(heldType, rotation, snapped);
            }

            // 옮기기는 한 번으로 끝. 새로 놓기는 같은 가구를 계속 놓을 수 있게 들고 있는다.
            if (movingId != 0) Drop();
        }
    }

    private void SetActive(bool value)
    {
        active = value;
        IsActive = value;
        paletteRoot.SetActive(value);
        if (value == false) Drop();
    }

    private void Drop()
    {
        if (movingId != 0 && HouseFurnitureView.Instance != null)
        {
            HouseFurnitureView.Instance.SetHidden(movingId, false);
        }

        heldType = -1;
        movingId = 0;
        ghost.enabled = false;
    }

    private void Pick(int type)
    {
        Drop();
        heldType = type;
        rotation = 0;
    }

    private void ShowFailReason(int reason)
    {
        string message;
        switch (reason)
        {
            case 1: message = "집 안에서만 꾸밀 수 있어요"; break;
            case 2: message = "내 집에서만 꾸밀 수 있어요"; break;
            case 3: message = "방 밖으로 나가요"; break;
            case 4: message = "다른 가구와 겹쳐요"; break;
            case 5: message = "입구는 비워 두세요"; break;
            case 6: message = "가구는 30개까지예요"; break;
            case 7: message = "조금 천천히!"; break;
            default: message = "없는 가구예요"; break;
        }
        ShowMessage(message);
    }

    private void ShowMessage(string message)
    {
        if (messageText == null) return;
        messageText.text = message;
        messageTimer = 2.0f;
    }

    // 아래쪽 가구 고르기 줄 + 안내. HUD와 같은 그림/크기 규칙(800x800 기준).
    private void BuildPalette()
    {
        // 집 씬에는 카메라만 있어서 EventSystem이 없을 수 있다. 없으면 버튼이 눌리지 않으니 만든다.
        if (EventSystem.current == null && FindObjectOfType<EventSystem>() == null)
        {
            new GameObject("EventSystem", typeof(EventSystem), typeof(StandaloneInputModule));
        }

        var canvasGo = new GameObject("BuildCanvas", typeof(RectTransform));
        canvasGo.transform.SetParent(transform, false);
        var canvas = canvasGo.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 60;
        var scaler = canvasGo.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(800, 800);
        scaler.matchWidthOrHeight = 0.5f;
        canvasGo.AddComponent<GraphicRaycaster>();
        RectTransform root = canvasGo.GetComponent<RectTransform>();

        TMP_FontAsset font = null;
        foreach (TMP_FontAsset asset in Resources.FindObjectsOfTypeAll<TMP_FontAsset>())
        {
            if (asset != null && asset.name.Contains("Pretendard")) { font = asset; break; }
        }

        // 꺼져 있어도 보이는 작은 안내
        messageText = MakeText(root, "", 18, new Color32(255, 230, 200, 255), font, new Vector2(0.5f, 0), new Vector2(0, 150), new Vector2(500, 26));
        MakeText(root, "B: 집 꾸미기", 13, new Color32(255, 246, 226, 220), font, new Vector2(1, 0), new Vector2(-14, 12), new Vector2(160, 20)).alignment = TextAlignmentOptions.Right;

        paletteRoot = new GameObject("Palette", typeof(RectTransform));
        paletteRoot.transform.SetParent(root, false);
        RectTransform paletteRect = (RectTransform)paletteRoot.transform;
        paletteRect.anchorMin = paletteRect.anchorMax = paletteRect.pivot = new Vector2(0.5f, 0);
        paletteRect.anchoredPosition = new Vector2(0, 40);
        paletteRect.sizeDelta = new Vector2(440, 100);

        Image bg = paletteRoot.AddComponent<Image>();
        bg.sprite = Resources.Load<Sprite>("UI/ui_panel");
        bg.type = Image.Type.Sliced;
        bg.pixelsPerUnitMultiplier = GameHUD.PixelScaleMultiplier;

        for (int i = 0; i < FurnitureTable.Count; ++i)
        {
            int type = i;
            var slot = new GameObject(FurnitureTable.Names[i], typeof(RectTransform));
            slot.transform.SetParent(paletteRect, false);
            RectTransform r = (RectTransform)slot.transform;
            r.anchorMin = r.anchorMax = r.pivot = new Vector2(0, 0.5f);
            r.anchoredPosition = new Vector2(16 + i * 84, 6);
            r.sizeDelta = new Vector2(72, 64);

            Image face = slot.AddComponent<Image>();
            face.sprite = Resources.Load<Sprite>("UI/ui_panel_dark");
            face.type = Image.Type.Sliced;
            face.pixelsPerUnitMultiplier = GameHUD.PixelScaleMultiplier;
            slot.AddComponent<Button>().onClick.AddListener(() => Pick(type));

            var icon = new GameObject("Icon", typeof(RectTransform));
            icon.transform.SetParent(r, false);
            Image iconImage = icon.AddComponent<Image>();
            iconImage.sprite = FurnitureTable.GetSprite(i);
            iconImage.preserveAspect = true;
            iconImage.raycastTarget = false;
            RectTransform ir = (RectTransform)icon.transform;
            ir.sizeDelta = new Vector2(56, 40);
            ir.anchoredPosition = new Vector2(0, 6);

            TextMeshProUGUI label = MakeText(r, FurnitureTable.Names[i], 13, Color.white, font, new Vector2(0.5f, 0), new Vector2(0, 4), new Vector2(72, 18));
            label.raycastTarget = false;
        }

        hintText = MakeText(paletteRect, "R 돌리기 · 클릭 놓기 · 우클릭 취소 · 놓인 가구 클릭해 옮기기 · X 치우기",
                            12, new Color32(150, 112, 84, 255), font, new Vector2(0.5f, 0), new Vector2(0, 6), new Vector2(430, 16));
        paletteRoot.SetActive(false);
    }

    private static TextMeshProUGUI MakeText(RectTransform parent, string value, float size, Color color, TMP_FontAsset font,
                                            Vector2 anchor, Vector2 position, Vector2 box)
    {
        var go = new GameObject("Text", typeof(RectTransform));
        go.transform.SetParent(parent, false);
        TextMeshProUGUI text = go.AddComponent<TextMeshProUGUI>();
        if (font != null) text.font = font;
        text.text = value;
        text.fontSize = size;
        text.color = color;
        text.alignment = TextAlignmentOptions.Center;
        text.enableWordWrapping = false;
        text.raycastTarget = false;
        text.outlineWidth = 0.2f;
        text.outlineColor = new Color32(60, 40, 30, 255);
        RectTransform r = text.rectTransform;
        r.anchorMin = r.anchorMax = r.pivot = anchor;
        r.anchoredPosition = position;
        r.sizeDelta = box;
        return text;
    }
}
