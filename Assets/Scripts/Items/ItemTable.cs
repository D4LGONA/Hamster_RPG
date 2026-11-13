// 2026-11-13: 아이템 표(클라). 이름 · 설명 · 아이콘만 갖고 있다.
// 효과(회복량, 공격력)는 서버 Server/Item.h가 정답이고, 여기 설명 글은 그 값을 보여주기만 한다.
// id는 서버와 같아야 한다.

using System.Collections.Generic;
using UnityEngine;

public static class ItemTable
{
    public enum Kind { Consumable, Weapon }

    public class Def
    {
        public ushort Id;
        public string Name;
        public string Description;
        public Kind Kind;
        public int BaseAttack;    // 무기 기본 공격력(서버 Item.h와 같은 값. 화면 표시용)
        public string IconPath;   // Resources 경로
        private Sprite icon;

        public Sprite Icon
        {
            get
            {
                if (icon == null)
                {
                    icon = Resources.Load<Sprite>(IconPath);
                }
                return icon;
            }
        }
    }

    private static readonly Dictionary<ushort, Def> table = new Dictionary<ushort, Def>
    {
        { 1,   new Def { Id = 1,   Name = "도토리",      Description = "먹으면 HP +30",     Kind = Kind.Consumable, IconPath = "Items/item_acorn" } },
        { 2,   new Def { Id = 2,   Name = "블루베리",    Description = "먹으면 HP +80",     Kind = Kind.Consumable, IconPath = "Items/item_blueberry" } },
        { 101, new Def { Id = 101, Name = "나뭇가지",    Description = "무기 · 공격력 +3",   Kind = Kind.Weapon,     BaseAttack = 3, IconPath = "Items/item_twig" } },
        { 102, new Def { Id = 102, Name = "고양이 발톱", Description = "무기 · 공격력 +8",   Kind = Kind.Weapon,     BaseAttack = 8, IconPath = "Items/item_cat_claw" } },
    };

    // 강화 규칙(서버 Item.h Enhance와 같은 공식). 창에 미리 보여주는 용도이고 판정은 서버가 한다.
    public const int EnhanceMaxLevel = 10;
    public const int EnhanceAttackPerLevel = 3;
    public static int EnhanceCost(int level) { return 50 << level; }
    public static int EnhanceRate(int level) { return Mathf.Max(10, 100 - level * 10); }

    public static Def Find(ushort id)
    {
        return table.TryGetValue(id, out Def def) ? def : null;
    }
}
