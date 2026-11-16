// 2026-11-16: 다른 햄스터 우클릭 -> 정보 카드
//
// 보이는 사람만 볼 수 있다: 같은 곳(월드/같은 집)에 있고 시야(VIEW_RANGE) 안일 때.
// 아무 id나 넣어서 멀리 있는 사람 정보를 캐 보는 걸 막는다(서버가 판정, 클라는 요청만).
// 공개하는 건 겉으로 드러나는 것만: 레벨/전투력/공격/방어/최대HP/장착 무기. 골드·가방·위치는 안 준다.
//
// playersLock은 값 몇 개 복사하는 동안만 잡고, 보내기는 락 밖에서 한다.

#include "stdafx.h"
#include "GameServer.h"

void GameServer::ProcessPlayerInfoRequest(std::uint32_t playerId, const CS_PLAYER_INFO_REQUEST_PACKET* packet)
{
    SC_PLAYER_INFO_PACKET info{};
    info.size = sizeof(SC_PLAYER_INFO_PACKET);
    info.type = PACKET_TYPE::SC_PLAYER_INFO;
    info.id = packet->targetId;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto me = players.find(playerId);
        auto target = players.find(packet->targetId);

        if (me == players.end())
        {
            return;
        }

        if (target != players.end() && target->first != playerId &&
            target->second.GetZoneId() == me->second.GetZoneId() &&
            WithinRadius(me->second.GetX(), me->second.GetY(), target->second.GetX(), target->second.GetY(), VIEW_RANGE))
        {
            const Player& p = target->second;
            const PlayerStats& s = p.GetStats();

            info.found = 1;
            const std::string& name = p.GetName();
            std::memcpy(info.name, name.data(), std::min<std::size_t>(name.size(), NAME_SIZE - 1));
            info.level = s.level;
            info.combatPower = s.combatPower;
            info.attack = s.attack;
            info.defense = s.defense;
            info.maxHp = s.maxHp;

            const int weaponSlot = p.GetInventory().GetEquippedSlot();
            if (weaponSlot >= 0)
            {
                const InventorySlotData& weapon = p.GetInventory().GetSlot(weaponSlot);
                info.weaponItemId = weapon.itemId;
                info.weaponEnhance = weapon.enhance;
            }
        }
    }

    SendPacket(playerId, &info, info.size);
}
