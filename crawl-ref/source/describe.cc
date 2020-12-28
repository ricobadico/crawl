/**
 * @file
 * @brief Functions used to print information about various game objects.
**/

#include "AppHdr.h"

#include "describe.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <string>

#include "ability.h"
#include "adjust.h"
#include "areas.h"
#include "art-enum.h"
#include "artefact.h"
#include "branch.h"
#include "cloud.h" // cloud_type_name
#include "clua.h"
#include "colour.h"
#include "database.h"
#include "dbg-util.h"
#include "decks.h"
#include "delay.h"
#include "describe-spells.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "evoke.h"
#include "fight.h"
#include "food.h"
#include "ghost.h"
#include "god-abil.h"
#include "god-item.h"
#include "god-passive.h"
#include "hints.h"
#include "invent.h"
#include "item-prop.h"
#include "item-status-flag-type.h"
#include "items.h"
#include "item-use.h"
#include "jobs.h"
#include "lang-fake.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "mon-cast.h" // mons_spell_range
#include "mon-death.h"
#include "mon-tentacle.h"
#include "monuse-flags.h"
#include "output.h"
#include "player-equip.h"
#include "religion.h"
#include "skills.h"
#include "species.h"
#include "spl-book.h"
#include "spl-summoning.h"
#include "spl-util.h"
#include "spl-wpnench.h"
#include "stash.h"
#include "state.h"
#include "stringutil.h" // to_string on Cygwin
#include "terrain.h"
#ifdef USE_TILE_LOCAL
 #include "tilereg-crt.h"
 #include "rltiles/tiledef-dngn.h"
#endif
#ifdef USE_TILE
 #include "rltiles/tiledef-feat.h"
 #include "tilepick.h"
 #include "tileview.h"
 #include "tile-flags.h"
#endif
#include "unicode.h"

using namespace ui;

int count_desc_lines(const string &_desc, const int width)
{
    string desc = get_linebreak_string(_desc, width);
    return count(begin(desc), end(desc), '\n');
}

int show_description(const string &body, const tile_def *tile)
{
    describe_info inf;
    inf.body << body;
    return show_description(inf, tile);
}

