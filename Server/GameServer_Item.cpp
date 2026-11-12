// 2026-11-13: 아이템(처치 보상, 개인 드랍, 줍기, 사용/장착)과 몬스터 반격.
//
// 개인 드랍: 바닥 아이템은 잡은 사람에게만 보이고 그 사람만 줍는다(SC_ITEM_DROP을 그 사람에게만 보낸다).
//   - 시야(AOI) 목록에 아이템까지 넣지 않아도 된다. 보내는 대상이 한 명이라 패킷도 적다.
//   - 남이 먼저 주워 가는 다툼이 없다.
// 줍기는 클라가 요청하지 않는다. 틱마다 서버가 '주인이 가까이 왔나'를 보고 직접 넣는다(서버 권위).

#include "stdafx.h"
#include "GameServer.h"

namespace
{
    struct KindReward { int exp; int gold; int damage; };

    KindReward RewardOf(WorldGen::MonsterKind kind)
    {
        switch (kind)
        {
        case WorldGen::MonsterKind::Caterpillar: return { 20, 10, 5 };
        case WorldGen::MonsterKind::Ladybug:     return { 35, 15, 8 };
        case WorldGen::MonsterKind::CatBoss:     return { 400, 200, 20 };
        }
        return { 20, 10, 5 };
    }
}

// playersLock 안에서. 잡은 사람에게 경험치/골드를 주고 드랍을 굴린다.
void GameServer::GrantKillRewards(Player& killer, const Monster& monster, bool& leveledUp,
                                  std::vector<SC_ITEM_DROP_PACKET>& dropSends)
{
    const KindReward reward = RewardOf(monster.GetKind());
    PlayerStats& stats = killer.GetStats();

    stats.gold += reward.gold;

    if (stats.AddExp(reward.exp))
    {
        leveledUp = true;
    }

    std::uniform_int_distribution<int> percent(1, 100);
    std::uniform_real_distribution<float> scatter(-0.6f, 0.6f);

    Items::ForEachDropRule(monster.GetKind(), [&](const Items::DropRule& rule)
    {
        if (percent(dropRng) > rule.percent)
        {
            return;
        }

        const std::uint32_t dropId = nextDropId++;
        GroundDrop drop{ killer.GetId(), rule.itemId, rule.count,
                         monster.GetX() + scatter(dropRng), monster.GetY() + scatter(dropRng), DROP_TTL_SEC };
        groundDrops.emplace(dropId, drop);

        SC_ITEM_DROP_PACKET packet{};
        packet.size = sizeof(SC_ITEM_DROP_PACKET);
        packet.type = PACKET_TYPE::SC_ITEM_DROP;
        packet.dropId = dropId;
        packet.itemId = drop.itemId;
        packet.count = drop.count;
        packet.x = drop.x;
        packet.y = drop.y;
        dropSends.push_back(packet);
    });
}

// 인벤토리 칸 사용. 먹을 것이면 먹고, 무기면 장착/해제(같은 칸을 다시 누르면 해제).
void GameServer::ProcessUseItem(std::uint32_t playerId, const CS_USE_ITEM_PACKET* packet)
{
    const int index = packet->slot;

    if (index < 0 || index >= INVENTORY_SLOTS)
    {
        return;
    }

    SC_INVENTORY_PACKET inventoryPacket{};
    PlayerStats stats;
    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(playersLock);

        auto iter = players.find(playerId);

        if (iter == players.end())
        {
            return;
        }

        Player& player = iter->second;
        Inventory& inventory = player.GetInventory();
        const InventorySlotData slot = inventory.GetSlot(index);
        const ItemDef* def = Items::Find(slot.itemId);

        if (def == nullptr)
        {
            return;   // 빈 칸
        }

        if (def->type == ItemType::Consumable)
        {
            PlayerStats& s = player.GetStats();

            if (s.hp >= s.maxHp)
            {
                return;   // 가득 찼는데 먹으면 아깝다. 아무 일도 안 한다
            }

            s.hp = std::min(s.maxHp, s.hp + def->heal);
            inventory.ConsumeOne(index);
            changed = true;
        }
        else if (def->type == ItemType::Weapon)
        {
            inventory.SetEquippedSlot(inventory.GetEquippedSlot() == index ? -1 : index);
            player.RefreshWeaponStats();
            changed = true;
        }

        if (changed)
        {
            inventory.FillPacket(inventoryPacket);
            stats = player.GetStats();
        }
    }

    if (changed)
    {
        SendInventory(playerId, inventoryPacket);
        SendStatUpdate(playerId, stats);
    }
}

void GameServer::SendInventory(std::uint32_t playerId, const SC_INVENTORY_PACKET& packet)
{
    SendPacket(playerId, &packet, packet.size);
}

