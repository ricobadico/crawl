/**
 * @file
 * @brief Misc functions.
**/

#include "AppHdr.h"

#include "item-prop.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "artefact.h"
#include "art-enum.h"
#include "decks.h"
#include "describe.h"
#include "god-passive.h"
#include "invent.h"
#include "items.h"
#include "item-status-flag-type.h"
#include "item-use.h"
#include "libutil.h" // map_find
#include "message.h"
#include "notes.h"
#include "orb-type.h"
#include "potion-type.h"
#include "random.h"
#include "religion.h"
#include "shopping.h"
#include "skills.h"
#include "spl-util.h"
#include "spl-wpnench.h"
#include "stringutil.h"
#include "terrain.h"
#include "xom.h"

static iflags_t _full_ident_mask(const item_def& item);

// XXX: Name strings in most of the following are currently unused!
struct armour_def
{
    /// The armour_type enum of this armour type.
    armour_type         id;
    /// The name of the armour. (E.g. "robe".)
    const char         *name;
    /// The base AC value provided by the armour, before skill & enchant.
    int                 ac;
    /// The base EV penalty of the armour; used for EV, stealth, spell %, &c.
    int                 ev;
    /// The base price of the item in shops.
    int                 price;

    /// The slot the armour is equipped into; e.g. EQ_BOOTS.
    equipment_type      slot;
    /// The smallest size creature the armour will fit.
    size_type           fit_min;
    /// The largest size creature the armour will fit.
    size_type           fit_max;
    /// Whether this armour is mundane or inherently 'special', for acq.
    bool                mundane; // (special armour doesn't need egos etc)
    /// The resists, vulns, &c that this armour type gives when worn.
    armflags_t          flags;
    /// Used in body armour 'acquirement' code; higher = generated more.
    int                 acquire_weight;
};

// would be nice to lookup the name from monster_for_armour, but that
// leads to static initialization races (plus 'gold' special case)
#if TAG_MAJOR_VERSION == 34
#define DRAGON_ARMOUR(id, name, ac, evp, prc, res)                          \
    { ARM_ ## id ## _DRAGON_HIDE, "removed " name " dragon hide", 0, 0, 0,  \
      EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, false, res, 0 },             \
    { ARM_ ## id ## _DRAGON_ARMOUR, name " dragon scales",  ac, evp, prc,   \
      EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, false, res, 25 }
#else
#define DRAGON_ARMOUR(id, name, ac, evp, prc, res)                          \
    { ARM_ ## id ## _DRAGON_ARMOUR, name " dragon scales",  ac, evp, prc,   \
      EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, false, res, 25 }
#endif

// Note: the Little-Giant range is used to make armours which are very
// flexible and adjustable and can be worn by any player character...
// providing they also pass the shape test, of course.
static int Armour_index[NUM_ARMOURS];
static const armour_def Armour_prop[] =
{
    { ARM_ANIMAL_SKIN,          "animal skin",            2,   0,     3,
        EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, true, ARMF_NO_FLAGS, 333 },
    { ARM_ROBE,                 "robe",                   2,   0,     7,
        EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, true, ARMF_NO_FLAGS, 1000 },
    { ARM_LEATHER_ARMOUR,       "leather armour",         3,  -40,   20,
        EQ_BODY_ARMOUR, SIZE_SMALL,  SIZE_MEDIUM, true },

    { ARM_RING_MAIL,            "ring mail",              5,  -70,   40,
        EQ_BODY_ARMOUR, SIZE_SMALL,  SIZE_MEDIUM, true, ARMF_NO_FLAGS, 1000 },
    { ARM_SCALE_MAIL,           "scale mail",             6, -100,   40,
        EQ_BODY_ARMOUR, SIZE_SMALL,  SIZE_MEDIUM, true, ARMF_NO_FLAGS, 1000 },
    { ARM_CHAIN_MAIL,           "chain mail",             8, -150,   45,
        EQ_BODY_ARMOUR, SIZE_SMALL,  SIZE_MEDIUM, true, ARMF_NO_FLAGS, 1000 },
    { ARM_PLATE_ARMOUR,         "plate armour",          10, -180,   230,
        EQ_BODY_ARMOUR, SIZE_SMALL, SIZE_MEDIUM, true, ARMF_NO_FLAGS, 1000 },
    { ARM_CRYSTAL_PLATE_ARMOUR, "crystal plate",         14, -230,   800,
        EQ_BODY_ARMOUR, SIZE_SMALL, SIZE_MEDIUM, false, ARMF_NO_FLAGS, 500 },

    { ARM_TROLL_LEATHER_ARMOUR, "troll leather",          4,  -40,    150,
       EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, false, ARMF_REGENERATION, 25 },

    { ARM_DEEP_TROLL_LEATHER_ARMOUR, "deep troll leather",  7,  -100,    200,
       EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, false, ARMF_REGENERATION, 25 },

    { ARM_IRON_TROLL_LEATHER_ARMOUR, "iron troll leather",  11, -180,    250,
       EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, false, ARMF_REGENERATION, 25 },

    { ARM_SALAMANDER_HIDE_ARMOUR, "salamander hide",      4, -50, 250,
       EQ_BODY_ARMOUR, SIZE_LITTLE, SIZE_GIANT, false, ARMF_RES_FIRE | ARMF_BERSERK, 25},

    { ARM_CLOAK,                "cloak",                  1,   0,   45,
        EQ_CLOAK,       SIZE_LITTLE, SIZE_GIANT, true },
    { ARM_SCARF,                "scarf",                  0,   0,   50,
        EQ_CLOAK,       SIZE_LITTLE, SIZE_GIANT, true },

    { ARM_GLOVES,               "gloves",                 1,   0,   45,
        EQ_GLOVES,      SIZE_SMALL,  SIZE_MEDIUM, true },

    { ARM_CLAW,                 "clawed gauntlets",       0,   0,   90,
        EQ_GLOVES,      SIZE_SMALL,  SIZE_MEDIUM, true },

    { ARM_HELMET,               "helmet",                 1,   0,   45,
        EQ_HELMET,      SIZE_SMALL,  SIZE_MEDIUM, true },
        
    { ARM_SKULL,                "bone dragon skull",      4,   0,  230,
        EQ_HELMET,      SIZE_MEDIUM, SIZE_GIANT, false, ARMF_RES_NEG, 0 },

    { ARM_HAT,                  "hat",                    0,   0,   40,
        EQ_HELMET,      SIZE_TINY, SIZE_GIANT, true },
        
    { ARM_CAP,                  "stocking cap",           0,   0,   40,
        EQ_HELMET,      SIZE_TINY, SIZE_GIANT, true },

    // Note that barding size is compared against torso so it currently
    // needs to fit medium, but that doesn't matter as much as race
    // and shapeshift status.
    { ARM_BOOTS,                "boots",                  1,   0,   45,
        EQ_BOOTS,       SIZE_SMALL,  SIZE_MEDIUM, true },

    // Changed max. barding size to large to allow for the appropriate
    // monster types (monsters don't differentiate between torso and general).
    { ARM_CENTAUR_BARDING,      "centaur barding",        4,  -60,  230,
        EQ_BARDING,       SIZE_MEDIUM, SIZE_LARGE, true },
    { ARM_NAGA_BARDING,         "naga barding",           4,  -60,  230,
        EQ_BARDING,       SIZE_MEDIUM, SIZE_LARGE, true },

    // Note: shields use ac-value as sh-value, EV pen is used as the basis
    // to calculate adjusted shield penalty.
#if TAG_MAJOR_VERSION == 34
    { ARM_BUCKLER,              "old buckler",                3,  -8,   45,
        EQ_WEAPON1,      SIZE_LITTLE, SIZE_MEDIUM, true },
    { ARM_SHIELD,               "old shield",                 8,  -30,  45,
        EQ_WEAPON1,      SIZE_SMALL,  SIZE_BIG, true    },
    { ARM_LARGE_SHIELD,         "old large shield",          13,  -50,  45,
        EQ_WEAPON1,      SIZE_MEDIUM, SIZE_GIANT, true  },
#endif

    // Following all ARM_ entries for the benefit of util/gather_items
    DRAGON_ARMOUR(STEAM,       "steam",                   5,   0,   400,
        ARMF_RES_STEAM),
    DRAGON_ARMOUR(ACID,        "acid",                    6,  -50,  400,
        ARMF_RES_CORR),
    DRAGON_ARMOUR(QUICKSILVER, "quicksilver",             9,  -70,  600,
        ARMF_RES_MAGIC),
    DRAGON_ARMOUR(SWAMP,       "swamp",                   7,  -70,  500,
        ARMF_RES_POISON),
    DRAGON_ARMOUR(FIRE,        "fire",                    8, -110,  600,
        ard(ARMF_RES_FIRE, 2) | ARMF_VUL_COLD),
    DRAGON_ARMOUR(ICE,         "ice",                     9, -110,  600,
        ard(ARMF_RES_COLD, 2) | ARMF_VUL_FIRE),
    DRAGON_ARMOUR(PEARL,       "pearl",                  10, -110, 1000,
        ARMF_RES_NEG),
    DRAGON_ARMOUR(STORM,       "storm",                  10, -150,  800,
        ARMF_RES_ELEC),
    DRAGON_ARMOUR(SHADOW,      "shadow",                 10, -150,  800,
        ard(ARMF_STEALTH, 4) | ARMF_INVIS),
    DRAGON_ARMOUR(GOLD,        "gold",                   12, -230,  800,
        ARMF_RES_FIRE | ARMF_RES_COLD | ARMF_RES_POISON),

#undef DRAGON_ARMOUR
};

/// The standard properties for a given shield type. (E.g. falchions)
struct shield_def
{
    /// The shield_type enum for this shield type.
    int                 id;
    /// The name of this shield type. (E.g. "buckler".)
    const char         *name;

    /// Weapon Stats for Hybrids.
    /// The base damage of the weapon. (0 for non-hybrids)
    int                 dam;
    /// The base to-hit bonus of the weapon.
    int                 hit;
    /// The number of aut it takes to swing the weapon with 0 skill.
    int                 speed;
    /// A value used to determine the bonus SH smaller monsters get using this shield.
    /// Derived from a mainline function that just uses the enum; which doesn't work
    /// With the enum changed. 0 for Large, -1 for average and -2 for small shields.
    int                 size_factor;
    /// The weapon skill corresponding to this weapon's use.
    skill_type          skill;
    /// A union of vorpal_damage_type flags (slash, crush, etc)
    int                 dam_type;

    /// Global Shield Stats
    /// Shield Hand provided
    int                 sh;
    /// Encumbrance rating for casting penalties
    int                 er;

    /// The size of the smallest creature that can wield the shield.
    size_type           min_2h_size;
    /// The smallest creature that can wield the shield one-handed.
    size_type           min_1h_size;
    /// The largest creature that can wield the shield.
    size_type           max_size;


    /// Used in *some* item generation code; higher = generated more often.
    int                 commonness;
    /// Base pricing for shops, before egos, enchantment, etc.
    int                 price;
};

static int Shield_index[NUM_SHIELDS];
static const shield_def Shield_prop[] =
{
    // Standard Shields
    { SHD_BUCKLER, "buckler", 0, 0, 8, -2, SK_SHIELDS, DAMV_CRUSHING,
        3, -8, SIZE_TINY, SIZE_LITTLE, SIZE_MEDIUM, 20, 45 },
    { SHD_SHIELD, "heater shield", 0, 0, 12, -1, SK_SHIELDS, DAMV_CRUSHING,
        8, -30, SIZE_LITTLE, SIZE_SMALL, SIZE_LARGE, 20, 45 },
    { SHD_LARGE_SHIELD, "scutum", 0, 0, 14, 0, SK_SHIELDS, DAMV_CRUSHING,
        13, -50, SIZE_SMALL, SIZE_MEDIUM, SIZE_GIANT, 5, 45 },

    // Hybrid Shield/Weapons
    { SHD_SAI, "sai",
        5, 3, 12, -2, SK_SHORT_BLADES, DAMV_PIERCING,
        3, -5, SIZE_TINY, SIZE_LITTLE, SIZE_MEDIUM, 5, 60 },
    { SHD_TARGE, "targe",
        7, 0, 13, -1, SK_SHIELDS, DAMV_CRUSHING,
        7, -35, SIZE_LITTLE, SIZE_SMALL, SIZE_GIANT, 10, 45 },
    { SHD_NUNCHAKU, "nunchaku",
        9, 2, 12, -1, SK_WHIPS_FLAILS, DAMV_CRUSHING,
        4, -15, SIZE_LITTLE, SIZE_LITTLE, SIZE_LARGE, 5, 90 },
};

typedef pair<brand_type, int> brand_weight_tuple;

/// The standard properties for a given weapon type. (E.g. falchions)
struct weapon_def
{
    /// The weapon_type enum for this weapon type.
    int                 id;
    /// The name of this weapon type. (E.g. "club".)
    const char         *name;
    /// The base damage of the weapon. (Later multiplied by skill, etc)
    int                 dam;
    /// The base to-hit bonus of the weapon.
    int                 hit;
    /// The number of aut it takes to swing the weapon with 0 skill.
    int                 speed;

    /// The weapon skill corresponding to this weapon's use.
    skill_type          skill;
    /// The size of the smallest creature that can wield the weapon.
    size_type           min_2h_size;
    /// The smallest creature that can wield the weapon one-handed.
    size_type           min_1h_size;
    /// The largest creature that can wield the weapon.
    size_type           max_size;
    /// The ammo fired by the weapon; MI_NONE for non-launchers.
    missile_type        ammo;

    /// A union of vorpal_damage_type flags (slash, crush, etc)
    int                 dam_type;
    /// Used in *some* item generation code; higher = generated more often.
    int                 commonness;
    /// Used in *some* item 'acquirement' code; higher = generated more.
    int                 acquire_weight;
    /// Base pricing for shops, before egos, enchantment, etc.
    int                 price;
    /// Used in non-artefact ego item generation. If empty, default to NORMAL.
    vector<brand_weight_tuple> brand_weights;
};

/**
  * "Why do we have all these ridiculous brand tables?"

  1) The main purpose of weapon brand distribution varying across weapon type
     is to help to balance the different weapon skills against each other -
     staves and short blades getting better brands as partial compensation for
     their other drawbacks, for instance. It is true that we have other knobs
     that we also use to balance different weapon types, but they don't all
     affect things in the same way. For instance, lajatangs having very good
     brands on average partially compensates for the rarity of good staves in a
     different way from how raising their base damage would - it means that
     finding a really great staff is of more comparable rarity to finding a
     really great axe. (This is important because finding a really great weapon
     like a lajatang of speed or elec or pain is one of the ways that players
     decide to use a weapon type in the first place.) Having this knob isn't
     redundant with having base damage and delay to modify - it is similar to
     being able to adjust the rarity of different base types of weapons.

 2)  The secondary purpose of varying weapon brand distribution is to give
     different weapon skills more individual feel. For instance, if you play a
     lot of maces chars in a row, then you will get used to using a lot of
     protection weapons and you'll never see vamp except on rare randarts, and
     then when you switch to axes for a few games you'll actually find vamp
     axes with some regularity and use them and be excited about that.

     This isn't a particularly strong effect with the current distributions -
     among the four "normal" weapon skills (axes/maces/polearms/longblades),
     only the m&f distribution is particularly distinctive. But it is
     definitely a noticeable effect if you play 5 non-maces games in a row and
     follow up with 5 maces games, and it contributes to making maces feel more
     distinct.

     They could probably be simplified to a certain extent (only one set of
     brands per weapon skill, for example), but there is a reason not to
     simplify them down to just one table.
 */

