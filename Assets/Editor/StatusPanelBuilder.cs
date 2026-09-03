using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// 상태창을 지금 열려 있는 씬에 실제 오브젝트로 만들어 넣는다.
//
// 왜 이렇게 하나:
// 씬 파일(.unity)을 손으로 쓰는 것보다 안전하다. UI 컴포넌트는 직렬화 필드가 많고
// 폰트 · 머티리얼 GUID까지 물려 있어서, 직접 쓰면 글자가 안 보이거나 씬이 안 열리는 식으로
// 조용히 깨진다. 여기서는 유니티 자신의 API로 만들게 하고 저장도 유니티가 한다.
//
// 한 번 구워두면 인스펙터에서 색 · 위치 · 폰트를 마음대로 만질 수 있고,
// 실행 중에 코드가 다시 만들지 않는다(StatusPanelUI.Create가 씬에 있는 것을 먼저 찾는다).
public static class StatusPanelBuilder
{
    private const string MenuPath = "Tools/Hamster/상태창 만들기";

    [MenuItem(MenuPath)]
    private static void BuildIntoScene()
    {
        StatusPanelUI existing = Object.FindObjectOfType<StatusPanelUI>();

        if (existing != null)
        {
            bool replace = EditorUtility.DisplayDialog(
                "상태창이 이미 있습니다",
                "지금 씬에 상태창이 이미 들어 있습니다. 지우고 새로 만들까요?\n" +
                "(인스펙터에서 바꿔둔 색이나 위치는 사라집니다)",
                "새로 만들기", "취소");

            if (replace == false)
            {
                return;
            }

            Undo.DestroyObjectImmediate(existing.gameObject);
        }

        GameObject root = new GameObject("StatusPanelUI");
        Undo.RegisterCreatedObjectUndo(root, "상태창 만들기");

        StatusPanelUI ui = root.AddComponent<StatusPanelUI>();

        // 만드는 코드는 StatusPanelUI 안에만 있다. 여기서 다시 짜면 둘이 어긋난다.
        ui.Build();

        Selection.activeGameObject = root;

        EditorSceneManager.MarkSceneDirty(root.scene);

        Debug.Log("[상태창] " + root.scene.name + " 씬에 만들었습니다. " +
                  "Ctrl+S로 저장하세요. 이제 인스펙터에서 직접 수정할 수 있습니다.");
    }

    // 씬이 열려 있지 않으면 메뉴를 회색으로 둔다.
    [MenuItem(MenuPath, true)]
    private static bool BuildIntoSceneValidate()
    {
        return UnityEngine.SceneManagement.SceneManager.GetActiveScene().isLoaded;
    }
}
