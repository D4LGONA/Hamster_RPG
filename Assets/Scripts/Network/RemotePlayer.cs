// 2026-06-16: 리모트 플레이어에 캐릭터 이름 추가
using TMPro;
using UnityEngine;

public class RemotePlayer : MonoBehaviour
{
    [Header("Name UI")]
    [SerializeField] private TMP_Text characterNameText;

    [Header("Interpolation")]
    [SerializeField] private float moveSpeed = 10.0f;
    [SerializeField] private float snapDistance = 2.0f;
    [SerializeField] private float arriveDistance = 0.01f;

    private Vector2 targetPosition;
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
        targetPosition = transform.position;
        hasTarget = false;
    }

    private void Update()
    {
        if (hasTarget == false)
        {
            return;
        }

        Vector2 currentPosition = transform.position;
        float distance = Vector2.Distance(currentPosition, targetPosition);

        if (distance >= snapDistance)
        {
            transform.position = new Vector3(targetPosition.x, targetPosition.y, transform.position.z);
            hasTarget = false;
            return;
        }

        if (distance <= arriveDistance)
        {
            transform.position = new Vector3(targetPosition.x, targetPosition.y, transform.position.z);
            hasTarget = false;
            return;
        }

        Vector2 nextPosition = Vector2.MoveTowards(
            currentPosition,
            targetPosition,
            moveSpeed * Time.deltaTime
        );

        transform.position = new Vector3(nextPosition.x, nextPosition.y, transform.position.z);
    }

    public void SetTargetPosition(Vector2 position)
    {
        targetPosition = position;
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