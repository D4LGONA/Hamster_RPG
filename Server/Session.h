// 2026-06-11: Session.h 파일 제작. 클라이언트 세션을 관리하는 클래스.
#pragma once

#include "stdafx.h"
#include "NetworkTypes.h"

class Session
{
public:
    Session(SOCKET newSocket)
        : socket(newSocket)
    {}

    ~Session() { Close(); }

	Session(const Session&) = delete; // = delete 키워드: 복사 생성자와 복사 할당 연산자를 삭제하여 객체의 복사를 방지한다.
    Session& operator=(const Session&) = delete;

    SOCKET GetSocket() const { return socket; }

    std::uint32_t GetPlayerId() const { return playerId; }
    void SetPlayerId(std::uint32_t id) { playerId = id; }

    bool Recv();
    bool Send(const void* data, int size);

    void Close()
    {
        if (socket != INVALID_SOCKET)
        {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }

private:
    SOCKET socket = INVALID_SOCKET;
    std::uint32_t playerId = 0;

    EXT_OVER recvOver;
};