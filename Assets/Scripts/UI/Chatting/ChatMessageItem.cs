using TMPro;
using UnityEngine;
using UnityEngine.UI;

public class ChatMessageItem : MonoBehaviour
{
    [SerializeField] private TMP_Text messageText;
    [SerializeField] private LayoutElement layoutElement;

    [SerializeField] private float verticalPadding = 6.0f;
    [SerializeField] private float minHeight = 22.0f;

    public void SetMessage(string senderName, string message)
    {
        if (messageText == null)
        {
            return;
        }

        messageText.text = "[" + senderName + "] " + message;

        UpdateHeight();
    }

    private void UpdateHeight()
    {
        if (layoutElement == null || messageText == null)
        {
            return;
        }

        messageText.ForceMeshUpdate();

        float width = messageText.rectTransform.rect.width;

        if (width <= 0.0f)
        {
            width = 280.0f;
        }

        Vector2 preferredValues = messageText.GetPreferredValues(messageText.text, width, 0.0f);

        float preferredHeight = preferredValues.y + verticalPadding;

        if (preferredHeight < minHeight)
        {
            preferredHeight = minHeight;
        }

        layoutElement.preferredHeight = preferredHeight;
    }
}