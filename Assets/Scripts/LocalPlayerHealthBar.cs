using UnityEngine;

// 내 캐릭터 머리 위 체력바.
//
// LocalPlayer 프리팹에 WorldHealthBar 를 넣고, 이 스크립트를 같이 붙이면 된다.
// 체력바 자체(스프라이트 두 장)는 프리팹에서 만들고 여기서는 채워지는 폭만 갱신한다.
//
// 값은 서버가 SC_STAT_UPDATE 로 보내준 hp / maxHp 다. 클라가 깎지 않는다.
[RequireComponent(typeof(LocalPlayerInterpolator))]
public class LocalPlayerHealthBar : MonoBehaviour
{
    [SerializeField] private WorldHealthBar healthBar;

    // 체력이 가득일 때는 숨긴다. 켜두면 늘 머리 위에 바가 떠 있어 지저분하다.
    [SerializeField] private bool hideWhenFull = true;

    private int lastStatVersion = -1;

    private void Awake()
    {
        if (healthBar == null)
        {
            healthBar = GetComponentInChildren<WorldHealthBar>(true);
        }
    }

    private void Update()
    {
        if (healthBar == null || NetworkClient.Instance == null)
        {
            return;
        }

        if (NetworkClient.Instance.HasMyStats() == false)
        {
            return;
        }

        // 스탯이 바뀐 때에만 손댄다. 매 프레임 돌릴 이유가 없다.
        int version = NetworkClient.Instance.GetStatVersion();

        if (version == lastStatVersion)
        {
            return;
        }

        lastStatVersion = version;

        Protocol.PlayerStatsData stats = NetworkClient.Instance.GetMyStats();

        float ratio = stats.maxHp > 0 ? (float)stats.hp / stats.maxHp : 0.0f;

        healthBar.SetRatio(ratio);

        if (hideWhenFull)
        {
            healthBar.gameObject.SetActive(ratio < 0.999f);
        }
    }
}
