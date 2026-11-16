// 2026-11-16: 랭킹 — 레벨 순위 + 지난 레이드 기여도.
//
// 스냅샷 방식: 순위표는 30초마다 새로 '만들어서' 통째로 바꿔 끼운다(std::atomic<std::shared_ptr>).
//   - 조회(랭킹 창 열기)는 게임 락(playersLock)을 잡지 않는다. 그 순간 들어 있는 목록의 shared_ptr를 복사해 가면 끝.
//     (std::atomic<shared_ptr>는 MSVC/libstdc++에서 lock-free가 아니라 포인터 복사 동안만 아주 짧은 내부 잠금을 쓴다.
//      목록 크기와 상관없이 포인터 하나 복사하는 시간이라, 이동/전투가 쓰는 playersLock과 경합하지 않는 게 핵심이다)
//     만드는 쪽이 새 목록으로 바꿔도, 읽던 사람은 자기가 가져간 옛 목록을 끝까지 안전하게 읽는다(참조 카운트).
//   - 매 조회마다 정렬하거나 DB를 치지 않는다. 800명이 동시에 창을 열어도 DB 쿼리는 30초에 한 번.
//   - 대신 최대 30초 늦은 순위가 보인다. 순위표에는 그 정도 지연이 괜찮다고 판단했다.
// 레벨 순위는 DB(접속 안 한 사람 포함)에서 가져오고, DB가 없으면 접속 중인 사람으로 만든다.

#include "stdafx.h"
#include "GameServer.h"

namespace
{
    RankEntry MakeEntry(const std::string& name, int value, int extra)
    {
        RankEntry entry{};
        std::memcpy(entry.name, name.data(), name.size() < NAME_SIZE - 1 ? name.size() : NAME_SIZE - 1);
        entry.value = value;
        entry.extra = extra;
        return entry;
    }
}

// 틱 스레드에서 30초마다(playersLock 안).
void GameServer::RefreshLevelRanking()
{
    if (dbWorker.IsEnabled())
    {
        // DB 스레드가 조회해서 스냅샷을 바꿔 끼운다. 틱은 기다리지 않는다.
        dbWorker.Post([this](Database& db)
        {
            std::vector<Database::LevelRankRow> rows;

            if (db.LoadLevelRanking(RANKING_COUNT, rows) == false)
            {
                std::cout << "[RANKING] load failed: " << db.GetLastError() << std::endl;
                return;
            }

            auto list = std::make_shared<RankingList>();
            for (const Database::LevelRankRow& row : rows)
            {
                list->push_back(MakeEntry(row.name, row.level, row.exp));
            }

            levelRanking.store(std::move(list));
        });
        return;
    }

    // DB 없음: 접속 중인 사람으로
    std::vector<const Player*> online;
    online.reserve(players.size());
    for (const auto& [id, player] : players)
    {
        online.push_back(&player);
    }

    const std::size_t count = std::min<std::size_t>(online.size(), RANKING_COUNT);
    std::partial_sort(online.begin(), online.begin() + count, online.end(), [](const Player* a, const Player* b)
    {
        const PlayerStats& sa = a->GetStats();
        const PlayerStats& sb = b->GetStats();
        return sa.level != sb.level ? sa.level > sb.level : sa.exp > sb.exp;
    });

    auto list = std::make_shared<RankingList>();
    for (std::size_t i = 0; i < count; ++i)
    {
        list->push_back(MakeEntry(online[i]->GetName(), online[i]->GetStats().level, online[i]->GetStats().exp));
    }

    levelRanking.store(std::move(list));
}

// IOCP 워커 스레드. playersLock 없이 스냅샷 두 개를 읽어 보낸다.
void GameServer::ProcessRankingRequest(std::uint32_t playerId)
{
    std::shared_ptr<const RankingList> levels = levelRanking.load();
    std::shared_ptr<const RankingList> raids = raidRanking.load();

    SC_RANKING_PACKET packet{};
    packet.size = sizeof(SC_RANKING_PACKET);
    packet.type = PACKET_TYPE::SC_RANKING;
    packet.levelCount = static_cast<std::uint8_t>(std::min<std::size_t>(levels->size(), RANKING_COUNT));
    packet.raidCount = static_cast<std::uint8_t>(std::min<std::size_t>(raids->size(), RANKING_COUNT));

    for (int i = 0; i < packet.levelCount; ++i) { packet.level[i] = (*levels)[i]; }
    for (int i = 0; i < packet.raidCount; ++i) { packet.raid[i] = (*raids)[i]; }

    SendPacket(playerId, &packet, packet.size);
}
