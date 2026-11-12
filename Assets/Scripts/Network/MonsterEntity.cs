using UnityEngine;

// 서버가 소유하는 몬스터를 클라에서 표시. 위치는 서버가 준 값으로 보간(RemotePlayer와 동일 방식).
// NetworkClient가 SC_SPAWN(몬스터)로 생성하고, SC_MOVEOBJECT로 SetTargetPosition을 호출한다.
// 2026-07-18: 전투 추가. hp/데미지/죽음은 전부 서버가 정하고 여기서는 받은 값을 보여주기만 한다.
// 2026-11-12: 몬스터 종류(애벌레/무당벌레/고양이 보스)별 외형. 서버가 스폰 패킷 name 칸에 종류를 실어 보낸다.
//             시트는 Resources/Monsters/ 에 있고 4방향 x 4프레임(줄: 정면/오른쪽/왼쪽/뒤). 움직이는 방향으로 걷기 프레임을 돌린다.
public class MonsterEntity : MonoBehaviour
{
    [Header("Walk Animation")]
    [SerializeField] private float walkFps = 8.0f;

    private Sprite[] frames;          // 16장. 없으면(종류 모름) 프리팹 기본 외형 그대로
    private int facingRow;            // 0 정면, 1 오른쪽, 2 왼쪽, 3 뒤
    private float walkClock;
    private float movingUntil;
    [Header("Interpolation")]
    // 서버 브로드캐스트 주기(15Hz ≈ 0.066초)보다 살짝 길게.
    [SerializeField] private float interpDuration = 0.08f;
    // 이 거리 이상 튀면 보간 대신 즉시 순간이동.
    [SerializeField] private float snapDistance = 5.0f;

    [Header("Combat Feedback")]
    // 맞았을 때 잠깐 이 색으로 번쩍인다.
    [SerializeField] private Color hitFlashColor = new Color(1.0f, 0.4f, 0.4f);
    [SerializeField] private float hitFlashDuration = 0.12f;
    // 죽고 나서 사라지기까지. 이 동안은 오브젝트가 남아 있지만 대상으로 잡히지 않는다.
    [SerializeField] private float deathFadeDuration = 0.4f;

    private Vector3 fromPosition;
    private Vector3 toPosition;
    private float elapsed;
    private bool hasTarget;

    private SpriteRenderer spriteRenderer;
    private Color baseColor = Color.white;
    private float flashRemain;

    private WorldHealthBar healthBar;

    private int hp;
    private int maxHp;
    private bool isDead;
    private float deathElapsed;

    public bool IsDead() { return isDead; }
    public string Kind { get; private set; } = "";   // "Caterpillar" / "Ladybug" / "CatBoss" (미니맵 점 색)
    public int GetHp() { return hp; }
    public int GetMaxHp() { return maxHp; }

    private void Awake()
    {
        spriteRenderer = GetComponentInChildren<SpriteRenderer>();

        if (spriteRenderer != null)
        {
            baseColor = spriteRenderer.color;
        }
    }

    private void Start()
    {
        fromPosition = transform.position;
        toPosition = transform.position;
        hasTarget = false;
    }

    // NetworkClient가 스폰 직후 부른다. kind = 서버 GameServer_View.cpp 의 이름("Caterpillar" 등)
    public void SetKind(string kind)
    {
        Kind = kind ?? "";

        // 머리 위 이름표: 프리팹 기본 글자("hello") 대신 몬스터 이름. 모르는 종류면 숨긴다.
        TMPro.TMP_Text label = GetComponentInChildren<TMPro.TMP_Text>(true);
        if (label != null)
        {
            string display = null;
            switch (Kind)
            {
                case "Caterpillar": display = "애벌레"; break;
                case "Ladybug":     display = "무당벌레"; break;
                case "CatBoss":     display = "고양이 대장"; break;
            }

            label.text = display ?? "";
            label.gameObject.SetActive(display != null);

            // 순둥(애벌레)은 흰 이름, 먼저 덤비는 몬스터는 빨간 이름. 보스는 아래에서 금색으로 덮는다.
            label.color = Kind == "Caterpillar" ? Color.white : new Color32(255, 110, 110, 255);

            if (Kind == "CatBoss")
            {
                label.color = new Color32(255, 214, 90, 255);   // 보스는 금색 이름
                // 보스 그림이 두 배라 이름표가 얼굴에 묻힌다. 이름표가 든 캔버스째 올린다.
                Transform labelRoot = label.canvas != null ? label.canvas.transform : label.transform;
                labelRoot.localPosition += new Vector3(0.0f, 1.2f, 0.0f);
            }
        }

        string sheet = null;
        switch (kind)
        {
            case "Caterpillar": sheet = "Monsters/Monster_Caterpillar"; break;
            case "Ladybug":     sheet = "Monsters/Monster_Ladybug"; break;
            case "CatBoss":     sheet = "Monsters/Boss_Cat"; break;
        }

        if (sheet == null || spriteRenderer == null)
        {
            return;
        }

        Sprite[] sliced = Resources.LoadAll<Sprite>(sheet);

        if (sliced == null || sliced.Length < 16)
        {
            Debug.LogWarning("[MONSTER] 시트를 찾지 못했습니다: " + sheet);
            return;
        }

        frames = WorldMapView.SortByIndex(sliced, 16);

        // 프리팹의 햄스터 애니메이터가 매 프레임 스프라이트를 덮어쓰므로 끈다.
        Animator animator = GetComponentInChildren<Animator>();
        if (animator != null)
        {
            animator.enabled = false;
        }

        spriteRenderer.sprite = frames[0];

        // 보스는 그림이 두 배(4유닛)라서 체력바를 머리 위로 올린다.
        if (kind == "CatBoss")
        {
            EnsureHealthBar();
            if (healthBar != null)
            {
                healthBar.transform.localPosition += new Vector3(0.0f, 1.2f, 0.0f);
            }
        }
    }

