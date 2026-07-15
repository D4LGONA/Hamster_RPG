using UnityEngine;

// 서버 권위 이동: 내 위치가 아니라 '입력 방향'을 서버로 보낸다.
// TCP라 순서가 보장되므로, 입력이 바뀔 때만(누르기 시작/방향 전환/뗌) 보내면 충분하다.
[RequireComponent(typeof(PlayerMovement))]
public class LocalPlayerNetworkSender : MonoBehaviour
{
    [Header("Network Send")]
    // 방향이 이만큼이라도 바뀌면 새로 전송.
    [SerializeField] private float inputChangeThreshold = 0.05f;

    private PlayerMovement playerMovement;
    private Vector2 lastSentInput = new Vector2(float.NaN, float.NaN);

    private void Awake()
    {
        playerMovement = GetComponent<PlayerMovement>();
    }

    private void Update()
    {
        if (NetworkClient.Instance == null)
        {
            return;
        }

        if (NetworkClient.Instance.IsConnected() == false)
        {
            return;
        }

        if (NetworkClient.Instance.IsLoggedIn() == false)
        {
            return;
        }

        Vector2 input = playerMovement.CurrentInput;

        // 이전에 보낸 입력과 거의 같으면 스킵(불필요한 패킷 억제).
        float delta = Vector2.Distance(input, lastSentInput);

        if (delta < inputChangeThreshold)
        {
            return;
        }

        NetworkClient.Instance.SendInput(input);
        lastSentInput = input;
    }
}
