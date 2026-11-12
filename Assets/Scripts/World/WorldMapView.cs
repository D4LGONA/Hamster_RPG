// 2026-11-12: 무한 월드 그리기.
// 카메라 주변 청크만 그린다. 바닥은 Tilemap 한 장에 청크 단위로 채우고/비우고,
// 나무/덤불/돌은 청크마다 부모 오브젝트 하나에 모아 두었다가 통째로 지운다.
// 무엇을 어디에 그릴지는 WorldGen 공식이 정한다. 서버도 같은 공식으로 충돌을 판정한다.
//
// 씬에 따로 배치하지 않아도 된다. GameScene이 로드되면 스스로 만들어진다.

using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.Tilemaps;

public class WorldMapView : MonoBehaviour
{
    private const string TargetScene = "GameScene";

    // 카메라 청크 기준 반경. 화면이 10x10유닛이고 청크가 16유닛이라 1이면 충분하다.
    private const int LoadRadius = 1;
    // 지우는 반경은 더 넓게. 경계에서 왔다 갔다 할 때 매번 지웠다 그리지 않게.
    private const int UnloadRadius = 3;

    // Tileset_Field.png 칸 번호 (왼쪽 위부터 가로로 0, 1, 2 ...)
    private const int TileRock = 6;
    private const int TileBush = 7;

    private static readonly Color OutsideColor = new Color(0.30f, 0.52f, 0.27f);

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    private static void Hook()
    {
        SceneManager.sceneLoaded -= OnSceneLoaded;
        SceneManager.sceneLoaded += OnSceneLoaded;
        OnSceneLoaded(SceneManager.GetActiveScene(), LoadSceneMode.Single);   // 에디터에서 GameScene부터 재생한 경우
    }

    private static void OnSceneLoaded(Scene scene, LoadSceneMode mode)
    {
        if (scene.name != TargetScene || FindObjectOfType<WorldMapView>() != null)
        {
            return;
        }

        var go = new GameObject("WorldMap");
        SceneManager.MoveGameObjectToScene(go, scene);
        go.AddComponent<WorldMapView>();
    }

    private Tilemap ground;
    private Transform objectsRoot;
    private Sprite[] tileSprites;
    private Sprite treeSprite;
    private Tile[] groundTiles;

    private readonly Dictionary<Vector2Int, GameObject> loaded = new Dictionary<Vector2Int, GameObject>();
    private readonly List<WorldGen.ObstacleSpot> obstacleBuffer = new List<WorldGen.ObstacleSpot>();
    private readonly List<Vector2Int> unloadBuffer = new List<Vector2Int>();
    private Vector2Int lastCenter = new Vector2Int(int.MinValue, int.MinValue);
    private bool ready;

    private void Awake()
    {
        Sprite[] sliced = Resources.LoadAll<Sprite>("Map/Tileset_Field");
        treeSprite = Resources.Load<Sprite>("Map/Tree");

        if (sliced == null || sliced.Length == 0)
        {
            Debug.LogWarning("[MAP] Resources/Map/Tileset_Field.png 를 찾지 못했습니다.");
            return;
        }

        tileSprites = SortByIndex(sliced, 16);

        // 바닥 타일 4종: 풀1, 풀2, 꽃1, 꽃2 (WorldGen.GroundAt 값과 같은 순서)
        groundTiles = new Tile[4];
        for (int i = 0; i < groundTiles.Length; ++i)
        {
            groundTiles[i] = ScriptableObject.CreateInstance<Tile>();
            groundTiles[i].sprite = tileSprites[i];
        }

        var gridGo = new GameObject("Grid", typeof(Grid));
        gridGo.transform.SetParent(transform, false);
        var groundGo = new GameObject("Ground", typeof(Tilemap), typeof(TilemapRenderer));
        groundGo.transform.SetParent(gridGo.transform, false);
        ground = groundGo.GetComponent<Tilemap>();
        groundGo.GetComponent<TilemapRenderer>().sortingOrder = -100;   // 캐릭터보다 항상 뒤

        objectsRoot = new GameObject("Objects").transform;
        objectsRoot.SetParent(transform, false);

        ready = true;
    }

