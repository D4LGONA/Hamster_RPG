// 2026-06-11: Protocol.cs 파일 제작. C++ 서버 protocol.h와 동일한 패킷 규칙을 사용한다.

using System;
using System.Text;

public static class Protocol
{
    public const string ServerIp = "127.0.0.1";
    public const int Port = 4000;

    public const int NameSize = 20;
    public const int HeaderSize = 4;

    public enum PacketType : ushort
    {
        // Client -> Server
        CS_LOGIN = 1,
        CS_MOVE,
        CS_LOGOUT,

        // Server -> Client
        SC_LOGIN_RESULT,
        SC_SPAWN,
        SC_DESPAWN,
        SC_MOVEOBJECT
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

        string value = Encoding.ASCII.GetString(buffer, offset, length);
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
        byte[] stringBytes = Encoding.ASCII.GetBytes(value ?? string.Empty);

        int copySize = Math.Min(stringBytes.Length, size - 1);
        Buffer.BlockCopy(stringBytes, 0, buffer, offset, copySize);

        offset += size;
    }
}