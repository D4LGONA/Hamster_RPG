// 2026-11-16: 1:1 거래.
//
// 흐름: 신청(우클릭 정보 카드) -> 상대 수락 -> 서로 올리기(가방 칸 최대 8개 + 해바라기씨) -> 둘 다 확인 -> 교환
//
// 막아야 하는 것
//  1) 바꿔치기: 상대가 확인을 누른 뒤 내가 올린 걸 바꾸면? -> 누가 무엇을 바꾸든 '양쪽' 확인을 지운다.
//  2) 복사: 올려 둔 아이템을 거래 중에 먹거나 장착/강화하면? -> 거래 중엔 가방을 못 쓰게 막고(ProcessUseItem/Enhance),
//     그래도 바뀌었을 수 있으니(레이드 보상이 같은 칸에 쌓이는 등) 교환 직전에 올린 순간의 내용과 그대로인지 다시 본다.
//  3) 반쪽 교환: 한쪽 가방이 꽉 차서 못 받으면? -> 두 가방의 '복사본'으로 먼저 끝까지 해 보고, 전부 성공할 때만 진짜에 적용.
//  4) 저장: 교환 직후 두 사람을 '한 트랜잭션'으로 DB에 넣는다. 한 사람만 저장되고 서버가 죽으면 아이템이 복사되거나 사라진다.
//     그리고 그 저장을 playersLock 안에서 큐에 넣어, 앞서 뜬 자동 저장 스냅샷이 뒤에 덮어쓰지 못하게 한다.
//
// 지금은 전부 playersLock 하나 안에서라 두 사람을 동시에 바꾸는 게 자연히 원자적이다.
// 락을 사람별로 나누는 다음 단계에서는 두 사람 락을 '항상 id 작은 쪽부터' 잡아야 교착(A가 B를, B가 A를 기다림)이 안 생긴다.

#include "stdafx.h"
#include "GameServer.h"

namespace
{
    SC_TRADE_CLOSED_PACKET MakeClosed(std::uint8_t reason)
    {
        SC_TRADE_CLOSED_PACKET p{};
        p.size = sizeof(SC_TRADE_CLOSED_PACKET);
        p.type = PACKET_TYPE::SC_TRADE_CLOSED;
        p.reason = reason;
        return p;
    }

    void FillName(char (&dst)[NAME_SIZE], const std::string& src)
    {
        std::memset(dst, 0, sizeof(dst));
        std::memcpy(dst, src.data(), src.size() < sizeof(dst) - 1 ? src.size() : sizeof(dst) - 1);
    }

    bool SameSlot(const InventorySlotData& a, const InventorySlotData& b)
    {
        return a.itemId == b.itemId && a.count == b.count && a.enhance == b.enhance;
    }
}

void GameServer::SendTradeClosed(const TradeCloseSends& sends)
{
    for (const auto& [targetId, packet] : sends)
    {
        SendPacket(targetId, &packet, packet.size);
    }
}

void GameServer::CloseTrade(std::uint32_t tradeId, std::uint8_t reasonA, std::uint8_t reasonB, TradeCloseSends& sends)
{
    auto iter = trades.find(tradeId);

    if (iter == trades.end())
    {
        return;
    }

    const TradeSession& t = iter->second;

    for (std::uint32_t id : { t.a, t.b })
    {
        auto playerIter = players.find(id);

        if (playerIter != players.end() && playerIter->second.GetTradeId() == tradeId)
        {
            playerIter->second.SetTradeId(0);
            sends.emplace_back(id, MakeClosed(id == t.a ? reasonA : reasonB));
        }
    }

    trades.erase(iter);
}

void GameServer::CancelTradeOf(std::uint32_t playerId, TradeCloseSends& sends)
{
    auto playerIter = players.find(playerId);

    if (playerIter == players.end() || playerIter->second.GetTradeId() == 0)
    {
        return;
    }

    const std::uint32_t tradeId = playerIter->second.GetTradeId();
    auto tradeIter = trades.find(tradeId);

    if (tradeIter == trades.end())
    {
        playerIter->second.SetTradeId(0);
        return;
    }

    const bool isA = tradeIter->second.a == playerId;
    CloseTrade(tradeId, isA ? 1 : 2, isA ? 2 : 1, sends);   // 나가는 쪽은 1(취소), 남은 쪽은 2(상대가 나감)
}

