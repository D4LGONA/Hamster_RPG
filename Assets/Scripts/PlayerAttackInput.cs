using UnityEngine;
using UnityEngine.EventSystems;

// 공격 입력. 좌클릭하면 마우스 쪽으로 휘두른다.
// 대상을 고르지 않고 방향만 보내며, 그 부채꼴에 누가 걸렸는지는 서버가 판정한다.
// 여기서 쿨타임을 재는 건 판정이 아니라 쓸데없는 패킷을 줄이려는 것이다.
public class PlayerAttackInput : MonoBehaviour
{
    private float lastAttackTime = -999.0f;

    private void Update()
    {
        if (NetworkClient.Instance == null || NetworkClient.Instance.IsLoggedIn() == false)
        {
            return;
        }

        if (Input.GetMouseButtonDown(0) == false)
        {
            return;
        }

        // 채팅창 같은 UI를 누른 클릭은 공격이 아니다.
        if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject())
        {
            return;
        }

        if (ChatUI.Instance != null && ChatUI.Instance.IsChatting())
        {
            return;
        }

        if (Time.time - lastAttackTime < Protocol.AttackCooldown)
        {
            return;
        }

        Vector2 direction;

        if (TryGetMouseDirection(out direction) == false)
        {
            return;
        }

        lastAttackTime = Time.time;

        NetworkClient.Instance.SendAttack(direction);

        // 빗나가도 휘둘렀다는 게 보여야 한다. 맞았는지는 SC_DAMAGE가 와야 알 수 있다.
        AttackSwing.Spawn(transform.position, direction);
    }

    private bool TryGetMouseDirection(out Vector2 direction)
    {
        direction = Vector2.zero;

        Camera camera = Camera.main;

        if (camera == null)
        {
            return false;
        }

        // 2D 직교 카메라라 z는 의미가 없다. x, y만 본다.
        Vector3 worldPoint = camera.ScreenToWorldPoint(Input.mousePosition);
        Vector2 delta = new Vector2(worldPoint.x - transform.position.x,
                                    worldPoint.y - transform.position.y);

        if (delta.sqrMagnitude < 0.0001f)
        {
            return false;   // 제자리를 찍었다
        }

        direction = delta.normalized;
        return true;
    }
}