/// brand weights for non-dagger shortblades (short sword & rapier)
static const vector<brand_weight_tuple> SBL_BRANDS = {
    { SPWPN_NORMAL,         33 },
    { SPWPN_VENOM,          17 },
    { SPWPN_SPEED,          10 },
    { SPWPN_ACID,            9 },
    { SPWPN_PROTECTION,      5 },
    { SPWPN_ELECTROCUTION,   6 },
    { SPWPN_HOLY_WRATH,      4 },
    { SPWPN_VAMPIRISM,       4 },
    { SPWPN_MOLTEN,          4 },
    { SPWPN_FREEZING,        4 },
    { SPWPN_DISTORTION,      2 },
    { SPWPN_ANTIMAGIC,       1 },
    { SPWPN_CHAOS,           1 },
};

/// brand weights for most m&f weapons
static const vector<brand_weight_tuple> M_AND_F_BRANDS = {
    { SPWPN_PROTECTION,     30 },
    { SPWPN_NORMAL,         28 },
    { SPWPN_HOLY_WRATH,     15 },
    { SPWPN_VORPAL,         12 },
    { SPWPN_ACID,           10 },
    { SPWPN_VENOM,           5 },
    { SPWPN_DISTORTION,      2 },
    { SPWPN_ANTIMAGIC,       1 },
    { SPWPN_PAIN,            1 },
    { SPWPN_CHAOS,           1 },
};

/// brand weights for demon weapons (whip, blade, trident)
static const vector<brand_weight_tuple> DEMON_BRANDS = {
    { SPWPN_NORMAL,         27 },
    { SPWPN_VENOM,          19 },
    { SPWPN_ELECTROCUTION,  16 },
    { SPWPN_ACID,            7 },
    { SPWPN_MOLTEN,          7 },
    { SPWPN_FREEZING,        7 },
    { SPWPN_VAMPIRISM,       7 },
    { SPWPN_PAIN,            4 },
    { SPWPN_ANTIMAGIC,       3 },
    { SPWPN_DISTORTION,      2 },
    { SPWPN_CHAOS,           2 },
};

/// brand weights for long blades.
static const vector<brand_weight_tuple> LBL_BRANDS = {
    { SPWPN_HOLY_WRATH,     22 },
    { SPWPN_NORMAL,         19 },
    { SPWPN_VORPAL,         15 },
    { SPWPN_ELECTROCUTION,  10 },
    { SPWPN_PROTECTION,      8 },
    { SPWPN_FREEZING,        5 },
    { SPWPN_MOLTEN,          5 },
    { SPWPN_ACID,            5 },
    { SPWPN_VAMPIRISM,       4 },
    { SPWPN_VENOM,           2 },
    { SPWPN_DISTORTION,      2 },
    { SPWPN_PAIN,            1 },
    { SPWPN_ANTIMAGIC,       1 },
    { SPWPN_CHAOS,           1 },
};

/// brand weights for axes.
static const vector<brand_weight_tuple> AXE_BRANDS = {
    { SPWPN_NORMAL,         30 },
    { SPWPN_VORPAL,         16 },
    { SPWPN_ELECTROCUTION,  11 },
    { SPWPN_MOLTEN,         10 },
    { SPWPN_FREEZING,       10 },
    { SPWPN_VENOM,           8 },
    { SPWPN_VAMPIRISM,       5 },
    { SPWPN_ACID,            3 },
    { SPWPN_DISTORTION,      2 },
    { SPWPN_ANTIMAGIC,       2 },
    { SPWPN_PAIN,            1 },
    { SPWPN_HOLY_WRATH,      1 },
    { SPWPN_CHAOS,           1 },
};

/// brand weights for most polearms.
static const vector<brand_weight_tuple> POLEARM_BRANDS = {
    { SPWPN_NORMAL,        35 },
    { SPWPN_VENOM,         17 },
    { SPWPN_PROTECTION,    12 },
    { SPWPN_VORPAL,         9 },
    { SPWPN_MOLTEN,         7 },
    { SPWPN_FREEZING,       7 },
    { SPWPN_VAMPIRISM,      5 },
    { SPWPN_DISTORTION,     2 },
    { SPWPN_PAIN,           2 },
    { SPWPN_ANTIMAGIC,      2 },
    { SPWPN_CHAOS,          1 },
    { SPWPN_DRAGON_SLAYING, 1 },
};

/// brand weights for most ranged weapons.
static const vector<brand_weight_tuple> RANGED_BRANDS = {
    { SPWPN_NORMAL,   57 },
    { SPWPN_MOLTEN,   16 },
    { SPWPN_FREEZING, 16 },
    { SPWPN_VORPAL,   10 },
    { SPWPN_CHAOS,     1 },
};

/// brand weights for holy (TSO-blessed) weapons.
static const vector<brand_weight_tuple> HOLY_BRANDS = {
    { SPWPN_HOLY_WRATH, 74 },
    { SPWPN_SILVER,     25 },
    { SPWPN_CHAOS,       1 },
};