void GameServer::FillTradeUpdate(std::uint32_t tradeId, std::uint32_t viewerId, SC_TRADE_UPDATE_PACKET& out)
{
    const TradeSession& t = trades.at(tradeId);
    const int mine = viewerId == t.a ? 0 : 1;

    out = SC_TRADE_UPDATE_PACKET{};
    out.size = sizeof(SC_TRADE_UPDATE_PACKET);
    out.type = PACKET_TYPE::SC_TRADE_UPDATE;

    TradeOfferData* targets[2] = { &out.mine, &out.theirs };
    const TradeOffer* sources[2] = { &t.offer[mine], &t.offer[1 - mine] };

    for (int k = 0; k < 2; ++k)
    {
        TradeOfferData& dst = *targets[k];
        const TradeOffer& src = *sources[k];
        dst.count = static_cast<std::uint8_t>(src.snapshot.size());
        for (std::size_t i = 0; i < src.snapshot.size() && i < TRADE_MAX_SLOTS; ++i)
        {
            dst.items[i] = src.snapshot[i];
        }
        dst.gold = src.gold;
        dst.confirmed = src.confirmed ? 1 : 0;
    }
}

void GameServer::ProcessTradeRequest(std::uint32_t playerId, const CS_TRADE_REQUEST_PACKET* packet)
{
    TradeCloseSends fails;
    bool invited = false;
    std::uint32_t targetId = 0;
    SC_TRADE_INVITE_PACKET invite{};

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        Player& me = iter->second;

        if (me.GetTradeId() != 0 || me.GetZoneId() != 0)
        {
            fails.emplace_back(playerId, MakeClosed(6));   // 내가 이미 거래 중이거나 집 안
        }
        else
        {
            // 정보 카드에서 고른 사람. 같은 월드에 있고, 거래가 유지되는 거리(6칸) 안이어야 한다.
            auto other = players.find(packet->targetId);

            if (packet->targetId == 0 || other == players.end() || packet->targetId == playerId || other->second.GetZoneId() != 0 ||
                WithinRadius(me.GetX(), me.GetY(), other->second.GetX(), other->second.GetY(), TRADE_KEEP_RANGE) == false)
            {
                fails.emplace_back(playerId, MakeClosed(7));   // 더 가까이
            }
            else if (other->second.GetTradeId() != 0)
            {
                fails.emplace_back(playerId, MakeClosed(8));   // 상대가 이미 거래 중
            }
            else
            {
                targetId = packet->targetId;
            }
        }

        if (targetId != 0)
        {
            {
                const std::uint32_t tradeId = nextTradeId++;
                TradeSession session;
                session.a = playerId;
                session.b = targetId;
                session.inviteTimer = TRADE_INVITE_SEC;
                trades.emplace(tradeId, session);

                // 신청받은 사람도 바로 '거래 중'으로 묶는다. 안 그러면 두 사람이 동시에 같은 사람에게 신청할 수 있다.
                me.SetTradeId(tradeId);
                players.at(targetId).SetTradeId(tradeId);

                invite.size = sizeof(SC_TRADE_INVITE_PACKET);
                invite.type = PACKET_TYPE::SC_TRADE_INVITE;
                invite.fromId = playerId;
                FillName(invite.fromName, me.GetName());
                invited = true;
            }
        }
    }

    SendTradeClosed(fails);

    if (invited)
    {
        SendPacket(targetId, &invite, invite.size);
    }
}

void GameServer::ProcessTradeRespond(std::uint32_t playerId, const CS_TRADE_RESPOND_PACKET* packet)
{
    TradeCloseSends closes;
    std::vector<std::pair<std::uint32_t, SC_TRADE_OPEN_PACKET>> opens;
    std::vector<std::pair<std::uint32_t, SC_TRADE_UPDATE_PACKET>> updates;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end() || iter->second.GetTradeId() == 0)
        {
            return;
        }

        const std::uint32_t tradeId = iter->second.GetTradeId();
        auto tradeIter = trades.find(tradeId);

        // 신청받은 사람만, 아직 안 열린 거래에만 답할 수 있다
        if (tradeIter == trades.end() || tradeIter->second.b != playerId || tradeIter->second.open)
        {
            return;
        }

        TradeSession& t = tradeIter->second;

        if (packet->accept == 0)
        {
            CloseTrade(tradeId, 5, 5, closes);
        }
        else
        {
            t.open = true;

            for (std::uint32_t id : { t.a, t.b })
            {
                const std::uint32_t partner = id == t.a ? t.b : t.a;
                SC_TRADE_OPEN_PACKET open{};
                open.size = sizeof(SC_TRADE_OPEN_PACKET);
                open.type = PACKET_TYPE::SC_TRADE_OPEN;
                open.partnerId = partner;
                FillName(open.partnerName, players.at(partner).GetName());
                opens.emplace_back(id, open);

                SC_TRADE_UPDATE_PACKET update;
                FillTradeUpdate(tradeId, id, update);
                updates.emplace_back(id, update);
            }
        }
    }

    SendTradeClosed(closes);
    for (auto& [id, p] : opens) { SendPacket(id, &p, p.size); }
    for (auto& [id, p] : updates) { SendPacket(id, &p, p.size); }
}

