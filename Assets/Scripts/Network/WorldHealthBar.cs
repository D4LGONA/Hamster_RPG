using UnityEngine;
using UnityEngine.UI;

// 체력바. 캐릭터와 몬스터가 같이 쓴다.
//
// 화면은 전부 프리팹에서 만든다. 이 스크립트는 채워지는 양만 조절한다.
// 위치 · 크기 · 색은 프리팹에서 정한 대로 간다.
//
// 숫자를 어디서 받느냐만 두 가지다.
//   몬스터 : MonsterEntity 가 SC_DAMAGE 를 받아 SetRatio 를 불러준다
//   내 캐릭터 : Track My Stats 를 켜면 스스로 서버가 준 hp/maxHp 를 읽는다
public class WorldHealthBar : MonoBehaviour
{
    [Header("채워지는 이미지")]
    // Image Type 은 Filled / Horizontal 이어야 한다. 안 맞춰뒀으면 실행할 때 맞춰준다.
    [SerializeField] private Image fillImage;

    [Header("동작")]
    // 내 캐릭터용. 켜면 서버가 보내주는 내 스탯을 스스로 읽는다.
    // 몬스터는 꺼둔다(MonsterEntity 가 먹여준다).
    [SerializeField] private bool trackMyStats;

    // 체력이 가득이면 숨긴다. 늘 머리 위에 바가 떠 있으면 지저분하다.
    [SerializeField] private bool hideWhenFull = true;

    private int lastStatVersion = -1;

    // 숨길 때 오브젝트를 끄면 Update 가 멈춰서 다시 켜지지 않는다.
    // 그래서 오브젝트는 켜둔 채로 그림(Image/Text)만 끈다.
    private Graphic[] graphics;

    private void Awake()
    {
        graphics = GetComponentsInChildren<Graphic>(true);

        SetupFillImage();

        if (hideWhenFull)
        {
            SetVisible(false);   // 맞기 전까지는 숨겨둔다
        }
    }

    private void Update()
    {
        if (trackMyStats == false)
        {
            return;
        }

        if (NetworkClient.Instance == null || NetworkClient.Instance.HasMyStats() == false)
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

        SetRatio(stats.maxHp > 0 ? (float)stats.hp / stats.maxHp : 0.0f);
    }

    public void SetRatio(float newRatio)
    {
        float ratio = Mathf.Clamp01(newRatio);

        if (fillImage != null)
        {
            SetupFillImage();
            fillImage.fillAmount = ratio;
        }

        if (hideWhenFull)
        {
            SetVisible(ratio < 0.999f);
        }
    }

    // 죽었을 때처럼 바로 감추고 싶을 때. MonsterEntity 가 부른다.
    public void Hide()
    {
        SetVisible(false);
    }

    private void SetVisible(bool visible)
    {
        if (graphics == null)
        {
            return;
        }

        for (int i = 0; i < graphics.Length; ++i)
        {
            if (graphics[i] != null)
            {
                graphics[i].enabled = visible;
            }
        }
    }

    // Filled / Horizontal 이 아니면 fillAmount 가 먹지 않는다.
    // 인스펙터에서 안 바꿔놨어도 동작하게 여기서 맞춰준다.
    private void SetupFillImage()
    {
        if (fillImage == null)
        {
            return;
        }

        if (fillImage.type != Image.Type.Filled)
        {
            fillImage.type = Image.Type.Filled;
        }

        fillImage.fillMethod = Image.FillMethod.Horizontal;
        fillImage.fillOrigin = (int)Image.OriginHorizontal.Left;
    }
}
