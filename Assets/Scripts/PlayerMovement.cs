using UnityEngine;

[RequireComponent(typeof(Rigidbody2D))]
[RequireComponent(typeof(Animator))]
public class PlayerMovement : MonoBehaviour
{
    [Header("Movement")]
    [SerializeField] private float moveSpeed = 3.0f;

    private Rigidbody2D rb;
    private Animator animator;

    private Vector2 moveInput;
    private Vector2 lastMoveDir = Vector2.down;

    private string currentAnimation;

    private void Awake()
    {
        rb = GetComponent<Rigidbody2D>();
        animator = GetComponent<Animator>();
    }

    private void Update()
    {
        if (ChatUI.Instance != null && ChatUI.Instance.IsChatting())
        {
            return;
        }

        ReadInput();
        UpdateAnimation();
    }

    private void FixedUpdate()
    {
        Move();
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

    private void Move()
    {
        Vector2 nextPosition = rb.position + moveInput * moveSpeed * Time.fixedDeltaTime;
        rb.MovePosition(nextPosition);
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