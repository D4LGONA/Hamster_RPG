using TMPro;
using UnityEngine;
using UnityEngine.UI;

// 캐릭터 상태창. Tab 또는 C로 연다.
//
// 화면은 에디터에서 만든다. 이 스크립트는 값만 채우고 열고 닫기만 한다.
// 기존 Canvas 아래에 패널을 만들고, 이 컴포넌트를 붙인 뒤 아래 칸들을 꽂아 주면 된다.
//
// 값은 전부 서버가 SC_STAT_UPDATE로 보내준 것이다. 클라가 계산하는 값은 없다.
public class StatusPanelUI : MonoBehaviour
{
    [Header("열고 닫을 대상")]
    // 비워두면 이 오브젝트 자신을 켜고 끈다.
    [SerializeField] private GameObject panel;

    [Header("값 표시")]
    [SerializeField] private TextMeshProUGUI nameText;
    [SerializeField] private TextMeshProUGUI levelText;
    [SerializeField] private TextMeshProUGUI expText;
    [SerializeField] private TextMeshProUGUI hpText;
    [SerializeField] private TextMeshProUGUI attackText;
    [SerializeField] private TextMeshProUGUI defenseText;
    [SerializeField] private TextMeshProUGUI moveSpeedText;
    [SerializeField] private TextMeshProUGUI combatPowerText;
    [SerializeField] private TextMeshProUGUI goldText;

    [Header("막대 (Image Type을 Filled / Horizontal 로)")]
    [SerializeField] private Image hpFill;
    [SerializeField] private Image expFill;

    [Header("설정")]
    [SerializeField] private KeyCode toggleKey = KeyCode.Tab;
    [SerializeField] private KeyCode altToggleKey = KeyCode.C;
    [SerializeField] private bool openOnStart;

    private int lastStatVersion = -1;

    private GameObject Target
    {
        get { return panel != null ? panel : gameObject; }
    }

    private void Start()
    {
        Target.SetActive(openOnStart);
    }

    private void Update()
    {
        if (NetworkClient.Instance == null || NetworkClient.Instance.IsLoggedIn() == false)
        {
            return;
        }

        // 채팅 중엔 Tab / C 가 글자 입력이다.
        bool chatting = ChatUI.Instance != null && ChatUI.Instance.IsChatting();

        if (chatting == false && (Input.GetKeyDown(toggleKey) || Input.GetKeyDown(altToggleKey)))
        {
            Target.SetActive(!Target.activeSelf);
        }

        if (Target.activeSelf == false)
        {
            return;
        }

        // 값이 바뀐 때에만 다시 그린다. 매 프레임 문자열을 만들면 쓸데없이 가비지가 쌓인다.
        int version = NetworkClient.Instance.GetStatVersion();

        if (version != lastStatVersion)
        {
            lastStatVersion = version;
            Refresh();
        }
    }

    private void Refresh()
    {
        SetText(nameText, NetworkClient.Instance.GetPlayerName());

        if (NetworkClient.Instance.HasMyStats() == false)
        {
            return;   // 아직 서버에서 스탯이 안 왔다
        }

        Protocol.PlayerStatsData stats = NetworkClient.Instance.GetMyStats();

        SetText(levelText, stats.level.ToString());
        SetText(expText, stats.exp + " / " + stats.requiredExp);
        SetText(hpText, stats.hp + " / " + stats.maxHp);
        SetText(attackText, stats.attack.ToString());
        SetText(defenseText, stats.defense.ToString());
        SetText(moveSpeedText, stats.moveSpeed.ToString("0.0"));
        SetText(combatPowerText, stats.combatPower.ToString());
        SetText(goldText, stats.gold.ToString());

        SetFill(expFill, stats.requiredExp > 0 ? (float)stats.exp / stats.requiredExp : 0.0f);
        SetFill(hpFill, stats.maxHp > 0 ? (float)stats.hp / stats.maxHp : 0.0f);
    }

    // 안 꽂아둔 칸은 그냥 건너뛴다. 필요한 것만 꽂아 써도 된다.
    private static void SetText(TextMeshProUGUI target, string value)
    {
        if (target != null)
        {
            target.text = value;
        }
    }

    private static void SetFill(Image target, float ratio)
    {
        if (target != null)
        {
            target.fillAmount = Mathf.Clamp01(ratio);
        }
    }
}
