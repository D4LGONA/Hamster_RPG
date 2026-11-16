// 2026-11-16: 다른 햄스터 우클릭 -> 정보 카드
//
// 마우스 아래(0.9칸 안)에서 가장 가까운 햄스터를 골라 서버에 물어본다.
// 클라가 아는 건 이름/위치뿐이라 레벨·전투력·무기는 서버가 알려 준다(서버가 '보이는 사람인지'도 확인).
// 카드는 그 햄스터가 화면에서 사라지면(멀어짐/로그아웃) 저절로 닫힌다.
using TMPro;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

public partial class GameHUD
{
    private const float InfoPickRadius = 0.9f;

    private GameObject infoCard;
    private TextMeshProUGUI infoName, infoLevel, infoPower, infoStats, infoWeapon;
    private Image infoWeaponIcon;
    private uint infoTargetId;

    private void BuildPlayerInfoCard(RectTransform canvas)
    {
        RectTransform panel = Panel(canvas, "PlayerInfoCard", panelSprite, new Vector2(1, 0.5f), new Vector2(-12, 40), new Vector2(230, 236));
        panel.GetComponent<Image>().raycastTarget = true;
        infoCard = panel.gameObject;

        Image portrait = Img(panel, "Portrait", portraitSprite, new Vector2(0, 1), new Vector2(16, -16), new Vector2(52, 52));
        portrait.preserveAspect = true;

        infoName = Text(panel, "Name", "", 18, Brown, new Vector2(0, 1), new Vector2(78, -18), new Vector2(120, 24), TextAlignmentOptions.Left);
        infoName.fontStyle = FontStyles.Bold;
        infoLevel = Text(panel, "Level", "", 14, BrownSoft, new Vector2(0, 1), new Vector2(78, -44), new Vector2(120, 20), TextAlignmentOptions.Left);

        RectTransform close = Panel(panel, "Close", panelDarkSprite, new Vector2(1, 1), new Vector2(-8, -8), new Vector2(26, 26));
        Text(close, "X", "X", 14, Color.white, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(26, 26), TextAlignmentOptions.Center).fontStyle = FontStyles.Bold;
        close.GetComponent<Image>().raycastTarget = true;
        close.gameObject.AddComponent<Button>().onClick.AddListener(() => infoCard.SetActive(false));

        infoPower = Text(panel, "Power", "", 17, Gold, new Vector2(0.5f, 1), new Vector2(0, -80), new Vector2(200, 24), TextAlignmentOptions.Center);
        infoPower.fontStyle = FontStyles.Bold;
        infoPower.outlineWidth = 0.22f;
        infoPower.outlineColor = new Color32(120, 60, 20, 255);

        infoStats = Text(panel, "Stats", "", 14, Brown, new Vector2(0.5f, 1), new Vector2(0, -110), new Vector2(200, 60), TextAlignmentOptions.Center);
        infoStats.enableWordWrapping = true;

        RectTransform weaponFrame = Panel(panel, "WeaponFrame", panelDarkSprite, new Vector2(0, 0), new Vector2(16, 16), new Vector2(48, 48));
        infoWeaponIcon = Img(weaponFrame, "Icon", null, new Vector2(0.5f, 0.5f), Vector2.zero, new Vector2(34, 34));
        infoWeaponIcon.preserveAspect = true;
        infoWeapon = Text(panel, "Weapon", "", 14, Brown, new Vector2(0, 0), new Vector2(74, 28), new Vector2(140, 24), TextAlignmentOptions.Left);
        infoWeapon.fontStyle = FontStyles.Bold;

        infoCard.SetActive(false);
    }

    private void UpdatePlayerInfo(NetworkClient net)
    {
        if (infoCard.activeSelf)
        {
            // 그 햄스터가 화면에서 사라졌으면 닫는다
            if (net.GetRemotePlayers().ContainsKey(infoTargetId) == false || Input.GetKeyDown(KeyCode.Escape))
            {
                infoCard.SetActive(false);
            }
        }

        if (Input.GetMouseButtonDown(1) == false || HouseBuildMode.IsActive)
        {
            return;
        }

        if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject())
        {
            return;
        }

        Camera cam = Camera.main;
        if (cam == null)
        {
            return;
        }

        Vector3 mouse = cam.ScreenToWorldPoint(Input.mousePosition);
        uint bestId = 0;
        float best = InfoPickRadius * InfoPickRadius;

        foreach (var pair in net.GetRemotePlayers())
        {
            if (pair.Value == null)
            {
                continue;
            }

            // 햄스터 그림의 가운데가 발보다 조금 위라 0.3 올려서 잰다
            Vector3 p = pair.Value.transform.position;
            float dx = p.x - mouse.x;
            float dy = p.y + 0.3f - mouse.y;
            float d = dx * dx + dy * dy;

            if (d <= best)
            {
                best = d;
                bestId = pair.Key;
            }
        }

        if (bestId != 0)
        {
            net.SendPlayerInfoRequest(bestId);
        }
    }

    private void ShowPlayerInfo(NetworkClient.PlayerInfo info)
    {
        if (info.found == false)
        {
            ShowToast("너무 멀어서 볼 수 없어요", Color.white);
            return;
        }

        infoTargetId = info.id;
        infoName.text = info.name;
        infoLevel.text = "Lv. " + info.level;
        infoPower.text = "전투력 " + info.combatPower.ToString("N0");
        infoStats.text = "공격 " + info.attack + "   방어 " + info.defense + "\n최대 체력 " + info.maxHp;

        ItemTable.Def weapon = info.weaponItemId != 0 ? ItemTable.Find(info.weaponItemId) : null;
        infoWeaponIcon.sprite = weapon != null ? weapon.Icon : null;
        infoWeaponIcon.enabled = weapon != null;
        infoWeapon.text = weapon == null ? "맨손" : (info.weaponEnhance > 0 ? "+" + info.weaponEnhance + " " : "") + weapon.Name;

        infoCard.SetActive(true);
    }
}