int show_description(const describe_info &inf, const tile_def *tile)
{
    auto vbox = make_shared<Box>(Widget::VERT);

    if (!inf.title.empty())
    {
        auto title_hbox = make_shared<Box>(Widget::HORZ);

#ifdef USE_TILE
        if (tile)
        {
            auto icon = make_shared<Image>();
            icon->set_tile(*tile);
            icon->set_margin_for_sdl(0, 10, 0, 0);
            title_hbox->add_child(move(icon));
        }
#else
        UNUSED(tile);
#endif

        auto title = make_shared<Text>(inf.title);
        title_hbox->add_child(move(title));

        title_hbox->set_cross_alignment(Widget::CENTER);
        title_hbox->set_margin_for_sdl(0, 0, 20, 0);
        title_hbox->set_margin_for_crt(0, 0, 1, 0);
        vbox->add_child(move(title_hbox));
    }

    auto desc_sw = make_shared<Switcher>();
    auto more_sw = make_shared<Switcher>();
    desc_sw->current() = 0;
    more_sw->current() = 0;

    const string descs[2] =  {
        trimmed_string(process_description(inf, false)),
        trimmed_string(inf.quote),
    };

#ifdef USE_TILE_LOCAL
# define MORE_PREFIX "[<w>!</w>" "|<w>Right-click</w>" "]: "
#else
# define MORE_PREFIX "[<w>!</w>" "]: "
#endif

    const char* mores[2] = {
        MORE_PREFIX "<w>Description</w>|Quote",
        MORE_PREFIX "Description|<w>Quote</w>",
    };

    for (int i = 0; i < (inf.quote.empty() ? 1 : 2); i++)
    {
        const auto &desc = descs[static_cast<int>(i)];
        auto scroller = make_shared<Scroller>();
        auto fs = formatted_string::parse_string(trimmed_string(desc));
        auto text = make_shared<Text>(fs);
        text->set_wrap_text(true);
        scroller->set_child(text);
        desc_sw->add_child(move(scroller));
        more_sw->add_child(make_shared<Text>(
                formatted_string::parse_string(mores[i])));
    }

    more_sw->set_margin_for_sdl(20, 0, 0, 0);
    more_sw->set_margin_for_crt(1, 0, 0, 0);
    desc_sw->expand_h = false;
    desc_sw->align_x = Widget::STRETCH;
    vbox->add_child(desc_sw);
    if (!inf.quote.empty())
        vbox->add_child(more_sw);

#ifdef USE_TILE_LOCAL
    vbox->max_size().width = tiles.get_crt_font()->char_width()*80;
#endif

    auto popup = make_shared<ui::Popup>(vbox);

    bool done = false;
    int lastch;
    popup->on_keydown_event([&](const KeyEvent& ev) {
        lastch = ev.key();
        if (!inf.quote.empty() && (lastch == '!' || lastch == CK_MOUSE_CMD || lastch == '^'))
            desc_sw->current() = more_sw->current() = 1 - desc_sw->current();
        else
            done = !desc_sw->current_widget()->on_event(ev);
        return true;
    });

#ifdef USE_TILE_WEB
    tiles.json_open_object();
    if (tile)
    {
        tiles.json_open_object("tile");
        tiles.json_write_int("t", tile->tile);
        tiles.json_write_int("tex", tile->tex);
        if (tile->ymax != TILE_Y)
            tiles.json_write_int("ymax", tile->ymax);
        tiles.json_close_object();
    }
    tiles.json_write_string("title", inf.title);
    tiles.json_write_string("prefix", inf.prefix);
    tiles.json_write_string("suffix", inf.suffix);
    tiles.json_write_string("footer", inf.footer);
    tiles.json_write_string("quote", inf.quote);
    tiles.json_write_string("body", inf.body.str());
    tiles.push_ui_layout("describe-generic", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif
    return lastch;
}

string process_description(const describe_info &inf, bool include_title)
{
    string desc;
    if (!inf.prefix.empty())
        desc += "\n\n" + trimmed_string(filtered_lang(inf.prefix));
    if (!inf.title.empty() && include_title)
        desc += "\n\n" + trimmed_string(filtered_lang(inf.title));
    desc += "\n\n" + trimmed_string(filtered_lang(inf.body.str()));
    if (!inf.suffix.empty())
        desc += "\n\n" + trimmed_string(filtered_lang(inf.suffix));
    if (!inf.footer.empty())
        desc += "\n\n" + trimmed_string(filtered_lang(inf.footer));
    trim_string(desc);
    return desc;
}

const char* jewellery_base_ability_string(int subtype)
{
    switch (subtype)
    {
#if TAG_MAJOR_VERSION == 34
    case RING_SUSTAIN_ATTRIBUTES: return "SustAt";
#endif
    case RING_WIZARDRY:           return "Wiz";
    case RING_FIRE:               return "Fire";
    case RING_ICE:                return "Ice";
    case RING_TELEPORTATION:      return "*Tele";
    case AMU_CHAOS:              return "Chaos";
#if TAG_MAJOR_VERSION == 34
    case RING_TELEPORT_CONTROL:   return "+cTele";
#endif
    case AMU_HARM:                return "Harm";
    case AMU_MANA_REGENERATION:   return "RegenMP";
    case AMU_THE_GOURMAND:        return "Gourm";
    case AMU_ACROBAT:             return "Acrobat";
#if TAG_MAJOR_VERSION == 34
    case AMU_CONTROLLED_FLIGHT:   return "cFly";
#endif
    case AMU_GUARDIAN_SPIRIT:     return "Spirit";
    case AMU_FAITH:               return "Faith";
    case AMU_REFLECTION:          return "Reflect";
    }
    return "";
}

#define known_proprt(prop) (proprt[(prop)] && (known[(prop)]))

/// How to display props of a given type?
enum class prop_note
{
    /// The raw numeral; e.g "Slay+3", "Int-1"
    numeral,
    /// Plusses and minuses; "rF-", "rC++"
    symbolic,
    /// Don't note the number; e.g. "rMut"
    plain,
};

struct property_annotators
{
    artefact_prop_type prop;
    prop_note spell_out;
};

static vector<string> _randart_propnames(const item_def& item,
                                         bool no_comma = false,
                                         bool curse = false)
{
    artefact_properties_t  proprt;
    artefact_known_props_t known;
    if (curse)
        curse_desc_properties(item, proprt);
    else 
        artefact_desc_properties(item, proprt, known);

    vector<string> propnames;

    // list the following in rough order of importance
    const property_annotators propanns[] =
    {
        // (Generally) negative attributes
        // These come first, so they don't get chopped off!
        { ARTP_PREVENT_SPELLCASTING,  prop_note::plain },
        { ARTP_PREVENT_TELEPORTATION, prop_note::plain },
        { ARTP_CONTAM,                prop_note::plain },
        { ARTP_ANGRY,                 prop_note::plain },
        { ARTP_CAUSE_TELEPORTATION,   prop_note::plain },
        { ARTP_NOISE,                 prop_note::plain },
        { ARTP_HARM,                  prop_note::plain },
        { ARTP_CORRODE,               prop_note::plain },
        { ARTP_DRAIN,                 prop_note::plain },
        { ARTP_SLOW,                  prop_note::plain },
        { ARTP_FRAGILE,               prop_note::plain },
        { ARTP_INACCURACY,            prop_note::plain },

        // Evokable abilities come second
        { ARTP_BLINK,                 prop_note::plain },
        { ARTP_BERSERK,               prop_note::plain },
        { ARTP_INVISIBLE,             prop_note::plain },
        { ARTP_FLY,                   prop_note::plain },

        // Resists, also really important
        { ARTP_ELECTRICITY,           prop_note::plain },
        { ARTP_POISON,                prop_note::plain },
        { ARTP_FIRE,                  prop_note::symbolic },
        { ARTP_COLD,                  prop_note::symbolic },
        { ARTP_NEGATIVE_ENERGY,       prop_note::symbolic },
        { ARTP_MAGIC_RESISTANCE,      prop_note::symbolic },
        { ARTP_REGENERATION,          prop_note::symbolic },
        { ARTP_RMUT,                  prop_note::plain },
        { ARTP_RCORR,                 prop_note::plain },

        // Quantitative attributes
        { ARTP_HP,                    prop_note::numeral },
        { ARTP_MAGICAL_POWER,         prop_note::numeral },
        { ARTP_AC,                    prop_note::numeral },
        { ARTP_EVASION,               prop_note::numeral },
        { ARTP_STRENGTH,              prop_note::numeral },
        { ARTP_INTELLIGENCE,          prop_note::numeral },
        { ARTP_DEXTERITY,             prop_note::numeral },
        { ARTP_SLAYING,               prop_note::numeral },
        { ARTP_SHIELDING,             prop_note::numeral },

        // Qualitative attributes (and Stealth)
        { ARTP_IMPROVED_VISION,       prop_note::plain },
        { ARTP_STEALTH,               prop_note::symbolic },
        { ARTP_CURSE,                 prop_note::plain },
        { ARTP_CLARITY,               prop_note::plain },
        { ARTP_RMSL,                  prop_note::plain },
    };

    if (!curse)
    {
        const unrandart_entry *entry = nullptr;
        if (is_unrandom_artefact(item))
            entry = get_unrand_entry(item.unrand_idx);

        // For randart jewellery, note the base jewellery type if it's not
        // covered by artefact_desc_properties()
        if (item.base_type == OBJ_JEWELLERY
            && (item_ident(item, ISFLAG_KNOW_TYPE)))
        {
            const char* type = jewellery_base_ability_string(item.sub_type);
            if (*type)
                propnames.push_back(type);
        }
        else if (item_brand_known(item)
            && !(is_unrandom_artefact(item) && entry
                && entry->flags & UNRAND_FLAG_SKIP_EGO && !item.props.exists(CHANGED_BRAND_KEY)))
        {
            string ego;
            if (item.base_type == OBJ_WEAPONS || (item.base_type == OBJ_SHIELDS && is_hybrid(item.sub_type))
                                              ||  item.is_type (OBJ_ARMOURS, ARM_CLAW))
                ego = weapon_brand_name(item, true);
            else if (item.base_type == OBJ_ARMOURS || (item.base_type == OBJ_SHIELDS && !is_hybrid(item.sub_type)))
                ego = armour_ego_name(item, true);
            else if (item.base_type == OBJ_STAVES)
            {
                ego = staff_artefact_brand_name(item);
            }
            if (!ego.empty())
            {
                // XXX: Ugly hack for adding a comma if needed.
                bool extra_props = false;
                for (const property_annotators &ann : propanns)
                    if (known_proprt(ann.prop) && ann.prop != ARTP_BRAND)
                    {
                        extra_props = true;
                        break;
                    }

                if (!no_comma && extra_props
                    || is_unrandom_artefact(item)
                    && entry && entry->inscrip != nullptr)
                {
                    ego += item.base_type == OBJ_STAVES ? ";" : ",";
                }

                propnames.push_back(ego);
            }
        }

        if (is_unrandom_artefact(item) && entry && entry->inscrip != nullptr)
            propnames.push_back(entry->inscrip);
    }

    for (const property_annotators &ann : propanns)
    {
        if (known_proprt(ann.prop) || (proprt[ann.prop] && curse))
        {
            const int val = proprt[ann.prop];

            // Don't show rF+/rC- for =Fire, or vice versa for =Ice.
            if (item.base_type == OBJ_JEWELLERY && !curse)
            {
                if (item.sub_type == RING_FIRE
                    && (ann.prop == ARTP_FIRE && val == 1
                        || ann.prop == ARTP_COLD && val == -1))
                {
                    continue;
                }
                if (item.sub_type == RING_ICE
                    && (ann.prop == ARTP_COLD && val == 1
                        || ann.prop == ARTP_FIRE && val == -1))
                {
                    continue;
                }
            }

            ostringstream work;
            switch (ann.spell_out)
            {
            case prop_note::numeral: // e.g. AC+4
                work << showpos << artp_name(ann.prop) << val;
                break;
            case prop_note::symbolic: // e.g. F++
            {
                work << artp_name(ann.prop);

                char symbol = val > 0 ? '+' : '-';
                const int sval = abs(val);
                if (sval > 4)
                    work << symbol << sval;
                else
                    work << string(sval, symbol);

                break;
            }
            case prop_note::plain: // e.g. rPois or SInv
                work << artp_name(ann.prop);
                break;
            }
            propnames.push_back(work.str());
        }
    }

    return propnames;
}

string artefact_inscription(const item_def& item, bool curse)
{
    if (item.base_type == OBJ_BOOKS)
        return "";

    const vector<string> propnames = _randart_propnames(item, false, curse);

    string insc = curse ? "(Curse: " : "";

    insc += comma_separated_line(propnames.begin(), propnames.end(),
                                       " ", " ");
    if (!insc.empty() && insc[insc.length() - 1] == ',')
        insc.erase(insc.length() - 1);

    if (curse)
    {
        if (item.is_type(OBJ_ARMOURS, ARM_SKULL))
            insc += ", +Necro";
        insc += ")";
    }
    return insc;
}

void add_inscription(item_def &item, string inscrip)
{
    if (!item.inscription.empty())
    {
        if (ends_with(item.inscription, ","))
            item.inscription += " ";
        else
            item.inscription += ", ";
    }

    item.inscription += inscrip;
}

static const char* _jewellery_base_ability_description(int subtype)
{
    switch (subtype)
    {
#if TAG_MAJOR_VERSION == 34
    case RING_SUSTAIN_ATTRIBUTES:
        return "It sustains your strength, intelligence and dexterity.";
#endif
    case RING_WIZARDRY:
        return "It improves your spell success rate.";
    case AMU_CHAOS:
        return "It infuses your magic with chaos; buffing its maximum damage and randomizing its effects. "
            "Additionally it randomly provides additional protection from elemental components of attacks.";
    case RING_FIRE:
        return "It enhances your fire magic.";
    case RING_ICE:
        return "It enhances your ice magic.";
    case RING_TELEPORTATION:
        return "It may teleport you next to monsters.";
#if TAG_MAJOR_VERSION == 34
    case RING_TELEPORT_CONTROL:
        return "It can be evoked for teleport control.";
#endif
    case AMU_HARM:
        return "It increases damage dealt and taken.";
    case AMU_MANA_REGENERATION:
        return "It increases your magic regeneration.";
    case AMU_THE_GOURMAND:
        return "It allows you to eat raw meat even when not hungry.";
    case AMU_ACROBAT:
        return "It helps you evade while moving and waiting.";
    case AMU_GUARDIAN_SPIRIT:
        return "It causes incoming damage to be split between your health and "
               "magic.";
    case AMU_FAITH:
        return "It allows you to gain divine favour quickly.";
    case AMU_REFLECTION:
        return "It shields you and reflects attacks.";
    case AMU_INACCURACY:
        return "It blurs your vision.";
    }
    return "";
}

struct property_descriptor
{
    artefact_prop_type property;
    const char* desc;           // If it contains %d, will be replaced by value.
    bool is_graded_resist;
};

static string _randart_descrip(const item_def &item, bool curse = false)
{
    string description;

    artefact_properties_t  proprt;
    artefact_known_props_t known;
    if (curse)
        curse_desc_properties(item, proprt);
    else
        artefact_desc_properties(item, proprt, known);

    const property_descriptor propdescs[] =
    {
        { ARTP_AC, "It affects your AC (%d).", false },
        { ARTP_EVASION, "It affects your evasion (%d).", false},
        { ARTP_STRENGTH, "It affects your strength (%d).", false},
        { ARTP_INTELLIGENCE, "It affects your intelligence (%d).", false},
        { ARTP_DEXTERITY, "It affects your dexterity (%d).", false},
        { ARTP_SLAYING, "It affects your accuracy and damage with ranged "
                        "weapons and melee attacks (%d).", false},
        { ARTP_FIRE, "fire", true},
        { ARTP_COLD, "cold", true},
        { ARTP_ELECTRICITY, "It insulates you from electricity.", false},
        { ARTP_POISON, "poison", true},
        { ARTP_NEGATIVE_ENERGY, "negative energy", true},
        { ARTP_MAGIC_RESISTANCE, "It affects your resistance to hostile "
                                 "enchantments.", false},
        { ARTP_HP, "It affects your health (%d).", false},
        { ARTP_MAGICAL_POWER, "It affects your magic capacity (%d).", false},
        { ARTP_IMPROVED_VISION, "It improves your vision.", false},
        { ARTP_INACCURACY, "It blurs your vision.", false },
        { ARTP_INVISIBLE, "It lets you turn invisible.", false},
        { ARTP_FLY, "It lets you fly.", false},
        { ARTP_BLINK, "It lets you blink.", false},
        { ARTP_BERSERK, "It lets you go berserk.", false},
        { ARTP_NOISE, "It may make noises in combat.", false},
        { ARTP_PREVENT_SPELLCASTING, "It prevents spellcasting.", false},
        { ARTP_CAUSE_TELEPORTATION, "It may teleport you next to monsters.", false},
        { ARTP_PREVENT_TELEPORTATION, "It prevents most forms of teleportation.",
          false},
        { ARTP_ANGRY,  "It may make you go berserk in combat.", false},
        { ARTP_CURSE, "When equipped, it binds to the wielder's soul until they gain enough experience to unwield again.", false},
        { ARTP_CLARITY, "It protects you against confusion.", false},
        { ARTP_CONTAM, "It causes magical contamination when unequipped.", false},
        { ARTP_RMSL, "It protects you from missiles.", false},
        { ARTP_REGENERATION, "It increases your rate of regeneration.", false},
        { ARTP_RCORR, "It provides partial protection from all sources of acid and corrosion.",
          false},
        { ARTP_RMUT, "It protects you from mutation.", false},
        { ARTP_CORRODE, "It may corrode you when you take damage.", false},
        { ARTP_DRAIN, "It causes draining when unequipped.", false},
        { ARTP_SLOW, "It may slow you when you take damage.", false},
        { ARTP_FRAGILE, "It will be destroyed if unequipped or dropped on death.", false },
        { ARTP_SHIELDING, "It affects your SH (%d).", false},
        { ARTP_HARM, "It increases damage dealt and taken.", false},
    };

    // Give a short description of the base type, for base types with no
    // corresponding ARTP.
    if (!curse && item.base_type == OBJ_JEWELLERY
        && (item_ident(item, ISFLAG_KNOW_TYPE)))
    {
        const char* type = _jewellery_base_ability_description(item.sub_type);
        if (*type)
        {
            description += "\n";
            description += type;
        }
    }

    for (const property_descriptor &desc : propdescs)
    {
        if (known_proprt(desc.property) || proprt[desc.property] && curse)
        {
            string sdesc = desc.desc;

            // FIXME Not the nicest hack.
            char buf[80];
            snprintf(buf, sizeof buf, "%+d", proprt[desc.property]);
            sdesc = replace_all(sdesc, "%d", buf);

            if (desc.is_graded_resist)
            {
                int idx = proprt[desc.property] + 3;
                idx = min(idx, 6);
                idx = max(idx, 0);

                const char* prefixes[] =
                {
                    "It makes you extremely vulnerable to ",
                    "It makes you very vulnerable to ",
                    "It makes you vulnerable to ",
                    "Buggy descriptor!",
                    "It protects you from ",
                    "It greatly protects you from ",
                    "It renders you almost immune to "
                };
                sdesc = prefixes[idx] + sdesc + '.';
            }

            description += '\n';
            description += sdesc;
        }
    }

    if (known_proprt(ARTP_STEALTH) || proprt[ARTP_STEALTH] && curse)
    {
        const int stval = proprt[ARTP_STEALTH];
        char buf[80];
        snprintf(buf, sizeof buf, "\nIt makes you %s%s stealthy.",
                 (stval < -1 || stval > 1) ? "much " : "",
                 (stval < 0) ? "less" : "more");
        description += buf;
    }

    if (curse && item.is_type(OBJ_ARMOURS, ARM_SKULL))
        description += "\nThe necromantic curse makes you closer to the forces of death. (Necromancy Enhancer).";

    return description;
}
#undef known_proprt

static const char *trap_names[] =
{
#if TAG_MAJOR_VERSION == 34
    "dart",
#endif
    "arrow", "spear",
#if TAG_MAJOR_VERSION > 34
    "dispersal",
    "teleport",
#endif
    "permanent teleport",
    "alarm", "blade",
    "bolt", "net", "Zot", "needle",
    "shaft", "passage", "pressure plate", "web",
#if TAG_MAJOR_VERSION == 34
    "gas", "teleport",
    "shadow", "dormant shadow", "dispersal"
#endif
};

string trap_name(trap_type trap)
{
    COMPILE_CHECK(ARRAYSZ(trap_names) == NUM_TRAPS);

    if (trap >= 0 && trap < NUM_TRAPS)
        return trap_names[trap];
    return "";
}

string full_trap_name(trap_type trap)
{
    string basename = trap_name(trap);
    switch (trap)
    {
    case TRAP_GOLUBRIA:
        return basename + " of Golubria";
    case TRAP_PLATE:
    case TRAP_WEB:
    case TRAP_SHAFT:
        return basename;
    default:
        return basename + " trap";
    }
}

int str_to_trap(const string &s)
{
    // "Zot trap" is capitalised in trap_names[], but the other trap
    // names aren't.
    const string tspec = lowercase_string(s);

    // allow a couple of synonyms
    if (tspec == "random" || tspec == "any")
        return TRAP_RANDOM;

    for (int i = 0; i < NUM_TRAPS; ++i)
        if (tspec == lowercase_string(trap_names[i]))
            return i;

    return -1;
}

/**
 * How should this panlord be described?
 *
 * @param name   The panlord's name; used as a seed for its appearance.
 * @param flying Whether the panlord can fly.
 * @returns a string including a description of its head, its body, its flight
 *          mode (if any), and how it smells or looks.
 */
static string _describe_demon(const string& name, bool flying)
{
    const uint32_t seed = hash32(&name[0], name.size());
    #define HRANDOM_ELEMENT(arr, id) arr[hash_with_seed(ARRAYSZ(arr), seed, id)]

    static const char* body_types[] =
    {
        "armoured",
        "vast, spindly",
        "fat",
        "obese",
        "muscular",
        "spiked",
        "splotchy",
        "slender",
        "tentacled",
        "emaciated",
        "bug-like",
        "skeletal",
        "mantis",
    };

    static const char* wing_names[] =
    {
        "with small, bat-like wings",
        "with bony wings",
        "with sharp, metallic wings",
        "with the wings of a moth",
        "with thin, membranous wings",
        "with dragonfly wings",
        "with large, powerful wings",
        "with fluttering wings",
        "with great, sinister wings",
        "with hideous, tattered wings",
        "with sparrow-like wings",
        "with hooked wings",
        "with strange knobs attached",
        "which hovers in mid-air",
        "with sacs of gas hanging from its back",
    };

    const char* head_names[] =
    {
        "a cubic structure in place of a head",
        "a brain for a head",
        "a hideous tangle of tentacles for a mouth",
        "the head of an elephant",
        "an eyeball for a head",
        "wears a helmet over its head",
        "a horn in place of a head",
        "a thick, horned head",
        "the head of a horse",
        "a vicious glare",
        "snakes for hair",
        "the face of a baboon",
        "the head of a mouse",
        "a ram's head",
        "the head of a rhino",
        "eerily human features",
        "a gigantic mouth",
        "a mass of tentacles growing from its neck",
        "a thin, worm-like head",
        "huge, compound eyes",
        "the head of a frog",
        "an insectoid head",
        "a great mass of hair",
        "a skull for a head",
        "a cow's skull for a head",
        "the head of a bird",
        "a large fungus growing from its neck",
    };

    static const char* misc_descs[] =
    {
        " It seethes with hatred of the living.",
        " Tiny orange flames dance around it.",
        " Tiny purple flames dance around it.",
        " It is surrounded by a weird haze.",
        " It glows with a malevolent light.",
        " It looks incredibly angry.",
        " It oozes with slime.",
        " It dribbles constantly.",
        " Mould grows all over it.",
        " Its body is covered in fungus.",
        " It is covered with lank hair.",
        " It looks diseased.",
        " It looks as frightened of you as you are of it.",
        " It moves in a series of hideous convulsions.",
        " It moves with an unearthly grace.",
        " It leaves a glistening oily trail.",
        " It shimmers before your eyes.",
        " It is surrounded by a brilliant glow.",
        " It radiates an aura of extreme power.",
        " It seems utterly heartbroken.",
        " It seems filled with irrepressible glee.",
        " It constantly shivers and twitches.",
        " Blue sparks crawl across its body.",
        " It seems uncertain.",
        " A cloud of flies swarms around it.",
        " The air around it ripples with heat.",
        " Crystalline structures grow on everything near it.",
        " It appears supremely confident.",
        " Its skin is covered in a network of cracks.",
        " Its skin has a disgusting oily sheen.",
        " It seems somehow familiar.",
        " It is somehow always in shadow.",
        " It is difficult to look away.",
        " It is constantly speaking in tongues.",
        " It babbles unendingly.",
        " Its body is scourged by hellfire.",
        " Its body is extensively scarred.",
        " You find it difficult to look away.",
    };

    static const char* smell_descs[] =
    {
        " It smells of brimstone.",
        " It is surrounded by a sickening stench.",
        " It smells of rotting flesh.",
        " It stinks of death.",
        " It stinks of decay.",
        " It smells delicious!",
    };

    ostringstream description;
    description << "One of the many lords of Pandemonium, " << name << " has ";

    description << article_a(HRANDOM_ELEMENT(body_types, 2));
    description << " body ";

    if (flying)
    {
        description << HRANDOM_ELEMENT(wing_names, 3);
        description << " ";
    }

    description << "and ";
    description << HRANDOM_ELEMENT(head_names, 1) << ".";

    if (!hash_with_seed(5, seed, 4) && you.can_smell()) // 20%
        description << HRANDOM_ELEMENT(smell_descs, 5);

    if (hash_with_seed(2, seed, 6)) // 50%
        description << HRANDOM_ELEMENT(misc_descs, 6);

    return description.str();
}

/**
 * Describe a given mutant beast's tier.
 *
 * @param tier      The mutant_beast_tier of the beast in question.
 * @return          A string describing the tier; e.g.
 *              "It is a juvenile, out of the larval stage but still below its
 *              mature strength."
 */
static string _describe_mutant_beast_tier(int tier)
{
    static const string tier_descs[] = {
        "It is of an unusually buggy age.",
        "It is larval and weak, freshly emerged from its mother's pouch.",
        "It is a juvenile, no longer larval but below its mature strength.",
        "It is mature, stronger than a juvenile but weaker than its elders.",
        "It is an elder, stronger than mature beasts.",
        "It is a primal beast, the most powerful of its kind.",
    };
    COMPILE_CHECK(ARRAYSZ(tier_descs) == NUM_BEAST_TIERS);

    ASSERT_RANGE(tier, 0, NUM_BEAST_TIERS);
    return tier_descs[tier];
}


/**
 * Describe a given mutant beast's facets.
 *
 * @param facets    A vector of the mutant_beast_facets in question.
 * @return          A string describing the facets; e.g.
 *              "It flies and flits around unpredictably, and its breath
 *               smoulders ominously."
 */
static string _describe_mutant_beast_facets(const CrawlVector &facets)
{
    static const string facet_descs[] = {
        " seems unusually buggy.",
        " sports a set of venomous tails",
        " flies swiftly and unpredictably",
        "s breath smoulders ominously",
        " is covered with eyes and tentacles",
        " flickers and crackles with electricity",
        " is covered in dense fur and muscle",
    };
    COMPILE_CHECK(ARRAYSZ(facet_descs) == NUM_BEAST_FACETS);

    if (facets.size() == 0)
        return "";

    return "It" + comma_separated_fn(begin(facets), end(facets),
                      [] (const CrawlStoreValue &sv) -> string {
                          const int facet = sv.get_int();
                          ASSERT_RANGE(facet, 0, NUM_BEAST_FACETS);
                          return facet_descs[facet];
                      }, ", and it", ", it")
           + ".";
}

static string _describe_abomination(const monster_info &mi)
{
    string retval;

    if (!mi.props.exists(ABOM_DEF))
        return "";

    abom_def def = mi_read_def(mi);
    
    for (int i = 0; i < 4; i++)
    {
        abom_facet_type facet = def.facets[i];
        if ((i > 0) && (facet > FAC_NON_FACET))
            retval += "\n";
        switch (facet)
        {
        case FAC_BEAK: 
            retval += "It has a distended neck ending a deformed horn or beak.";
            break;
        case FAC_CLASSIC:
            retval += "Part of its body is fashioned into a whiplike tendril, ending in exposed bone. This tendril appears to ground it somewhat.";
            break;
        case FAC_DRAINBLADE:
            retval += "A sharped blade of rough bone protrudes from its abdomen; the jagged blade exudes a dark aura.";
            break;
        case FAC_EYEBASH:
            retval += "It seems to have fashioned several eyes, lungs and other organs into a gorey flail. The sight of the teeth sticking from its eyeballs is rather haunting.";
            retval += " Apparently having these organs on the outside protects it from burns.";
            break;
        case FAC_GOOEY:
            retval += "A gooey mixture of acidic bile and blood coats its flesh.";
            break;
        case FAC_HYDRA:
            retval += "In the middle of its body; there's a chattering of "; 
            retval += number_in_words(mi.num_heads);
            retval += " partially exposed cracked skulls and deformed faces.";
            retval += "Somehow; bits of exposed brain on the surface increase its magical resistance.";
            break;
        case FAC_INTESTINES:
            retval += "It's completely disemboweled, somehow having organs on the outside helps keep it cool.";
            retval += " It wields a rope made of twisted intestines and senews still pulsating with a foul mockery of life.";
            break;
        case FAC_PLATED:
            retval += "Foul plates; perhaps of former scales or exoskeletons protrude from its 'skin', they are coated in some kind of slime that weakens those that touch it and provide it with some 'shielding' to the abomination.";
            break;
        case FAC_SPINY:
            retval += "Sharp spines of bone protude like bleeding compound fractures all over its flesh. These are rather cold to the touch.";
            break;
        case FAC_THAGOMIZER:
            retval += "A spiky ball of various bits of bone is on the end of its 'tail' being struck with this ball will likely lead to bone shards being impaled in the victim's flesh.";
            break;
        case FAC_TRAMPLE:
            retval += "Bulkier than the average abomination; the sheer amount of fat hanging off of it protect from cold attacks and it can push around foes by striking them with several 'limbs' at once.";
            break;
        case FAC_VILEORIFACE:
            retval += "Somewhere towards the middle of the pulsing abomination is an open ribcage, which now serves as a giant gaping maw. Bits of rotting flesh drip from the edges; badly poisoning those it clamps down on.";
            break;
        case FAC_WINGS:
            retval += "Flesh stretched thinly over several limbs has formed a wretched, yet functional pair of wings. As well as providing flight; being raked over by the sharp edges of the wings will drain the speed of those they hit.";
            break;
        case FAC_NON_FACET:
        default: // Shouldn't happen but...
            retval += "";
            break;
        }
    }

    return retval;
}

/**
 * Describe a given mutant beast's special characteristics: its tier & facets.
 *
 * @param mi    The player-visible information about the monster in question.
 * @return      A string describing the monster; e.g.
 *              "It is a juvenile, out of the larval stage but still below its
 *              mature strength. It flies and flits around unpredictably, and
 *              its breath has a tendency to ignite when angered."
 */
static string _describe_mutant_beast(const monster_info &mi)
{
    const int xl = mi.props[MUTANT_BEAST_TIER].get_short();
    const int tier = mutant_beast_tier(xl);
    const CrawlVector facets = mi.props[MUTANT_BEAST_FACETS].get_vector();
    return _describe_mutant_beast_facets(facets)
           + " " + _describe_mutant_beast_tier(tier);
}

/**
 * Is the item associated with some specific training goal?  (E.g. mindelay)
 *
 * @return the goal, or 0 if there is none, scaled by 10.
 */
static int _item_training_target(const item_def &item)
{
    if (item.base_type == OBJ_WEAPONS || item.base_type == OBJ_STAVES)
        return weapon_min_delay_skill(item) * 10;

    else if (item.base_type == OBJ_SHIELDS)
    {
        int x = round(you.get_shield_skill_to_offset_penalty(item) * 10);

        if (is_hybrid(item.sub_type))
        {
            int y = weapon_min_delay_skill(item) * 10;
            return max(x, y);
        }
        else
            return x;
    }

    else
        return 0;
}

/**
 * Does an item improve with training some skill?
 *
 * @return the skill, or SK_NONE if there is none. Note: SK_NONE is *not* 0.
 */
static skill_type _item_training_skill(const item_def &item)
{
    if (item.base_type == OBJ_WEAPONS || item.base_type == OBJ_STAVES
        || item.base_type == OBJ_SHIELDS)
        return item_attack_skill(item);
    else if (item.base_type == OBJ_ARMOURS)
        return SK_ARMOUR;
    else if (item_is_evokable(item)) // not very accurate
        return SK_EVOCATIONS;
    else
        return SK_NONE;
}

/**
 * Whether it would make sense to set a training target for an item.
 *
 * @param item the item to check.
 * @param ignore_current whether to ignore any current training targets (e.g. if there is a higher target, it might not make sense to set a lower one).
 */
static bool _could_set_training_target(const item_def &item, bool ignore_current)
{
    if (!crawl_state.need_save || is_useless_item(item) || you.species == SP_GNOLL)
        return false;

    const skill_type skill = _item_training_skill(item);
    if (skill == SK_NONE)
        return false;

    const int target = min(_item_training_target(item), 270);

    return target && !is_useless_skill(skill)
       && you.skill(skill, 10, false, false, false) < target
       && (ignore_current || you.get_training_target(skill) < target);
}

// Returns if it makes sense to set a skill target for this dual wield combination.
static bool _dual_wield_target(const item_def &item, const int target)
{
    if (!crawl_state.need_save || is_useless_item(item) || you.species == SP_GNOLL)
        return false;

    const skill_type skill = _item_training_skill(item);
    if (skill == SK_NONE)
        return false;

    return target && you.can_currently_train[skill]
        && you.skill(skill, 10, false, false, false) < target
        && you.get_training_target(skill) < target;
}

/**
 * Produce the "Your skill:" line for item descriptions where specific skill targets
 * are relevant (weapons, missiles, shields)
 *
 * @param skill the skill to look at.
 * @param show_target_button whether to show the button for setting a skill target.
 * @param scaled_target a target, scaled by 10, to use when describing the button.
 */
static string _your_skill_desc(skill_type skill, bool show_target_button, int scaled_target)
{
    if (!crawl_state.need_save || skill == SK_NONE)
        return "";
    string target_button_desc = "";
    int min_scaled_target = min(scaled_target, 270);
    if (show_target_button &&
            you.get_training_target(skill) < min_scaled_target)
    {
        target_button_desc = make_stringf(
            "; use <white>(s)</white> to set %d.%d as a target for %s.",
                                min_scaled_target / 10, min_scaled_target % 10,
                                skill_name(skill));
    }
    int you_skill_temp = you.skill(skill, 10, false, true, true);
    int you_skill = you.skill(skill, 10, false, false, false);

    return make_stringf("Your %sskill: %d.%d%s",
                            (you_skill_temp != you_skill ? "(base) " : ""),
                            you_skill / 10, you_skill % 10,
                            target_button_desc.c_str());
}

static string _your_dual_skill_desc(skill_type skill0, skill_type skill1, bool show_target_button, int scaled_target0, int scaled_target1)
{
    if (!crawl_state.need_save || skill0 == SK_NONE)
        return "";
    string target_button_desc = "";
    int min_scaled_target0 = min(scaled_target0, 270);
    int min_scaled_target1 = min(scaled_target1, 270);
    int current_skill0 = max(you.get_training_target(skill0), you.skill(skill0, 10));
    int current_skill1 = max(you.get_training_target(skill1), you.skill(skill1, 10));
    if (show_target_button)
    {
        if (current_skill0 < min_scaled_target0)
        {
            if (current_skill1 < min_scaled_target1)
                target_button_desc = make_stringf(    
                    "\n    Use <white>(s)</white> to set %d.%d as a target for %s"
                    " and %d.%d as a target for %s.",
                    min_scaled_target0 / 10, min_scaled_target0 % 10,
                    skill_name(skill0),
                    min_scaled_target1 / 10, min_scaled_target1 % 10,
                    skill_name(skill1));
            else
                target_button_desc = make_stringf(
                    "\n    Use <white>(s)</white> to set %d.%d as a target for %s.",
                    min_scaled_target0 / 10, min_scaled_target0 % 10,
                    skill_name(skill0));
        }
        else if (current_skill1 < min_scaled_target1)
            target_button_desc = make_stringf(
                "\n    Use <white>(s)</white> to set %d.%d as a target for %s.",
                min_scaled_target1 / 10, min_scaled_target1 % 10,
                skill_name(skill1));
    }
    int you_skill_temp0 = you.skill(skill0, 10, false, true, true);
    int you_skill0 = you.skill(skill0, 10, false, false, false);
    int you_skill_temp1 = you.skill(skill1, 10, false, true, true);
    int you_skill1 = you.skill(skill1, 10, false, false, false);
    const bool steve = (you_skill_temp0 != you_skill0 || you_skill_temp1 != you_skill1);

    return make_stringf("Your %sskill: %d.%d (%s) ; %d.%d (%s)%s",
        (steve ? "(base) " : ""),
        you_skill0 / 10, you_skill0 % 10, skill_name(skill0),
        you_skill1 / 10, you_skill1 % 10, skill_name(skill1),
        target_button_desc.c_str());
}

/**
 * Produce a description of a skill target for items where specific targets are
 * relevant.
 *
 * @param skill the skill to look at.
 * @param scaled_target a skill level target, scaled by 10.
 * @param training a training value, from 0 to 100. Need not be the actual training
 * value.
 */
static string _skill_target_desc(skill_type skill, int scaled_target,
                                        unsigned int training)
{
    string description = "";
    scaled_target = min(scaled_target, 270);

    const bool max_training = (training == 100);
    const bool hypothetical = !crawl_state.need_save ||
                                    (training != you.training[skill]);

    const skill_diff diffs = skill_level_to_diffs(skill,
                                (double) scaled_target / 10, training, false);
    const int level_diff = xp_to_level_diff(diffs.experience / 10, 10);

    if (level_diff == 0)
        return description;

    if (max_training)
        description += "At 100% training ";
    else if (!hypothetical)
    {
        description += make_stringf("At current training (%d%%) ",
                                        you.training[skill]);
    }
    else
        description += make_stringf("At a training level of %d%% ", training);

    description += make_stringf(
        "you %s reach %d.%d in %s %d.%d XLs.",
            hypothetical ? "would" : "will",
            scaled_target / 10, scaled_target % 10,
            (you.experience_level + (level_diff + 9) / 10) > 27
                                ? "the equivalent of" : "about",
            level_diff / 10, level_diff % 10);
    if (you.wizard)
    {
        description += make_stringf("\n    (%d xp, %d skp)",
                                    diffs.experience, diffs.skill_points);
    }
    return description;
}

/**
 * Append two skill target descriptions: one for 100%, and one for the
 * current training rate.
 */
static void _append_skill_target_desc(string &description, skill_type skill,
                                        int scaled_target, bool dual)
{
    if (scaled_target < 0 || scaled_target > 270)
        return;
    if (you.species != SP_GNOLL)
    {
        if (dual) description += "\n    " + _skill_target_desc(skill, scaled_target, 50);
        else      description += "\n    " + _skill_target_desc(skill, scaled_target, 100);
    }
    if (you.training[skill] > 0 && you.training[skill] < 100)
    {
        description += "\n    " + _skill_target_desc(skill, scaled_target,
                                                    you.training[skill]);
    }
}

static bool _check_set_dual_skill(const item_def &item)
{
    if (you.weapon(0) && you.weapon(1) &&
        is_melee_weapon(*you.weapon(0)) && is_melee_weapon(*you.weapon(1)) &&
        (you.weapon(0) == &item || you.weapon(1) == &item))
    {
        if (item_attack_skill(*you.weapon(0)) == item_attack_skill(*you.weapon(1)))
            return you.skill(item_attack_skill(*you.weapon(0)), 10) < 10 * dual_wield_mindelay_skill(*you.weapon(0), *you.weapon(1));
        else
            return (you.skill(item_attack_skill(*you.weapon(0)), 10) < (40 + _item_training_target(*you.weapon(0))) ||
                you.skill(item_attack_skill(*you.weapon(1)), 10) < (40 + _item_training_target(*you.weapon(1))));
    }

    return false;
}

static void _append_weapon_stats(string &description, const item_def &item)
{
    const int base_dam = weapon_damage(item);
    const int ammo_type = fires_ammo_type(item);
    const int ammo_dam = ammo_type == MI_NONE ? 0 :
                                                ammo_type_damage(ammo_type);
    const skill_type skill = _item_training_skill(item);
    const int mindelay_skill = _item_training_target(item);

    const bool could_set_target = _could_set_training_target(item, true);
    bool could_set_dual_target = false;

    if (item.base_type == OBJ_SHIELDS)
    {
        description += make_stringf(
            "\nBase accuracy: %+d  Base damage: %d  Base attack delay: %.1f"
            "\nThis weapon's minimum attack delay (%.1f) is reached at skill level %d.",
            property(item, PSHD_HIT),
            base_dam,
            (float)property(item, PSHD_SPEED) / 10,
            (float)weapon_min_delay(item, item_brand_known(item)) / 10,
            mindelay_skill / 10);
    }

    if (item.base_type == OBJ_WEAPONS || item.base_type == OBJ_STAVES)
    {
        description += make_stringf(
            "\nBase accuracy: %+d  Base damage: %d  Base attack delay: %.1f"
            "\nThis weapon's minimum attack delay (%.1f) is reached at skill level %d.",
            property(item, PWPN_HIT),
            base_dam + ammo_dam,
            (float)property(item, PWPN_SPEED) / 10,
            (float)weapon_min_delay(item, item_brand_known(item)) / 10,
            mindelay_skill / 10);
    }

    int skill_level = 0;
    int skill_level1 = 0;
    bool dual = false;

    if (you.weapon(0) && you.weapon(1) && 
        is_melee_weapon(*you.weapon(0)) && is_melee_weapon(*you.weapon(1)) &&
        (you.weapon(0) == &item ||you.weapon(1) == &item))
    {
        const int delay = dual_wield_base_delay(*you.weapon(0), *you.weapon(1));
        const int min_delay = max(weapon_min_delay(*you.weapon(0)), weapon_min_delay(*you.weapon(1)));

        if (item_attack_skill(*you.weapon(0)) == item_attack_skill(*you.weapon(1)))
        {
            skill_level = 10 * dual_wield_mindelay_skill(*you.weapon(0), *you.weapon(1));

            description += make_stringf(
                "\n\nDual wielding stats based on both currently wielded weapons."
                "\nBase dual wielding attack delay: %.1f"
                "\nThis combinations's minimum attack delay (%.1f) is reached at skill level %d.",

                (float)delay / 10,
                (float)min_delay / 10,
                skill_level/10);

            could_set_dual_target = _dual_wield_target(item, skill_level);
        }
        else
        {

            skill_level  = div_round_up(3 * _item_training_target(*you.weapon(0)), 2);
            skill_level1 = div_round_up(3 * _item_training_target(*you.weapon(1)), 2);
            dual = true;
            could_set_dual_target = _dual_wield_target(*you.weapon(0), skill_level) || _dual_wield_target(*you.weapon(1), skill_level1);

            description += make_stringf(
                "\n\nDual wielding stats based on both currently wielded weapons."
                "\nBase dual wielding attack delay: %.1f"
                "\nThis combinations's minimum attack delay (%.1f) is reached with"
                "\nboth weapon's skills, %d (%s) and %d (%s).",

                (float)delay / 10,
                (float)min_delay / 10,
                skill_level / 10,
                skill_name(item_attack_skill(*you.weapon(0))),
                skill_level1 / 10,
                skill_name(item_attack_skill(*you.weapon(1))));
        }
    }

    if (!is_useless_item(item))
    {
        if (dual)
            description += "\n    " + _your_dual_skill_desc(item_attack_skill(*you.weapon(0)),
                item_attack_skill(*you.weapon(1)),
                could_set_dual_target && in_inventory(item), skill_level, skill_level1);
        else
            description += "\n    " + _your_skill_desc(skill,
                could_set_dual_target || could_set_target && in_inventory(item), max(mindelay_skill, skill_level));
    }

    if (could_set_dual_target)
    {
        if (dual)
        {
            _append_skill_target_desc(description, item_attack_skill(*you.weapon(0)), skill_level, true);
            _append_skill_target_desc(description, item_attack_skill(*you.weapon(1)), skill_level1, true);
        }
        else
            _append_skill_target_desc(description, skill, skill_level, false);
    }
    else if (could_set_target)
        _append_skill_target_desc(description, skill, mindelay_skill, false);
}

static string _handedness_string(const item_def &item)
{
    string description;

    switch (you.hands_reqd(item))
    {
    case HANDS_ONE:
        if (you.species == SP_FORMICID)
            description += "It is wielded with one pair of hands.";
        else
            description += "It is one handed.";
        break;
    case HANDS_TWO:
        description += "It is two handed.";
        break;
    }

    return description;
}

static string _armour_brand_desc(const item_def item)
{
    string description = "\n\n";

    int ego = get_armour_ego_type(item);

    switch (ego)
    {
    case SPARM_RUNNING:
        if (item.sub_type == ARM_NAGA_BARDING)
            description += "It allows its wearer to slither at a great speed.";
        else
            description += "It allows its wearer to run at a great speed.";
        break;
    case SPARM_FIRE_RESISTANCE:
        description += "It protects its wearer from heat.";
        break;
    case SPARM_COLD_RESISTANCE:
        description += "It protects its wearer from cold.";
        break;
    case SPARM_POISON_RESISTANCE:
        description += "It protects its wearer from poison.";
        break;
    case SPARM_IMPROVED_VISION:
        description += "It improves the wearer's vision.";
        break;
    case SPARM_INVISIBILITY:
        description += "When activated it hides its wearer from "
            "the sight of others, but also increases "
            "their metabolic rate by a large amount.";
        break;
    case SPARM_STRENGTH:
        description += "It increases the physical power of its wearer (+3 to strength).";
        break;
    case SPARM_DEXTERITY:
        description += "It increases the dexterity of its wearer (+3 to dexterity).";
        break;
    case SPARM_INTELLIGENCE:
        description += "It makes you more clever (+3 to intelligence).";
        break;
    case SPARM_PONDEROUSNESS:
        description += "It is very cumbersome, thus slowing your movement.";
        break;
    case SPARM_INSULATION:
        description += "Its thick rubbery material insulates against electric shocks.";
        break;
    case SPARM_MAGIC_RESISTANCE:
        description += "It increases its wearer's resistance "
            "to enchantments.";
        break;
    case SPARM_PROTECTION:
        description += "It protects its wearer from harm (+3 to AC).";
        break;
    case SPARM_STEALTH:
        description += "It greatly increases the stealth of the wearer, when they are walking on dry land.";
        break;
    case SPARM_SOFT:
        description += "Its extreme softness protects the wearer from petrification.";
        break;
    case SPARM_RESISTANCE:
        description += "It protects its wearer from the effects "
            "of both cold and heat.";
        break;
    case SPARM_POSITIVE_ENERGY:
        description += "It protects its wearer from "
            "the effects of negative energy.";
        break;

        // This is only for robes.
    case SPARM_ARCHMAGI:
        description += "It increases the power of its wearer's "
            "magical spells.";
        break;
    case SPARM_HIGH_PRIEST:
        description += "It increases the strength god powers "
            "when invoked by the wearer.";
        break;
#if TAG_MAJOR_VERSION == 34
    case SPARM_PRESERVATION:
        description += "It does nothing special.";
        break;
#endif
    case SPARM_REFLECTION:
        description += "It reflects blocked things back in the "
            "direction they came from.";
        break;

    case SPARM_SPIRIT_SHIELD:
        description += "It shields its wearer from harm at the cost "
            "of magical power.";
        break;

    case SPARM_NORMAL:
        description += "It has no special ego (it is not resistant to "
            "fire, etc), but is still enchanted in some way - "
            "positive or negative.";

        break;

        // These are only for gloves.
    case SPARM_ARCHERY:
        description += "It improves your effectiveness with ranged "
            "weaponry, such as bows and javelins (Slay+4).";
        break;

    case SPARM_WIELDING:
        description += "A magical aura from these gloves protects you"
            " against any negative effects coming from your weapon,"
            " granting you the ability to swap and attack with various"
            " weapons without fear of many negative artifact properties or"
            " brand effects. Additionally it improves your grip on melee"
            " weapons, making you more effective with them (Slay+2).";
        break;

    case SPARM_STURDY:
        description += "These boots make your extremely sure on your"
            " feet. Making you immune to all effects that would move you"
            " against your will.";
        break;

        // These are only for scarves.
    case SPARM_REPULSION:
        description += "It protects its wearer by repelling missiles.";
        break;

    case SPARM_CLOUD_IMMUNE:
        description += "It completely protects its wearer from the effects of clouds.";
        break;
    }

    return description;
}

static string _weapon_brand_desc(const item_def &item)
{

    const int damtype = get_vorpal_type(item);

    string description = "\n\n";

    switch (get_weapon_brand(item))
    {
    case SPWPN_ACID:
        description += "It is coated in a slimy acidic goo that may deal extra damage to those"
            " that don't resist corrosion. Additionally may debuff the target's defensive"
            " and weapon capabilities by coating them in acid.";

        if (!is_range_weapon(item) &&
            (damtype == DVORP_SLICING || damtype == DVORP_CHOPPING
                || damtype == DVORP_DP || damtype == DVORP_TP))
        {
            description += " Big, acidic blades are also staple "
                "armaments of hydra-hunters.";
        }
        break;
    case SPWPN_MOLTEN:
        if (is_range_weapon(item))
        {
            description += "It melts metal ammo placed within it; making them malleable so they can"
                " partially ignore armour. Causes less base damage than a standard weapon; but partially"
                " ignores enemy's defense, occasionally melts through shields, and burns causing"
                " additional damage to those that don't resist heat.";
        }
        else
            description += "Its malleable surface is completely molten, allowing it to meld around and"
            " partially ignore armour. Causes less base damage than a standard weapon; but partially"
            " ignores enemy's defense and burns causing additional damage to those that don't resist heat.";

        if (!is_range_weapon(item) &&
            (damtype == DVORP_SLICING || damtype == DVORP_CHOPPING
                || damtype == DVORP_DP || damtype == DVORP_TP))
        {
            description += " Big, molten blades are also staple "
                "armaments of hydra-hunters.";
        }
        break;
    case SPWPN_FREEZING:
        if (is_range_weapon(item))
        {
            description += "It causes projectiles fired from it to freeze "
                "those they strike,";
        }
        else
        {
            description += "It has been specially enchanted to freeze "
                "those struck by it,";
        }
        description += " causing extra injury to most foes "
            "and up to half again as much damage against particularly "
            "susceptible opponents.";
        if (is_range_weapon(item))
            description += " They";
        else
            description += " It";
        description += " can also slow down cold-blooded creatures.";
        break;
    case SPWPN_HOLY_WRATH:
        description += "It has been blessed by the Shining One";
        if (is_range_weapon(item))
        {
            description += ", and any ";
            description += ammo_name(item);
            description += " fired from it will";
        }
        else
            description += " to";
        description += " cause great damage to the undead and demons.";
        break;
    case SPWPN_ELECTROCUTION:
        if (is_range_weapon(item))
        {
            description += "It charges the ammunition it shoots with "
                "electricity; occasionally upon a hit, such missiles "
                "may discharge and cause terrible harm.";
        }
        else
        {
            description += "Occasionally, upon striking a foe, it will "
                "discharge some electrical energy and cause terrible "
                "harm.";
        }
        break;
    case SPWPN_DRAGON_SLAYING:
        description += "This legendary weapon is deadly to all "
            "dragonkind.";
        break;
    case SPWPN_VENOM:
        if (is_range_weapon(item))
            description += "It poisons the ammo it fires.";
        else
            description += "It poisons the flesh of those it strikes.";
        break;
    case SPWPN_PROTECTION:
        description += "It protects the one who uses it against "
            "injury (+AC).";
        break;
    case SPWPN_DRAINING:
        description += "A truly terrible weapon, it drains the "
            "life of those it strikes.";
        break;
    case SPWPN_SPEED:
        description += "Attacks with this weapon are significantly faster.";
        break;
    case SPWPN_VORPAL:
        if (is_range_weapon(item))
        {
            description += "Any ";
            description += ammo_name(item);
            description += " fired from it inflicts extra damage.";
        }
        else
        {
            description += "It inflicts extra damage upon your "
                "enemies.";
        }
        break;
    case SPWPN_CHAOS:
        if (is_range_weapon(item))
        {
            description += "Each projectile launched from it has a "
                "different, random effect.";
        }
        else
        {
            description += "Each time it hits an enemy it has a "
                "different, random effect.";
        }
        break;
    case SPWPN_VAMPIRISM:
        description += "It inflicts no extra harm, but heals "
            "its wielder when it wounds a living foe.";
        break;
    case SPWPN_PAIN:
        description += "In the hands of one skilled in necromantic "
            "magic, it inflicts extra damage on living creatures.";
        break;
    case SPWPN_DISTORTION:
        description += "It warps and distorts space around it.";
        if (!you.wearing_ego(EQ_GLOVES, SPARM_WIELDING) && !have_passive(passive_t::safe_distortion))
            description += " Unwielding it can cause banishment or high damage.";
        break;
    case SPWPN_PENETRATION:
        description += "Ammo fired by it will pass through the "
            "targets it hits, potentially hitting all targets in "
            "its path until it reaches maximum range.";
        break;
    case SPWPN_REAPING:
        description += "If a monster killed with it leaves a "
            "corpse in good enough shape, the corpse will be "
            "animated as a zombie friendly to the killer.";
        break;
    case SPWPN_ANTIMAGIC:
        description += "It reduces the magical energy of the wielder, "
            "and disrupts the spells and magical abilities of those "
            "hit. Natural abilities and divine invocations are not "
            "affected.";
        break;
    case SPWPN_NORMAL:
        description += "It has no special brand (it is not molten, "
            "freezing, etc), but is still enchanted in some way - "
            "positive or negative.";
        break;
    case SPWPN_SILVER:
        description += "It deals substantially increased damage to chaotic "
            "and magically transformed beings. It also inflicts "
            "extra damage against mutated beings, according to "
            "how mutated they are.";
        break;
    default:
        description += "This is a buggy removed brand.";
        break;
    }

    if (you.duration[DUR_EXCRUCIATING_WOUNDS] && &item == you.weapon(0))
    {
        description += "\nIt is temporarily rebranded; it is actually a";
        if ((int)you.props[ORIGINAL_BRAND_KEY] == SPWPN_NORMAL)
            description += "n unbranded weapon.";
        else
        {
            description += " weapon of "
                + ego_type_string(item, false, (brand_type)you.props[ORIGINAL_BRAND_KEY].get_int())
                + ".";
        }
    }

    return description;
}

static string _warlock_mirror_reflect_desc()
{
    const int SH = crawl_state.need_save ? player_shield_class() : 0;
    const int reflect_chance = 100 * SH / omnireflect_chance_denom(SH);
    return "\n\nWith your current SH, it has a " + to_string(reflect_chance) +
        "% chance to reflect enchantments and other normally unblockable "
        "effects.";
}

static string _describe_shield(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    description = "";

    if (verbose)
    {
        description += "\n";
        if (is_hybrid(item.sub_type))
            _append_weapon_stats(description, item);

        if (item_type_known(item) && is_hybrid(item.sub_type)
            && get_weapon_brand(item) != SPWPN_NORMAL)
            description += _weapon_brand_desc(item);
    }

    const int target_skill = _item_training_target(item);
    const int penalty_skill = round(you.get_shield_skill_to_offset_penalty(item) * 10);
    description += "\n";
    description += "\nBase shield rating: "
        + to_string(property(item, PSHD_SH));
    const bool could_set_target = _could_set_training_target(item, true);

    if (!is_useless_item(item))
    {
        description += "       Skill to remove penalty: "
            + make_stringf("%d.%d", penalty_skill / 10,
                penalty_skill % 10);

        if (crawl_state.need_save)
        {
            description += "\n                            "
                + _your_skill_desc(item_attack_skill(item),
                    could_set_target && in_inventory(item), target_skill);
        }
        else
            description += "\n";
        if (could_set_target)
        {
            _append_skill_target_desc(description, item_attack_skill(item),
                target_skill, false);
        }
    }

    if (!is_hybrid(item.sub_type))
    {
        int ego = get_armour_ego_type(item);

        if (ego != SPARM_NORMAL && item_type_known(item) && verbose)
        {
            description += _armour_brand_desc(item);
        }
    }

    if (is_unrandom_artefact(item, UNRAND_WARLOCK_MIRROR))
        description += _warlock_mirror_reflect_desc();

    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            description += "\n";
            description += rand_desc;
        }

        // Can't happen, right? (XXX)
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES) && item_type_known(item))
            description += "\nThis shield may have some hidden properties.";
    }

    if (!is_artefact(item))
    {
        int max_ench = MAX_WPN_ENCHANT;
        if (!is_weapon(item))
            max_ench = property(item, PSHD_SH);

        if (item_ident(item, ISFLAG_KNOW_PLUSES) && item.plus >= max_ench)
            description += "\n\nIt cannot be enchanted further.";
        else
        {
            description += "\n\nIt can be maximally enchanted to +"
                + to_string(max_ench) + ".";
        }
    }

    return description;
}