static int Weapon_index[NUM_WEAPONS];
static const weapon_def Weapon_prop[] =
{
    // Maces & Flails
    { WPN_CLUB,              "club",                5,  3, 13,
        SK_MACES_STAVES, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_CRUSHING, 10, 0, 10, {} },
#if TAG_MAJOR_VERSION == 34
    { WPN_SPIKED_FLAIL,      "spiked flail",        5,  3, 13,
        SK_WHIPS_FLAILS, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_CRUSHING, 0, 0, 0, {} },
#endif
    { WPN_WHIP,              "whip",                6,  2, 11,
        SK_WHIPS_FLAILS, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_SLASHING, 4, 0, 25, {
            { SPWPN_NORMAL,        32 },
            { SPWPN_VENOM,         16 },
            { SPWPN_ELECTROCUTION, 16 },
            { SPWPN_ACID,           7 },
            { SPWPN_FREEZING,       6 },
            { SPWPN_MOLTEN,         6 },
            { SPWPN_VAMPIRISM,      5 },
            { SPWPN_PAIN,           4 },
            { SPWPN_HOLY_WRATH,     3 },
            { SPWPN_DISTORTION,     2 },
            { SPWPN_ACID,           2 },
            { SPWPN_ANTIMAGIC,      1 },
        }},
    { WPN_MACE,              "mace",                8,  3, 14,
        SK_MACES_STAVES, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_CRUSHING, 9, 10, 30, M_AND_F_BRANDS },
    { WPN_FLAIL,             "flail",              10,  0, 14,
        SK_WHIPS_FLAILS, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_CRUSHING, 8, 10, 35, M_AND_F_BRANDS },
    { WPN_MORNINGSTAR,       "morningstar",        13, -2, 15,
        SK_MACES_STAVES, SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING | DAM_PIERCE, 7, 10, 40, {
            { SPWPN_PROTECTION,     30 },
            { SPWPN_NORMAL,         15 },
            { SPWPN_HOLY_WRATH,     15 },
            { SPWPN_ACID,           10 },
            { SPWPN_VORPAL,          7 },
            { SPWPN_VENOM,           5 },
            { SPWPN_MOLTEN,          4 },
            { SPWPN_FREEZING,        4 },
            { SPWPN_DISTORTION,      2 },
            { SPWPN_ANTIMAGIC,       2 },
            { SPWPN_PAIN,            2 },
            { SPWPN_VAMPIRISM,       2 },
            { SPWPN_CHAOS,           2 },
        }},
    { WPN_DEMON_WHIP,        "demon whip",         11,  1, 11,
        SK_WHIPS_FLAILS, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_SLASHING, 0, 2, 150, DEMON_BRANDS },
    { WPN_SACRED_SCOURGE,    "sacred scourge",     12,  0, 11,
        SK_WHIPS_FLAILS, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_SLASHING, 0, 0, 200, HOLY_BRANDS },
    { WPN_DIRE_FLAIL,        "dire flail",         19, -3, 13,
        SK_WHIPS_FLAILS, SIZE_MEDIUM, SIZE_BIG, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING | DAM_PIERCE, 2, 10, 40, M_AND_F_BRANDS },
    { WPN_EVENINGSTAR,       "eveningstar",        15, -1, 15,
        SK_MACES_STAVES, SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING | DAM_PIERCE, 0, 2, 150, {
            { SPWPN_PROTECTION,     30 },
            { SPWPN_ACID,           17 },
            { SPWPN_HOLY_WRATH,     15 },
            { SPWPN_NORMAL,          8 },
            { SPWPN_VORPAL,          6 },
            { SPWPN_VENOM,           6 },
            { SPWPN_MOLTEN,          6 },
            { SPWPN_FREEZING,        6 },
            { SPWPN_DISTORTION,      2 },
            { SPWPN_ANTIMAGIC,       2 },
            { SPWPN_PAIN,            2 },
            { SPWPN_VAMPIRISM,       2 },
            { SPWPN_CHAOS,           2 },
        }},
    { WPN_GREAT_MACE,        "great mace",         25, -4, 17,
        SK_MACES_STAVES, SIZE_MEDIUM, SIZE_BIG, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 3, 10, 65, M_AND_F_BRANDS },
    { WPN_GIANT_CLUB,        "giant club",         30, -6, 16,
        SK_MACES_STAVES, SIZE_LARGE, SIZE_GIANT, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 1, 10, 17, {} },
    { WPN_GIANT_SPIKED_CLUB, "giant spiked club",  34, -7, 18,
        SK_MACES_STAVES, SIZE_LARGE, SIZE_GIANT, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING | DAM_PIERCE, 1, 10, 19, {} },

    // Short Blades
    { WPN_DAGGER,            "dagger",              4,  6, 10,
        SK_SHORT_BLADES, SIZE_LITTLE, SIZE_LITTLE, SIZE_MEDIUM, MI_NONE,
        DAMV_PIERCING, 10, 10, 20, {
            { SPWPN_VENOM,          23 },
            { SPWPN_NORMAL,         20 },
            { SPWPN_SPEED,          10 },
            { SPWPN_ACID,            9 },
            { SPWPN_PROTECTION,      6 },
            { SPWPN_ELECTROCUTION,   6 },
            { SPWPN_HOLY_WRATH,      5 },
            { SPWPN_VAMPIRISM,       4 },
            { SPWPN_MOLTEN,          4 },
            { SPWPN_FREEZING,        4 },
            { SPWPN_CHAOS,           3 },
            { SPWPN_PAIN,            2 },
            { SPWPN_DISTORTION,      1 },
            { SPWPN_ANTIMAGIC,       1 },
        }},
    { WPN_TANTO,             "tanto",               5,  6,  7,
        SK_SHORT_BLADES, SIZE_LITTLE, SIZE_LITTLE, SIZE_LARGE, MI_NONE,
        DAMV_PIERCING, 0, 2, 150, {} },
    { WPN_SHORT_SWORD,       "short sword",         6,  4, 11,
        SK_SHORT_BLADES, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_PIERCING, 8, 10, 30, SBL_BRANDS },
    { WPN_CLEAVER,          "cleaver",              7, -2, 14,
        SK_SHORT_BLADES, SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_SLICING,  4, 10, 40, SBL_BRANDS },
    { WPN_RAPIER,           "rapier",               8,  4, 12,
        SK_SHORT_BLADES, SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_PIERCING, 8, 10, 40, SBL_BRANDS },
    { WPN_KATAR,            "katar",                12,  4, 16,
        SK_SHORT_BLADES, SIZE_LITTLE, SIZE_SMALL, SIZE_BIG, MI_NONE,
        DAMV_PIERCING, 1, 2, 150, SBL_BRANDS },
    { WPN_KRIS,             "ogreish kris",         14, -3, 12,
        SK_SHORT_BLADES, SIZE_LARGE, SIZE_LARGE, SIZE_GIANT, MI_NONE,
        DAMV_PIERCING, 1, 10, 60, SBL_BRANDS },


    // Long Blades
    { WPN_FALCHION,              "falchion",               7,  2, 13,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_SLICING, 7, 10, 30, LBL_BRANDS }, // DAMV_CHOPPING...?
    { WPN_LONG_SWORD,            "long sword",            9,  1, 14,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_SLICING, 7, 10, 35, LBL_BRANDS },
    { WPN_SCIMITAR,              "scimitar",              11, 0, 14,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 6, 10, 40, LBL_BRANDS },
    { WPN_DEMON_BLADE,           "demon blade",           12, -1, 13,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 2, 150, DEMON_BRANDS },
    { WPN_EUDEMON_BLADE,         "blessed blade",         13, -2, 12,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 0, 200, HOLY_BRANDS },
    { WPN_DOUBLE_SWORD,          "double sword",          14, -1, 15,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 2, 150, LBL_BRANDS },
    { WPN_GREAT_SWORD,           "great sword",           22, -3, 17,
        SK_LONG_BLADES,  SIZE_MEDIUM, SIZE_BIG, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 6, 10, 65, LBL_BRANDS },
    { WPN_TRIPLE_SWORD,          "triple sword",          26, -4, 19,
        SK_LONG_BLADES,  SIZE_MEDIUM, SIZE_GIANT, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 2, 100, LBL_BRANDS },
#if TAG_MAJOR_VERSION == 34
    { WPN_BLESSED_FALCHION,      "old falchion",         7,  2, 13,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 0, 0, {} },
    { WPN_BLESSED_LONG_SWORD,    "old long sword",      9,  1, 14,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 0, 0, {} },
    { WPN_BLESSED_SCIMITAR,      "old scimitar",        11, -2, 14,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_LITTLE, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 0, 0, {} },
    { WPN_BLESSED_DOUBLE_SWORD, "old double sword",     14, -1, 15,
        SK_LONG_BLADES,  SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 0, 0, {} },
    { WPN_BLESSED_GREAT_SWORD,   "old great sword",     14, -3, 16,
        SK_LONG_BLADES,  SIZE_MEDIUM, SIZE_GIANT,  SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 0, 0, {} },
    { WPN_BLESSED_TRIPLE_SWORD,      "old triple sword", 17, -4, 19,
        SK_LONG_BLADES,  SIZE_MEDIUM, SIZE_GIANT,  SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 0, 0, 0, {} },
#endif

    // Axes
    { WPN_HAND_AXE,          "hatchet",             7,  3, 13,
        SK_AXES_HAMMERS,       SIZE_LITTLE, SIZE_LITTLE, SIZE_LARGE, MI_NONE,
        DAMV_CHOPPING, 6, 10, 30, AXE_BRANDS },
    { WPN_HAMMER,            "hammer",              9,  3, 15,
        SK_AXES_HAMMERS,       SIZE_LITTLE, SIZE_LITTLE, SIZE_LARGE, MI_NONE,
        DAMV_CRUSHING, 6, 12, 40, M_AND_F_BRANDS },
    { WPN_WAR_AXE,           "war axe",            11,  0, 15,
        SK_AXES_HAMMERS,       SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_CHOPPING, 7, 10, 35, AXE_BRANDS },
    { WPN_BROAD_AXE,         "broad axe",          13, -2, 16,
        SK_AXES_HAMMERS,       SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_CHOPPING, 4, 10, 40, AXE_BRANDS },
    { WPN_BATTLEAXE,         "battleaxe",          23, -4, 17,
        SK_AXES_HAMMERS,       SIZE_MEDIUM, SIZE_BIG, SIZE_GIANT, MI_NONE,
        DAMV_CHOPPING, 6, 10, 65, AXE_BRANDS },
    { WPN_EXECUTIONERS_AXE,  "executioner's axe",  27, -6, 20,
        SK_AXES_HAMMERS,       SIZE_MEDIUM, SIZE_GIANT, SIZE_GIANT, MI_NONE,
        DAMV_CHOPPING, 0, 2, 100, AXE_BRANDS },

    // Polearms
    { WPN_SPEAR,             "spear",               6,  4, 11,
        SK_POLEARMS,     SIZE_LITTLE, SIZE_LITTLE, SIZE_BIG, MI_NONE,
        DAMV_PIERCING, 8, 10, 30, {
            { SPWPN_NORMAL,        43 },
            { SPWPN_VENOM,         17 },
            { SPWPN_VORPAL,        12 },
            { SPWPN_MOLTEN,         7 },
            { SPWPN_FREEZING,       7 },
            { SPWPN_VAMPIRISM,      5 },
            { SPWPN_DRAGON_SLAYING, 5 },
            { SPWPN_DISTORTION,     2 },
            { SPWPN_PAIN,           2 },
            { SPWPN_ANTIMAGIC,      2 },
            { SPWPN_CHAOS,          2 },
        }},
    { WPN_TRIDENT,           "trident",             9,  1, 13,
        SK_POLEARMS,     SIZE_LITTLE, SIZE_MEDIUM, SIZE_BIG, MI_NONE,
        DAMV_PIERCING, 6, 10, 35, POLEARM_BRANDS },
    { WPN_HALBERD,           "halberd",            19, -3, 15,
        SK_POLEARMS,     SIZE_MEDIUM, SIZE_BIG,  SIZE_GIANT, MI_NONE,
        DAMV_CHOPPING | DAM_PIERCE, 5, 10, 40, POLEARM_BRANDS },
    { WPN_SCYTHE,            "scythe",             21, -4, 20,
        SK_POLEARMS,     SIZE_MEDIUM, SIZE_GIANT, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 2, 0, 30, POLEARM_BRANDS },
    { WPN_DEMON_TRIDENT,     "demon trident",      12,  1, 13,
        SK_POLEARMS,     SIZE_LITTLE, SIZE_MEDIUM, SIZE_BIG, MI_NONE,
        DAMV_PIERCING, 0, 2, 150, DEMON_BRANDS },
    { WPN_TRISHULA,          "trishula",           13,  0, 13,
        SK_POLEARMS,     SIZE_LITTLE, SIZE_MEDIUM, SIZE_BIG, MI_NONE,
        DAMV_PIERCING, 0, 0, 200, HOLY_BRANDS },
    { WPN_GLAIVE,            "glaive",             22, -3, 17,
        SK_POLEARMS,     SIZE_MEDIUM, SIZE_BIG, SIZE_GIANT, MI_NONE,
        DAMV_CHOPPING, 5, 10, 65, POLEARM_BRANDS },
    { WPN_BARDICHE,          "bardiche",           27, -6, 20,
        SK_POLEARMS,     SIZE_MEDIUM, SIZE_GIANT, SIZE_GIANT, MI_NONE,
        DAMV_CHOPPING, 1, 2, 90, POLEARM_BRANDS },

    // Staves
    // WPN_STAFF is for weapon stats for magical staves only.
    { WPN_STAFF,             "staff",               5,  5, 12,
        SK_MACES_STAVES,       SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 0, 0, 15, {} },
    { WPN_QUARTERSTAFF,      "quarterstaff",        15, 3, 13,
        SK_MACES_STAVES,       SIZE_LITTLE, SIZE_BIG, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 8, 10, 40, {
            { SPWPN_NORMAL,     48 },
            { SPWPN_PROTECTION, 18 },
            { SPWPN_ACID,        8 },
            { SPWPN_VORPAL,      8 },
            { SPWPN_SPEED,       8 },
            { SPWPN_DISTORTION,  2 },
            { SPWPN_PAIN,        2 },
            { SPWPN_HOLY_WRATH,  2 },
            { SPWPN_ANTIMAGIC,   2 },
            { SPWPN_CHAOS,       2 },
        }},
    { WPN_LAJATANG,          "lajatang",            24,-3, 14,
        SK_MACES_STAVES,       SIZE_LITTLE, SIZE_BIG, SIZE_GIANT, MI_NONE,
        DAMV_SLICING, 2, 2, 150, {
            { SPWPN_NORMAL,         31 },
            { SPWPN_SPEED,          12 },
            { SPWPN_ELECTROCUTION,  12 },
            { SPWPN_VAMPIRISM,      12 },
            { SPWPN_PROTECTION,      9 },
            { SPWPN_VENOM,           7 },
            { SPWPN_PAIN,            7 },
            { SPWPN_ANTIMAGIC,       4 },
            { SPWPN_DISTORTION,      3 },
            { SPWPN_CHAOS,           3 },
        }},

        // Virtuals, these aren't supposed to spawn; just part of 
        // a hack, if forced to spawn they are all plain staves.
    { WPN_AVERAGE,             "average (4/4/4)",               5,  5, 12,
        SK_MACES_STAVES,       SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 0, 0, 15,{} },

    { WPN_STRONG, "strong (8/2/2)", 5, 5, 12,
        SK_MACES_STAVES, SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 0, 0, 15, {} },
    { WPN_INTELLIGENT, "intelligent (2/8/2)", 5, 5, 12,
        SK_MACES_STAVES, SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 0, 0, 15, {} },
    { WPN_DEFT, "deft (2/2/8)", 5, 5, 12,
        SK_MACES_STAVES, SIZE_LITTLE, SIZE_MEDIUM, SIZE_GIANT, MI_NONE,
        DAMV_CRUSHING, 0, 0, 15, {} },

    // Range weapons
    { WPN_HUNTING_SLING,     "hunting sling",       9,  2, 12,
        SK_SLINGS,       SIZE_LITTLE, SIZE_LITTLE, SIZE_MEDIUM, MI_SLING_BULLET,
        DAMV_NON_MELEE, 8, 10, 15, RANGED_BRANDS },
    { WPN_FUSTIBALUS,        "fustibalus",         12, -1, 14,
        SK_SLINGS,       SIZE_LITTLE, SIZE_LITTLE, SIZE_LARGE, MI_SLING_BULLET,
        DAMV_NON_MELEE, 2, 2, 150, RANGED_BRANDS },
    { WPN_MANGONEL,        "mangonel",             22, -5, 19,
        SK_SLINGS,       SIZE_LARGE,   SIZE_GIANT, SIZE_GIANT, MI_LARGE_ROCK,
        DAMV_NON_MELEE, 4, 10, 250, RANGED_BRANDS },

    { WPN_HAND_CROSSBOW,     "hand crossbow",      12,  5, 15,
        SK_CROSSBOWS,    SIZE_LITTLE, SIZE_LITTLE, SIZE_MEDIUM, MI_BOLT,
        DAMV_NON_MELEE, 7, 10, 35, RANGED_BRANDS },
    { WPN_ARBALEST,          "arbalest",           18,  2, 19,
        SK_CROSSBOWS,    SIZE_LITTLE, SIZE_BIG, SIZE_GIANT, MI_BOLT,
        DAMV_NON_MELEE, 5, 10, 45, RANGED_BRANDS },
    { WPN_TRIPLE_CROSSBOW,   "triple crossbow",     9, -1, 22,
        SK_CROSSBOWS,    SIZE_LITTLE, SIZE_GIANT, SIZE_GIANT, MI_TRIPLE_BOLT,
        DAMV_NON_MELEE, 0, 2, 100, RANGED_BRANDS },

    { WPN_SHORTBOW,          "shortbow",           11,  2, 12,
        SK_BOWS,         SIZE_LITTLE, SIZE_GIANT, SIZE_MEDIUM, MI_ARROW,
        DAMV_NON_MELEE, 8, 10, 30, RANGED_BRANDS },
    { WPN_LONGBOW,           "longbow",            17,  0, 15,
        SK_BOWS,         SIZE_MEDIUM, SIZE_BIG, SIZE_GIANT, MI_ARROW,
        DAMV_NON_MELEE, 2, 10, 45, RANGED_BRANDS },
};

struct missile_def
{
    int         id;
    const char *name;
    int         dam;
    bool        throwable;
};

static int Missile_index[NUM_MISSILES];
static const missile_def Missile_prop[] =
{
#if TAG_MAJOR_VERSION == 34
    { MI_DART,          "dart",          2, true  },
#endif
    { MI_NEEDLE,        "needle",        0, true  },
    { MI_STONE,         "stone",         2, true  },
    { MI_SNOWBALL,      "snowball",      2, true  },
    { MI_BLADE,         "rusty metal",   2, true  },
    { MI_SKULL,         "skull",         2, true  },
    { MI_BONE,          "bone",          2, true  },
    { MI_ROOT,          "gnarled root",  2, true  },
    { MI_MUD,           "clump of mud",  2, true  },
    { MI_SEASHELL,      "seashell",      2, true  },
    { MI_ABYSS,         "chaotic dust",  2, true  },
    { MI_PANDEMONIUM,   "weird clump",   2, true  },
    { MI_OOZE,          "ball of ooze",  2, true  },
    { MI_GOLD,          "gold ore",      2, true  },
    { MI_BANDAGE,    "ball of bandages", 2, true  },
    { MI_ARROW,         "arrow",         0, false },
    { MI_BOLT,          "bolt",          0, false },
    { MI_TRIPLE_BOLT,"triplet of bolts", 0, false },
    { MI_DOUBLE_BOLT, "couple of bolts", 0, false },
    { MI_LARGE_ROCK,    "boulder",       0, false },
    { MI_SLING_BULLET,  "sling bullet",  0, false },
    { MI_JAVELIN,       "javelin",      10, true  },
    { MI_THROWING_NET,  "throwing net",  0, true  },
    { MI_TOMAHAWK,      "tomahawk",      6, true  },
    { MI_PIE,           "kream pie",     0, true  },
};

struct food_def
{
    int         id;
    const char *name;
    int         normal_nutr;
    int         carn_nutr;
    int         herb_nutr;
};

static int Food_index[NUM_FOODS];
static const food_def Food_prop[] =
{
    { FOOD_RATION,       "ration",       3400,  1900,  1900 },
    { FOOD_CHUNK,        "chunk",        1000,  1300,     0 },

#if TAG_MAJOR_VERSION == 34
    // is_real_food assumes we list FOOD_ROYAL_JELLY as the first removed
    // food here, after all the unremoved foods.
    { FOOD_UNUSED,       "buggy pizza",     0,     0,     0 },
    { FOOD_ROYAL_JELLY,  "buggy jelly",  2000,  2000,  2000 },
    { FOOD_BREAD_RATION, "buggy ration", 4400,     0,  5900 },
    { FOOD_FRUIT,        "buggy fruit",   850,     0,  1000 },
    { FOOD_AMBROSIA,     "buggy fruit",     0,     0,     0 },
    { FOOD_ORANGE,       "buggy fruit",  1000,  -300,   300 },
    { FOOD_BANANA,       "buggy fruit",  1000,  -300,   300 },
    { FOOD_LEMON,        "buggy fruit",  1000,  -300,   300 },
    { FOOD_PEAR,         "buggy fruit",   700,  -200,   200 },
    { FOOD_APPLE,        "buggy fruit",   700,  -200,   200 },
    { FOOD_APRICOT,      "buggy fruit",   700,  -200,   200 },
    { FOOD_CHOKO,        "buggy fruit",   600,  -200,   200 },
    { FOOD_RAMBUTAN,     "buggy fruit",   600,  -200,   200 },
    { FOOD_LYCHEE,       "buggy fruit",   600,  -200,   200 },
    { FOOD_STRAWBERRY,   "buggy fruit",   200,   -50,    50 },
    { FOOD_GRAPE,        "buggy fruit",   100,   -20,    20 },
    { FOOD_SULTANA,      "buggy fruit",    70,   -20,    20 },
    { FOOD_CHEESE,       "buggy fruit",  1200,     0,     0 },
    { FOOD_SAUSAGE,      "buggy fruit",  1200,   150,  -400 },
    { FOOD_BEEF_JERKY,   "buggy fruit",  1500,   200,  -200 },
    { FOOD_PIZZA,        "buggy fruit",  1500,     0,     0 },
#endif
};

// Must call this functions early on so that the above tables can
// be accessed correctly.
void init_properties()
{
    // The compiler would complain about too many initializers but not
    // about too few, so we check this ourselves.
    COMPILE_CHECK(NUM_ARMOURS  == ARRAYSZ(Armour_prop));
    COMPILE_CHECK(NUM_WEAPONS  == ARRAYSZ(Weapon_prop));
    COMPILE_CHECK(NUM_MISSILES == ARRAYSZ(Missile_prop));
    COMPILE_CHECK(NUM_SHIELDS  == ARRAYSZ(Shield_prop));
    COMPILE_CHECK(NUM_FOODS    == ARRAYSZ(Food_prop));

    for (int i = 0; i < NUM_ARMOURS; i++)
        Armour_index[ Armour_prop[i].id ] = i;

    for (int i = 0; i < NUM_WEAPONS; i++)
        Weapon_index[ Weapon_prop[i].id ] = i;

    for (int i = 0; i < NUM_SHIELDS; i++)
        Shield_index[ Shield_prop[i].id] = i;

    for (int i = 0; i < NUM_MISSILES; i++)
        Missile_index[ Missile_prop[i].id ] = i;

    for (int i = 0; i < NUM_FOODS; i++)
        Food_index[ Food_prop[i].id ] = i;
}

