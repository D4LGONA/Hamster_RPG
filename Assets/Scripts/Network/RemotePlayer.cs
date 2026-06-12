// 2026-06-12: RemotePlayer.cs 파일 제작. 다른 클라이언트의 위치를 보간해서 보여준다.

using UnityEngine;

public class RemotePlayer : MonoBehaviour
{
    [SerializeField] private float interpolationSpeed = 12.0f;

    private Vector2 targetPosition;

    private void Awake()
    {
        targetPosition = transform.position;
    }

    private void Update()
    {
        Vector3 current = transform.position;
        Vector3 target = new Vector3(targetPosition.x, targetPosition.y, current.z);

        transform.position = Vector3.Lerp(current, target, interpolationSpeed * Time.deltaTime);
    }

    public void SetTargetPosition(Vector2 position)
    {
        targetPosition = position;
    }
}