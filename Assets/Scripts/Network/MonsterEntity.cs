using UnityEngine;

// 서버가 소유하는 몬스터를 클라에서 표시. 위치는 서버가 준 값으로 보간(RemotePlayer와 동일 방식).
// NetworkClient가 SC_SPAWN(몬스터)로 생성하고, SC_MOVEOBJECT로 SetTargetPosition을 호출한다.
public class MonsterEntity : MonoBehaviour
{
    [Header("Interpolation")]
    // 서버 브로드캐스트 주기(15Hz ≈ 0.066초)보다 살짝 길게.
    [SerializeField] private float interpDuration = 0.08f;
    // 이 거리 이상 튀면 보간 대신 즉시 순간이동.
    [SerializeField] private float snapDistance = 5.0f;

    private Vector3 fromPosition;
    private Vector3 toPosition;
    private float elapsed;
    private bool hasTarget;

    private void Start()
    {
        fromPosition = transform.position;
        toPosition = transform.position;
        hasTarget = false;
    }

    private void Update()
    {
        if (hasTarget == false)
        {
            return;
        }

        elapsed += Time.deltaTime;

        float t = Mathf.Clamp01(elapsed / interpDuration);
        transform.position = Vector3.Lerp(fromPosition, toPosition, t);
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
}