const set<pair<object_class_type, int> > removed_items =
{
#if TAG_MAJOR_VERSION == 34
    { OBJ_JEWELLERY, AMU_CONTROLLED_FLIGHT },
    { OBJ_JEWELLERY, RING_REGENERATION },
    { OBJ_JEWELLERY, RING_SUSTAIN_ATTRIBUTES },
    { OBJ_JEWELLERY, RING_TELEPORT_CONTROL },
    { OBJ_JEWELLERY, RING_PROTECTION_FROM_COLD },
    { OBJ_JEWELLERY, RING_SEE_INVISIBLE },
    { OBJ_JEWELLERY, RING_FLIGHT },
    { OBJ_JEWELLERY, RING_CHAOS },
    { OBJ_STAVES,    STAFF_CHANNELING },
    { OBJ_STAVES,    STAFF_CONJURATION },
    { OBJ_STAVES,    STAFF_WIZARDRY },
    { OBJ_STAVES,    STAFF_ENERGY },
    { OBJ_POTIONS,   POT_STRONG_POISON },
    { OBJ_POTIONS,   POT_BLOOD_COAGULATED },
    { OBJ_POTIONS,   POT_PORRIDGE },
    { OBJ_POTIONS,   POT_RESTORE_ABILITIES },
    { OBJ_BOOKS,     BOOK_WIZARDRY },
    { OBJ_BOOKS,     BOOK_CONTROL },
    { OBJ_BOOKS,     BOOK_BUGGY_DESTRUCTION },
    { OBJ_BOOKS,     BOOK_ENVENOMATIONS },
    { OBJ_BOOKS,     BOOK_AKASHIC_RECORD },
    { OBJ_BOOKS,     BOOK_CONJURATIONS },
    { OBJ_BOOKS,     BOOK_MANUAL },
    { OBJ_RODS,      ROD_VENOM },
    { OBJ_RODS,      ROD_WARDING },
    { OBJ_RODS,      ROD_DESTRUCTION },
    { OBJ_RODS,      ROD_SWARM },
    { OBJ_RODS,      ROD_LIGHTNING },
    { OBJ_RODS,      ROD_IGNITION },
    { OBJ_RODS,      ROD_CLOUDS },
    { OBJ_RODS,      ROD_INACCURACY },
    { OBJ_RODS,      ROD_SHADOWS },
    { OBJ_RODS,      ROD_IRON },
    { OBJ_SCROLLS,   SCR_ENCHANT_WEAPON},
    { OBJ_SCROLLS,   SCR_ENCHANT_WEAPON_II },
    { OBJ_SCROLLS,   SCR_ENCHANT_WEAPON_III },
    { OBJ_SCROLLS,   SCR_RECHARGING},
    { OBJ_SCROLLS,   SCR_REMOVE_CURSE},
    { OBJ_SCROLLS,   SCR_AMNESIA},
    { OBJ_WANDS,     WAND_MAGIC_DARTS_REMOVED },
    { OBJ_WANDS,     WAND_FROST_REMOVED },
    { OBJ_WANDS,     WAND_FIRE_REMOVED },
    { OBJ_WANDS,     WAND_COLD_REMOVED },
    { OBJ_WANDS,     WAND_DIGGING_REMOVED },
    { OBJ_WANDS,     WAND_INVISIBILITY_REMOVED },
    { OBJ_WANDS,     WAND_TELEPORTATION_REMOVED },
    { OBJ_WANDS,     WAND_SLOWING_REMOVED },
    { OBJ_WANDS,     WAND_CONFUSION_REMOVED },
    { OBJ_WANDS,     WAND_LIGHTNING_REMOVED },
    { OBJ_SCROLLS,   SCR_IDENTIFY },
    { OBJ_SCROLLS,   SCR_CURSE_WEAPON },
    { OBJ_SCROLLS,   SCR_CURSE_ARMOUR },
    { OBJ_SCROLLS,   SCR_CURSE_JEWELLERY },
    { OBJ_FOOD,      FOOD_BREAD_RATION },
    { OBJ_FOOD,      FOOD_ROYAL_JELLY },
    { OBJ_FOOD,      FOOD_UNUSED },
    { OBJ_FOOD,      FOOD_FRUIT },
#endif
    // Outside the #if because we probably won't remove these.
    { OBJ_RUNES,     RUNE_ELF },
    { OBJ_RUNES,     RUNE_FOREST },
    { OBJ_JEWELLERY, AMU_NOTHING }, // These should only spawn as uniques
    { OBJ_STAVES,    STAFF_NOTHING }, // These should only spawn as uniques
};

bool item_type_removed(object_class_type base, int subtype)
{
    return removed_items.count({ base, subtype }) != 0;
}

// Some convenient functions to hide the bit operations and create
// an interface layer between the code and the data in case this
// gets changed again. - bwr

//
// Item cursed status functions:
//
bool item_known_cursed(const item_def &item)
{
    return _full_ident_mask(item) & ISFLAG_KNOW_CURSE
           && item_ident(item, ISFLAG_KNOW_CURSE) && item.cursed();
}

// If item is a new unrand, takes a note of it and returns true.
// Otherwise, takes no action and returns false.
static bool _maybe_note_found_unrand(const item_def &item)
{
    if (is_unrandom_artefact(item) && !(item.flags & ISFLAG_SEEN))
    {
        take_note(Note(NOTE_FOUND_UNRAND, 0, 0, item.name(DESC_THE),
                       origin_desc(item)));
        return true;
    }
    return false;
}

/**
 * Is the provided item cursable? Note: this function would leak
 * information about unidentified holy wrath weapons, which is alright
 * because only Ashenzari worshippers can deliberately curse items and
 * they see all weapon egos anyway.
 *
 * @param item  The item under consideration.
 * @return      Whether the given item is a blessed weapon.
 */
bool item_is_cursable(const item_def &item, bool ignore_holy_wrath)
{
    if (!item_type_has_curses(item.base_type))
        return false;
    if (!ignore_holy_wrath
        && item.base_type == OBJ_WEAPONS
        && (get_weapon_brand(item) == SPWPN_HOLY_WRATH
            || you.duration[DUR_EXCRUCIATING_WOUNDS]
            && (item.link == you.props[PAINED_WEAPON_KEY].get_short())
            && you.props[ORIGINAL_BRAND_KEY].get_int() == SPWPN_HOLY_WRATH))
    {
        return false;
    }

    if (item.is_type(OBJ_ARMOURS, ARM_QUICKSILVER_DRAGON_ARMOUR))
        return false;

    if (item.fragile())
        return false;
    return true;
}

// Curses a random player inventory item.
bool curse_an_item(bool ignore_holy_wrath)
{
    // allowing these would enable mummy scumming
    if (have_passive(passive_t::want_curses))
    {
        mprf(MSGCH_GOD, "The curse is absorbed by %s.",
             god_name(you.religion).c_str());
        return false;
    }

    int count = 0;
    item_def *found = nullptr;

    for (auto &item : you.inv)
    {
        if (!item.defined())
            continue;

        if (!item_is_cursable(item, ignore_holy_wrath))
            continue;

        // Item is valid for cursing, so we'll give it a chance.
        count++;
        if (one_chance_in(count))
            found = &item;
    }

    // Any item to curse?
    if (!found)
        return false;

    do_curse_item(*found, false);

    return true;
}

void auto_id_inventory()
{
    for (auto &item : you.inv)
        if (item.defined())
            passive_id_item(item, false);
}

void do_curse_item(item_def &item, bool quiet)
{
    // Already cursed?
    if (bool(item.flags & ISFLAG_CURSED) && item.props.exists(CURSE_PROPS_KEY))
        return;

    if (item.is_type(OBJ_ARMOURS, ARM_QUICKSILVER_DRAGON_ARMOUR))
        return;

    if (!is_weapon(item) && item.base_type != OBJ_ARMOURS
        && item.base_type != OBJ_JEWELLERY
        && item.base_type != OBJ_SHIELDS)
    {
        return;
    }

    // Holy wrath weapons cannot be cursed.
    if ((item.base_type == OBJ_WEAPONS || item.base_type == OBJ_SHIELDS && is_hybrid(item.sub_type))
        && (get_weapon_brand(item) == SPWPN_HOLY_WRATH
            || you.duration[DUR_EXCRUCIATING_WOUNDS]
               && item.link == you.props[PAINED_WEAPON_KEY].get_short()
               && you.props[ORIGINAL_BRAND_KEY].get_int() == SPWPN_HOLY_WRATH))
    {
        if (!quiet)
            mprf("Your %s glows black briefly, but repels the curse.",
                 item.name(DESC_PLAIN).c_str());
        return;
    }

    if (!quiet)
        mprf("Your %s glows black for a moment.",
             item.name(DESC_PLAIN).c_str());

    curse_item(item);

    // Xom is amused by the player's items being cursed, especially if
    // they're worn/equipped.
    if (in_inventory(item))
    {
        int amusement = 50;

        if (item_is_equipped(item))
        {
            amusement *= 2;

            if (you.equip[EQ_WEAPON0] == item.link || you.equip[EQ_WEAPON1] == item.link)
            {
                // Redraw the weapon.
                you.wield_change = true;
            }

            ash_check_bondage();
            auto_id_inventory();
        }

        xom_is_stimulated(amusement);
    }
}

/**
 * Attempt to un-curse the given item.
 *
 * @param item      The item in question.
 * @param check_bondage     Whether to update the player's Ash bondage status.
 *                          (Ash ?rc delays this until later.)
 */
void do_uncurse_item(item_def &item, bool check_bondage)
{
    const bool in_inv = in_inventory(item);

    if (in_inv)
    {
        if (you.equip[EQ_WEAPON0] == item.link || you.equip[EQ_WEAPON1] == item.link)
        {
            // Redraw the weapon.
            you.wield_change = true;
        }
        item.flags |= ISFLAG_KNOW_CURSE;
    }
    uncurse_item(item);

    if (check_bondage && in_inv)
        ash_check_bondage();
}

/**
 * Make a net stationary (because it currently traps a victim).
 *
 * @param item The net item.
*/
void set_net_stationary(item_def &item)
{
    if (item.is_type(OBJ_MISSILES, MI_THROWING_NET))
        item.net_placed = true;
}

/**
 * Is the item stationary (unmovable)?
 *
 * Currently only carrion and nets with a trapped victim are stationary.
 * @param item The item.
 * @return  True iff the item is stationary.
*/
bool item_is_stationary(const item_def &item)
{
    return item.base_type == OBJ_CORPSES || item_is_stationary_net(item);
}

/**
 * Is the item a stationary net?
 *
 * @param item The item.
 * @return  True iff the item is a stationary net.
*/
bool item_is_stationary_net(const item_def &item)
{
    return item.is_type(OBJ_MISSILES, MI_THROWING_NET) && item.net_placed;
}

/**
 * Get the actor held in a stationary net.
 *
 * @param net A stationary net item.
 * @return  A pointer to the actor in the net, guaranteed to be non-null.
 */
actor *net_holdee(const item_def &net)
{
    ASSERT(item_is_stationary_net(net));
    // Stationary nets should not be in inventory etc.
    ASSERT_IN_BOUNDS(net.pos);
    actor * const a = actor_at(net.pos);
    ASSERTM(a, "No actor in stationary net at (%d,%d)", net.pos.x, net.pos.y);
    return a;
}

static bool _is_affordable(const item_def &item)
{
    // Temp items never count.
    if (item.flags & ISFLAG_SUMMONED)
        return false;

    // Already in our grubby mitts.
    if (in_inventory(item))
        return true;

    // Disregard shop stuff above your reach.
    if (is_shop_item(item))
        return (int)item_value(item) <= you.gold;

    // Explicitly marked by a vault.
    if (item.flags & ISFLAG_UNOBTAINABLE)
        return false;

    // On the ground or a monster has it. Violence is the answer.
    return true;
}

//
// Item identification status:
//
bool item_ident(const item_def &item, iflags_t flags)
{
    return (item.flags & flags) == flags;
}

void set_ident_flags(item_def &item, iflags_t flags)
{
    preserve_quiver_slots p;
    if ((item.flags & flags) != flags)
    {
        item.flags |= flags;
        request_autoinscribe();

        if (in_inventory(item))
        {
            shopping_list.cull_identical_items(item);
            item_skills(item, you.start_train);
        }
    }

    if (fully_identified(item))
    {
        if (notes_are_active() && !(item.flags & ISFLAG_NOTED_ID)
            && !get_ident_type(item)
            && is_interesting_item(item))
        {
            if (!_maybe_note_found_unrand(item))
            {
                // Make a note of this non-unrand item
                take_note(Note(NOTE_ID_ITEM, 0, 0, item.name(DESC_A),
                               origin_desc(item)));
            }

            // Sometimes (e.g. shops) you can ID an item before you get it;
            // don't note twice in those cases.
            item.flags |= (ISFLAG_NOTED_ID | ISFLAG_NOTED_GET);
        }
    }

    if (item.flags & ISFLAG_KNOW_TYPE && !is_artefact(item)
        && _is_affordable(item) && you.chapter != CHAPTER_NONDUNGEON_START)
    {
        if (item.base_type == OBJ_WEAPONS)
            you.seen_weapon[item.sub_type] |= 1 << item.brand;
        if (item.base_type == OBJ_ARMOURS)
            you.seen_armour[item.sub_type] |= 1 << item.brand;
        if (item.base_type == OBJ_MISCELLANY)
            you.seen_misc.set(item.sub_type);
    }
}

void unset_ident_flags(item_def &item, iflags_t flags)
{
    preserve_quiver_slots p;
    item.flags &= (~flags);
}

// Returns the mask of interesting identify bits for this item
// (e.g., scrolls don't have know-cursedness).
static iflags_t _full_ident_mask(const item_def& item)
{
    // KNOW_PROPERTIES is only relevant for artefacts, handled later.
    iflags_t flagset = ISFLAG_IDENT_MASK & ~ISFLAG_KNOW_PROPERTIES;
    switch (item.base_type)
    {
    case OBJ_FOOD:
    case OBJ_CORPSES:
    case OBJ_MISSILES:
    case OBJ_ORBS:
    case OBJ_RUNES:
    case OBJ_GOLD:
#if TAG_MAJOR_VERSION == 34
    case OBJ_RODS:
#endif
        flagset = 0;
        break;
    case OBJ_BOOKS:
#if TAG_MAJOR_VERSION == 34
        if (item.sub_type == BOOK_BUGGY_DESTRUCTION)
        {
            flagset = 0;
            break;
        }
        // Intentional fall-through.
#endif
    case OBJ_MANUALS:
    case OBJ_SCROLLS:
    case OBJ_POTIONS:
    case OBJ_WANDS:
        flagset = ISFLAG_KNOW_TYPE;
        break;
    case OBJ_STAVES:
        flagset = ISFLAG_KNOW_TYPE | ISFLAG_KNOW_CURSE;
        break;
    case OBJ_JEWELLERY:
        flagset = (ISFLAG_KNOW_CURSE | ISFLAG_KNOW_TYPE);
        if (jewellery_has_pluses(item))
            flagset |= ISFLAG_KNOW_PLUSES;
        break;
    case OBJ_MISCELLANY:
        if (is_deck(item))
            flagset = ISFLAG_KNOW_TYPE;
        else
            flagset = 0;
        break;
    case OBJ_WEAPONS:
    case OBJ_SHIELDS:
    case OBJ_ARMOURS:
        // All flags necessary for full identification.
    default:
        break;
    }

    if (item_type_known(item.base_type, item.sub_type) && !is_artefact(item))
        flagset &= (~ISFLAG_KNOW_TYPE);

    if (is_artefact(item))
        flagset |= ISFLAG_KNOW_PROPERTIES;

    return flagset;
}

