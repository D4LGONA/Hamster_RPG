#include "stdafx.h"
#include "GameServer.h"

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    GameServer server;

    // --random-spawn <범위> : 부하 테스트 전용. 로그인 위치를 ±범위 안 랜덤으로 흩어놓는다.
    // 안 주면 평소대로 전원 원점에 스폰한다.
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--random-spawn") == 0 && i + 1 < argc)
        {
            float extent = static_cast<float>(std::atof(argv[++i]));
            server.SetRandomSpawn(extent);
            std::cout << "[LOADTEST] random spawn: +-" << extent << std::endl;
        }
        else if (std::strcmp(argv[i], "--profile") == 0)
        {
            server.SetProfile(true);
            std::cout << "[LOADTEST] tick profile on (5s interval)" << std::endl;
        }
    }

    server.Run();

    return 0;
}
