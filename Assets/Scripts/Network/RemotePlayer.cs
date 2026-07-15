// 2026-06-16: ����Ʈ �÷��̾ ĳ���� �̸� �߰�
using TMPro;
using UnityEngine;

public class RemotePlayer : MonoBehaviour
{
    [Header("Name UI")]
    [SerializeField] private TMP_Text characterNameText;

    [Header("Interpolation")]
    // 서버 브로드캐스트 주기(15Hz ≈ 0.066초)보다 살짝 길게. 다음 패킷 올 때까지 멈추지 않고 등속으로 채운다.
    [SerializeField] private float interpDuration = 0.08f;
    // 이 거리 이상 튀면 보간 대신 즉시 순간이동(텔레포트/디싱크 보정).
    [SerializeField] private float snapDistance = 5.0f;

    private Vector3 fromPosition;   // 보간 시작점(패킷 받은 순간의 실제 위치)
    private Vector3 toPosition;     // 보간 목표점(서버가 보낸 위치)
    private float elapsed;
    private bool hasTarget;

    private void Awake()
    {
        if (characterNameText == null)
        {
            characterNameText = GetComponentInChildren<TMP_Text>();
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

        // 너무 크게 튀면 보간 없이 즉시 이동(입장 직후/텔레포트/큰 지연 보정).
        if (Vector2.Distance(transform.position, next) >= snapDistance)
        {
            transform.position = next;
            fromPosition = next;
            toPosition = next;
            elapsed = 0.0f;
            hasTarget = false;
            return;
        }

        // 지금 있는 위치에서 새 목표까지 다시 보간 시작 (이전 보간 도중에 와도 이어짐).
        fromPosition = transform.position;
        toPosition = next;
        elapsed = 0.0f;
        hasTarget = true;
    }

    public void SetName(string characterName)
    {
        if (characterNameText == null)
        {
            return;
        }

        characterNameText.text = characterName;
    }
}