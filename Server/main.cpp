#include "stdafx.h"
#include "GameServer.h"

// 2026-11-12: 콘솔 창을 닫거나 Ctrl+C로 끌 때 접속 중인 캐릭터를 저장하고 끈다.
// 이 핸들러는 별도 스레드에서 불린다. 윈도우는 창 닫기 때 약 5초를 기다려 준다.
namespace
{
    GameServer* g_server = nullptr;

    BOOL WINAPI OnConsoleClose(DWORD ctrlType)
    {
        if (g_server != nullptr &&
            (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_BREAK_EVENT))
        {
            g_server->ShutdownSave();
        }
        return FALSE;   // 기본 동작(프로세스 종료)은 그대로
    }
}

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
        else if (std::strcmp(argv[i], "--raid-at") == 0 && i + 1 < argc)
        {
            // --raid-at <초> [--raid-hp <HP>] : 레이드 부하 테스트. 왕 고양이를 이 시간 뒤에 부른다.
            float at = static_cast<float>(std::atof(argv[++i]));
            int hp = 0;
            if (i + 2 < argc && std::strcmp(argv[i + 1], "--raid-hp") == 0)
            {
                hp = std::atoi(argv[i + 2]);
                i += 2;
            }
            server.SetRaidTest(at, hp);
            std::cout << "[LOADTEST] raid boss at " << at << "s, hp " << (hp > 0 ? hp : 30000) << std::endl;
        }
        else if (std::strcmp(argv[i], "--profile") == 0)
        {
            server.SetProfile(true);
            std::cout << "[LOADTEST] tick profile on (5s interval)" << std::endl;
        }
    }

    g_server = &server;
    SetConsoleCtrlHandler(OnConsoleClose, TRUE);

    server.Run();

    return 0;
}
