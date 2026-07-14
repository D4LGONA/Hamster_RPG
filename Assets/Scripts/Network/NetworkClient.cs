// 2026-06-12: NetworkClient.cs
// ���� ����, �α���, ��Ŷ �ۼ���, �÷��̾� ����/�̵� ����ȭ ����
// 2026-06-16: SendChat(), StatUpdate �߰�

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Threading;
using UnityEngine;
using UnityEngine.SceneManagement;

public class NetworkClient : MonoBehaviour
{
    public static NetworkClient Instance { get; private set; }

    [Header("Network")]
    [SerializeField] private string serverIp = Protocol.ServerIp;
    [SerializeField] private int serverPort = Protocol.Port;
    [SerializeField] private string gameSceneName = "GameScene";

    [Header("Prefab")]
    [SerializeField] private GameObject localPlayerPrefab;
    [SerializeField] private GameObject remotePlayerPrefab;

    [Header("Debug")]
    [SerializeField] private bool enableNetworkLog = true;

    private Protocol.PlayerStatsData myStats;
    private bool hasMyStats;

    private TcpClient client;
    private NetworkStream stream;
    private Thread receiveThread;

    private volatile bool isConnected;
    private volatile bool isLoggedIn;

    private uint myPlayerId;
    private string playerName = "Hamster";

    private GameObject localPlayerObject;
    private Vector2 loginPosition;

    private readonly object disconnectLock = new object();
    private readonly object sendLock = new object();

    private readonly byte[] receiveTempBuffer = new byte[4096];
    private readonly List<byte> receiveBuffer = new List<byte>();
    private readonly ConcurrentQueue<Action> mainThreadActions = new ConcurrentQueue<Action>();

    private readonly Dictionary<uint, RemotePlayer> remotePlayers = new Dictionary<uint, RemotePlayer>();
    private readonly List<SpawnInfo> pendingSpawns = new List<SpawnInfo>();
    private readonly Dictionary<uint, Vector2> pendingMoves = new Dictionary<uint, Vector2>();

    private struct SpawnInfo
    {
        public uint id;
        public string name;
        public Vector2 position;
    }

    private void Awake()
    {
        Application.runInBackground = true;

        if (Instance != null)
        {
            Destroy(gameObject);
            return;
        }

        Instance = this;
        DontDestroyOnLoad(gameObject);
        SceneManager.sceneLoaded += OnSceneLoaded;
    }

    private void Update()
    {
        while (mainThreadActions.TryDequeue(out Action action)) action.Invoke();
    }

    private void OnDestroy()
    {
        if (Instance != this) return;

        SceneManager.sceneLoaded -= OnSceneLoaded;
        Instance = null;
    }

    private void OnApplicationQuit()
    {
        SendLogout();
        Disconnect();
    }

    public bool IsConnected() { return isConnected; }
    public bool IsLoggedIn() { return isLoggedIn; }
    public uint GetMyPlayerId() { return myPlayerId; }
    public bool HasMyStats() { return hasMyStats; }
    public Protocol.PlayerStatsData GetMyStats() { return myStats; }

    public void ConnectToServer(string inputName)
    {
        if (isConnected)
        {
            Log("[NETWORK] Already connected");
            return;
        }

        playerName = string.IsNullOrWhiteSpace(inputName) ? "Hamster" : inputName.Trim();

        try
        {
            client = new TcpClient();
            client.NoDelay = true;
            client.Connect(serverIp, serverPort);

            stream = client.GetStream();
            isConnected = true;
            isLoggedIn = false;

            receiveThread = new Thread(ReceiveLoop);
            receiveThread.IsBackground = true;
            receiveThread.Start();

            SendLogin();
            Log("[NETWORK] Connected. name = " + playerName);
        }
        catch (Exception e)
        {
            Debug.LogError("[NETWORK] Connect failed: " + e.Message);
            Disconnect();
        }
    }

    public void SendMove(Vector2 position)
    {
        if (isConnected == false || isLoggedIn == false || stream == null)
        {
            Debug.LogWarning("[NETWORK] SendMove blocked. isConnected = " + isConnected + ", isLoggedIn = " + isLoggedIn + ", stream null = " + (stream == null));
            return;
        }

        Log("[NETWORK] SendMove x = " + position.x + ", y = " + position.y);
        SendRaw(Protocol.MakeMovePacket(position.x, position.y));
    }

