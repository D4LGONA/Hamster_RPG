// 2026-11-16: 1:1 거래 창
//
// 신청: 다른 햄스터 우클릭 -> 정보 카드 [거래 신청]
// 거래 창: 왼쪽 = 내 가방(누르면 올리기/내리기, 최대 8칸), 오른쪽 = 상대가 올린 것
//          해바라기씨 -100/-10/+10/+100, [확인] [취소]
// 누가 뭘 바꾸면 양쪽 확인이 풀린다(서버가 그렇게 한다). 둘 다 확인하면 서버가 한 번에 바꿔 준다.
using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

public partial class GameHUD
{
    private GameObject tradeInviteWindow;
    private TextMeshProUGUI tradeInviteText;

    private GameObject tradeWindow;
    private TextMeshProUGUI tradeTitle, tradeMyGold, tradeTheirGold, tradeStatus;
    private readonly Image[] tradeMySlotIcons = new Image[Protocol.InventorySlots];
    private readonly Image[] tradeMySlotBg = new Image[Protocol.InventorySlots];
    private readonly TextMeshProUGUI[] tradeMySlotCounts = new TextMeshProUGUI[Protocol.InventorySlots];
    private readonly Image[] tradeTheirIcons = new Image[Protocol.TradeMaxSlots];
    private readonly TextMeshProUGUI[] tradeTheirCounts = new TextMeshProUGUI[Protocol.TradeMaxSlots];

    private readonly List<int> tradeSlots = new List<int>();   // 내가 올린 가방 칸
    private int tradeGold;
    private int tradeInventoryVersion = -1;
    private bool tradeMyConfirmed, tradeTheirConfirmed;

    private static readonly Color SlotPicked = new Color32(255, 214, 90, 255);

