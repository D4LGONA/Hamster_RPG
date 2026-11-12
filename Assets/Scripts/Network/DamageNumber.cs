using TMPro;
using UnityEngine;

// 맞은 자리에 떠오르는 데미지 숫자. 프리팹 없이 런타임에 만든다.
public class DamageNumber : MonoBehaviour
{
    private const float RiseSpeed = 1.2f;
    private const float Lifetime = 0.7f;
    private const float FontSize = 3.0f;

    private TextMeshPro text;
    private float elapsed;
    private Color baseColor;

    // 2026-11-13: 내가 맞았을 때는 빨간 숫자
    public static void SpawnHurt(Vector3 worldPosition, int damage)
    {
        Spawn(worldPosition, damage);
        if (lastSpawned != null)
        {
            lastSpawned.text.color = new Color(1.0f, 0.35f, 0.35f);
            lastSpawned.baseColor = lastSpawned.text.color;
        }
    }

    private static DamageNumber lastSpawned;

    public static void Spawn(Vector3 worldPosition, int damage)
    {
        GameObject obj = new GameObject("DamageNumber");

        // 겹쳐 뜨면 읽기 어려우니 살짝 흩뿌린다.
        float jitterX = Random.Range(-0.15f, 0.15f);
        obj.transform.position = worldPosition + new Vector3(jitterX, 0.5f, 0.0f);

        DamageNumber damageNumber = obj.AddComponent<DamageNumber>();
        damageNumber.Setup(damage);
        lastSpawned = damageNumber;
    }

    private void Setup(int damage)
    {
        text = gameObject.AddComponent<TextMeshPro>();
        text.text = damage.ToString();
        text.fontSize = FontSize;
        text.alignment = TextAlignmentOptions.Center;
        text.color = new Color(1.0f, 0.85f, 0.3f);

        baseColor = text.color;

        // 스프라이트에 가리지 않게 정렬을 올린다. TMP가 자기 MeshRenderer를 관리하므로
        // 렌더러를 직접 만지지 않고 TMP 쪽 프로퍼티를 쓴다.
        text.sortingOrder = 1000;

        // 폭이 좁으면 숫자가 줄바꿈되면서 잘린다.
        text.enableWordWrapping = false;
        text.rectTransform.sizeDelta = new Vector2(2.0f, 1.0f);
    }

    private void Update()
    {
        elapsed += Time.deltaTime;

        transform.position += Vector3.up * RiseSpeed * Time.deltaTime;

        if (text != null)
        {
            Color color = baseColor;
            color.a = Mathf.Clamp01(1.0f - elapsed / Lifetime);
            text.color = color;
        }

        if (elapsed >= Lifetime)
        {
            Destroy(gameObject);
        }
    }
}
