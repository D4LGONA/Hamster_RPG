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

    [Header("Animation")]
    [SerializeField] private Animator animator;
    // 이 프레임 이동량이 이보다 크면 '걷는 중'으로 본다(제곱거리).
    [SerializeField] private float moveThresholdSqr = 0.0000001f;

    private Vector3 fromPosition;   // 보간 시작점(패킷 받은 순간의 실제 위치)
    private Vector3 toPosition;     // 보간 목표점(서버가 보낸 위치)
    private float elapsed;
    private bool hasTarget;

    private Vector2 lastMoveDir = Vector2.down;   // 정지 시 바라볼 방향
    private string currentAnimation;

    private void Awake()
    {
        if (characterNameText == null)
        {
            characterNameText = GetComponentInChildren<TMP_Text>();
        }

        if (animator == null)
        {
            animator = GetComponent<Animator>();
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
        Vector3 before = transform.position;

        if (hasTarget)
        {
            elapsed += Time.deltaTime;

            float t = Mathf.Clamp01(elapsed / interpDuration);
            transform.position = Vector3.Lerp(fromPosition, toPosition, t);
        }

        // 이번 프레임에 실제로 움직인 양으로 애니메이션 결정(서버가 방향을 안 보내도 이동으로 유추).
        Vector2 delta = transform.position - before;
        UpdateAnimation(delta);
    }

    private void UpdateAnimation(Vector2 delta)
    {
        if (delta.sqrMagnitude > moveThresholdSqr)
        {
            lastMoveDir = delta.normalized;
            PlayWalkAnimation(lastMoveDir);
        }
        else
        {
            PlayIdleAnimation(lastMoveDir);
        }
    }

    // 아래 매핑은 PlayerMovement(로컬)와 동일하게 맞춘다.
    private void PlayWalkAnimation(Vector2 dir)
    {
        if (Mathf.Abs(dir.x) > Mathf.Abs(dir.y))
        {
            if (dir.x > 0) PlayAnimation("Player_Walk_Right");
            else PlayAnimation("Player_Walk_Left");
        }
        else
        {
            if (dir.y > 0) PlayAnimation("Player_Walk_Back");
            else PlayAnimation("Player_Walk_Down");
        }
    }

    private void PlayIdleAnimation(Vector2 dir)
    {
        if (Mathf.Abs(dir.x) > Mathf.Abs(dir.y))
        {
            if (dir.x > 0) PlayAnimation("Player_Idle_Right");
            else PlayAnimation("Player_Idle_Left");
        }
        else
        {
            if (dir.y > 0) PlayAnimation("Player_Idle_Up");
            else PlayAnimation("Player_Idle_Down");
        }
    }

    private void PlayAnimation(string animationName)
    {
        if (animator == null) return;
        if (currentAnimation == animationName) return;

        currentAnimation = animationName;
        animator.Play(animationName);
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