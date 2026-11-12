// 2026-11-13: 아이템 표, 인벤토리, 드랍 표.
//
// 아이템 정의(이름/효과)는 서버가 정답이다. 클라(Assets/Scripts/Items/ItemTable.cs)는 아이콘과 이름만 같은 id로 갖고 있다.
// id를 바꾸면 DB에 저장된 인벤토리와 클라 표도 같이 바꿔야 한다. 새 아이템은 새 id로 추가할 것.

#pragma once

#include <array>
#include <cstdint>
#include "protocol.h"
#include "WorldGen.h"

enum class ItemType : std::uint8_t
{
    None,
    Consumable,   // 먹으면 HP 회복. 사라진다
    Weapon,       // 장착하면 공격력 +. 한 번에 하나
};

struct ItemDef
{
    std::uint16_t id;
    ItemType type;
    std::uint16_t maxStack;
    int heal;     // Consumable
    int attack;   // Weapon
};

namespace Items
{
    constexpr std::uint16_t ACORN = 1;        // 도토리      HP +30
    constexpr std::uint16_t BLUEBERRY = 2;    // 블루베리    HP +80
    constexpr std::uint16_t TWIG = 101;       // 나뭇가지    공격력 +3
    constexpr std::uint16_t CAT_CLAW = 102;   // 고양이 발톱 공격력 +8

    constexpr ItemDef TABLE[] = {
        { ACORN,     ItemType::Consumable, 99, 30, 0 },
        { BLUEBERRY, ItemType::Consumable, 99, 80, 0 },
        { TWIG,      ItemType::Weapon,      1,  0, 3 },
        { CAT_CLAW,  ItemType::Weapon,      1,  0, 8 },
    };

    inline const ItemDef* Find(std::uint16_t id)
    {
        for (const ItemDef& def : TABLE)
        {
            if (def.id == id)
            {
                return &def;
            }
        }
        return nullptr;
    }

    // 몬스터 종류별 드랍 표. 줄마다 따로 굴린다(한 번에 여러 개가 나올 수 있다).
    struct DropRule { std::uint16_t itemId; int percent; std::uint16_t count; };

    constexpr DropRule CATERPILLAR_DROPS[] = { { ACORN, 35, 1 }, { TWIG, 6, 1 } };
    constexpr DropRule LADYBUG_DROPS[]     = { { ACORN, 30, 1 }, { BLUEBERRY, 15, 1 }, { TWIG, 10, 1 } };
    constexpr DropRule CAT_BOSS_DROPS[]    = { { BLUEBERRY, 100, 3 }, { CAT_CLAW, 40, 1 } };

    template <typename Fn>
    void ForEachDropRule(WorldGen::MonsterKind kind, Fn&& fn)
    {
        switch (kind)
        {
        case WorldGen::MonsterKind::Caterpillar: for (const DropRule& r : CATERPILLAR_DROPS) { fn(r); } break;
        case WorldGen::MonsterKind::Ladybug:     for (const DropRule& r : LADYBUG_DROPS) { fn(r); } break;
        case WorldGen::MonsterKind::CatBoss:     for (const DropRule& r : CAT_BOSS_DROPS) { fn(r); } break;
        }
    }
}

// 20칸 가방 + 무기 장착 칸. 플레이어 하나에 하나. playersLock 안에서만 만진다.
class Inventory
{
public:
    // 전부 들어가면 넣고 true. 하나라도 안 들어가면 아무것도 안 넣고 false(바닥에 그대로 남는다).
    bool TryAdd(std::uint16_t itemId, std::uint16_t count)
    {
        const ItemDef* def = Items::Find(itemId);

        if (def == nullptr || count == 0)
        {
            return false;
        }

        if (CanFit(*def, count) == false)
        {
            return false;
        }

        std::uint16_t left = count;

        // 1) 같은 아이템이 있는 칸부터 채운다
        for (InventorySlotData& slot : slots)
        {
            if (left == 0) { break; }
            if (slot.itemId != itemId || slot.count >= def->maxStack) { continue; }

            std::uint16_t room = static_cast<std::uint16_t>(def->maxStack - slot.count);
            std::uint16_t put = left < room ? left : room;
            slot.count = static_cast<std::uint16_t>(slot.count + put);
            left = static_cast<std::uint16_t>(left - put);
        }

        // 2) 빈 칸
        for (InventorySlotData& slot : slots)
        {
            if (left == 0) { break; }
            if (slot.itemId != 0) { continue; }

            std::uint16_t put = left < def->maxStack ? left : def->maxStack;
            slot.itemId = itemId;
            slot.count = put;
            left = static_cast<std::uint16_t>(left - put);
        }

        return true;
    }

    // 장착 중인 무기의 공격력. 맨손이면 0.
    int GetWeaponAttack() const
    {
        if (equippedSlot < 0 || equippedSlot >= INVENTORY_SLOTS)
        {
            return 0;
        }

        const ItemDef* def = Items::Find(slots[equippedSlot].itemId);
        return (def != nullptr && def->type == ItemType::Weapon) ? def->attack : 0;
    }

    void ConsumeOne(int index)
    {
        InventorySlotData& slot = slots[index];

        if (slot.count > 1)
        {
            --slot.count;
            return;
        }

        slot = InventorySlotData{ 0, 0 };

        if (equippedSlot == index)
        {
            equippedSlot = -1;
        }
    }

    void Clear()
    {
        slots.fill(InventorySlotData{ 0, 0 });
        equippedSlot = -1;
    }

    // DB에서 불러올 때. 모르는 아이템 id나 범위 밖 칸은 버린다(아이템을 표에서 지웠을 때 서버가 죽지 않게).
    void SetSlot(int index, std::uint16_t itemId, std::uint16_t count)
    {
        if (index < 0 || index >= INVENTORY_SLOTS || Items::Find(itemId) == nullptr || count == 0)
        {
            return;
        }
        slots[index] = InventorySlotData{ itemId, count };
    }

    void SetEquippedSlot(int index)
    {
        equippedSlot = (index >= 0 && index < INVENTORY_SLOTS) ? static_cast<std::int8_t>(index) : -1;

        if (GetWeaponAttack() == 0)
        {
            equippedSlot = -1;   // 무기가 아닌 칸을 가리키고 있으면 맨손
        }
    }

    const InventorySlotData& GetSlot(int index) const { return slots[index]; }
    std::int8_t GetEquippedSlot() const { return equippedSlot; }

    void FillPacket(SC_INVENTORY_PACKET& packet) const
    {
        packet.size = sizeof(SC_INVENTORY_PACKET);
        packet.type = PACKET_TYPE::SC_INVENTORY;
        packet.equippedWeaponSlot = equippedSlot;

        for (int i = 0; i < INVENTORY_SLOTS; ++i)
        {
            packet.slots[i] = slots[i];
        }
    }

private:
    bool CanFit(const ItemDef& def, std::uint16_t count) const
    {
        int room = 0;

        for (const InventorySlotData& slot : slots)
        {
            if (slot.itemId == 0)
            {
                room += def.maxStack;
            }
            else if (slot.itemId == def.id)
            {
                room += def.maxStack - slot.count;
            }

            if (room >= count)
            {
                return true;
            }
        }

        return false;
    }

    std::array<InventorySlotData, INVENTORY_SLOTS> slots{};
    std::int8_t equippedSlot = -1;
};
