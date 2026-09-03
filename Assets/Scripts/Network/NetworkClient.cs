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
    [SerializeField] private string houseSceneName = "HouseScene";

    [Header("Prefab")]
    [SerializeField] private GameObject localPlayerPrefab;
    [SerializeField] private GameObject remotePlayerPrefab;
    [SerializeField] private GameObject monsterPrefab;
    // 비워두면 코드가 임시 네모를 그린다. 프리팹을 꽂으면 그걸 쓴다.
    // 콜라이더는 필요 없다. 충돌은 서버가 판정한다.
    [SerializeField] private GameObject housePrefab;

    [Header("Debug")]
    [SerializeField] private bool enableNetworkLog = true;

    private Protocol.PlayerStatsData myStats;
    private bool hasMyStats;
    private int statVersion;

    private TcpClient client;
    private NetworkStream stream;
    private Thread receiveThread;

    private volatile bool isConnected;
    private volatile bool isLoggedIn;

    private uint myPlayerId;
    private string playerName = "Hamster";

    private GameObject localPlayerObject;
    private LocalPlayerInterpolator localInterpolator;   // 서버 권위 이동: 내 위치를 서버 값으로 보간
    private Vector2 loginPosition;

    private readonly object disconnectLock = new object();
    private readonly object sendLock = new object();

    private readonly byte[] receiveTempBuffer = new byte[4096];
    private readonly List<byte> receiveBuffer = new List<byte>();
    private readonly ConcurrentQueue<Action> mainThreadActions = new ConcurrentQueue<Action>();

    private readonly Dictionary<uint, RemotePlayer> remotePlayers = new Dictionary<uint, RemotePlayer>();
    private readonly Dictionary<uint, MonsterEntity> monsters = new Dictionary<uint, MonsterEntity>();
    private readonly Dictionary<uint, HouseEntity> houses = new Dictionary<uint, HouseEntity>();

    // 지금 있는 존. 0이면 월드, 그 외에는 그 집의 id. 서버가 SC_ZONE_CHANGE로 알려준다.
    private uint currentZoneId;
    private Vector2 zoneStartPosition;

    // 씬을 갈아끼우는 중에는 spawn을 바로 만들지 않고 pending에 쌓아둔다.
    // 지금 만들어봐야 씬이 로드되는 순간 다 destroy되기 때문이다.
    private bool sceneReady;
    private readonly List<SpawnInfo> pendingSpawns = new List<SpawnInfo>();
    private readonly Dictionary<uint, Vector2> pendingMoves = new Dictionary<uint, Vector2>();

    private struct SpawnInfo
    {
        public uint id;
        public string name;
        public Vector2 position;
        public byte entityType;
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

    // 지금 존에서 있어야 할 씬. 이 씬이 실제로 올라와 있어야 오브젝트를 만들 수 있다.
    private string ExpectedSceneName()
    {
        return currentZoneId == 0 ? gameSceneName : houseSceneName;
    }

    private bool IsSceneReady()
    {
        return sceneReady && SceneManager.GetActiveScene().name == ExpectedSceneName();
    }

    public bool IsConnected() { return isConnected; }
    public bool IsLoggedIn() { return isLoggedIn; }
    public uint GetMyPlayerId() { return myPlayerId; }
    public bool HasMyStats() { return hasMyStats; }
    public Protocol.PlayerStatsData GetMyStats() { return myStats; }
    public string GetPlayerName() { return playerName; }

    // 스탯이 갱신될 때마다 올라간다. UI가 이 값만 보고 바뀐 때에만 다시 그리면
    // 매 프레임 문자열을 새로 만들지 않아도 된다.
    public int GetStatVersion() { return statVersion; }

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

    public void SendInput(Vector2 direction)
    {
        if (isConnected == false || isLoggedIn == false || stream == null)
        {
            Debug.LogWarning("[NETWORK] SendInput blocked. isConnected = " + isConnected + ", isLoggedIn = " + isLoggedIn + ", stream null = " + (stream == null));
            return;
        }

        Log("[NETWORK] SendInput dx = " + direction.x + ", dy = " + direction.y);
        SendRaw(Protocol.MakeInputPacket(direction.x, direction.y));
    }

    // 공격 요청. 휘두른 방향만 보낸다. 누가 맞았는지 · 데미지 · 죽음은 전부 서버가 정하고
    // SC_DAMAGE / SC_DEATH로 돌아온다.
    public void SendAttack(Vector2 direction)
    {
        if (isConnected == false || isLoggedIn == false || stream == null)
        {
            Debug.LogWarning("[NETWORK] SendAttack blocked.");
            return;
        }

        if (direction.sqrMagnitude < 0.0001f)
        {
            return;   // 방향이 없으면 서버가 어차피 버린다
        }

        direction = direction.normalized;

        Log("[NETWORK] SendAttack dx = " + direction.x + ", dy = " + direction.y);
        SendRaw(Protocol.MakeAttackPacket(direction.x, direction.y));
    }

    // T키. 설치/입장/퇴장 중 무엇이 될지는 서버가 정한다.
    public void SendHouseAction()
    {
        if (isConnected == false || isLoggedIn == false || stream == null)
        {
            Debug.LogWarning("[NETWORK] SendHouseAction blocked.");
            return;
        }

        Log("[NETWORK] SendHouseAction");
        SendRaw(Protocol.MakeHouseActionPacket());
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
        // 지금 존에 맞는 씬이 아니면 무시한다(타이틀 씬 등).
        if (scene.name != ExpectedSceneName()) return;

        Log("[NETWORK] Scene loaded: " + scene.name + ", zone = " + currentZoneId);

        sceneReady = true;

        // 집 안 씬에는 카메라만 있다. 방은 서버가 허용하는 이동 범위에 맞춰 여기서 그린다.
        if (currentZoneId != 0)
        {
            HouseRoom.Build();
        }

        // 상태창. 씬마다 새로 만든다(씬이 바뀌면 이전 것은 파괴되므로).
        StatusPanelUI.Create();

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

        localPlayerObject = Instantiate(localPlayerPrefab,
            new Vector3(zoneStartPosition.x, zoneStartPosition.y, 0.0f), Quaternion.identity);
        localPlayerObject.name = "LocalPlayer_" + myPlayerId;

        // 공격 / 하우징 입력. 프리팹에 붙여도 되고, 안 붙어 있으면 여기서 붙인다.
        if (localPlayerObject.GetComponent<PlayerAttackInput>() == null)
        {
            localPlayerObject.AddComponent<PlayerAttackInput>();
        }

        if (localPlayerObject.GetComponent<PlayerHouseInput>() == null)
        {
            localPlayerObject.AddComponent<PlayerHouseInput>();
        }

        // 서버 권위 이동: 내 위치는 서버가 보내주는 SC_MOVEOBJECT로 갱신된다. 초기 목표는 로그인 위치.
        localInterpolator = localPlayerObject.GetComponent<LocalPlayerInterpolator>();
        if (localInterpolator == null)
        {
            Debug.LogError("[NETWORK] LocalPlayer prefab has no LocalPlayerInterpolator component.");
        }
        else
        {
            localInterpolator.SetTargetPosition(zoneStartPosition);
        }

        // 2026-07-14: 로컬 플레이어는 로그인 후 생성되므로, 시네머신 카메라의 Follow를 이 시점에 연결한다.
        var virtualCamera = FindObjectOfType<Cinemachine.CinemachineVirtualCamera>();
        if (virtualCamera != null)
        {
            virtualCamera.Follow = localPlayerObject.transform;
        }
        else if (currentZoneId == 0)
        {
            // 집 안에서는 방 전체가 한 화면에 들어와서 따라다닐 카메라가 필요 없다.
            // 월드에서 없는 것만 문제다.
            Debug.LogWarning("[NETWORK] CinemachineVirtualCamera not found in scene. Camera will not follow the player.");
        }

        Log("[NETWORK] Spawn local player. id = " + myPlayerId +
            ", x = " + zoneStartPosition.x + ", y = " + zoneStartPosition.y);
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
            case Protocol.PacketType.SC_DAMAGE:
                HandleDamage(packet);
                break;
            case Protocol.PacketType.SC_DEATH:
                HandleDeath(packet);
                break;
            case Protocol.PacketType.SC_ZONE_CHANGE:
                HandleZoneChange(packet);
                break;
            case Protocol.PacketType.SC_HOUSE_RESULT:
                HandleHouseResult(packet);
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

            currentZoneId = 0;
            zoneStartPosition = loginPosition;
            sceneReady = false;

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
        byte entityType = Protocol.ReadByte(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            if (id == myPlayerId)
            {
                Log("[NETWORK] Ignore my spawn packet. id = " + id);
                return;
            }

            Vector2 position = new Vector2(x, y);
            Log("[NETWORK] Spawn packet. id = " + id + ", type = " + entityType + ", x = " + x + ", y = " + y);

            if (IsSceneReady() == false)
            {
                AddPendingSpawn(id, name, position, entityType);
                Log("[NETWORK] Add pending spawn. id = " + id);
                return;
            }

            SpawnEntity(id, name, position, entityType);
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

            if (monsters.TryGetValue(id, out MonsterEntity monster))
            {
                Destroy(monster.gameObject);
                monsters.Remove(id);
            }

            if (houses.TryGetValue(id, out HouseEntity house))
            {
                Destroy(house.gameObject);
                houses.Remove(id);
            }

            Log("[NETWORK] Despawn id = " + id);
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

            Vector2 position = new Vector2(x, y);

            // 서버 권위 이동: 내 위치도 서버가 소유. 내 캐릭터는 로컬 보간기로 따라간다(Option A).
            if (id == myPlayerId)
            {
                if (localInterpolator != null)
                {
                    localInterpolator.SetTargetPosition(position);
                }
                return;
            }

            if (remotePlayers.TryGetValue(id, out RemotePlayer remotePlayer))
            {
                remotePlayer.SetTargetPosition(position);
                Log("[NETWORK] Apply remote move. id = " + id);
                return;
            }

            if (monsters.TryGetValue(id, out MonsterEntity monster))
            {
                monster.SetTargetPosition(position);
                Log("[NETWORK] Apply monster move. id = " + id);
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

    // 누가 누구를 때려서 얼마가 깎였는지. 남은 hp도 같이 오므로 클라는 계산하지 않고 받아 적기만 한다.
    private void HandleDamage(byte[] packet)
    {
        int offset = Protocol.HeaderSize;

        uint attackerId = Protocol.ReadUInt(packet, ref offset);
        uint targetId = Protocol.ReadUInt(packet, ref offset);
        int damage = Protocol.ReadInt(packet, ref offset);
        int targetHp = Protocol.ReadInt(packet, ref offset);
        int targetMaxHp = Protocol.ReadInt(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            Log("[NETWORK] Damage packet. attacker = " + attackerId + ", target = " + targetId +
                ", damage = " + damage + ", hp = " + targetHp + "/" + targetMaxHp);

            if (monsters.TryGetValue(targetId, out MonsterEntity monster))
            {
                monster.OnDamaged(damage, targetHp, targetMaxHp);
                return;
            }

            // 아직 몬스터만 맞는다(서버가 플레이어를 때리지 않음). 플레이어가 대상인 패킷이
            // 오기 시작하면 여기서 받으면 된다.
            Log("[NETWORK] Damage target not found. id = " + targetId);
        });
    }

    // 죽음. 서버는 SC_DESPAWN을 따로 보내지 않고 이것만 보내므로 여기서 정리까지 해야 한다.
    // 10초 뒤 부활하면 같은 id로 SC_SPAWN이 새로 온다.
    private void HandleDeath(byte[] packet)
    {
        int offset = Protocol.HeaderSize;
        uint id = Protocol.ReadUInt(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            Log("[NETWORK] Death packet. id = " + id);

            RemovePendingSpawn(id);
            if (pendingMoves.ContainsKey(id)) pendingMoves.Remove(id);

            if (monsters.TryGetValue(id, out MonsterEntity monster))
            {
                // 사전에서 먼저 빼둔다. 죽는 연출이 끝날 때까지 오브젝트는 남아 있지만,
                // 그 사이 같은 id로 스폰이 와도 "이미 있다"고 무시하면 안 되기 때문이다.
                monsters.Remove(id);
                monster.OnDeath();
            }
        });
    }

    private void HandleStatUpdate(byte[] packet)
    {
        int offset = Protocol.HeaderSize;
        Protocol.PlayerStatsData stats = Protocol.ReadPlayerStatsData(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            // 레벨업은 스탯 갱신으로만 알 수 있다(전용 패킷이 없음). 첫 수신은 비교 대상이 없으니 뺀다.
            bool leveledUp = hasMyStats && stats.level > myStats.level;

            myStats = stats;
            hasMyStats = true;
            statVersion++;

            if (leveledUp && ChatUI.Instance != null)
            {
                ChatUI.Instance.AddSystemMessage("레벨 " + stats.level + " 달성!");
            }

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

    // 존이 바뀌었다. 서버는 이 패킷을 새 존의 spawn보다 먼저 보내주므로,
    // 여기서 씬 로드를 걸어두면 뒤따라 오는 spawn들은 pending으로 쌓였다가 씬이 뜬 뒤 처리된다.
    private void HandleZoneChange(byte[] packet)
    {
        int offset = Protocol.HeaderSize;

        uint zoneId = Protocol.ReadUInt(packet, ref offset);
        float x = Protocol.ReadFloat(packet, ref offset);
        float y = Protocol.ReadFloat(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            Log("[NETWORK] Zone change. zoneId = " + zoneId + ", x = " + x + ", y = " + y);

            currentZoneId = zoneId;
            zoneStartPosition = new Vector2(x, y);
            sceneReady = false;

            ClearAllEntities();

            SceneManager.LoadScene(ExpectedSceneName());
        });
    }

    private void HandleHouseResult(byte[] packet)
    {
        int offset = Protocol.HeaderSize;

        byte success = Protocol.ReadByte(packet, ref offset);
        byte reason = Protocol.ReadByte(packet, ref offset);

        mainThreadActions.Enqueue(() =>
        {
            Log("[NETWORK] House result. success = " + success + ", reason = " + reason);

            if (ChatUI.Instance == null)
            {
                return;
            }

            if (success != 0)
            {
                ChatUI.Instance.AddSystemMessage("집을 지었습니다. T를 누르면 들어갑니다.");
                return;
            }

            // 사유는 서버 protocol.h의 reason 값과 같다.
            if (reason == 1)
            {
                ChatUI.Instance.AddSystemMessage("다른 집과 너무 가깝습니다. 좀 더 떨어진 곳에 지어보세요.");
            }
            else if (reason == 2)
            {
                ChatUI.Instance.AddSystemMessage("여기에는 집을 지을 수 없습니다.");
            }
            else if (reason == 3)
            {
                ChatUI.Instance.AddSystemMessage("몬스터가 사는 자리입니다. 다른 곳에 지어보세요.");
            }
            else
            {
                ChatUI.Instance.AddSystemMessage("집을 짓지 못했습니다.");
            }
        });
    }

    // 씬을 갈아끼우기 직전에 부른다. 씬이 바뀌면 오브젝트는 어차피 파괴되지만,
    // 사전에 죽은 참조가 남아 있으면 다음 존에서 id가 겹칠 때 엉킨다.
    private void ClearAllEntities()
    {
        foreach (KeyValuePair<uint, RemotePlayer> pair in remotePlayers)
        {
            if (pair.Value != null) Destroy(pair.Value.gameObject);
        }

        foreach (KeyValuePair<uint, MonsterEntity> pair in monsters)
        {
            if (pair.Value != null) Destroy(pair.Value.gameObject);
        }

        foreach (KeyValuePair<uint, HouseEntity> pair in houses)
        {
            if (pair.Value != null) Destroy(pair.Value.gameObject);
        }

        remotePlayers.Clear();
        monsters.Clear();
        houses.Clear();
        pendingSpawns.Clear();
        pendingMoves.Clear();
    }

    private void SpawnEntity(uint id, string name, Vector2 position, byte entityType)
    {
        if (entityType == Protocol.EntityMonster)
        {
            SpawnMonster(id, position);
        }
        else if (entityType == Protocol.EntityHouse)
        {
            SpawnHouse(id, name, position);
        }
        else
        {
            SpawnRemotePlayer(id, name, position);
        }
    }

    // 집은 프리팹 없이 런타임에 만든다(지금은 네모 한 장이라 에디터에 둘 것이 없다).
    private void SpawnHouse(uint id, string ownerName, Vector2 position)
    {
        if (houses.ContainsKey(id))
        {
            Log("[NETWORK] House already exists. id = " + id);
            return;
        }

        houses.Add(id, HouseEntity.Create(housePrefab, id, ownerName, position));

        Log("[NETWORK] Spawn house. id = " + id + ", owner = " + ownerName +
            ", x = " + position.x + ", y = " + position.y);
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

    private void SpawnMonster(uint id, Vector2 position)
    {
        if (monsters.ContainsKey(id))
        {
            Log("[NETWORK] Monster already exists. id = " + id);
            return;
        }

        if (monsterPrefab == null)
        {
            Debug.LogError("[NETWORK] monsterPrefab is null");
            return;
        }

        GameObject obj = Instantiate(monsterPrefab, new Vector3(position.x, position.y, 0.0f), Quaternion.identity);
        obj.name = "Monster_" + id;

        MonsterEntity monster = obj.GetComponent<MonsterEntity>();

        if (monster == null)
        {
            Debug.LogError("[NETWORK] Monster prefab has no MonsterEntity component. id = " + id);
            Destroy(obj);
            return;
        }

        monster.SetTargetPosition(position);
        monsters.Add(id, monster);

        if (pendingMoves.TryGetValue(id, out Vector2 pendingPosition))
        {
            monster.SetTargetPosition(pendingPosition);
            pendingMoves.Remove(id);
        }

        Log("[NETWORK] Spawn monster. id = " + id + ", x = " + position.x + ", y = " + position.y);
    }

    private void AddPendingSpawn(uint id, string name, Vector2 position, byte entityType)
    {
        for (int i = 0; i < pendingSpawns.Count; ++i)
        {
            if (pendingSpawns[i].id != id) continue;

            SpawnInfo updatedSpawn = new SpawnInfo();
            updatedSpawn.id = id;
            updatedSpawn.name = name;
            updatedSpawn.position = position;
            updatedSpawn.entityType = entityType;
            pendingSpawns[i] = updatedSpawn;
            return;
        }

        SpawnInfo spawnInfo = new SpawnInfo();
        spawnInfo.id = id;
        spawnInfo.name = name;
        spawnInfo.position = position;
        spawnInfo.entityType = entityType;
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

            SpawnEntity(spawnInfo.id, spawnInfo.name, spawnInfo.position, spawnInfo.entityType);
        }

        pendingSpawns.Clear();

        List<uint> appliedMoveIds = new List<uint>();

        foreach (KeyValuePair<uint, Vector2> pair in pendingMoves)
        {
            if (remotePlayers.TryGetValue(pair.Key, out RemotePlayer remotePlayer))
            {
                remotePlayer.SetTargetPosition(pair.Value);
                appliedMoveIds.Add(pair.Key);
                continue;
            }

            if (monsters.TryGetValue(pair.Key, out MonsterEntity monster))
            {
                monster.SetTargetPosition(pair.Value);
                appliedMoveIds.Add(pair.Key);
            }
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