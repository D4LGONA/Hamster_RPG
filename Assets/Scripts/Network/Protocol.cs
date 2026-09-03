// 2026-06-11: Protocol.cs ���� ����. C++ ���� protocol.h�� ������ ��Ŷ ��Ģ�� ����Ѵ�.
// 2026-06-16: CS_CHAT, SC_CHAT �߰�, ���ڿ� ���ڵ� UTF-8 ����

using System;
using System.Collections.Generic;
using System.Text;

public static class Protocol
{
    public const string ServerIp = "127.0.0.1";
    public const int Port = 4000;

    public const int NameSize = 20;
    public const int HeaderSize = 4;
    public const int ChatSize = 256;

    // SC_SPAWN의 엔티티 종류 (서버 protocol.h와 일치).
    public const byte EntityPlayer = 0;
    public const byte EntityMonster = 1;

    // 전투 규칙. 서버 GameServer.h의 값과 같다.
    // 판정은 전부 서버가 하고, 클라는 쿨타임 동안 패킷을 안 보내는 것과
    // 휘두른 범위를 눈에 보이게 그리는 데에만 쓴다.
    public const float AttackRange = 2.5f;
    public const float AttackCooldown = 0.8f;
    public const float AttackArcHalfDegrees = 50.0f;

    private static readonly Encoding PacketEncoding = new UTF8Encoding(false);

    public enum PacketType : ushort
    {
        // Client -> Server
        CS_LOGIN = 1,
        CS_INPUT = 2,     // 서버 권위 이동: 위치가 아니라 '입력 방향'을 보낸다.
        CS_LOGOUT = 3,
        CS_CHAT = 4,

        // Server -> Client
        SC_LOGIN_RESULT = 5,
        SC_SPAWN = 6,
        SC_DESPAWN = 7,
        SC_MOVEOBJECT = 8,
        SC_CHAT = 9,
        SC_STAT_UPDATE = 10,

        // 2026-07-17: 전투. 값이 서버 protocol.h와 물려 있으므로 중간에 끼워넣지 말고
        // 반드시 맨 뒤에만 붙일 것.
        CS_ATTACK = 11,
        SC_DAMAGE = 12,
        SC_DEATH = 13
    }
    public struct PlayerStatsData
    {
        public uint id;

        public int level;
        public int exp;
        public int requiredExp;
        public int gold;

        public int hp;
        public int maxHp;
        public int attack;
        public int defense;

        public float moveSpeed;

        public int combatPower;
    }

    public static PlayerStatsData ReadPlayerStatsData(byte[] buffer, ref int offset)
    {
        PlayerStatsData data = new PlayerStatsData();

        data.id = ReadUInt(buffer, ref offset);

        data.level = ReadInt(buffer, ref offset);
        data.exp = ReadInt(buffer, ref offset);
        data.requiredExp = ReadInt(buffer, ref offset);
        data.gold = ReadInt(buffer, ref offset);

        data.hp = ReadInt(buffer, ref offset);
        data.maxHp = ReadInt(buffer, ref offset);
        data.attack = ReadInt(buffer, ref offset);
        data.defense = ReadInt(buffer, ref offset);

        data.moveSpeed = ReadFloat(buffer, ref offset);

        data.combatPower = ReadInt(buffer, ref offset);

        return data;
    }

    public static byte[] MakeLoginPacket(string playerName)
    {
        byte[] packet = new byte[HeaderSize + NameSize];

        int offset = 0;
        WriteHeader(packet, ref offset, PacketType.CS_LOGIN);
        WriteFixedString(packet, ref offset, playerName, NameSize);

        return packet;
    }

    // 서버 권위 이동: 위치가 아니라 이동 방향(정규화, 정지=0,0)을 보낸다.
    public static byte[] MakeInputPacket(float dx, float dy)
    {
        byte[] packet = new byte[HeaderSize + sizeof(float) + sizeof(float)];

        int offset = 0;
        WriteHeader(packet, ref offset, PacketType.CS_INPUT);
        WriteFloat(packet, ref offset, dx);
        WriteFloat(packet, ref offset, dy);

        return packet;
    }