void GameServer::ProcessTradeOffer(std::uint32_t playerId, const CS_TRADE_OFFER_PACKET* packet)
{
    std::vector<std::pair<std::uint32_t, SC_TRADE_UPDATE_PACKET>> updates;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end() || iter->second.GetTradeId() == 0)
        {
            return;
        }

        const std::uint32_t tradeId = iter->second.GetTradeId();
        auto tradeIter = trades.find(tradeId);

        if (tradeIter == trades.end() || tradeIter->second.open == false)
        {
            return;
        }

        Player& me = iter->second;
        TradeSession& t = tradeIter->second;
        TradeOffer& offer = t.offer[t.a == playerId ? 0 : 1];

        // 검사: 칸 수, 칸 번호 범위·중복·빈 칸, 골드 범위. 하나라도 이상하면 통째로 무시한다.
        if (packet->slotCount > TRADE_MAX_SLOTS || packet->gold < 0 || packet->gold > me.GetStats().gold)
        {
            return;
        }

        std::vector<int> slots;
        std::vector<InventorySlotData> snapshot;

        for (int i = 0; i < packet->slotCount; ++i)
        {
            const int slot = packet->slots[i];

            if (slot < 0 || slot >= INVENTORY_SLOTS ||
                std::find(slots.begin(), slots.end(), slot) != slots.end() ||
                me.GetInventory().GetSlot(slot).itemId == 0)
            {
                return;
            }

            slots.push_back(slot);
            snapshot.push_back(me.GetInventory().GetSlot(slot));
        }

        offer.slots = std::move(slots);
        offer.snapshot = std::move(snapshot);
        offer.gold = packet->gold;

        // 바꿔치기 방지: 누가 무엇을 바꾸든 양쪽 확인을 지운다
        t.offer[0].confirmed = false;
        t.offer[1].confirmed = false;

        for (std::uint32_t id : { t.a, t.b })
        {
            SC_TRADE_UPDATE_PACKET update;
            FillTradeUpdate(tradeId, id, update);
            updates.emplace_back(id, update);
        }
    }

    for (auto& [id, p] : updates) { SendPacket(id, &p, p.size); }
}