    private void BuildTradeWindows(RectTransform canvas)
    {
        // ---- 신청 받았을 때 ----
        RectTransform invite = Panel(canvas, "TradeInvite", panelSprite, new Vector2(0.5f, 0.5f), new Vector2(0, 120), new Vector2(300, 120));
        invite.GetComponent<Image>().raycastTarget = true;
        tradeInviteWindow = invite.gameObject;
        tradeInviteText = Text(invite, "Text", "", 16, Brown, new Vector2(0.5f, 1), new Vector2(0, -20), new Vector2(280, 24), TextAlignmentOptions.Center);
        tradeInviteText.fontStyle = FontStyles.Bold;
        SmallButton(invite, "수락", new Vector2(0.5f, 0), new Vector2(-60, 16), () => { NetworkClient.Instance?.SendTradeRespond(true); tradeInviteWindow.SetActive(false); });
        SmallButton(invite, "거절", new Vector2(0.5f, 0), new Vector2(60, 16), () => { NetworkClient.Instance?.SendTradeRespond(false); tradeInviteWindow.SetActive(false); });
        tradeInviteWindow.SetActive(false);

        // ---- 거래 창 ----
        const int columns = 5;
        const float cell = 40.0f;
        const float gap = 4.0f;

        RectTransform panel = Panel(canvas, "TradeWindow", panelSprite, new Vector2(0.5f, 0.5f), new Vector2(0, -10), new Vector2(520, 380));
        panel.GetComponent<Image>().raycastTarget = true;
        tradeWindow = panel.gameObject;

        tradeTitle = Text(panel, "Title", "거래", 20, Brown, new Vector2(0.5f, 1), new Vector2(0, -12), new Vector2(400, 28), TextAlignmentOptions.Center);
        tradeTitle.fontStyle = FontStyles.Bold;

        Text(panel, "MineLabel", "내 가방 (눌러서 올리기)", 13, BrownSoft, new Vector2(0, 1), new Vector2(20, -46), new Vector2(240, 18), TextAlignmentOptions.Left);

        for (int i = 0; i < Protocol.InventorySlots; ++i)
        {
            int index = i;
            float x = 20 + (i % columns) * (cell + gap);
            float y = -68 - (i / columns) * (cell + gap);

            RectTransform slot = Panel(panel, "Mine" + i, panelDarkSprite, new Vector2(0, 1), new Vector2(x, y), new Vector2(cell, cell));
            tradeMySlotBg[i] = slot.GetComponent<Image>();
            tradeMySlotBg[i].raycastTarget = true;
            slot.gameObject.AddComponent<Button>().onClick.AddListener(() => ToggleTradeSlot(index));

            tradeMySlotIcons[i] = Img(slot, "Icon", null, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(28, 28));
            tradeMySlotIcons[i].preserveAspect = true;
            tradeMySlotCounts[i] = Text(slot, "Count", "", 11, Color.white, new Vector2(1, 0), new Vector2(-3, 1), new Vector2(36, 14), TextAlignmentOptions.Right);
            tradeMySlotCounts[i].outlineWidth = 0.25f;
            tradeMySlotCounts[i].outlineColor = new Color32(30, 20, 14, 255);
        }

        // 내 해바라기씨
        tradeMyGold = Text(panel, "MyGold", "", 15, Brown, new Vector2(0, 1), new Vector2(20, -252), new Vector2(216, 22), TextAlignmentOptions.Center);
        tradeMyGold.fontStyle = FontStyles.Bold;
        int[] steps = { -100, -10, 10, 100 };
        for (int k = 0; k < steps.Length; ++k)
        {
            int step = steps[k];
            SmallButton(panel, (step > 0 ? "+" : "") + step, new Vector2(0, 1), new Vector2(20 + k * 55, -280), () => ChangeTradeGold(step), new Vector2(50, 28));
        }

        // 상대가 올린 것
        Text(panel, "TheirLabel", "상대가 올린 것", 13, BrownSoft, new Vector2(1, 1), new Vector2(-20, -46), new Vector2(216, 18), TextAlignmentOptions.Left);
        for (int i = 0; i < Protocol.TradeMaxSlots; ++i)
        {
            float x = -20 - (3 - (i % 4)) * (cell + gap + 10);
            float y = -68 - (i / 4) * (cell + gap);
            RectTransform slot = Panel(panel, "Theirs" + i, panelDarkSprite, new Vector2(1, 1), new Vector2(x, y), new Vector2(cell + 10, cell));
            tradeTheirIcons[i] = Img(slot, "Icon", null, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(28, 28));
            tradeTheirIcons[i].preserveAspect = true;
            tradeTheirCounts[i] = Text(slot, "Count", "", 11, Color.white, new Vector2(1, 0), new Vector2(-3, 1), new Vector2(46, 14), TextAlignmentOptions.Right);
            tradeTheirCounts[i].outlineWidth = 0.25f;
            tradeTheirCounts[i].outlineColor = new Color32(30, 20, 14, 255);
        }
        tradeTheirGold = Text(panel, "TheirGold", "", 15, Brown, new Vector2(1, 1), new Vector2(-20, -164), new Vector2(216, 22), TextAlignmentOptions.Center);
        tradeTheirGold.fontStyle = FontStyles.Bold;

        tradeStatus = Text(panel, "Status", "", 14, Brown, new Vector2(0.5f, 0), new Vector2(0, 62), new Vector2(480, 22), TextAlignmentOptions.Center);
        tradeStatus.fontStyle = FontStyles.Bold;

        SmallButton(panel, "확인", new Vector2(0.5f, 0), new Vector2(-70, 16), () => NetworkClient.Instance?.SendTradeConfirm(), new Vector2(120, 38));
        SmallButton(panel, "취소", new Vector2(0.5f, 0), new Vector2(70, 16), () => NetworkClient.Instance?.SendTradeCancel(), new Vector2(120, 38));

        tradeWindow.SetActive(false);
    }

    private void SmallButton(RectTransform parent, string label, Vector2 anchor, Vector2 position, System.Action onClick, Vector2? size = null)
    {
        Vector2 s = size ?? new Vector2(100, 34);
        RectTransform button = Panel(parent, "Button_" + label, Resources.Load<Sprite>("UI/ui_button"), anchor, position, s);
        button.GetComponent<Image>().raycastTarget = true;
        button.gameObject.AddComponent<Button>().onClick.AddListener(() => onClick());
        TextMeshProUGUI text = Text(button, "Label", label, 15, Color.white, new Vector2(0.5f, 0.5f), Vector2.zero, s, TextAlignmentOptions.Center);
        text.fontStyle = FontStyles.Bold;
        text.outlineWidth = 0.22f;
        text.outlineColor = new Color32(120, 60, 20, 255);
    }

    private void UpdateTrade(NetworkClient net)
    {
        if (tradeWindow.activeSelf == false)
        {
            return;
        }

        int version = net.GetInventoryVersion();
        if (version != tradeInventoryVersion)
        {
            tradeInventoryVersion = version;
            RefreshTradeMine(net);
        }
    }

    private void RefreshTradeMine(NetworkClient net)
    {
        for (int i = 0; i < Protocol.InventorySlots; ++i)
        {
            NetworkClient.InventorySlot slot = net.GetInventorySlot(i);
            ItemTable.Def def = slot.itemId != 0 ? ItemTable.Find(slot.itemId) : null;
            tradeMySlotIcons[i].sprite = def != null ? def.Icon : null;
            tradeMySlotIcons[i].enabled = def != null;
            tradeMySlotCounts[i].text = def == null ? "" : (slot.enhance > 0 ? "+" + slot.enhance : (slot.count > 1 ? slot.count.ToString() : ""));
            tradeMySlotBg[i].color = tradeSlots.Contains(i) ? SlotPicked : Color.white;
        }

        tradeMyGold.text = "내 해바라기씨  " + tradeGold + " / " + net.GetMyStats().gold;
    }