static string _describe_weapon(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    description = "";

    if (verbose)
    {
        description += "\n";
        _append_weapon_stats(description, item);
    }

    const int spec_ench = (is_artefact(item) || verbose)
                          ? get_weapon_brand(item) : SPWPN_NORMAL;

    if (verbose)
    {
        switch (item_attack_skill(item))
        {
        case SK_POLEARMS:
            description += "\n\nIt can be evoked to extend its reach.";
            break;
        case SK_AXES_HAMMERS:
            description += "\n\nIt hits all enemies adjacent to the wielder, "
                           "dealing less damage to those not targeted.";
            break;
        case SK_LONG_BLADES:
            description += "\n\nIt can be used to riposte, swiftly "
                           "retaliating against a missed attack.";
            break;
        case SK_SHORT_BLADES:
            {
                string adj = item.is_type(OBJ_WEAPONS, WPN_KATAR) ? "extremely"
                                                                  : "particularly";
                description += "\n\nIt is " + adj + " good for stabbing"
                               " unaware enemies.";
            }
            break;
        default:
            break;
        }
    }

    if (spec_ench != SPWPN_NORMAL && item_type_known(item))
        description += _weapon_brand_desc(item);

    if (is_unrandom_artefact(item, UNRAND_STORM_BOW))
    {
        description += "\n\nAmmo fired by it will pass through the "
            "targets it hits, potentially hitting all targets in "
            "its path until it reaches maximum range.";
    }
    else if (is_unrandom_artefact(item, UNRAND_THERMIC_ENGINE))
    {
        description += "\n\nIt has been specially enchanted to freeze "
            "those struck by it, causing extra injury to most foes "
            "and up to half again as much damage against particularly "
            "susceptible opponents.";
    }

    if (you.duration[DUR_EXCRUCIATING_WOUNDS] && &item == you.weapon())
    {
        description += "\nIt is temporarily rebranded; it is actually a";
        if ((int) you.props[ORIGINAL_BRAND_KEY] == SPWPN_NORMAL)
            description += "n unbranded weapon.";
        else
        {
            description += " weapon of "
                        + ego_type_string(item, false,
                           (brand_type) you.props[ORIGINAL_BRAND_KEY].get_int())
                        + ".";
        }
    }

    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            description += "\n";
            description += rand_desc;
        }

        // XXX: Can't happen, right?
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES)
            && item_type_known(item))
        {
            description += "\nThis weapon may have some hidden properties.";
        }
    }

    if (verbose)
    {
        description += "\n\nThis ";
        if (is_unrandom_artefact(item))
            description += get_artefact_base_name(item);
        else
            description += "weapon";
        description += " falls into the";

        const skill_type skill = item_attack_skill(item);

        description +=
            make_stringf(" '%s' category. ",
                         skill == SK_FIGHTING ? "buggy" : skill_name(skill));

        description += _handedness_string(item);

        if (!you.could_wield(item, true) && crawl_state.need_save)
        {
            if (you.body_size(PSIZE_TORSO,true) < SIZE_MEDIUM)
                description += "\nIt is too large for you to wield.";
            else
                description += "\nIt is too small for you to wield.";

        }
    }

    if (!is_artefact(item))
    {
        if (item_ident(item, ISFLAG_KNOW_PLUSES) && item.plus >= MAX_WPN_ENCHANT)
            description += "\nIt cannot be enchanted further.";
        else
        {
            description += "\nIt can be maximally enchanted to +"
                           + to_string(MAX_WPN_ENCHANT) + ".";
        }
    }

    return description;
}

