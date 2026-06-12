// 2026-06-12: TitleConnectUI.cs 파일 제작. 타이틀 화면에서 이름을 입력받고 서버 접속을 요청한다.

using TMPro;
using UnityEngine;
using UnityEngine.UI;

public class TitleConnectUI : MonoBehaviour
{
    [SerializeField] private TMP_InputField nameInputField;
    [SerializeField] private Button connectButton;

    private void Awake()
    {
        if (connectButton != null)
        {
            connectButton.onClick.AddListener(OnClickConnect);
        }
    }

    private void OnDestroy()
    {
        if (connectButton != null)
        {
            connectButton.onClick.RemoveListener(OnClickConnect);
        }
    }

    private void OnClickConnect()
    {
        if (NetworkClient.Instance == null)
        {
            Debug.LogError("[TITLE] NetworkClient is null");
            return;
        }

        string playerName = "Hamster";

        if (nameInputField != null)
        {
            playerName = nameInputField.text;
        }

        NetworkClient.Instance.ConnectToServer(playerName);

        if (connectButton != null)
        {
            connectButton.interactable = false;
        }
    }
}