bool fully_identified(const item_def& item)
{
    return item_ident(item, _full_ident_mask(item));
}

//
// Equipment description:
//
iflags_t get_equip_desc(const item_def &item)
{
    return item.flags & ISFLAG_COSMETIC_MASK;
}

void set_equip_desc(item_def &item, iflags_t flags)
{
    ASSERT((flags & ~ISFLAG_COSMETIC_MASK) == 0);

    item.flags &= ~ISFLAG_COSMETIC_MASK; // delete previous
    item.flags |= flags;
}

bool is_helmet(const item_def& item)
{
    return item.base_type == OBJ_ARMOURS && get_armour_slot(item) == EQ_HELMET;
}

bool is_hard_helmet(const item_def &item)
{
    return item.is_type(OBJ_ARMOURS, ARM_HELMET);
}

//
// Ego item functions:
//

/**
 * For a given weapon type, randomly choose an appropriate brand.
 *
 * @param wpn_type  The type of weapon in question.
 * @return          An appropriate brand. (e.g. fire, pain, venom, etc)
 *                  May be SPWPN_NORMAL.
 */
brand_type choose_weapon_brand(weapon_type wpn_type)
{
    const vector<brand_weight_tuple> weights
        = Weapon_prop[ Weapon_index[wpn_type] ].brand_weights;
    if (!weights.size())
        return SPWPN_NORMAL;

    const brand_type *brand = random_choose_weighted(weights);
    ASSERT(brand);
    return *brand;
}

// BCADNOTE: Call this more sparingly than mainline; we're allowing it to change artefact brands.
void set_item_ego_type(item_def &item, int ego_type)
{
    if ((ego_type != SPWPN_HOLY_WRATH) && (ego_type != SPWPN_SILVER))
        convert2bad(item);
    else
        convert2good(item);

    if (is_artefact(item))
    {
        CrawlVector &rap = item.props[ARTEFACT_PROPS_KEY].get_vector();

        if (rap[ARTP_BRAND].get_short() == (short)ego_type)
            return;

        rap[ARTP_BRAND] = (short)ego_type;

        // We need to reroll it's name and looks in case the base type changed.
        item.props[ARTEFACT_APPEAR_KEY].get_string() = make_artefact_name(item, true);
        item.props[ARTEFACT_NAME_KEY].get_string()   = make_artefact_name(item, false);

        item.props[CHANGED_BRAND_KEY].get_bool() = true;
    }
    else
        item.brand = ego_type;

    return;
}

brand_type get_weapon_brand(const item_def &item)
{
    if (!item.is_type(OBJ_ARMOURS, ARM_CLAW) && (!is_weapon(item) || item.base_type == OBJ_STAVES))
        return SPWPN_NORMAL;

    // Weapon ego types are "brands", so we do the randart lookup here.
    if (is_artefact(item))
        return static_cast<brand_type>(artefact_property(item, ARTP_BRAND));

    return static_cast<brand_type>(item.brand);
}

facet_type get_staff_facet(const item_def &item)
{
    if (!item.defined() || item.base_type != OBJ_STAVES)
        return SPSTF_NORMAL;

    if (is_artefact(item))
        return static_cast<facet_type>(artefact_property(item, ARTP_BRAND));

    return static_cast<facet_type>(item.brand);
}

special_missile_type get_ammo_brand(const item_def &item)
{
    // No artefact arrows yet. -- bwr
    if (item.base_type != OBJ_MISSILES || is_artefact(item))
        return SPMSL_NORMAL;

    return static_cast<special_missile_type>(item.brand);
}

special_armour_type get_armour_ego_type(const item_def &item)
{
    // Armour ego types are "brands", so we do the randart lookup here.
    if (item.base_type != OBJ_ARMOURS && !(item.base_type == OBJ_SHIELDS && !is_hybrid(item.sub_type))
        || item.is_type(OBJ_ARMOURS, ARM_CLAW))
    {
        return SPARM_NORMAL;
    }

    if (is_artefact(item))
        return static_cast<special_armour_type>(artefact_property(item, ARTP_BRAND));

    return static_cast<special_armour_type>(item.brand);
}

/// A map between monster species & their hides.
static map<monster_type, armour_type> _monster_hides = {
    { MONS_TROLL,               ARM_TROLL_LEATHER_ARMOUR },
    { MONS_DEEP_TROLL,          ARM_DEEP_TROLL_LEATHER_ARMOUR },
    { MONS_IRON_TROLL,          ARM_IRON_TROLL_LEATHER_ARMOUR },
    { MONS_SALAMANDER,          ARM_SALAMANDER_HIDE_ARMOUR },

    { MONS_FIRE_DRAGON,         ARM_FIRE_DRAGON_ARMOUR },
    { MONS_ICE_DRAGON,          ARM_ICE_DRAGON_ARMOUR },
    { MONS_STEAM_DRAGON,        ARM_STEAM_DRAGON_ARMOUR },
    { MONS_ACID_DRAGON,         ARM_ACID_DRAGON_ARMOUR },
    { MONS_STORM_DRAGON,        ARM_STORM_DRAGON_ARMOUR },
    { MONS_GOLDEN_DRAGON,       ARM_GOLD_DRAGON_ARMOUR },
    { MONS_SWAMP_DRAGON,        ARM_SWAMP_DRAGON_ARMOUR },
    { MONS_PEARL_DRAGON,        ARM_PEARL_DRAGON_ARMOUR },
    { MONS_SHADOW_DRAGON,       ARM_SHADOW_DRAGON_ARMOUR },
    { MONS_QUICKSILVER_DRAGON,  ARM_QUICKSILVER_DRAGON_ARMOUR },

    { MONS_BONE_DRAGON,         ARM_SKULL },
};

/**
 * If a monster of the given type dies, what kind of armour can it leave?
 *
 * @param mc    The class of monster in question.
 * @return      The armour_type of the given monster's armour, or NUM_ARMOURS
 *              if the monster does not leave armour.
 */
armour_type hide_for_monster(monster_type mc)
{
    return lookup(_monster_hides, mons_species(mc), NUM_ARMOURS);
}

/**
 * Return whether a piece of armour is enchantable.
 *
 * This function ignores the current enchantment level, so is still
 * true for maximally-enchanted items.
 *
 * @param item      The item being considered.
 * @return          True if the armour can have a +X enchantment.
 */
bool armour_is_enchantable(const item_def &item)
{
    ASSERT(item.base_type == OBJ_ARMOURS);
    return item.sub_type != ARM_QUICKSILVER_DRAGON_ARMOUR
        && item.sub_type != ARM_SCARF;
}

/**
 * Return the enchantment limit of a piece of armour.
 *
 * @param item      The item being considered.
 * @return          The maximum enchantment the item can hold.
 */
int armour_max_enchant(const item_def &item)
{
    ASSERT(item.base_type == OBJ_ARMOURS);

    // Unenchantables.
    if (!armour_is_enchantable(item))
        return 0;

    const int eq_slot = get_armour_slot(item);

    int max_plus = MAX_SEC_ENCHANT;
    if (eq_slot == EQ_BODY_ARMOUR
        || item.sub_type == ARM_CENTAUR_BARDING
        || item.sub_type == ARM_NAGA_BARDING
        || item.sub_type == ARM_SKULL)
    {
        max_plus = property(item, PARM_AC);
    }

    return max_plus;
}

/**
 * Find the set of all armours made from monsters.
 */
static set<armour_type> _make_hide_armour_set()
{
    set<armour_type> _hide_armour_set;
    // iter over armours created from monsters
    for (auto it: _monster_hides)
        _hide_armour_set.insert(it.second);
    return _hide_armour_set;
}
static set<armour_type> _hide_armour_set = _make_hide_armour_set();

/**
 * Is the given armour a type that drops from dead monsters (i.e. dragon or
 * troll armour)?
 *
 * @param item      The armour in question.
 * @return          Whether the given item comes from a monster corpse.
 *                  (Note that ARM_ANIMAL_SKIN doesn't come from in-game
*                    monsters (these days) and so doesn't count.)
 */
bool armour_is_hide(const item_def &item)
{
    return item.base_type == OBJ_ARMOURS
           && armour_type_is_hide(static_cast<armour_type>(item.sub_type));
}

/**
 * Is the given armour a type that drops from dead monsters (i.e. dragon or
 * troll armour)?
 *
 * @param type      The armour_type in question.
 * @return          Whether the given item comes from a monster corpse.
 *                  (Note that ARM_ANIMAL_SKIN doesn't come from in-game
 *                   monsters (these days) and so doesn't count.)
 */
bool armour_type_is_hide(armour_type type)
{
    return _hide_armour_set.count(type);
}

/**
 * Generate a map from monster armours to the monsters that drop them.
 */
static map<armour_type, monster_type> _make_hide_monster_map()
{
    map<armour_type, monster_type> hide_to_mons;
    for (auto monster_data : _monster_hides)
    {
        const armour_type hide = monster_data.second;
        const monster_type mon = monster_data.first;
        hide_to_mons[hide] = mon;
    }

    return hide_to_mons;
}

static map<armour_type, monster_type> hide_to_mons = _make_hide_monster_map();

/**
 * Find the monster that the given type of dragon/troll armour came from.
 *
 * @param arm   The type of armour in question.
 * @return      The corresponding monster type; e.g. MONS_FIRE_DRAGON for
 *              ARM_FIRE_DRAGON_ARMOUR,
 *              MONS_TROLL for ARM_TROLL_LEATHER_ARMOUR...
 */
monster_type monster_for_hide(armour_type arm)
{
    monster_type *mon = map_find(hide_to_mons, arm);
    ASSERT(mon);
    return *mon;
}

/**
 * Is this armour considered inherently 'special' for acquirement?
 *
 * @param arm   The armour item in question.
 * @return      Whether that type of armour is considered 'special'.
 */
bool armour_is_special(const item_def &item)
{
    ASSERT(item.base_type == OBJ_ARMOURS);

    return !Armour_prop[ Armour_index[item.sub_type] ].mundane;
}

/**
 * What weight does this armour type have for acquirement? (Higher = appears
 * more often.)
 *
 * @param arm   The armour item in question.
 * @return      The base weight the armour should have in acquirement, before
 *              skills & other effects are factored in.
 */
int armour_acq_weight(const armour_type armour)
{
    return Armour_prop[ Armour_index[armour] ].acquire_weight;
}

equipment_type get_armour_slot(const item_def &item)
{
    ASSERT(item.base_type == OBJ_ARMOURS);

    return Armour_prop[ Armour_index[item.sub_type] ].slot;
}

equipment_type get_armour_slot(armour_type arm)
{
    return Armour_prop[ Armour_index[arm] ].slot;
}

bool jewellery_is_amulet(const item_def &item)
{
    ASSERT(item.base_type == OBJ_JEWELLERY);

    return jewellery_is_amulet(item.sub_type);
}

bool jewellery_is_amulet(int sub_type)
{
    return sub_type >= AMU_FIRST_AMULET;
}

/**
 * How many size categories away is the given type of armour from fitting a
 * creature of the given size - and in what direction?
 *
 * @param sub_type  The type of armour in question.
 * @param size      The size of the creature in question.
 * @return          The difference between the size of the creature and the
 *                  closest size of creature that might wear the armour.
 *                  Negative if the creature is too small.
 *                  Positive if the creature is too large.
 *                  0 if the creature is just right.
 */
static int _fit_armour_size(armour_type sub_type, size_type size)
{
    const size_type min = Armour_prop[ Armour_index[sub_type] ].fit_min;
    const size_type max = Armour_prop[ Armour_index[sub_type] ].fit_max;

    if (size < min)
        return min - size;    // negative means levels too small
    else if (size > max)
        return max - size;    // positive means levels too large

    return 0;
}

/**
 * How many size categories away is the given armour from fitting a creature of
 * the given size - and in what direction?
 *
 * @param item      The armour in question.
 * @param size      The size of the creature in question.
 * @return          The difference between the size of the creature and the
 *                  closest size of creature that might wear the armour.
 *                  Negative if the creature is too small.
 *                  Positive if the creature is too large.
 *                  0 if the creature is just right.
 */
int fit_armour_size(const item_def &item, size_type size)
{
    ASSERT(item.base_type == OBJ_ARMOURS);
    return _fit_armour_size(static_cast<armour_type>(item.sub_type), size);
}

/**
 * Does the given type of armour fit a creature of the given size?
 *
 * @param sub_type  The type of armour in question.
 * @param size      The size of the creature that might wear the armour.
 * @return          Whether the armour fits based on size. (It might still not
 *                  fit for other reasons, e.g. mutations...)
 */
bool check_armour_size(armour_type sub_type, size_type size)
{
    return _fit_armour_size(sub_type, size) == 0;
}

/**
 * Does the given armour fit a creature of the given size?
 *
 * @param item      The armour in question.
 * @param size      The size of the creature that might wear the armour.
 * @return          Whether the armour fits based on size. (It might still not
 *                  fit for other reasons, e.g. mutations...)
 */
bool check_armour_size(const item_def &item, size_type size)
{
    ASSERT(item.base_type == OBJ_ARMOURS);

    return check_armour_size(static_cast<armour_type>(item.sub_type), size);
}

int wand_charge_value(int type)
{
    switch (type)
    {
    case WAND_HEAL_WOUNDS:
        return 6;

    case WAND_CLOUDS:
    case WAND_SCATTERSHOT:
    case WAND_HASTING:
        return 9;

    case WAND_ICEBLAST:
    case WAND_ACID:
    case WAND_ENSLAVEMENT:
    case WAND_ENSNARE:
        return 15;

    case WAND_POLYMORPH:
    default:
        return 24;

    case WAND_FLAME:
    case WAND_RANDOM_EFFECTS:
        return 32;
    }
}

/**
 * Is the given item a wand which is empty? Wands are normally destroyed when
 * their charges are exhausted, but empty wands can still happen through
 * transfered games.
 *
 * @param item  The item in question.
 * @return      Whether the wand is empty.
 */
