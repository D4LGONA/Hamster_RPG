// 2026-11-13: 집 안 가구 그리기. NetworkClient가 들고 있는 가구 목록(서버가 보낸 그대로)을 보고
// 바뀔 때마다 스프라이트를 맞춘다. 요청 후 응답을 기다렸다 그리는 게 아니라 "서버가 보내 준 대로 그린다".

using System.Collections.Generic;
using UnityEngine;

public class HouseFurnitureView : MonoBehaviour
{
    private readonly Dictionary<uint, SpriteRenderer> views = new Dictionary<uint, SpriteRenderer>();
    private readonly List<uint> removeBuffer = new List<uint>();
    private int lastVersion = -1;

    public static HouseFurnitureView Instance { get; private set; }

    private void Awake() { Instance = this; }
    private void OnDestroy() { if (Instance == this) Instance = null; }

    // 꾸미기 모드가 "마우스 아래 가구"를 찾을 때. 러그는 다른 가구 밑에 깔려 있으니 마지막에 고른다.
    public bool TryPick(Vector2 worldPos, out uint furnitureId)
    {
        furnitureId = 0;
        NetworkClient net = NetworkClient.Instance;
        if (net == null) return false;

        bool found = false;
        bool foundIsRug = true;

        foreach (var pair in net.GetFurniture())
        {
            Vector2 half = FurnitureTable.HalfExtents(pair.Value.itemType, pair.Value.rotation);
            if (Mathf.Abs(worldPos.x - pair.Value.x) > half.x || Mathf.Abs(worldPos.y - pair.Value.y) > half.y) continue;

            bool isRug = pair.Value.itemType == FurnitureTable.Rug;
            if (found == false || (foundIsRug && isRug == false))
            {
                furnitureId = pair.Key;
                found = true;
                foundIsRug = isRug;
            }
        }

        return found;
    }

    public void SetHidden(uint furnitureId, bool hidden)
    {
        if (views.TryGetValue(furnitureId, out SpriteRenderer r) && r != null) r.enabled = !hidden;
    }

    private void Update()
    {
        NetworkClient net = NetworkClient.Instance;
        if (net == null || net.GetFurnitureVersion() == lastVersion) return;

        lastVersion = net.GetFurnitureVersion();
        IReadOnlyDictionary<uint, NetworkClient.FurnitureData> data = net.GetFurniture();

        removeBuffer.Clear();
        foreach (uint id in views.Keys)
        {
            if (data.ContainsKey(id) == false) removeBuffer.Add(id);
        }
        foreach (uint id in removeBuffer)
        {
            if (views[id] != null) Destroy(views[id].gameObject);
            views.Remove(id);
        }

        foreach (var pair in data)
        {
            if (views.TryGetValue(pair.Key, out SpriteRenderer r) == false || r == null)
            {
                var go = new GameObject("Furniture_" + pair.Key);
                go.transform.SetParent(transform, false);
                r = go.AddComponent<SpriteRenderer>();
                views[pair.Key] = r;
            }

            Apply(r, pair.Value);
        }
    }

    public static void Apply(SpriteRenderer r, NetworkClient.FurnitureData d)
    {
        r.sprite = FurnitureTable.GetSprite(d.itemType);
        r.transform.position = new Vector3(d.x, d.y, 0.0f);
        r.transform.rotation = Quaternion.Euler(0.0f, 0.0f, d.rotation * 90.0f);
        // 러그는 바닥에 깔리므로 언제나 캐릭터/가구 뒤. 나머지는 캐릭터와 Y축으로 앞뒤가 정해진다.
        r.sortingOrder = d.itemType == FurnitureTable.Rug ? -15 : 0;
    }
}