    public void SendChat(string message)
    {
        if (isConnected == false || isLoggedIn == false || stream == null)
        {
            Debug.LogWarning("[NETWORK] SendChat blocked. message = " + message);
            return;
        }

        if (string.IsNullOrWhiteSpace(message)) return;

        message = message.Trim();
        Log("[NETWORK] SendChat message = " + message);
        SendRaw(Protocol.MakeChatPacket(message));
    }

    private void OnSceneLoaded(Scene scene, LoadSceneMode mode)
    {
        if (scene.name != gameSceneName) return;

        Log("[NETWORK] Game scene loaded");
        SpawnLocalPlayer();
        FlushPendingPackets();
    }

    private void SpawnLocalPlayer()
    {
        if (localPlayerPrefab == null)
        {
            Debug.LogError("[NETWORK] LocalPlayerPrefab is null");
            return;
        }

        if (localPlayerObject != null) Destroy(localPlayerObject);

        localPlayerObject = Instantiate(localPlayerPrefab, new Vector3(loginPosition.x, loginPosition.y, 0.0f), Quaternion.identity);
        localPlayerObject.name = "LocalPlayer_" + myPlayerId;

        // 2026-07-14: 로컬 플레이어는 로그인 후 생성되므로, 시네머신 카메라의 Follow를 이 시점에 연결한다.
        var virtualCamera = FindObjectOfType<Cinemachine.CinemachineVirtualCamera>();
        if (virtualCamera != null)
        {
            virtualCamera.Follow = localPlayerObject.transform;
        }
        else
        {
            Debug.LogWarning("[NETWORK] CinemachineVirtualCamera not found in scene. Camera will not follow the player.");
        }

        Log("[NETWORK] Spawn local player. id = " + myPlayerId + ", x = " + loginPosition.x + ", y = " + loginPosition.y);
    }

    private void SendLogin()
    {
        if (isConnected == false || stream == null) return;

        SendRaw(Protocol.MakeLoginPacket(playerName));
        Log("[NETWORK] SendLogin name = " + playerName);
    }

    private void SendLogout()
    {
        if (isConnected == false || stream == null) return;

        SendRaw(Protocol.MakeLogoutPacket());
        Log("[NETWORK] SendLogout");
    }

    private void SendRaw(byte[] packet)
    {
        if (packet == null || packet.Length <= 0) return;

        lock (sendLock)
        {
            if (stream == null) return;

            try { stream.Write(packet, 0, packet.Length); }
            catch (Exception e)
            {
                Debug.LogError("[NETWORK] Send failed: " + e.Message);
                Disconnect();
            }
        }
    }

    private void ReceiveLoop()
    {
        while (isConnected)
        {
            try
            {
                if (stream == null)
                {
                    Disconnect();
                    break;
                }

                int bytes = stream.Read(receiveTempBuffer, 0, receiveTempBuffer.Length);

                if (bytes <= 0)
                {
                    Disconnect();
                    break;
                }

                for (int i = 0; i < bytes; ++i) receiveBuffer.Add(receiveTempBuffer[i]);
                ProcessReceiveBuffer();
            }
            catch (Exception e)
            {
                mainThreadActions.Enqueue(() => Debug.LogWarning("[NETWORK] Receive stopped: " + e.Message));
                Disconnect();
                break;
            }
        }
    }

    private void ProcessReceiveBuffer()
    {
        while (true)
        {
            if (receiveBuffer.Count < Protocol.HeaderSize) return;

            ushort packetSize = Protocol.ReadPacketSize(receiveBuffer.ToArray());

            if (packetSize < Protocol.HeaderSize)
            {
                Debug.LogError("[NETWORK] Invalid packet size = " + packetSize);
                Disconnect();
                return;
            }

            if (receiveBuffer.Count < packetSize) return;

            byte[] packet = receiveBuffer.GetRange(0, packetSize).ToArray();
            receiveBuffer.RemoveRange(0, packetSize);

            ProcessPacket(packet);
        }
    }

    private void ProcessPacket(byte[] packet)
    {
        Protocol.PacketType packetType = Protocol.ReadPacketType(packet);
        mainThreadActions.Enqueue(() => Log("[NETWORK] Recv packet type = " + packetType + ", size = " + packet.Length));

        switch (packetType)
        {
            case Protocol.PacketType.SC_LOGIN_RESULT: 
                HandleLoginResult(packet); 
                break;
            case Protocol.PacketType.SC_SPAWN: 
                HandleSpawn(packet); 
                break;
            case Protocol.PacketType.SC_DESPAWN: 
                HandleDespawn(packet); 
                break;
            case Protocol.PacketType.SC_MOVEOBJECT: 
                HandleMove(packet); 
                break;
            case Protocol.PacketType.SC_CHAT: 
                HandleChat(packet); 
                break;
            case Protocol.PacketType.SC_STAT_UPDATE: 
                HandleStatUpdate(packet); 
                break;
            default:
                mainThreadActions.Enqueue(() => Debug.LogWarning("[NETWORK] Unknown packet type: " + packetType));
                break;
        }
    }

