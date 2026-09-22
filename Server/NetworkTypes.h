// NetworkTypes.h
#pragma once

#include "stdafx.h"

constexpr int BUFSIZE = 1024;

enum class IO_TYPE
{
    ACCEPT,
    RECV,
    SEND
};

struct EXT_OVER
{
    WSAOVERLAPPED overlapped{};
    WSABUF wsabuf{};

    char buffer[BUFSIZE]{};

    IO_TYPE ioType = IO_TYPE::RECV;

    SOCKET acceptSocket = INVALID_SOCKET;

    EXT_OVER()
    {
        Reset();
        ioType = IO_TYPE::RECV;
    }

    void Reset()
    {
        ZeroMemory(&overlapped, sizeof(overlapped));
        ZeroMemory(buffer, sizeof(buffer));

        wsabuf.buf = buffer;
        wsabuf.len = BUFSIZE;
    }

    void SetupAccept(SOCKET socket)
    {
        Reset();
        ioType = IO_TYPE::ACCEPT;
        acceptSocket = socket;
    }

    // savedBytes: 버퍼 앞에 남겨 둔 꼬리 길이. 그 뒤부터, 남은 칸만큼만 받는다.
    // Reset()을 부르면 꼬리가 지워지므로 overlapped만 초기화한다.
    void SetupRecv(int savedBytes)
    {
        ZeroMemory(&overlapped, sizeof(overlapped));

        wsabuf.buf = buffer + savedBytes;
        wsabuf.len = BUFSIZE - savedBytes;

        ioType = IO_TYPE::RECV;
    }

    void SetupSend(const char* data, int len)
    {
        Reset();
        ioType = IO_TYPE::SEND;

        if (len > BUFSIZE)
        {
            len = BUFSIZE;
        }

        std::memcpy(buffer, data, len);
        wsabuf.buf = buffer;
        wsabuf.len = len;
    }
};