bool is_known_empty_wand(const item_def &item)
{
    if (item.base_type != OBJ_WANDS)
        return false;

    return item_ident(item, ISFLAG_KNOW_TYPE) && item.charges <= 0;
}

// Returns whether an item can be enchanted further.

bool is_enchantable_item(const item_def &item)
{
    if (is_artefact(item))
        return false;

    if (item.base_type == OBJ_ARMOURS)
    {
        // Armour types that can never be enchanted.
        if (!armour_is_enchantable(item))
            return false;

        // Artefacts or highly enchanted armour cannot be enchanted.
        if (item.plus >= armour_max_enchant(item))
            return false;

        return true;
    }
    if (item.base_type == OBJ_SHIELDS)
    {
        if (is_hybrid(item.sub_type))
        {
            if (item.plus >= MAX_WPN_ENCHANT)
                return false;
            return true;
        }
        if (item.plus >= property(item, PSHD_SH))
            return false;
        return true;
    }
    if (item.base_type == OBJ_WEAPONS || item.base_type == OBJ_STAVES)
    {
        if (item.plus >= MAX_WPN_ENCHANT)
            return false;
        return true;
    }
    if (item.base_type == OBJ_WANDS)
        return true;
    
    return false;
}

//
// Weapon information and checking functions:
//

// Checks how rare a weapon is. Many of these have special routines for
// placement, especially those with a rarity of zero. Chance is out of 10.
// ^^^ vvv "rarity" is exactly the wrong term - inverted...
int weapon_rarity(int w_type)
{
    return Weapon_prop[Weapon_index[w_type]].commonness;
}

// Out of 100 for shields.
int shield_rarity(int s_type)
{
    return Shield_prop[Shield_index[s_type]].commonness;
}

// Is the given shield type a defensive shield or a weapon hybrid.
bool is_hybrid(int s_type)
{
    return Shield_prop[Shield_index[s_type]].dam > 0;
}

int get_vorpal_type(const item_def &item)
{
    int ret = DVORP_NONE;

    if (item.is_type(OBJ_ARMOURS, ARM_CLAW))
        ret = DVORP_CLAWING;

    if (item.base_type == OBJ_WEAPONS)
        ret = (Weapon_prop[Weapon_index[item.sub_type]].dam_type & DAMV_MASK);

    if (item.base_type == OBJ_SHIELDS && is_hybrid(item.sub_type))
        ret = (Shield_prop[Shield_index[item.sub_type]].dam_type & DAMV_MASK);

    return ret;
}

int get_damage_type(const item_def &item)
{
    if (item.base_type == OBJ_WEAPONS)
        return Weapon_prop[Weapon_index[item.sub_type]].dam_type & DAM_MASK;

    if (item.base_type == OBJ_SHIELDS && is_hybrid(item.sub_type))
        return Shield_prop[Shield_index[item.sub_type]].dam_type & DAM_MASK;

    return DAM_BASH;
}

static bool _does_damage_type(const item_def &item, int dam_type)
{
    return get_damage_type(item) & dam_type;
}

int single_damage_type(const item_def &item)
{
    int ret = get_damage_type(item);

    if (ret > 0)
    {
        int count = 0;

        for (int i = 1; i <= DAM_MAX_TYPE; i <<= 1)
        {
            if (!_does_damage_type(item, i))
                continue;

            if (one_chance_in(++count))
                ret = i;
        }
    }

    return ret;
}

// Give hands required to wield weapon for a torso of "size".
// Not adjusted by species or anything, which is why it's "basic".
hands_reqd_type basic_hands_reqd(const item_def &item, size_type size)
{
    if (item.base_type == OBJ_SHIELDS)
        return size >= Shield_prop[Shield_index[item.sub_type]].min_1h_size ? HANDS_ONE
                                                                            : HANDS_TWO;

    const auto wpn_type = [&item]() {
        if (item.base_type == OBJ_WEAPONS)
            return static_cast<weapon_type>(item.sub_type);
        if (item.base_type == OBJ_STAVES)
            return WPN_STAFF;
        return WPN_UNKNOWN;
    }();

    if (wpn_type == WPN_UNKNOWN)
        return HANDS_ONE;

    return size >= Weapon_prop[Weapon_index[wpn_type]].min_1h_size ? HANDS_ONE
                                                                   : HANDS_TWO;
}

hands_reqd_type hands_reqd(const actor* ac, object_class_type base_type, int sub_type)
{
    item_def item;
    item.base_type = base_type;
    item.sub_type  = sub_type;
    // This function is used for item generation only, so use the actor's
    // (player's) base size, not its current form.
    return ac->hands_reqd(item, true);
}

/**
 * Is the provided type a kind of giant club?
 *
 * @param wpn_type  The weapon_type under consideration.
 * @return          Whether it's a kind of giant club.
 */
bool is_giant_club_type(int wpn_type)
{
    return wpn_type == WPN_GIANT_CLUB
           || wpn_type == WPN_GIANT_SPIKED_CLUB;
}

/**
 * Is the provided type a kind of ranged weapon?
 *
 * @param wpn_type  The weapon_type under consideration.
 * @return          Whether it's a kind of launcher.
 */
bool is_ranged_weapon_type(int wpn_type)
{
    return Weapon_prop[Weapon_index[wpn_type]].ammo != MI_NONE;
}

/**
 * Is the provided type a kind of demon weapon?
 *
 * @param wpn_type  The weapon_type under consideration.
 * @return          Whether it's a kind of demon weapon.
 */
bool is_demonic_weapon_type(int wpn_type)
{
    return wpn_type == WPN_DEMON_BLADE
           || wpn_type == WPN_DEMON_WHIP
           || wpn_type == WPN_DEMON_TRIDENT;
}

/**
 * Is the provided type a kind of blessed weapon?
 *
 * @param wpn_type  The weapon_type under consideration.
 * @return          Whether it's a kind of blessed weapon.
 */
bool is_blessed_weapon_type(int wpn_type)
{
    return wpn_type == WPN_EUDEMON_BLADE
           || wpn_type == WPN_SACRED_SCOURGE
           || wpn_type == WPN_TRISHULA;
}

/**
 * Is the weapon type provided magical (& can't be generated in a usual way)?
 * (I.e., magic staves.)
 *
 * @param wpn_type  The weapon_type under consideration.
 * @return          Whether it's a magic staff.
 */
bool is_magic_weapon_type(int wpn_type)
{
    return wpn_type == WPN_STAFF;
}


bool is_melee_weapon(const item_def &weapon)
{
    return is_weapon(weapon) && !is_range_weapon(weapon);
}

/**
 * Is the provided item a demon weapon?
 *
 * @param item  The item under consideration.
 * @return      Whether the given item is a demon weapon.
 */
bool is_demonic(const item_def &item)
{
    return item.base_type == OBJ_WEAPONS
           && is_demonic_weapon_type(item.sub_type);
}

/**
 * Is the provided item a blessed weapon? (Not just holy wrath.)
 *
 * @param item  The item under consideration.
 * @return      Whether the given item is a blessed weapon.
 */
bool is_blessed(const item_def &item)
{
    return item.base_type == OBJ_WEAPONS
            && is_blessed_weapon_type(item.sub_type);
}

bool is_blessed_convertible(const item_def &item)
{
    return !is_artefact(item)
           && (item.base_type == OBJ_WEAPONS
               && (is_demonic(item)
                   || is_blessed(item)));
}

static map<weapon_type, weapon_type> _holy_upgrades =
{
    { WPN_DEMON_BLADE, WPN_EUDEMON_BLADE },
    { WPN_DEMON_WHIP, WPN_SACRED_SCOURGE },
    { WPN_DEMON_TRIDENT, WPN_TRISHULA },
};

bool convert2good(item_def &item)
{
    if (item.base_type != OBJ_WEAPONS)
        return false;

    if (auto repl = map_find(_holy_upgrades, (weapon_type) item.sub_type))
    {
        item.sub_type = *repl;
        return true;
    }

    return false;
}

bool convert2bad(item_def &item)
{
    if (item.base_type != OBJ_WEAPONS)
        return false;

    auto it = find_if(begin(_holy_upgrades), end(_holy_upgrades),
                      [&item](const pair<const weapon_type, weapon_type> elt)
                      {
                          return elt.second == item.sub_type;
                      });
    if  (it != end(_holy_upgrades))
    {
        item.sub_type = it->first;
        return true;
    }

    return false;
}

bool is_blessable_item(const item_def &item)
{
    if (item.cursed())
        return true;

    if (is_artefact(item))
        return false;

    switch (item.base_type)
    {
    case OBJ_ARMOURS:
        if (item.sub_type == ARM_QUICKSILVER_DRAGON_ARMOUR)
            return false;
        // fallthrough . . . 
    case OBJ_SHIELDS:
    case OBJ_STAVES:
    case OBJ_WEAPONS:
        return true;

    default:
        return false;
    }
}

bool is_brandable_weapon(const item_def &wpn, bool allow_ranged, bool divine)
{
    if (is_artefact(wpn) && !divine)
        return false;

    if (wpn.base_type == OBJ_WEAPONS)
    {
        if (!allow_ranged && is_range_weapon(wpn))
            return false;

        // Only gods can rebrand blessed weapons, and they revert back to their
        // old base type in the process.
        if (is_blessed(wpn) && !divine)
            return false;

        return true;
    }

    if (wpn.base_type == OBJ_SHIELDS && is_hybrid(wpn.sub_type))
        return true;

    if (wpn.is_type(OBJ_ARMOURS, ARM_CLAW))
        return true;

    return false;
}


skill_type staff_magic_skill(const item_def &item)
{
    switch (item.sub_type)
    {
    case STAFF_FIRE:
        return SK_FIRE_MAGIC;
    case STAFF_COLD:
        return SK_ICE_MAGIC;
    case STAFF_AIR:
        return SK_AIR_MAGIC;
    case STAFF_DEATH:
        return SK_NECROMANCY;
    case STAFF_EARTH:
        return SK_EARTH_MAGIC;
    case STAFF_POISON:
        return SK_POISON_MAGIC;
    case STAFF_SUMMONING:
        return SK_SUMMONINGS;
    case STAFF_TRANSMUTATION:
        return SK_TRANSMUTATIONS;
    default:
        return SK_EVOCATIONS;
    }
}

// Currently only used for a single ng-restr call
skill_type wpn_skill(weapon_type wpn)
{
    
    return Weapon_prop[Weapon_index[wpn]].skill;
}

/**
 * Returns the skill used by the given item to attack.
 *
 * @param item  The item under consideration.
 * @return      The skill used to attack with the given item; defaults to
 *              SK_FIGHTING if no melee or ranged skill applies.
 */
skill_type item_attack_skill(const item_def &item)
{
    if (item.base_type == OBJ_WEAPONS)
        return Weapon_prop[Weapon_index[item.sub_type]].skill;
    else if (item.base_type == OBJ_STAVES)
        return SK_MACES_STAVES;
    else if (item.base_type == OBJ_SHIELDS)
        return Shield_prop[Shield_index[item.sub_type]].skill;

    // This is used to mark that only fighting applies.
    return SK_FIGHTING;
}

/**
 * Returns the skill used by the given item type to attack.
 *
 * @param wclass  The item base type under consideration.
 * @param wtype   The item subtype under consideration.
 * @return      The skill used to attack with the given item type; defaults to
 *              SK_FIGHTING if no melee skill applies.
 */
skill_type item_attack_skill(object_class_type wclass, int wtype)
{
    item_def    wpn;

    wpn.base_type = wclass;
    wpn.sub_type = wtype;

    return item_attack_skill(wpn);
}

// True if item is a staff that deals extra damage based on Evocations skill.
bool staff_uses_evocations(const item_def &item)
{
    if (item.base_type != OBJ_STAVES)
        return false;

    return true;
}

bool item_skills(const item_def &item, set<skill_type> &skills)
{
    if (item.is_type(OBJ_BOOKS, BOOK_MANUAL))
    {
        const skill_type skill = static_cast<skill_type>(item.plus);
        if (training_restricted(skill))
            skills.insert(skill);
    }

    // Jewellery with evokable abilities, wands and similar unwielded
    // evokers allow training.
    if (item_is_evokable(item, false, false, false, true)
        && !is_deck(item)
        || item.base_type == OBJ_JEWELLERY && gives_ability(item))
    {
        skills.insert(SK_EVOCATIONS);
    }

    // Shields and abilities on armours allow training as long as your species
    // can wear them.
    if (item.base_type == OBJ_ARMOURS && can_wear_armour(item, false, true))
    {
        if (is_shield(item))
            skills.insert(SK_SHIELDS);

        if (gives_ability(item))
            skills.insert(SK_EVOCATIONS);
    }

    // Weapons and staves allow training as long as your species can wield them.
    if (!you.could_wield(item, true, true))
        return !skills.empty();

    if (item_is_evokable(item, false, false, false, false)
        && !is_deck(item)
        || staff_uses_evocations(item)
        || item.base_type == OBJ_WEAPONS && gives_ability(item))
    {
        skills.insert(SK_EVOCATIONS);
    }

    skill_type sk = item_attack_skill(item);
    if (sk != SK_FIGHTING)
        skills.insert(sk);

    return !skills.empty();
}

bool is_wieldable_weapon(weapon_type wpn, size_type size)
{
    if (Weapon_prop[Weapon_index[wpn]].min_2h_size <= size)
        return Weapon_prop[Weapon_index[wpn]].max_size >= size;
    return false;
}

/**
 * Checks if the provided weapon is wieldable by a creature of the given size.
 *
 * @param item      The weapon in question.
 * @param size      The size of the creature trying to wield the weapon.
 * @return          Whether a creature of the given size can wield the weapon.
 */
bool is_weapon_wieldable(const item_def &item, size_type size)
{
    if (item.is_type(OBJ_MISCELLANY, MISC_LANTERN_OF_SHADOWS))
        return true;

    if (is_unrandom_artefact(item, UNRAND_KLOWN))
        return true;

    if (item.base_type == OBJ_SHIELDS)
    {
        if (Shield_prop[Shield_index[item.sub_type]].min_1h_size > size)
            return false;
        if (Shield_prop[Shield_index[item.sub_type]].max_size < size)
            return false;
        return true;
    }

    const weapon_type subtype = OBJ_STAVES == item.base_type ? WPN_STAFF
                                                             : (weapon_type)item.sub_type;

    if (item.base_type == OBJ_WEAPONS || item.base_type == OBJ_STAVES)
        return is_wieldable_weapon(subtype, size);

    return false; // Shouldn't get here, but just in case.
}

//
// Launcher and ammo functions:
//
missile_type fires_ammo_type(const item_def &item)
{
    if (item.base_type != OBJ_WEAPONS)
        return MI_NONE;

    if (is_unrandom_artefact(item, UNRAND_KLOWN))
        return MI_PIE;

    return Weapon_prop[Weapon_index[item.sub_type]].ammo;
}