    // 공격 요청. 방향 공격이라 대상을 클라가 고르지 않는다.
    // 휘두른 방향(정규화)만 보내고, 누가 맞았는지는 서버가 부채꼴로 판정한다.
    public static byte[] MakeAttackPacket(float dx, float dy)
    {
        byte[] packet = new byte[HeaderSize + sizeof(float) + sizeof(float)];

        int offset = 0;
        WriteHeader(packet, ref offset, PacketType.CS_ATTACK);
        WriteFloat(packet, ref offset, dx);
        WriteFloat(packet, ref offset, dy);

        return packet;
    }

    public static byte[] MakeLogoutPacket()
    {
        byte[] packet = new byte[HeaderSize];

        int offset = 0;
        WriteHeader(packet, ref offset, PacketType.CS_LOGOUT);

        return packet;
    }

    public static byte[] MakeChatPacket(string message)
    {
        byte[] packet = new byte[HeaderSize + ChatSize];

        int offset = 0;
        WriteHeader(packet, ref offset, PacketType.CS_CHAT);
        WriteFixedString(packet, ref offset, message, ChatSize);

        return packet;
    }

    private static void WriteHeader(byte[] buffer, ref int offset, PacketType packetType)
    {
        WriteUShort(buffer, ref offset, checked((ushort)buffer.Length));
        WriteUShort(buffer, ref offset, (ushort)packetType);
    }

    public static ushort ReadPacketSize(byte[] buffer, int offset = 0)
    {
        return BitConverter.ToUInt16(buffer, offset);
    }

    public static PacketType ReadPacketType(byte[] buffer, int offset = 0)
    {
        return (PacketType)BitConverter.ToUInt16(buffer, offset + sizeof(ushort));
    }

    public static byte ReadByte(byte[] buffer, ref int offset)
    {
        byte value = buffer[offset];
        offset += sizeof(byte);
        return value;
    }

    public static ushort ReadUShort(byte[] buffer, ref int offset)
    {
        ushort value = BitConverter.ToUInt16(buffer, offset);
        offset += sizeof(ushort);
        return value;
    }

    public static uint ReadUInt(byte[] buffer, ref int offset)
    {
        uint value = BitConverter.ToUInt32(buffer, offset);
        offset += sizeof(uint);
        return value;
    }

    public static int ReadInt(byte[] buffer, ref int offset)
    {
        int value = BitConverter.ToInt32(buffer, offset);
        offset += sizeof(int);
        return value;
    }

    public static float ReadFloat(byte[] buffer, ref int offset)
    {
        float value = BitConverter.ToSingle(buffer, offset);
        offset += sizeof(float);
        return value;
    }

    public static string ReadFixedString(byte[] buffer, ref int offset, int size)
    {
        int length = 0;

        while (length < size && buffer[offset + length] != 0)
        {
            ++length;
        }

        string value = PacketEncoding.GetString(buffer, offset, length);

        offset += size;
        return value;
    }

    private static void WriteUShort(byte[] buffer, ref int offset, ushort value)
    {
        byte[] bytes = BitConverter.GetBytes(value);
        Buffer.BlockCopy(bytes, 0, buffer, offset, bytes.Length);
        offset += bytes.Length;
    }

    private static void WriteFloat(byte[] buffer, ref int offset, float value)
    {
        byte[] bytes = BitConverter.GetBytes(value);
        Buffer.BlockCopy(bytes, 0, buffer, offset, bytes.Length);
        offset += bytes.Length;
    }

    private static void WriteFixedString(byte[] buffer, ref int offset, string value, int size)
    {
        if (value == null)
        {
            value = string.Empty;
        }

        byte[] stringBytes = GetSafeUtf8Bytes(value, size - 1);

        Buffer.BlockCopy(stringBytes, 0, buffer, offset, stringBytes.Length);

        for (int i = stringBytes.Length; i < size; ++i)
        {
            buffer[offset + i] = 0;
        }

        offset += size;
    }

    private static byte[] GetSafeUtf8Bytes(string value, int maxBytes)
    {
        List<byte> result = new List<byte>();

        for (int i = 0; i < value.Length; ++i)
        {
            string currentChar;

            if (char.IsHighSurrogate(value[i]) && i + 1 < value.Length && char.IsLowSurrogate(value[i + 1]))
            {
                currentChar = value.Substring(i, 2);
                ++i;
            }
            else
            {
                currentChar = value[i].ToString();
            }

            byte[] charBytes = PacketEncoding.GetBytes(currentChar);

            if (result.Count + charBytes.Length > maxBytes)
            {
                break;
            }

            result.AddRange(charBytes);
        }

        return result.ToArray();
    }
}