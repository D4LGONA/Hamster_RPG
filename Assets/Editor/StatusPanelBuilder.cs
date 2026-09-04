using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// 상태창과 체력바를 실제 오브젝트/프리팹으로 만들어 주는 에디터 도구.
//
// 왜 이렇게 하나:
// 씬/프리팹 파일(.unity, .prefab)을 손으로 쓰는 것보다 안전하다. UI 컴포넌트는 직렬화 필드가
// 많고 폰트 · 머티리얼 GUID까지 물려 있어서, 직접 쓰면 글자가 안 보이거나 파일이 안 열리는
// 식으로 조용히 깨진다. 여기서는 유니티 자신의 API로 만들게 하고 저장도 유니티가 한다.
//
// 만드는 코드는 StatusPanelUI.Build() / WorldHealthBar.Build() 안에만 있다.
// 여기서 다시 짜면 실행 중에 만드는 것과 에디터로 굽는 것이 어긋난다.
public static class StatusPanelBuilder
{
    private const string PrefabFolder = "Assets/Prefabs";

    private const string StatusPrefabMenu = "Tools/Hamster/상태창 프리팹 만들기";
    private const string StatusSceneMenu = "Tools/Hamster/상태창 씬에 만들기";
    private const string HealthBarMenu = "Tools/Hamster/체력바 프리팹 만들기";

    private const string StatusPrefabPath = PrefabFolder + "/StatusPanelUI.prefab";
    private const string HealthBarPrefabPath = PrefabFolder + "/HealthBar.prefab";

    // ─────────────────────────────────────────────
    //  상태창 프리팹
    // ─────────────────────────────────────────────
    [MenuItem(StatusPrefabMenu)]
    private static void BuildStatusPrefab()
    {
        if (ConfirmOverwrite(StatusPrefabPath) == false)
        {
            return;
        }

        GameObject temp = new GameObject("StatusPanelUI");
        temp.AddComponent<StatusPanelUI>().Build();

        GameObject prefab = SaveAsPrefab(temp, StatusPrefabPath, "상태창");

        if (prefab == null)
        {
            return;
        }

        Debug.Log("[상태창] " + StatusPrefabPath + " 를 만들었습니다. " +
                  "Hierarchy의 NetworkClient를 고른 뒤 Status Panel Prefab 칸에 끌어놓으세요.");
    }

    // ─────────────────────────────────────────────
    //  체력바 프리팹
    // ─────────────────────────────────────────────
    // 하나만 만들어서 LocalPlayer 와 Enemy 프리팹에 각각 자식으로 끌어넣으면 된다.
    // 중첩 프리팹이라 나중에 원본 하나만 고치면 둘 다 따라 바뀐다.
    [MenuItem(HealthBarMenu)]
    private static void BuildHealthBarPrefab()
    {
        if (ConfirmOverwrite(HealthBarPrefabPath) == false)
        {
            return;
        }

        GameObject temp = new GameObject("HealthBar");

        // 주인 스프라이트가 없으므로 null. 높이는 프리팹에서 직접 옮기면 된다.
        temp.AddComponent<WorldHealthBar>().Build(null, 0.0f);

        GameObject prefab = SaveAsPrefab(temp, HealthBarPrefabPath, "체력바");

        if (prefab == null)
        {
            return;
        }

        Debug.Log("[체력바] " + HealthBarPrefabPath + " 를 만들었습니다. " +
                  "LocalPlayer 와 Enemy 프리팹에 각각 자식으로 끌어넣으세요. " +
                  "LocalPlayer 에는 LocalPlayerHealthBar 스크립트도 같이 붙이면 됩니다.");
    }

    // ─────────────────────────────────────────────
    //  상태창을 지금 씬에 직접 넣기
    // ─────────────────────────────────────────────
    [MenuItem(StatusSceneMenu)]
    private static void BuildStatusIntoScene()
    {
        StatusPanelUI existing = Object.FindObjectOfType<StatusPanelUI>();

        if (existing != null)
        {
            bool replace = EditorUtility.DisplayDialog(
                "상태창이 이미 있습니다",
                "지금 씬에 상태창이 이미 들어 있습니다. 지우고 새로 만들까요? " +
                "인스펙터에서 바꿔둔 색이나 위치는 사라집니다.",
                "새로 만들기", "취소");

            if (replace == false)
            {
                return;
            }

            Undo.DestroyObjectImmediate(existing.gameObject);
        }

        GameObject root = new GameObject("StatusPanelUI");
        Undo.RegisterCreatedObjectUndo(root, "상태창 만들기");
        root.AddComponent<StatusPanelUI>().Build();

        Selection.activeGameObject = root;
        EditorSceneManager.MarkSceneDirty(root.scene);

        Debug.Log("[상태창] " + root.scene.name + " 씬에 만들었습니다. Ctrl+S로 저장하세요.");
    }

    [MenuItem(StatusSceneMenu, true)]
    private static bool BuildStatusIntoSceneValidate()
    {
        return UnityEngine.SceneManagement.SceneManager.GetActiveScene().isLoaded;
    }

    // ─────────────────────────────────────────────
    //  공통
    // ─────────────────────────────────────────────
    private static bool ConfirmOverwrite(string path)
    {
        if (AssetDatabase.LoadAssetAtPath<GameObject>(path) == null)
        {
            return true;
        }

        return EditorUtility.DisplayDialog(
            "프리팹이 이미 있습니다",
            path + " 가 이미 있습니다. 덮어쓸까요? 그동안 바꾼 색이나 크기는 사라집니다.",
            "덮어쓰기", "취소");
    }

    // 프리팹으로 구우려면 일단 씬에 만들어야 한다. 굽고 나서 지운다.
    private static GameObject SaveAsPrefab(GameObject temp, string path, string label)
    {
        if (AssetDatabase.IsValidFolder(PrefabFolder) == false)
        {
            AssetDatabase.CreateFolder("Assets", "Prefabs");
        }

        GameObject prefab = PrefabUtility.SaveAsPrefabAsset(temp, path);

        Object.DestroyImmediate(temp);

        if (prefab == null)
        {
            Debug.LogError("[" + label + "] 프리팹 저장에 실패했습니다: " + path);
            return null;
        }

        AssetDatabase.SaveAssets();

        Selection.activeObject = prefab;
        EditorGUIUtility.PingObject(prefab);

        return prefab;
    }
}