void GameServer::ProcessTradeConfirm(std::uint32_t playerId)
{
    std::vector<std::pair<std::uint32_t, SC_TRADE_UPDATE_PACKET>> updates;
    std::vector<std::pair<std::uint32_t, SC_INVENTORY_PACKET>> inventories;
    std::vector<std::pair<std::uint32_t, PlayerStats>> statSends;
    TradeCloseSends closes;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end() || iter->second.GetTradeId() == 0)
        {
            return;
        }

        const std::uint32_t tradeId = iter->second.GetTradeId();
        auto tradeIter = trades.find(tradeId);

        if (tradeIter == trades.end() || tradeIter->second.open == false)
        {
            return;
        }

        TradeSession& t = tradeIter->second;
        t.offer[t.a == playerId ? 0 : 1].confirmed = true;

        if (t.offer[0].confirmed == false || t.offer[1].confirmed == false)
        {
            for (std::uint32_t id : { t.a, t.b })
            {
                SC_TRADE_UPDATE_PACKET update;
                FillTradeUpdate(tradeId, id, update);
                updates.emplace_back(id, update);
            }
        }
        else
        {
            // ---- 둘 다 확인: 교환 ----
            Player& pa = players.at(t.a);
            Player& pb = players.at(t.b);
            Player* people[2] = { &pa, &pb };

            // 1) 올린 순간과 지금 가방/골드가 같은가
            bool unchanged = true;
            for (int k = 0; k < 2 && unchanged; ++k)
            {
                const TradeOffer& o = t.offer[k];
                if (people[k]->GetStats().gold < o.gold) { unchanged = false; }
                for (std::size_t i = 0; i < o.slots.size() && unchanged; ++i)
                {
                    unchanged = SameSlot(people[k]->GetInventory().GetSlot(o.slots[i]), o.snapshot[i]);
                }
            }

            if (unchanged == false)
            {
                CloseTrade(tradeId, 3, 3, closes);
            }
            else
            {
                // 2) 복사본으로 끝까지 해 본다: 각자 올린 칸을 빼고, 상대가 올린 걸 넣는다
                Inventory next[2] = { pa.GetInventory(), pb.GetInventory() };
                for (int k = 0; k < 2; ++k)
                {
                    for (int slot : t.offer[k].slots) { next[k].TakeSlot(slot); }
                }

                bool fits = true;
                for (int k = 0; k < 2 && fits; ++k)
                {
                    for (const InventorySlotData& item : t.offer[1 - k].snapshot)
                    {
                        if (next[k].TryAddSlot(item) == false) { fits = false; break; }
                    }
                }

                if (fits == false)
                {
                    CloseTrade(tradeId, 4, 4, closes);
                }
                else
                {
                    // 3) 전부 됐으니 진짜에 적용(여기서부터는 실패할 일이 없다)
                    const int goldA = t.offer[0].gold;
                    const int goldB = t.offer[1].gold;

                    pa.GetInventory() = next[0];
                    pb.GetInventory() = next[1];
                    pa.GetStats().gold += goldB - goldA;
                    pb.GetStats().gold += goldA - goldB;
                    pa.RefreshWeaponStats();
                    pb.RefreshWeaponStats();

                    for (Player* p : people)
                    {
                        SC_INVENTORY_PACKET inv{};
                        p->GetInventory().FillPacket(inv);
                        inventories.emplace_back(p->GetId(), inv);
                        statSends.emplace_back(p->GetId(), p->GetStats());
                    }

                    // 4) 두 사람을 한 트랜잭션으로 저장. 락 안에서 큐에 넣어 앞선 자동 저장보다 뒤에 적용되게 한다.
                    if (dbWorker.IsEnabled())
                    {
                        std::vector<CharacterData> both;
                        if (pa.IsPersistent()) { both.push_back(MakeSaveData(pa)); }
                        if (pb.IsPersistent()) { both.push_back(MakeSaveData(pb)); }

                        if (both.empty() == false)
                        {
                            dbWorker.Post([both](Database& db)
                            {
                                if (db.SaveCharacters(both) == false)
                                {
                                    std::cout << "[DB] trade save failed: " << db.GetLastError() << std::endl;
                                }
                            });
                        }
                    }

                    std::cout << "[TRADE] " << pa.GetName() << " <-> " << pb.GetName()
                              << " items " << t.offer[0].slots.size() << "/" << t.offer[1].slots.size()
                              << " gold " << goldA << "/" << goldB << std::endl;

                    CloseTrade(tradeId, 0, 0, closes);
                }
            }
        }
    }

    for (auto& [id, p] : updates) { SendPacket(id, &p, p.size); }
    for (auto& [id, p] : inventories) { SendInventory(id, p); }
    for (auto& [id, s] : statSends) { SendStatUpdate(id, s); }
    SendTradeClosed(closes);
}

void GameServer::ProcessTradeCancel(std::uint32_t playerId)
{
    TradeCloseSends closes;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end() || iter->second.GetTradeId() == 0)
        {
            return;
        }

        CloseTrade(iter->second.GetTradeId(), 1, 1, closes);
    }

    SendTradeClosed(closes);
}

// 틱(playersLock 안): 수락을 15초 안 하면 닫고, 열린 거래는 둘이 6유닛 넘게 멀어지면 닫는다.
void GameServer::UpdateTrades(float deltaSeconds, TradeCloseSends& sends)
{
    std::vector<std::pair<std::uint32_t, std::uint8_t>> toClose;

    for (auto& [tradeId, t] : trades)
    {
        auto pa = players.find(t.a);
        auto pb = players.find(t.b);

        if (pa == players.end() || pb == players.end())
        {
            toClose.emplace_back(tradeId, 2);
            continue;
        }

        if (t.open == false)
        {
            t.inviteTimer -= deltaSeconds;
            if (t.inviteTimer <= 0.0f) { toClose.emplace_back(tradeId, 5); }
            continue;
        }

        if (pa->second.GetZoneId() != 0 || pb->second.GetZoneId() != 0 ||
            WithinRadius(pa->second.GetX(), pa->second.GetY(), pb->second.GetX(), pb->second.GetY(), TRADE_KEEP_RANGE) == false)
        {
            toClose.emplace_back(tradeId, 2);
        }
    }

    for (auto& [tradeId, reason] : toClose)
    {
        CloseTrade(tradeId, reason, reason, sends);
    }
}