// 틱(playersLock 안): 시간이 다 된 드랍을 지우고, 주인이 가까이 온 드랍은 가방에 넣는다.
void GameServer::UpdateGroundDrops(float deltaSeconds, TickItemSends& sends)
{
    std::unordered_set<std::uint32_t> inventoryChanged;

    for (auto iter = groundDrops.begin(); iter != groundDrops.end(); )
    {
        GroundDrop& drop = iter->second;
        const std::uint32_t dropId = iter->first;
        drop.ttl -= deltaSeconds;

        auto ownerIter = players.find(drop.ownerId);
        bool remove = drop.ttl <= 0.0f || ownerIter == players.end();   // 시간 끝 또는 주인이 나갔다
        bool picked = false;

        if (remove == false)
        {
            Player& owner = ownerIter->second;

            if (owner.GetZoneId() == 0 &&
                WithinRadius(owner.GetX(), owner.GetY(), drop.x, drop.y, DROP_PICKUP_RADIUS) &&
                owner.GetInventory().TryAdd(drop.itemId, drop.count))   // 가방이 꽉 찼으면 바닥에 남는다
            {
                picked = true;
                remove = true;
                inventoryChanged.insert(drop.ownerId);

                SC_ITEM_PICKUP_PACKET pickup{};
                pickup.size = sizeof(SC_ITEM_PICKUP_PACKET);
                pickup.type = PACKET_TYPE::SC_ITEM_PICKUP;
                pickup.itemId = drop.itemId;
                pickup.count = drop.count;
                sends.pickups.emplace_back(drop.ownerId, pickup);
            }
        }

        if (remove == false)
        {
            ++iter;
            continue;
        }

        if (ownerIter != players.end() || picked)
        {
            SC_ITEM_REMOVE_PACKET removePacket{};
            removePacket.size = sizeof(SC_ITEM_REMOVE_PACKET);
            removePacket.type = PACKET_TYPE::SC_ITEM_REMOVE;
            removePacket.dropId = dropId;
            sends.removes.emplace_back(drop.ownerId, removePacket);
        }

        iter = groundDrops.erase(iter);
    }

    for (std::uint32_t ownerId : inventoryChanged)
    {
        auto ownerIter = players.find(ownerId);

        if (ownerIter != players.end())
        {
            SC_INVENTORY_PACKET packet{};
            ownerIter->second.GetInventory().FillPacket(packet);
            sends.inventories.emplace_back(ownerId, packet);
        }
    }
}

// 틱(playersLock 안): 타겟에 붙은 몬스터가 문다. HP가 0이 되면 골드 10%를 잃고 광장으로 돌아간다.
// 광장으로 옮긴 플레이어는 playerMovers에 넣어, 뒤이은 시야 diff가 주변을 새로 계산하게 한다(순간이동도 이동이다).
void GameServer::UpdateMonsterAttacks(float deltaSeconds, std::vector<std::uint32_t>& playerMovers, TickItemSends& sends)
{
    std::unordered_set<std::uint32_t> hurt;

    for (auto& [monsterId, monster] : monsters)
    {
        if (monster.IsAlive() == false)
        {
            continue;
        }

        float cooldown = monster.GetAttackCooldown() - deltaSeconds;
        monster.SetAttackCooldown(cooldown > 0.0f ? cooldown : 0.0f);

        const std::uint32_t targetId = monster.GetTargetId();

        if (targetId == 0 || cooldown > 0.0f)
        {
            continue;
        }

        auto targetIter = players.find(targetId);

        if (targetIter == players.end())
        {
            continue;
        }

        Player& target = targetIter->second;

        if (target.GetZoneId() != 0 ||
            WithinRadius(monster.GetX(), monster.GetY(), target.GetX(), target.GetY(), MONSTER_ATTACK_RANGE) == false)
        {
            continue;
        }

        PlayerStats& s = target.GetStats();
        int damage = RewardOf(monster.GetKind()).damage - s.defense / 2;

        if (damage < 1)
        {
            damage = 1;
        }

        s.hp -= damage;
        monster.SetAttackCooldown(MONSTER_ATTACK_COOLDOWN);
        hurt.insert(targetId);

        SC_DAMAGE_PACKET hit{};
        hit.size = sizeof(SC_DAMAGE_PACKET);
        hit.type = PACKET_TYPE::SC_DAMAGE;
        hit.attackerId = monsterId;
        hit.targetId = targetId;
        hit.damage = damage;
        hit.targetHp = s.hp > 0 ? s.hp : 0;
        hit.targetMaxHp = s.maxHp;
        sends.damages.emplace_back(targetId, hit);

        if (s.hp > 0)
        {
            continue;
        }

        // 쓰러짐: 골드 10% 잃고, HP 가득 채워 광장으로
        const int goldLost = static_cast<int>(s.gold * FAINT_GOLD_LOSS);
        s.gold -= goldLost;
        s.hp = s.maxHp;

        float spawnX = 0.0f;
        float spawnY = 0.0f;
        PickSpawnPosition(spawnX, spawnY);

        const float oldX = target.GetX();
        const float oldY = target.GetY();
        target.SetPosition(spawnX, spawnY);
        target.SetInput(0.0f, 0.0f);
        sectorGrid.Move(targetId, oldX, oldY, spawnX, spawnY);

        if (std::find(playerMovers.begin(), playerMovers.end(), targetId) == playerMovers.end())
        {
            playerMovers.push_back(targetId);
        }

        // 나를 쫓던 몬스터들은 놓아준다
        for (auto& [otherId, other] : monsters)
        {
            if (other.GetTargetId() == targetId)
            {
                other.SetTargetId(0);
                other.SetAggroTimer(0.0f);
            }
        }

        SC_PLAYER_FAINT_PACKET faint{};
        faint.size = sizeof(SC_PLAYER_FAINT_PACKET);
        faint.type = PACKET_TYPE::SC_PLAYER_FAINT;
        faint.goldLost = goldLost;
        sends.faints.emplace_back(targetId, faint);
    }

    for (std::uint32_t playerId : hurt)
    {
        auto iter = players.find(playerId);

        if (iter != players.end())
        {
            sends.stats.emplace_back(playerId, iter->second.GetStats());
        }
    }
}