bool is_range_weapon(const item_def &item)
{
    return is_weapon(item) && is_ranged_weapon_type(item.sub_type);
}

const char *ammo_name(missile_type ammo)
{
    return ammo < 0 || ammo >= NUM_MISSILES ? "eggplant"
           : Missile_prop[ Missile_index[ammo] ].name;
}

const char *ammo_name(const item_def &bow)
{
    ASSERT(is_range_weapon(bow));
    return ammo_name(fires_ammo_type(bow));
}

const char *ammo_name(const weapon_type bow)
{
    missile_type mi = Weapon_prop[Weapon_index[bow]].ammo;
    ASSERT(mi != MI_NONE);
    return ammo_name(mi);
}

// Returns true if item has an associated launcher.
bool has_launcher(const item_def &ammo)
{
    ASSERT(ammo.base_type == OBJ_MISSILES);
    return !(Missile_prop[Missile_index[ammo.sub_type]].throwable);
}

// Returns true if item can be reasonably thrown without a launcher.
bool is_throwable(const actor *actor, const item_def &wpn, bool force)
{
    if (wpn.base_type != OBJ_MISSILES)
        return false;

    const size_type bodysize = actor->body_size();

    if (!force)
    {
        if (bodysize < SIZE_MEDIUM
            && wpn.sub_type == MI_JAVELIN)
        {
            return false;
        }
    }

    return Missile_prop[Missile_index[wpn.sub_type]].throwable;
}

// Decide if something is launched or thrown.
launch_retval is_launched(const actor *actor, const item_def *launcher0,
                          const item_def *launcher1, const item_def &missile)
{
    if (missile.base_type != OBJ_MISSILES)
        return launch_retval::FUMBLED;

    if (launcher0 && missile.launched_by(*launcher0))
        return launch_retval::LAUNCHED;

    if (launcher1 && missile.launched_by(*launcher1))
        return launch_retval::LAUNCHED;

    return is_throwable(actor, missile) ? launch_retval::THROWN : launch_retval::FUMBLED;
}

/**
 * Returns the base damage of a given item type.
 *
 * @param missile_type      The missile type to get the damage for.
 * @return                  The base damage of the given missile type.
 */
int ammo_type_damage(int missile_type)
{
    return Missile_prop[ Missile_index[missile_type] ].dam;
}


//
// Reaching functions:
//
reach_type weapon_reach(const item_def &item)
{
    if (is_unrandom_artefact(item, UNRAND_RIFT))
        return REACH_THREE;
    if (item_attack_skill(item) == SK_POLEARMS)
        return REACH_TWO;
    return REACH_NONE;
}

//
// Macguffins
//
bool item_is_unique_rune(const item_def &item)
{
    return item.base_type == OBJ_RUNES
           && item.sub_type != RUNE_DEMONIC
           && item.sub_type != RUNE_ABYSSAL;
}

bool item_is_orb(const item_def &item)
{
    return item.is_type(OBJ_ORBS, ORB_ZOT);
}

bool item_is_horn_of_geryon(const item_def &item)
{
    return item.is_type(OBJ_MISCELLANY, MISC_HORN_OF_GERYON);
}

bool item_is_spellbook(const item_def &item)
{
    return item.base_type == OBJ_BOOKS
#if TAG_MAJOR_VERSION == 34
           && item.sub_type != BOOK_BUGGY_DESTRUCTION
           && item.sub_type != BOOK_MANUAL;
#endif
}

//
// Ring functions:

// Returns whether jewellery has plusses.
bool jewellery_has_pluses(const item_def &item)
{
    ASSERT(item.base_type == OBJ_JEWELLERY);

    return false;
}

// Returns true if having two rings of the same type on at the same
// has more effect than just having one on.
bool ring_has_stackable_effect(const item_def &item)
{
    ASSERT(item.base_type == OBJ_JEWELLERY);
    ASSERT(!jewellery_is_amulet(item));

    if (!item_type_known(item))
        return false;

    if (jewellery_has_pluses(item))
        return true;

    switch (item.sub_type)
    {
    case RING_LIFE_PROTECTION:
    case RING_STEALTH:
    case RING_ATTENTION:
    case RING_WIZARDRY:
    case RING_FIRE:
    case RING_ICE:
    case RING_SLAYING:
    case RING_PROTECTION:
    case RING_EVASION:
    case RING_STRENGTH:
    case RING_INTELLIGENCE:
    case RING_DEXTERITY:
        return true;

    default:
        break;
    }

    return false;
}

//
// Food functions:
//
#if TAG_MAJOR_VERSION == 34
bool is_real_food(food_type food)
{
    return food < NUM_FOODS && Food_index[food] < Food_index[FOOD_UNUSED];
}

#endif
bool is_blood_potion(const item_def &item)
{
    if (item.base_type != OBJ_POTIONS)
        return false;

    return item.sub_type == POT_BLOOD
#if TAG_MAJOR_VERSION == 34
           || item.sub_type == POT_BLOOD_COAGULATED
#endif
            ;
}

bool food_is_meaty(int food_type)
{
    ASSERTM(food_type >= 0 && food_type < NUM_FOODS,
            "Bad food type %d (NUM_FOODS = %d)",
            food_type, NUM_FOODS);

    return Food_prop[Food_index[food_type]].herb_nutr == 0;
}

bool food_is_meaty(const item_def &item)
{
    if (item.base_type != OBJ_FOOD)
        return false;

    return food_is_meaty(item.sub_type);
}

int food_value(const item_def &item)
{
    ASSERT(item.defined() && item.base_type == OBJ_FOOD);

    const food_def &food = Food_prop[Food_index[item.sub_type]];

    return you.get_mutation_level(MUT_HERBIVOROUS) > 0 ? food.herb_nutr
         : you.get_mutation_level(MUT_CARNIVOROUS) > 0 ? food.carn_nutr
                                                       : food.normal_nutr;
}

//
// Generic item functions:
//
int get_armour_res_fire(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS || (arm.base_type == OBJ_SHIELDS && !is_hybrid(arm.sub_type)));

    int res = 0;

    // intrinsic armour abilities
    if (arm.base_type == OBJ_ARMOURS)
        res += armour_type_prop(arm.sub_type, ARMF_RES_FIRE);

    // check ego resistance
    const int ego = get_armour_ego_type(arm);
    if (ego == SPARM_FIRE_RESISTANCE || ego == SPARM_RESISTANCE)
        res += 1;

    if (check_artp && is_artefact(arm))
        res += artefact_property(arm, ARTP_FIRE);

    return res;
}

int get_armour_res_cold(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS || (arm.base_type == OBJ_SHIELDS && !is_hybrid(arm.sub_type)));

    int res = 0;

    // intrinsic armour abilities
    if (arm.base_type == OBJ_ARMOURS)
        res += armour_type_prop(arm.sub_type, ARMF_RES_COLD);

    // check ego resistance
    const int ego = get_armour_ego_type(arm);
    if (ego == SPARM_COLD_RESISTANCE || ego == SPARM_RESISTANCE)
        res += 1;

    if (check_artp && is_artefact(arm))
        res += artefact_property(arm, ARTP_COLD);

    return res;
}

int get_armour_res_poison(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS || (arm.base_type == OBJ_SHIELDS && !is_hybrid(arm.sub_type)));

    int res = 0;

    // intrinsic armour abilities
    if (arm.base_type == OBJ_ARMOURS)
        res += armour_type_prop(arm.sub_type, ARMF_RES_POISON);

    // check ego resistance
    if (get_armour_ego_type(arm) == SPARM_POISON_RESISTANCE)
        res += 1;

    if (check_artp && is_artefact(arm))
        res += artefact_property(arm, ARTP_POISON);

    return res;
}

int get_armour_res_elec(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS || (arm.base_type == OBJ_SHIELDS && !is_hybrid(arm.sub_type)));

    int res = 0;

    if (arm.brand == SPARM_INSULATION)
        res++;

    // intrinsic armour abilities
    if (arm.base_type == OBJ_ARMOURS)
        res += armour_type_prop(arm.sub_type, ARMF_RES_ELEC);

    if (check_artp && is_artefact(arm))
        res += artefact_property(arm, ARTP_ELECTRICITY);

    return res;
}

int get_armour_life_protection(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS || (arm.base_type == OBJ_SHIELDS && !is_hybrid(arm.sub_type)));

    int res = 0;

    // intrinsic armour abilities
    if (arm.base_type == OBJ_ARMOURS)
        res += armour_type_prop(arm.sub_type, ARMF_RES_NEG);

    // check for ego resistance
    if (get_armour_ego_type(arm) == SPARM_POSITIVE_ENERGY)
        res += 1;

    if (check_artp && is_artefact(arm))
        res += artefact_property(arm, ARTP_NEGATIVE_ENERGY);

    return res;
}

int get_armour_res_magic(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS || (arm.base_type == OBJ_SHIELDS && !is_hybrid(arm.sub_type)));

    int res = 0;

    // intrinsic armour abilities
    if (arm.base_type == OBJ_ARMOURS)
        res += armour_type_prop(arm.sub_type, ARMF_RES_MAGIC) * MR_PIP;

    // check for ego resistance
    if (get_armour_ego_type(arm) == SPARM_MAGIC_RESISTANCE)
        res += MR_PIP;

    if (check_artp && is_artefact(arm))
        res += MR_PIP * artefact_property(arm, ARTP_MAGIC_RESISTANCE);

    return res;
}

bool get_armour_improved_vision(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS);

    // check for ego resistance
    if (get_armour_ego_type(arm) == SPARM_IMPROVED_VISION)
        return true;

    if (check_artp && is_artefact(arm))
        return artefact_property(arm, ARTP_IMPROVED_VISION);

    return false;
}

int get_armour_res_corr(const item_def &arm)
{
    ASSERT(arm.base_type == OBJ_ARMOURS);

    // intrinsic armour abilities
    return armour_type_prop(arm.sub_type, ARMF_RES_CORR);
}

int get_armour_repel_missiles(const item_def &arm, bool check_artp)
{
    ASSERT(arm.base_type == OBJ_ARMOURS);

    // check for ego resistance
    if (get_armour_ego_type(arm) == SPARM_REPULSION)
        return true;

    if (check_artp && is_artefact(arm))
        return artefact_property(arm, ARTP_RMSL);

    return false;
}

int get_armour_cloud_immunity(const item_def &arm)
{
    ASSERT(arm.base_type == OBJ_ARMOURS);

    if (get_armour_ego_type(arm) == SPARM_CLOUD_IMMUNE)
        return true;

    return false;
}

int get_jewellery_res_fire(const item_def &ring, bool check_artp)
{
    ASSERT(ring.base_type == OBJ_JEWELLERY);

    int res = 0;

    // intrinsic jewellery abilities
    switch (ring.sub_type)
    {
    case RING_FIRE:
        res += 1;
        break;
    case RING_ICE:
        res -= 1;
        break;
    default:
        break;
    }

    if (check_artp && is_artefact(ring))
        res += artefact_property(ring, ARTP_FIRE);

    return res;
}

int get_jewellery_res_cold(const item_def &ring, bool check_artp)
{
    ASSERT(ring.base_type == OBJ_JEWELLERY);

    int res = 0;

    // intrinsic jewellery abilities
    switch (ring.sub_type)
    {
    case RING_ICE:
        res += 1;
        break;
    case RING_FIRE:
        res -= 1;
        break;
    default:
        break;
    }

    if (check_artp && is_artefact(ring))
        res += artefact_property(ring, ARTP_COLD);

    return res;
}

int get_jewellery_res_poison(const item_def &ring, bool check_artp)
{
    ASSERT(ring.base_type == OBJ_JEWELLERY);

    int res = 0;

    if (ring.sub_type == RING_POISON_RESISTANCE)
        res += 1;

    if (check_artp && is_artefact(ring))
        res += artefact_property(ring, ARTP_POISON);

    return res;
}

int get_jewellery_res_elec(const item_def &ring, bool check_artp)
{
    ASSERT(ring.base_type == OBJ_JEWELLERY);

    int res = 0;

    if (check_artp && is_artefact(ring))
        res += artefact_property(ring, ARTP_ELECTRICITY);

    return res;
}

int get_jewellery_life_protection(const item_def &ring, bool check_artp)
{
    ASSERT(ring.base_type == OBJ_JEWELLERY);

    int res = 0;

    // check for ego resistance
    if (ring.sub_type == RING_LIFE_PROTECTION)
        res += 1;

    if (check_artp && is_artefact(ring))
        res += artefact_property(ring, ARTP_NEGATIVE_ENERGY);

    return res;
}

int get_jewellery_res_magic(const item_def &ring, bool check_artp)
{
    ASSERT(ring.base_type == OBJ_JEWELLERY);

    int res = 0;

    if (ring.sub_type == RING_PROTECTION_FROM_MAGIC)
        res += 40;

    if (check_artp && is_artefact(ring))
        res += 40 * artefact_property(ring, ARTP_MAGIC_RESISTANCE);

    return res;
}

bool get_jewellery_improved_vision(const item_def &ring, bool check_artp)
{
    ASSERT(ring.base_type == OBJ_JEWELLERY);

    if (check_artp && is_artefact(ring))
        return artefact_property(ring, ARTP_IMPROVED_VISION);

    return false;
}

int weapon_damage(const item_def &item)
{
    if (item.base_type == OBJ_SHIELDS)
        return property(item, PSHD_DAMAGE);

    return property(item, PWPN_DAMAGE);
}

int weapon_delay(const item_def &item)
{
    int mod = 0;
    
    if (item.base_type == OBJ_SHIELDS)
    {
        if (!is_hybrid(item.sub_type))
            mod = (SIZE_MEDIUM - you.body_size(PSIZE_TORSO, true)) * 2;
        return property(item, PSHD_SPEED + mod);
    }

    return property(item, PWPN_SPEED);
}