static string _describe_ammo(const item_def &item)
{
    string description;

    description.reserve(64);

    const bool can_launch = has_launcher(item);
    const bool can_throw  = is_throwable(&you, item, true);

    if (item.brand && item_type_known(item))
    {
        description += "\n\n";

        string threw_or_fired;
        if (can_throw)
        {
            threw_or_fired += "threw";
            if (can_launch)
                threw_or_fired += " or ";
        }
        if (can_launch)
            threw_or_fired += "fired";

        switch (item.brand)
        {
#if TAG_MAJOR_VERSION == 34
        case SPMSL_FLAME:
            description += "It burns those it strikes, causing extra injury "
                    "to most foes and up to half again as much damage against "
                    "particularly susceptible opponents. Compared to normal "
                    "ammo, it is twice as likely to be destroyed on impact.";
            break;
        case SPMSL_FROST:
            description += "It freezes those it strikes, causing extra injury "
                    "to most foes and up to half again as much damage against "
                    "particularly susceptible opponents. It can also slow down "
                    "cold-blooded creatures. Compared to normal ammo, it is "
                    "twice as likely to be destroyed on impact.";
            break;
#endif
        case SPMSL_CHAOS:
            description += "When ";

            if (can_throw)
            {
                description += "thrown, ";
                if (can_launch)
                    description += "or ";
            }

            if (can_launch)
                description += "fired from an appropriate launcher, ";

            description += "it has a random effect.";
            break;
        case SPMSL_POISONED:
            description += "It is coated with poison.";
            break;
        case SPMSL_CURARE:
            description += "It is tipped with a substance that causes "
                           "asphyxiation, dealing direct damage as well as "
                           "poisoning and slowing those it strikes.\n"
                           "It is twice as likely to be destroyed on impact as "
                           "other needles.";
            break;
        case SPMSL_PETRIFICATION:
            description += "It is tipped with a petrifying substance.";
            break;
        case SPMSL_SLEEP:
            description += "It is coated with a fast-acting tranquilizer.";
            break;
        case SPMSL_CONFUSION:
            description += "It is tipped with a substance that causes confusion.";
            break;
#if TAG_MAJOR_VERSION == 34
        case SPMSL_SICKNESS:
            description += "It has been contaminated by something likely to cause disease.";
            break;
#endif
        case SPMSL_FRENZY:
            description += "It is tipped with a substance that sends those it "
                           "hits into a mindless rage, attacking friend and "
                           "foe alike.";
            break;
        case SPMSL_RETURNING:
            description += "A skilled user can throw it in such a way that it "
                           "will return to its owner.";
            break;
        case SPMSL_PENETRATION:
            description += "It will pass through any targets it hits, "
                           "potentially hitting all targets in its path until "
                           "it reaches its maximum range.";
            break;
        case SPMSL_DISPERSAL:
            description += "It will cause any target it hits to blink, with a "
                           "tendency towards blinking further away from the "
                           "one who " + threw_or_fired + " it.";
            break;
        case SPMSL_EXPLODING:
            description += "It will explode into fragments upon hitting a "
                           "target, hitting an obstruction, or reaching its "
                           "maximum range.";
            break;
        case SPMSL_STEEL:
            description += "It deals increased damage compared to normal ammo.";
            break;
        case SPMSL_SILVER:
            description += "It deals substantially increased damage to chaotic "
                           "and magically transformed beings. It also inflicts "
                           "extra damage against mutated beings, according to "
                           "how mutated they are.";
            break;
        }
    }

    const int dam = property(item, PWPN_DAMAGE);
    if (dam)
    {
        const int throw_delay = (10 + dam / 2);

        description += make_stringf(
            "\nBase damage: %d  Base attack delay: %.1f",
            dam, (float) throw_delay / 10);
    }

    return description;
}

static string _describe_armour(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    if (verbose)
    {
        const int evp = property(item, PARM_EVASION);
        description += "\n\nBase armour rating: "
                    + to_string(property(item, PARM_AC));
        if (get_armour_slot(item) == EQ_BODY_ARMOUR)
        {
            description += "       Encumbrance rating: "
                        + to_string(-evp / 10);
        }
        // Bardings reduce evasion by a fixed amount, and don't have any of
        // the other effects of encumbrance.
        else if (evp)
        {
            description += "       Evasion: "
                        + to_string(evp / 30);
        }

        // only display player-relevant info if the player exists
        if (crawl_state.need_save && get_armour_slot(item) == EQ_BODY_ARMOUR)
            description += make_stringf("\nWearing mundane armour of this type "
                                        "will give the following: %d AC",
                                         you.base_ac_from(item, 100) / 100);
    }

    const int ego = get_armour_ego_type(item);
    const bool enchanted = get_equip_desc(item) && ego == SPARM_NORMAL
                           && !item_ident(item, ISFLAG_KNOW_PLUSES);

    if ((ego != SPARM_NORMAL || enchanted) && item_type_known(item) && verbose)
    {
        description += _armour_brand_desc(item);
    }

    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            description += "\n";
            description += rand_desc;
        }

        // Can't happen, right? (XXX)
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES) && item_type_known(item))
            description += "\nThis armour may have some hidden properties.";
    }
    else
    {
        const int max_ench = armour_max_enchant(item);
        if (item.plus < max_ench || !item_ident(item, ISFLAG_KNOW_PLUSES))
        {
            description += "\n\nIt can be maximally enchanted to +"
                           + to_string(max_ench) + ".";
        }
        else
            description += "\n\nIt cannot be enchanted further.";
    }

    return description;
}

static string _describe_jewellery(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    if (verbose && !is_artefact(item))
    {
        // Explicit description of ring power.
        switch (item.sub_type)
        {
        case RING_PROTECTION:
            description += make_stringf("\nIt boosts your AC (%+d).",
                                        5);
            break;

        case RING_EVASION:
            description += make_stringf("\nIt boosts your evasion (%+d).",
                                        5);
            break;

        case RING_STRENGTH:
            description += make_stringf("\nIt boosts your strength (%+d).",
                                        5);
            break;

        case RING_INTELLIGENCE:
            description += make_stringf("\nIt boosts your intelligence (%+d).",
                                        5);
            break;

        case RING_DEXTERITY:
            description += make_stringf("\nIt boosts your dexterity (%+d).",
                                        5);
            break;

        case RING_SLAYING:
            description += make_stringf("\nIt boosts your accuracy and"
                    " damage with ranged weapons and melee attacks (%+d).",
                    5);
            break;

        case AMU_REFLECTION:
            description += make_stringf("\nIt boosts your shielding (%+d).",
                                        5);
            break;

        default:
            break;
        }
    }

    // Artefact properties.
    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            description += "\n";
            description += rand_desc;
        }
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES) ||
            !item_ident(item, ISFLAG_KNOW_TYPE))
        {
            description += "\nThis ";
            description += (jewellery_is_amulet(item) ? "amulet" : "ring");
            description += " may have hidden properties.";
        }
    }

    return description;
}

static bool _compare_card_names(card_type a, card_type b)
{
    return string(card_name(a)) < string(card_name(b));
}

static bool _check_buggy_deck(const item_def &deck, string &desc)
{
    if (!is_deck(deck))
    {
        desc += "This isn't a deck at all!\n";
        return true;
    }

    const CrawlHashTable &props = deck.props;

    if (!props.exists(CARD_KEY)
        || props[CARD_KEY].get_type() != SV_VEC
        || props[CARD_KEY].get_vector().get_type() != SV_BYTE
        || cards_in_deck(deck) == 0)
    {
        return true;
    }

    return false;
}

static string _describe_deck(const item_def &item)
{
    string description;

    description.reserve(100);

    description += "\n";

    if (_check_buggy_deck(item, description))
        return "";

    if (item_type_known(item))
        description += deck_contents(item.sub_type) + "\n";

    description += make_stringf("\nMost decks begin with %d to %d cards.",
                                MIN_STARTING_CARDS,
                                MAX_STARTING_CARDS);

    const vector<card_type> drawn_cards = get_drawn_cards(item);
    if (!drawn_cards.empty())
    {
        description += "\n";
        description += "Drawn card(s): ";
        description += comma_separated_fn(drawn_cards.begin(),
                                          drawn_cards.end(),
                                          card_name);
    }

    const int num_cards = cards_in_deck(item);
    // The list of known cards, ending at the first one not known to be at the
    // top.
    vector<card_type> seen_top_cards;
    // Seen cards in the deck not necessarily contiguous with the start. (If
    // Nemelex wrath shuffled a deck that you stacked, for example.)
    vector<card_type> other_seen_cards;
    bool still_contiguous = true;
    for (int i = 0; i < num_cards; ++i)
    {
        uint8_t flags;
        const card_type card = get_card_and_flags(item, -i-1, flags);
        if (flags & CFLAG_SEEN)
        {
            if (still_contiguous)
                seen_top_cards.push_back(card);
            else
                other_seen_cards.push_back(card);
        }
        else
            still_contiguous = false;
    }

    if (!seen_top_cards.empty())
    {
        description += "\n";
        description += "Next card(s): ";
        description += comma_separated_fn(seen_top_cards.begin(),
                                          seen_top_cards.end(),
                                          card_name);
    }
    if (!other_seen_cards.empty())
    {
        description += "\n";
        sort(other_seen_cards.begin(), other_seen_cards.end(),
             _compare_card_names);

        description += "Seen card(s): ";
        description += comma_separated_fn(other_seen_cards.begin(),
                                          other_seen_cards.end(),
                                          card_name);
    }

    return description;
}

bool is_dumpable_artefact(const item_def &item)
{
    return is_known_artefact(item) && item_ident(item, ISFLAG_KNOW_PROPERTIES);
}

/**
 * Describe a specified item.
 *
 * @param item    The specified item.
 * @param verbose Controls various switches for the length of the description.
 * @param dump    This controls which style the name is shown in.
 * @param lookup  If true, the name is not shown at all.
 *   If either of those two are true, the DB description is not shown.
 * @return a string with the name, db desc, and some other data.
 */
string get_item_description(const item_def &item, bool verbose,
                            bool dump, bool lookup)
{
    ostringstream description;

#ifdef DEBUG_DIAGNOSTICS
    if (!dump && !you.suppress_wizard)
    {
        description << setfill('0');
        description << "\n\n"
                    << "base: " << static_cast<int>(item.base_type)
                    << " sub: " << static_cast<int>(item.sub_type)
                    << " plus: " << item.plus << " plus2: " << item.plus2
                    << " special: " << item.special
                    << "\n"
                    << "quant: " << item.quantity
                    << " rnd?: " << static_cast<int>(item.rnd)
                    << " flags: " << hex << setw(8) << item.flags
                    << dec << "\n"
                    << "x: " << item.pos.x << " y: " << item.pos.y
                    << " link: " << item.link
                    << " slot: " << item.slot
                    << " ident_type: "
                    << get_ident_type(item)
                    << "\nannotate: "
                    << stash_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item);
    }
#endif

    if (verbose || (item.base_type != OBJ_WEAPONS
                    && item.base_type != OBJ_ARMOURS
                    && item.base_type != OBJ_BOOKS
                    && item.base_type != OBJ_SHIELDS))
    {
        description << "\n\n";

        bool need_base_desc = !lookup;

        if (dump)
        {
            description << "["
                        << item.name(DESC_DBNAME, true, false, false)
                        << "]";
            need_base_desc = false;
        }
        else if (is_unrandom_artefact(item) && item_type_known(item))
        {
            const string desc = getLongDescription(get_artefact_name(item));
            if (!desc.empty())
            {
                description << desc;
                need_base_desc = false;
                description.seekp((streamoff)-1, ios_base::cur);
                description << " ";
            }
        }
        // Randart jewellery properties will be listed later,
        // just describe artefact status here.
        else if (is_artefact(item) && item_type_known(item)
                 && item.base_type == OBJ_JEWELLERY)
        {
            description << "It is an ancient artefact.";
            need_base_desc = false;
        }

        if (need_base_desc)
        {
            string db_name = item.name(DESC_DBNAME, true, false, false);
            string db_desc = getLongDescription(db_name);

            if (db_desc.empty())
            {
                if (item_type_known(item))
                {
                    description << "[ERROR: no desc for item name '" << db_name
                                << "']. Perhaps this item has been removed?\n";
                }
                else
                {
                    description << uppercase_first(item.name(DESC_A, true,
                                                             false, false));
                    description << ".\n";
                }
            }
            else
                description << db_desc;

            // Get rid of newline at end of description; in most cases we
            // will be adding "\n\n" immediately, and we want only one,
            // not two, blank lines. This allow allows the "unpleasant"
            // message for chunks to appear on the same line.
            description.seekp((streamoff)-1, ios_base::cur);
            description << " ";
        }
    }

    bool need_extra_line = true;
    string desc;
    switch (item.base_type)
    {
    // Weapons, armour, jewellery, books might be artefacts.
    case OBJ_WEAPONS:
        desc = _describe_weapon(item, verbose);
        if (desc.empty())
            need_extra_line = false;
        else
            description << desc;
        break;

    case OBJ_SHIELDS:
        desc = _describe_shield(item, verbose);
        if (desc.empty())
            need_extra_line = false;
        else
            description << desc;
        break;

    case OBJ_ARMOURS:
        desc = _describe_armour(item, verbose);
        if (desc.empty())
            need_extra_line = false;
        else
            description << desc;
        break;

    case OBJ_JEWELLERY:
        desc = _describe_jewellery(item, verbose);
        if (desc.empty())
            need_extra_line = false;
        else
            description << desc;
        break;

    case OBJ_BOOKS:
        if (!verbose && is_random_artefact(item))
        {
            desc += describe_item_spells(item);
            if (desc.empty())
                need_extra_line = false;
            else
                description << desc;
        }
        break;

    case OBJ_MISSILES:
        description << _describe_ammo(item);
        break;

    case OBJ_CORPSES:
        if (item.sub_type == CORPSE_SKELETON)
            break;

        // intentional fall-through
    case OBJ_FOOD:
        if (item.base_type == OBJ_CORPSES || item.sub_type == FOOD_CHUNK)
        {
            switch (determine_chunk_effect(item))
            {
            case CE_NOXIOUS:
                description << "\n\nThis meat is toxic.";
                break;
            default:
                break;
            }
        }
        break;

    case OBJ_STAVES:
        {
            string stats = "\n";
            _append_weapon_stats(stats, item);
            description << stats;
        }
        switch (get_staff_facet(item))
        {
        case SPSTF_ACCURACY: 
            description << "\n\nSpells enhanced by this staff never miss.";
            break;
        case SPSTF_CHAOS:
            description << "\n\nSpells enhanced by this staff find their maximum damage significantly boosted, ";
            description << "but their damage type scrambled completely randomly. Additionally they have random ";
            description << "effects on those damaged by their magic, including debilitating hexes and powerful charms.";
            break;
        case SPSTF_ENERGY:
            description << "\n\nSpells enhanced by this staff cost the caster one less mana to cast.";
            break;
        case SPSTF_FLAY:
            description << "\n\nThose struck with this staff may have their resistance to the elemental damage of its spells reduced. ";
            description << "The odds of this reduction depend on the wielder's skill with hexes. Additionally boosts the power of hexes spells.";
            break;
        case SPSTF_MENACE:
            description << "\n\nThe minimum and maximum damage of damaging spells cast with this staff are significantly increased.";
            break;
        case SPSTF_REAVER:
            description << "\n\nIncreases the wielders strength by five. Additionally they will find that their armour encumbers the spellcasting of ";
            description << "spells enhanced by this staff less than other spells.";
            break;
        case SPSTF_SCOPED:
            description << "\n\nMany spells enhanced by this staff can reach one tile farther away.";
            break;
        case SPSTF_SHIELD:
            description << "\n\nExudes a magical forcefield from the head of the staff; granting some measure of shielding for the wielder. ";
            description << "The strength of the forcefield depends on the wielder's skill in the staff's primary element and charms. ";
            description << "Additional increases the spellpower of charms spells.";
            break;
        case SPSTF_WARP:
            description << "\n\nA strange crooked staff. Spells that are enhanced by this staff, which normally take a beam trajectory will be ";
            description << "teleported directly to their targets. Additionally enhances translocations spells.";
            break;
        case SPSTF_WIZARD:
            description << "\n\nThis staff decreases the difficulty of casting the spells it enhances.";
            break;
        default:
            break;
        }
        description << "\n\nIt falls into the 'Staves' category. ";
        description << _handedness_string(item);
        description << "\n\n";
        if (!is_artefact(item) && item.plus < 9)
            description << "It can be maximally enchanted to +9. ";
        description << "Its enchantment grants additive spellpower to the spells it enhances.";
        break;

    case OBJ_MISCELLANY:
        if (is_deck(item))
            description << _describe_deck(item);
        if (item.sub_type == MISC_ZIGGURAT && you.zigs_completed)
        {
            const int zigs = you.zigs_completed;
            description << "\n\nIt is surrounded by a "
                        << (zigs >= 27 ? "blinding " : // just plain silly
                            zigs >=  9 ? "dazzling " :
                            zigs >=  3 ? "bright " :
                                         "gentle ")
                        << "glow.";
        }
        if (is_xp_evoker(item))
        {
            description << "\n\nOnce "
                        << (item.sub_type == MISC_LIGHTNING_ROD
                            ? "all charges have been used"
                            : "activated")
                        << ", this device "
                        << (!item_is_horn_of_geryon(item) ?
                           "and all other devices of its kind " : "")
                        << "will be rendered temporarily inert. However, "
                        << (!item_is_horn_of_geryon(item) ? "they " : "it ")
                        << "will recharge as you gain experience."
                        << (!evoker_charges(item.sub_type) ?
                           " The device is presently inert." : "");
        }
        break;

    case OBJ_POTIONS:
#ifdef DEBUG_BLOOD_POTIONS
        // List content of timer vector for blood potions.
        if (!dump && is_blood_potion(item))
        {
            item_def stack = static_cast<item_def>(item);
            CrawlHashTable &props = stack.props;
            if (!props.exists("timer"))
                description << "\nTimers not yet initialized.";
            else
            {
                CrawlVector &timer = props["timer"].get_vector();
                ASSERT(!timer.empty());

                description << "\nQuantity: " << stack.quantity
                            << "        Timer size: " << (int) timer.size();
                description << "\nTimers:\n";
                for (const CrawlStoreValue& store : timer)
                    description << store.get_int() << "  ";
            }
        }
#endif

    case OBJ_SCROLLS:
    case OBJ_ORBS:
    case OBJ_GOLD:
    case OBJ_RUNES:
    case OBJ_WANDS:
#if TAG_MAJOR_VERSION == 34
    case OBJ_RODS:
#endif
        // No extra processing needed for these item types.
        break;

    default:
        die("Bad item class");
    }

    if (!verbose && item_known_cursed(item))
    {
        description << "\nIt has a curse placed upon it. { ";
        description << artefact_inscription(item, true);
        description << "}";
    }
    else
    {
        if (verbose)
        {
            if (need_extra_line)
                description << "\n";

            if (item_known_cursed(item))
            {
                description << "\nThis item has the following curse placed upon it:";
                description << _randart_descrip(item, true);
                if (is_artefact(item))
                    description << "\n";
            }

            if (is_artefact(item))
            {
                if (item.base_type == OBJ_ARMOURS
                    || item.base_type == OBJ_WEAPONS)
                {
                    description << "\nThis ancient artefact cannot be changed "
                        "by magic or mundane means.";
                }
                // Randart jewellery has already displayed this line.
                else if (item.base_type != OBJ_JEWELLERY
                         || (item_type_known(item) && is_unrandom_artefact(item)))
                {
                    description << "\nIt is an ancient artefact.";
                }
            }
        }
    }

    if (god_hates_item(item))
    {
        description << "\n\n" << uppercase_first(god_name(you.religion))
                    << " disapproves of the use of such an item.";
    }

    if (verbose && origin_describable(item))
        description << "\n" << origin_desc(item) << ".";

    // This information is obscure and differs per-item, so looking it up in
    // a docs file you don't know to exist is tedious.
    if (verbose)
    {
        description << "\n\n" << "Stash search prefixes: "
                    << userdef_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item);
        string menu_prefix = item_prefix(item, false);
        if (!menu_prefix.empty())
            description << "\nMenu/colouring prefixes: " << menu_prefix;
    }

    return description.str();
}

string get_cloud_desc(cloud_type cloud, bool include_title)
{
    if (cloud == CLOUD_NONE)
        return "";
    const string cl_name = cloud_type_name(cloud);
    const string cl_desc = getLongDescription(cl_name + " cloud");

    string ret;
    if (include_title)
        ret = "A cloud of " + cl_name + (cl_desc.empty() ? "." : ".\n\n");
    ret += cl_desc + extra_cloud_info(cloud);
    return ret;
}

static vector<pair<string,string>> _get_feature_extra_descs(const coord_def &pos)
{
    vector<pair<string,string>> ret;
    dungeon_feature_type feat = env.map_knowledge(pos).feat();
    if (!feat_is_solid(feat))
    {
        if (haloed(pos) && !umbraed(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "A halo.", getLongDescription("haloed")));
        }
        if (umbraed(pos) && !haloed(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "An umbra.", getLongDescription("umbraed")));
        }
        if (liquefied(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "Liquefied ground.", getLongDescription("liquefied")));
        }
        if (disjunction_haloed(pos))
        {
            ret.emplace_back(pair<string,string>(
                    "Translocational energy.", getLongDescription("disjunction haloed")));
        }
    }
    if (const cloud_type cloud = env.map_knowledge(pos).cloud())
    {
        ret.emplace_back(pair<string,string>(
                    "A cloud of "+cloud_type_name(cloud)+".", get_cloud_desc(cloud, false)));
    }
    return ret;
}

void get_feature_desc(const coord_def &pos, describe_info &inf, bool include_extra)
{
    dungeon_feature_type feat = env.map_knowledge(pos).feat();

    string desc      = feature_description_at(pos, false, DESC_A, false);
    string db_name   = feat == DNGN_ENTER_SHOP ? "a shop" : desc;
    string long_desc = getLongDescription(db_name);

    inf.title = uppercase_first(desc);
    if (!ends_with(desc, ".") && !ends_with(desc, "!")
        && !ends_with(desc, "?"))
    {
        inf.title += ".";
    }

    const string marker_desc =
        env.markers.property_at(pos, MAT_ANY, "feature_description_long");

    // suppress this if the feature changed out of view
    if (!marker_desc.empty() && grd(pos) == feat)
        long_desc += marker_desc;

    // Display branch descriptions on the entries to those branches.
    if (feat_is_stair(feat))
    {
        for (branch_iterator it; it; ++it)
        {
            if (it->entry_stairs == feat)
            {
                long_desc += "\n";
                long_desc += getLongDescription(it->shortname);
                break;
            }
        }
    }

    // mention the ability to pray at altars
    if (feat_is_altar(feat))
    {
        long_desc +=
            make_stringf("\n(Pray here with '%s' to learn more.)\n",
                         command_to_string(CMD_GO_DOWNSTAIRS).c_str());
    }

    inf.body << long_desc;

    if (include_extra)
    {
        auto extra_descs = _get_feature_extra_descs(pos);
        for (const auto &d : extra_descs)
            inf.body << (d == extra_descs.back() ? "" : "\n") << d.second;
    }

    inf.quote = getQuoteString(db_name);
}

