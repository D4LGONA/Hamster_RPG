using UnityEngine;

// 휘두른 부채꼴을 반투명하게 칠해 보여주는 연출. 프리팹 없이 런타임에 메시를 만든다.
// 서버가 판정하는 범위(Protocol.AttackRange / AttackArcHalfDegrees)와 같은 모양이라,
// 어디까지 닿는지 눈으로 확인할 수 있다.
//
// 클릭한 자리에 고정된다. 서버도 그 순간의 위치로 판정했으므로, 휘두르는 동안 걸어도
// 부채꼴이 따라가면 오히려 실제 판정과 다른 그림이 된다.
[RequireComponent(typeof(MeshFilter), typeof(MeshRenderer))]
public class AttackSwing : MonoBehaviour
{
    private const float Lifetime = 0.22f;
    // 이 비율까지 부채꼴이 한쪽 끝에서 반대쪽으로 열리고, 남은 구간에 사라진다.
    private const float SweepRatio = 0.6f;
    private const int ArcSegments = 24;
    private const float MaxAlpha = 0.35f;

    private static Material sharedSwingMaterial;

    private readonly Color baseColor = new Color(1.0f, 0.95f, 0.6f);

    private Mesh mesh;

    private float startDegrees;
    private float totalDegrees;
    private float elapsed;

    public static void Spawn(Vector3 origin, Vector2 direction)
    {
        GameObject obj = new GameObject("AttackSwing");
        obj.transform.position = new Vector3(origin.x, origin.y, 0.0f);

        AttackSwing swing = obj.AddComponent<AttackSwing>();
        swing.Setup(direction);
    }

    private void Setup(Vector2 direction)
    {
        // 휘두른 방향이 부채꼴의 한가운데. 거기서 좌우로 반각씩 벌린다.
        float centerDegrees = Mathf.Atan2(direction.y, direction.x) * Mathf.Rad2Deg;

        startDegrees = centerDegrees - Protocol.AttackArcHalfDegrees;
        totalDegrees = Protocol.AttackArcHalfDegrees * 2.0f;

        mesh = new Mesh();
        mesh.MarkDynamic();   // 매 프레임 다시 만든다

        GetComponent<MeshFilter>().mesh = mesh;

        MeshRenderer meshRenderer = GetComponent<MeshRenderer>();
        meshRenderer.sharedMaterial = GetSwingMaterial();
        meshRenderer.sortingOrder = 500;   // 캐릭터 스프라이트(0) 위에

        Rebuild(0.0f, MaxAlpha);
    }

    private void Update()
    {
        elapsed += Time.deltaTime;

        float t = Mathf.Clamp01(elapsed / Lifetime);

        float sweep = Mathf.Clamp01(t / SweepRatio);

        // 열리는 동안은 진하게, 다 열린 뒤부터 사라진다.
        float alpha = t <= SweepRatio
            ? MaxAlpha
            : Mathf.Lerp(MaxAlpha, 0.0f, (t - SweepRatio) / (1.0f - SweepRatio));

        Rebuild(sweep, alpha);

        if (t >= 1.0f)
        {
            Destroy(gameObject);
        }
    }

    // 삼각형 부채꼴. 가운데 점 하나 + 호 위의 점들을 이어 붙인다.
    private void Rebuild(float sweep, float alpha)
    {
        // 0도짜리 부채꼴은 그릴 게 없으므로 최소한은 열어둔다.
        sweep = Mathf.Max(sweep, 0.06f);

        int segments = Mathf.Max(1, Mathf.CeilToInt(ArcSegments * sweep));
        float arcDegrees = totalDegrees * sweep;
        float stepDegrees = arcDegrees / segments;

        Vector3[] vertices = new Vector3[segments + 2];
        Color[] colors = new Color[segments + 2];
        int[] triangles = new int[segments * 3];

        Color centerColor = baseColor;
        centerColor.a = alpha * 0.35f;   // 손잡이 쪽은 옅게

        Color rimColor = baseColor;
        rimColor.a = alpha;              // 칼끝 쪽이 진하게

        vertices[0] = Vector3.zero;      // 로컬 원점 = 휘두른 사람 자리
        colors[0] = centerColor;

        for (int i = 0; i <= segments; ++i)
        {
            float radians = (startDegrees + stepDegrees * i) * Mathf.Deg2Rad;

            vertices[i + 1] = new Vector3(Mathf.Cos(radians), Mathf.Sin(radians), 0.0f)
                              * Protocol.AttackRange;
            colors[i + 1] = rimColor;
        }

        for (int i = 0; i < segments; ++i)
        {
            triangles[i * 3 + 0] = 0;
            triangles[i * 3 + 1] = i + 1;
            triangles[i * 3 + 2] = i + 2;
        }

        // 정점 수가 프레임마다 바뀌므로, 새 배열을 넣기 전에 비워야 인덱스 범위 오류가 안 난다.
        mesh.Clear();
        mesh.vertices = vertices;
        mesh.colors = colors;
        mesh.triangles = triangles;
    }

    private void OnDestroy()
    {
        if (mesh != null)
        {
            Destroy(mesh);   // 인스턴스마다 새로 만든 메시라 직접 치운다
        }
    }

    // Sprites/Default는 정점 색과 알파 블렌딩을 그대로 받아준다. 텍스처가 없으면 흰색으로 친다.
    // 색은 정점에 넣으므로 머티리얼은 하나를 모두가 공유해도 된다.
    private static Material GetSwingMaterial()
    {
        if (sharedSwingMaterial != null)
        {
            return sharedSwingMaterial;
        }

        sharedSwingMaterial = new Material(Shader.Find("Sprites/Default"));

        return sharedSwingMaterial;
    }
}
