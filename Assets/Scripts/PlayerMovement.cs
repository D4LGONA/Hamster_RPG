using UnityEngine;

[RequireComponent(typeof(Animator))]
public class PlayerMovement : MonoBehaviour
{
    private Animator animator;

    private Vector2 moveInput;
    private Vector2 lastMoveDir = Vector2.down;

    private string currentAnimation;

    // 서버 권위 이동: 이 값은 LocalPlayerNetworkSender가 읽어 서버로 보낸다.
    // 실제 위치 이동은 서버가 계산하고, 클라는 애니메이션만 로컬에서 처리한다.
    public Vector2 CurrentInput { get { return moveInput; } }

    private void Awake()
    {
        animator = GetComponent<Animator>();
    }

    private void Update()
    {
        // 채팅 중엔 입력을 0으로 (서버에 정지 전송 + 애니메이션 idle).
        if (ChatUI.Instance != null && ChatUI.Instance.IsChatting())
        {
            moveInput = Vector2.zero;
            UpdateAnimation();
            return;
        }

        ReadInput();
        UpdateAnimation();
    }

    private void ReadInput()
    {
        moveInput.x = Input.GetAxisRaw("Horizontal");
        moveInput.y = Input.GetAxisRaw("Vertical");

        moveInput = moveInput.normalized;

        if (moveInput.sqrMagnitude > 0.01f)
        {
            lastMoveDir = moveInput;
        }
    }

    private void UpdateAnimation()
    {
        if (moveInput.sqrMagnitude > 0.01f)
        {
            PlayWalkAnimation(moveInput);
        }
        else
        {
            PlayIdleAnimation(lastMoveDir);
        }
    }

    private void PlayWalkAnimation(Vector2 dir)
    {
        if (Mathf.Abs(dir.x) > Mathf.Abs(dir.y))
        {
            if (dir.x > 0)
                PlayAnimation("Player_Walk_Right");
            else
                PlayAnimation("Player_Walk_Left");
        }
        else
        {
            if (dir.y > 0)
                PlayAnimation("Player_Walk_Back");
            else
                PlayAnimation("Player_Walk_Down");
        }
    }

    private void PlayIdleAnimation(Vector2 dir)
    {
        if (Mathf.Abs(dir.x) > Mathf.Abs(dir.y))
        {
            if (dir.x > 0)
                PlayAnimation("Player_Idle_Right");
            else
                PlayAnimation("Player_Idle_Left");
        }
        else
        {
            if (dir.y > 0)
                PlayAnimation("Player_Idle_Up");
            else
                PlayAnimation("Player_Idle_Down");
        }
    }

    private void PlayAnimation(string animationName)
    {
        if (currentAnimation == animationName)
            return;

        currentAnimation = animationName;
        animator.Play(animationName);
    }
}