    private void LateUpdate()
    {
        Camera cam = Camera.main;

        if (ready == false || cam == null)
        {
            return;
        }

        cam.backgroundColor = OutsideColor;

        Vector3 p = cam.transform.position;
        var center = new Vector2Int(WorldGen.ChunkOf(WorldGen.TileOf(p.x)), WorldGen.ChunkOf(WorldGen.TileOf(p.y)));

        if (center == lastCenter)
        {
            return;   // 같은 청크 안에서 움직이는 동안은 할 일이 없다
        }

        lastCenter = center;

        for (int dy = -LoadRadius; dy <= LoadRadius; ++dy)
        {
            for (int dx = -LoadRadius; dx <= LoadRadius; ++dx)
            {
                var chunk = new Vector2Int(center.x + dx, center.y + dy);

                if (loaded.ContainsKey(chunk) == false)
                {
                    LoadChunk(chunk);
                }
            }
        }

        unloadBuffer.Clear();
        foreach (Vector2Int chunk in loaded.Keys)
        {
            if (Mathf.Abs(chunk.x - center.x) > UnloadRadius || Mathf.Abs(chunk.y - center.y) > UnloadRadius)
            {
                unloadBuffer.Add(chunk);
            }
        }

        foreach (Vector2Int chunk in unloadBuffer)
        {
            UnloadChunk(chunk);
        }
    }

    private void LoadChunk(Vector2Int chunk)
    {
        int size = WorldGen.ChunkSize;
        int baseX = chunk.x * size;
        int baseY = chunk.y * size;

        // 바닥: 청크 한 장을 한 번에 채운다. SetTile을 256번 부르는 것보다 훨씬 싸다.
        var tiles = new TileBase[size * size];
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                tiles[y * size + x] = groundTiles[WorldGen.GroundAt(baseX + x, baseY + y)];
            }
        }
        ground.SetTilesBlock(new BoundsInt(baseX, baseY, 0, size, size, 1), tiles);

        // 장애물
        var parent = new GameObject($"Chunk_{chunk.x}_{chunk.y}").transform;
        parent.SetParent(objectsRoot, false);

        obstacleBuffer.Clear();
        WorldGen.ObstaclesInChunk(chunk.x, chunk.y, obstacleBuffer);

        foreach (WorldGen.ObstacleSpot spot in obstacleBuffer)
        {
            float cx = spot.TileX + 0.5f;
            float cy = spot.TileY + 0.5f;

            switch (spot.Kind)
            {
                // 나무 그림은 2x3칸이지만 막히는 건 밑동 한 칸. 피벗(밑동)을 그 칸에 둔다.
                case WorldGen.Obstacle.Tree: PlaceSprite(parent, treeSprite, cx, spot.TileY + 0.3f); break;
                case WorldGen.Obstacle.Bush: PlaceSprite(parent, tileSprites[TileBush], cx, cy); break;
                case WorldGen.Obstacle.Rock: PlaceSprite(parent, tileSprites[TileRock], cx, cy); break;
            }
        }

        loaded.Add(chunk, parent.gameObject);
    }

    private void UnloadChunk(Vector2Int chunk)
    {
        int size = WorldGen.ChunkSize;
        ground.SetTilesBlock(new BoundsInt(chunk.x * size, chunk.y * size, 0, size, size, 1), new TileBase[size * size]);

        if (loaded.TryGetValue(chunk, out GameObject go))
        {
            Destroy(go);
        }

        loaded.Remove(chunk);
    }

    private static void PlaceSprite(Transform parent, Sprite sprite, float x, float y)
    {
        if (sprite == null)
        {
            return;
        }

        var go = new GameObject(sprite.name);
        go.transform.SetParent(parent, false);
        go.transform.position = new Vector3(x, y, 0.0f);
        go.AddComponent<SpriteRenderer>().sprite = sprite;   // 순서 0: 캐릭터와 Y축으로 앞뒤가 정해진다
    }

    // 잘린 스프라이트를 이름 끝 번호(Tileset_Field_7)로 정렬한다. LoadAll 순서는 보장되지 않는다.
    public static Sprite[] SortByIndex(Sprite[] sliced, int capacity)
    {
        var result = new Sprite[capacity];

        foreach (Sprite s in sliced)
        {
            int underscore = s.name.LastIndexOf('_');

            if (underscore >= 0 && int.TryParse(s.name.Substring(underscore + 1), out int index) && index < capacity)
            {
                result[index] = s;
            }
        }

        return result;
    }
}