    private void HandleLoginResult(byte[] packet)
    {
        int offset = Protocol.HeaderSize;

        byte success = Protocol.ReadByte(packet, ref offset);
        uint id = Protocol.ReadUInt(packet, ref offset);
        float x = Protocol.ReadFloat(packet, ref offset);
        float y = Protocol.ReadFloat(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            if (success == 0)
            {
                Debug.LogError("[NETWORK] Login failed");
                return;
            }

            myPlayerId = id;
            isLoggedIn = true;
            loginPosition = new Vector2(x, y);

            Log("[NETWORK] Login success. myPlayerId = " + myPlayerId + ", x = " + x + ", y = " + y);
            SceneManager.LoadScene(gameSceneName);
        });
    }

    private void HandleSpawn(byte[] packet)
    {
        int offset = Protocol.HeaderSize;

        uint id = Protocol.ReadUInt(packet, ref offset);
        string name = Protocol.ReadFixedString(packet, ref offset, Protocol.NameSize);
        float x = Protocol.ReadFloat(packet, ref offset);
        float y = Protocol.ReadFloat(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            if (id == myPlayerId)
            {
                Log("[NETWORK] Ignore my spawn packet. id = " + id);
                return;
            }

            Vector2 position = new Vector2(x, y);
            Log("[NETWORK] Spawn packet. id = " + id + ", name = " + name + ", x = " + x + ", y = " + y);

            if (SceneManager.GetActiveScene().name != gameSceneName)
            {
                AddPendingSpawn(id, name, position);
                Log("[NETWORK] Add pending spawn. id = " + id);
                return;
            }

            SpawnRemotePlayer(id, name, position);
        });
    }

    private void HandleDespawn(byte[] packet)
    {
        int offset = Protocol.HeaderSize;
        uint id = Protocol.ReadUInt(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            RemovePendingSpawn(id);
            if (pendingMoves.ContainsKey(id)) pendingMoves.Remove(id);

            if (remotePlayers.TryGetValue(id, out RemotePlayer remotePlayer))
            {
                Destroy(remotePlayer.gameObject);
                remotePlayers.Remove(id);
            }

            Log("[NETWORK] Despawn playerId = " + id);
        });
    }

    private void HandleMove(byte[] packet)
    {
        int offset = Protocol.HeaderSize;

        uint id = Protocol.ReadUInt(packet, ref offset);
        float x = Protocol.ReadFloat(packet, ref offset);
        float y = Protocol.ReadFloat(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            Log("[NETWORK] Move packet. id = " + id + ", x = " + x + ", y = " + y);

            if (id == myPlayerId)
            {
                Log("[NETWORK] Ignore my move packet. id = " + id);
                return;
            }

            Vector2 position = new Vector2(x, y);

            if (remotePlayers.TryGetValue(id, out RemotePlayer remotePlayer))
            {
                remotePlayer.SetTargetPosition(position);
                Log("[NETWORK] Apply remote move. id = " + id);
                return;
            }

            pendingMoves[id] = position;
            Debug.LogWarning("[NETWORK] Move target not found. Save pending move. id = " + id);
        });
    }

    private void HandleChat(byte[] packet)
    {
        int offset = Protocol.HeaderSize;

        uint id = Protocol.ReadUInt(packet, ref offset);
        string name = Protocol.ReadFixedString(packet, ref offset, Protocol.NameSize);
        string message = Protocol.ReadFixedString(packet, ref offset, Protocol.ChatSize);

        mainThreadActions.Enqueue(() =>
        {
            Log("[NETWORK] Chat packet. id = " + id + ", name = " + name + ", message = " + message);
            if (ChatUI.Instance != null) ChatUI.Instance.AddMessage(name, message);
        });
    }

    private void HandleStatUpdate(byte[] packet)
    {
        int offset = Protocol.HeaderSize;
        Protocol.PlayerStatsData stats = Protocol.ReadPlayerStatsData(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            myStats = stats;
            hasMyStats = true;

            Log("[NETWORK] Stat update. id = " + stats.id +
                ", level = " + stats.level +
                ", exp = " + stats.exp + "/" + stats.requiredExp +
                ", gold = " + stats.gold +
                ", hp = " + stats.hp + "/" + stats.maxHp +
                ", attack = " + stats.attack +
                ", defense = " + stats.defense +
                ", moveSpeed = " + stats.moveSpeed +
                ", combatPower = " + stats.combatPower);
        });
    }

    private void SpawnRemotePlayer(uint id, string name, Vector2 position)
    {
        if (remotePlayers.ContainsKey(id))
        {
            Log("[NETWORK] Remote player already exists. id = " + id);
            return;
        }

        if (remotePlayerPrefab == null)
        {
            Debug.LogError("[NETWORK] RemotePlayerPrefab is null");
            return;
        }

        GameObject obj = Instantiate(remotePlayerPrefab, new Vector3(position.x, position.y, 0.0f), Quaternion.identity);
        obj.name = "RemotePlayer_" + id + "_" + name;

        RemotePlayer remotePlayer = obj.GetComponent<RemotePlayer>();

        if (remotePlayer == null)
        {
            Debug.LogError("[NETWORK] RemotePlayer prefab has no RemotePlayer component. id = " + id);
            Destroy(obj);
            return;
        }

        remotePlayer.SetName(name);
        remotePlayer.SetTargetPosition(position);
        remotePlayers.Add(id, remotePlayer);

        if (pendingMoves.TryGetValue(id, out Vector2 pendingPosition))
        {
            remotePlayer.SetTargetPosition(pendingPosition);
            pendingMoves.Remove(id);
            Log("[NETWORK] Apply pending move. id = " + id + ", x = " + pendingPosition.x + ", y = " + pendingPosition.y);
        }

        Log("[NETWORK] Spawn remote player. id = " + id + ", name = " + name + ", x = " + position.x + ", y = " + position.y);
    }

    private void AddPendingSpawn(uint id, string name, Vector2 position)
    {
        for (int i = 0; i < pendingSpawns.Count; ++i)
        {
            if (pendingSpawns[i].id != id) continue;

            SpawnInfo updatedSpawn = new SpawnInfo();
            updatedSpawn.id = id;
            updatedSpawn.name = name;
            updatedSpawn.position = position;
            pendingSpawns[i] = updatedSpawn;
            return;
        }

        SpawnInfo spawnInfo = new SpawnInfo();
        spawnInfo.id = id;
        spawnInfo.name = name;
        spawnInfo.position = position;
        pendingSpawns.Add(spawnInfo);
    }

    private void RemovePendingSpawn(uint id)
    {
        for (int i = pendingSpawns.Count - 1; i >= 0; --i)
        {
            if (pendingSpawns[i].id == id) pendingSpawns.RemoveAt(i);
        }
    }

    private void FlushPendingPackets()
    {
        Log("[NETWORK] Flush pending packets. spawn count = " + pendingSpawns.Count + ", move count = " + pendingMoves.Count);

        for (int i = 0; i < pendingSpawns.Count; ++i)
        {
            SpawnInfo spawnInfo = pendingSpawns[i];
            SpawnRemotePlayer(spawnInfo.id, spawnInfo.name, spawnInfo.position);
        }

        pendingSpawns.Clear();

        List<uint> appliedMoveIds = new List<uint>();

        foreach (KeyValuePair<uint, Vector2> pair in pendingMoves)
        {
            if (remotePlayers.TryGetValue(pair.Key, out RemotePlayer remotePlayer) == false) continue;

            remotePlayer.SetTargetPosition(pair.Value);
            appliedMoveIds.Add(pair.Key);
            Log("[NETWORK] Flush pending move. id = " + pair.Key + ", x = " + pair.Value.x + ", y = " + pair.Value.y);
        }

        for (int i = 0; i < appliedMoveIds.Count; ++i) pendingMoves.Remove(appliedMoveIds[i]);
    }

    private void Disconnect()
    {
        lock (disconnectLock)
        {
            isConnected = false;
            isLoggedIn = false;

            if (stream != null)
            {
                try { stream.Close(); } catch { }
                stream = null;
            }

            if (client != null)
            {
                try { client.Close(); } catch { }
                client = null;
            }
        }
    }

    private void Log(string message)
    {
        if (enableNetworkLog) Debug.Log(message);
    }
}