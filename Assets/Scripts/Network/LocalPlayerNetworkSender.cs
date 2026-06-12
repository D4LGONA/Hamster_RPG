using UnityEngine;

public class LocalPlayerNetworkSender : MonoBehaviour
{
    [Header("Network Send")]
    [SerializeField] private float sendInterval = 0.5f;
    [SerializeField] private float minMoveDistance = 0.01f;

    private float sendTimer;
    private Vector2 lastSentPosition;

    private void Start()
    {
        lastSentPosition = transform.position;
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

        sendTimer += Time.deltaTime;

        if (sendTimer < sendInterval)
        {
            return;
        }

        sendTimer = 0.0f;

        Vector2 currentPosition = transform.position;

        float moveDistance = Vector2.Distance(lastSentPosition, currentPosition);

        if (moveDistance < minMoveDistance)
        {
            return;
        }

        NetworkClient.Instance.SendMove(currentPosition);
        lastSentPosition = currentPosition;
    }
}