// 2026-11-12: DB 1단계. MySQL에 ODBC로 붙는다.
//
// 구성
//   Database : 연결 하나 + 프로시저 호출. DB 스레드 혼자만 쓴다(ODBC 연결은 스레드 간에 나눠 쓰지 않는다).
//   DbWorker : DB 전용 스레드 + 작업 큐. 워커/틱 스레드는 작업을 넣기만 하고 바로 돌아간다.
//
// 왜 스레드를 따로 두나: 쿼리 한 번이 수 ms다. 게임 틱(66ms)이나 IOCP 워커가 기다리면
// 그동안 이동/전투가 멈춘다. 그래서 DB는 줄 세워서 한 스레드가 처리하고, 결과는 콜백으로 돌려준다.
//
// 연결 문자열: db_config.txt (없으면 DB 없이 돈다. 캐릭터가 저장되지 않을 뿐 게임은 그대로)
// 테이블/프로시저: Server/sql/02_character.sql

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// 저장/불러오기 단위. 원본 값만 들고 다닌다(파생 스탯은 PlayerStats::Recalculate가 만든다).
struct CharacterData
{
    std::string name;
    int level = 1;
    int exp = 0;
    int gold = 0;
    int hp = 100;
    float x = 0.0f;
    float y = 0.0f;
    bool isNew = true;
};

class Database
{
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool Connect(const std::string& connectionString);
    void Disconnect();
    bool IsConnected() const { return dbc != nullptr; }

    // 없으면 만든다. 실패하면 false(그때 out은 손대지 않는다).
    bool LoadCharacter(const std::string& name, CharacterData& out);

    // 여러 명을 트랜잭션 하나로 저장한다. 자동 저장 때 800명을 한 번에 넣어도 커밋은 한 번.
    bool SaveCharacters(const std::vector<CharacterData>& list);

    const std::string& GetLastError() const { return lastError; }

private:
    bool TryLoad(const std::string& name, CharacterData& out);
    bool TrySave(const std::vector<CharacterData>& list);
    bool ReconnectIfDead();
    void RecordError(short handleType, void* handle, const char* where);

    void* env = nullptr;   // SQLHENV  (헤더에 sql.h를 퍼뜨리지 않으려고 void*로 들고 있다)
    void* dbc = nullptr;   // SQLHDBC
    std::string connectionString;
    std::string lastError;
};

class DbWorker
{
public:
    using Job = std::function<void(Database&)>;

    // 연결에 실패하면 false. 그때는 스레드를 띄우지 않고 IsEnabled() == false로 남는다.
    bool Start(const std::string& connectionString);

    // 남은 작업을 다 처리하고 스레드를 끝낸다(종료 직전 저장이 날아가지 않게).
    void Stop();

    bool IsEnabled() const { return enabled; }
    void Post(Job job);
    std::size_t GetQueueSize();

private:
    void Run();

    Database db;
    std::thread thread;
    std::mutex queueLock;
    std::condition_variable queueReady;
    std::deque<Job> queue;
    bool stopping = false;
    std::atomic<bool> enabled{ false };
};

// db_config.txt 한 줄(# 주석 무시)을 읽는다. 실행 위치에 따라 Server/ 또는 Server/x64/Debug/ 일 수 있어 몇 곳을 본다.
std::string LoadDbConnectionString();
