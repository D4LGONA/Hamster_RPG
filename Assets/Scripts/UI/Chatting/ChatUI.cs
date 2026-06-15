using System.Collections;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

public class ChatUI : MonoBehaviour
{
    public static ChatUI Instance { get; private set; }

    [Header("Root")]
    [SerializeField] private GameObject chatPreview;
    [SerializeField] private GameObject chatWindow;

    [Header("Preview")]
    [SerializeField] private TMP_Text previewText;

    [Header("Chat Window")]
    [SerializeField] private ScrollRect chatScrollRect;
    [SerializeField] private Transform contentRoot;
    [SerializeField] private TMP_InputField chatInput;
    [SerializeField] private ChatMessageItem messageItemPrefab;

    [Header("Option")]
    [SerializeField] private int maxMessageCount = 80;

    private bool isChatOpen;

    private void Awake()
    {
        Instance = this;
    }

    private void Start()
    {
        CloseChat();

        if (previewText != null)
        {
            previewText.text = "";
        }

        AddSystemMessage("채팅창 준비 완료");
    }

    private void Update()
    {
        if (Input.GetKeyDown(KeyCode.Return))
        {
            if (isChatOpen)
            {
                TrySendMessage();
            }
            else
            {
                OpenChat();
            }
        }

        if (Input.GetKeyDown(KeyCode.Escape))
        {
            if (isChatOpen)
            {
                CloseChat();
            }
        }
    }

    private void OnDestroy()
    {
        if (Instance == this)
        {
            Instance = null;
        }
    }

    public bool IsChatting()
    {
        return isChatOpen;
    }

    private void OpenChat()
    {
        isChatOpen = true;

        if (chatPreview != null)
        {
            chatPreview.SetActive(false);
        }

        if (chatWindow != null)
        {
            chatWindow.SetActive(true);
        }

        if (chatInput != null)
        {
            chatInput.text = "";
            chatInput.ActivateInputField();
            chatInput.Select();
        }

        StartCoroutine(ScrollToBottom());
    }

    private void CloseChat()
    {
        isChatOpen = false;

        if (chatPreview != null)
        {
            chatPreview.SetActive(true);
        }

        if (chatWindow != null)
        {
            chatWindow.SetActive(false);
        }

        if (chatInput != null)
        {
            chatInput.text = "";
            chatInput.DeactivateInputField();
        }
    }

    private void TrySendMessage()
    {
        if (chatInput == null)
        {
            return;
        }

        string message = chatInput.text;

        if (string.IsNullOrWhiteSpace(message))
        {
            CloseChat();
            return;
        }

        message = message.Trim();
        chatInput.text = "";

        AddMessage("Me", message);

        if (NetworkClient.Instance != null)
        {
            NetworkClient.Instance.SendChat(message);
        }

        CloseChat();
    }

    public void AddSystemMessage(string message)
    {
        AddMessage("System", message);
    }

    public void AddMessage(string senderName, string message)
    {
        if (contentRoot == null || messageItemPrefab == null)
        {
            return;
        }

        ChatMessageItem item = Instantiate(messageItemPrefab, contentRoot);
        item.gameObject.SetActive(true);
        item.SetMessage(senderName, message);

        if (previewText != null)
        {
            previewText.text = "[" + senderName + "] " + message;
        }

        while (contentRoot.childCount > maxMessageCount)
        {
            Destroy(contentRoot.GetChild(0).gameObject);
        }

        StartCoroutine(ScrollToBottom());
    }

    private IEnumerator ScrollToBottom()
    {
        yield return null;

        if (chatScrollRect != null)
        {
            chatScrollRect.verticalNormalizedPosition = 0.0f;
        }
    }
}