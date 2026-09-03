using TMPro;
using UnityEngine;

// 월드에 놓인 집. 서버가 소유하고 클라는 보여주기만 한다.
//
// 콜라이더는 붙이지 않는다. 충돌은 서버가 게임 틱에서 AABB로 판정하고(IsBlockedByHouse),
// 클라는 서버가 준 좌표를 따라갈 뿐이라 유니티 물리가 끼어들 자리가 없다.
//
// NetworkClient에 housePrefab을 꽂아두면 그걸 쓰고, 비어 있으면 코드로 네모를 그린다.
public class HouseEntity : MonoBehaviour
{
    private static Sprite squareSprite;

    public static HouseEntity Create(GameObject prefab, uint id, string ownerName, Vector2 position)
    {
        Vector3 spawnPosition = new Vector3(position.x, position.y, 0.0f);

        GameObject obj;

        if (prefab != null)
        {
            obj = Instantiate(prefab, spawnPosition, Quaternion.identity);
        }
        else
        {
            obj = new GameObject();
            obj.transform.position = spawnPosition;
        }

        obj.name = "House_" + id + "_" + ownerName;

        HouseEntity house = obj.GetComponent<HouseEntity>();

        if (house == null)
        {
            house = obj.AddComponent<HouseEntity>();
        }

        house.Setup(ownerName, prefab == null);

        return house;
    }

    private void Setup(string ownerName, bool buildDefaultBody)
    {
        SpriteRenderer body;

        if (buildDefaultBody)
        {
            // 프리팹이 없을 때 쓰는 임시 네모.
            body = gameObject.AddComponent<SpriteRenderer>();
            body.sprite = GetSquareSprite();
            body.color = new Color(0.72f, 0.51f, 0.35f);
            body.sortingOrder = -10;   // 캐릭터(0)보다 뒤

            // 서버 판정과 같은 크기로. 보이는 네모가 곧 못 지나가는 범위다.
            transform.localScale = new Vector3(Protocol.HouseSize, Protocol.HouseSize, 1.0f);
        }
        else
        {
            body = GetComponentInChildren<SpriteRenderer>();
            WarnIfSizeMismatch(body);
        }

        CreateNameLabel(ownerName, body);
    }

    // 보이는 크기와 서버가 막는 크기가 다르면 "왜 여기서 막히지"가 된다. 미리 알려준다.
    private void WarnIfSizeMismatch(SpriteRenderer body)
    {
        if (body == null)
        {
            Debug.LogWarning("[HOUSE] 프리팹에 SpriteRenderer가 없습니다. 집이 안 보입니다.");
            return;
        }

        float width = body.bounds.size.x;
        float height = body.bounds.size.y;

        if (Mathf.Abs(width - Protocol.HouseSize) > 0.3f || Mathf.Abs(height - Protocol.HouseSize) > 0.3f)
        {
            Debug.LogWarning(string.Format(
                "[HOUSE] 프리팹 크기가 {0:0.00} x {1:0.00} 인데 서버는 {2:0.00} x {2:0.00} 로 막습니다. " +
                "보이는 것과 실제 벽이 어긋납니다. 스프라이트 Pixels Per Unit이나 Transform Scale로 맞추세요.",
                width, height, Protocol.HouseSize));
        }
    }

    private void CreateNameLabel(string ownerName, SpriteRenderer body)
    {
        GameObject labelObj = new GameObject("OwnerName");

        // 집 위쪽에 띄운다. 프리팹 크기가 제각각일 수 있으니 스프라이트 실제 높이를 기준으로 잡는다.
        float top = body != null ? body.bounds.max.y : transform.position.y + Protocol.HouseSize * 0.5f;
        labelObj.transform.position = new Vector3(transform.position.x, top + 0.35f, 0.0f);

        // worldPositionStays = true 로 붙여야 부모가 스케일돼 있어도 글자가 같이 늘어나지 않는다.
        labelObj.transform.SetParent(transform, true);

        TextMeshPro label = labelObj.AddComponent<TextMeshPro>();
        label.text = string.IsNullOrEmpty(ownerName) ? "빈 집" : ownerName + "의 집";
        label.fontSize = 2.4f;
        label.alignment = TextAlignmentOptions.Center;
        label.color = new Color(1.0f, 0.95f, 0.85f);
        label.enableWordWrapping = false;
        label.sortingOrder = 100;
        label.rectTransform.sizeDelta = new Vector2(6.0f, 1.0f);
    }

    private static Sprite GetSquareSprite()
    {
        if (squareSprite != null)
        {
            return squareSprite;
        }

        Texture2D texture = new Texture2D(1, 1);
        texture.SetPixel(0, 0, Color.white);
        texture.Apply();

        // pixelsPerUnit을 1로 두면 스케일 값이 곧 월드 크기가 된다.
        squareSprite = Sprite.Create(texture, new Rect(0.0f, 0.0f, 1.0f, 1.0f), new Vector2(0.5f, 0.5f), 1.0f);

        return squareSprite;
    }
}
