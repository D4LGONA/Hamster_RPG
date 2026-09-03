using UnityEngine;

// 서버가 소유하는 몬스터를 클라에서 표시. 위치는 서버가 준 값으로 보간(RemotePlayer와 동일 방식).
// NetworkClient가 SC_SPAWN(몬스터)로 생성하고, SC_MOVEOBJECT로 SetTargetPosition을 호출한다.
// 2026-07-18: 전투 추가. hp/데미지/죽음은 전부 서버가 정하고 여기서는 받은 값을 보여주기만 한다.
public class MonsterEntity : MonoBehaviour
{
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

    private MonsterHealthBar healthBar;

    private int hp;
    private int maxHp;
    private bool isDead;
    private float deathElapsed;

    public bool IsDead() { return isDead; }
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

    private void Update()
    {
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

    // 체력바는 처음 맞았을 때 만든다. 안 그러면 멀쩡한 몬스터 머리 위에까지 바가 떠서 지저분하다.
    private void EnsureHealthBar()
    {
        if (healthBar != null)
        {
            return;
        }

        healthBar = gameObject.AddComponent<MonsterHealthBar>();
        healthBar.Build(spriteRenderer);
    }
}
