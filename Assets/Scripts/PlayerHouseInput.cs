using UnityEngine;

// T키. 무엇을 할지는 서버가 정한다(집 없으면 설치 / 있으면 귀환 / 집 안이면 퇴장).
// 집을 가졌는지 아는 쪽이 서버라 클라가 미리 판단할 수 없다.
public class PlayerHouseInput : MonoBehaviour
{
    // 씬을 갈아끼우는 동안 연타로 여러 번 나가는 걸 막는다.
    private const float ActionCooldown = 0.5f;

    private float lastActionTime = -999.0f;

    private void Update()
    {
        if (NetworkClient.Instance == null || NetworkClient.Instance.IsLoggedIn() == false)
        {
            return;
        }

        if (Input.GetKeyDown(KeyCode.T) == false)
        {
            return;
        }

        // 채팅 중엔 T가 글자 입력이다.
        if (ChatUI.Instance != null && ChatUI.Instance.IsChatting())
        {
            return;
        }

        if (Time.time - lastActionTime < ActionCooldown)
        {
            return;
        }

        lastActionTime = Time.time;
        NetworkClient.Instance.SendHouseAction();
    }
}
