// This needs to be re-ordered when TAG_MAJOR_VERSION changes!
static const vector<spell_type> spellbook_templates[] =
{

{   // Book of Minor Magic
    SPELL_FLAME_TONGUE,
    SPELL_BLINK,
    SPELL_CALL_IMP,
    SPELL_SLOW,
    SPELL_CONJURE_FLAME,
    SPELL_MEPHITIC_CLOUD,
},
#if TAG_MAJOR_VERSION == 34
{   // Book of Conjurations
    SPELL_STING,
    SPELL_SEARING_RAY,
    SPELL_BLINDING_SPRAY,
    SPELL_FULMINANT_PRISM,
    SPELL_FORCE_LANCE,
    SPELL_THROW_ICICLE,
},
#endif
{   // Book of Flames
    SPELL_FOXFIRE,
    SPELL_SEARING_RAY,
    SPELL_CONJURE_FLAME,
    SPELL_INNER_FLAME,
    SPELL_STICKY_FLAME,
    SPELL_FIREBALL,
},

{   // Book of Frost
    SPELL_FREEZE,
    SPELL_THROW_FROST, // BCADDO: Replace this with something.
    SPELL_OZOCUBUS_ARMOUR,
    SPELL_THROW_ICICLE,
    SPELL_HAILSTORM,
    SPELL_SUMMON_ICE_BEAST,
},

{   // Book of Summonings
    SPELL_RECALL,
    SPELL_AURA_OF_ABJURATION,
    SPELL_SUMMON_FOREST,
    SPELL_SUMMON_MANA_VIPER,
    SPELL_SHADOW_CREATURES,
},

{   // Book of Fire
    SPELL_IGNITE_POISON,
    SPELL_FIREBALL,
    SPELL_BOLT_OF_FIRE,
    SPELL_STARBURST,
    SPELL_RING_OF_FLAMES,
    SPELL_UNSTABLE_FIERY_DASH,
},

{   // Book of Ice
    SPELL_ICE_FORM,
    SPELL_OZOCUBUS_REFRIGERATION,
    SPELL_BOLT_OF_COLD,
    SPELL_FREEZING_CLOUD,
    SPELL_SIMULACRUM,
    SPELL_ICICLE_CASCADE,
},

{   // Book of Spatial Translocations
    SPELL_BLINK,
    SPELL_SHROUD_OF_GOLUBRIA,
    SPELL_TELEPORT_OTHER,
    SPELL_BECKONING,
    SPELL_FORCE_LANCE,
    SPELL_GOLUBRIAS_PASSAGE,
},

{   // Book of Enchantments
    SPELL_CAUSE_FEAR,
    SPELL_VIOLENT_UNRAVELLING,
    SPELL_SILENCE,
    SPELL_DEFLECT_MISSILES,
    SPELL_DISCORD,
},

{   // Young Poisoner's Handbook
    SPELL_POISONOUS_VAPOURS,
    SPELL_STING,
    SPELL_MEPHITIC_CLOUD,
    SPELL_IGNITE_POISON,
    SPELL_OLGREBS_TOXIC_RADIANCE,
},

{   // Book of the Tempests
    SPELL_DISCHARGE,
    SPELL_LIGHTNING_BOLT,
    SPELL_ICICLE_CASCADE,
    SPELL_TORNADO,
    SPELL_SHATTER,
},

{   // Book of Death
    SPELL_CORPSE_ROT,
    SPELL_SUBLIMATION_OF_BLOOD,
    SPELL_EXCRUCIATING_WOUNDS,
    SPELL_BOLT_OF_DRAINING,
    SPELL_AGONY,
},

{   // Book of Misfortune
    SPELL_CONFUSING_TOUCH,
    SPELL_CONFUSE,
    SPELL_GRAVITAS,
    SPELL_PETRIFY,
    SPELL_VIOLENT_UNRAVELLING,
},

{   // Book of Changes
    SPELL_BEASTLY_APPENDAGE,
    SPELL_STICKS_TO_SNAKES,
    SPELL_SNAKES_TO_STICKS,
    SPELL_SCORPION_FORM,
    SPELL_ICE_FORM,
    SPELL_BLADE_HANDS,
},

{   // Book of Transfigurations
    SPELL_IRRADIATE,
    SPELL_CIGOTUVIS_DEGENERATION,
    SPELL_STATUE_FORM,
    SPELL_INFESTATION,
    SPELL_DRAGON_FORM,
},

{   // Fen Folio
    SPELL_CORPSE_ROT,
    SPELL_STONE_ARROW,
    SPELL_SUMMON_FOREST,
    SPELL_NOXIOUS_BOG,
    SPELL_SUMMON_HYDRA_MOUNT,
},

#if TAG_MAJOR_VERSION > 34
{   // Book of Battle
    SPELL_INFUSION,
    SPELL_SHROUD_OF_GOLUBRIA,
    SPELL_SONG_OF_SLAYING,
    SPELL_SPECTRAL_WEAPON,
    SPELL_REGENERATION,
},
#endif
{   // Book of Clouds
    SPELL_POISONOUS_VAPOURS,
    SPELL_MEPHITIC_CLOUD,
    SPELL_CONJURE_FLAME,
    SPELL_STILL_WINDS,
    SPELL_FREEZING_CLOUD,
    SPELL_RING_OF_FLAMES,
},

{   // Book of Necromancy
    SPELL_PAIN,
    SPELL_SKELETAL_UPRISING,
    SPELL_VAMPIRIC_DRAINING,
    SPELL_REGENERATION,
    SPELL_ANIMATE_DEAD,
},

{   // Book of Callings
    SPELL_SUMMON_SMALL_MAMMAL,
    SPELL_CALL_IMP,
    SPELL_CALL_CANINE_FAMILIAR,
    SPELL_SUMMON_ICE_BEAST,
    SPELL_SUMMON_LIGHTNING_SPIRE,
},

{   // Book of Maledictions
    SPELL_MAGIC_CANDLE,
    SPELL_HIBERNATION,
    SPELL_CONFUSE,
    SPELL_TUKIMAS_DANCE,
    SPELL_BLINDING_SPRAY,
    SPELL_FULMINANT_PRISM
},

{   // Book of Air
    SPELL_SHOCK,
    SPELL_SWIFTNESS,
    SPELL_DISCHARGE,
    SPELL_AIRSTRIKE,
    SPELL_LIGHTNING_BOLT,
},

{   // Book of the Sky
    SPELL_SUMMON_LIGHTNING_SPIRE,
    SPELL_SILENCE,
    SPELL_DEFLECT_MISSILES,
    SPELL_CONJURE_BALL_LIGHTNING,
    SPELL_TORNADO,
},

{   // Book of the Warp
    SPELL_PORTAL_PROJECTILE,
    SPELL_MUSE_OAMS_AIR_BLAST,
    SPELL_DISPERSAL,
    SPELL_CONTROLLED_BLINK,
    SPELL_UNSTABLE_FIERY_DASH,
    SPELL_DISJUNCTION,
},

#if TAG_MAJOR_VERSION == 34
{   // Book of Envenomations
    SPELL_SCORPION_FORM,
    SPELL_OLGREBS_TOXIC_RADIANCE,
},
#endif

{   // Book of Unlife
    SPELL_RECALL,
    SPELL_ANIMATE_DEAD,
    SPELL_BORGNJORS_VILE_CLUTCH,
    SPELL_DEATH_CHANNEL,
    SPELL_CIGOTUVIS_DEGENERATION,
    SPELL_SIMULACRUM,
},

#if TAG_MAJOR_VERSION == 34
{   // Book of Control
    SPELL_SKELETAL_UPRISING
},

{   // Book of Battle
    SPELL_INFUSION,
    SPELL_SHROUD_OF_GOLUBRIA,
    SPELL_SONG_OF_SLAYING,
    SPELL_SPECTRAL_WEAPON,
    SPELL_REGENERATION,
},
#endif

{   // Book of Geomancy
    SPELL_SANDBLAST,
    SPELL_PASSWALL,
    SPELL_STONE_ARROW,
    SPELL_PETRIFY,
    SPELL_LRD,
    SPELL_SMD,
},

{   // Book of Earth
    SPELL_LEDAS_LIQUEFACTION,
    SPELL_BOLT_OF_MAGMA,
    SPELL_STATUE_FORM,
    SPELL_IRON_SHOT,
    SPELL_DIG,
    SPELL_SHATTER,
},

#if TAG_MAJOR_VERSION == 34
{   // Book of Wizardry
    SPELL_FORCE_LANCE,
    SPELL_AGONY,
    SPELL_INVISIBILITY,
    SPELL_SPELLFORGED_SERVITOR,
},
#endif

{   // Book of Power
    SPELL_BATTLESPHERE,
    SPELL_BOLT_OF_MAGMA,
    SPELL_IRON_SHOT,
    SPELL_IOOD,
    SPELL_SPELLFORGED_SERVITOR,
},

{   // Book of Cantrips
    SPELL_CONFUSING_TOUCH,
    SPELL_MAGIC_CANDLE,
    SPELL_SUMMON_SMALL_MAMMAL,
    SPELL_FLAME_TONGUE,
},

{   // Book of Party Tricks
    SPELL_SUMMON_BUTTERFLIES,
    SPELL_PROJECTED_NOISE,
    SPELL_TUKIMAS_DANCE,
    SPELL_APPORTATION,
    SPELL_INVISIBILITY,
},

#if TAG_MAJOR_VERSION == 34
{   // Akashic Record
    SPELL_DISPERSAL,
    SPELL_MALIGN_GATEWAY,
    SPELL_CONTROLLED_BLINK,
    SPELL_DISJUNCTION,
},
#endif

{   // Book of Debilitation
    SPELL_MAGIC_CANDLE,
    SPELL_SLOW,
    SPELL_INNER_FLAME,
    SPELL_PORTAL_PROJECTILE,
    SPELL_CAUSE_FEAR,
    SPELL_LEDAS_LIQUEFACTION,
},

{   // Book of the Dragon
    SPELL_FLAME_TONGUE,
    SPELL_CAUSE_FEAR,
    SPELL_BOLT_OF_FIRE,
    SPELL_DRAGON_FORM,
    SPELL_DRAGON_CALL,
},

{   // Book of Burglary
    SPELL_SWIFTNESS,
    SPELL_PASSWALL,
    SPELL_GOLUBRIAS_PASSAGE,
    SPELL_LRD,
    SPELL_SMD,
    SPELL_DARKNESS,
    SPELL_INVISIBILITY,
},

{   // Book of Dreams
    SPELL_HIBERNATION,
    SPELL_SILENCE,
    SPELL_DARKNESS,
    SPELL_SHADOW_CREATURES,
    SPELL_BORGNJORS_VILE_CLUTCH,
    SPELL_INFESTATION,
},

{   // Book of Alchemy
    SPELL_SUBLIMATION_OF_BLOOD,
    SPELL_IGNITE_POISON,
    SPELL_PETRIFY,
    SPELL_IRRADIATE,
    SPELL_NOXIOUS_BOG,
},

{   // Book of Beasts
    SPELL_SUMMON_BUTTERFLIES,
    SPELL_CALL_CANINE_FAMILIAR,
    SPELL_SUMMON_ICE_BEAST,
    SPELL_SUMMON_MANA_VIPER,
    SPELL_SUMMON_SPIDER_MOUNT,
},

{   // Book of Annihilations
    SPELL_POISON_ARROW,
    SPELL_CHAIN_LIGHTNING,
    SPELL_LEHUDIBS_CRYSTAL_SPEAR,
    SPELL_GLACIATE,
    SPELL_FIRE_STORM,
},

{   // Grand Grimoire
    SPELL_HAUNT,
    SPELL_MONSTROUS_MENAGERIE,
    SPELL_MALIGN_GATEWAY,
    SPELL_SUMMON_JUNGLE,
},

{   // Necronomicon
    SPELL_HAUNT,
    SPELL_BORGNJORS_REVIVIFICATION,
    SPELL_DEATHS_DOOR,
    SPELL_NECROMUTATION,
},

};

COMPILE_CHECK(ARRAYSZ(spellbook_templates) == 1 + MAX_FIXED_BOOK);
