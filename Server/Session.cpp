// 2026-06-11: Session.cpp 파일 제작. Session 클래스 구현.

#include "stdafx.h"
#include "Session.h"

bool Session::Recv()
{
    if (socket == INVALID_SOCKET) return false;

    recvOver.SetupRecv(savedBytes);

    DWORD recvBytes = 0;
    DWORD flags = 0;

    int result = WSARecv(socket, &recvOver.wsabuf, 1, &recvBytes, &flags, &recvOver.overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();

        if (error != WSA_IO_PENDING)
        {
            std::cout << "[RECV ERROR] playerId = " << playerId << ", error = " << error << std::endl;
            return false;
        }
    }
    return true;
}

bool Session::Send(const void* data, int size)
{
    if (socket == INVALID_SOCKET) return false;
    if (data == nullptr || size <= 0) return false;

    EXT_OVER* sendOver = new EXT_OVER();
    sendOver->SetupSend(reinterpret_cast<const char*>(data), size);

    DWORD sendBytes = 0;

    int result = WSASend(socket, &sendOver->wsabuf, 1, &sendBytes, 0, &sendOver->overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int error = WSAGetLastError();

        if (error != WSA_IO_PENDING)
        {
            std::cout << "[SEND ERROR] playerId = " << playerId << ", error = " << error << std::endl;
            delete sendOver;
            return false;
        }
    }
    return true;
}