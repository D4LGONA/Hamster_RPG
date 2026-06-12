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
        SetupRecv();
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

    void SetupRecv()
    {
        Reset();
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