    private void ToggleTradeSlot(int index)
    {
        NetworkClient net = NetworkClient.Instance;
        if (net == null || net.GetInventorySlot(index).itemId == 0)
        {
            return;
        }

        if (tradeSlots.Remove(index) == false)
        {
            if (tradeSlots.Count >= Protocol.TradeMaxSlots)
            {
                tradeStatus.text = "한 번에 8칸까지만 올릴 수 있어요";
                return;
            }
            tradeSlots.Add(index);
        }

        net.SendTradeOffer(tradeSlots, tradeGold);
        RefreshTradeMine(net);
    }

    private void ChangeTradeGold(int delta)
    {
        NetworkClient net = NetworkClient.Instance;
        if (net == null)
        {
            return;
        }

        tradeGold = Mathf.Clamp(tradeGold + delta, 0, net.GetMyStats().gold);
        net.SendTradeOffer(tradeSlots, tradeGold);
        RefreshTradeMine(net);
    }

    private void ShowTradeInvite(string fromName)
    {
        tradeInviteText.text = fromName + " 님이 거래를 원해요";
        tradeInviteWindow.SetActive(true);
    }

    private void OpenTrade(string partnerName)
    {
        tradeInviteWindow.SetActive(false);
        tradeSlots.Clear();
        tradeGold = 0;
        tradeMyConfirmed = tradeTheirConfirmed = false;
        tradeTitle.text = partnerName + " 님과 거래";
        tradeStatus.text = "";
        tradeInventoryVersion = -1;
        inventoryWindow.SetActive(false);
        tradeWindow.SetActive(true);
    }

    private void ApplyTradeUpdate(NetworkClient.TradeOffer mine, NetworkClient.TradeOffer theirs)
    {
        tradeMyConfirmed = mine.confirmed;
        tradeTheirConfirmed = theirs.confirmed;

        for (int i = 0; i < Protocol.TradeMaxSlots; ++i)
        {
            bool has = i < theirs.items.Length;
            ItemTable.Def def = has ? ItemTable.Find(theirs.items[i].itemId) : null;
            tradeTheirIcons[i].sprite = def != null ? def.Icon : null;
            tradeTheirIcons[i].enabled = def != null;
            tradeTheirCounts[i].text = !has ? "" : (theirs.items[i].enhance > 0 ? "+" + theirs.items[i].enhance
                                                    : (theirs.items[i].count > 1 ? theirs.items[i].count.ToString() : ""));
        }

        tradeTheirGold.text = "상대 해바라기씨  " + theirs.gold;

        if (tradeMyConfirmed && tradeTheirConfirmed) tradeStatus.text = "교환 중...";
        else if (tradeMyConfirmed) tradeStatus.text = "나: 확인 ✔   상대: 고민 중...";
        else if (tradeTheirConfirmed) tradeStatus.text = "상대가 확인했어요! 괜찮으면 확인을 눌러요";
        else tradeStatus.text = "올릴 걸 고르고 확인을 눌러요";
    }

    private void CloseTradeWindow(int reason)
    {
        bool wasOpen = tradeWindow.activeSelf || tradeInviteWindow.activeSelf;
        tradeWindow.SetActive(false);
        tradeInviteWindow.SetActive(false);
        tradeSlots.Clear();
        tradeGold = 0;

        switch (reason)
        {
            case 0: ShowToast("거래 완료!", new Color32(160, 255, 160, 255)); break;
            case 1: if (wasOpen) ShowToast("거래를 취소했어요", Color.white); break;
            case 2: ShowToast("상대가 떠나서 거래가 끝났어요", new Color32(255, 200, 140, 255)); break;
            case 3: ShowToast("올린 물건이 바뀌어서 취소됐어요", new Color32(255, 160, 160, 255)); break;
            case 4: ShowToast("가방이 꽉 차서 교환할 수 없어요", new Color32(255, 160, 160, 255)); break;
            case 5: ShowToast("거래 신청이 거절/만료됐어요", Color.white); break;
            case 6: ShowToast("지금은 거래를 신청할 수 없어요", Color.white); break;
            case 7: ShowToast("조금 더 가까이 가야 해요", Color.white); break;
            case 8: ShowToast("그 햄스터는 이미 거래 중이에요", Color.white); break;
        }
    }
}
