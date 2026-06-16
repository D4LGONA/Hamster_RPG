#include "stdafx.h"
#include "GameServer.h"

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    GameServer server;
    server.Run();

    return 0;
}