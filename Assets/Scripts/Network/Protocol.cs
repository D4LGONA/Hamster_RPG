// 2026-06-11: Protocol.cs 파일 제작. C++ 서버 protocol.h와 동일한 패킷 규칙을 사용한다.
// 2026-06-16: CS_CHAT, SC_CHAT 추가, 문자열 인코딩 UTF-8 적용

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

    private static readonly Encoding PacketEncoding = new UTF8Encoding(false);

    public enum PacketType : ushort
    {
        // Client -> Server
        CS_LOGIN = 1,
        CS_MOVE = 2,
        CS_LOGOUT = 3,
        CS_CHAT = 4,

        // Server -> Client
        SC_LOGIN_RESULT = 5,
        SC_SPAWN = 6,
        SC_DESPAWN = 7,
        SC_MOVEOBJECT = 8,
        SC_CHAT = 9,
        SC_STAT_UPDATE
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

    public static byte[] MakeMovePacket(float x, float y)
    {
        byte[] packet = new byte[HeaderSize + sizeof(float) + sizeof(float)];

        int offset = 0;
        WriteHeader(packet, ref offset, PacketType.CS_MOVE);
        WriteFloat(packet, ref offset, x);
        WriteFloat(packet, ref offset, y);

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