    private void UpdateWalkAnimation()
    {
        if (frames == null || spriteRenderer == null)
        {
            return;
        }

        int frame = 0;

        if (Time.time < movingUntil)
        {
            walkClock += Time.deltaTime * walkFps;
            frame = (int)walkClock % 4;
        }
        else
        {
            walkClock = 0.0f;
        }

        Sprite next = frames[facingRow * 4 + frame];
        if (next != null)
        {
            spriteRenderer.sprite = next;
        }
    }

    private void Update()
    {
        UpdateWalkAnimation();
        UpdateInterpolation();
        UpdateHitFlash();
        UpdateDeath();
    }

    private void UpdateInterpolation()
    {
        if (hasTarget == false)
        {
            return;
        }

        elapsed += Time.deltaTime;

        float t = Mathf.Clamp01(elapsed / interpDuration);
        transform.position = Vector3.Lerp(fromPosition, toPosition, t);
    }

    private void UpdateHitFlash()
    {
        if (flashRemain <= 0.0f || spriteRenderer == null)
        {
            return;
        }

        flashRemain -= Time.deltaTime;

        if (flashRemain <= 0.0f && isDead == false)
        {
            spriteRenderer.color = baseColor;
        }
    }

    private void UpdateDeath()
    {
        if (isDead == false)
        {
            return;
        }

        deathElapsed += Time.deltaTime;

        float t = Mathf.Clamp01(deathElapsed / deathFadeDuration);

        if (spriteRenderer != null)
        {
            Color color = baseColor;
            color.a = Mathf.Lerp(baseColor.a, 0.0f, t);
            spriteRenderer.color = color;
        }

        if (t >= 1.0f)
        {
            Destroy(gameObject);
        }
    }

    public void SetTargetPosition(Vector2 position)
    {
        Vector3 next = new Vector3(position.x, position.y, transform.position.z);

        // 바라보는 방향: 많이 움직인 축을 따른다. 이동 패킷이 끊기면 잠시 뒤 서 있는 자세로.
        Vector2 delta = (Vector2)(next - transform.position);
        if (delta.sqrMagnitude > 0.0001f)
        {
            if (Mathf.Abs(delta.x) > Mathf.Abs(delta.y))
            {
                facingRow = delta.x > 0.0f ? 1 : 2;
            }
            else
            {
                facingRow = delta.y > 0.0f ? 3 : 0;
            }
            movingUntil = Time.time + 0.2f;
        }

        if (Vector2.Distance(transform.position, next) >= snapDistance)
        {
            transform.position = next;
            fromPosition = next;
            toPosition = next;
            elapsed = 0.0f;
            hasTarget = false;
            return;
        }

        fromPosition = transform.position;
        toPosition = next;
        elapsed = 0.0f;
        hasTarget = true;
    }

    // SC_DAMAGE 수신. 남은 hp가 패킷에 실려오므로 클라가 빼기를 하지 않는다(서버가 유일한 정답).
    public void OnDamaged(int damage, int newHp, int newMaxHp)
    {
        if (isDead)
        {
            return;
        }

        hp = newHp;
        maxHp = newMaxHp;

        if (spriteRenderer != null)
        {
            spriteRenderer.color = hitFlashColor;
            flashRemain = hitFlashDuration;
        }

        EnsureHealthBar();

        if (healthBar != null)
        {
            healthBar.SetRatio(maxHp > 0 ? (float)hp / maxHp : 0.0f);
        }

        DamageNumber.Spawn(transform.position, damage);
    }

    // SC_DEATH 수신. NetworkClient가 사전에서 먼저 빼고 부르므로, 여기서는 연출만 하고 사라지면 된다.
    public void OnDeath()
    {
        if (isDead)
        {
            return;
        }

        isDead = true;
        hp = 0;
        deathElapsed = 0.0f;

        if (healthBar != null)
        {
            healthBar.Hide();
        }
    }

    // 체력바는 프리팹에 넣어둔 것을 쓴다. 없으면 그냥 안 보일 뿐 동작에는 지장 없다.
    private void EnsureHealthBar()
    {
        if (healthBar == null)
        {
            healthBar = GetComponentInChildren<WorldHealthBar>(true);
        }
    }
}