void describe_feature_wide(const coord_def& pos)
{
    typedef struct {
        string title, body, quote;
        tile_def tile;
    } feat_info;

    vector<feat_info> feats;

    {
        describe_info inf;
        get_feature_desc(pos, inf, false);
        feat_info f = { "", "", "", tile_def(TILEG_TODO, TEX_GUI)};
        f.title = inf.title;
        f.body = trimmed_string(inf.body.str());
#ifdef USE_TILE
        tileidx_t tile = tileidx_feature(pos);
        apply_variations(env.tile_flv(pos), &tile, pos);
        f.tile = tile_def(tile, get_dngn_tex(tile));
#endif
        f.quote = trimmed_string(inf.quote);
        feats.emplace_back(f);
    }
    auto extra_descs = _get_feature_extra_descs(pos);
    for (const auto &desc : extra_descs)
    {
        feat_info f = { "", "", "", tile_def(TILEG_TODO, TEX_GUI)};
        f.title = desc.first;
        f.body = trimmed_string(desc.second);
#ifdef USE_TILE
        if (desc.first == "A halo.")
            f.tile = tile_def(TILE_HALO_RANGE, TEX_FEAT);
        else if (desc.first == "An umbra.")
            f.tile = tile_def(TILE_UMBRA, TEX_FEAT);
        else if  (desc.first == "Liquefied ground.")
            f.tile = tile_def(TILE_LIQUEFACTION, TEX_FLOOR);
        else
            f.tile = tile_def(env.tile_bk_cloud(pos) & ~TILE_FLAG_FLYING, TEX_DEFAULT);
#endif
        feats.emplace_back(f);
    }
    if (crawl_state.game_is_hints())
    {
        string hint_text = trimmed_string(hints_describe_pos(pos.x, pos.y));
        if (!hint_text.empty())
        {
            feat_info f = { "", "", "", tile_def(TILEG_TODO, TEX_GUI)};
            f.title = "Hints.";
            f.body = hint_text;
            f.tile = tile_def(TILEG_STARTUP_HINTS, TEX_GUI);
            feats.emplace_back(f);
        }
    }

    auto scroller = make_shared<Scroller>();
    auto vbox = make_shared<Box>(Widget::VERT);

    for (const auto &feat : feats)
    {
        auto title_hbox = make_shared<Box>(Widget::HORZ);
#ifdef USE_TILE
        auto icon = make_shared<Image>();
        icon->set_tile(feat.tile);
        title_hbox->add_child(move(icon));
#endif
        auto title = make_shared<Text>(feat.title);
        title->set_margin_for_sdl(0, 0, 0, 10);
        title_hbox->add_child(move(title));
        title_hbox->set_cross_alignment(Widget::CENTER);

        const bool has_desc = feat.body != feat.title && feat.body != "";

        if (has_desc || &feat != &feats.back())
        {
            title_hbox->set_margin_for_crt(0, 0, 1, 0);
            title_hbox->set_margin_for_sdl(0, 0, 20, 0);
        }
        vbox->add_child(move(title_hbox));

        if (has_desc)
        {
            formatted_string desc_text = formatted_string::parse_string(feat.body);
            if (!feat.quote.empty())
            {
                desc_text.cprintf("\n\n");
                desc_text += formatted_string::parse_string(feat.quote);
            }
            auto text = make_shared<Text>(desc_text);
            if (&feat != &feats.back())
            {
                text->set_margin_for_sdl(0, 0, 20, 0);
                text->set_margin_for_crt(0, 0, 1, 0);
            }
            text->set_wrap_text(true);
            vbox->add_child(text);
        }
    }
#ifdef USE_TILE_LOCAL
    vbox->max_size().width = tiles.get_crt_font()->char_width()*80;
#endif
    scroller->set_child(move(vbox));

    auto popup = make_shared<ui::Popup>(scroller);

    bool done = false;
    popup->on_keydown_event([&](const KeyEvent& ev) {
        done = !scroller->on_event(ev);
        return true;
    });

#ifdef USE_TILE_WEB
    tiles.json_open_object();
    tiles.json_open_array("feats");
    for (const auto &feat : feats)
    {
        tiles.json_open_object();
        tiles.json_write_string("title", feat.title);
        tiles.json_write_string("body", trimmed_string(feat.body));
        tiles.json_write_string("quote", trimmed_string(feat.quote));
        tiles.json_open_object("tile");
        tiles.json_write_int("t", feat.tile.tile);
        tiles.json_write_int("tex", feat.tile.tex);
        if (feat.tile.ymax != TILE_Y)
            tiles.json_write_int("ymax", feat.tile.ymax);
        tiles.json_close_object();
        tiles.json_close_object();
    }
    tiles.json_close_array();
    tiles.push_ui_layout("describe-feature-wide", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif
}

void describe_feature_type(dungeon_feature_type feat)
{
    describe_info inf;
    string name = feature_description(feat, NUM_TRAPS, "", DESC_A, false);
    string title = uppercase_first(name);
    if (!ends_with(title, ".") && !ends_with(title, "!") && !ends_with(title, "?"))
        title += ".";
    inf.title = title;
    inf.body << getLongDescription(name);
#ifdef USE_TILE
    const tileidx_t idx = tileidx_feature_base(feat);
    tile_def tile = tile_def(idx, get_dngn_tex(idx));
    show_description(inf, &tile);
#else
    show_description(inf);
#endif
}

void get_item_desc(const item_def &item, describe_info &inf)
{
    // Don't use verbose descriptions if the item contains spells,
    // so we can actually output these spells if space is scarce.
    const bool verbose = !item.has_spells();
    string name = item.name(DESC_INVENTORY_EQUIP) + ".";
    if (!in_inventory(item))
        name = uppercase_first(name);
    inf.body << name << get_item_description(item, verbose);
}

static vector<command_type> _allowed_actions(const item_def& item)
{
    vector<command_type> actions;
    actions.push_back(CMD_ADJUST_INVENTORY);
    if ((item_equip_slot(item) == EQ_WEAPON0 || item_equip_slot(item) == EQ_WEAPON1) && !item.soul_bound())
        actions.push_back(CMD_UNWIELD_WEAPON);
    switch (item.base_type)
    {
    case OBJ_WEAPONS:
    case OBJ_STAVES:
    case OBJ_SHIELDS:
        if (_could_set_training_target(item, false) || _check_set_dual_skill(item))
            actions.push_back(CMD_SET_SKILL_TARGET);
        // intentional fallthrough
    case OBJ_MISCELLANY:
        if (!item_is_equipped(item) && item_is_wieldable(item))
            actions.push_back(CMD_WIELD_WEAPON);
        break;
    case OBJ_MISSILES:
        if (_could_set_training_target(item, false))
            actions.push_back(CMD_SET_SKILL_TARGET);
        break;
    case OBJ_ARMOURS:
        if (_could_set_training_target(item, false))
            actions.push_back(CMD_SET_SKILL_TARGET);
        if (item_is_equipped(item))
            actions.push_back(CMD_REMOVE_ARMOUR);
        else
            actions.push_back(CMD_WEAR_ARMOUR);
        break;
    case OBJ_FOOD:
        if (can_eat(item, true, false))
            actions.push_back(CMD_EAT);
        break;
    case OBJ_SCROLLS:
    //case OBJ_BOOKS: these are handled differently
        actions.push_back(CMD_READ);
        break;
    case OBJ_JEWELLERY:
        if (item_is_equipped(item))
            actions.push_back(CMD_REMOVE_JEWELLERY);
        else
            actions.push_back(CMD_WEAR_JEWELLERY);
        break;
    case OBJ_POTIONS:
        if (!you_foodless()) // mummies and lich form forbidden
            actions.push_back(CMD_QUAFF);
        break;
    default:
        ;
    }
#if defined(CLUA_BINDINGS)
    if (clua.callbooleanfn(false, "ch_item_wieldable", "i", &item))
        actions.push_back(CMD_WIELD_WEAPON);
#endif

    if (item_is_evokable(item))
        actions.push_back(CMD_EVOKE);

    actions.push_back(CMD_DROP);

    if (!crawl_state.game_is_tutorial())
        actions.push_back(CMD_INSCRIBE_ITEM);

    return actions;
}

static string _actions_desc(const vector<command_type>& actions, const item_def& item)
{
    static const map<command_type, string> act_str =
    {
        { CMD_WIELD_WEAPON, "(w)ield" },
        { CMD_UNWIELD_WEAPON, "(u)nwield" },
        { CMD_QUIVER_ITEM, "(q)uiver" },
        { CMD_WEAR_ARMOUR, "(w)ear" },
        { CMD_REMOVE_ARMOUR, "(t)ake off" },
        { CMD_EVOKE, "e(v)oke" },
        { CMD_EAT, "(e)at" },
        { CMD_READ, "(r)ead" },
        { CMD_WEAR_JEWELLERY, "(p)ut on" },
        { CMD_REMOVE_JEWELLERY, "(r)emove" },
        { CMD_QUAFF, "(q)uaff" },
        { CMD_DROP, "(d)rop" },
        { CMD_INSCRIBE_ITEM, "(i)nscribe" },
        { CMD_ADJUST_INVENTORY, "(=)adjust" },
        { CMD_SET_SKILL_TARGET, "(s)kill" },
    };
    return comma_separated_fn(begin(actions), end(actions),
                                [] (command_type cmd)
                                {
                                    return act_str.at(cmd);
                                },
                                ", or ")
           + " the " + item.name(DESC_BASENAME) + ".";
}

// Take a key and a list of commands and return the command from the list
// that corresponds to the key. Note that some keys are overloaded (but with
// mutually-exclusive actions), so it's not just a simple lookup.
static command_type _get_action(int key, vector<command_type> actions)
{
    static const map<command_type, int> act_key =
    {
        { CMD_WIELD_WEAPON,     'w' },
        { CMD_UNWIELD_WEAPON,   'u' },
        { CMD_QUIVER_ITEM,      'q' },
        { CMD_WEAR_ARMOUR,      'w' },
        { CMD_REMOVE_ARMOUR,    't' },
        { CMD_EVOKE,            'v' },
        { CMD_EAT,              'e' },
        { CMD_READ,             'r' },
        { CMD_WEAR_JEWELLERY,   'p' },
        { CMD_REMOVE_JEWELLERY, 'r' },
        { CMD_QUAFF,            'q' },
        { CMD_DROP,             'd' },
        { CMD_INSCRIBE_ITEM,    'i' },
        { CMD_ADJUST_INVENTORY, '=' },
        { CMD_SET_SKILL_TARGET, 's' },
    };

    key = tolower_safe(key);

    for (auto cmd : actions)
        if (key == act_key.at(cmd))
            return cmd;

    return CMD_NO_CMD;
}

/**
 * Do the specified action on the specified item.
 *
 * @param item    the item to have actions done on
 * @param actions the list of actions to search in
 * @param keyin   the key that was pressed
 * @return whether to stay in the inventory menu afterwards
 */
static bool _do_action(item_def &item, const vector<command_type>& actions, int keyin)
{
    const command_type action = _get_action(keyin, actions);
    if (action == CMD_NO_CMD)
        return true;

    const int slot = item.link;
    ASSERT_RANGE(slot, 0, ENDOFPACK);

    switch (action)
    {
    case CMD_WIELD_WEAPON:     wield_weapon(true, slot);            break;
    case CMD_UNWIELD_WEAPON:   
    {
        const bool handedness = (slot == you.equip[EQ_WEAPON0]);
        unwield_item(handedness, true);    
    }
    break;
    case CMD_QUIVER_ITEM:      quiver_item(slot);                   break;
    case CMD_WEAR_ARMOUR:      wear_armour(slot);                   break;
    case CMD_REMOVE_ARMOUR:    takeoff_armour(slot);                break;
    case CMD_EAT:              eat_food(slot);                      break;
    case CMD_READ:             read(&item);                         break;
    case CMD_WEAR_JEWELLERY:   puton_ring(slot);                    break;
    case CMD_REMOVE_JEWELLERY: remove_ring(slot, true);             break;
    case CMD_QUAFF:            drink(&item);                        break;
    case CMD_DROP:             drop_item(slot, item.quantity);      break;
    case CMD_INSCRIBE_ITEM:    inscribe_item(item);                 break;
    case CMD_ADJUST_INVENTORY: adjust_item(slot);                   break;
    case CMD_SET_SKILL_TARGET: target_item(item);                   break;
    case CMD_EVOKE:
#ifndef USE_TILE_LOCAL
        redraw_console_sidebar();
#endif
        evoke_item(slot);
        break;
    default:
        die("illegal inventory cmd %d", action);
    }
    return false;
}

void target_item(item_def &item)
{
    // Dual Wielding Skill Targeting
    if (you.weapon(0) && you.weapon(1) &&
        is_melee_weapon(*you.weapon(0)) && is_melee_weapon(*you.weapon(1)) &&
        (you.weapon(0) == &item || you.weapon(1) == &item))
    {
        const skill_type skill0 = _item_training_skill(*you.weapon(0));
        const skill_type skill1 = _item_training_skill(*you.weapon(1));

        if (skill0 == skill1)
        {
            const int target = 10 * dual_wield_mindelay_skill(*you.weapon(0), *you.weapon(1));

            if (target <= you.skill(skill0, 10))
                return;

            you.set_training_target(skill0, target, true);
            // ensure that the skill is at least enabled
            if (you.train[skill0] == TRAINING_DISABLED)
                you.train[skill0] = TRAINING_ENABLED;
            you.train_alt[skill0] = you.train[skill0];
            reset_training();
        }
        else
        {
            const int target0 = (40 + _item_training_target(*you.weapon(0)));
            const int target1 = (40 + _item_training_target(*you.weapon(1)));

            if (target0 > you.skill(skill0, 10))
            {
                you.set_training_target(skill0, target0, true);
                // ensure that the skill is at least enabled
                if (you.train[skill0] == TRAINING_DISABLED)
                    you.train[skill0] = TRAINING_ENABLED;
                you.train_alt[skill0] = you.train[skill0];
                reset_training();
            }
            if (target1 > you.skill(skill1, 10))
            {
                you.set_training_target(skill1, target1, true);
                // ensure that the skill is at least enabled
                if (you.train[skill1] == TRAINING_DISABLED)
                    you.train[skill1] = TRAINING_ENABLED;
                you.train_alt[skill1] = you.train[skill1];
                reset_training();
            }
        }
    }

    else {
        const skill_type skill = _item_training_skill(item);
        if (skill == SK_NONE)
            return;

        const int target = _item_training_target(item);
        if (target == 0)
            return;

        you.set_training_target(skill, target, true);
        // ensure that the skill is at least enabled
        if (you.train[skill] == TRAINING_DISABLED)
            you.train[skill] = TRAINING_ENABLED;
        you.train_alt[skill] = you.train[skill];
        reset_training();
    }
}

/**
 *  Describe any item in the game.
 *
 *  @param item       the item to be described.
 *  @param fixup_desc a function (possibly null) to modify the
 *                    description before it's displayed.
 *  @return whether to stay in the inventory menu afterwards.
 */
bool describe_item(item_def &item, function<void (string&)> fixup_desc)
{
    if (!item.defined())
        return true;

    string name = item.name(DESC_INVENTORY_EQUIP) + ".";
    if (!in_inventory(item))
        name = uppercase_first(name);

    string desc = get_item_description(item, true, false);

    string quote;
    if (is_unrandom_artefact(item) && item_type_known(item))
        quote = getQuoteString(get_artefact_name(item));
    else
        quote = getQuoteString(item.name(DESC_DBNAME, true, false, false));

    if (!(crawl_state.game_is_hints_tutorial()
          || quote.empty()))
    {
        desc += "\n\n" + quote;
    }

    if (crawl_state.game_is_hints())
        desc += "\n\n" + hints_describe_item(item);

    if (fixup_desc)
        fixup_desc(desc);

    formatted_string fs_desc = formatted_string::parse_string(desc);

    spellset spells = item_spellset(item);
    formatted_string spells_desc;
    describe_spellset(spells, &item, spells_desc, nullptr);
#ifdef USE_TILE_WEB
    string desc_without_spells = fs_desc.to_colour_string();
#endif
    fs_desc += spells_desc;

    const bool do_actions = in_inventory(item) // Dead men use no items.
            && !(you.pending_revival || crawl_state.updating_scores);

    vector<command_type> actions;
    if (do_actions)
        actions = _allowed_actions(item);

    auto vbox = make_shared<Box>(Widget::VERT);
    auto title_hbox = make_shared<Box>(Widget::HORZ);

#ifdef USE_TILE
    vector<tile_def> item_tiles;
    get_tiles_for_item(item, item_tiles, true);
    if (item_tiles.size() > 0)
    {
        auto tiles_stack = make_shared<Stack>();
        for (const auto &tile : item_tiles)
        {
            auto icon = make_shared<Image>();
            icon->set_tile(tile);
            tiles_stack->add_child(move(icon));
        }
        title_hbox->add_child(move(tiles_stack));
    }
#endif

    auto title = make_shared<Text>(name);
    title->set_margin_for_sdl(0, 0, 0, 10);
    title_hbox->add_child(move(title));

    title_hbox->set_cross_alignment(Widget::CENTER);
    title_hbox->set_margin_for_crt(0, 0, 1, 0);
    title_hbox->set_margin_for_sdl(0, 0, 20, 0);
    vbox->add_child(move(title_hbox));

    auto scroller = make_shared<Scroller>();
    auto text = make_shared<Text>(fs_desc.trim());
    text->set_wrap_text(true);
    scroller->set_child(text);
    vbox->add_child(scroller);

    formatted_string footer_text("", CYAN);
    if (!actions.empty())
    {
        if (!spells.empty())
            footer_text.cprintf("Select a spell, or ");
        footer_text += formatted_string(_actions_desc(actions, item));
        auto footer = make_shared<Text>();
        footer->set_text(footer_text);
        footer->set_margin_for_crt(1, 0, 0, 0);
        footer->set_margin_for_sdl(20, 0, 0, 0);
        vbox->add_child(move(footer));
    }

#ifdef USE_TILE_LOCAL
    vbox->max_size().width = tiles.get_crt_font()->char_width()*80;
#endif

    auto popup = make_shared<ui::Popup>(move(vbox));

    bool done = false;
    command_type action;
    int lastch;
    popup->on_keydown_event([&](const KeyEvent& ev) {
        const auto key = ev.key() == '{' ? 'i' : ev.key();
        lastch = key;
        action = _get_action(key, actions);
        if (action != CMD_NO_CMD)
            done = true;
        else if (key == ' ' || key == CK_ESCAPE)
            done = true;
        else if (scroller->on_event(ev))
            return true;
        const vector<pair<spell_type,char>> spell_map = map_chars_to_spells(spells, &item);
        auto entry = find_if(spell_map.begin(), spell_map.end(),
                [key](const pair<spell_type,char>& e) { return e.second == key; });
        if (entry == spell_map.end())
            return false;
        describe_spell(entry->first, nullptr, &item);
        done = already_learning_spell();
        return true;
    });

#ifdef USE_TILE_WEB
    tiles.json_open_object();
    tiles.json_write_string("title", name);
    desc_without_spells += "SPELLSET_PLACEHOLDER";
    trim_string(desc_without_spells);
    tiles.json_write_string("body", desc_without_spells);
    write_spellset(spells, &item, nullptr);

    tiles.json_write_string("actions", footer_text.tostring());
    tiles.json_open_array("tiles");
    for (const auto &tile : item_tiles)
    {
        tiles.json_open_object();
        tiles.json_write_int("t", tile.tile);
        tiles.json_write_int("tex", tile.tex);
        if (tile.ymax != TILE_Y)
            tiles.json_write_int("ymax", tile.ymax);
        tiles.json_close_object();
    }
    tiles.json_close_array();
    tiles.push_ui_layout("describe-item", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif

    if (action != CMD_NO_CMD)
        return _do_action(item, actions, lastch);
    else if (item.has_spells())
    {
        // only continue the inventory loop if we didn't start memorizing a
        // spell & didn't destroy the item for amnesia.
        return !already_learning_spell();
    }
    return true;
}

void inscribe_item(item_def &item)
{
    mprf_nocap(MSGCH_EQUIPMENT, "%s", item.name(DESC_INVENTORY).c_str());

    const bool is_inscribed = !item.inscription.empty();
    string prompt = is_inscribed ? "Replace inscription with what? "
                                 : "Inscribe with what? ";

    char buf[79];
    int ret = msgwin_get_line(prompt, buf, sizeof buf, nullptr,
                              item.inscription);
    if (ret)
    {
        canned_msg(MSG_OK);
        return;
    }

    string new_inscrip = buf;
    trim_string_right(new_inscrip);

    if (item.inscription == new_inscrip)
    {
        canned_msg(MSG_OK);
        return;
    }

    item.inscription = new_inscrip;

    mprf_nocap(MSGCH_EQUIPMENT, "%s", item.name(DESC_INVENTORY).c_str());
    you.wield_change  = true;
    you.redraw_quiver = true;
}

/**
 * List the simple calculated stats of a given spell, when cast by the player
 * in their current condition.
 *
 * @param spell     The spell in question.
 */
static string _player_spell_stats(const spell_type spell)
{
    string description;
    description += make_stringf("\nLevel: %d", spell_difficulty(spell));

    const string schools = spell_schools_string(spell);
    description +=
        make_stringf("        School%s: %s",
                     schools.find("/") != string::npos ? "s" : "",
                     schools.c_str());

    if (!crawl_state.need_save
        || (get_spell_flags(spell) & spflag::monster))
    {
        return description; // all other info is player-dependent
    }

    const string failure = failure_rate_to_string(raw_spell_fail(spell));
    description += make_stringf("        Fail: %s", failure.c_str());

    description += "\n\nPower : ";
    description += spell_power_string(spell);
    description += "\nRange : ";
    description += spell_range_string(spell);
    description += "\nHunger: ";
    description += spell_hunger_string(spell);
    description += "\nNoise : ";
    description += spell_noise_string(spell);
    description += "\n";
    return description;
}

string get_skill_description(skill_type skill, bool need_title)
{
    string lookup = skill_name(skill);
    string result = "";

    if (need_title)
    {
        result = lookup;
        result += "\n\n";
    }

    result += getLongDescription(lookup);

    switch (skill)
    {
        case SK_INVOCATIONS:
            if (you.species == SP_DEMIGOD || you.char_class == JOB_DEMIGOD)
            {
                result += "\n";
                result += "How on earth did you manage to pick this up?";
            }
            else if (you_worship(GOD_TROG))
            {
                result += "\n";
                result += "Note that Trog doesn't use Invocations, due to its "
                          "close connection to magic.";
            }
            break;

        case SK_SPELLCASTING:
            if (you_worship(GOD_TROG))
            {
                result += "\n";
                result += "Keep in mind, though, that Trog will greatly "
                          "disapprove of this.";
            }
            break;
        default:
            // No further information.
            break;
    }

    return result;
}

/// How much power do we think the given monster casts this spell with?
static int _hex_pow(const spell_type spell, const int hd)
{
    const int cap = 200;
    const int pow = mons_power_for_hd(spell, hd) / ENCH_POW_FACTOR;
    return min(cap, pow);
}

/**
 * What are the odds of the given spell, cast by a monster with the given
 * spell_hd, affecting the player?
 */
int hex_chance(const spell_type spell, const int hd)
{
    const int capped_pow = _hex_pow(spell, hd);
    const int chance = hex_success_chance(you.res_magic(), capped_pow,
                                          100, true);
    if (spell == SPELL_STRIP_RESISTANCE)
        return chance + (100 - chance) / 3; // ignores mr 1/3rd of the time
    return chance;
}

/**
 * Describe mostly non-numeric player-specific information about a spell.
 *
 * (E.g., your god's opinion of it, whether it's in a high-level book that
 * you can't memorise from, whether it's currently useless for whatever
 * reason...)
 *
 * @param spell     The spell in question.
 */
static string _player_spell_desc(spell_type spell)
{
    if (!crawl_state.need_save || (get_spell_flags(spell) & spflag::monster))
        return ""; // all info is player-dependent

    ostringstream description;

    // Report summon cap
    const int limit = summons_limit(spell);
    if (limit)
    {
        description << "You can sustain at most " + number_in_words(limit)
                    << " creature" << (limit > 1 ? "s" : "")
                    << " summoned by this spell.\n";
    }

    if (god_hates_spell(spell, you.religion))
    {
        description << uppercase_first(god_name(you.religion))
                    << " frowns upon the use of this spell.\n";
        if (god_loathes_spell(spell, you.religion))
            description << "You'd be excommunicated if you dared to cast it!\n";
    }
    else if (god_likes_spell(spell, you.religion))
    {
        description << uppercase_first(god_name(you.religion))
                    << " supports the use of this spell.\n";
    }

    if (!you_can_memorise(spell))
    {
        description << "\nYou cannot "
                    << (you.has_spell(spell) ? "cast" : "memorise")
                    << " this spell because "
                    << desc_cannot_memorise_reason(spell)
                    << "\n";
    }
    else if (spell_is_useless(spell, true, false))
    {
        description << "\nThis spell will have no effect right now because "
                    << spell_uselessness_reason(spell, true, false)
                    << "\n";
    }

    return description.str();
}


/**
 * Describe a spell, as cast by the player.
 *
 * @param spell     The spell in question.
 * @return          Information about the spell; does not include the title or
 *                  db description, but does include level, range, etc.
 */
string player_spell_desc(spell_type spell)
{
    return _player_spell_stats(spell) + _player_spell_desc(spell);
}

/**
 * Examine a given spell. Set the given string to its description, stats, &c.
 * If it's a book in a spell that the player is holding, mention the option to
 * memorise it.
 *
 * @param spell         The spell in question.
 * @param mon_owner     If this spell is being examined from a monster's
 *                      description, 'spell' is that monster. Else, null.
 * @param description   Set to the description & details of the spell.
 * @param item          The item holding the spell, if any.
 * @return              Whether you can memorise the spell.
 */
static bool _get_spell_description(const spell_type spell,
                                  const monster_info *mon_owner,
                                  string &description,
                                  const item_def* item = nullptr)
{
    description.reserve(500);

    const string long_descrip = getLongDescription(
                                    string(mi_spell_title(spell, mon_owner)) + " spell");

    if (!long_descrip.empty())
        description += long_descrip;
    else
    {
        description += "This spell has no description. "
                       "Casting it may therefore be unwise. "
#ifdef DEBUG
                       "Instead, go fix it. ";
#else
                       "Please file a bug report.";
#endif
    }

    if (mon_owner)
    {
        const int hd = mon_owner->spell_hd();
        const int range = spell_range(spell, mons_power_for_hd(spell, hd));
        description += "\nRange : "
                       + range_string(range, range, mons_char(mon_owner->type))
                       + "\n";

        // only display this if the player exists (not in the main menu)
        if (crawl_state.need_save && (get_spell_flags(spell) & spflag::MR_check)
#ifndef DEBUG_DIAGNOSTICS
            && mon_owner->attitude != ATT_FRIENDLY
#endif
            )
        {
            string wiz_info;
#ifdef WIZARD
            if (you.wizard)
                wiz_info += make_stringf(" (pow %d)", _hex_pow(spell, hd));
#endif
            description += you.immune_to_hex(spell)
                ? make_stringf("You cannot be affected by this "
                               "spell right now. %s\n",
                               wiz_info.c_str())
                : make_stringf("Chance to beat your MR: %d%%%s\n",
                               hex_chance(spell, hd),
                               wiz_info.c_str());
        }

    }
    else
        description += player_spell_desc(spell);

    // Don't allow memorization after death.
    // (In the post-game inventory screen.)
    if (crawl_state.player_is_dead())
        return false;

    const string quote = getQuoteString(string(mi_spell_title(spell, mon_owner)) + " spell");
    if (!quote.empty())
        description += "\n" + quote;

    if (item && item->base_type == OBJ_BOOKS
        && (in_inventory(*item)
            || item->pos == you.pos() && !is_shop_item(*item))
        && !you.has_spell(spell) && you_can_memorise(spell))
    {
        return true;
    }

    return false;
}

/**
 * Make a list of all books that contain a given spell.
 *
 * @param spell_type spell      The spell in question.
 * @return                      A formatted list of books containing
 *                              the spell, e.g.:
 *    \n\nThis spell can be found in the following books: dreams, burglary.
 *    or
 *    \n\nThis spell is not found in any books.
 */
static string _spell_sources(const spell_type spell)
{
    item_def item;
    set_ident_flags(item, ISFLAG_IDENT_MASK);
    vector<string> books;

    item.base_type = OBJ_BOOKS;
    for (int i = 0; i < NUM_FIXED_BOOKS; i++)
    {
        if (item_type_removed(OBJ_BOOKS, i))
            continue;
        for (spell_type sp : spellbook_template(static_cast<book_type>(i)))
            if (sp == spell)
            {
                item.sub_type = i;
                books.push_back(item.name(DESC_PLAIN));
            }
    }

    if (books.empty())
        return "\nThis spell is not found in any books.";

    string desc;

    desc += "\nThis spell can be found in the following book";
    if (books.size() > 1)
        desc += "s";
    desc += ":\n ";
    desc += comma_separated_line(books.begin(), books.end(), "\n ", "\n ");

    return desc;
}

/**
 * Provide the text description of a given spell.
 *
 * @param spell     The spell in question.
 * @param inf[out]  The spell's description is concatenated onto the end of
 *                  inf.body.
 */
void get_spell_desc(const spell_type spell, describe_info &inf)
{
    string desc;
    _get_spell_description(spell, nullptr, desc);
    inf.body << desc;
}

/**
 * Examine a given spell. List its description and details, and handle
 * memorizing the spell in question, if the player is able & chooses to do so.
 *
 * @param spelled   The spell in question.
 * @param mon_owner If this spell is being examined from a monster's
 *                  description, 'mon_owner' is that monster. Else, null.
 * @param item      The item holding the spell, if any.
 */
void describe_spell(spell_type spell, const monster_info *mon_owner,
                    const item_def* item, bool show_booklist)
{
    string desc;
    const bool can_mem = _get_spell_description(spell, mon_owner, desc, item);
    if (show_booklist)
        desc += _spell_sources(spell);

    auto vbox = make_shared<Box>(Widget::VERT);
#ifdef USE_TILE_LOCAL
    vbox->max_size().width = tiles.get_crt_font()->char_width()*80;
#endif

    auto title_hbox = make_shared<Box>(Widget::HORZ);
#ifdef USE_TILE
    auto spell_icon = make_shared<Image>();
    spell_icon->set_tile(tile_def(tileidx_spell(spell), TEX_GUI));
    title_hbox->add_child(move(spell_icon));
#endif

    string spl_title = mi_spell_title(spell, mon_owner);
    trim_string(desc);

    auto title = make_shared<Text>();
    title->set_text(spl_title);
    title->set_margin_for_sdl(0, 0, 0, 10);
    title_hbox->add_child(move(title));

    title_hbox->set_cross_alignment(Widget::CENTER);
    title_hbox->set_margin_for_crt(0, 0, 1, 0);
    title_hbox->set_margin_for_sdl(0, 0, 20, 0);
    vbox->add_child(move(title_hbox));

    auto scroller = make_shared<Scroller>();
    auto text = make_shared<Text>();
    text->set_text(formatted_string::parse_string(desc));
    text->set_wrap_text(true);
    scroller->set_child(move(text));
    vbox->add_child(scroller);

    if (can_mem)
    {
        auto more = make_shared<Text>();
        more->set_text(formatted_string("(M)emorise this spell.", CYAN));
        more->set_margin_for_crt(1, 0, 0, 0);
        more->set_margin_for_sdl(20, 0, 0, 0);
        vbox->add_child(move(more));
    }

    auto popup = make_shared<ui::Popup>(move(vbox));

    bool done = false;
    int lastch;
    popup->on_keydown_event([&](const KeyEvent& ev) {
        lastch = ev.key();
        done = (toupper_safe(lastch) == 'M' && can_mem || lastch == CK_ESCAPE
            || lastch == CK_ENTER || lastch == ' ');
        if (scroller->on_event(ev))
            return true;
        return done;
    });

#ifdef USE_TILE_WEB
    tiles.json_open_object();
    auto tile = tile_def(tileidx_spell(spell), TEX_GUI);
    tiles.json_open_object("tile");
    tiles.json_write_int("t", tile.tile);
    tiles.json_write_int("tex", tile.tex);
    if (tile.ymax != TILE_Y)
        tiles.json_write_int("ymax", tile.ymax);
    tiles.json_close_object();
    tiles.json_write_string("title", spl_title);
    tiles.json_write_string("desc", desc);
    tiles.json_write_bool("can_mem", can_mem);
    tiles.push_ui_layout("describe-spell", 0);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif

    if (toupper_safe(lastch) == 'M' && can_mem)
    {
        redraw_screen(); // necessary to ensure stats is redrawn (!?)
        if (!learn_spell(spell) || !you.turn_is_over)
            more();
    }
}

/**
 * Examine a given ability. List its description and details.
 *
 * @param ability   The ability in question.
 */
void describe_ability(ability_type ability)
{
    describe_info inf;
    inf.title = ability_name(ability);
    inf.body << get_ability_desc(ability, false);
#ifdef USE_TILE
    tile_def tile = tile_def(tileidx_ability(ability), TEX_GUI);
    show_description(inf, &tile);
#else
    show_description(inf);
#endif
}


static string _describe_draconian(const monster_info& mi)
{
    string description;
    const int subsp = mi.draco_or_demonspawn_subspecies();

    if (subsp != mi.type)
    {
        description += "It has ";

        switch (subsp)
        {
        case MONS_RED_DRACONIAN:            description += "fiery red";                 break;
        case MONS_WHITE_DRACONIAN:          description += "icy white";                 break;
        case MONS_GREEN_DRACONIAN:          description += "lurid green";               break;
        case MONS_CYAN_DRACONIAN:           description += "empereal azure";            break;
        case MONS_LIME_DRACONIAN:           description += "slimy lime-green";          break;
        case MONS_SILVER_DRACONIAN:         description += "gleaming silver";           break;
        case MONS_BLUE_DRACONIAN:           description += "flickering blue";           break;
        case MONS_PURPLE_DRACONIAN:         description += "rich purple";               break;
        case MONS_PINK_DRACONIAN:           description += "fierce pink";               break;
        case MONS_MAGENTA_DRACONIAN:        description += "hazy amethyst";             break;
        case MONS_BLACK_DRACONIAN:          description += "shadowy dark";              break;
        case MONS_OLIVE_DRACONIAN:          description += "sickly drab";               break;
        case MONS_DRACONIAN:                description += "plain brown";               break;
        case MONS_TEAL_DRACONIAN:           description += "spectral turquoise";        break;
        case MONS_GOLDEN_DRACONIAN:         description += "imposing gold";             break;
        case MONS_PEARL_DRACONIAN:          description += "opalescent pearl";          break;
        case MONS_SCINTILLATING_DRACONIAN:  description += "scintillating rainbow";     break;
        case MONS_BLOOD_DRACONIAN:          description += "gory crimson";              break;
        case MONS_PLATINUM_DRACONIAN:       description += "lustrous platinum";         break;
        default:                                                                        break;
        }

        description += " scales. ";

        if (subsp == MONS_BONE_DRACONIAN)
            return "It is a strong animated skeleton. Bits of bone fall from its mouth.";
    }

    switch (subsp)
    {
    case MONS_PLATINUM_DRACONIAN:
        description += "Its sheen appears to make it somehow faster.";
        break;
    case MONS_BLOOD_DRACONIAN:
        description += "Blood appears to drip from its mouth.";
        break;
    case MONS_SCINTILLATING_DRACONIAN:
        description += "It crackles and sparks random colours.";
        break;
    case MONS_PEARL_DRACONIAN:
        description += "It radiates holy energy.";
        break;
    case MONS_GOLDEN_DRACONIAN:
        description += "It's nostrils seems to be covered in a strange mix of flames and frost.";
        break;
    case MONS_TEAL_DRACONIAN:
        description += "It flickers in and out of reality.";
        break;
    case MONS_OLIVE_DRACONIAN:
        if (you.can_smell())
            description += "A horrid stench of rotten flesh comes from it.";
        else
            description += "It seems to attract flies.";
        break;
    case MONS_BLACK_DRACONIAN:
        description += "A foreboding aura of negative eminates from it.";
        break;
    case MONS_PINK_DRACONIAN:
        description += "Tiny butterflies encircle and land upon it.";
        break;
    case MONS_CYAN_DRACONIAN:           
        description += "The air seems to bend around it.";            
        break;
    case MONS_BLUE_DRACONIAN:
        description += "Sparks flare out of its mouth and nostrils.";
        break;
    case MONS_LIME_DRACONIAN:
        description += "Acidic fumes swirl around it.";
        break;
    case MONS_GREEN_DRACONIAN:
        description += "Venom drips from its jaws.";
        break;
    case MONS_PURPLE_DRACONIAN:
        description += "Its outline shimmers with magical energy.";
        break;
    case MONS_RED_DRACONIAN:
        description += "Smoke pours from its nostrils.";
        break;
    case MONS_WHITE_DRACONIAN:
        description += "Frost pours from its nostrils.";
        break;
    case MONS_SILVER_DRACONIAN:
        description += "It shines with Zin's blessed silver.";
        break;
    case MONS_MAGENTA_DRACONIAN:
        description += "It is cloaked in a thick magical fog.";
        break;
    default:
        break;
    }

    return description;
}

static string _describe_demonspawn_role(monster_type type)
{
    switch (type)
    {
    case MONS_BLOOD_SAINT:
        return "It weaves powerful and unpredictable spells of devastation.";
    case MONS_WARMONGER:
        return "It is devoted to combat, disrupting the magic of its foes as "
               "it battles endlessly.";
    case MONS_CORRUPTER:
        return "It corrupts space around itself, and can twist even the very "
               "flesh of its opponents.";
    case MONS_BLACK_SUN:
        return "It shines with an unholy radiance, and wields powers of "
               "darkness from its devotion to the deities of death.";
    default:
        return "";
    }
}

static string _describe_demonspawn_base(int species)
{
    switch (species)
    {
    case MONS_MONSTROUS_DEMONSPAWN:
        return "It is more beast now than whatever species it is descended from.";
    case MONS_GELID_DEMONSPAWN:
        return "It is covered in icy armour.";
    case MONS_INFERNAL_DEMONSPAWN:
        return "It gives off an intense heat.";
    case MONS_TORTUROUS_DEMONSPAWN:
        return "It menaces with bony spines.";
    }
    return "";
}

static string _describe_demonspawn(const monster_info& mi)
{
    string description;
    const int subsp = mi.draco_or_demonspawn_subspecies();

    description += _describe_demonspawn_base(subsp);

    if (subsp != mi.type)
    {
        const string demonspawn_role = _describe_demonspawn_role(mi.type);
        if (!demonspawn_role.empty())
            description += " " + demonspawn_role;
    }

    return description;
}

static const char* _get_resist_name(mon_resist_flags res_type)
{
    switch (res_type)
    {
    case MR_RES_ELEC:
        return "electricity";
    case MR_RES_POISON:
        return "poison";
    case MR_RES_FIRE:
        return "fire";
    case MR_RES_STEAM:
        return "steam";
    case MR_RES_COLD:
        return "cold";
    case MR_RES_ACID:
        return "acid";
    case MR_RES_ROTTING:
        return "rotting";
    case MR_RES_NEG:
        return "negative energy";
    case MR_RES_DAMNATION:
        return "hellfire";
    case MR_RES_WIND:
        return "winds";
    default:
        return "buggy resistance";
    }
}

static const char* _get_threat_desc(mon_threat_level_type threat)
{
    switch (threat)
    {
    case MTHRT_TRIVIAL: return "harmless";
    case MTHRT_EASY:    return "easy";
    case MTHRT_TOUGH:   return "dangerous";
    case MTHRT_NASTY:   return "extremely dangerous";
    case MTHRT_UNDEF:
    default:            return "buggily threatening";
    }
}

/**
 * Describe monster attack 'flavours' that trigger before the attack.
 *
 * @param flavour   The flavour in question; e.g. AF_SWOOP.
 * @return          A description of anything that happens 'before' an attack
 *                  with the given flavour;
 *                  e.g. "swoop behind its target and ".
 */
static const char* _special_flavour_prefix(attack_flavour flavour)
{
    switch (flavour)
    {
        case AF_KITE:
            return "retreat from adjacent foes and ";
        case AF_SWOOP:
            return "swoop behind its foe and ";
        default:
            return "";
    }
}

/**
 * Describe monster attack 'flavours' that have extra range.
 *
 * @param flavour   The flavour in question; e.g. AF_REACH_STING.
 * @return          If the flavour has extra-long range, say so. E.g.,
 *                  " from a distance". (Else "").
 */
static const char* _flavour_range_desc(attack_flavour flavour)
{
    if (flavour == AF_REACH || flavour == AF_REACH_STING)
        return " from a distance";
    return "";
}

static string _flavour_base_desc(attack_flavour flavour)
{
    static const map<attack_flavour, string> base_descs = {
        { AF_ACID,              "deal extra acid damage"},
        { AF_BLINK,             "blink itself" },
        { AF_COLD,              "deal up to %d extra cold damage" },
        { AF_CONFUSE,           "cause confusion" },
        { AF_DRAIN_STR,         "drain strength" },
        { AF_DRAIN_INT,         "drain intelligence" },
        { AF_DRAIN_DEX,         "drain dexterity" },
        { AF_DRAIN_STAT,        "drain strength, intelligence or dexterity" },
        { AF_DRAIN_XP,          "drain skills" },
        { AF_ELEC,              "deal up to %d extra electric damage" },
        { AF_FIRE,              "deal up to %d extra fire damage" },
        { AF_BARBS,             "impale with sharp barbs" },
        { AF_HUNGER,            "cause hunger" },
        { AF_MUTATE,            "cause mutations" },
        { AF_POISON_PETRIFY,    "poison and cause petrification" },
        { AF_POISON,            "cause poisoning" },
        { AF_POISON_STR,        "cause poison, which drains strength" },
        { AF_POISON_STAT,       "cause poison, which drains a random stat"},
        { AF_POISON_STRONG,     "cause strong poisoning" },
        { AF_ROT,               "cause rotting" },
        { AF_VAMPIRIC,          "drain health from the living" },
#if TAG_MAJOR_VERSION == 34
        { AF_KLOWN,             "cause random powerful effects" },
#endif
        { AF_DISTORT,           "cause wild translocation effects" },
        { AF_RAGE,              "cause berserking" },
        { AF_STICKY_FLAME,      "apply sticky flame" },
        { AF_CHAOTIC,           "cause unpredictable effects" },
        { AF_PURE_CHAOS,        "cause unpredictable effects" },
        { AF_STEAL,             "steal items" },
        { AF_CRUSH,             "begin ongoing constriction" },
        { AF_REACH,             "" },
        { AF_HOLY,              "deal extra damage to undead and demons" },
        { AF_PIERCE_AC,         " partially ignoring the target's armour" },
        { AF_ANTIMAGIC,         "drain magic" },
        { AF_PAIN,              "cause pain to the living" },
        { AF_ENSNARE,           "ensnare with webbing" },
        { AF_ENGULF,            "engulf inside itself" },
        { AF_PURE_FIRE,         "" },
        { AF_DRAIN_SPEED,       "drain speed" },
        { AF_VULN,              "reduce resistance to hostile enchantments" },
        { AF_SHADOWSTAB,        "deal increased damage when unseen" },
        { AF_DROWN,             "deal drowning damage" },
        { AF_CORRODE,           "cause corrosion" },
        { AF_SCARAB,            "drain speed and drain health" },
        { AF_MIASMATA,          "inject with foul rotting flesh" },
        { AF_TRAMPLE,           "knock back the defender" },
        { AF_REACH_STING,       "cause poisoning" },
        { AF_WEAKNESS,          "cause weakness" },
        { AF_KITE,              "" },
        { AF_SWOOP,             "" },
        { AF_PLAIN,             "" },
		{ AF_CONTAM,            "cause magical contamination"},
    };

    const string* desc = map_find(base_descs, flavour);
    ASSERT(desc);
    return *desc;
}

/**
 * Provide a short, and-prefixed flavour description of the given attack
 * flavour, if any.
 *
 * @param flavour  E.g. AF_COLD, AF_PLAIN.
 * @param HD       The hit dice of the monster using the flavour.
 * @return         "" if AF_PLAIN; else " <desc>", e.g.
 *                 " to deal up to 27 extra cold damage if any damage is dealt".
 */
static string _flavour_effect(attack_flavour flavour, int HD)
{
    const string base_desc = _flavour_base_desc(flavour);
    if (base_desc.empty())
        return base_desc;

    const int flavour_dam = flavour_damage(flavour, HD, false);
    const string flavour_desc = make_stringf(base_desc.c_str(), flavour_dam);

    if (flavour == AF_PIERCE_AC)
        return flavour_desc;

    if (!flavour_triggers_damageless(flavour)
        && flavour != AF_KITE && flavour != AF_SWOOP)
    {
        return " to " + flavour_desc + " if any damage is dealt";
    }

    return " to " + flavour_desc;
}

static string _heads_desc(attack_type type, int heads)
{
    if (type != AT_MULTIBITE)
        return "";
    return make_stringf(" up to %s times,", number_in_words(heads).c_str());
}

static string _head_append(attack_type type, int heads)
{
    if (type != AT_MULTIBITE)
        return "";
    return heads > 1 ? " each" : "";
}

struct mon_attack_info
{
    mon_attack_def definition;
    const item_def* weapon;
    bool operator < (const mon_attack_info &other) const
    {
        return std::tie(definition.type, definition.flavour,
                        definition.damage, weapon)
             < std::tie(other.definition.type, other.definition.flavour,
                        other.definition.damage, other.weapon);
    }
};

/**
 * What weapon is the given monster using for the given attack, if any?
 *
 * @param mi        The monster in question.
 * @param atk       The attack number. (E.g. 0, 1, 2...)
 * @return          The melee weapon being used by the monster for the given
 *                  attack, if any.
 */
static const item_def* _weapon_for_attack(const monster_info& mi, int atk)
{
    if (mi.attack[atk].type == AT_SHIELD)
        return mi.inv[MSLOT_SHIELD].get();

    const item_def* weapon
       = atk == 0 ? mi.inv[MSLOT_WEAPON].get() :
         atk == 1 && mi.wields_two_weapons() ? mi.inv[MSLOT_ALT_WEAPON].get() :
         nullptr;

    if (weapon && is_weapon(*weapon))
        return weapon;
    return nullptr;
}

static string _monster_attacks_description(const monster_info& mi)
{
    ostringstream result;
    brand_type special_flavour = SPWPN_NORMAL;

    if (mi.props.exists(SPECIAL_WEAPON_KEY))
    {
        ASSERT(mi.type == MONS_PANDEMONIUM_LORD || mons_is_pghost(mi.type));
        special_flavour = (brand_type) mi.props[SPECIAL_WEAPON_KEY].get_int();
    }

    vector<string> attack_descs;
    for (int i = 0; i < MAX_NUM_ATTACKS; ++i)
    {
        const mon_attack_def &atk = mi.attack[i];

        if (atk.type == AT_NONE)
            continue;

        const item_def* weapon = _weapon_for_attack(mi, i);
        const mon_attack_info &info = { atk, weapon };
        const mon_attack_def &attack = info.definition;

        const string weapon_name =
              info.weapon ? info.weapon->name(DESC_PLAIN).c_str()
            : ghost_brand_name(special_flavour, mi.type).c_str();
        const string dmg = info.weapon ? make_stringf(" (%d)", weapon_damage(*info.weapon)) : "";
        const string weapon_note = weapon_name.size() ?
            make_stringf(" plus %s %s%s",
                        mi.pronoun(PRONOUN_POSSESSIVE), weapon_name.c_str(), dmg.c_str())
            : "";

        // XXX: hack alert
        if (attack.flavour == AF_PURE_FIRE)
        {
            attack_descs.push_back(
                make_stringf("%s for up to %d fire damage",
                             mon_attack_name(attack.type, false).c_str(),
                             flavour_damage(attack.flavour, mi.hd, false)));
            continue;
        }

        // Damage is listed in parentheses for attacks with a flavour
        // description, but not for plain attacks.
        bool has_flavour = !_flavour_base_desc(attack.flavour).empty();
        const string damage_desc =
            make_stringf("%sfor up to %d damage%s%s%s",
                         has_flavour ? "(" : "",
                         attack.damage,
                         _head_append(attack.type, mi.num_heads).c_str(),
                         weapon_note.c_str(),
                         has_flavour ? ")" : "");

        attack_descs.push_back(
            make_stringf("%s%s%s%s %s%s",
                         _special_flavour_prefix(attack.flavour),
                         mon_attack_name(attack.type, false).c_str(),
                         _flavour_range_desc(attack.flavour),
                         _heads_desc(attack.type, mi.num_heads).c_str(),
                         damage_desc.c_str(),
                         _flavour_effect(attack.flavour, mi.hd).c_str()));
    }


    if (!attack_descs.empty())
    {
        result << uppercase_first(mi.pronoun(PRONOUN_SUBJECTIVE));
        result << " can " << comma_separated_line(attack_descs.begin(),
                                                  attack_descs.end(),
                                                  "; and ", "; ");
        result << ".\n";
    }

    return result.str();
}

static string _monster_spells_description(const monster_info& mi)
{
    // Show monster spells and spell-like abilities.
    if (!mi.has_spells())
        return "";

    formatted_string description;
    describe_spellset(monster_spellset(mi), nullptr, description, &mi);
    description.cprintf("\nTo read a description, press the key listed above. "
        "(x%%) indicates the chance to beat your MR, "
        "and (y) indicates the spell range");
    description.cprintf(crawl_state.need_save
        ? "; shown in red if you are in range.\n"
        : ".\n");

    return description.to_colour_string();
}

static const char *_speed_description(int speed)
{
    // These thresholds correspond to the player mutations for fast and slow.
    ASSERT(speed != 10);

    if (speed < 7)
        return "extremely slowly";
    else if (speed < 8)
        return "very slowly";
    else if (speed < 10)
        return "slowly";
    else if (speed > 15)
        return "extremely quickly";
    else if (speed > 13)
        return "very quickly";
    return "quickly";
}

static void _add_energy_to_string(int speed, int energy, string what,
                                  vector<string> &fast, vector<string> &slow)
{
    if (energy == 10)
        return;

    const int act_speed = (speed * 10) / energy;
    if (act_speed > 10)
        fast.push_back(what + " " + _speed_description(act_speed) + " (" + to_string(act_speed) + ")");
    if (act_speed < 10)
        slow.push_back(what + " " + _speed_description(act_speed) + " (" + to_string(act_speed) + ")");
}

static void _describe_monster_hd(const monster_info& mi, ostringstream &result)
{
    // BCADDO: Find a way to describe spell HD here again. Deprecating this since it's usually nonsense now.
    result << "HD: " << mi.hd << "\n";
}

/**
 * Append information about a given monster's HP to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_hp(const monster_info& mi, ostringstream &result)
{
    result << "Max HP: " << mi.get_max_hp_desc() << "\n";
}

/**
 * Append information about a given monster's AC to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_ac(const monster_info& mi, ostringstream &result)
{
    string msg = "";
    bool applied = false;
    if (mi.is(MB_CORROSION) || mi.is(MB_WRETCHED) || mi.is(MB_SUBMERGED))
    {
        msg = " (";
        if (mi.is(MB_CORROSION))
        {
            msg += "corroded";
            applied = true;
        }
        if (mi.is(MB_WRETCHED))
        {
            if (applied != 0)
                msg += ", ";
            msg += "wretched";
            applied = true;
        }
        if (mi.is(MB_SUBMERGED))
        {
            if (applied != 0)
                msg += ", ";
            msg += "submerged";
        }
        msg += ")";
    }
    result << "AC: " << mi.ac << msg << "\n";
}

/**
 * Append information about a given monster's EV to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_ev(const monster_info& mi, ostringstream &result)
{
    string msg = "";
    if (mi.ev < mi.base_ev)
        msg = " (incap)";
    if (mi.ev > mi.base_ev && mi.is(MB_SUBMERGED))
        msg = " (swimming)";
    result << "EV: " << mi.ev << msg << "\n";
}

static bool _incap(const monster_info& mi)
{
    return (mi.is(MB_CONFUSED) || mi.is(MB_PARALYSED)  || mi.is(MB_SLEEPING)
        || mi.is(MB_PETRIFIED) || mi.is(MB_PETRIFYING) || mi.is(MB_CAUGHT)
        || mi.is(MB_INSANE)    || mi.is(MB_MAD)        || mi.is(MB_PINNED)
        || mi.is(MB_WEBBED)    || mi.is(MB_WITHDRAWN));
}

/**
* Append information about a given monster's SH to the provided stream.
*
* @param mi[in]            Player-visible info about the monster in question.
* @param result[in,out]    The stringstream to append to.
*/
static void _describe_monster_sh(const monster_info& mi, ostringstream &result)
{
    if (mi.sh > 0 && _incap(mi))
        result << "SH: 0 (incap)" << "\n";
    else
        result << "SH: " << mi.sh << "\n";
}

/**
 * Append information about a given monster's MR to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_mr(const monster_info& mi, ostringstream &result)
{
    if (mi.res_magic() == MAG_IMMUNE)
    {
        result << "MR: ∞\n";
        return;
    }
    result << "MR: " << mi.res_magic() << "\n";
}

static void _describe_experience_value(const monster_info& mi, ostringstream &result)
{
    result << "XP Value: " << mi.xp_val << "\n";
}

static void _describe_summon_duration(const monster_info& mi, ostringstream &result)
{
    if (mi.dur > 0)
    {
        float dur = mi.dur / 10.0;
        result << "\nRemaining Summon Duration: " << dur << " turns.\n";
    }
}

// Size adjectives
const char* const size_adj[] =
{
    "tiny",
    "very small",
    "small",
    "medium",
    "large",
    "very large",
    "giant",
};
COMPILE_CHECK(ARRAYSZ(size_adj) == NUM_SIZE_LEVELS);

// This is used in monster description and on '%' screen for player size
const char* get_size_adj(const size_type size, bool ignore_medium)
{
    ASSERT_RANGE(size, 0, ARRAYSZ(size_adj));
    if (ignore_medium && size == SIZE_MEDIUM)
        return nullptr; // don't mention medium size
    return size_adj[size];
}

// Describe a monster's (intrinsic) resistances, speed and a few other
// attributes.
static string _monster_stat_description(const monster_info& mi)
{
    if (mons_is_sensed(mi.type) || mons_is_projectile(mi.type))
        return "";

    ostringstream result;

    _describe_monster_hd(mi, result);
    _describe_monster_hp(mi, result);
    _describe_monster_ac(mi, result);
    _describe_monster_ev(mi, result);
    _describe_monster_sh(mi, result);
    _describe_monster_mr(mi, result);
    _describe_experience_value(mi, result);
    _describe_summon_duration(mi, result);

    result << "\n";

    resists_t resist = mi.resists();

    const mon_resist_flags resists[] =
    {
        MR_RES_ELEC,    MR_RES_POISON, MR_RES_FIRE,
        MR_RES_STEAM,   MR_RES_COLD,   MR_RES_ACID,
        MR_RES_ROTTING, MR_RES_NEG,    MR_RES_DAMNATION,
        MR_RES_WIND,
    };

    vector<string> extreme_resists;
    vector<string> high_resists;
    vector<string> base_resists;
    vector<string> suscept;

    for (mon_resist_flags rflags : resists)
    {
        int level = get_resist(resist, rflags);

        if (level != 0)
        {
            const char* attackname = _get_resist_name(rflags);
            if (rflags == MR_RES_DAMNATION || rflags == MR_RES_WIND)
                level = 3; // one level is immunity
            level = max(level, -1);
            level = min(level,  3);
            switch (level)
            {
                case -1:
                    suscept.emplace_back(attackname);
                    break;
                case 1:
                    base_resists.emplace_back(attackname);
                    break;
                case 2:
                    high_resists.emplace_back(attackname);
                    break;
                case 3:
                    extreme_resists.emplace_back(attackname);
                    break;
            }
        }
    }

    if (mi.props.exists(CLOUD_IMMUNE_MB_KEY) && mi.props[CLOUD_IMMUNE_MB_KEY])
        extreme_resists.emplace_back("clouds of all kinds");

    vector<string> resist_descriptions;
    if (!extreme_resists.empty())
    {
        const string tmp = "immune to "
            + comma_separated_line(extreme_resists.begin(),
                                   extreme_resists.end());
        resist_descriptions.push_back(tmp);
    }
    if (!high_resists.empty())
    {
        const string tmp = "very resistant to "
            + comma_separated_line(high_resists.begin(), high_resists.end());
        resist_descriptions.push_back(tmp);
    }
    if (!base_resists.empty())
    {
        const string tmp = "resistant to "
            + comma_separated_line(base_resists.begin(), base_resists.end());
        resist_descriptions.push_back(tmp);
    }

    const char* pronoun = mi.pronoun(PRONOUN_SUBJECTIVE);
    const bool plural = mi.pronoun_plurality();

    if (mi.threat != MTHRT_UNDEF)
    {
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("look", plural) << " "
               << _get_threat_desc(mi.threat) << ".\n";
    }

    if (mi.type == MONS_CHAOS_ELEMENTAL)
    {
        result << "Its resistance to electricity, acid and extremes "
               << "of temperature ever shift with its form. \n";
    }

    if (!resist_descriptions.empty())
    {
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("are", plural) << " "
               << comma_separated_line(resist_descriptions.begin(),
                                       resist_descriptions.end(),
                                       "; and ", "; ")
               << ".\n";
    }

    // Is monster susceptible to anything? (On a new line.)
    if (!suscept.empty())
    {
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("are", plural) << " susceptible to "
               << comma_separated_line(suscept.begin(), suscept.end())
               << ".\n";
    }

    if (mi.is(MB_SUBMERGED))
    {
        result << uppercase_first(pronoun) << " "
            << conjugate_verb("are", plural)
            << " submerged underwater, boosting general protection,"
            << " insulating against acids and extremes of temperature and making"
            << " it immune to the clouds above the water, but inflicting a"
            << " weakness to electricity (rF+ rC+ AC+4 rCorr+ rElec-).\n";
    }

    if (mi.is(MB_CHAOTIC))
    {
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("are", plural)
               << " vulnerable to silver and hated by Zin.\n";
    }

    if (mons_class_flag(mi.type, M_STATIONARY)
        && !mons_is_tentacle_or_tentacle_segment(mi.type))
    {
        result << uppercase_first(pronoun) << " cannot move.\n";
    }

    if (mons_class_flag(mi.type, M_COLD_BLOOD)
        && get_resist(resist, MR_RES_COLD) <= 0)
    {
        result << uppercase_first(pronoun)
               << " " << conjugate_verb("are", plural)
               << " cold-blooded and may be slowed by cold attacks.\n";
    }

    // Seeing invisible.
    if (mi.can_see_invisible())
        result << uppercase_first(pronoun) << " can see invisible.\n";

    // Echolocation, wolf noses, jellies, etc
    if (!mons_can_be_blinded(mi.type))
    {
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("are", plural)
               << " immune to blinding.\n";
    }
    // XXX: could mention "immune to dazzling" here, but that's spammy, since
    // it's true of such a huge number of monsters. (undead, statues, plants).
    // Might be better to have some place where players can see holiness &
    // information about holiness.......?

    if (mi.intel() <= I_BRAINLESS)
    {
        // Matters for Ely.
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("are", plural) << " mindless.\n";
    }
    else if (mi.intel() >= I_HUMAN)
    {
        // Matters for Yred, Gozag, Zin, TSO, Alistair....
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("are", plural) << " intelligent.\n";
    }

    // Unusual monster speed.
    const int speed = mi.base_speed();
    bool did_speed = false;
    if (speed != 0)
    {
        did_speed = true;
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("have", plural) << " "
               << mi.speed_description();
    }
    const mon_energy_usage def = DEFAULT_ENERGY;
    if (!(mi.menergy == def))
    {
        const mon_energy_usage me = mi.menergy;
        vector<string> fast, slow;
        if (!did_speed)
            result << uppercase_first(pronoun) << " ";
        _add_energy_to_string(speed, me.move,
                              conjugate_verb("cover", plural) + " ground",
                              fast, slow);
        // since MOVE_ENERGY also sets me.swim
        if (me.swim != me.move)
        {
            _add_energy_to_string(speed, me.swim,
                                  conjugate_verb("swim", plural), fast, slow);
        }
        _add_energy_to_string(speed, me.attack,
                              conjugate_verb("attack", plural), fast, slow);
        if (mons_class_itemuse(mi.type) & MU_WEAPON_RANGED)
        {
            _add_energy_to_string(speed, me.missile,
                                  conjugate_verb("shoot", plural), fast, slow);
        }
        _add_energy_to_string(
            speed, me.spell,
            mi.is_actual_spellcaster() ? conjugate_verb("cast", plural)
                                         + " spells" :
            mi.is_priest()             ? conjugate_verb("use", plural)
                                         + " invocations"
                                       : conjugate_verb("use", plural)
                                         + " natural abilities", fast, slow);
        _add_energy_to_string(speed, me.special,
                              conjugate_verb("use", plural)
                              + " special abilities",
                              fast, slow);
        if (mons_class_itemuse(mi.type) & MU_EVOKE)
        {
            _add_energy_to_string(speed, me.item,
                                  conjugate_verb("use", plural) + " items",
                                  fast, slow);
        }

        if (speed >= 10)
        {
            if (did_speed && fast.size() == 1)
                result << " and " << fast[0];
            else if (!fast.empty())
            {
                if (did_speed)
                    result << ", ";
                result << comma_separated_line(fast.begin(), fast.end());
            }
            if (!slow.empty())
            {
                if (did_speed || !fast.empty())
                    result << ", but ";
                result << comma_separated_line(slow.begin(), slow.end());
            }
        }
        else if (speed < 10)
        {
            if (did_speed && slow.size() == 1)
                result << " and " << slow[0];
            else if (!slow.empty())
            {
                if (did_speed)
                    result << ", ";
                result << comma_separated_line(slow.begin(), slow.end());
            }
            if (!fast.empty())
            {
                if (did_speed || !slow.empty())
                    result << ", but ";
                result << comma_separated_line(fast.begin(), fast.end());
            }
        }
        result << ".\n";
    }
    else if (did_speed)
        result << ".\n";

    if (mi.type == MONS_SHADOW)
    {
        // Cf. monster::action_energy() in monster.cc.
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("cover", plural)
               << " ground more quickly when invisible.\n";
    }

    if (mi.airborne())
        result << uppercase_first(pronoun) << " can fly.\n";

    // Unusual regeneration rates.
    if (!mi.can_regenerate())
        result << uppercase_first(pronoun) << " cannot regenerate.\n";
    else if (mons_class_fast_regen(mi.type))
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("regenerate", plural)
               << " quickly.\n";

    const char* mon_size = get_size_adj(mi.body_size(), true);
    if (mon_size)
    {
        result << uppercase_first(pronoun) << " "
               << conjugate_verb("are", plural) << " "
               << mon_size << ".\n";
    }

    if (in_good_standing(GOD_ZIN, 0) && !mi.pos.origin() && monster_at(mi.pos))
    {
        recite_counts retval;
        monster *m = monster_at(mi.pos);
        auto eligibility = zin_check_recite_to_single_monster(m, retval);
        if (eligibility == RE_INELIGIBLE)
        {
            result << uppercase_first(pronoun) <<
                    " cannot be affected by reciting Zin's laws.";
        }
        else if (eligibility == RE_TOO_STRONG)
        {
            result << uppercase_first(pronoun) << " "
                   << conjugate_verb("are", plural)
                   << " too strong to be affected by reciting Zin's laws.";
        }
        else // RE_ELIGIBLE || RE_RECITE_TIMER
        {
            result << uppercase_first(pronoun) <<
                            " can be affected by reciting Zin's laws.";
        }

        if (you.wizard)
        {
            result << " (Recite power:" << zin_recite_power()
                   << ", Hit dice:" << mi.hd << ")";
        }
        result << "\n";
    }

    result << _monster_attacks_description(mi);
    result << _monster_spells_description(mi);

    return result.str();
}

branch_type serpent_of_hell_branch(monster_type m)
{
    switch (m)
    {
    case MONS_SERPENT_OF_HELL_COCYTUS:
        return BRANCH_COCYTUS;
    case MONS_SERPENT_OF_HELL_DIS:
        return BRANCH_DIS;
    case MONS_SERPENT_OF_HELL_TARTARUS:
        return BRANCH_TARTARUS;
    case MONS_SERPENT_OF_HELL:
        return BRANCH_GEHENNA;
    default:
        die("bad serpent of hell monster_type");
    }
}

string serpent_of_hell_flavour(monster_type m)
{
    return lowercase_string(branches[serpent_of_hell_branch(m)].shortname);
}

// Fetches the monster's database description and reads it into inf.
void get_monster_db_desc(const monster_info& mi, describe_info &inf,
                         bool &has_stat_desc)
{
    if (inf.title.empty())
        inf.title = getMiscString(mi.common_name(DESC_DBNAME) + " title");
    if (inf.title.empty())
        inf.title = uppercase_first(mi.full_name(DESC_A)) + ".";

    string db_name;

    if (mi.props.exists("dbname"))
        db_name = mi.props["dbname"].get_string();
    else if (mi.mname.empty())
        db_name = mi.db_name();
    else
        db_name = mi.full_name(DESC_PLAIN);

    if (mons_species(mi.type) == MONS_SERPENT_OF_HELL)
        db_name += " " + serpent_of_hell_flavour(mi.type);

    // This is somewhat hackish, but it's a good way of over-riding monsters'
    // descriptions in Lua vaults by using MonPropsMarker. This is also the
    // method used by set_feature_desc_long, etc. {due}
    if (!mi.description.empty())
        inf.body << mi.description;
    // Don't get description for player ghosts.
    else if (mi.type != MONS_PLAYER_GHOST
             && mi.type != MONS_PLAYER_ILLUSION)
    {
        inf.body << getLongDescription(db_name);
    }

    // And quotes {due}
    if (!mi.quote.empty())
        inf.quote = mi.quote;
    else
        inf.quote = getQuoteString(db_name);

    string symbol;
    symbol += get_monster_data(mi.type)->basechar;
    if (isaupper(symbol[0]))
        symbol = "cap-" + symbol;

    string quote2;
    if (!mons_is_unique(mi.type))
    {
        string symbol_prefix = "__" + symbol + "_prefix";
        inf.prefix = getLongDescription(symbol_prefix);

        string symbol_suffix = "__" + symbol + "_suffix";
        quote2 = getQuoteString(symbol_suffix);
    }

    if (!inf.quote.empty() && !quote2.empty())
        inf.quote += "\n";
    inf.quote += quote2;

    const string it = mi.pronoun(PRONOUN_SUBJECTIVE);
    const string it_o = mi.pronoun(PRONOUN_OBJECTIVE);
    const string It = uppercase_first(it);
    const string is = conjugate_verb("are", mi.pronoun_plurality());

    switch (mi.type)
    {
    case MONS_BLACK_DRACONIAN:
    case MONS_PINK_DRACONIAN:
    case MONS_LIME_DRACONIAN:
    case MONS_GREEN_DRACONIAN:
    case MONS_PURPLE_DRACONIAN:
    case MONS_RED_DRACONIAN:
    case MONS_WHITE_DRACONIAN:
    case MONS_SILVER_DRACONIAN:
    case MONS_BLUE_DRACONIAN:
    case MONS_CYAN_DRACONIAN:
    case MONS_OLIVE_DRACONIAN:
    case MONS_BONE_DRACONIAN:
    case MONS_TEAL_DRACONIAN:
    case MONS_GOLDEN_DRACONIAN:
    case MONS_PEARL_DRACONIAN:
    case MONS_SCINTILLATING_DRACONIAN:
    case MONS_BLOOD_DRACONIAN:
    case MONS_PLATINUM_DRACONIAN:
    case MONS_MAGENTA_DRACONIAN:
    case MONS_DRACONIAN_SHIFTER:
    case MONS_DRACONIAN_SCORCHER:
    case MONS_DRACONIAN_ANNIHILATOR:
    case MONS_DRACONIAN_SHARPSHOOTER:
    case MONS_DRACONIAN_STORMCALLER:
    case MONS_DRACONIAN_MONK:
    case MONS_DRACONIAN_KNIGHT:
    {
        inf.body << "\n" << _describe_draconian(mi) << "\n";
        break;
    }

    case MONS_MONSTROUS_DEMONSPAWN:
    case MONS_GELID_DEMONSPAWN:
    case MONS_INFERNAL_DEMONSPAWN:
    case MONS_TORTUROUS_DEMONSPAWN:
    case MONS_BLOOD_SAINT:
    case MONS_WARMONGER:
    case MONS_CORRUPTER:
    case MONS_BLACK_SUN:
    {
        inf.body << "\n" << _describe_demonspawn(mi) << "\n";
        break;
    }

    case MONS_PLAYER_GHOST:
        inf.body << "The apparition of " << get_ghost_description(mi) << ".\n";
        if (mi.props.exists(MIRRORED_GHOST_KEY))
            inf.body << "It looks just like you...spooky!\n";
        break;

    case MONS_PLAYER_ILLUSION:
        inf.body << "An illusion of " << get_ghost_description(mi) << ".\n";
        break;

    case MONS_PANDEMONIUM_LORD:
        inf.body << _describe_demon(mi.mname, mi.airborne()) << "\n";
        break;

    case MONS_MUTANT_BEAST:
        // vault renames get their own descriptions
        if (mi.mname.empty() || !mi.is(MB_NAME_REPLACE))
            inf.body << _describe_mutant_beast(mi) << "\n";
        break;

    case MONS_ABOMINATION_SMALL:
    case MONS_ABOMINATION_LARGE:
        inf.body << _describe_abomination(mi) << "\n";
        break;

    case MONS_BLOCK_OF_ICE:
        if (mi.is(MB_SLOWLY_DYING))
            inf.body << "\nIt is quickly melting away.\n";
        break;

    case MONS_PILLAR_OF_SALT:
        if (mi.is(MB_SLOWLY_DYING))
            inf.body << "\nIt is quickly crumbling away.\n";
        break;

    case MONS_PROGRAM_BUG:
        inf.body << "If this monster is a \"program bug\", then it's "
                "recommended that you save your game and reload. Please report "
                "monsters who masquerade as program bugs or run around the "
                "dungeon without a proper description to the authorities.\n";
        break;

    default:
        break;
    }

    if (!mons_is_unique(mi.type))
    {
        string symbol_suffix = "__";
        symbol_suffix += symbol;
        symbol_suffix += "_suffix";

        string suffix = getLongDescription(symbol_suffix)
                      + getLongDescription(symbol_suffix + "_examine");

        if (!suffix.empty())
            inf.body << "\n" << suffix;
    }

    const int curse_power = mummy_curse_power(mi.type);
    if (curse_power && !mi.is(MB_SUMMONED))
    {
        inf.body << "\n" << It << " will inflict a ";
        if (curse_power > 10)
            inf.body << "powerful ";
        inf.body << "necromantic curse on "
                 << mi.pronoun(PRONOUN_POSSESSIVE) << " foe when destroyed.\n";
    }

    // Get information on resistances, speed, etc.
    string result = _monster_stat_description(mi);
    if (!result.empty())
    {
        inf.body << "\n" << result;
        has_stat_desc = true;
    }

    bool stair_use = false;
    if (!mons_class_can_use_stairs(mi.type))
    {
        inf.body << It << " " << is << " incapable of using stairs.\n";
        stair_use = true;
    }

    if (mi.is(MB_SUMMONED))
    {
        inf.body << "\nThis monster has been summoned, and is thus only "
                    "temporary. Killing " << it_o << " yields no experience, "
                    "nutrition or items";
        if (!stair_use)
        {
            inf.body << ", and " << it << " " << is
                     << " incapable of using stairs";
        }
        inf.body << ".\n";
    }
    else if (mi.is(MB_PERM_SUMMON))
    {
        inf.body << "\nThis monster has been summoned in a durable way. "
                    "Killing " << it_o << " yields no experience, nutrition "
                    "or items, but " << it << " cannot be abjured.\n";
    }
    else if (mi.is(MB_NO_REWARD))
    {
        inf.body << "\nKilling this monster yields no experience, nutrition or"
                    " items.";
    }
    else if (mons_class_leaves_hide(mi.type))
    {
        inf.body << "\nIf " << it << " " << is <<
                    " slain, it may be possible to recover "
                 << mi.pronoun(PRONOUN_POSSESSIVE)
                 << " hide, which can be used as armour.\n";
    }

    if (mi.is(MB_SUMMONED_CAPPED))
    {
        inf.body << "\nYou have summoned too many monsters of this kind to "
                    "sustain them all, and thus this one will shortly "
                    "expire.\n";
    }

    if (!inf.quote.empty())
        inf.quote += "\n";

#ifdef DEBUG_DIAGNOSTICS
    if (you.suppress_wizard)
        return;
    if (mi.pos.origin() || !monster_at(mi.pos))
        return; // not a real monster
    monster& mons = *monster_at(mi.pos);

    if (mons.has_originating_map())
    {
        inf.body << make_stringf("\nPlaced by map: %s",
                                 mons.originating_map().c_str());
    }

    inf.body << "\nMonster health: "
             << mons.hit_points << "/" << mons.max_hit_points << "\n";

    const actor *mfoe = mons.get_foe();
    inf.body << "Monster foe: "
             << (mfoe? mfoe->name(DESC_PLAIN, true)
                 : "(none)");

    vector<string> attitude;
    if (mons.friendly())
        attitude.emplace_back("friendly");
    if (mons.neutral())
        attitude.emplace_back("neutral");
    if (mons.good_neutral())
        attitude.emplace_back("good_neutral");
    if (mons.strict_neutral())
        attitude.emplace_back("strict_neutral");
    if (mons.pacified())
        attitude.emplace_back("pacified");
    if (mons.wont_attack())
        attitude.emplace_back("wont_attack");
    if (!attitude.empty())
    {
        string att = comma_separated_line(attitude.begin(), attitude.end(),
                                          "; ", "; ");
        if (mons.has_ench(ENCH_INSANE))
            inf.body << "; frenzied and insane (otherwise " << att << ")";
        else
            inf.body << "; " << att;
    }
    else if (mons.has_ench(ENCH_INSANE))
        inf.body << "; frenzied and insane";

    inf.body << "\n\nHas holiness: ";
    inf.body << holiness_description(mi.holi);
    inf.body << ".";

    const monster_spells &hspell_pass = mons.spells;
    bool found_spell = false;

    for (unsigned int i = 0; i < hspell_pass.size(); ++i)
    {
        if (!found_spell)
        {
            inf.body << "\n\nMonster Spells:\n";
            found_spell = true;
        }

        inf.body << "    " << i << ": "
                 << mi_spell_title(hspell_pass[i].spell, &mi)
                 << " (";
        if (hspell_pass[i].flags & MON_SPELL_EMERGENCY)
            inf.body << "emergency, ";
        if (hspell_pass[i].flags & MON_SPELL_NATURAL)
            inf.body << "natural, ";
        if (hspell_pass[i].flags & MON_SPELL_MAGICAL)
            inf.body << "magical, ";
        if (hspell_pass[i].flags & MON_SPELL_WIZARD)
            inf.body << "wizard, ";
        if (hspell_pass[i].flags & MON_SPELL_PRIEST)
            inf.body << "priest, ";
        if (hspell_pass[i].flags & MON_SPELL_BREATH)
            inf.body << "breath, ";
        inf.body << (int) hspell_pass[i].freq << ")";
    }

    bool has_item = false;
    for (mon_inv_iterator ii(mons); ii; ++ii)
    {
        if (!has_item)
        {
            inf.body << "\n\nMonster Inventory:\n";
            has_item = true;
        }
        inf.body << "    " << ii.slot() << ": "
                 << ii->name(DESC_A, false, true);
    }

    if (mons.props.exists("blame"))
    {
        inf.body << "\n\nMonster blame chain:\n";

        const CrawlVector& blame = mons.props["blame"].get_vector();

        for (const auto &entry : blame)
            inf.body << "    " << entry.get_string() << "\n";
    }
    inf.body << "\n\n" << debug_constriction_string(&mons);
#endif
}

int describe_monsters(const monster_info &mi, const string& /*footer*/)
{
    bool has_stat_desc = false;
    describe_info inf;
    formatted_string desc;

    get_monster_db_desc(mi, inf, has_stat_desc);

    spellset spells = monster_spellset(mi);

    auto vbox = make_shared<Box>(Widget::VERT);
    auto title_hbox = make_shared<Box>(Widget::HORZ);

#ifdef USE_TILE_LOCAL
    auto dgn = make_shared<Dungeon>();
    dgn->width = dgn->height = 1;
    dgn->buf().add_monster(mi, 0, 0);
    title_hbox->add_child(move(dgn));
#endif

    auto title = make_shared<Text>();
    title->set_text(inf.title);
    title->set_margin_for_sdl(0, 0, 0, 10);
    title_hbox->add_child(move(title));

    title_hbox->set_cross_alignment(Widget::CENTER);
    title_hbox->set_margin_for_crt(0, 0, 1, 0);
    title_hbox->set_margin_for_sdl(0, 0, 20, 0);
    vbox->add_child(move(title_hbox));

    desc += inf.body.str();
    if (crawl_state.game_is_hints())
        desc += formatted_string(hints_describe_monster(mi, has_stat_desc));
    desc += inf.footer;
    desc = formatted_string::parse_string(trimmed_string(desc));

    const formatted_string quote = formatted_string(trimmed_string(inf.quote));

    auto desc_sw = make_shared<Switcher>();
    auto more_sw = make_shared<Switcher>();
    desc_sw->current() = 0;
    more_sw->current() = 0;

#ifdef USE_TILE_LOCAL
# define MORE_PREFIX "[<w>!</w>" "|<w>Right-click</w>" "]: "
#else
# define MORE_PREFIX "[<w>!</w>" "]: "
#endif

    const char* mores[2] = {
        MORE_PREFIX "<w>Description</w>|Quote",
        MORE_PREFIX "Description|<w>Quote</w>",
    };

    for (int i = 0; i < (inf.quote.empty() ? 1 : 2); i++)
    {
        const formatted_string *content[2] = { &desc, &quote };
        auto scroller = make_shared<Scroller>();
        auto text = make_shared<Text>(content[i]->trim());
        text->set_wrap_text(true);
        scroller->set_child(text);
        desc_sw->add_child(move(scroller));

        more_sw->add_child(make_shared<Text>(
                formatted_string::parse_string(mores[i])));
    }

    more_sw->set_margin_for_sdl(20, 0, 0, 0);
    more_sw->set_margin_for_crt(1, 0, 0, 0);
    desc_sw->expand_h = false;
    desc_sw->align_x = Widget::STRETCH;
    vbox->add_child(desc_sw);
    if (!inf.quote.empty())
        vbox->add_child(more_sw);

#ifdef USE_TILE_LOCAL
    vbox->max_size().width = tiles.get_crt_font()->char_width()*80;
#endif

    auto popup = make_shared<ui::Popup>(move(vbox));

    bool done = false;
    int lastch;
    popup->on_keydown_event([&](const KeyEvent& ev) {
        const auto key = ev.key();
        lastch = key;
        done = key == CK_ESCAPE;
        if (!inf.quote.empty() && (key == '!' || key == CK_MOUSE_CMD))
        {
            int n = (desc_sw->current() + 1) % 2;
            desc_sw->current() = more_sw->current() = n;
#ifdef USE_TILE_WEB
            tiles.json_open_object();
            tiles.json_write_int("pane", n);
            tiles.ui_state_change("describe-monster", 0);
#endif
        }
        if (desc_sw->current_widget()->on_event(ev))
            return true;
        const vector<pair<spell_type,char>> spell_map = map_chars_to_spells(spells, nullptr);
        auto entry = find_if(spell_map.begin(), spell_map.end(),
                [key](const pair<spell_type,char>& e) { return e.second == key; });
        if (entry == spell_map.end())
            return false;
        describe_spell(entry->first, &mi, nullptr);
        return true;
    });

#ifdef USE_TILE_WEB
    tiles.json_open_object();
    tiles.json_write_string("title", inf.title);
    formatted_string needle;
    describe_spellset(spells, nullptr, needle, &mi);
    string desc_without_spells = desc.to_colour_string();
    if (!needle.empty())
    {
        desc_without_spells = replace_all(desc_without_spells,
                needle.to_colour_string(), "SPELLSET_PLACEHOLDER");
    }
    tiles.json_write_string("body", desc_without_spells);
    tiles.json_write_string("quote", quote);
    write_spellset(spells, nullptr, &mi);

    {
        tileidx_t t    = tileidx_monster(mi);
        tileidx_t t0   = t & TILE_FLAG_MASK;
        tileidx_t flag = t & (~TILE_FLAG_MASK);

        if (!mons_class_is_stationary(mi.type) || mi.type == MONS_TRAINING_DUMMY)
        {
            tileidx_t mcache_idx = mcache.register_monster(mi);
            t = flag | (mcache_idx ? mcache_idx : t0);
            t0 = t & TILE_FLAG_MASK;
        }

        tiles.json_write_int("fg_idx", t0);
        tiles.json_write_name("flag");
        tiles.write_tileidx(flag);

        if (t0 >= TILEP_MCACHE_START)
        {
            mcache_entry *entry = mcache.get(t0);
            if (entry)
                tiles.send_mcache(entry, false);
            else
            {
                tiles.json_write_comma();
                tiles.write_message("\"doll\":[[%d,%d]]", TILEP_MONS_UNKNOWN, TILE_Y);
                tiles.json_write_null("mcache");
            }
        }
        else if (t0 >= TILE_MAIN_MAX)
        {
            tiles.json_write_comma();
            tiles.write_message("\"doll\":[[%u,%d]]", (unsigned int) t0, TILE_Y);
            tiles.json_write_null("mcache");
        }
    }
    tiles.push_ui_layout("describe-monster", 1);
#endif

    ui::run_layout(move(popup), done);

#ifdef USE_TILE_WEB
    tiles.pop_ui_layout();
#endif

    return lastch;
}

static const char* xl_rank_names[] =
{
    "weakling",
    "amateur",
    "novice",
    "journeyman",
    "adept",
    "veteran",
    "master",
    "legendary"
};

static string _xl_rank_name(const int xl_rank)
{
    const string rank = xl_rank_names[xl_rank];

    return article_a(rank);
}

string short_ghost_description(const monster *mon, bool abbrev)
{
    ASSERT(mons_is_pghost(mon->type));

    const ghost_demon &ghost = *(mon->ghost);
    const char* rank = xl_rank_names[ghost_level_to_rank(ghost.xl)];

    string desc = make_stringf("%s %s %s", rank,
                               species_name(ghost.species, SPNAME_PLAIN, false).c_str(),
                               get_job_name(ghost.job));

    if (abbrev || strwidth(desc) > 40)
    {
        desc = make_stringf("%s %s%s",
                            rank,
                            get_species_abbrev(ghost.species),
                            get_job_abbrev(ghost.job));
    }

    return desc;
}

// Describes the current ghost's previous owner. The caller must
// prepend "The apparition of" or whatever and append any trailing
// punctuation that's wanted.
string get_ghost_description(const monster_info &mi, bool concise)
{
    ostringstream gstr;

    const species_type gspecies = mi.i_ghost.species;

    gstr << mi.mname << " the "
         << skill_title_by_rank(mi.i_ghost.best_skill,
                        mi.i_ghost.best_skill_rank,
                        gspecies,
                        species_has_low_str(gspecies), mi.i_ghost.religion)
         << ", " << _xl_rank_name(mi.i_ghost.xl_rank) << " ";

    if (concise)
    {
        gstr << get_species_abbrev(gspecies)
             << get_job_abbrev(mi.i_ghost.job);
    }
    else
    {
        gstr << species_name(gspecies, SPNAME_PLAIN, false)
             << " "
             << get_job_name(mi.i_ghost.job);
    }

    if (mi.i_ghost.religion != GOD_NO_GOD)
    {
        gstr << " of "
             << god_name(mi.i_ghost.religion);
    }

    return gstr.str();
}

void describe_skill(skill_type skill)
{
    describe_info inf;
    inf.title = skill_name(skill);
    inf.body << get_skill_description(skill, false);
#ifdef USE_TILE
    tile_def tile = tile_def(tileidx_skill(skill, TRAINING_ENABLED), TEX_GUI);
    show_description(inf, &tile);
#else
    show_description(inf);
#endif
}

// only used in tiles
string get_command_description(const command_type cmd, bool terse)
{
    string lookup = command_to_name(cmd);

    if (!terse)
        lookup += " verbose";

    string result = getLongDescription(lookup);
    if (result.empty())
    {
        if (!terse)
        {
            // Try for the terse description.
            result = get_command_description(cmd, true);
            if (!result.empty())
                return result + ".";
        }
        return command_to_name(cmd);
    }

    return result.substr(0, result.length() - 1);
}

/**
 * Provide auto-generated information about the given cloud type. Describe
 * opacity & related factors.
 *
 * @param cloud_type        The cloud_type in question.
 * @return e.g. "\nThis cloud is opaque; one tile will not block vision, but
 *      multiple will."
 */
string extra_cloud_info(cloud_type cloud_type)
{
    const bool opaque = is_opaque_cloud(cloud_type);
    const string opacity_info = !opaque ? "" :
        "\nThis cloud is opaque; one tile will not block vision, but "
        "multiple will.";
    return opacity_info;
}