int property(const item_def &item, int prop_type)
{
    weapon_type weapon_sub;

    switch (item.base_type)
    {
    case OBJ_ARMOURS:
        return armour_prop(item.sub_type, prop_type);

    case OBJ_SHIELDS:
        switch (prop_type)
        {
        case PSHD_SH:
            return Shield_prop[Shield_index[item.sub_type]].sh;
        case PSHD_ER:
            return Shield_prop[Shield_index[item.sub_type]].er;
        case PSHD_SPEED:
            return Shield_prop[Shield_index[item.sub_type]].speed;
        case PSHD_DAMAGE:
            return Shield_prop[Shield_index[item.sub_type]].dam;
        case PSHD_HIT:
            return Shield_prop[Shield_index[item.sub_type]].hit;
        case PSHD_SIZE:
            return Shield_prop[Shield_index[item.sub_type]].size_factor;
        }

    case OBJ_WEAPONS:
        if (is_unrandom_artefact(item))
        {
            switch (prop_type)
            {
            case PWPN_DAMAGE:
                return Weapon_prop[ Weapon_index[item.sub_type] ].dam
                       + artefact_property(item, ARTP_BASE_DAM);
            case PWPN_HIT:
                return Weapon_prop[ Weapon_index[item.sub_type] ].hit
                       + artefact_property(item, ARTP_BASE_ACC);
            case PWPN_SPEED:
                return Weapon_prop[ Weapon_index[item.sub_type] ].speed
                       + artefact_property(item, ARTP_BASE_DELAY);
            }
        }
        if (prop_type == PWPN_DAMAGE)
            return Weapon_prop[ Weapon_index[item.sub_type] ].dam;
        else if (prop_type == PWPN_HIT)
            return Weapon_prop[ Weapon_index[item.sub_type] ].hit;
        else if (prop_type == PWPN_SPEED)
            return Weapon_prop[ Weapon_index[item.sub_type] ].speed;
        else if (prop_type == PWPN_ACQ_WEIGHT)
            return Weapon_prop[ Weapon_index[item.sub_type] ].acquire_weight;
        break;

    case OBJ_MISSILES:
        if (prop_type == PWPN_DAMAGE)
            return Missile_prop[ Missile_index[item.sub_type] ].dam;
        break;

    case OBJ_STAVES:
        weapon_sub = WPN_STAFF;

        if (prop_type == PWPN_DAMAGE)
            return Weapon_prop[ Weapon_index[weapon_sub] ].dam;
        else if (prop_type == PWPN_HIT)
            return Weapon_prop[ Weapon_index[weapon_sub] ].hit;
        else if (prop_type == PWPN_SPEED)
            return Weapon_prop[ Weapon_index[weapon_sub] ].speed;
        break;

    default:
        break;
    }

    return 0;
}

/**
 * Find a given property of a given armour_type.
 *
 * @param armour        The armour_type in question (e.g. ARM_ROBE)
 * @param prop_type     The property being requested (e.g. PARM_AC)
 * @return              The property value, if the prop_type is valid.
 *                      Otherwise, 0 (!?)
 *                      ^ hopefully never comes up...
 */
int armour_prop(int armour, int prop_type)
{
    switch (prop_type)
    {
        case PARM_AC:
            return Armour_prop[ Armour_index[armour] ].ac;
        case PARM_EVASION:
            return Armour_prop[ Armour_index[armour] ].ev;
        default:
            return 0; // !?
    }
}

// Returns true if item is evokable.
bool gives_ability(const item_def &item)
{
    if (!item_type_known(item))
        return false;

    switch (item.base_type)
    {
    case OBJ_WEAPONS:
        break;
    case OBJ_JEWELLERY:
        if (item.sub_type == AMU_RAGE)
            return true;
        break;
    case OBJ_ARMOURS:
    {
        const equipment_type eq = get_armour_slot(item);
        if (eq == EQ_NONE)
            return false;
        const special_armour_type ego = get_armour_ego_type(item);

        if (ego == SPARM_INVISIBILITY)
            return true;
        break;
    }
    default:
        return false;
    }

    if (!is_artefact(item))
        return false;

    // Check for evokable randart properties.
    for (int rap = ARTP_INVISIBLE; rap <= ARTP_BERSERK; rap++)
        if (artefact_property(item, static_cast<artefact_prop_type>(rap)))
            return true;

    // Unrands that grant an evokable ability.
    if (is_unrandom_artefact(item, UNRAND_THIEF)
        || is_unrandom_artefact(item, UNRAND_RATSKIN_CLOAK)
        || is_unrandom_artefact(item, UNRAND_RCLOUDS))
    {
        return true;
    }

    return false;
}

// Returns true if the item confers an intrinsic that is shown on the % screen.
bool gives_resistance(const item_def &item)
{
    if (!item_type_known(item))
        return false;

    switch (item.base_type)
    {
    case OBJ_WEAPONS:
        break;
    case OBJ_JEWELLERY:
        if (!jewellery_is_amulet(item))
        {
            if (   item.sub_type == RING_POISON_RESISTANCE
                || item.sub_type == RING_LIFE_PROTECTION
                || item.sub_type == RING_PROTECTION_FROM_MAGIC
                || item.sub_type == RING_FIRE
                || item.sub_type == RING_ICE)
            {
                return true;
            }
        }
        else
        {
            if (item.sub_type != AMU_RAGE && item.sub_type != AMU_INACCURACY)
                return true;
        }
        break;
    case OBJ_ARMOURS:
    {
        const equipment_type eq = get_armour_slot(item);
        if (eq == EQ_NONE)
            return false;

        const int ego = get_armour_ego_type(item);
        if (ego >= SPARM_FIRE_RESISTANCE && ego <= SPARM_IMPROVED_VISION
            || ego == SPARM_RESISTANCE || ego == SPARM_POSITIVE_ENERGY)
        {
            return true;
        }
        break;
    }
    case OBJ_STAVES:
        if (item.sub_type >= STAFF_FIRE && item.sub_type <= STAFF_POISON
            || item.sub_type == STAFF_AIR
            || item.sub_type == STAFF_DEATH)
        {
            return true;
        }
        return false;
    default:
        return false;
    }

    if (!is_artefact(item))
        return false;

    // Check for randart resistances.
    for (int rap = ARTP_FIRE; rap <= ARTP_BERSERK; rap++)
    {
        if (rap == ARTP_MAGIC_RESISTANCE || rap >= ARTP_INVISIBLE)
            continue;

        if (artefact_property(item, static_cast<artefact_prop_type>(rap)))
            return true;
    }

    return false;
}

bool item_is_jelly_edible(const item_def &item)
{
    // Don't eat mimics.
    if (item.flags & ISFLAG_MIMIC)
        return false;

    // Only eat corpses now.
    if (item.base_type == OBJ_CORPSES)
        return true;

    return false;
}

equipment_type get_item_slot(const item_def& item)
{
    return get_item_slot(item.base_type, item.sub_type);
}

equipment_type get_item_slot(object_class_type type, int sub_type)
{
    switch (type)
    {
    case OBJ_WEAPONS:
    case OBJ_STAVES:
    case OBJ_SHIELDS:
#if TAG_MAJOR_VERSION == 34
    case OBJ_RODS:
#endif
    case OBJ_MISCELLANY:
        return EQ_WEAPON0;

    case OBJ_ARMOURS:
        return get_armour_slot(static_cast<armour_type>(sub_type));

    case OBJ_JEWELLERY:
        return jewellery_is_amulet(sub_type) ? EQ_AMULET : EQ_RINGS;

    default:
        break;
    }

    return EQ_NONE;
}

bool is_shield(const item_def &item)
{
    return (item.base_type == OBJ_SHIELDS);
}

bool shield_reflects(const item_def &shield)
{   
    if (is_shield(shield) && !is_hybrid(shield.sub_type))
        return get_armour_ego_type(shield) == SPARM_REFLECTION;
    else
        return false;
}

void ident_reflector(item_def *item)
{
    if (!is_artefact(*item))
        set_ident_flags(*item, ISFLAG_KNOW_TYPE);
}

string item_base_name(const item_def &item)
{
    return item_base_name(item.base_type, item.sub_type);
}

string item_base_name(object_class_type type, int sub_type)
{
    switch (type)
    {
    case OBJ_WEAPONS:
        return Weapon_prop[Weapon_index[sub_type]].name;
    case OBJ_MISSILES:
        return Missile_prop[Missile_index[sub_type]].name;
    case OBJ_ARMOURS:
        return Armour_prop[Armour_index[sub_type]].name;
    case OBJ_SHIELDS:
        return Shield_prop[Shield_index[sub_type]].name;
    case OBJ_JEWELLERY:
        return jewellery_is_amulet(sub_type) ? "amulet" : "ring";
    default:
        return "";
    }
}

string food_type_name(int sub_type)
{
    return Food_prop[Food_index[sub_type]].name;
}

const char* weapon_base_name(weapon_type subtype)
{
    return Weapon_prop[Weapon_index[subtype]].name;
}

void remove_whitespace(string &str)
{
    str.erase(remove_if(str.begin(), str.end(),
        static_cast<int(*)(int)>(isspace)), str.end());
}

/**
 * Try to find a weapon, given the weapon's name without whitespace.
 *
 * @param name_nospace  The weapon's base name, with all whitespace removed.
 * @return              The id of the weapon, or WPN_UNKNOWN if nothing matches.
 */
weapon_type name_nospace_to_weapon(string name_nospace)
{
    for (const weapon_def &wpn : Weapon_prop)
    {
        string weap_nospace = wpn.name;
        remove_whitespace(weap_nospace);

        if (name_nospace == weap_nospace)
            return (weapon_type) wpn.id;
    }

    // No match found
    return WPN_UNKNOWN;
}

void seen_item(const item_def &item)
{
    if (!is_artefact(item) && _is_affordable(item) && !you.chapter == CHAPTER_NONDUNGEON_START)
    {
        // Known brands will be set in set_item_flags().
        if (item.base_type == OBJ_WEAPONS)
            you.seen_weapon[item.sub_type] |= 1U << SP_UNKNOWN_BRAND;
        if (item.base_type == OBJ_ARMOURS)
            you.seen_armour[item.sub_type] |= 1U << SP_UNKNOWN_BRAND;
        if (item.base_type == OBJ_MISCELLANY
            && !is_deck(item))
        {
            you.seen_misc.set(item.sub_type);
        }
    }

    _maybe_note_found_unrand(item);

    // major hack. Deconstify should be safe here, but it's still repulsive.
    item_def& malleable_item = const_cast<item_def &>(item);

    malleable_item.flags |= ISFLAG_SEEN;
    malleable_item.flags |= ISFLAG_KNOW_CURSE;
    if (item.base_type == OBJ_GOLD && !item.tithe_state)
    {
        malleable_item.plus = (you_worship(GOD_ZIN)) ? TS_FULL_TITHE
                                                     : TS_NO_PIETY;
    }

    if (item_type_has_ids(item.base_type) && !is_artefact(item)
        && item_ident(item, ISFLAG_KNOW_TYPE)
        && !you.type_ids[item.base_type][item.sub_type])
    {
        // Can't cull shop items here -- when called from view, we shouldn't
        // access the UI. Old ziggurat prompts are a very minor case of what
        // could go wrong.
        set_ident_type(item.base_type, item.sub_type, true);
    }
}

/// Map of xp evokers to you.props[] xp debt keys.
static const map<int, const char*> debt_map = {
    { MISC_FAN_OF_GALES,        "fan_debt" },
    { MISC_LAMP_OF_FIRE,        "lamp_debt" },
    { MISC_PHIAL_OF_FLOODS,     "phial_debt" },
    { MISC_HORN_OF_GERYON,      "horn_debt" },
    { MISC_LIGHTNING_ROD,       "rod_debt" },
};

/**
 * Is the given item an xp-charged evocable? (That is, one that recharges as
 * the player gains xp.)
 *
 * @param item      The item in question.
 * @return          Whether the given item is an xp evocable. (One of the
 *                  elemental evocables or the Horn of Geryon.)
 */
bool is_xp_evoker(const item_def &item)
{
    return item.base_type == OBJ_MISCELLANY
           && map_find(debt_map, item.sub_type);
}

/**
 * Return the xp debt corresponding to the given type of evoker.
 * Asserts that the given evoker type actually corresponds to an xp evoker.
 *
 * @param evoker_type       The misc_item_type of the evoker in question.
 * @return                  The level of xp debt the given evoker type has
 *                          before it can be used again.
 */
int &evoker_debt(int evoker_type)
{
    const char* const *prop_name = map_find(debt_map, evoker_type);
    ASSERT(prop_name);
    return you.props[*prop_name].get_int();
}

/**
 * How many max charges can the given XP evoker have?
 *
 * @param evoker_type The type of evoker.
 * @returns The max number of charges.
 */
int evoker_max_charges(int evoker_type)
{
    return evoker_type == MISC_LIGHTNING_ROD ? LIGHTNING_MAX_CHARGE : 1;
}

/**
 * What is the XP debt of using one charge of the given XP evoker type? This
 * debt represents a cost after scaling by a level-based XP factor.
 *
 * @params evoker_type The item sub type of the evoker
 * @returns The debt of using a charge.
 */
int evoker_charge_xp_debt(int evoker_type)
{
    return evoker_type == MISC_LIGHTNING_ROD
        ? XP_EVOKE_LIGHTNING_ROD_DEBT
        : XP_EVOKE_DEBT;
}

/**
 * How many remaining charges does the given XP evoker have?
 *
 * @param evoker_type The item subtype of the evoker.
 * @returns The number of remaining charges.
 */
int evoker_charges(int evoker_type)
{
    const int max_charges = evoker_max_charges(evoker_type);
    const int charge_xp_debt = evoker_charge_xp_debt(evoker_type);
    const int debt = evoker_debt(evoker_type);
    return min(max_charges,
            max_charges - debt / charge_xp_debt - (debt % charge_xp_debt > 0));
}

void expend_xp_evoker(int evoker_type)
{
    evoker_debt(evoker_type) += evoker_charge_xp_debt(evoker_type);
}

/// witchcraft. copied from mon-util.h's get_resist
static inline int _get_armour_flag(armflags_t all, armour_flag res)
{
    if (res > ARMF_LAST_MULTI)
        return all & res ? 1 : 0;
    int v = (all / res) & 7;
    if (v > 4)
        return v - 8;
    return v;
}

/**
 * What inherent special properties does the given armour type have?
 *
 * @param arm   The given armour type.
 * @return      A bitfield of special properties.
 */
static armflags_t _armour_type_flags(const uint8_t arm)
{
    return Armour_prop[ Armour_index[arm] ].flags;
}

/**
 * What value does the given armour type have for the innate special property?
 *
 * @param arm   The given armour type.
 * @param prop  The property in question.
 * @return      A value for that property; ranges -3 to 4.
 */
int armour_type_prop(const uint8_t arm, const armour_flag prop)
{
    return _get_armour_flag(_armour_type_flags(arm), prop);
}

/**
 * For store pricing purposes, how much is the given type of weapon worth,
 * before curses, egos, etc are taken into account?
 *
 * @param type      The type of weapon in question; e.g. WPN_DAGGER.
 * @return          A value in gold; e.g. 20.
 */
int weapon_base_price(weapon_type type)
{
    return Weapon_prop[ Weapon_index[type] ].price;
}

/**
 * For store pricing purposes, how much is the given type of armour worth,
 * before curses, egos, etc are taken into account?
 *
 * @param type      The type of weapon in question; e.g. ARM_RING_MAIL.
 * @return          A value in gold; e.g. 45.
 */
int armour_base_price(armour_type type)
{
    return Armour_prop[ Armour_index[type] ].price;
}

int shield_base_price(shield_type type)
{
    return Shield_prop[Shield_index[type]].price;
}