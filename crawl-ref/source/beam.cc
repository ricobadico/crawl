/**
 * @file
 * @brief Functions related to ranged attacks.
**/

#include "AppHdr.h"

#include "beam.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>

#include "ability.h"    // Drac_breath_power()
#include "act-iter.h"
#include "areas.h"
#include "art-enum.h"
#include "attack.h"
#include "attitude-change.h"
#include "bloodspatter.h"
#include "chardump.h"
#include "chaos.h"
#include "cloud.h"
#include "colour.h"
#include "coordit.h"
#include "delay.h"
#include "directn.h"
#include "dungeon.h"
#include "english.h"
#include "exercise.h"
#include "fight.h"
#include "food.h"
#include "god-abil.h"
#include "god-blessing.h"
#include "god-conduct.h"
#include "god-item.h"
#include "god-passive.h" // passive_t::convert_orcs
#include "item-use.h"
#include "item-prop.h"
#include "items.h"
#include "libutil.h"
#include "losglobal.h"
#include "los.h"
#include "message.h"
#include "mon-behv.h"
#include "mon-cast.h"
#include "mon-death.h"
#include "mon-ench.h"
#include "mon-place.h"
#include "mon-poly.h"
#include "mon-util.h"
#include "mutation.h"
#include "nearby-danger.h"
#include "ouch.h"
#include "player.h"
#include "player-stats.h"
#include "potion.h"
#include "prompt.h"
#include "ranged-attack.h"
#include "religion.h"
#include "shout.h"
#include "spl-clouds.h"
#include "spl-damage.h"
#include "spl-goditem.h"
#include "spl-miscast.h"
#include "spl-monench.h"
#include "spl-summoning.h"
#include "spl-transloc.h"
#include "spl-util.h"
#include "spl-zap.h"
#include "state.h"
#include "stepdown.h"
#include "stringutil.h"
#include "target.h"
#include "teleport.h"
#include "terrain.h"
#include "throw.h"
#ifdef USE_TILE
 #include "tilepick.h"
#endif
#include "tiles-build-specific.h"
#include "transform.h"
#include "traps.h"
#include "unwind.h"
#include "viewchar.h"
#include "view.h"
#include "xom.h"

#define SAP_MAGIC_CHANCE() x_chance_in_y(7, 10)

// Helper functions (some of these should probably be public).
static void _ench_animation(int flavour, const monster* mon = nullptr,
                            bool force = false);
static beam_type _chaos_beam_flavour(bolt* beam);
static string _beam_type_name(beam_type type);
static bool _cigotuvi(monster * mon, actor * agent);
int _ench_pow_to_dur(int pow);

tracer_info::tracer_info()
{
    reset();
}

void tracer_info::reset()
{
    count = power = hurt = helped = 0;
    dont_stop = false;
}

const tracer_info& tracer_info::operator+=(const tracer_info &other)
{
    count  += other.count;
    power  += other.power;
    hurt   += other.hurt;
    helped += other.helped;

    dont_stop = dont_stop || other.dont_stop;

    return *this;
}

bool bolt::is_blockable() const
{
    // BEAM_ELECTRICITY is added here because chain lightning is not
    // a true beam (stops at the first target it gets to and redirects
    // from there)... but we don't want it shield blockable.
    return !pierce && !is_explosion && flavour != BEAM_ELECTRICITY
           && hit != AUTOMATIC_HIT && flavour != BEAM_VISUAL;
}

/// Can 'omnireflection' (from the Warlock's Mirror) potentially reflect this?
bool bolt::is_omnireflectable() const
{
    return !is_explosion && flavour != BEAM_VISUAL
            && origin_spell != SPELL_GLACIATE;
}

void bolt::emit_message(const char* m)
{
    const string message = m;
    if (!message_cache.count(message))
        mpr(m);

    message_cache.insert(message);
}

kill_category bolt::whose_kill() const
{
    if (YOU_KILL(thrower) || source_id == MID_YOU_FAULTLESS)
        return KC_YOU;
    else if (MON_KILL(thrower))
    {
        if (source_id == MID_ANON_FRIEND)
            return KC_FRIENDLY;
        const monster* mon = monster_by_mid(source_id);
        if (mon && mon->friendly())
            return KC_FRIENDLY;
    }
    return KC_OTHER;
}

// A simple animated flash from Rupert Smith (expanded to be more
// generic).
static void _zap_animation(int colour, const monster* mon = nullptr,
                           bool force = false)
{
    coord_def p = you.pos();

    if (mon)
    {
        if (!force && !mon->visible_to(&you))
            return;

        p = mon->pos();
    }

    if (!you.see_cell(p))
        return;

    const coord_def drawp = grid2view(p);

    if (in_los_bounds_v(drawp))
    {
#ifdef USE_TILE
        tiles.add_overlay(p, tileidx_zap(colour));
#endif
#ifndef USE_TILE_LOCAL
        view_update();
        cgotoxy(drawp.x, drawp.y, GOTO_DNGN);
        put_colour_ch(colour, dchar_glyph(DCHAR_FIRED_ZAP));
#endif

        update_screen();

        scaled_delay(50);
    }
}

// Special front function for zap_animation to interpret enchantment flavours.
static void _ench_animation(int flavour, const monster* mon, bool force)
{
    element_type elem;
    switch (flavour)
    {
    case BEAM_HEALING:
        elem = ETC_HEAL;
        break;
    case BEAM_INFESTATION:
    case BEAM_PAIN:
    case BEAM_AGONY:
    case BEAM_VILE_CLUTCH:
        elem = ETC_UNHOLY;
        break;
    case BEAM_DISPEL_UNDEAD:
        elem = ETC_HOLY;
        break;
    case BEAM_POLYMORPH:
    case BEAM_MALMUTATE:
        elem = ETC_MUTAGENIC;
        break;
    case BEAM_CHAOS:
        elem = ETC_RANDOM;
        break;
    case BEAM_TELEPORT:
    case BEAM_BANISH:
    case BEAM_BLINK:
    case BEAM_BLINK_CLOSE:
        elem = ETC_WARP;
        break;
    case BEAM_MAGIC:
        elem = ETC_MAGIC;
        break;
    default:
        elem = ETC_ENCHANT;
        break;
    }

    _zap_animation(element_colour(elem), mon, force);
}

// If needs_tracer is true, we need to check the beam path for friendly
// monsters.
spret zapping(zap_type ztype, int power, bolt &pbolt,
                   bool needs_tracer, const char* msg, bool fail)
{
    dprf(DIAG_BEAM, "zapping: power=%d", power);

    pbolt.thrower = KILL_YOU_MISSILE;

    // Check whether tracer goes through friendlies.
    // NOTE: Whenever zapping() is called with a randomised value for power
    // (or effect), player_tracer should be called directly with the highest
    // power possible respecting current skill, experience level, etc.
    if (needs_tracer && !player_tracer(ztype, power, pbolt))
        return spret::abort;

    fail_check();
    // Fill in the bolt structure.
    zappy(ztype, power, false, pbolt);

    if (msg)
        mpr(msg);

    if (ztype == ZAP_LIGHTNING_BOLT)
    {
        noisy(spell_effect_noise(SPELL_LIGHTNING_BOLT),
               clamp_in_bounds(pbolt.target), "You hear a mighty clap of thunder!");
        pbolt.heard = true;
    }

    if (ztype == ZAP_DIG)
        pbolt.aimed_at_spot = false;

    pbolt.fire();

    return spret::success;
}

// Returns true if the path is considered "safe", and false if there are
// monsters in the way the player doesn't want to hit.
bool player_tracer(zap_type ztype, int power, bolt &pbolt, int range)
{
    // Non-controlleable during confusion.
    // (We'll shoot in a different direction anyway.)
    if (you.confused())
        return true;

    zappy(ztype, power, false, pbolt);

    pbolt.is_tracer     = true;
    pbolt.source        = you.pos();
    pbolt.source_id     = MID_PLAYER;
    pbolt.attitude      = ATT_FRIENDLY;
    pbolt.thrower       = KILL_YOU_MISSILE;

    // Init tracer variables.
    pbolt.friend_info.reset();
    pbolt.foe_info.reset();

    pbolt.foe_ratio        = 100;
    pbolt.beam_cancelled   = false;
    pbolt.dont_stop_player = false;
    pbolt.dont_stop_trees  = false;

    // Clear misc
    pbolt.seen          = false;
    pbolt.heard         = false;
    pbolt.reflections   = 0;
    pbolt.bounces       = 0;

    // Save range before overriding it
    const int old_range = pbolt.range;
    if (range)
        pbolt.range = range;

    pbolt.fire();

    if (range)
        pbolt.range = old_range;

    // Should only happen if the player answered 'n' to one of those
    // "Fire through friendly?" prompts.
    if (pbolt.beam_cancelled)
    {
        dprf(DIAG_BEAM, "Beam cancelled.");
        you.turn_is_over = false;
        return false;
    }

    // Set to non-tracing for actual firing.
    pbolt.is_tracer = false;
    return true;
}

// Returns true if the player wants / needs to abort based on god displeasure
// with targeting this target with this spell. Returns false otherwise.
static bool _stop_because_god_hates_target_prompt(monster* mon,
                                                  spell_type spell)
{
    if (spell == SPELL_TUKIMAS_DANCE)
    {
        const item_def * const first = mon->weapon(0);
        const item_def * const second = mon->weapon(1);
        bool prompt = first && god_hates_item(*first)
                      || second && god_hates_item(*second);
        if (prompt
            && !yesno("Animating this weapon would place you under penance. "
            "Really cast this spell?", false, 'n'))
        {
            return true;
        }
    }

    return false;
}

template<typename T>
class power_deducer
{
public:
    virtual T operator()(int pow) const = 0;
    virtual ~power_deducer() {}
};

typedef power_deducer<int> tohit_deducer;

template<int adder, int mult_num = 0, int mult_denom = 1>
class tohit_calculator : public tohit_deducer
{
public:
    int operator()(int pow) const override
    {
        return adder + pow * mult_num / mult_denom;
    }
};

typedef power_deducer<dice_def> dam_deducer;

template<int numdice, int adder, int mult_num, int mult_denom>
class dicedef_calculator : public dam_deducer
{
public:
    dice_def operator()(int pow) const override
    {
        return dice_def(numdice, adder + pow * mult_num / mult_denom);
    }
};

template<int numdice, int adder, int mult_num, int mult_denom>
class calcdice_calculator : public dam_deducer
{
public:
    dice_def operator()(int pow) const override
    {
        return calc_dice(numdice, adder + pow * mult_num / mult_denom);
    }
};

struct zap_info
{
    zap_type ztype;
    const char* name;           // nullptr means handled specially
    int player_power_cap;
    dam_deducer* player_damage;
    tohit_deducer* player_tohit;    // Enchantments have power modifier here
    dam_deducer* monster_damage;
    tohit_deducer* monster_tohit;
    colour_t colour;
    bool is_enchantment;
    beam_type flavour;
    dungeon_char_type glyph;
    bool always_obvious;
    bool can_beam;
    bool is_explosion;
    int hit_loudness;
};

#include "zap-data.h"

static int zap_index[NUM_ZAPS];

void init_zap_index()
{
    for (int i = 0; i < NUM_ZAPS; ++i)
        zap_index[i] = -1;

    for (unsigned int i = 0; i < ARRAYSZ(zap_data); ++i)
        zap_index[zap_data[i].ztype] = i;
}

static const zap_info* _seek_zap(zap_type z_type)
{
    ASSERT_RANGE(z_type, 0, NUM_ZAPS);
    if (zap_index[z_type] == -1)
        return nullptr;
    else
        return &zap_data[zap_index[z_type]];
}

int zap_power_cap(zap_type z_type)
{
    const zap_info* zinfo = _seek_zap(z_type);

    return zinfo ? zinfo->player_power_cap : 0;
}

int zap_ench_power(zap_type z_type, int pow, bool is_monster)
{
    const zap_info* zinfo = _seek_zap(z_type);
    if (!zinfo)
        return pow;

    if (zinfo->player_power_cap > 0 && !is_monster)
        pow = min(zinfo->player_power_cap, pow);

    tohit_deducer* ench_calc = is_monster ? zinfo->monster_tohit
                                          : zinfo->player_tohit;
    if (zinfo->is_enchantment && ench_calc)
        return (*ench_calc)(pow);
    else
        return pow;
}

void zappy(zap_type z_type, int power, bool is_monster, bolt &pbolt)
{
    const zap_info* zinfo = _seek_zap(z_type);

    // None found?
    if (zinfo == nullptr)
    {
        dprf("Couldn't find zap type %d", z_type);
        return;
    }

    // Fill
    pbolt.name           = zinfo->name;
    pbolt.flavour        = zinfo->flavour;
    pbolt.real_flavour   = zinfo->flavour;
    pbolt.colour         = zinfo->colour;
    pbolt.glyph          = dchar_glyph(zinfo->glyph);
    pbolt.obvious_effect = zinfo->always_obvious;
    pbolt.pierce         = zinfo->can_beam;
    pbolt.is_explosion   = zinfo->is_explosion;

    if (zinfo->player_power_cap > 0 && !is_monster)
        power = min(zinfo->player_power_cap, power);

    ASSERT(zinfo->is_enchantment == pbolt.is_enchantment());

    pbolt.ench_power = zap_ench_power(z_type, power, is_monster);

    if (zinfo->is_enchantment)
        pbolt.hit = AUTOMATIC_HIT;
    else
    {
        tohit_deducer* hit_calc = is_monster ? zinfo->monster_tohit
                                             : zinfo->player_tohit;
        ASSERT(hit_calc);
        pbolt.hit = (*hit_calc)(power);
        if (pbolt.hit != AUTOMATIC_HIT && !is_monster)
        {
            pbolt.hit *= (10 + you.vision());
            pbolt.hit /= 10;
            pbolt.hit = max(0, pbolt.hit);
        }
    }

    dam_deducer* dam_calc = is_monster ? zinfo->monster_damage
                                       : zinfo->player_damage;
    if (dam_calc)
        pbolt.damage = (*dam_calc)(power);

    if (pbolt.origin_spell == SPELL_NO_SPELL)
        pbolt.origin_spell = zap_to_spell(z_type);

    if (!is_monster && pbolt.origin_spell != SPELL_NO_SPELL)
    {
        if (pbolt.is_enchantment() && determine_chaos(&you, pbolt.origin_spell))
        {
            if (pbolt.origin_spell == SPELL_INNER_FLAME)
            {
                pbolt.real_flavour = BEAM_ENTROPIC_BURST;
                pbolt.flavour      = BEAM_ENTROPIC_BURST;
                pbolt.colour       = ETC_JEWEL;
            }
            if (one_chance_in(4))
            {
                pbolt.real_flavour = BEAM_CHAOS_ENCHANTMENT;
                pbolt.flavour      = BEAM_CHAOS_ENCHANTMENT;
                pbolt.colour       = ETC_JEWEL;
            }
        }
        else if (!pbolt.is_enchantment())
        {
            if (you.staff() && is_unrandom_artefact(*you.staff(), UNRAND_MAJIN))
            {
                pbolt.damage.size = div_rand_round(pbolt.damage.size * 5, 4);
                pbolt.real_flavour = pbolt.flavour = BEAM_ELDRITCH;
                pbolt.colour = ETC_UNHOLY;
            }
            else if (determine_chaos(&you, pbolt.origin_spell))
            {
                pbolt.damage.size = div_rand_round(pbolt.damage.size * 5, 4);
                pbolt.real_flavour = pbolt.flavour = BEAM_CHAOTIC;
                pbolt.colour = ETC_JEWEL;
            }
            if (you.staff() && staff_enhances_spell(you.staff(), pbolt.origin_spell))
            {
                if (get_staff_facet(*you.staff()) == SPSTF_ACCURACY)
                    pbolt.hit = AUTOMATIC_HIT;
                if (get_staff_facet(*you.staff()) == SPSTF_MENACE)
                {
                    if (pbolt.damage.num > 6)
                        pbolt.damage.num++;
                    pbolt.damage.num++;
                }
            }
        }
    }

    if (pbolt.loudness == 0)
        pbolt.loudness = zinfo->hit_loudness;
}

bool bolt::can_affect_actor(const actor *act) const
{
    // Blinkbolt doesn't hit its caster, since they are the bolt.
    if (origin_spell == SPELL_BLINKBOLT && act->mid == source_id)
        return false;
    auto cnt = hit_count.find(act->mid);
    if (cnt != hit_count.end() && cnt->second >= 2)
    {
        // Note: this is done for balance, even if it hurts realism a bit.
        // It is arcane knowledge which wall patterns will cause lightning
        // to bounce thrice, double damage for ordinary bounces is enough.
#ifdef DEBUG_DIAGNOSTICS
        if (!quiet_debug)
            dprf(DIAG_BEAM, "skipping beam hit, affected them twice already");
#endif
        return false;
    }

    return true;
}

static beam_type _chaos_enchant_type()
{
    beam_type ret_val;
    ret_val = random_choose_weighted(
        28, BEAM_CHAOTIC_INFUSION,
        14, BEAM_CONFUSION,
        14, BEAM_ENTROPIC_BURST,
        // We don't have a distortion beam, so choose from the three effects
        // we can use, based on the lower weight distortion has.
        5, BEAM_BANISH,
        5, BEAM_BLINK,
        5, BEAM_TELEPORT,
        // From here are beam effects analogous to effects that happen when
        // SPWPN_CHAOS chooses itself again as the ego (roughly 1/7 chance).
        // Weights similar to those from chaos_effects in attack.cc
        10, BEAM_SLOW,
        10, BEAM_HASTE,
        10, BEAM_INVISIBILITY,
        10, BEAM_PETRIFY,
        5, BEAM_BERSERK,
        // Combined weight for poly, clone, and "shapeshifter" effects.
        5, BEAM_POLYMORPH,
        // Seen through miscast effects.
        5, BEAM_ACID,
        5, BEAM_DAMNATION,
        5, BEAM_STICKY_FLAME,
        5, BEAM_DISINTEGRATION,
        // These are not actualy used by SPWPN_CHAOS, but are here to augment
        // the list of effects, since not every SPWN_CHAOS effect has an
        // analogous BEAM_ type.
        4, BEAM_MIGHT,
        4, BEAM_HEALING,
        4, BEAM_AGILITY,
        4, BEAM_ENSNARE);
    return ret_val;
    // I guess these line splits are for the RNG?
}

// Choose the beam effect for BEAM_CHAOS that's analogous to the effect used by
// SPWPN_CHAOS, with weightings similar to those use by that brand. XXX: Rework
// this and SPWPN_CHAOS to use the same tables.
static beam_type _chaos_beam_flavour(bolt* beam)
{
    UNUSED(beam);

    beam_type flavour;
    flavour = random_choose_weighted(
        // SPWPN_CHAOS randomizes to brands analogous to these beam effects
        // with similar weights.
        70, BEAM_FIRE,
        70, BEAM_COLD,
        70, BEAM_ELECTRICITY,
        70, BEAM_POISON,
        // Combined weight from drain + vamp.
        70, BEAM_NEG,
        35, BEAM_HOLY,
       115, BEAM_CHAOS_ENCHANTMENT);
        
        // SPLIT.
    if (flavour == BEAM_CHAOS_ENCHANTMENT)
        flavour = _chaos_enchant_type();

    return flavour;
}

bool bolt::visible() const
{
    return !is_tracer && glyph != 0 && !is_enchantment();
}

void bolt::initialise_fire()
{
    // Fix some things which the tracer might have set.
    extra_range_used   = 0;
    in_explosion_phase = false;
    use_target_as_pos  = false;
    hit_count.clear();

    if (special_explosion != nullptr)
    {
        ASSERT(!is_explosion);
        ASSERT(special_explosion->is_explosion);
        ASSERT(special_explosion->special_explosion == nullptr);
        special_explosion->in_explosion_phase = false;
        special_explosion->use_target_as_pos  = false;
    }

    if (chose_ray)
    {
        ASSERT_IN_BOUNDS(ray.pos());

        if (source == coord_def())
            source = ray.pos();
    }

    if (target == source)
    {
        range             = 0;
        aimed_at_feet     = true;
        auto_hit          = true;
        aimed_at_spot     = true;
        use_target_as_pos = true;
    }

    ASSERT_IN_BOUNDS(source);
    ASSERT_RANGE(flavour, BEAM_NONE + 1, BEAM_FIRST_PSEUDO);
    ASSERT(!drop_item || item && item->defined());
    ASSERTM(range >= 0, "beam '%s', source '%s', item '%s'; has range -1",
            name.c_str(),
            (source_id == MID_PLAYER ? "player" :
                          monster_by_mid(source_id) ?
                             monster_by_mid(source_id)->name(DESC_PLAIN, true) :
                          "unknown").c_str(),
            (item ? item->name(DESC_PLAIN, false, true) : "none").c_str());
    ASSERT(!aimed_at_feet || source == target);

    message_cache.clear();

    // seen might be set by caller to suppress this.
    if (!seen && you.see_cell(source) && range > 0 && visible())
    {
        seen = true;
        const monster* mon = monster_at(source);

        if (flavour != BEAM_VISUAL
            && !YOU_KILL(thrower)
            && !crawl_state.is_god_acting()
            && (!mon || !mon->observable()))
        {
            mprf("%s appears from out of thin air!",
                 article_a(name, false).c_str());
        }
    }

    // Visible self-targeted beams are always seen, even though they don't
    // leave a path.
    if (you.see_cell(source) && target == source && visible())
        seen = true;

    // The agent may die during the beam's firing, need to save these now.
    // If the beam was reflected, assume it can "see" anything, since neither
    // the reflector nor the original source was particularly aiming for this
    // target. WARNING: if you change this logic, keep in mind that
    // menv[YOU_FAULTLESS] cannot be safely queried for properties like
    // can_see_invisible.
    if (reflections > 0)
        nightvision = can_see_invis = true;
    else
    {
        // XXX: Should non-agents count as seeing invisible?
        nightvision = agent() && agent()->nightvision();
        can_see_invis = agent() && agent()->can_see_invisible();
    }

#ifdef DEBUG_DIAGNOSTICS
    // Not a "real" tracer, merely a range/reachability check.
    if (quiet_debug)
        return;

    dprf(DIAG_BEAM, "%s%s%s [%s] (%d,%d) to (%d,%d): "
          "gl=%d col=%d flav=%d hit=%d dam=%dd%d range=%d",
          (pierce) ? "beam" : "missile",
          (is_explosion) ? "*" :
          (is_big_cloud()) ? "+" : "",
          (is_tracer) ? " tracer" : "",
          name.c_str(),
          source.x, source.y,
          target.x, target.y,
          glyph, colour, flavour,
          hit, damage.num, damage.size,
          range);
#endif
}

void bolt::apply_beam_conducts()
{
    if (!is_tracer && YOU_KILL(thrower))
    {
        switch (flavour)
        {
        case BEAM_DAMNATION:
        {
            const int level = 2 + random2(3);
            did_god_conduct(DID_EVIL, level, god_cares());
            break;
        }
        default:
            break;
        }
    }
}

void bolt::choose_ray()
{
    if ((!chose_ray || reflections > 0)
        && !find_ray(source, target, ray, opc_solid_see)
        // If fire is blocked, at least try a visible path so the
        // error message is better.
        && !find_ray(source, target, ray, opc_default))
    {
        fallback_ray(source, target, ray);
    }
}

// Draw the bolt at p if needed.
void bolt::draw(const coord_def& p)
{
    if (is_tracer || is_enchantment() || !you.see_cell(p))
        return;

    // We don't clean up the old position.
    // First, most people like to see the full path,
    // and second, it is hard to do it right with
    // respect to killed monsters, cloud trails, etc.

    const coord_def drawpos = grid2view(p);

    if (!in_los_bounds_v(drawpos))
        return;

#ifdef USE_TILE
    if (tile_beam == -1)
        tile_beam = tileidx_bolt(*this);

    if (tile_beam != -1)
    {
        int dist = (p - source).rdist();
        tiles.add_overlay(p, vary_bolt_tile(tile_beam, dist));
    }
#endif
#ifndef USE_TILE_LOCAL
    cgotoxy(drawpos.x, drawpos.y, GOTO_DNGN);
    put_colour_ch(colour == BLACK ? random_colour(true)
                                  : element_colour(colour),
                  glyph);

    // Get curses to update the screen so we can see the beam.
    update_screen();
#endif
    scaled_delay(draw_delay);
}

// Bounce a bolt off a solid feature.
// The ray is assumed to have just been advanced into
// the feature.
void bolt::bounce()
{
    ASSERT(cell_is_solid(ray.pos()));
    // Don't bounce player tracers off unknown cells, or cells that we
    // incorrectly thought were non-bouncy.
    if (is_tracer && agent() == &you)
    {
        const dungeon_feature_type feat = env.map_knowledge(ray.pos()).feat();

        if (feat == DNGN_UNSEEN || !feat_is_solid(feat) || !is_bouncy(feat))
        {
            ray.regress();
            finish_beam();
            return;
        }
    }

    do
    {
        ray.regress();
    }
    while (cell_is_solid(ray.pos()));

    extra_range_used += range_used(true);
    bounce_pos = ray.pos();
    bounces++;
    reflect_grid rg;
    for (adjacent_iterator ai(ray.pos(), false); ai; ++ai)
        rg(*ai - ray.pos()) = cell_is_solid(*ai);
    ray.bounce(rg);
    extra_range_used += 2;

    ASSERT(!cell_is_solid(ray.pos()));
}

void bolt::fake_flavour()
{
    if (real_flavour == BEAM_RANDOM)
        flavour = static_cast<beam_type>(random_range(BEAM_FIRE, BEAM_ACID));
    else if (real_flavour == BEAM_CHAOS)
        flavour = _chaos_beam_flavour(this);
    else if (real_flavour == BEAM_CHAOS_ENCHANTMENT)
        flavour = _chaos_enchant_type();
    else if (real_flavour == BEAM_CRYSTAL_SPEAR)
        flavour = coinflip() ? BEAM_CRYSTAL_FIRE : BEAM_CRYSTAL_ICE;
    else if (real_flavour == BEAM_ELDRITCH)
    {
        name = pierce ? "eldritch beam of " : is_explosion ? "eldritch blast of " : "eldritch shard of ";
        switch (random2(14))
        {
        case 0:
            flavour = BEAM_LAVA;
            colour = RED;
            name += "magma";
            break;
        case 1:
            flavour = BEAM_MIASMA;
            colour = BLACK;
            name += "miasma";
            break;
        case 2:
            flavour = BEAM_ELECTRICITY;
            colour = LIGHTCYAN;
            name += "lightning";
            break;
        case 3:
        case 7:
            flavour = BEAM_NEG;
            colour = DARKGREY;
            name += "negative energy";
            break;
        case 4:
        case 8:
        case 12:
            flavour = BEAM_ACID;
            colour = YELLOW;
            name += "acid";
            break;
        case 5:
        case 9:
        case 11:
        case 14:
        default: // Just in case.
            flavour = BEAM_DAMNATION;
            colour = LIGHTRED;
            name += "hellfire";
            break;
        case 6:
        case 10:
        case 13:
            flavour = BEAM_DEVASTATION;
            colour = ETC_UNHOLY;
            name += "destruction";
            break;
        }
    }
    else if (real_flavour == BEAM_CHAOTIC || real_flavour == BEAM_CHAOTIC_DEVASTATION)
    {
        name = pierce ? "chaotic beam of " : is_explosion ? "chaotic blast of " : "chaotic shard of ";
        if (origin_spell == SPELL_CHAIN_OF_CHAOS)
            name = "arc of chaotic ";
        if (real_flavour == BEAM_CHAOTIC_DEVASTATION)
            name = "chaotic blast of ";
        switch (random2(12))
        {
        case 0:
            if (coinflip())
            {
                flavour = BEAM_FIRE;
                colour = RED;
                name += "fire";
            }
            else
            {
                flavour = BEAM_LAVA;
                colour = RED;
                name += "magma";
            }
            break;
        case 1:
            if (coinflip())
            {
                flavour = BEAM_COLD;
                colour = WHITE;
                name += "cold";
            }
            else
            {
                flavour = BEAM_FREEZE;
                colour = WHITE;
                name += "ice";
            }
            break;
        case 2:
            flavour = BEAM_ELECTRICITY;
            colour = LIGHTCYAN;
            name += "lightning";
            break;
        case 3:
            if (one_chance_in(4) && !is_good_god(you.religion))
            {
                flavour = BEAM_MIASMA;
                colour = BLACK;
                name += "miasma";
            }
            else if (one_chance_in(3))
            {
                flavour = BEAM_POISON_ARROW;
                colour = LIGHTGREEN;
                name += "strong poison";
            }
            else
            {
                flavour = BEAM_POISON;
                colour = LIGHTGREEN;
                name += "venom";
            }
            break;
        case 4:
            flavour = BEAM_NEG;
            colour = DARKGREY;
            if (!is_good_god(you.religion))
            {
                name += "negative energy";
                break; // Fallthrough if you're with a good god.
            }
        case 5:
            flavour = BEAM_SILVER_FRAG;
            colour = LIGHTGRAY;
            name += "silver fragments";
            break;
        case 6:
            flavour = BEAM_WATER;
            colour = LIGHTBLUE;
            name += "water";
            break;
        case 7:
            flavour = BEAM_DAMNATION;
            colour = LIGHTRED;
            if (!is_good_god(you.religion))
            {
                name += "hellfire";
                break; // Fallthrough if you're with a good god.
            }
        case 8:
            flavour = BEAM_HOLY;
            colour = ETC_HOLY;
            name += "blessed fire";
            break;
        case 9:
            flavour = BEAM_ACID;
            colour = YELLOW;
            name += "acid";
            break;
        case 10:
        default:
            flavour = BEAM_DEVASTATION;
            colour = LIGHTMAGENTA;
            name += "destruction";
            break;
        case 11:
            flavour = BEAM_WAND_HEALING;
            colour = ETC_HEAL;
            name += "healing mist";
            break;
        }
    }
    else if (real_flavour == BEAM_CRYSTAL && flavour == BEAM_CRYSTAL)
    {
        flavour = random_choose(BEAM_FIRE, BEAM_COLD);
        hit_verb = (flavour == BEAM_FIRE) ? "burns" :
                   (flavour == BEAM_COLD) ? "freezes"
                                          : "bugs";
    }
}

void bolt::digging_wall_effect()
{
    if (env.markers.property_at(pos(), MAT_ANY, "veto_dig") == "veto")
    {
        finish_beam();
        return;
    }
    
    bool stop_dig = false;
    const dungeon_feature_type feat = grd(pos());
    if (feat_is_endless(feat) || feat_is_permarock(feat) 
        || feat_is_closed_door(feat) || feat_is_tree(feat)
        || (feat_is_metal(feat) && feat != DNGN_GRATE)
        || feat_is_runed(feat))
        stop_dig = true;
    else if (feat == DNGN_CLEAR_STONE_WALL || feat == DNGN_STONE_WALL
             || feat == DNGN_CRYSTAL_WALL || feat == DNGN_RUNED_CLEAR_STONE_WALL)
        tunnelpower -= 50;
    else if (feat_is_solid(feat))
        tunnelpower -= 20;
    if (tunnelpower < 0)
        stop_dig = true;
    if (!stop_dig)
    {
        destroy_wall(pos());
        if (!msg_generated && feat != DNGN_ORCISH_IDOL)
        {
            if (!you.see_cell(pos()))
            {
                if (!silenced(you.pos()))
                {
                    if (feat == DNGN_GRATE)
                        mprf(MSGCH_SOUND, "You hear a grinding noise.");
                    else 
                        mprf(MSGCH_SOUND, "You hear a grinding noise.");
                    obvious_effect = true; // You may still see the caster.
                    msg_generated = true;
                }
                return;
            }

            obvious_effect = true;
            msg_generated = true;

            string wall;
            if (feat == DNGN_GRATE)
            {
                if (!silenced(you.pos()))
                    mprf(MSGCH_SOUND, "The grate screeches as it bends and collapses.");
                else
                    mpr("The grate bends and falls apart.");
                return;
            }
            else if (feat == DNGN_SLIMY_WALL)
                wall = "slime";
            else if (feat_is_metal(feat))
                wall = "metal";
            else if (feat == DNGN_CRYSTAL_WALL)
                wall = "crystal";
            else if (player_in_branch(BRANCH_PANDEMONIUM))
                wall = "weird stuff";
            else
                wall = "rock";

            mprf("%s %s shatters into small pieces.",
                 agent() && agent()->is_player() ? "The" : "Some",
                 wall.c_str());
        }
        // Orcish idols are important enough to send a second message even if a previous one has sent.
        // Since otherwise (if it's the player's fault) they may have no idea how they were harmed.
        else if (feat == DNGN_ORCISH_IDOL)
        {
            if (!you.see_cell(pos()))
            {
                if (!silenced(you.pos()))
                {
                    mprf(MSGCH_SOUND, "You hear a hideous screaming!");
                    obvious_effect = true; // You may still see the caster.
                    msg_generated = true;
                }
                return;
            }

            obvious_effect = true;
            msg_generated = true;

            if (!silenced(you.pos()))
                mprf(MSGCH_SOUND, "The idol screams as its substance crumbles away!");
            else
                mpr("The idol twists and shakes as its substance crumbles away!");
            if (agent() && agent()->is_player())
                did_god_conduct(DID_DESTROY_ORCISH_IDOL, 8);
            return;
        }
    }
    else if (feat_is_wall(feat))
        finish_beam();
}

void bolt::burn_wall_effect()
{
    dungeon_feature_type feat = grd(pos());
    // Fire affects trees and (wooden) doors.
    if ((!feat_is_tree(feat) && !feat_is_door(feat))
        || env.markers.property_at(pos(), MAT_ANY, "veto_fire") == "veto"
        || !can_burn_trees()) // sanity
    {
        finish_beam();
        return;
    }

    // Destroy the wall.
    destroy_wall(pos());
    if (you.see_cell(pos()))
    {
        if (feat_is_door(feat))
            emit_message("The door bursts into flame!");
        else if (feat == DNGN_MANGROVE)
            emit_message("The tree smoulders and burns.");
        else if (feat == DNGN_SLIMESHROOM)
        {
            if (jiyva_is_dead())
                emit_message("The remains of the mushroom burn like a torch!");
            else
                emit_message("The mushroom smoulders and burns.");
        }
        else
            emit_message("The tree burns like a torch!");
    }
    else if (you.can_smell())
        emit_message("You smell burning wood.");
    if (whose_kill() == KC_YOU && feat_is_tree(feat))
        did_god_conduct(DID_KILL_PLANT, 1, god_cares());
    else if (whose_kill() == KC_FRIENDLY && !crawl_state.game_is_arena() && feat_is_tree(feat))
        did_god_conduct(DID_KILL_PLANT, 1, god_cares());

    // Trees do not burn so readily in a wet environment.
    if (feat == DNGN_MANGROVE || (feat == DNGN_SLIMESHROOM && !jiyva_is_dead()))
        place_cloud(CLOUD_FIRE, pos(), random2(12)+5, agent());
    else
        place_cloud(CLOUD_FOREST_FIRE, pos(), random2(30)+25, agent());
    obvious_effect = true;

    finish_beam();
}

int bolt::range_used(bool leg_only) const
{
    const int leg_length = pos().distance_from(leg_source());
    return leg_only ? leg_length : leg_length + extra_range_used;
}

void bolt::finish_beam()
{
    extra_range_used = BEAM_STOP;
}

void bolt::affect_wall()
{
    if (is_tracer)
    {
        if (!in_bounds(pos()) || !can_affect_wall(pos(), true))
            finish_beam();

        // potentially warn about offending your god by burning trees
        const bool god_relevant = you.religion == GOD_FEDHAS
                                  && can_burn_trees();
        const bool vetoed = env.markers.property_at(pos(), MAT_ANY, "veto_fire")
                            == "veto";

        if (god_relevant && feat_is_tree(grd(pos())) && !vetoed && env.map_knowledge(pos()).known()
            && !is_targeting && YOU_KILL(thrower) && !dont_stop_trees)
        {
            const string prompt =
                make_stringf("Are you sure you want to burn %s?",
                             feature_description_at(pos(), false, DESC_THE,
                                                    false).c_str());

            if (yesno(prompt.c_str(), false, 'n'))
                dont_stop_trees = true;
            else
            {
                canned_msg(MSG_OK);
                beam_cancelled = true;
                finish_beam();
            }
        }

        if (grd(pos()) == DNGN_ORCISH_IDOL && !vetoed && env.map_knowledge(pos()).known()
            && !is_targeting && YOU_KILL(thrower) && flavour == BEAM_DIGGING)
        {
            if (!yesno("Really insult Beogh by defacing this idol?", false, 'n'))
            {
                canned_msg(MSG_OK);
                beam_cancelled = true;
                finish_beam();
            }
        }

        // The only thing that doesn't stop at walls.
        if (flavour != BEAM_DIGGING)
            finish_beam();
        return;
    }
    if (in_bounds(pos()))
    {
        if (flavour == BEAM_DIGGING)
            digging_wall_effect();
        else if (can_burn_trees())
            burn_wall_effect();
        else if (grd(pos()) == DNGN_GRATE)
        {
            destroy_wall(pos());

            if (you.see_cell(pos()))
                emit_message("The acid corrodes the grate, causing it to collapse in on itself!");
            else if (!silenced(you.pos()))
                emit_message("You hear metal creaking and collapsing.");

            finish_beam();
        }
    }
    if (cell_is_solid(pos()))
        finish_beam();
}

coord_def bolt::pos() const
{
    if (in_explosion_phase || use_target_as_pos)
        return target;
    else
        return ray.pos();
}

bool bolt::need_regress() const
{
    // XXX: The affects_wall check probably makes some of the
    //      others obsolete.
    return (is_explosion && !in_explosion_phase)
           || drop_item
           || cell_is_solid(pos()) && !can_affect_wall(pos())
           || origin_spell == SPELL_PRIMAL_WAVE;
}

void bolt::affect_cell()
{
    // Shooting through clouds affects accuracy.
    if (cloud_at(pos()) && hit != AUTOMATIC_HIT)
        hit = max(hit - 2, 0);

    fake_flavour();

    // Note that this can change the solidity of the wall.
    if (cell_is_solid(pos()))
        affect_wall();

    // If the player can ever walk through walls, this will need
    // special-casing too.
    bool hit_player = found_player();
    if (hit_player && can_affect_actor(&you))
    {
        const int prev_reflections = reflections;
        affect_player();
        if (reflections != prev_reflections)
            return;
        if (hit == AUTOMATIC_HIT && !pierce)
            finish_beam();
    }

    // Stop single target beams from affecting a monster if they already
    // affected the player on this square. -cao
    if (!hit_player || pierce || is_explosion)
    {
        monster *m = monster_at(pos());
        if (m && can_affect_actor(m))
        {
            const bool ignored = ignores_monster(m);
            affect_monster(m);
            if (hit == AUTOMATIC_HIT && !pierce && !ignored
                // Assumes tracers will always have an agent!
                && (!is_tracer || m->visible_to(agent())))
            {
                finish_beam();
            }
        }
    }

    if (!cell_is_solid(pos()))
        affect_ground();
}

static void _undo_tracer(bolt &orig, bolt &copy)
{
    // FIXME: we should have a better idea of what gets changed!
    orig.target           = copy.target;
    orig.source           = copy.source;
    orig.aimed_at_spot    = copy.aimed_at_spot;
    orig.extra_range_used = copy.extra_range_used;
    orig.auto_hit         = copy.auto_hit;
    orig.ray              = copy.ray;
    orig.colour           = copy.colour;
    orig.flavour          = copy.flavour;
    orig.real_flavour     = copy.real_flavour;
    orig.bounces          = copy.bounces;
    orig.bounce_pos       = copy.bounce_pos;
}

// This saves some important things before calling fire().
void bolt::fire()
{
    path_taken.clear();

    if (flavour == BEAM_DIGGING)
    {   // Two lines because RNG rules.
        tunnelpower = damage.size * random_range(6, 15);
        tunnelpower = div_rand_round(tunnelpower, 10);
    }

    if (special_explosion)
        special_explosion->is_tracer = is_tracer;

    if (is_tracer)
    {
        bolt boltcopy = *this;
        if (special_explosion != nullptr)
            boltcopy.special_explosion = new bolt(*special_explosion);

        do_fire();

        if (special_explosion != nullptr)
        {
            _undo_tracer(*special_explosion, *boltcopy.special_explosion);
            delete boltcopy.special_explosion;
        }

        _undo_tracer(*this, boltcopy);
    }
    else
        do_fire();

    //XXX: suspect, but code relies on path_taken being non-empty
    if (path_taken.empty())
        path_taken.push_back(source);

    if (special_explosion != nullptr)
    {
        seen           = seen  || special_explosion->seen;
        heard          = heard || special_explosion->heard;
    }
}

void bolt::do_fire()
{
    initialise_fire();

    if (range < extra_range_used && range > 0)
    {
#ifdef DEBUG
        dprf(DIAG_BEAM, "fire_beam() called on already done beam "
             "'%s' (item = '%s')", name.c_str(),
             item ? item->name(DESC_PLAIN).c_str() : "none");
#endif
        return;
    }

    apply_beam_conducts();
    cursor_control coff(false);

#ifdef USE_TILE
    tile_beam = -1;

    if (item && !is_tracer && (flavour == BEAM_MISSILE
                               || flavour == BEAM_VISUAL))
    {
        const coord_def diff = target - source;
        tile_beam = tileidx_item_throw(get_item_info(*item), diff.x, diff.y);
    }
#endif

    msg_generated = false;
    if (!aimed_at_feet)
    {
        choose_ray();
        // Take *one* step, so as not to hurt the source.
        ray.advance();
    }

    // Note: nothing but this loop should be changing the ray.
    while (map_bounds(pos()))
    {
        if (range_used() > range)
        {
            ray.regress();
            extra_range_used++;
            ASSERT(range_used() >= range);
            break;
        }

        const dungeon_feature_type feat = grd(pos());

        if (in_bounds(target)
            // We ran into a solid wall with a real beam...
            && (feat_is_solid(feat)
                && flavour != BEAM_DIGGING && flavour <= BEAM_LAST_REAL
                && !cell_is_solid(target)
            // or visible firewood with a non-penetrating beam...
                || !pierce
                   && monster_at(pos())
                   && you.can_see(*monster_at(pos()))
                   && !ignores_monster(monster_at(pos()))
                   && mons_is_firewood(*monster_at(pos())))
            // and it's a player tracer...
            // (!is_targeting so you don't get prompted while adjusting the aim)
            && is_tracer && !is_targeting && YOU_KILL(thrower)
            // and we're actually between you and the target...
            && !passed_target && pos() != target && pos() != source
            // ?
            && foe_info.count == 0 && bounces == 0 && reflections == 0
            // and you aren't shooting out of LOS.
            && you.see_cell(target))
        {
            // Okay, with all those tests passed, this is probably an instance
            // of the player manually targeting something whose line of fire
            // is blocked, even though its line of sight isn't blocked. Give
            // a warning about this fact.
            string prompt = "Your line of fire to ";
            const monster* mon = monster_at(target);

            if (mon && mon->observable())
                prompt += mon->name(DESC_THE);
            else
            {
                prompt += "the targeted "
                        + feature_description_at(target, false, DESC_PLAIN, false);
            }

            prompt += " is blocked by "
                    + (feat_is_solid(feat) ?
                        feature_description_at(pos(), false, DESC_A, false) :
                        monster_at(pos())->name(DESC_A));

            prompt += ". Continue anyway?";

            if (!yesno(prompt.c_str(), false, 'n'))
            {
                canned_msg(MSG_OK);
                beam_cancelled = true;
                finish_beam();
                return;
            }

            // Well, we warned them.
        }

        // digging is taken care of in affect_cell
        if (feat_is_solid(feat) && !can_affect_wall(pos())
                                                    && flavour != BEAM_DIGGING)
        {
            if (is_bouncy(feat))
            {
                bounce();
                // see comment in bounce(); the beam will be cancelled if this
                // is a tracer and showing the bounce would be an info leak.
                // In that case, we have to break early to avoid adding this
                // square to path_taken twice, which would make it look like a
                // a bounce ANYWAY.
                if (range_used() > range)
                    break;
            }
            else
            {
                // Regress for explosions: blow up in an open grid (if regressing
                // makes any sense). Also regress when dropping items.
                if (pos() != source && need_regress())
                {
                    do
                    {
                        ray.regress();
                    }
                    while (ray.pos() != source && cell_is_solid(ray.pos()));

                    // target is where the explosion is centered, so update it.
                    if (is_explosion && !is_tracer)
                        target = ray.pos();
                }
                break;
            }
        }

        path_taken.push_back(pos());

        if (!affects_nothing)
            affect_cell();

        if (range_used() > range)
            break;

        if (beam_cancelled)
            return;

        // Weapons of returning should find an inverse ray
        // through find_ray and setup_retrace, but they didn't
        // always in the past, and we don't want to crash
        // if they accidentally pass through a corner.
        // Dig tracers continue through unseen cells.
        ASSERT(!cell_is_solid(pos())
               || is_tracer && can_affect_wall(pos(), true)
               || affects_nothing); // returning weapons

        const bool was_seen = seen;
        if (!was_seen && range > 0 && visible() && you.see_cell(pos()))
            seen = true;

        if (flavour != BEAM_VISUAL && !was_seen && seen && !is_tracer)
        {
            mprf("%s appears from out of your range of vision.",
                 article_a(name, false).c_str());
        }

        // Reset chaos beams so that it won't be considered an invisible
        // enchantment beam for the purposes of animation.
        if (real_flavour == BEAM_CHAOS)
            flavour = real_flavour;

        // Actually draw the beam/missile/whatever, if the player can see
        // the cell.
        if (animate)
            draw(pos());

        if (pos() == target)
        {
            passed_target = true;
            if (stop_at_target())
                break;
        }

        noise_generated = false;

        ray.advance();
    }

    if (!map_bounds(pos()))
    {
        ASSERT(!aimed_at_spot);

        int tries = max(GXM, GYM);
        while (!map_bounds(ray.pos()) && tries-- > 0)
            ray.regress();

        // Something bizarre happening if we can't get back onto the map.
        ASSERT(map_bounds(pos()));
    }

    // The beam has terminated.
    if (!affects_nothing)
        affect_endpoint();

    // Tracers need nothing further.
    if (is_tracer || affects_nothing)
        return;

    // Canned msg for enchantments that affected no-one, but only if the
    // enchantment is yours (and it wasn't a chaos beam, since with chaos
    // enchantments are entirely random, and if it randomly attempts
    // something which ends up having no obvious effect then the player
    // isn't going to realise it).
    if (!msg_generated && !obvious_effect && is_enchantment()
        && real_flavour != BEAM_CHAOS
        && YOU_KILL(thrower))
    {
        canned_msg(MSG_NOTHING_HAPPENS);
    }

    // Reactions if a monster zapped the beam.
    if (monster_by_mid(source_id))
    {
        if (foe_info.hurt == 0 && friend_info.hurt > 0)
            xom_is_stimulated(100);
        else if (foe_info.helped > 0 && friend_info.helped == 0)
            xom_is_stimulated(100);

        // Allow friendlies to react to projectiles, except when in
        // sanctuary when pet_target can only be explicitly changed by
        // the player.
        const monster* mon = monster_by_mid(source_id);
        if (foe_info.hurt > 0 && !mon->wont_attack() && !crawl_state.game_is_arena()
            && you.pet_target == MHITNOT && env.sanctuary_time <= 0)
        {
            you.pet_target = mon->mindex();
        }
    }
}

// Returns damage taken by a monster from a "flavoured" (fire, ice, etc.)
// attack -- damage from clouds and branded weapons handled elsewhere.
int mons_adjust_flavoured(monster* mons, bolt &pbolt, int hurted,
                          bool doFlavouredEffects)
{
    // If we're not doing flavoured effects, must be preliminary
    // damage check only.
    // Do not print messages or apply any side effects!
    int original = hurted;

    if (pbolt.flavour == BEAM_PARADOXICAL)
    {
        pbolt.real_flavour = BEAM_PARADOXICAL;
        if (grid_distance(coord_def(1, 1), you.pos()) % 2)
            pbolt.flavour = BEAM_FIRE;
        else
            pbolt.flavour = BEAM_COLD;
    }

    switch (pbolt.flavour)
    {
    case BEAM_ROT:
    {
        if (mons->is_insubstantial() && bool(mons->holiness() & MH_UNDEAD))
            return 0;
            
        // Early out for tracer/no side effects.
        if (!doFlavouredEffects)
            return hurted;

        bool success = false;

        if (bool(mons->holiness() & MH_NONLIVING) && mons->res_acid() < 3)
        {
            mprf("The vicious blight erodes %s", mons->name(DESC_THE).c_str());
            if (one_chance_in(3))
                mons->corrode_equipment("foul blight", 1);
        }
        else
        {
            if (miasma_monster(mons, pbolt.agent()))
                success = true;

            simple_monster_message(*mons, " seems to rot from the inside!");

            if (!success)
            {
                if (poison_monster(mons, pbolt.agent(), 1 + random2(3), true, false))
                    success = true;
            }
            if (!success || one_chance_in(4))
            {
                if (!one_chance_in(3))
                {
                    if (mons->can_mutate())
                        mons->malmutate("foul blight");
                    else
                        mons->weaken(pbolt.agent(), 8);
                }
                else
                    mons->corrode_equipment("foul blight", 1);
            }
        }

        if (YOU_KILL(pbolt.thrower))
            did_god_conduct(DID_UNCLEAN, 2, pbolt.god_cares());
    }
    case BEAM_CRYSTAL_FIRE:
    case BEAM_FIRE:
    case BEAM_STEAM:
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);

        if (!hurted)
        {
            if (original > 0 && doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");
        }
        else if (original > hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " resists.");
        }
        else if (original < hurted && doFlavouredEffects)
        {
            if (mons->is_icy())
                simple_monster_message(*mons, " melts!");
            else if (mons_species(mons->type) == MONS_BUSH
                     && mons->res_fire() < 0)
            {
                simple_monster_message(*mons, " is on fire!");
            }
            else if (pbolt.flavour == BEAM_STEAM)
                simple_monster_message(*mons, " is scalded terribly!");
            else
                simple_monster_message(*mons, " is burned terribly!");
        }
        break;

    case BEAM_WATER:
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);
        if (hurted > original && doFlavouredEffects)
            simple_monster_message(*mons, " is doused terribly!");
        break;

    case BEAM_COLD:
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);
        if (!hurted)
        {
            if (original > 0 && doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");
        }
        else if (original > hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " resists.");
        }
        else if (original < hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " is frozen!");
        }
        break;

    case BEAM_SILVER:
    case BEAM_SILVER_FRAG:
    {
        if (doFlavouredEffects)
        {
            string msg;
            silver_damages_victim(mons, hurted, msg);
            if (!msg.empty())
                mpr(msg);
        }
        break;
    }

    case BEAM_ELECTRICITY:
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);
        if (!hurted)
        {
            if (original > 0 && doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");
        }
        else if (original > hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " resists.");
        }
        else if (original < hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " is electrocuted!");
        }
        break;

    case BEAM_ACID_WAVE:
    case BEAM_ACID:
    {
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);

        if (!hurted)
        {
            if (original > 0 && doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");
        }
        
        if (original > hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " resists.");
        }
        
        if (original < hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " is burned terribly!");
        }

        if (hurted && mons->res_acid() <= 2 && doFlavouredEffects)
            mons->splash_with_acid(pbolt.agent(), div_round_up(hurted, 10));
        break;
    }

    case BEAM_POISON:
    {
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);

        if (doFlavouredEffects)
        {
            if (!hurted)
                simple_monster_message(*mons,
                    (original > 0) ? " completely resists."
                    : " appears unharmed.");
            else if (hurted < original)
                simple_monster_message(*mons, " partially resists.");
            else
                poison_monster(mons, pbolt.agent());
        }

        break;
    }

    case BEAM_IRRADIATE:
        if (doFlavouredEffects && hurted)
            mons->malmutate("mutagenic radiation");
        break;

    case BEAM_POISON_ARROW:
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);
        if (hurted < original)
        {
            if (doFlavouredEffects)
            {
                simple_monster_message(*mons, " partially resists.");

                poison_monster(mons, pbolt.agent(), 2, true);
            }
        }
        else if (doFlavouredEffects)
            poison_monster(mons, pbolt.agent(), 4, true);

        break;

    case BEAM_NEG:
        if (mons->res_negative_energy() == 3)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");

            hurted = 0;
        }
        else
        {
            hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);

            // Early out if no side effects.
            if (!doFlavouredEffects)
                return hurted;

            if (original > hurted)
                simple_monster_message(*mons, " resists.");
            else if (original < hurted)
                simple_monster_message(*mons, " is drained terribly!");

            if (mons->observable())
                pbolt.obvious_effect = true;

            mons->drain_exp(pbolt.agent());

            if (YOU_KILL(pbolt.thrower))
                did_god_conduct(DID_EVIL, 2, pbolt.god_cares());
        }
        break;

    case BEAM_MIASMA:
        if (mons->res_rotting())
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");

            hurted = 0;
        }
        else
        {
            // Early out for tracer/no side effects.
            if (!doFlavouredEffects)
                return hurted;

            miasma_monster(mons, pbolt.agent());

            if (YOU_KILL(pbolt.thrower))
                did_god_conduct(DID_UNCLEAN, 2, pbolt.god_cares());
        }
        break;

    case BEAM_HOLY:
    {
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);
        if (doFlavouredEffects && original > 0
            && (!hurted || hurted != original))
        {
            simple_monster_message(*mons, hurted == 0 ? " completely resists." :
                                    hurted < original ? " resists." :
                                    " writhes in agony!");

        }
        break;
    }

    case BEAM_CRYSTAL_ICE:
    case BEAM_FREEZE:
    case BEAM_ICE:
        // Weird special case; but decided to put it in for practical purposes
        if (mons->is_icy() && pbolt.name == "icy shards")
        {
            simple_monster_message(*mons, " is unaffected.");
            hurted = 0;
            break;
        }

        // ice - 40% of damage is cold, other 60% is impact and
        // can't be resisted (except by AC, of course)
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);
        if (hurted < original)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " partially resists.");
        }
        else if (hurted > original)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " is frozen!");
        }
        break;

    case BEAM_LAVA:
        hurted = resist_adjust_damage(mons, pbolt.flavour, hurted);

        if (hurted < original)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " partially resists.");
        }
        else if (hurted > original)
        {
            if (mons->is_icy())
            {
                if (doFlavouredEffects)
                    simple_monster_message(*mons, " melts!");
            }
            else
            {
                if (doFlavouredEffects)
                    simple_monster_message(*mons, " is burned terribly!");
            }
        }
        break;

    case BEAM_DAMNATION:
        if (mons->res_damnation())
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");

            hurted = 0;
        }
        break;

    case BEAM_MEPHITIC:
        if (mons->res_poison() > 0)
        {
            if (original > 0 && doFlavouredEffects)
                simple_monster_message(*mons, " completely resists.");

            hurted = 0;
        }
        break;

    case BEAM_MAGIC_CANDLE:
        if (doFlavouredEffects)
            backlight_monster(mons);
        // Fallthrough

    case BEAM_BLOOD:
    case BEAM_FOG:
        hurted = 0;
        break;

    case BEAM_BUTTERFLY:
        if (doFlavouredEffects && mons->is_summoned())
        {
            mon_enchant abj = mons->get_ench(ENCH_ABJ);

            if (pbolt.agent()->is_player())
            {
                if (mons->wont_attack())
                {
                    abj.duration += hurted * BASELINE_DELAY;
                    mprf("You extend %s time in this world.", mons->name(DESC_ITS).c_str());
                }
                else
                {
                    abj.duration = max(abj.duration - hurted * BASELINE_DELAY, 1);
                    simple_monster_message(*mons, " shudders.");
                }
            }
            else
            {
                if (mons_aligned(pbolt.agent(), mons))
                {
                    abj.duration += hurted * BASELINE_DELAY;
                    mprf("%s extend %s time in this world.", pbolt.agent()->name(DESC_THE).c_str(), mons->name(DESC_ITS).c_str());
                }
                else
                {
                    abj.duration = max(abj.duration - hurted * BASELINE_DELAY, 1);
                    simple_monster_message(*mons, " shudders%s.");
                }
            }
            mons->update_ench(abj);
        }
        hurted = 0;
        break;

    case BEAM_WAND_HEALING:
        if (doFlavouredEffects)
        {
            if (pbolt.agent()->is_player())
            {
                if (!mons->wont_attack() && !mons->neutral() && you.religion == GOD_ELYVILON)
                    try_to_pacify(*mons, hurted, hurted * 2);
                else
                    heal_monster(*mons, hurted);
            }
            else
            {
                if (you.can_see(*mons) && mons->hit_points < mons->max_hit_points)
                    simple_monster_message(*mons, " wounds heal themselves!");
                mons->heal(hurted);
            }
        }
        hurted = 0;
        break;

    case BEAM_SPORE:
        if (mons->type == MONS_BALLISTOMYCETE)
            hurted = 0;
        break;

    case BEAM_AIR:
        if (mons->res_wind())
            hurted = 0;
        else if (mons->airborne())
            hurted += hurted / 2;
        if (original < hurted)
        {
            if (doFlavouredEffects)
                simple_monster_message(*mons, " gets badly buffeted.");
        }
        break;

    case BEAM_ENSNARE:
        if (doFlavouredEffects)
            ensnare(mons, hurted);
        hurted = 0;
        break;

    default:
        break;
    }

    if (doFlavouredEffects && mons->alive())
    {
        const int burn_power = (pbolt.is_explosion) ? 5 :
                               (pbolt.pierce)       ? 3
                                                    : 2;
        mons->expose_to_element(pbolt.flavour, burn_power, false);
    }

    // Reset!
    if (pbolt.real_flavour == BEAM_PARADOXICAL)
        pbolt.flavour = BEAM_PARADOXICAL;

    return hurted;
}

static bool _monster_resists_mass_enchantment(monster* mons,
                                              enchant_type wh_enchant,
                                              int pow,
                                              bool* did_msg)
{
    // Assuming that the only mass charm is control undead.
    if (wh_enchant == ENCH_CHARM)
    {
        if (you.get_mutation_level(MUT_NO_LOVE))
            return true;

        if (mons->friendly())
            return true;

        if (!(mons->holiness() & MH_UNDEAD))
            return true;

        int res_margin = mons->check_res_magic(pow);
        if (res_margin > 0)
        {
            if (simple_monster_message(*mons,
                    mons->resist_margin_phrase(res_margin).c_str()))
            {
                *did_msg = true;
            }
            return true;
        }
    }
    else if (wh_enchant == ENCH_INSANE
             || mons->holiness() & MH_NATURAL)
    {
        if (wh_enchant == ENCH_FEAR
            && mons->friendly())
        {
            return true;
        }

        if (wh_enchant == ENCH_INSANE
            && !mons->can_go_frenzy())
        {
            return true;
        }

        int res_margin = mons->check_res_magic(pow);
        if (res_margin > 0)
        {
            if (simple_monster_message(*mons,
                    mons->resist_margin_phrase(res_margin).c_str()))
            {
                *did_msg = true;
            }
            return true;
        }
    }
    // Mass enchantments around lots of plants/fungi shouldn't cause a flood
    // of "is unaffected" messages. --Eino
    else if (mons_is_firewood(*mons))
        return true;
    else  // trying to enchant an unnatural creature doesn't work
    {
        if (simple_monster_message(*mons, " is unaffected."))
            *did_msg = true;
        return true;
    }

    // If monster was affected, then there was a message.
    *did_msg = true;
    return false;
}

// Enchants all monsters in player's sight.
// If m_succumbed is non-nullptr, will be set to the number of monsters that
// were enchanted. If m_attempted is not nullptr, will be set to the number of
// monsters that we tried to enchant.
spret mass_enchantment(enchant_type wh_enchant, int pow, bool fail)
{
    fail_check();
    bool did_msg = false;

    // Give mass enchantments a power multiplier.
    pow *= 3;
    pow /= 2;

    pow = min(pow, 200);

    for (monster_iterator mi; mi; ++mi)
    {
        if (!you.see_cell_no_trans(mi->pos()))
            continue;

        if (mi->has_ench(wh_enchant))
            continue;

        bool resisted = _monster_resists_mass_enchantment(*mi, wh_enchant,
                                                          pow, &did_msg);

        if (resisted)
            continue;

        if ((wh_enchant == ENCH_INSANE && mi->go_frenzy(&you))
            || (wh_enchant == ENCH_CHARM && mi->has_ench(ENCH_HEXED))
            || (wh_enchant != ENCH_INSANE
                && mi->add_ench(mon_enchant(wh_enchant, 0, &you))))
        {
            // Do messaging.
            const char* msg;
            switch (wh_enchant)
            {
            case ENCH_FEAR:      msg = " looks frightened!";      break;
            case ENCH_CHARM:     msg = " submits to your will.";  break;
            default:             msg = nullptr;                   break;
            }
            if (msg && simple_monster_message(**mi, msg))
                did_msg = true;

            // Reassert control over hexed undead.
            if (wh_enchant == ENCH_CHARM && mi->has_ench(ENCH_HEXED))
                mi->del_ench(ENCH_HEXED);

            // Extra check for fear (monster needs to reevaluate behaviour).
            if (wh_enchant == ENCH_FEAR)
                behaviour_event(*mi, ME_SCARE, &you);
        }
    }

    if (!did_msg)
        canned_msg(MSG_NOTHING_HAPPENS);

    if (wh_enchant == ENCH_INSANE)
        did_god_conduct(DID_HASTY, 8, true);

    return spret::success;
}

// Petrification works in two stages. First the monster is slowed down in
// all of its actions, and when that times out it remains properly petrified
// (no movement or actions). The second part is similar to paralysis,
// except that insubstantial monsters can't be affected and damage is
// drastically reduced.
void bolt::apply_bolt_petrify(monster* mons)
{
    if (mons->petrified())
        return;

    if (mons->res_petrify())
        return;

    if (mons->petrifying())
    {
        // If the petrifying is not yet finished, we can force it to happen
        // right away by casting again. Otherwise, the spell has no further
        // effect.
        mons->del_ench(ENCH_PETRIFYING, true, false);
        // del_ench() would do it, but let's call it ourselves for proper agent
        // blaming and messaging.
        if (mons->fully_petrify(agent()))
            obvious_effect = true;
    }
    else if (mons->add_ench(mon_enchant(ENCH_PETRIFYING, 0, agent())))
    {
        if (!mons_is_immotile(*mons)
            && simple_monster_message(*mons, " is moving more slowly."))
        {
            obvious_effect = true;
        }
    }
}

static bool _curare_hits_monster(actor *agent, monster* mons, int levels)
{
    if (!mons->alive())
        return false;

    if (mons->res_poison() > 0)
        return false;

    poison_monster(mons, agent, levels, false);

    int hurted = 0;

    if (!mons->is_unbreathing())
    {
        hurted = roll_dice(levels, 6);

        if (hurted)
        {
            simple_monster_message(*mons, " convulses.");
            mons->hurt(agent, hurted, BEAM_POISON);
        }
    }

    if (mons->alive())
    {
        if (!mons->cannot_move())
        {
            simple_monster_message(*mons, mons->has_ench(ENCH_SLOW)
                                         ? " seems to be slow for longer."
                                         : " seems to slow down.");
        }
        // FIXME: calculate the slow duration more cleanly
        mon_enchant me(ENCH_SLOW, 0, agent);
        levels -= 2;
        while (levels > 0)
        {
            mon_enchant me2(ENCH_SLOW, 0, agent);
            me.set_duration(mons, &me2);
            levels -= 2;
        }
        mons->add_ench(me);
    }

    return hurted > 0;
}

// Actually poisons a monster (with message).
bool poison_monster(monster* mons, const actor *who, int levels,
                    bool force, bool verbose)
{
    if (!mons->alive() || levels <= 0)
        return false;

    if (monster_resists_this_poison(*mons, force))
        return false;

    const mon_enchant old_pois = mons->get_ench(ENCH_POISON);
    mons->add_ench(mon_enchant(ENCH_POISON, levels, who));
    const mon_enchant new_pois = mons->get_ench(ENCH_POISON);

    // Actually do the poisoning. The order is important here.
    if (new_pois.degree > old_pois.degree
        || new_pois.degree >= MAX_ENCH_DEGREE_DEFAULT)
    {
        if (verbose)
        {
            const char* msg;
            if (new_pois.degree >= MAX_ENCH_DEGREE_DEFAULT)
                msg = " looks as sick as possible!";
            else if (old_pois.degree > 0)
                msg = " looks even sicker.";
            else
                msg = " is poisoned.";

            simple_monster_message(*mons, msg);
        }
    }

    return new_pois.duration > old_pois.duration;
}

// Actually poisons, rots, and/or slows a monster with miasma (with
// message).
bool miasma_monster(monster* mons, const actor* who)
{
    if (!mons->alive())
        return false;

    if (mons->res_rotting())
        return false;

    bool success = poison_monster(mons, who);

    if (who && who->is_player()
        && is_good_god(you.religion)
        && !(success && you_worship(GOD_SHINING_ONE))) // already penalized
    {
        did_god_conduct(DID_EVIL, 5 + random2(3));
    }

    if (mons->max_hit_points > 4 && coinflip())
    {
        mons->max_hit_points--;
        mons->hit_points = min(mons->max_hit_points, mons->hit_points);
        success = true;
    }

    if (one_chance_in(3))
    {
        bolt beam;
        beam.flavour = BEAM_SLOW;
        beam.apply_enchantment_to_monster(mons);
        success = true;
    }

    return success;
}

// Actually napalms a monster (with message).
bool napalm_monster(monster* mons, const actor *who, int levels, bool verbose)
{
    if (!mons->alive())
        return false;

    if (mons->res_sticky_flame() || levels <= 0 || mons->has_ench(ENCH_WATER_HOLD)
        || mons->has_ench(ENCH_AIR_HOLD))
    {
        return false;
    }

    const mon_enchant old_flame = mons->get_ench(ENCH_STICKY_FLAME);
    mons->add_ench(mon_enchant(ENCH_STICKY_FLAME, levels, who));
    const mon_enchant new_flame = mons->get_ench(ENCH_STICKY_FLAME);

    // Actually do the napalming. The order is important here.
    if (new_flame.degree > old_flame.degree)
    {
        if (verbose)
            simple_monster_message(*mons, " is covered in liquid flames!");
        if (who)
            behaviour_event(mons, ME_WHACK, who);
    }

    return new_flame.degree > old_flame.degree;
}

static bool _curare_hits_player(actor* agent, int levels, string name,
                                string source_name, bool mount)
{
    ASSERT(!crawl_state.game_is_arena());

    if (mount)
    {
        if (you.res_poison(true, true) && !one_chance_in(3))
            return false;

        poison_mount(roll_dice(levels, 12) + 1);

        int hurted = roll_dice(levels, 6);

        if (hurted)
        {
            mprf("The curare asphyxiates your %s (%d).", you.mount_name(true).c_str(), hurted);
            damage_mount(hurted);
        }

        slow_mount(10 + random2(levels + random2(3 * levels)));

        return true;
    }

    if (player_res_poison() >= 3
        || player_res_poison() > 0 && !one_chance_in(3))
    {
        return false;
    }

    poison_player(roll_dice(levels, 12) + 1, source_name, name);

    int hurted = 0;

    if (!you.is_unbreathing())
    {
        hurted = roll_dice(levels, 6);

        if (hurted)
        {
            mprf("You have difficulty breathing (%d).", hurted);
            ouch(hurted, KILLED_BY_CURARE, agent->mid,
                 "curare-induced apnoea");
        }
    }

    slow_player(10 + random2(levels + random2(3 * levels)));

    return hurted > 0;
}


bool curare_actor(actor* source, actor* target, int levels, string name,
                  string source_name, bool mount)
{
    if (target->is_player())
        return _curare_hits_player(source, levels, name, source_name, mount);
    else
        return _curare_hits_monster(source, target->as_monster(), levels);
}

// XXX: This is a terrible place for this, but it at least does go with
// curare_actor().
int silver_damages_victim(actor* victim, int damage, string &dmg_msg, bool mount)
{
    int ret = 0;
    if (mount)
    {
        if (is_chaotic_type(mount_mons()))
            ret = div_rand_round(damage * 3, 4);
        else
            return 0;
    }
    else if (victim->how_chaotic() || victim->is_player() && player_is_shapechanged())
    {
        ret = div_rand_round(damage * 3, 4);
    }
    else if (victim->is_player())
    {
        // For mutation damage, we want to count innate mutations for
        // demonspawn but not other species.
        int multiplier = you.how_mutated(false, true, true, you.char_class == JOB_DEMONSPAWN);
        if (multiplier == 0)
            return 0;

        if (multiplier > 15)
            multiplier = 15;

        ret = div_rand_round(damage * multiplier, 20);

        if (you.is_fairy() && x_chance_in_y(20 - multiplier, 20))
            ret = 0;
    }
    else
        return 0;

    dmg_msg = make_stringf("The silver sears %s%s%s", mount ? "your " : "", 
                                                      mount ? you.mount_name(true).c_str() : victim->name(DESC_THE).c_str(), 
                                                      attack_strength_punctuation(ret).c_str());
    return ret;
}

//  Used by monsters in "planning" which spell to cast. Fires off a "tracer"
//  which tells the monster what it'll hit if it breathes/casts etc.
//
//  The output from this tracer function is written into the
//  tracer_info variables (friend_info and foe_info).
//
//  Note that beam properties must be set, as the tracer will take them
//  into account, as well as the monster's intelligence.
void fire_tracer(const actor* act, bolt &pbolt, bool explode_only,
                 bool explosion_hole)
{
    const monster* mons = act->as_monster();

    // Don't fiddle with any input parameters other than tracer stuff!
    pbolt.is_tracer     = true;
    pbolt.source        = act->pos();
    pbolt.source_id     = act->mid;
    pbolt.attitude      = act->is_player() ? ATT_FRIENDLY : mons_attitude(*mons);

    // Init tracer variables.
    pbolt.foe_info.reset();
    pbolt.friend_info.reset();

    // Clear misc
    pbolt.reflections   = 0;
    pbolt.bounces       = 0;

    // If there's a specifically requested foe_ratio, honour it.
    if (!pbolt.foe_ratio)
    {
        pbolt.foe_ratio     = 80;        // default - see mons_should_fire()

        if (act->is_player())
            pbolt.foe_ratio = 100;
        else
        {
            if (mons_is_hepliaklqana_ancestor(mons->type))
                pbolt.foe_ratio = 100; // do not harm the player!
            // Foe ratio for summoning greater demons & undead -- they may be
            // summoned, but they're hostile and would love nothing better
            // than to nuke the player and his minions.
            else if (mons_att_wont_attack(pbolt.attitude)
                && !mons_att_wont_attack(mons->attitude))
            {
                pbolt.foe_ratio = 25;
            }
        }
    }

    pbolt.in_explosion_phase = false;

    // Fire!
    if (explode_only)
        pbolt.explode(false, explosion_hole);
    else
        pbolt.fire();

    // Unset tracer flag (convenience).
    pbolt.is_tracer = false;
}

static coord_def _random_point_hittable_from(const coord_def &c,
                                            int base_radius,
                                            int margin = 1,
                                            int tries = 5)
{
    while (tries-- > 0)
    {
        const int radius = random_range(1, base_radius);
        const coord_def point = dgn_random_point_from(c, radius, margin);
        if (point.origin())
            continue;
        if (!cell_see_cell(c, point, LOS_SOLID))
            continue;
        return point;
    }
    return coord_def();
}

void create_feat_splash(coord_def center, int radius, int nattempts, bool acid)
{
    const dungeon_feature_type feat = acid ? DNGN_SLIMY_WATER : DNGN_SHALLOW_WATER;
    const terrain_change_type type = acid ? TERRAIN_CHANGE_SLIME : TERRAIN_CHANGE_FLOOD;

    // Always affect center, if compatible
    if ((grd(center) == DNGN_FLOOR || grd(center) == feat))
        temp_change_terrain(center, feat, 100 + random2(100), type);

    if (grd(center) == DNGN_LAVA)
        temp_change_terrain(center, DNGN_OBSIDIAN, 100 + random2(100), TERRAIN_CHANGE_FROZEN);

    for (int i = 0; i < nattempts; ++i)
    {
        const coord_def newp(_random_point_hittable_from(center, radius));
        if (newp.origin() || (grd(newp) != DNGN_FLOOR && grd(newp) != feat && grd(newp) != DNGN_LAVA))
            continue;
        
        if (grd(newp) == DNGN_LAVA)
            temp_change_terrain(newp, DNGN_OBSIDIAN, 100 + random2(100), TERRAIN_CHANGE_FROZEN);
        else
            temp_change_terrain(newp, feat, 100 + random2(100), type);
    }
}

bool imb_can_splash(coord_def origin, coord_def center,
                    vector<coord_def> path_taken, coord_def target)
{
    // Don't go back along the path of the beam (the explosion doesn't
    // reverse direction). We do this to avoid hitting the caster and
    // also because we don't want aiming one
    // square past a lone monster to be optimal.
    if (origin == target)
        return false;
    if (find(begin(path_taken), end(path_taken), target) != end(path_taken))
        return false;

    // Don't go far away from the caster (not enough momentum).
    if (grid_distance(origin, center + (target - center)*2)
        > you.current_vision)
    {
        return false;
    }

    return true;
}

void bolt_parent_init(const bolt &parent, bolt &child)
{
    child.name           = parent.name;
    child.short_name     = parent.short_name;
    child.aux_source     = parent.aux_source;
    child.source_id      = parent.source_id;
    child.origin_spell   = parent.origin_spell;
    child.glyph          = parent.glyph;
    child.colour         = parent.colour;

    child.flavour        = parent.flavour;

    // We don't copy target since that is often overriden.
    child.thrower        = parent.thrower;
    child.source         = parent.source;
    child.source_name    = parent.source_name;
    child.attitude       = parent.attitude;

    child.pierce         = parent.pierce ;
    child.is_explosion   = parent.is_explosion;
    child.ex_size        = parent.ex_size;
    child.foe_ratio      = parent.foe_ratio;

    child.is_tracer      = parent.is_tracer;
    child.is_targeting   = parent.is_targeting;

    child.range          = parent.range;
    child.hit            = parent.hit;
    child.damage         = parent.damage;
    if (parent.ench_power != -1)
        child.ench_power = parent.ench_power;

    child.friend_info.dont_stop = parent.friend_info.dont_stop;
    child.foe_info.dont_stop    = parent.foe_info.dont_stop;
    child.dont_stop_player      = parent.dont_stop_player;
    child.dont_stop_trees       = parent.dont_stop_trees;

#ifdef DEBUG_DIAGNOSTICS
    child.quiet_debug    = parent.quiet_debug;
#endif
}

static void _maybe_imb_explosion(bolt *parent, coord_def center)
{
    if (parent->origin_spell != SPELL_THROW_ICICLE
        || parent->in_explosion_phase)
    {
        return;
    }
    const int dist = grid_distance(parent->source, center);
    if (dist == 0 || (!parent->is_tracer && !x_chance_in_y(3, 2 + 2 * dist)))
        return;
    bolt beam;

    bolt_parent_init(*parent, beam);
    beam.name           = "icy shards";
    beam.aux_source     = "icicle";
    beam.range          = 3;
    beam.hit            = AUTOMATIC_HIT;
    beam.colour         = LIGHTCYAN;
    beam.obvious_effect = true;
    beam.pierce         = false;
    beam.is_explosion   = false;
    beam.flavour        = BEAM_ICE;
    // So as not to recur infinitely
    beam.origin_spell = SPELL_NO_SPELL;
    beam.passed_target  = true; // The centre was the target.
    beam.aimed_at_spot  = true;
    if (you.see_cell(center))
        beam.seen = true;
    beam.source         = center;

    bool first = true;
    for (adjacent_iterator ai(center); ai; ++ai)
    {
        if (!imb_can_splash(parent->source, center, parent->path_taken, *ai))
            continue;
        if (!beam.is_tracer && one_chance_in(4))
            continue;

        if (first && !beam.is_tracer)
        {
            if (you.see_cell(center))
                mpr("The icicle shatters into a spray of ice shards!");
            noisy(spell_effect_noise(SPELL_THROW_ICICLE),
                  center);
            first = false;
        }
        beam.friend_info.reset();
        beam.foe_info.reset();
        beam.friend_info.dont_stop = parent->friend_info.dont_stop;
        beam.foe_info.dont_stop = parent->foe_info.dont_stop;
        beam.target = center + (*ai - center) * 2;
        beam.fire();
        parent->friend_info += beam.friend_info;
        parent->foe_info    += beam.foe_info;
        if (beam.is_tracer && beam.beam_cancelled)
        {
            parent->beam_cancelled = true;
            return;
        }
    }
}

static void _malign_offering_effect(actor* victim, const actor* agent, int damage)
{
    if (!agent || damage < 1)
        return;

    // The victim may die.
    coord_def c = victim->pos();

    mprf("%s life force is offered up.", victim->name(DESC_ITS).c_str());
    damage = victim->hurt(agent, damage, BEAM_MALIGN_OFFERING, KILLED_BY_BEAM,
                          "", "by a malign offering");

    // Actors that had LOS to the victim (blocked by glass, clouds, etc),
    // even if they couldn't actually see each other because of blindness
    // or invisibility.
    for (actor_near_iterator ai(c, LOS_NO_TRANS); ai; ++ai)
    {
        if (mons_aligned(agent, *ai) && !(ai->holiness() & MH_NONLIVING)
            && *ai != victim)
        {
            if (ai->heal(max(1, damage * 2 / 3)) && you.can_see(**ai))
            {
                mprf("%s %s healed.", ai->name(DESC_THE).c_str(),
                                      ai->conj_verb("are").c_str());
            }
        }
    }
}

/**
 * Turn a BEAM_UNRAVELLING beam into a BEAM_UNRAVELLED_MAGIC beam, and make
 * it explode appropriately.
 *
 * @param[in,out] beam      The beam being turned into an explosion.
 */
static void _unravelling_explode(bolt &beam)
{
    beam.damage       = dice_def(3, 3 + div_rand_round(beam.ench_power, 6));
    beam.colour       = ETC_MUTAGENIC;
    beam.flavour      = BEAM_UNRAVELLED_MAGIC;
    beam.ex_size      = 1;
    beam.is_explosion = true;
    // and it'll explode 'naturally' a little later.
}

bool bolt::is_bouncy(dungeon_feature_type feat) const
{
    // Don't bounce off open sea.
    if (feat_is_endless(feat))
        return false;

    if (real_flavour == BEAM_CHAOS
        && feat_is_solid(feat))
    {
        return true;
    }

    if ((flavour == BEAM_CRYSTAL || real_flavour == BEAM_CRYSTAL
         || flavour == BEAM_BOUNCY_TRACER)
        && feat_is_solid(feat)
        && !feat_is_tree(feat))
    {
        return true;
    }

    if (is_enchantment())
        return false;

    if (flavour == BEAM_ELECTRICITY && !feat_is_metal(feat)
        && !feat_is_tree(feat))
    {
        return true;
    }

    if ((flavour == BEAM_FIRE || flavour == BEAM_COLD)
        && feat == DNGN_CRYSTAL_WALL)
    {
        return true;
    }

    return false;
}

cloud_type bolt::get_cloud_type() const
{
    if (origin_spell == SPELL_NOXIOUS_CLOUD)
        return CLOUD_MEPHITIC;

    if (origin_spell == SPELL_POISONOUS_CLOUD)
        return CLOUD_POISON;

    if (origin_spell == SPELL_HOLY_BREATH)
        return CLOUD_HOLY;

    if (origin_spell == SPELL_FLAMING_CLOUD)
        return CLOUD_FIRE;

    if (origin_spell == SPELL_CHAOS_BREATH)
        return CLOUD_CHAOS;
	
    if (origin_spell == SPELL_RADIATION_BREATH)
        return CLOUD_MUTAGENIC;

    if (origin_spell == SPELL_MIASMA_BREATH)
        return CLOUD_MIASMA;

    if (origin_spell == SPELL_TRIPLE_BREATH)
        return CLOUD_POISON;

    if (origin_spell == SPELL_FREEZING_CLOUD)
        return CLOUD_COLD;

    if (origin_spell == SPELL_SPECTRAL_CLOUD)
        return CLOUD_SPECTRAL;

    if (origin_spell == SPELL_EMPOWERED_BREATH)
    {
        if (flavour == BEAM_FIRE)
            return CLOUD_STEAM;
        if (flavour == BEAM_COLD)
            return CLOUD_COLD;
        if (flavour == BEAM_IRRADIATE)
            return CLOUD_MUTAGENIC;
        if (flavour == BEAM_HOLY)
            return CLOUD_HOLY;
        if (flavour == BEAM_PARADOXICAL)
            return CLOUD_POISON;
        if (flavour == BEAM_NEG)
            return CLOUD_NEGATIVE_ENERGY;
        if (flavour == BEAM_ROT)
            return CLOUD_ROT;
    }

    return CLOUD_NONE;
}

int bolt::get_cloud_pow() const
{
    if (origin_spell == SPELL_FREEZING_CLOUD
        || origin_spell == SPELL_POISONOUS_CLOUD
        || origin_spell == SPELL_HOLY_BREATH)
    {
        return random_range(10, 20);
    }

    if (origin_spell == SPELL_SPECTRAL_CLOUD 
        || origin_spell == SPELL_EMPOWERED_BREATH)
        return random_range(15, 30);

    return damage.roll() / 3;
}

int bolt::get_cloud_size(bool min, bool max) const
{
    if (origin_spell == SPELL_MEPHITIC_CLOUD
        || origin_spell == SPELL_MIASMA_BREATH
        || origin_spell == SPELL_FREEZING_CLOUD)
    {
        return 10;
    }

    if (min)
        return 8;
    if (max)
        return 12;

    if (origin_spell == SPELL_EMPOWERED_BREATH)
    {
        if (flavour == BEAM_FIRE || flavour == BEAM_PARADOXICAL)
            return 15 + random2(10);
        if (flavour == BEAM_COLD || flavour == BEAM_IRRADIATE || flavour == BEAM_NEG)
            return 2 + random2(7);
    }

    return 8 + random2(5);
}

void bolt::affect_endpoint()
{
    if (!in_bounds(pos()))
        return;

    if (special_explosion)
    {
        special_explosion->target = pos();
        special_explosion->refine_for_explosion();
        special_explosion->explode();

        // XXX: we're significantly overcounting here.
        foe_info      += special_explosion->foe_info;
        friend_info   += special_explosion->friend_info;
        beam_cancelled = beam_cancelled || special_explosion->beam_cancelled;
    }

    // Leave an object, if applicable.
    if (drop_item && item)
        drop_object();

    if (is_explosion)
    {
        target = pos();
        refine_for_explosion();
        explode();
        return;
    }

    _maybe_imb_explosion(this, pos());

    const cloud_type cloud = get_cloud_type();

    if (is_tracer)
    {
        if (cloud == CLOUD_NONE)
            return;

        targeter_cloud tgt(agent(), range, get_cloud_size(true),
                            get_cloud_size(false, true));
        tgt.set_aim(pos());
        for (const auto &entry : tgt.seen)
        {
            if (entry.second != AFF_YES && entry.second != AFF_MAYBE)
                continue;

            if (entry.first == you.pos())
                tracer_affect_player();
            else if (monster* mon = monster_at(entry.first))
                tracer_affect_monster(mon);

            if (agent()->is_player() && beam_cancelled)
                return;
        }

        return;
    }

    if (real_flavour == BEAM_CHAOTIC || real_flavour == BEAM_CHAOTIC_DEVASTATION)
    {
        if (flavour == BEAM_WATER || flavour == BEAM_LAVA)
        {
            bool lava = (flavour == BEAM_LAVA);
            int dur = damage.roll();
            if (grd(pos()) == DNGN_FLOOR)
                temp_change_terrain(pos(), lava ? DNGN_LAVA : DNGN_SHALLOW_WATER,
                    random_range(dur * 2, dur * 3), TERRAIN_CHANGE_FLOOD);
            for (rectangle_iterator ri(pos(), lava ? 1 : 2); ri; ++ri)
            {
                if ((grd(*ri) == DNGN_FLOOR) && ((lava && one_chance_in(4)) || !lava && !one_chance_in(3)))
                    temp_change_terrain(*ri, lava ? DNGN_LAVA : DNGN_SHALLOW_WATER,
                        random_range(dur * 2, dur * 3), TERRAIN_CHANGE_FLOOD);
            }
        }
    }

    if (!is_explosion && !noise_generated && loudness)
    {
        // Digging can target squares on the map boundary, though it
        // won't remove them of course.
        const coord_def noise_position = clamp_in_bounds(pos());
        noisy(loudness, noise_position, source_id);
        noise_generated = true;
    }

    if (cloud != CLOUD_NONE)
        big_cloud(cloud, agent(), pos(), get_cloud_pow(), get_cloud_size());

    // you like special cases, right?
    switch (origin_spell)
    {
    case SPELL_PRIMAL_WAVE:
        if (you.see_cell(pos()))
        {
            mpr("The wave splashes down.");
            noisy(spell_effect_noise(SPELL_PRIMAL_WAVE), pos());
        }
        else
        {
            noisy(spell_effect_noise(SPELL_PRIMAL_WAVE),
                  pos(), "You hear a splash.");
        }

        if (flavour == BEAM_ACID_WAVE)
            create_feat_splash(pos(), 3, random_range(8, 20, 2), true);
        else
            create_feat_splash(pos(), 2, random_range(3, 12, 2));
        break;

    case SPELL_BLINKBOLT:
    {
        actor *act = agent(true); // use orig actor even when reflected
        if (!act || !act->alive())
            return;

        for (vector<coord_def>::reverse_iterator citr = path_taken.rbegin();
             citr != path_taken.rend(); ++citr)
        {
            if (act->is_habitable(*citr) && act->blink_to(*citr, false))
                return;
        }
        return;
    }

    case SPELL_EMPOWERED_BREATH: // Only player gets empowered breath so; these are all player effects.
        // Acid handled elsewhere.
        if (flavour == BEAM_FIRE && !path_taken.empty())
        {
            for (adjacent_iterator ai(pos(), false); ai; ++ai)
            {
                if (!cell_is_solid(*ai) && (*ai == pos() || !one_chance_in(3)))
                    place_cloud(CLOUD_FIRE, *ai, 5 + random2(5), agent(), 2);
            }
        }
        break;

    case SPELL_SEARING_BREATH:
        if (!path_taken.empty() && !cell_is_solid(pos()))
            place_cloud(CLOUD_FIRE, pos(), 5 + random2(5), agent());
        break;

    case SPELL_BREATHE_CHAOTIC:
        if (!path_taken.empty() && !cell_is_solid(pos()))
            place_cloud(chaos_cloud(), pos(), 10 + random2(5), agent(), 3);
        for (adjacent_iterator ai(pos()); ai; ++ai)
        {
            if (!cell_is_solid(*ai) && !one_chance_in(3))
                place_cloud(chaos_cloud(), *ai, 5 + random2(5), agent(), 3);
        }
        break;

    case SPELL_MAGIC_CANDLE:
    {
        if (!hit_something)
        {
            if (feat_is_water(grd(pos())) || grd(pos()) == DNGN_LAVA)
            {
                noisy(2, pos(), source_id);
                noise_generated = true;
                if (!silenced(you.pos()))
                {
                    string x = "";
                    if (grd(pos()) == DNGN_LAVA)
                        x = " sizzling";
                    mprf("You hear a%s splash.", x.c_str());
                }
            }
            else
            {
                mpr("The magic candle falls to the ground, lighting the tile it fell upon for a short while.");
                const int expiry = you.elapsed_time + 60;
                env.sunlight.emplace_back(pos(), expiry);

                {
                    unwind_var<int> no_time(you.time_taken, 0);
                    process_sunlights(false);
                }
            }
        }
        break;
    }

    case SPELL_ENSNARE:
    case SPELL_WAND_ENSNARE:
    {
        if (!actor_at(pos()) && grd(pos()) == DNGN_FLOOR)
        {
            const int pow = damage.roll();
            place_specific_trap(pos(), TRAP_WEB, pow + random2(pow));
            grd(pos()) = DNGN_TRAP_WEB;
        }
        break;
    }

    default:
        break;
    }
}

bool bolt::stop_at_target() const
{
    // the pos check is to avoid a ray.cc assert for a ray that goes nowhere
    return is_explosion || is_big_cloud() ||
            (aimed_at_spot && (pos() == source || flavour != BEAM_DIGGING));
}

void bolt::drop_object()
{
    // BCADNOTE: Removed an assert due to a rare crash. Shouldn't be a behavior change (undefined item that
    // would likely be destroyed anyways. Consider restoring if the removal causes some issue down the line
    // I don't see why it would, but leaving note in case.)
    // Conditions: beam is missile and not tracer.
    if ((item == nullptr) || !item->defined() || is_tracer || !was_missile)
        return;

    // Summoned creatures' thrown items disappear.
    if (item->flags & ISFLAG_SUMMONED)
    {
        if (you.see_cell(pos()))
        {
            mprf("%s %s!",
                 item->name(DESC_THE).c_str(),
                 summoned_poof_msg(agent() ? agent()->as_monster() : nullptr,
                                   *item).c_str());
        }
        item_was_destroyed(*item);
        return;
    }

    if (!thrown_object_destroyed(item))
    {
        if (item->sub_type == MI_THROWING_NET)
        {
            monster* m = monster_at(pos());
            // Player or monster at position is caught in net.
            if (you.pos() == pos() && you.attribute[ATTR_HELD]
                || m && m->caught())
            {
                // If no trapping net found mark this one.
                if (get_trapping_net(pos(), true) == NON_ITEM)
                {
                    set_net_stationary(*item);
                    copy_item_to_grid(*item, pos(), 1);
                    return;
                }
                else
                    item_was_destroyed(*item);
            }
            else
                item_was_destroyed(*item);
        }
        else
            copy_item_to_grid(*item, pos(), 1);
    }
    else 
        item_was_destroyed(*item);
}

// Returns true if the beam hits the player, fuzzing the beam if necessary
// for monsters without see invis firing tracers at the player.
bool bolt::found_player() const
{
    const bool needs_fuzz = (is_tracer && !can_see_invis
                             && you.invisible() && !YOU_KILL(thrower));
    const int dist = needs_fuzz? 2 : 0;

    return grid_distance(pos(), you.pos()) <= dist;
}

void bolt::affect_ground()
{
    // Explosions only have an effect during their explosion phase.
    // Special cases can be handled here.
    if (is_explosion && !in_explosion_phase)
        return;

    if (is_tracer)
        return;

    // Spore explosions might spawn a fungus. The spore explosion
    // covers 21 tiles in open space, so the expected number of spores
    // produced is the x in x_chance_in_y() in the conditional below.
    if (is_explosion && flavour == BEAM_SPORE
        && agent() && !agent()->is_summoned())
    {
        if (env.grid(pos()) == DNGN_FLOOR)
            env.pgrid(pos()) |= FPROP_MOLD;

        if (x_chance_in_y(2, 21)
           && mons_class_can_pass(MONS_BALLISTOMYCETE, env.grid(pos()))
           && !actor_at(pos()))
        {
            beh_type beh = attitude_creation_behavior(attitude);
            // A friendly spore or hyperactive can exist only with Fedhas
            // in which case the inactive ballistos spawned should be
            // good_neutral to avoid hidden piety costs of Fedhas abilities
            if (beh == BEH_FRIENDLY)
                beh = BEH_GOOD_NEUTRAL;

            const god_type god = agent()->deity();

            if (create_monster(mgen_data(MONS_BALLISTOMYCETE,
                                         beh,
                                         pos(),
                                         MHITNOT,
                                         MG_FORCE_PLACE,
                                         god)))
            {
                remove_mold(pos());
                if (you.see_cell(pos()))
                    mpr("A fungus suddenly grows.");

            }
        }
    }

    affect_place_clouds();
}

bool bolt::is_fiery() const
{
    return flavour == BEAM_FIRE || flavour == BEAM_LAVA || flavour == BEAM_STICKY_FLAME || origin_spell == SPELL_SLIME_RUSH;
}

/// Can this bolt burn trees it hits?
bool bolt::can_burn_trees() const
{
    bool flavour_match  = (flavour == BEAM_FIRE);
         flavour_match |= (flavour == BEAM_ELECTRICITY);
         flavour_match |= (flavour == BEAM_LAVA);

    const bool enough_dam = damage.max() > 30;

    return flavour_match && enough_dam;
}

bool bolt::can_affect_wall(const coord_def& p, bool map_knowledge) const
{
    dungeon_feature_type wall = grd(p);

    // digging might affect unseen squares, as far as the player knows
    if (map_knowledge && flavour == BEAM_DIGGING &&
                                        !env.map_knowledge(pos()).seen())
    {
        return true;
    }

    // digging
    if (flavour == BEAM_DIGGING && feat_is_diggable(wall))
        return true;

    if (can_burn_trees())
        return (feat_is_tree(wall) || feat_is_door(wall));

    // Lee's Rapid Deconstruction
    if (origin_spell == SPELL_LRD)
        return true; // smite targeting, we don't care

    return false;
}

// Also used to terrain change ice bridges now (no real reason to have a seperate function for that).
// Also used to summon butterflies with butterfly breath.
void bolt::affect_place_clouds()
{
    if (in_explosion_phase)
        affect_place_explosion_clouds();

    const coord_def p = pos();
    const dungeon_feature_type feat = grd(p);
    actor * defender = actor_at(p);
    bool see_destruction = false;
    bool see_preservation = false;

    // Terrain changes don't care about the clouds.
    if (feat == DNGN_LAVA && (flavour == BEAM_COLD || flavour == BEAM_FREEZE))
    {    
        if (defender && !defender->airborne())
        {
            if (defender->is_player())
            {
                mprf(MSGCH_WARN, "The lava turns into stone around your %s.", you.foot_name(true).c_str());
                you.increase_duration(DUR_LAVA_CAKE, 5 + random2(damage.size/3));
            }
            else
            {
                mprf("%s is trapped by lava hardening to stone.",
                    defender->name(DESC_THE).c_str());
                defender->as_monster()->add_ench(
                    mon_enchant(ENCH_LAVA_CAKE, 0, agent(),
                    (5 + random2(damage.size/3))));
            }
        }
        temp_change_terrain(p, DNGN_OBSIDIAN, damage.roll() * 5, TERRAIN_CHANGE_FROZEN);
    }

    if (feat_is_water(feat) && (flavour == BEAM_COLD || flavour == BEAM_FREEZE))
    {
        if (defender && !defender->airborne())
        {
            if (defender->is_player())
            {
                mprf(MSGCH_WARN, "You are encased in ice.");
                you.increase_duration(DUR_FROZEN, 5 + random2(damage.size/3));
            }
            else
            {
                mprf("%s is flash-frozen.",
                    defender->name(DESC_THE).c_str());
                defender->as_monster()->add_ench(
                    mon_enchant(ENCH_FROZEN, 0, agent(),
                    (5 + random2(damage.size/3))));
            }
        }
        else
        {
            for (stack_iterator si(p); si; ++si)
            {
                if (!is_artefact(*si))
                {
                    item_was_destroyed(*si);
                    destroy_item(si->index());
                    if (player_likes_water())
                        see_destruction = true;
                }
                else
                    see_preservation = true;
            }
            temp_change_terrain(p, DNGN_ICE, damage.roll() * 5, TERRAIN_CHANGE_FROZEN);
        }
    }

    if (see_destruction)
        mpr("Ice forming cracks and breaks items beneath the surface."); // Not the best solution, but at least it's one that seems logical.

    if (see_preservation)
        mpr("A magical artifact is magically pushed up through the ice!");

    // BCADNOTE: Any vault or Abyss placed ice/obsidian is assumed to be permanent and unaltered by this.
    if ((feat == DNGN_ICE || feat == DNGN_OBSIDIAN) && (flavour == BEAM_COLD || flavour == BEAM_FREEZE))
        mutate_terrain_change_duration(p, damage.roll() * 5, true);

    if (feat == DNGN_ICE && is_fiery())
    {
        if (mutate_terrain_change_duration(p, damage.roll() * -1))
            mpr("The fire melts away some of the ice.");
    }

    // Is there already a cloud here?
    if (cloud_struct* cloud = cloud_at(p))
    {
        // fire cancelling cold & vice versa
        if ((cloud->type == CLOUD_COLD
             && (flavour == BEAM_FIRE || flavour == BEAM_LAVA))
            || (cloud->type == CLOUD_FIRE && flavour == BEAM_COLD))
        {
            if (player_can_hear(p))
                mprf(MSGCH_SOUND, "You hear a sizzling sound!");

            delete_cloud(p);
            extra_range_used += 5;
        }
        return;
    }

    // No clouds here, free to make new ones.

    if (origin_spell == SPELL_POISONOUS_CLOUD)
        place_cloud(CLOUD_POISON, p, (damage.roll() + damage.roll()) / 3, agent());

    if (origin_spell == SPELL_HOLY_BREATH)
        place_cloud(CLOUD_HOLY, p, (damage.roll() + damage.roll()) / 3, agent());

    if (origin_spell == SPELL_FLAMING_CLOUD)
        place_cloud(CLOUD_FIRE, p, (damage.roll() + damage.roll()) / 3, agent());

    if (!feat_is_critical(grd(pos())) && !feat_is_watery(grd(pos())) && (is_explosion && origin_spell == SPELL_SLIME_SHARDS && !one_chance_in(3)
        || flavour == BEAM_ACID_WAVE))
    {
        const int d = 6 + random2(3 + you.skill(SK_INVOCATIONS));
        temp_change_terrain(pos(), DNGN_SLIMY_WATER, d * BASELINE_DELAY, TERRAIN_CHANGE_SLIME);
        if (origin_spell == SPELL_SLIME_RUSH)
            place_cloud(CLOUD_FIRE, p, d - 1, agent());
    }

    // Fire/cold over water/lava
    if (feat == DNGN_LAVA && flavour == BEAM_COLD
        || (feat_is_watery(feat) && (feat != DNGN_SLIMY_WATER) 
           && (feat != DNGN_DEEP_SLIMY_WATER) && is_fiery()))
    {
        place_cloud(CLOUD_STEAM, p, 2 + random2(5), agent(), 11);
    }

    if (is_fiery() && is_snowcovered(p) && x_chance_in_y(damage.roll(), 100))
        env.pgrid(p) &= ~FPROP_SNOW;

    if (feat_is_watery(feat) && feat != DNGN_SLIMY_WATER && feat != DNGN_DEEP_SLIMY_WATER
        && (flavour == BEAM_COLD || flavour == BEAM_FREEZE)
        && damage.max() > 35)
    {
        place_cloud(CLOUD_COLD, p, damage.max() / 30 + 1, agent());
    }

    if (flavour == BEAM_MIASMA)
        place_cloud(CLOUD_MIASMA, p, random2(5) + 2, agent());

    if (flavour == BEAM_ROT)
        place_cloud(CLOUD_ROT, p, random2(5) + 2, agent());

    if (flavour == BEAM_STEAM)
        place_cloud(CLOUD_STEAM, p, random2(5) + 2, agent());

    if (flavour == BEAM_FOG)
        place_cloud(CLOUD_PURPLE_SMOKE, p, damage.roll() + 2, agent(), 2);

    if (flavour == BEAM_BLOOD)
        place_cloud(CLOUD_BLOOD, p, damage.roll() + 2, agent(), 2);

    if (flavour == BEAM_PARADOXICAL)
        place_cloud(grid_distance(coord_def(1, 1), p) % 2 ? CLOUD_COLD : CLOUD_FIRE, p, random2(5) + 2, agent());

    if (origin_spell == SPELL_UNSTABLE_FIERY_DASH)
        place_cloud(flavour == BEAM_LAVA ? CLOUD_FIRE : chaos_cloud(), pos(), 5 + random2(5), agent());

    if (flavour == BEAM_BUTTERFLY && !actor_at(p))
    {
        monster_type butttype = MONS_BUTTERFLY;
        int power = drac_breath_power(true);
        if (origin_spell == SPELL_EMPOWERED_BREATH && x_chance_in_y(power, 90))
            butttype = MONS_SPHINX_MOTH;

        monster * butterfly = create_monster( mgen_data(butttype, BEH_COPY, p, agent()->is_player() ? int{ MHITYOU }
                                : agent()->as_monster()->foe, MG_AUTOFOE).set_summoned(agent(), 2, SPELL_NO_SPELL, GOD_NO_GOD));
        if (butterfly)
        {
            butterfly->move_to_pos(p);
            mon_enchant abj = butterfly->get_ench(ENCH_ABJ);

            if (butttype == MONS_SPHINX_MOTH)
            {
                butterfly->set_hit_dice(3 + div_rand_round(power, 5));
                butterfly->max_hit_points = butterfly->hit_points = butterfly->max_hit_points * butterfly->get_hit_dice() / 10;
            }

            abj.duration = (damage.roll() * BASELINE_DELAY);
            butterfly->update_ench(abj);
        }
    }

    //XXX: these use the name for a gameplay effect.
    if (name == "poison gas")
        place_cloud(CLOUD_POISON, p, random2(4) + 3, agent());

    if (name == "blast of choking fumes")
        place_cloud(CLOUD_MEPHITIC, p, random2(4) + 3, agent());

    if (name == "trail of fire")
        place_cloud(CLOUD_FIRE, p, random2(ench_power) + ench_power, agent());

    if (origin_spell == SPELL_PETRIFYING_CLOUD)
        place_cloud(CLOUD_PETRIFY, p, random2(4) + 4, agent());

    if (origin_spell == SPELL_SPECTRAL_CLOUD)
        place_cloud(CLOUD_SPECTRAL, p, random2(6) + 5, agent());

    if (origin_spell == SPELL_DEATH_RATTLE)
        place_cloud(CLOUD_MIASMA, p, random2(4) + 4, agent());
}

void bolt::affect_place_explosion_clouds()
{
    const coord_def p = pos();

    // First check: fire/cold over water/lava.
    if (grd(p) == DNGN_LAVA && flavour == BEAM_COLD
        || feat_is_watery(grd(p)) && (grd(p) != DNGN_SLIMY_WATER) && (grd(p) != DNGN_DEEP_SLIMY_WATER) && is_fiery())
    {
        place_cloud(CLOUD_STEAM, p, 2 + random2(5), agent());
        return;
    }

    if (is_fiery() && is_snowcovered(p) && x_chance_in_y(damage.roll(), 100))
        env.pgrid(p) &= ~FPROP_SNOW;

    if (feat_is_door(grd(p)) && is_fiery())
    {
        destroy_wall(p);
        place_cloud(CLOUD_FIRE, p, 2 + random2(5), agent());
    }

    if (flavour == BEAM_MEPHITIC || origin_spell == SPELL_MEPHITIC_CLOUD)
    {
        bool chaos = real_flavour == BEAM_CHAOTIC;
        const coord_def center = (aimed_at_feet ? source : ray.pos());
        if (p == center || x_chance_in_y(125 + ench_power, 225))
        {
            place_cloud(chaos ? chaos_cloud() : CLOUD_MEPHITIC, p, roll_dice(2, 3 + ench_power / 20),
                        agent());
        }
    }

    if (origin_spell == SPELL_FIRE_STORM)
    {
        bool chaos = real_flavour == BEAM_CHAOTIC;

        place_cloud(chaos ? chaos_cloud() : CLOUD_FIRE, p, 2 + random2avg(5,2), agent());

        // XXX: affect other open spaces?
        if (grd(p) == DNGN_FLOOR && !monster_at(p) && one_chance_in(4))
        {
            const god_type god =
                (crawl_state.is_god_acting()) ? crawl_state.which_god_acting()
                                              : GOD_NO_GOD;
            const beh_type att =
                (whose_kill() == KC_OTHER ? BEH_HOSTILE : BEH_FRIENDLY);

            actor* summ = agent();
            mgen_data mg(chaos ? MONS_CHAOS_VORTEX : MONS_FIRE_VORTEX, att, p, MHITNOT, MG_NONE, god);
            mg.set_summoned(summ, 1, SPELL_FIRE_STORM);

            // Spell-summoned monsters need to have a live summoner.
            if (summ == nullptr || !summ->alive())
            {
                if (!source_name.empty())
                    mg.non_actor_summoner = source_name;
                else if (god != GOD_NO_GOD)
                    mg.non_actor_summoner = god_name(god);
            }

            mons_place(mg);
        }
    }
}

// A little helper function to handle the calling of ouch()...
void bolt::internal_ouch(int dam)
{
    monster* monst = monster_by_mid(source_id);

    const char *what = aux_source.empty() ? name.c_str() : aux_source.c_str();

    if (YOU_KILL(thrower) && you.duration[DUR_QUAD_DAMAGE])
        dam *= 4;

    // The order of this is important.
    if (monst && monst->type == MONS_PLAYER_SHADOW
        && !monst->mname.empty())
    {
        ouch(dam, KILLED_BY_DIVINE_WRATH, MID_NOBODY,
             aux_source.empty() ? nullptr : aux_source.c_str(), true,
             source_name.empty() ? nullptr : source_name.c_str(), is_fiery());
    }
    else if (monst && (monst->type == MONS_BALLISTOMYCETE_SPORE
                       || monst->type == MONS_BALL_LIGHTNING
                       || monst->type == MONS_ENTROPIC_SPHERE
                       || monst->type == MONS_HYPERACTIVE_BALLISTOMYCETE
                       || monst->type == MONS_FULMINANT_PRISM
                       || monst->type == MONS_BENNU // death flames
                       ))
    {
        ouch(dam, KILLED_BY_SPORE, source_id,
             aux_source.c_str(), true,
             source_name.empty() ? nullptr : source_name.c_str());
    }
    else if (flavour == BEAM_DISINTEGRATION || flavour == BEAM_DEVASTATION
        || flavour == BEAM_ENERGY || flavour == BEAM_ICY_DEVASTATION
        || flavour == BEAM_CHAOTIC_DEVASTATION)
    {
        ouch(dam, KILLED_BY_DISINT, source_id, what, true,
             source_name.empty() ? nullptr : source_name.c_str(), is_fiery());
    }
    else if (YOU_KILL(thrower) && aux_source.empty())
    {
        if (reflections > 0)
            ouch(dam, KILLED_BY_REFLECTION, reflector, name.c_str(), true, nullptr, is_fiery());
        else if (bounces > 0)
            ouch(dam, KILLED_BY_BOUNCE, MID_PLAYER, name.c_str(), true, nullptr, is_fiery());
        else
        {
            if (aimed_at_feet && effect_known)
                ouch(dam, KILLED_BY_SELF_AIMED, MID_PLAYER, name.c_str(), true, nullptr, is_fiery());
            else
                ouch(dam, KILLED_BY_TARGETING, MID_PLAYER, name.c_str(), true, nullptr, is_fiery());
        }
    }
    else if (MON_KILL(thrower) || aux_source == "exploding inner flame")
        ouch(dam, KILLED_BY_BEAM, source_id,
             aux_source.c_str(), true,
             source_name.empty() ? nullptr : source_name.c_str(), is_fiery());
    else // KILL_MISC || (YOU_KILL && aux_source)
        ouch(dam, KILLED_BY_WILD_MAGIC, source_id, aux_source.c_str(), true, nullptr, is_fiery());
}

// [ds] Apply a fuzz if the monster lacks see invisible and is trying to target
// an invisible player. This makes invisibility slightly more powerful.
bool bolt::fuzz_invis_tracer()
{
    // Did the monster have a rough idea of where you are?
    int dist = grid_distance(target, you.pos());

    // No, ditch this.
    if (dist > 2)
        return false;

    // Apply fuzz now.
    coord_def fuzz;
    fuzz.x = random_range(-2, 2);
    fuzz.y = random_range(-2, 2);
    coord_def newtarget = target + fuzz;

    if (in_bounds(newtarget))
        target = newtarget;

    // Fire away!
    return true;
}

// A first step towards to-hit sanity for beams. We're still being
// very kind to the player, but it should be fairer to monsters than
// 4.0.
static bool _test_beam_hit(int attack, int defence, bool pierce,
                           int defl, defer_rand &r)
{
    if (attack == AUTOMATIC_HIT)
        return true;

    if (defl >= 3)
        defl--;

    if (pierce)
    {
        if (defl > 1)
            attack = r[0].random2(attack * 2) / 3;
        else if (defl && attack >= 2) // don't increase acc of 0
            attack = r[0].random_range((attack + 1) / 2 + 1, attack);
    }
    else if (defl)
        attack = r[0].random2(attack / defl);

    dprf(DIAG_BEAM, "Beam attack: %d, defence: %d", attack, defence);

    attack = r[1].random2(attack);
    defence = r[2].random2avg(defence, 2);

    dprf(DIAG_BEAM, "Beam new attack: %d, defence: %d", attack, defence);

    return attack >= defence;
}

bool bolt::is_harmless(const monster* mon) const
{
    // For enchantments, this is already handled in nasty_to().
    if (is_enchantment())
        return !nasty_to(mon);

    // The others are handled here.
    switch (flavour)
    {
    case BEAM_VISUAL:
    case BEAM_DIGGING:
    case BEAM_WAND_HEALING:
    case BEAM_FOG:
    case BEAM_BUTTERFLY: // Not necessarily true; but it's smart and always harmless to allies.
        return true;

    case BEAM_HOLY:
        return mon->res_holy_energy() >= 3;

    case BEAM_STEAM:
        return mon->res_steam() >= 3;

    case BEAM_FIRE:
        return mon->res_fire() >= 3;

    case BEAM_COLD:
        return mon->res_cold() >= 3;

    case BEAM_MIASMA:
        return mon->res_rotting();

    case BEAM_BLOOD:
    case BEAM_NEG:
        return mon->res_negative_energy() == 3;

    case BEAM_ELECTRICITY:
        return mon->res_elec() >= 3;

    case BEAM_POISON:
        return mon->res_poison() >= 3;

    case BEAM_ACID:
        return mon->res_acid() >= 3;

    case BEAM_PETRIFY:
        return mon->stasis() || mon->res_petrify() || mon->petrified();

    case BEAM_MEPHITIC:
        return mon->res_poison() > 0 || mon->is_unbreathing();

    default:
        return false;
    }
}

// N.b. only called for player-originated beams; if that is changed,
// be sure to adjust various assumptions based on the spells/abilities
// available to the player.
bool bolt::harmless_to_player() const
{
    dprf(DIAG_BEAM, "beam flavour: %d", flavour);

    if (you.cloud_immune() && is_big_cloud())
        return true;

    switch (flavour)
    {
    case BEAM_VISUAL:
    case BEAM_DIGGING:
    case BEAM_WAND_HEALING:
    case BEAM_FOG:
        return true;

    // Positive enchantments.
    case BEAM_HASTE:
    case BEAM_HEALING:
    case BEAM_MIGHT:
    case BEAM_AGILITY:
    case BEAM_INVISIBILITY:
    case BEAM_RESISTANCE:
        return true;

    case BEAM_HOLY:
        return you.res_holy_energy() >= 3;

    case BEAM_MIASMA:
        return you.res_rotting();

    case BEAM_BLOOD:
    case BEAM_NEG:
        return player_prot_life(false) >= 3;

    case BEAM_POISON:
        return player_res_poison(false) >= 3
               || is_big_cloud() && player_res_poison(false) > 0;

    case BEAM_MEPHITIC:
        // With clarity, meph still does a tiny amount of damage (1d3 - 1).
        // Normally we'd just ignore it, but we shouldn't let a player
        // kill themselves without a warning.
        return player_res_poison(false) > 0 || you.is_unbreathing()
            || you.clarity(false) && you.hp > 2;

    case BEAM_ELECTRICITY:
        return player_res_electricity(false);

    case BEAM_PETRIFY:
        return you.stasis() || you.petrified();

    case BEAM_COLD:
        return is_big_cloud() && you.has_mutation(MUT_FREEZING_CLOUD_IMMUNITY);

    case BEAM_VIRULENCE:
        return player_res_poison(false) >= 3;

    default:
        return false;
    }
}

bool bolt::is_reflectable(const actor &whom) const
{
    if (range_used() > range)
        return false;

    // Catch players dual-wielding shields.
    if (whom.is_player() && player_omnireflects())
        return is_omnireflectable();

    const item_def *it = whom.shield();
    return (it && is_shield(*it) && shield_reflects(*it)) || whom.reflection();
}

bool bolt::is_big_cloud() const
{
    return testbits(get_spell_flags(origin_spell), spflag::cloud);
}

coord_def bolt::leg_source() const
{
    if (bounces > 0 && map_bounds(bounce_pos))
        return bounce_pos;
    else
        return source;
}

// Reflect a beam back the direction it came. This is used
// by shields of reflection.
void bolt::reflect()
{
    reflections++;

    target = leg_source();
    source = pos();

    // Reset bounce_pos, so that if we somehow reflect again before reaching
    // the wall that we won't keep heading towards the wall.
    bounce_pos.reset();

    if (pos() == you.pos())
    {
        reflector = MID_PLAYER;
        count_action(CACT_BLOCK, -1, BLOCK_REFLECT);
    }
    else if (monster* m = monster_at(pos()))
        reflector = m->mid;
    else
    {
        reflector = MID_NOBODY;
#ifdef DEBUG
        dprf(DIAG_BEAM, "Bolt reflected by neither player nor "
             "monster (bolt = %s, item = %s)", name.c_str(),
             item ? item->name(DESC_PLAIN).c_str() : "none");
#endif
    }

    if (real_flavour == BEAM_CHAOS)
        flavour = real_flavour;

    choose_ray();
}

void bolt::tracer_affect_player()
{
    if (flavour == BEAM_UNRAVELLING && player_is_debuffable())
        is_explosion = true;

    // Check whether thrower can see player, unless thrower == player.
    if (YOU_KILL(thrower))
    {
        if (!dont_stop_player && !harmless_to_player())
        {
            string prompt = make_stringf("That %s is likely to hit you. Continue anyway?",
                                         item ? name.c_str() : "beam");

            if (yesno(prompt.c_str(), false, 'n'))
            {
                friend_info.count++;
                friend_info.power += you.experience_level;
                // Don't ask about aiming at ourself twice.
                dont_stop_player = true;
            }
            else
            {
                canned_msg(MSG_OK);
                beam_cancelled = true;
                finish_beam();
            }
        }
    }
    else if (can_see_invis || !you.invisible() || fuzz_invis_tracer())
    {
        if (mons_att_wont_attack(attitude))
        {
            friend_info.count++;
            friend_info.power += you.experience_level;
        }
        else
        {
            foe_info.count++;
            foe_info.power += you.experience_level;
        }
    }

    extra_range_used += range_used_on_hit();
}

/* Determine whether the beam hit or missed the player, and tell them if it
 * missed.
 *
 * @return  true if the beam missed, false if the beam hit the player.
 */
bool bolt::misses_player()
{
    if (flavour == BEAM_VISUAL)
        return true;

    if (origin_spell == SPELL_SLIME_SHARDS && you.is_icy())
        return true;

    if (is_explosion || aimed_at_feet || auto_hit)
        return false;

    const int dodge = you.evasion();
    int real_tohit  = hit;

    if (real_tohit != AUTOMATIC_HIT)
    {
        // Monsters shooting at an invisible player are very inaccurate.
        if (you.invisible() && !can_see_invis)
            real_tohit /= 2;

        // Backlit is easier to hit:
        if (you.backlit(false))
            real_tohit += 2 + random2(8);

        // Umbra is harder to hit:
        if (!nightvision && you.umbra())
            real_tohit -= 2 + random2(4);
    }

    const int SH = player_shield_class();
    if ((player_omnireflects() && is_omnireflectable()
         || is_blockable())
        && !aimed_at_feet
        && SH > 0)
    {
        // We use the original to-hit here.
        // (so that effects increasing dodge chance don't increase block...?)
        const int testhit = random2(hit * 130 / 100
                                    + you.shield_block_penalty());

        const int block = you.shield_bonus();

        // 50% chance of blocking ench-type effects at 20 displayed sh
        const bool omnireflected
            = hit == AUTOMATIC_HIT
              && x_chance_in_y(SH, omnireflect_chance_denom(SH));

        dprf(DIAG_BEAM, "Beamshield: hit: %d, block %d", testhit, block);
        if ((testhit < block && hit != AUTOMATIC_HIT) || omnireflected)
        {
            const string refl_name = name.empty() &&
                                     origin_spell != SPELL_NO_SPELL ?
                                        mon_spell_title(origin_spell, actor_by_mid(source_id)) :
                                        name;

            const item_def *shield = you.shield();
            lose_staff_shield(flavour, 2);

            if (is_reflectable(you))
            {
                if (shield && shield_reflects(*shield))
                {
                    mprf("Your %s reflects the %s!",
                            shield->name(DESC_PLAIN).c_str(),
                            refl_name.c_str());
                }
                else
                {
                    mprf("The %s reflects off an invisible shield around you!",
                            refl_name.c_str());
                }
                reflect();
            }
            else
            {
                mprf("You block the %s.", name.c_str());
                finish_beam();
            }
            you.shield_block_succeeded(agent());
            return true;
        }

        // Some training just for the "attempt".
        practise_shield_block(false);
    }

    if (is_enchantment())
        return false;

    if (!aimed_at_feet)
        practise_being_shot_at();

    defer_rand r;

    int defl = you.missile_deflection();

    if (!_test_beam_hit(real_tohit, dodge, pierce, 0, r))
    {
        mprf("The %s misses you.", name.c_str());
        count_action(CACT_DODGE, DODGE_EVASION);
    }
    else if (defl && !_test_beam_hit(real_tohit, dodge, pierce, defl, r))
    {
        int healz = 0; 
        
        if (defl >= 3)
        {
            you.heal(healz);
            healz = 4 + random2(8);
        }

        // active voice to imply stronger effect
        mprf(defl == 1 ? "The %s is repelled%s." : 
             defl >= 3 ? "You devour the %s%s"  :
                         "You deflect the %s%s!",
             name.c_str(), defl < 3 ? "" : attack_strength_punctuation(healz).c_str());

        you.ablate_deflection();
        count_action(CACT_DODGE, DODGE_DEFLECT);
    }
    else
        return false;

    return true;
}

void bolt::affect_player_enchantment(bool resistible)
{
    if (resistible
        && has_saving_throw()
        && you.check_res_magic(ench_power) > 0)
    {
        // You resisted it.

        // Give a message.
        bool need_msg = true;
        if (thrower != KILL_YOU_MISSILE)
        {
            const monster* mon = monster_by_mid(source_id);
            if (mon && !mon->observable())
            {
                mprf("Something tries to affect you, but you %s.",
                     you.res_magic() == MAG_IMMUNE ? "are unaffected"
                                                   : "resist");
                need_msg = false;
            }
        }
        if (need_msg)
        {
            if (you.res_magic() == MAG_IMMUNE)
                canned_msg(MSG_YOU_UNAFFECTED);
            else
            {
                // the message reflects the level of difficulty resisting.
                const int margin = you.res_magic() - ench_power;
                mprf("You%s", you.resist_margin_phrase(margin).c_str());
            }
        }
        // You *could* have gotten a free teleportation in the Abyss,
        // but no, you resisted.
        if (flavour == BEAM_TELEPORT && player_in_branch(BRANCH_ABYSS))
            xom_is_stimulated(200);

        extra_range_used += range_used_on_hit();
        return;
    }

    // Never affects the player.
    if (flavour == BEAM_INFESTATION || flavour == BEAM_VILE_CLUTCH)
        return;

    // You didn't resist it.
    if (animate)
        _ench_animation(effect_known ? real_flavour : BEAM_MAGIC);

    bool nasty = true, nice = false;

    const bool blame_player = god_cares() && YOU_KILL(thrower);

    switch (flavour)
    {
    case BEAM_HIBERNATION:
    case BEAM_SLEEP:
        you.put_to_sleep(nullptr, ench_power, flavour == BEAM_HIBERNATION);
        break;

    case BEAM_POLYMORPH:
        obvious_effect = you.polymorph(ench_power);
        break;

    case BEAM_MALMUTATE:
    case BEAM_UNRAVELLED_MAGIC:
        mpr("Strange energies course through your body.");
        you.malmutate(aux_source.empty() ? get_source_name() :
                      (get_source_name() + "/" + aux_source));
        obvious_effect = true;
        break;

    case BEAM_SLOW:
        slow_player(10 + random2(ench_power));
        obvious_effect = true;
        break;

    case BEAM_HASTE:
        haste_player(3 + ench_power + random2(ench_power));
        did_god_conduct(DID_HASTY, 10, blame_player);
        obvious_effect = true;
        nasty = false;
        nice  = true;
        break;

    case BEAM_HEALING:
        potionlike_effect(POT_HEAL_WOUNDS, ench_power, true);
        obvious_effect = true;
        nasty = false;
        nice  = true;
        break;

    case BEAM_MIGHT:
        potionlike_effect(POT_MIGHT, ench_power);
        obvious_effect = true;
        nasty = false;
        nice  = true;
        break;

    case BEAM_INVISIBILITY:
        you.attribute[ATTR_INVIS_UNCANCELLABLE] = 1;
        potionlike_effect(POT_INVISIBILITY, ench_power);
        contaminate_player(1000 + random2(1000), blame_player);
        obvious_effect = true;
        nasty = false;
        nice  = true;
        break;

    case BEAM_PETRIFY:
        you.petrify(agent());
        obvious_effect = true;
        break;

    case BEAM_CONFUSION:
        confuse_player(5 + random2(3));
        obvious_effect = true;
        break;

    case BEAM_TELEPORT:
        you_teleport();

        // An enemy helping you escape while in the Abyss, or an
        // enemy stabilizing a teleport that was about to happen.
        if (!mons_att_wont_attack(attitude) && player_in_branch(BRANCH_ABYSS))
            xom_is_stimulated(200);

        obvious_effect = true;
        break;

    case BEAM_BLINK:
        uncontrolled_blink();
        obvious_effect = true;
        break;

    case BEAM_BLINK_CLOSE:
        blink_other_close(&you, source);
        obvious_effect = true;
        break;

    case BEAM_ENSLAVE:
        mprf(MSGCH_WARN, "Your will is overpowered!");
        confuse_player(5 + random2(3));
        obvious_effect = true;
        break;     // enslavement - confusion?

    case BEAM_BANISH:
        if (YOU_KILL(thrower))
        {
            mpr("This spell isn't strong enough to banish yourself.");
            break;
        }
        you.banish(agent(), get_source_name(),
                   agent()->get_experience_level());
        obvious_effect = true;
        break;

    case BEAM_PAIN:
    {
        if (aux_source.empty())
            aux_source = "by nerve-wracking pain";

        const int dam = resist_adjust_damage(&you, flavour, damage.roll());
        if (dam)
        {
            mprf("Pain shoots through your body%s", attack_strength_punctuation(dam).c_str());
            internal_ouch(dam);
            obvious_effect = true;
        }
        else
            canned_msg(MSG_YOU_UNAFFECTED);
        break;
    }

    case BEAM_AGONY:
        torment_player(agent(), TORMENT_AGONY);
        obvious_effect = true;
        break;

    case BEAM_DISPEL_UNDEAD:
        if (you.undead_state() == US_ALIVE)
        {
            canned_msg(MSG_YOU_UNAFFECTED);
            break;
        }

        mpr("You convulse!");

        if (aux_source.empty())
            aux_source = "by dispel undead";

        internal_ouch(damage.roll());
        obvious_effect = true;
        break;

    case BEAM_DISINTEGRATION:
        mpr("You are blasted!");

        if (aux_source.empty())
            aux_source = "disintegration bolt";

        {
            int amt = damage.roll();
            internal_ouch(amt);

            if (you.can_bleed())
                blood_spray(you.pos(), MONS_PLAYER, amt / 5);
        }

        obvious_effect = true;
        break;

    case BEAM_PORKALATOR:
        if (!transform(ench_power, transformation::pig, true))
        {
            mpr("You feel a momentary urge to oink.");
            break;
        }

        you.transform_uncancellable = true;
        obvious_effect = true;
        break;

    case BEAM_BERSERK:
        you.go_berserk(blame_player);
        obvious_effect = true;
        break;

    case BEAM_SENTINEL_MARK:
        you.sentinel_mark();
        obvious_effect = true;
        break;

    case BEAM_DIMENSION_ANCHOR:
        mprf("You feel %sfirmly anchored in space.",
             you.duration[DUR_DIMENSION_ANCHOR] ? "more " : "");
        you.increase_duration(DUR_DIMENSION_ANCHOR, 12 + random2(15), 50);
        if (you.duration[DUR_TELEPORT])
        {
            you.duration[DUR_TELEPORT] = 0;
            mpr("Your teleport is interrupted.");
        }
        you.redraw_evasion = true;
        obvious_effect = true;
        break;

    case BEAM_VULNERABILITY:
        if (!you.duration[DUR_LOWERED_MR])
        {
            mpr("Your magical defenses are stripped away!");
            you.redraw_resists = true;
        }
        you.increase_duration(DUR_LOWERED_MR, 12 + random2(18), 50);
        obvious_effect = true;
        break;

    case BEAM_CIGOTUVI:
        you.drain_exp(agent());
        you.polymorph(100);
        obvious_effect = true;
        break;

    case BEAM_MALIGN_OFFERING:
    {
        const int dam = resist_adjust_damage(&you, flavour, damage.roll());
        if (dam)
        {
            _malign_offering_effect(&you, agent(), dam);
            obvious_effect = true;
        }
        else
            canned_msg(MSG_YOU_UNAFFECTED);
        break;
    }

    case BEAM_VIRULENCE:
        // Those completely immune cannot be made more susceptible this way
        if (you.res_poison(false) >= 3)
        {
            canned_msg(MSG_YOU_UNAFFECTED);
            break;
        }

        mpr("You feel yourself grow more vulnerable to poison.");
        you.increase_duration(DUR_POISON_VULN, 12 + random2(18), 50);
        obvious_effect = true;
        break;

    case BEAM_AGILITY:
        potionlike_effect(POT_AGILITY, ench_power);
        obvious_effect = true;
        nasty = false;
        nice  = true;
        break;

    case BEAM_SAP_MAGIC:
        if (!SAP_MAGIC_CHANCE())
        {
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        mprf(MSGCH_WARN, "Your magic feels %stainted.",
             you.duration[DUR_SAP_MAGIC] ? "more " : "");
        you.increase_duration(DUR_SAP_MAGIC, random_range(20, 30), 50);
        break;

    case BEAM_DRAIN_MAGIC:
    {
        int amount = random2avg(ench_power / 8, 3);
        if (you.is_fairy())
            amount = div_rand_round(amount, 6);
        amount = min(you.magic_points, amount);
        if (!amount)
            break;
        mprf(MSGCH_WARN, "You feel your power leaking away.");
        dec_mp(amount);
        if (agent() && (agent()->type == MONS_EYE_OF_DRAINING
                        || agent()->type == MONS_GHOST_MOTH))
        {
            agent()->heal(amount);
        }
        obvious_effect = true;
        break;
    }

    case BEAM_TUKIMAS_DANCE:
        cast_tukimas_dance(ench_power, &you);
        obvious_effect = true;
        break;

    case BEAM_RESISTANCE:
        potionlike_effect(POT_RESISTANCE, min(ench_power, 200));
        obvious_effect = true;
        nasty = false;
        nice  = true;
        break;

    case BEAM_UNRAVELLING:
        if (!player_is_debuffable())
            break;

        debuff_player();
        _unravelling_explode(*this);
        obvious_effect = true;
        break;

    default:
        // _All_ enchantments should be enumerated here!
        mpr("Software bugs nibble your toes!");
        break;
    }

    if (nasty)
    {
        if (mons_att_wont_attack(attitude))
        {
            friend_info.hurt++;
            if (source_id == MID_PLAYER)
            {
                // Beam from player rebounded and hit player.
                if (!aimed_at_feet)
                    xom_is_stimulated(200);
            }
            else
            {
                // Beam from an ally or neutral.
                xom_is_stimulated(100);
            }
        }
        else
            foe_info.hurt++;
    }
    else if (nice)
    {
        if (mons_att_wont_attack(attitude))
            friend_info.helped++;
        else
        {
            foe_info.helped++;
            xom_is_stimulated(100);
        }
    }

    // Regardless of effect, we need to know if this is a stopper
    // or not - it seems all of the above are.
    extra_range_used += range_used_on_hit();
}

void bolt::affect_actor(actor *act)
{
    if (act->is_monster())
        affect_monster(act->as_monster());
    else
        affect_player();
}

struct pie_effect
{
    const char* desc;
    function<bool(const actor& def)> valid;
    function<void (actor& def, const bolt &beam)> effect;
    int weight;
};

static const vector<pie_effect> pie_effects = {
    {
        "plum",
        [](const actor &defender) {
            return defender.is_player();
        },
        [](actor &/*defender*/, const bolt &/*beam*/) {
            if (you.duration[DUR_VERTIGO])
                mpr("You feel your light-headedness will last longer.");
            else
                mpr("You feel light-headed.");

            you.increase_duration(DUR_VERTIGO, 10 + random2(11), 50);
        },
        10
    },
    {
        "lemon",
        [](const actor &defender) {
            return defender.is_player() && !you_foodless();
        },
        [](actor &/*defender*/, const bolt &/*beam*/) {
            if (you.duration[DUR_NO_POTIONS])
                mpr("You feel your inability to drink will last longer.");
            else
                mpr("You feel unable to drink.");

            you.increase_duration(DUR_NO_POTIONS, 10 + random2(11), 50);
        },
        10
    },
    {
        "blueberry",
        nullptr,
        [](actor &defender, const bolt &beam) {
            if (defender.is_monster())
            {
                monster *mons = defender.as_monster();
                simple_monster_message(*mons, " loses the ability to speak.");
                mons->add_ench(mon_enchant(ENCH_MUTE, 0, beam.agent(),
                            4 + random2(7) * BASELINE_DELAY));
            }
            else
            {
                if (you.duration[DUR_SILENCE])
                    mpr("You feel your silence will last longer.");
                else
                    mpr("An unnatural silence engulfs you.");

                you.increase_duration(DUR_SILENCE, 4 + random2(7), 10);
                invalidate_agrid(true);

                if (you.beheld())
                    you.update_beholders();
            }
        },
        10
    },
    {
        "raspberry",
        [](const actor &defender) {
            return defender.is_player();
        },
        [](actor &/*defender*/, const bolt &/*beam*/) {
            for (int i = 0; i < NUM_STATS; ++i)
                lose_stat(static_cast<stat_type>(i), 1 + random2(3));
        },
        10
    },
    {
        "cherry",
        [](const actor &defender) {
            return defender.is_player() || defender.res_fire() < 3;
        },
        [](actor &defender, const bolt &beam) {
            if (defender.is_monster())
            {
                monster *mons = defender.as_monster();
                simple_monster_message(*mons,
                        " looks more vulnerable to fire.");
                mons->add_ench(mon_enchant(ENCH_FIRE_VULN, 0,
                             beam.agent(),
                             15 + random2(11) * BASELINE_DELAY));
            }
            else
            {
                if (you.duration[DUR_FIRE_VULN])
                {
                    mpr("You feel your vulnerability to fire will last "
                        "longer.");
                }
                else
                    mpr("Cherry-coloured flames burn away your fire "
                        "resistance!");

                you.increase_duration(DUR_FIRE_VULN, 15 + random2(11), 50);
            }
        },
        6
    },
    {
        "moon pie",
        [](const actor &defender) {
            return defender.can_polymorph();
        },
        [](actor &defender, const bolt &/*beam*/) {
            defender.polymorph(100, false);
        },
        4
    },
};

static pie_effect _random_pie_effect(const actor &defender)
{
    vector<pair<const pie_effect&, int>> weights;
    for (const pie_effect &effect : pie_effects)
        if (!effect.valid || effect.valid(defender))
            weights.push_back({effect, effect.weight});

    ASSERT(!weights.empty());

    return *random_choose_weighted(weights);
}

// Mount toggle is doing something completely different here but it still helps in calls from attack.
void impale_player_with_barbs(bool mt)
{
    if (mt)
    {
        mprf("The barbed spikes become lodged in your %s.", you.mount_name(true).c_str());
        if (!you.duration[DUR_MOUNT_BARBS])
            you.set_duration(DUR_MOUNT_BARBS, random_range(4, 8));
        else
            you.increase_duration(DUR_MOUNT_BARBS, random_range(2, 4), 12);
    }
    else
    {
        if (you.get_mutation_level(MUT_INSUBSTANTIAL) == 1)
            mpr("The barbed spikes sting slightly as they fall through your immaterial body.");
        else if (you.get_mutation_level(MUT_SLIME) >= 3 || you.get_mutation_level(MUT_OOZOMORPH))
            mpr("The barbed spikes fail to stick to your viscuous form.");
        else
        {
            mpr("The barbed spikes become lodged in your body.");
            if (!you.duration[DUR_BARBS])
                you.set_duration(DUR_BARBS, random_range(4, 8));
            else
                you.increase_duration(DUR_BARBS, random_range(2, 4), 12);

            if (you.attribute[ATTR_BARBS_POW])
            {
                you.attribute[ATTR_BARBS_POW] =
                    min(6, you.attribute[ATTR_BARBS_POW]++);
            }
            else
                you.attribute[ATTR_BARBS_POW] = 4;
        }
    }
}

void impale_monster_with_barbs(monster* mon, actor* agent, string what)
{
    if (mon->is_insubstantial() || mons_genus(mon->type) == MONS_JELLY)
        return;
    mprf("The %s become lodged in %s.", what.c_str(), mon->name(DESC_THE).c_str());
    mon->add_ench(mon_enchant(ENCH_BARBS, 1, agent,
        random_range(5, 7) * BASELINE_DELAY));
}

void bolt::affect_player()
{
    hit_count[MID_PLAYER]++;

    // Explosions only have an effect during their explosion phase.
    // Special cases can be handled here.
    if (is_explosion && !in_explosion_phase)
    {
        // Trigger the explosion.
        finish_beam();
        return;
    }

    // Digging -- don't care.
    if (flavour == BEAM_DIGGING)
        return;

    if (is_tracer)
    {
        tracer_affect_player();
        return;
    }

    // Trigger an interrupt, so travel will stop on misses which
    // generate smoke.
    if (!YOU_KILL(thrower))
    {
        if (agent() && agent()->is_monster())
        {
            interrupt_activity(activity_interrupt::monster_attacks,
                agent()->as_monster());
        }
        else
            interrupt_activity(activity_interrupt::monster_attacks);
    }

    if (flavour == BEAM_MISSILE && item)
    {
        ranged_attack attk(agent(true), &you, item, use_target_as_pos, agent());
        attk.set_path(*this);
        attk.attack();
        // fsim purposes - throw_it detects if an attack connected through
        // hit_verb
        if (attk.ev_margin >= 0 && hit_verb.empty())
            hit_verb = attk.attack_verb;
        if (attk.reflected)
            reflect();
        extra_range_used += attk.range_used;
        return;
    }

    if (misses_player())
        return;

    bool hits_mount = mount_hit() || you.mounted() && in_explosion_phase;
    bool hits_you = !hits_mount || in_explosion_phase;

    if (hits_you)
    {
        if (real_flavour == BEAM_CHAOTIC)
        {
            int dur = damage.roll();
            dur += damage.size;
            chaotic_status(&you, dur, agent());
        }

        if (real_flavour == BEAM_CHAOTIC_DEVASTATION)
            chaotic_status(&you, roll_dice(5, 20), agent());
    }

    const bool engulfs = is_explosion || is_big_cloud();

    if (is_enchantment())
    {
        if (real_flavour == BEAM_CHAOS || real_flavour == BEAM_RANDOM)
        {
            if (hit_verb.empty())
                hit_verb = engulfs ? "engulfs" : "hits";
            mprf("The %s %s you!", name.c_str(), hit_verb.c_str());
        }

        affect_player_enchantment();
        return;
    }

    msg_generated = true;

    // FIXME: Lots of duplicated code here (compare handling of
    // monsters)
    int yu_pre_ac_dam = 0;
    int mt_pre_ac_dam = 0;
    int max_dam = damage.max();

    // Roll the damage.
    if (hits_you && !(origin_spell == SPELL_FLASH_FREEZE && you.duration[DUR_FROZEN]))
        yu_pre_ac_dam += (damage.roll() + damage.roll() + damage.roll());
    if (hits_mount && !(origin_spell == SPELL_FLASH_FREEZE && you.duration[DUR_MOUNT_FROZEN]))
        mt_pre_ac_dam += (damage.roll() + damage.roll() + damage.roll());

    yu_pre_ac_dam /= 3;
    mt_pre_ac_dam /= 3;

    int yu_pre_res_dam = apply_AC(&you, yu_pre_ac_dam, max_dam);
    int mt_pre_res_dam = hits_mount ? apply_AC(&you, mt_pre_ac_dam, max_dam, true) : 0;

#ifdef DEBUG_DIAGNOSTICS
    dprf(DIAG_BEAM, "Player damage: before AC=%d; after AC=%d",
        pre_ac_dam, pre_res_dam);
#endif

    practise_being_shot();

    bool was_affected = false;
    int  old_hp = you.hp;
    int  old_mt_hp = you.mount_hp;

    yu_pre_res_dam = max(0, yu_pre_res_dam);
    mt_pre_res_dam = max(0, mt_pre_res_dam);

    // If the beam is an actual missile or of the MMISSILE type (Earth magic)
    // we might bleed on the floor.
    if (!engulfs
        && (flavour == BEAM_MISSILE || flavour == BEAM_MMISSILE))
    {
        // assumes DVORP_PIERCING, factor: 0.5
        int blood = min(you.hp, yu_pre_res_dam / 2);
        bleed_onto_floor(you.pos(), MONS_PLAYER, blood, true);
        if (hits_mount)
        {
            blood = min(you.mount_hp, mt_pre_res_dam / 2);
            bleed_onto_floor(you.pos(), mount_mons(), blood, true);
        }
    }

    if (origin_spell == SPELL_BECKONING && you.alive())
        beckon(source, you, *this, damage.size, *agent());

    // Apply resistances to damage, but don't print "You resist" messages yet
    int yu_final_dam = check_your_resists(yu_pre_res_dam, flavour, "", this, false);
    int mt_final_dam = hits_mount ? check_your_resists(mt_pre_res_dam, flavour, "", this, false, true) : 0;

    if (you.is_icy() && name == "icy shards")
        yu_final_dam = 0;

    // Tell the player the beam hit
    if (hit_verb.empty())
        hit_verb = engulfs ? "engulfs" : "hits";

    bool harmless = (flavour == BEAM_MAGIC_CANDLE || flavour == BEAM_WAND_HEALING
        || flavour == BEAM_FOG);

    hit_something = true;

    if (hits_you && flavour != BEAM_VISUAL && !is_enchantment())
    {
        mprf("The %s %s you%s%s", name.c_str(), hit_verb.c_str(),
            (yu_final_dam || harmless) ? "" : " but does no damage",
            harmless ? "." : attack_strength_punctuation(yu_final_dam).c_str());
    }

    if (hits_mount && flavour != BEAM_VISUAL && !is_enchantment())
    {
        mprf("The %s %s your %s%s%s", name.c_str(), hit_verb.c_str(),
            you.mount_name(true).c_str(),
            (mt_final_dam || harmless) ? "" : " but does no damage",
            harmless ? "." : attack_strength_punctuation(mt_final_dam).c_str());
    }

    // Now print the messages associated with checking resistances, so that
    // these come after the beam actually hitting.
    // Note that this must be called with the pre-resistance damage, so that
    // poison effects etc work properly.
    if (hits_you)
    {
        if (you.is_icy() && name == "icy shards")
            mprf("You are unaffected (0).");
        else
            check_your_resists(yu_pre_res_dam, flavour, "", this, true);
    }

    if (hits_mount)
        check_your_resists(mt_pre_res_dam, flavour, "", this, true, true);

    if (flavour == BEAM_MIASMA)
    {
        if (yu_final_dam > 0)
            was_affected |= miasma_player(agent(), name);
        if (mt_final_dam > 0)
            was_affected |= miasma_mount();
    }

    if (flavour == BEAM_ROT)
    {
        if (yu_final_dam > 0)
        {
            bool success = false;

            mprf(MSGCH_WARN, "You feel yourself rotting from the inside.");

            if (miasma_player(agent(), "vicious blight"))
                success = true;
            if (!success)
            {
                if (poison_player(5 + roll_dice(3, 8), agent() ? agent()->name(DESC_A) : "", "vicious blight", true))
                    success = true;
            }
            if (!success || one_chance_in(4))
                you.drain_stat(STAT_RANDOM, 2 + random2(3));
        }
        if (mt_final_dam > 0)
        {
            bool success = false;

            mprf(MSGCH_WARN, "Your mount seems to rot from the inside.");

            if (miasma_mount())
                success = true;
            if (!success)
            {
                if (poison_mount(5 + roll_dice(3, 8), true))
                    success = true;
            }
            if (!success || one_chance_in(4))
                you.corrode_equipment("vicious blight", 1, true);
        }
    }

    if (flavour == BEAM_DEVASTATION || flavour == BEAM_ENERGY
        || flavour == BEAM_ICY_DEVASTATION || real_flavour == BEAM_CHAOTIC_DEVASTATION) // DISINTEGRATION already handled
    {
        blood_spray(you.pos(), MONS_PLAYER, yu_final_dam / 5);
        if (hits_mount)
            blood_spray(you.pos(), mount_mons(), mt_final_dam / 5);
    }

    // Confusion effect for spore explosions
    if (flavour == BEAM_SPORE && yu_final_dam
        && !(you.holiness() & MH_UNDEAD)
        && !you.is_unbreathing())
    {
        confuse_player(2 + random2(3));
    }

    if (flavour == BEAM_SPORE && mt_final_dam
        && !(you.holiness(true) & MH_UNDEAD)
        && !you.is_unbreathing(true))
    {
        mprf("Your %s chokes on the spores.", you.mount_name(true).c_str());
        you.increase_duration(DUR_MOUNT_BREATH, 3 + random2(4), 20);
    }

    if (flavour == BEAM_UNRAVELLED_MAGIC && hits_you)
        affect_player_enchantment();

    // handling of missiles
    if (item && item->base_type == OBJ_MISSILES)
    {
        if (item->sub_type == MI_THROWING_NET)
        {
            if (player_caught_in_net())
            {
                if (monster_by_mid(source_id))
                    xom_is_stimulated(50);
                was_affected = true;
            }
        }
        else if (item->brand == SPMSL_CURARE)
        {
            if (hits_you && x_chance_in_y(90 - 3 * you.armour_class(), 100))
            {
                curare_actor(agent(), (actor*)&you, 2, name, source_name);
                was_affected = true;
            }
            else if (hits_mount && x_chance_in_y(90 - 3 * mount_ac(), 100))
            {
                curare_actor(agent(), (actor*)&you, 2, name, source_name, true);
                was_affected = true;
            }
        }
    }

    // Sticky flame.
    if (origin_spell == SPELL_STICKY_FLAME
        || origin_spell == SPELL_STICKY_FLAME_RANGE)
    {
        if (!player_res_sticky_flame())
        {
            napalm_player(random2avg(7, 3) + 1, get_source_name(), aux_source);
            was_affected = true;
        }
    }

    // need to trigger qaz resists after reducing damage from ac/resists.
    //    for some reason, strength 2 is the standard. This leads to qaz's
    //    resists triggering 2 in 5 times at max piety.
    //    perhaps this should scale with damage?
    // what to do for hybrid damage?  E.g. bolt of magma, icicle, poison arrow?
    // Right now just ignore the physical component.
    // what about acid?
    you.expose_to_element(flavour, 2, false);

    // Manticore spikes
    if (origin_spell == SPELL_THROW_BARBS)
    {
        if (yu_final_dam > 0)
            impale_player_with_barbs();
        if (mt_final_dam > 0)
            impale_player_with_barbs(true);
    }

    if (origin_spell == SPELL_QUICKSILVER_BOLT && hits_you)
        debuff_player();

    if (origin_spell == SPELL_THROW_PIE && yu_final_dam > 0)
    {
        const pie_effect effect = _random_pie_effect(you);
        mprf("%s!", effect.desc);
        effect.effect(you, *this);
    }

    dprf(DIAG_BEAM, "Damage: %d", final_dam);

    if (yu_final_dam > 0 || old_hp < you.hp || was_affected
        || mt_final_dam > 0 || old_mt_hp < you.mount_hp)
    {
        if (mons_att_wont_attack(attitude))
        {
            friend_info.hurt++;

            // Beam from player rebounded and hit player.
            // Xom's amusement at the player's being damaged is handled
            // elsewhere.
            if (source_id == MID_PLAYER)
            {
                if (!aimed_at_feet)
                    xom_is_stimulated(200);
            }
            else if (was_affected)
                xom_is_stimulated(100);
        }
        else
            foe_info.hurt++;
    }

    internal_ouch(yu_final_dam);
    if (hits_mount)
        damage_mount(mt_final_dam);

    // Acid. (Apply this afterward, to avoid bad message ordering.)
    if (flavour == BEAM_ACID || flavour == BEAM_ACID_WAVE)
    {
        you.splash_with_acid(agent(), div_round_up(yu_final_dam, 10), true);
        if (hits_mount)
            you.splash_with_acid(agent(), div_round_up(mt_final_dam, 10), true, nullptr, true);
    }

    extra_range_used += range_used_on_hit();

    if (hits_mount)
    {
        knockback_actor(&you, mt_final_dam);
        pull_actor(&you, mt_final_dam);
    }
    else if (!you.mounted())
    {
        knockback_actor(&you, yu_final_dam);
        pull_actor(&you, yu_final_dam);
    }

    if (origin_spell == SPELL_FLASH_FREEZE
        || name == "blast of ice"
        || origin_spell == SPELL_GLACIATE && !is_explosion)
    {
        if (hits_you)
        {
            if (you.duration[DUR_FROZEN])
            {
                if (origin_spell == SPELL_FLASH_FREEZE)
                    canned_msg(MSG_YOU_UNAFFECTED);
            }
            else
            {
                mprf(MSGCH_WARN, "You are encased in ice.");
                you.duration[DUR_FROZEN] = (2 + random2(3)) * BASELINE_DELAY;
            }
        }
        else if (hits_mount && you.mounted()) // Glaciate may have killed mount
        {
            if (you.duration[DUR_MOUNT_FROZEN])
            {
                if (origin_spell == SPELL_FLASH_FREEZE)
                    mprf("Your %s is unaffected.", you.mount_name(true).c_str());
            }
            else
            {
                mprf(MSGCH_WARN, "Your %s is encased in ice.", you.mount_name(true).c_str());
                you.duration[DUR_MOUNT_FROZEN] = (2 + random2(3)) * BASELINE_DELAY;
            }
        }
    }
    else if (hits_you && origin_spell == SPELL_BLINDING_SPRAY
             && !(you.holiness() & (MH_UNDEAD | MH_NONLIVING | MH_PLANT)))
    {
        if (x_chance_in_y(85 - you.experience_level * 3 , 100))
            you.confuse(agent(), 5 + random2(3));
    }
    else if (origin_spell == SPELL_CHILLING_BREATH)
    {
        if (yu_final_dam)
            you.slow_down(agent(), max(random2(10), yu_final_dam / 3));
        if (mt_final_dam)
            slow_mount(max(random2(10), mt_final_dam / 3));
    }
}

int bolt::apply_AC(const actor *victim, int hurted, int max_dmg, bool mount)
{
    switch (flavour)
    {
    case BEAM_DAMNATION:
    case BEAM_ENSNARE:
        ac_rule = ac_type::none; break;
    case BEAM_ELECTRICITY:
        ac_rule = ac_type::half; break;
    case BEAM_FRAG:
    case BEAM_SILVER_FRAG: // fallthrough
        ac_rule = ac_type::triple; break;
    default: ;
    }

    return victim->apply_ac(hurted, max_dmg, ac_rule, 0, !is_tracer, mount);
}

void bolt::update_hurt_or_helped(monster* mon)
{
    if (!mons_atts_aligned(attitude, mons_attitude(*mon)))
    {
        if (!is_harmless(mon))
            foe_info.hurt++;
        else if (nice_to(monster_info(mon)))
        {
            foe_info.helped++;
            // Accidentally helped a foe.
            if (!is_tracer && !effect_known && mons_is_threatening(*mon))
            {
                const int interest =
                    (flavour == BEAM_INVISIBILITY && can_see_invis) ? 25 : 100;
                xom_is_stimulated(interest);
            }
        }
    }
    else
    {
        if (!is_harmless(mon))
        {
            friend_info.hurt++;

            // Harmful beam from this monster rebounded and hit the monster.
            if (!is_tracer && mon->mid == source_id)
                xom_is_stimulated(100);
        }
        else if (nice_to(monster_info(mon)))
            friend_info.helped++;
    }
}

void bolt::tracer_enchantment_affect_monster(monster* mon)
{
    // Only count tracers as hitting creatures they could potentially affect
    if (ench_flavour_affects_monster(flavour, mon, true)
        && !(has_saving_throw() && mons_immune_magic(*mon)))
    {
        // Update friend or foe encountered.
        if (!mons_atts_aligned(attitude, mons_attitude(*mon)))
        {
            foe_info.count++;
            foe_info.power += mon->get_experience_level();
        }
        else
        {
            friend_info.count++;
            friend_info.power += mon->get_experience_level();
        }
    }

    handle_stop_attack_prompt(mon);
    if (!beam_cancelled)
        extra_range_used += range_used_on_hit();
}

// Return false if we should skip handling this monster.
bool bolt::determine_damage(monster* mon, int& preac, int& postac, int& final)
{
    preac = postac = final = 0;

    const bool freeze_immune =
        origin_spell == SPELL_FLASH_FREEZE && mon->has_ench(ENCH_FROZEN);

    // [ds] Changed how tracers determined damage: the old tracer
    // model took the average damage potential, subtracted the average
    // AC damage reduction and called that the average damage output.
    // This could easily predict an average damage output of 0 for
    // high AC monsters, with the result that monsters often didn't
    // bother using ranged attacks at high AC targets.
    //
    // The new model rounds up average damage at every stage, so it
    // will predict a damage output of 1 even if the average damage
    // expected is much closer to 0. This will allow monsters to use
    // ranged attacks vs high AC targets.
      // [1KB] What ds' code actually does is taking the max damage minus
      // average AC. This does work well, even using no AC would. An
      // attack that _usually_ does no damage but can possibly do some means
      // we'll ultimately get it through. And monsters with weak ranged
      // almost always would do no better in melee.
    //
    // This is not an entirely beneficial change; the old tracer
    // damage system would make monsters with weak ranged attacks
    // close in to their foes, while the new system will make it more
    // likely that such monsters will hang back and make ineffective
    // ranged attacks. Thus the new tracer damage calculation will
    // hurt monsters with low-damage ranged attacks and high-damage
    // melee attacks. I judge this an acceptable compromise (for now).
    //
    const int preac_max_damage =
        (freeze_immune) ? 0
                        : damage.max();

    // preac: damage before AC modifier
    // postac: damage after AC modifier
    // final: damage after AC and resists
    // All these are invalid if we return false.

    if (is_tracer)
    {
        // Was mean between min and max;
        preac = preac_max_damage;
    }
    else if (!freeze_immune)
    {
        preac = damage.roll() + damage.roll() + damage.roll();
        preac /= 3;
    }

    if (name == "icy shards" && mon->is_icy())
        return preac = 0;

    int tracer_postac_max = preac_max_damage;

    postac = apply_AC(mon, preac, preac_max_damage);

    if (is_tracer)
    {
        postac = div_round_up(tracer_postac_max, 2);

        const int adjusted_postac_max =
            mons_adjust_flavoured(mon, *this, tracer_postac_max, false);

        final = div_round_up(adjusted_postac_max, 2);
    }
    else
    {
        postac = max(0, postac);
        // Don't do side effects (beam might miss or be a tracer).
        final = mons_adjust_flavoured(mon, *this, postac, false);
    }

    // Sanity check. Importantly for
    // tracer_nonenchantment_affect_monster, final > 0
    // implies preac > 0.
    ASSERT(0 <= postac);
    ASSERT(postac <= preac);
    ASSERT(0 <= final);
    ASSERT(preac > 0 || final == 0);

    return true;
}

void bolt::handle_stop_attack_prompt(monster* mon)
{
    if (thrower != KILL_YOU_MISSILE && thrower != KILL_YOU
        || is_harmless(mon)
        || friend_info.dont_stop && foe_info.dont_stop)
    {
        return;
    }

    bool prompted = false;

    if (stop_attack_prompt(mon, true, target, &prompted)
        || _stop_because_god_hates_target_prompt(mon, origin_spell))
    {
        beam_cancelled = true;
        finish_beam();
    }
    // Handle enslaving monsters when OTR is up: give a prompt for attempting
    // to enslave monsters that don't have rPois with Toxic status.
    else if (flavour == BEAM_ENSLAVE && you.duration[DUR_TOXIC_RADIANCE]
             && mon->res_poison() <= 0)
    {
        string verb = make_stringf("enslave %s", mon->name(DESC_THE).c_str());
        if (otr_stop_summoning_prompt(verb))
        {
            beam_cancelled = true;
            finish_beam();
            prompted = true;
        }
    }

    if (prompted)
    {
        friend_info.dont_stop = true;
        foe_info.dont_stop = true;
    }
}

void bolt::tracer_nonenchantment_affect_monster(monster* mon)
{
    // Dash only counts new targets to prevent being OP by being a lot of B.Magma at single target.
    if (origin_spell == SPELL_UNSTABLE_FIERY_DASH
        && mon->props.exists(DASH_KEY)
        && mon->props[DASH_KEY].get_bool())
    {
        return;
    }

    int preac, post, final;

    if (!determine_damage(mon, preac, post, final))
        return;

    // Check only if actual damage and the monster is worth caring about.
    if (final > 0 && mons_is_threatening(*mon))
    {
        ASSERT(preac > 0);

        // Monster could be hurt somewhat, but only apply the
        // monster's power based on how badly it is affected.
        // For example, if a fire giant (power 16) threw a
        // fireball at another fire giant, and it only took
        // 1/3 damage, then power of 5 would be applied.

        // Counting foes is only important for monster tracers.
        if (!mons_atts_aligned(attitude, mons_attitude(*mon)))
        {
            foe_info.power += 2 * final * mon->get_experience_level() / preac;
            foe_info.count++;
        }
        else
        {
            // Discourage summoned monsters firing on their summoner.
            const monster* mon_source = agent()->as_monster();
            if (mon_source && mon_source->summoner == mon->mid)
                friend_info.power = 100;
            else
            {
                friend_info.power
                    += 2 * final * mon->get_experience_level() / preac;
            }
            friend_info.count++;
        }
    }

    // Maybe the user wants to cancel at this point.
    handle_stop_attack_prompt(mon);
    if (beam_cancelled)
        return;

    // Either way, we could hit this monster, so update range used.
    extra_range_used += range_used_on_hit();
}

void bolt::tracer_affect_monster(monster* mon)
{
    // Ignore unseen monsters.
    if (!agent() || !agent()->can_see(*mon))
        return;

    if (flavour == BEAM_UNRAVELLING && monster_is_debuffable(*mon))
        is_explosion = true;

    // Trigger explosion on exploding beams.
    if (is_explosion && !in_explosion_phase)
    {
        finish_beam();
        return;
    }

    // Special explosions (current exploding missiles) aren't
    // auto-hit, so we need to explode them at every possible
    // end-point?
    if (special_explosion)
    {
        bolt orig = *special_explosion;
        affect_endpoint();
        *special_explosion = orig;
    }

    if (is_enchantment())
        tracer_enchantment_affect_monster(mon);
    else
        tracer_nonenchantment_affect_monster(mon);

    _maybe_imb_explosion(this, pos());
}

void bolt::enchantment_affect_monster(monster* mon)
{
    god_conduct_trigger conducts[3];

    bool hit_woke_orc = false;

    // Nasty enchantments will annoy the monster, and are considered
    // naughty (even if a monster might resist).
    if (nasty_to(mon))
    {
        if (YOU_KILL(thrower))
        {
            set_attack_conducts(conducts, *mon, you.can_see(*mon));

            if (have_passive(passive_t::convert_orcs)
                && mons_genus(mon->type) == MONS_ORC
                && mon->asleep() && you.see_cell(mon->pos()))
            {
                hit_woke_orc = true;
            }
        }
        behaviour_event(mon, ME_ANNOY, agent());
    }
    else if (flavour != BEAM_HIBERNATION || !mon->asleep())
        behaviour_event(mon, ME_ALERT, agent());

    // Doing this here so that the player gets to see monsters
    // "flicker and vanish" when turning invisible....
    if (animate)
    {
        _ench_animation(effect_known ? real_flavour
                                     : BEAM_MAGIC,
                        mon, effect_known);
    }

    // Try to hit the monster with the enchantment. The behaviour_event above
    // may have caused a pacified monster to leave the level, so only try to
    // enchant it if it's still here. If the monster did leave the level, set
    // obvious_effect so we don't get "Nothing appears to happen".
    int res_margin = 0;
    const mon_resist_type ench_result = mon->alive()
                                      ? try_enchant_monster(mon, res_margin)
                                      : (obvious_effect = true, MON_OTHER);

    if (mon->alive())           // Aftereffects.
    {
        // Message or record the success/failure.
        switch (ench_result)
        {
        case MON_RESIST:
            if (simple_monster_message(*mon,
                                   mon->resist_margin_phrase(res_margin).c_str()))
            {
                msg_generated = true;
            }
            break;
        case MON_UNAFFECTED:
            if (simple_monster_message(*mon, " is unaffected."))
                msg_generated = true;
            break;
        case MON_AFFECTED:
        case MON_OTHER:         // Should this really be here?
            update_hurt_or_helped(mon);
            break;
        }

        if (hit_woke_orc)
            beogh_follower_convert(mon, true);
    }

    extra_range_used += range_used_on_hit();
}

static bool _dazzle_monster(monster* mons, actor* act)
{
    if (!mons_can_be_dazzled(mons->type))
        return false;

    if (x_chance_in_y(19 - mons->get_hit_dice(), 20))
    {
        simple_monster_message(*mons, " gets blinded by venom in their eyes.");
        mons->add_ench(mon_enchant(ENCH_BLIND, 1, act,
                       random_range(4, 8) * BASELINE_DELAY));
        return true;
    }

    return false;
}

static monster_type _chaos_pillar()
{
    return random_choose_weighted(
        4, MONS_JELLY,
        4, MONS_PULSATING_LUMP,
        3, MONS_CHAOS_ELEMENTAL,
        2, MONS_CRAWLING_CORPSE,
        5, MONS_DEMONIC_PLANT,
        1, MONS_GOLDEN_EYE,
        1, MONS_INSUBSTANTIAL_WISP,
        1, MONS_CHAOS_VORTEX,
        1, MONS_SPATIAL_MAELSTROM,
        1, MONS_SKY_BEAST,
        1, MONS_FETID_CYST,
        1, MONS_STARCURSED_MASS);
}

static void _glaciate_freeze(monster* mon, killer_type englaciator,
                             int kindex, bool chaos)
{
    const coord_def where = mon->pos();
    const monster_type pillar_type =
        mons_is_zombified(*mon) ? mons_zombie_base(*mon)
                                : mons_species(mon->type);
    const int hd = mon->get_experience_level();

    if (!chaos)
        simple_monster_message(*mon, " is frozen into a solid block of ice!");
    else
        mprf("The very fabric of %s comes apart.", mon->name(DESC_THE).c_str());

    if (chaos && one_chance_in(3))
    {
        mon->flags |= MF_EXPLODE_KILL;
        if (place_monster_corpse(*mon, false))
            return;
    }

    // If the monster leaves a corpse when it dies, destroy the corpse.
    item_def* corpse = monster_die(*mon, englaciator, kindex);
    if (corpse)
        destroy_item(corpse->index(), true);

    if (monster *pillar = create_monster(
                        mgen_data(chaos ? _chaos_pillar() : MONS_BLOCK_OF_ICE,
                                  BEH_HOSTILE,
                                  where,
                                  MHITNOT,
                                  MG_FORCE_PLACE).set_base(pillar_type),
                                  false))
    {
        // Enemies with more HD leave longer-lasting blocks of ice.
        int time_left = (random2(8) + hd) * BASELINE_DELAY;
        mon_enchant temp_en(ENCH_SLOWLY_DYING, 1, 0, time_left);
        if (pillar->has_ench(ENCH_SLOWLY_DYING))
            pillar->update_ench(temp_en);
        else
        {
            temp_en.duration *= 3;
            pillar->add_ench(temp_en);
        }
        if (chaos)
        {
            if (!pillar->is_stationary())
            {
                pillar->behaviour = BEH_NEUTRAL;
                pillar->add_ench(mon_enchant(ENCH_CONFUSION, 1, 0, INFINITE_DURATION));
            }
            pillar->flags |= MF_CLOUD_IMMUNE;
            pillar->flags |= MF_EXPLODE_KILL;
        }
    }
}

void bolt::monster_post_hit(monster* mon, int dmg)
{
    // Don't annoy anyone with a harmless mist.
    if (flavour == BEAM_WAND_HEALING || flavour == BEAM_FOG)
        return;

    // Suppress the message for scattershot.
    if (YOU_KILL(thrower) && you.see_cell(mon->pos())
        && name != "burst of metal fragments")
    {
        print_wounds(*mon);
    }

    // Don't annoy friendlies or good neutrals if the player's beam
    // did no damage. Hostiles will still take umbrage.
    if (dmg > 0 || !mon->wont_attack() || !YOU_KILL(thrower))
    {
        bool was_asleep = mon->asleep();
        special_missile_type m_brand = SPMSL_FORBID_BRAND;
        if (item && item->base_type == OBJ_MISSILES)
            m_brand = get_ammo_brand(*item);

        if (origin_spell == SPELL_BECKONING && mon->alive())
        {
            actor &ma = *mon;
            beckon(source, ma, *this, damage.size, *agent());
        }

        if (item && item->base_type == OBJ_MISSILES
            && item->sub_type == MI_SLING_BULLET
            && !effect_known && mon->wont_attack())
        {
            return; // Don't annoy friendlies with ricochets.
        }

        // Don't immediately turn insane monsters hostile.
        if (m_brand != SPMSL_FRENZY)
        {
            behaviour_event(mon, ME_ANNOY, agent());
            // behaviour_event can make a monster leave the level or vanish.
            if (!mon->alive())
                return;
        }

        // Don't allow needles of sleeping to awaken monsters.
        if (m_brand == SPMSL_SLEEP && was_asleep && !mon->asleep())
            mon->put_to_sleep(agent(), 0);
    }

    if (YOU_KILL(thrower) && !mon->wont_attack() && !mons_is_firewood(*mon))
        you.pet_target = mon->mindex();

    // Sticky flame.
    if (origin_spell == SPELL_STICKY_FLAME
        || origin_spell == SPELL_STICKY_FLAME_RANGE)
    {
        const int levels = min(4, 1 + random2(dmg) / 2);
        napalm_monster(mon, agent(), levels);
    }

    // Acid splash from yellow draconians / acid dragons
    if (origin_spell == SPELL_ACID_SPLASH
        || (origin_spell == SPELL_EMPOWERED_BREATH && flavour == BEAM_ACID))
    {
        mon->splash_with_acid(agent(), 3);

        for (adjacent_iterator ai(target); ai; ++ai)
        {
            if (*ai == source)
                continue;
            if (origin_spell == SPELL_EMPOWERED_BREATH && !cell_is_solid(*ai)
                && x_chance_in_y(3 + apply_invo_enhancer(you.skill(SK_INVOCATIONS), false), 45))
            {
                place_cloud(CLOUD_ACID, *ai, 5 + random2(5), &you, 1);
            }
            // the acid can splash onto adjacent targets
            if (grid_distance(*ai, target) != 1)
                continue;
            if (actor *victim = actor_at(*ai))
            {
                if (you.see_cell(*ai))
                {
                    mprf("The acid splashes onto %s!",
                         victim->name(DESC_THE).c_str());
                }

                victim->splash_with_acid(agent(), 3);
            }
        }
    }

    // Handle missile effects.
    if (item && item->base_type == OBJ_MISSILES
        && item->brand == SPMSL_CURARE && ench_power == AUTOMATIC_HIT)
    {
        curare_actor(agent(), mon, 2, name, source_name);
    }

    // purple draconian breath
    if (origin_spell == SPELL_QUICKSILVER_BOLT)
        debuff_monster(*mon);

    if (dmg)
        beogh_follower_convert(mon, true);

    knockback_actor(mon, dmg);

    if (origin_spell == SPELL_BLINDING_SPRAY)
        _dazzle_monster(mon, agent());
    else if (origin_spell == SPELL_FLASH_FREEZE
             || name == "blast of ice"
             || origin_spell == SPELL_GLACIATE && !is_explosion)
    {
        if (origin_spell == SPELL_GLACIATE && real_flavour != BEAM_FREEZE)
            chaotic_debuff(mon, 30, agent());
        else if (mon->has_ench(ENCH_FROZEN))
        {
            if (origin_spell == SPELL_FLASH_FREEZE)
                simple_monster_message(*mon, " is unaffected.");
        }
        else
        {
            simple_monster_message(*mon, " is flash-frozen.");
            mon->add_ench(ENCH_FROZEN);
        }
    }

    if (origin_spell == SPELL_CHILLING_BREATH && dmg > 0)
        do_slow_monster(*mon, agent(), max(random2(10), dmg / 3));

    // Apply chaos effects.
    if (mon->alive() && (real_flavour == BEAM_CHAOTIC || real_flavour == BEAM_CHAOTIC_DEVASTATION) && !mons_class_is_firewood(mon->type))
    {
        int dur = damage.roll();
        dur += damage.size;

        chaotic_status(mon, dur, agent());
    }

    if (origin_spell == SPELL_EMPOWERED_BREATH)
    {
        if (flavour == BEAM_COLD && dmg > 0)
        {
            do_slow_monster(*mon, agent(), max(random2(10), dmg / 3));

            if (!mon->has_ench(ENCH_FROZEN) && x_chance_in_y(agent()->skill(SK_INVOCATIONS), mon->get_hit_dice() * 2))
            {
                simple_monster_message(*mon, " is flash-frozen.");
                mon->add_ench(ENCH_FROZEN);
            }
        }
        if (flavour == BEAM_MMISSILE && you.drac_colour != DR_BROWN)
        {
            if (monster_is_debuffable(*mon))
            {
                debuff_monster(*mon);
                mon->malmutate("unraveling magic");
            }

            if (mon->res_magic() != MAG_IMMUNE)
            {
                if (!mon->has_ench(ENCH_LOWERED_MR))
                    mprf("%s magical defenses are stripped away!",
                        mon->name(DESC_ITS).c_str());

                mon_enchant lowered_mr(ENCH_LOWERED_MR, 1, agent(),
                    (20 + random2(20)) * BASELINE_DELAY);
                mon->add_ench(lowered_mr);
            }
        }
        if (flavour == BEAM_IRRADIATE)
        {
            switch (random2(3))
            {
            case 0:
                if (mon->check_res_magic(drac_breath_power(true) * 3))
                {
                    if (_cigotuvi(mon, &you))
                        break;
                }   // else fallthrough
            case 1:
                mon->drain_exp(&you);
                break;
            case 2:
                mon->weaken(&you, drac_breath_power(true));
                break;
            }
        }
    }

    if (origin_spell == SPELL_THROW_BARBS && dmg > 0)
        impale_monster_with_barbs(mon, agent());

    if (origin_spell == SPELL_THROW_PIE && dmg > 0)
    {
        const pie_effect effect = _random_pie_effect(*mon);
        if (you.see_cell(mon->pos()))
            mprf("%s!", effect.desc);
        effect.effect(*mon, *this);
    }
}

void bolt::knockback_actor(actor *act, int dam)
{
    if (!act || !can_knockback(*act, dam))
        return;

    const int distance =
        (origin_spell == SPELL_FORCE_LANCE)
            ? 2 + div_rand_round(ench_power, 30) :
        (origin_spell == SPELL_MUSE_OAMS_AIR_BLAST)
            ? 1 + div_rand_round(ench_power, 50) : 1;

    const int roll = origin_spell == SPELL_FORCE_LANCE
                     ? 7 + 0.5 * ench_power
                     : 17;
    const int weight = max_corpse_chunks(act->is_monster() ? act->type :
                                   player_species_to_mons_species(you.species));

    // Can't knockback self (Should never happen anyways).
    if (agent() == act)
        return;

    const coord_def oldpos = act->pos();

    if (source == target && agent())
    {
        if (!find_ray(agent()->pos(), act->pos(), ray, opc_fullyopaque))
            return;

        int infinite_loop_protection = 0;

        while (ray.pos() != oldpos)
        {
            infinite_loop_protection++;
            ray.advance();

            if (infinite_loop_protection > 15)
                return;
        }
    }

    if (act->is_stationary())
        return;
    // Tornado moved it or distortion blinked it away on the same turn it was hit.
    if (ray.pos() != oldpos)
        return;

    coord_def newpos = oldpos;
    for (int dist_travelled = 0; dist_travelled < distance; ++dist_travelled)
    {
        if (x_chance_in_y(weight, roll))
            continue;

        const ray_def oldray(ray);

        ray.advance();

        newpos = ray.pos();
        if (newpos == oldray.pos()
            || cell_is_solid(newpos)
            || actor_at(newpos)
            || !act->can_pass_through(newpos))
        {
            ray = oldray;
            break;
        }

        act->move_to_pos(newpos);
        if (act->is_player())
            stop_delay(true);
    }

    if (newpos == oldpos)
        return;

    if (you.can_see(*act))
    {
        mprf("%s %s knocked back by the %s.",
                act->name(DESC_THE).c_str(),
                act->conj_verb("are").c_str(),
                name.c_str());
    }

    act->props[KNOCKBACK_KEY] = (int)agent()->mid;

    if (act->pos() != newpos)
        act->collide(newpos, agent(), ench_power);

    // Stun the monster briefly so that it doesn't look as though it wasn't
    // knocked back at all
    if (act->is_monster())
        act->as_monster()->speed_increment -= random2(6) + 4;

    act->apply_location_effects(oldpos, killer(),
                                actor_to_death_source(agent()));
}

void bolt::pull_actor(actor *act, int dam)
{
    if (!act || !can_pull(*act, dam))
        return;

    // How far we'll try to pull the actor to make them adjacent to the source.
    const int distance = (act->pos() - source).rdist() - 1;
    ASSERT(distance > 0);

    const coord_def oldpos = act->pos();
    ASSERT(ray.pos() == oldpos);

    coord_def newpos = oldpos;
    for (int dist_travelled = 0; dist_travelled < distance; ++dist_travelled)
    {
        const ray_def oldray(ray);

        ray.regress();

        newpos = ray.pos();
        if (newpos == oldray.pos()
            || cell_is_solid(newpos)
            || actor_at(newpos)
            || !act->can_pass_through(newpos)
            || !act->is_habitable(newpos))
        {
            ray = oldray;
            break;
        }

        act->move_to_pos(newpos);
        if (act->is_player())
            stop_delay(true);
    }

    if (newpos == oldpos)
        return;

    if (you.can_see(*act))
    {
        mprf("%s %s yanked forward by the %s.", act->name(DESC_THE).c_str(),
             act->conj_verb("are").c_str(), name.c_str());
    }

    act->props[PULLED_KEY] = (int)agent()->mid;

    if (act->pos() != newpos)
        act->collide(newpos, agent(), ench_power);

    act->apply_location_effects(oldpos, killer(),
                                actor_to_death_source(agent()));
}

// Return true if the player's god will be unforgiving about the effects
// of this beam.
bool bolt::god_cares() const
{
    return effect_known || effect_wanton;
}

// Return true if the block succeeded (including reflections.)
bool bolt::attempt_block(monster* mon)
{
    const int shield_block = mon->shield_bonus();
    if (shield_block <= 0)
        return false;

    const int sh_hit = random2(hit * 130 / 100 + mon->shield_block_penalty());
    if (sh_hit >= shield_block)
        return false;

    item_def *shield = mon->mslot_item(MSLOT_SHIELD);
    if (is_reflectable(*mon))
    {
        if (mon->observable())
        {
            if (shield && is_shield(*shield) && shield_reflects(*shield))
            {
                mprf("%s reflects the %s off %s %s!",
                     mon->name(DESC_THE).c_str(),
                     name.c_str(),
                     mon->pronoun(PRONOUN_POSSESSIVE).c_str(),
                     shield->name(DESC_PLAIN).c_str());
                ident_reflector(shield);
            }
            else
            {
                mprf("The %s reflects off an invisible shield around %s!",
                     name.c_str(),
                     mon->name(DESC_THE).c_str());

                item_def *amulet = mon->mslot_item(MSLOT_JEWELLERY);
                if (amulet)
                    ident_reflector(amulet);
            }
        }
        else if (you.see_cell(pos()))
            mprf("The %s bounces off of thin air!", name.c_str());

        reflect();
    }
    else if (you.see_cell(pos()))
    {
        mprf("%s blocks the %s.",
             mon->name(DESC_THE).c_str(), name.c_str());
        finish_beam();
    }

    mon_lose_staff_shield(*mon, flavour, 2);
    mon->shield_block_succeeded(agent());
    return true;
}

/// Is the given monster a bush or bush-like 'monster', and can the given beam
/// travel through it without harm?
bool bolt::bush_immune(const monster &mons) const
{
    return
        (mons_species(mons.type) == MONS_BUSH || mons.type == MONS_BRIAR_PATCH)
        && !pierce && !is_explosion
        && !is_enchantment()
        && target != mons.pos()
        && origin_spell != SPELL_STICKY_FLAME
        && origin_spell != SPELL_STICKY_FLAME_RANGE
        && origin_spell != SPELL_CHAIN_LIGHTNING;
}

void bolt::affect_monster(monster* mon)
{
    // Don't hit dead monsters.
    if (!mon->alive() || mon->type == MONS_PLAYER_SHADOW)
        return;

    hit_count[mon->mid]++;

    if (shoot_through_monster(*this, mon) && !is_tracer)
    {
        // FIXME: Could use a better message, something about
        // dodging that doesn't sound excessively weird would be
        // nice.
        if (you.see_cell(mon->pos()))
        {
            if (testbits(mon->flags, MF_DEMONIC_GUARDIAN))
                mpr("Your demonic guardian avoids your attack.");
            else if (mons_is_hepliaklqana_ancestor(mon->type))
                mpr("Your ancestor avoids your attack.");
            else if (mons_enslaved_soul(*mon))
                mprf("%s avoids your attack.", mon->name(DESC_YOUR).c_str());
            else if (mons_is_avatar(mon->type))
                mprf("Your attack phases harmlessly through %s.", mon->name(DESC_YOUR).c_str());
            else if (!bush_immune(*mon))
            {
                simple_god_message(
                    make_stringf(" protects %s plant from harm.",
                        attitude == ATT_FRIENDLY ? "your" : "a").c_str(),
                    GOD_FEDHAS);
            }
        }
    }

    if (flavour == BEAM_WATER && mon->type == MONS_WATER_ELEMENTAL && !is_tracer)
    {
        if (you.see_cell(mon->pos()))
            mprf("The %s passes through %s.", name.c_str(), mon->name(DESC_THE).c_str());
    }

    if (ignores_monster(mon))
        return;

    // Handle tracers separately.
    if (is_tracer)
    {
        tracer_affect_monster(mon);
        return;
    }

    // Visual - wake monsters.
    if (flavour == BEAM_VISUAL)
    {
        behaviour_event(mon, ME_DISTURB, agent(), source);
        return;
    }

    if (origin_spell == SPELL_UNSTABLE_FIERY_DASH)
        mon->props[DASH_KEY] = true;

    if (flavour == BEAM_MISSILE && item)
    {
        ranged_attack attk(agent(true), mon, item, use_target_as_pos, agent());
        if (source_name == "a ricochet")
            attk.ricochet();
        attk.set_path(*this);
        attk.attack();
        // fsim purposes - throw_it detects if an attack connected through
        // hit_verb
        if (attk.ev_margin >= 0 && hit_verb.empty())
            hit_verb = attk.attack_verb;
        if (attk.reflected)
            reflect();
        extra_range_used += attk.range_used;
        return;
    }

    // Explosions always 'hit'.
    const bool engulfs = (is_explosion || is_big_cloud());

    if (is_enchantment())
    {
        if (real_flavour == BEAM_CHAOS || real_flavour == BEAM_RANDOM)
        {
            if (hit_verb.empty())
                hit_verb = engulfs ? "engulfs" : "hits";
            if (you.see_cell(mon->pos()))
            {
                mprf("The %s %s %s.", name.c_str(), hit_verb.c_str(),
                     mon->name(DESC_THE).c_str());
            }
            else if (heard && !hit_noise_msg.empty())
                mprf(MSGCH_SOUND, "%s", hit_noise_msg.c_str());
        }
        // no to-hit check
        enchantment_affect_monster(mon);
        return;
    }

    if (is_explosion && !in_explosion_phase)
    {
        // It hit a monster, so the beam should terminate.
        // Don't actually affect the monster; the explosion
        // will take care of that.
        finish_beam();
        return;
    }

    // We need to know how much the monster _would_ be hurt by this,
    // before we decide if it actually hits.
    int preac, postac, final;
    if (!determine_damage(mon, preac, postac, final))
        return;

#ifdef DEBUG_DIAGNOSTICS
    dprf(DIAG_BEAM, "Monster: %s; Damage: pre-AC: %d; post-AC: %d; post-resist: %d",
         mon->name(DESC_PLAIN).c_str(), preac, postac, final);
#endif

    // Player beams which hit friendlies or good neutrals will annoy
    // them and be considered naughty if they do damage (this is so as
    // not to penalise players that fling fireballs into a melee with
    // fire elementals on their side - the elementals won't give a sh*t,
    // after all).

    god_conduct_trigger conducts[3];

    if (nasty_to(mon))
    {
        if (YOU_KILL(thrower) && final > 0)
            set_attack_conducts(conducts, *mon, you.can_see(*mon));
    }

    if (engulfs && flavour == BEAM_SPORE // XXX: engulfs is redundant?
        && mon->holiness() & MH_NATURAL
        && !mon->is_unbreathing())
    {
        apply_enchantment_to_monster(mon);
    }

    if (flavour == BEAM_UNRAVELLED_MAGIC)
    {
        int unused; // res_margin
        try_enchant_monster(mon, unused);
    }

    // Make a copy of the to-hit before we modify it.
    int beam_hit = hit;

    if (beam_hit != AUTOMATIC_HIT)
    {
        if (mon->invisible() && !can_see_invis)
            beam_hit /= 2;

        // Backlit is easier to hit:
        if (mon->backlit(false))
            beam_hit += 2 + random2(8);

        // Umbra is harder to hit:
        if (!nightvision && mon->umbra())
            beam_hit -= 2 + random2(4);
    }

    // The monster may block the beam.
    if (!engulfs && is_blockable() && attempt_block(mon))
        return;

    defer_rand r;
    int rand_ev = random2(mon->evasion());
    int defl = mon->missile_deflection();

    // FIXME: We're randomising mon->evasion, which is further
    // randomised inside test_beam_hit. This is so we stay close to the
    // 4.0 to-hit system (which had very little love for monsters).
    if (!engulfs && !_test_beam_hit(beam_hit, rand_ev, pierce, defl, r))
    {
        const bool deflected = _test_beam_hit(beam_hit, rand_ev, pierce, 0, r);
        // If the PLAYER cannot see the monster, don't tell them anything!
        if (mon->observable() && name != "burst of metal fragments")
        {
            // if it would have hit otherwise...
            if (_test_beam_hit(beam_hit, rand_ev, pierce, 0, r))
            {
                string deflects = (defl == 2) ? "deflects" : "repels";
                msg::stream << mon->name(DESC_THE) << " "
                            << deflects << " the " << name
                            << '!' << endl;
            }
            else
            {
                msg::stream << "The " << name << " misses "
                            << mon->name(DESC_THE) << '.' << endl;
            }
        }
        if (deflected)
            mon->ablate_deflection();
        return;
    }

    update_hurt_or_helped(mon);
    hit_something = true;

    // We'll say ballistomycete spore explosions don't trigger the ally attack
    // conduct for Fedhas worshipers. Mostly because you can accidentally blow
    // up a group of 8 plants and get placed under penance until the end of
    // time otherwise. I'd prefer to do this elsewhere but the beam information
    // goes out of scope.
    //
    // Also exempting miscast explosions from this conduct -cao
    if (you_worship(GOD_FEDHAS)
        && (flavour == BEAM_SPORE
            || source_id == MID_PLAYER
               && aux_source.find("your miscasting") != string::npos))
    {
        conducts[0].set();
    }

    if (!is_explosion && !noise_generated)
    {
        heard = noisy(loudness, pos(), source_id) || heard;
        noise_generated = true;
    }

    if (!mon->alive())
        return;

    // The beam hit.
    if (you.see_cell(mon->pos()))
    {
        // Monsters are never currently helpless in ranged combat.
        if (hit_verb.empty())
            hit_verb = engulfs ? "engulfs" : "hits";

        bool harmless = (flavour == BEAM_MAGIC_CANDLE || flavour == BEAM_WAND_HEALING
                      || flavour == BEAM_FOG);

        // If the beam did no damage because of resistances,
        // mons_adjust_flavoured below will print "%s completely resists", so
        // no need to also say "does no damage" here.
        mprf("The %s %s %s%s%s",
             name.c_str(),
             hit_verb.c_str(),
             mon->name(DESC_THE).c_str(),
             (postac || harmless) ? "" : " but does no damage",
             harmless ? "." : attack_strength_punctuation(final).c_str());

        if (origin_spell == SPELL_SLIME_SHARDS && one_chance_in(3))
            mon->splash_with_acid(&you, 1, true, "corroded by icy fragments");
    }
    else if (heard && !hit_noise_msg.empty())
        mprf(MSGCH_SOUND, "%s", hit_noise_msg.c_str());
    // The player might hear something, if _they_ fired a missile
    // (not magic beam).
    else if (!silenced(you.pos()) && flavour == BEAM_MISSILE
             && YOU_KILL(thrower))
    {
        mprf(MSGCH_SOUND, "The %s hits something.", name.c_str());
    }

    // Apply flavoured specials.
    mons_adjust_flavoured(mon, *this, postac, true);

    // mons_adjust_flavoured may kill the monster directly.
    if (mon->alive())
    {
        // If the beam is an actual missile or of the MMISSILE type
        // (Earth magic) we might bleed on the floor.
        if (!engulfs
            && (flavour == BEAM_MISSILE || flavour == BEAM_MMISSILE)
            && !mon->is_summoned())
        {
            // Using raw_damage instead of the flavoured one!
            // assumes DVORP_PIERCING, factor: 0.5
            const int blood = min(postac/2, mon->hit_points);
            bleed_onto_floor(mon->pos(), mon->type, blood, true);
        }
        // Now hurt monster.
        if (real_flavour == BEAM_CHAOTIC_DEVASTATION)
            mon->hurt(agent(), final, real_flavour, KILLED_BY_BEAM, "", "", false);
        else
            mon->hurt(agent(), final, flavour, KILLED_BY_BEAM, "", "", false);
    }

    if (mon->alive())
        monster_post_hit(mon, final);
    // The monster (e.g. a spectral weapon) might have self-destructed in its
    // behaviour_event called from mon->hurt() above. If that happened, it
    // will have been cleaned up already (and is therefore invalid now).
    else if (!invalid_monster(mon))
    {
        // Preserve name of the source monster if it winds up killing
        // itself.
        if (mon->mid == source_id && source_name.empty())
            source_name = mon->name(DESC_A, true);

        int kindex = actor_to_death_source(agent());
        if (origin_spell == SPELL_GLACIATE
            && !mon->is_insubstantial()
            && x_chance_in_y(3, 5))
        {
            // Includes monster_die as part of converting to block of ice.
            _glaciate_freeze(mon, thrower, kindex, (real_flavour != BEAM_FREEZE));
        }
        // Prevent spore explosions killing plants from being registered
        // as a Fedhas misconduct. Deaths can trigger the ally dying or
        // plant dying conducts, but spore explosions shouldn't count
        // for either of those.
        //
        // FIXME: Should be a better way of doing this. For now, we are
        // just falsifying the death report... -cao
        else if (you_worship(GOD_FEDHAS) && flavour == BEAM_SPORE
            && fedhas_protects(mon))
        {
            if (mon->attitude == ATT_FRIENDLY)
                mon->attitude = ATT_HOSTILE;
            monster_die(*mon, KILL_MON, kindex);
        }
        else
        {
            killer_type ref_killer = thrower;
            if (!YOU_KILL(thrower) && reflector == MID_PLAYER)
            {
                ref_killer = KILL_YOU_MISSILE;
                kindex = YOU_FAULTLESS;
            }
            if (real_flavour == BEAM_CHAOTIC_DEVASTATION)
                mon->flags |= MF_EXPLODE_KILL;
            monster_die(*mon, ref_killer, kindex);
        }
    }

    extra_range_used += range_used_on_hit();
}

bool bolt::ignores_monster(const monster* mon) const
{
    // Digging doesn't affect monsters (should it harm earth elementals?).
    if (flavour == BEAM_DIGGING)
        return true;

    // The targeters might call us with nullptr in the event of a remembered
    // monster that is no longer there. Treat it as opaque.
    if (!mon)
        return false;

    // All kinds of beams go past orbs of destruction and friendly
    // battlespheres.
    if ((mons_is_projectile(*mon) && !(mon->type == MONS_BOULDER_BEETLE))
        || (mons_is_avatar(mon->type) && mons_aligned(agent(), mon)))
    {
        return true;
    }

    // Missiles go past bushes and briar patches, unless aimed directly at them
    if (bush_immune(*mon))
        return true;

    if (shoot_through_monster(*this, mon))
        return true;

    // Fire storm creates these, so we'll avoid affecting them.
    if (origin_spell == SPELL_FIRE_STORM && mon->type == MONS_FIRE_VORTEX)
        return true;

    // Don't blow up blocks of ice with the spell that creates them.
    if (origin_spell == SPELL_GLACIATE && mon->type == MONS_BLOCK_OF_ICE)
        return true;

    if (flavour == BEAM_WATER && mon->type == MONS_WATER_ELEMENTAL)
        return true;

    return false;
}

bool bolt::has_saving_throw() const
{
    if (aimed_at_feet)
        return false;

    switch (flavour)
    {
    case BEAM_HASTE:
    case BEAM_MIGHT:
    case BEAM_BERSERK:
    case BEAM_HEALING:
    case BEAM_INVISIBILITY:
    case BEAM_DISPEL_UNDEAD:
    case BEAM_BLINK_CLOSE:
    case BEAM_BLINK:
    case BEAM_MALIGN_OFFERING:
    case BEAM_AGILITY:
    case BEAM_RESISTANCE:
    case BEAM_MALMUTATE:
    case BEAM_SAP_MAGIC:
    case BEAM_UNRAVELLING:
    case BEAM_UNRAVELLED_MAGIC:
    case BEAM_INFESTATION:
    case BEAM_IRRESISTIBLE_CONFUSION:
    case BEAM_VILE_CLUTCH:
    case BEAM_AGONY:
        return false;
    case BEAM_VULNERABILITY:
        return !one_chance_in(3);  // Ignores MR 1/3 of the time
    case BEAM_PETRIFY:             // Giant eyeball petrification is irresistible
        return !(agent() && agent()->type == MONS_FLOATING_EYE);
    default:
        return true;
    }
}

bool ench_flavour_affects_monster(beam_type flavour, const monster* mon,
                                  bool intrinsic_only)
{
    bool rc = true;
    switch (flavour)
    {
    case BEAM_MALMUTATE:
    case BEAM_UNRAVELLED_MAGIC:
        rc = mon->can_mutate();
        break;

    case BEAM_SLOW:
    case BEAM_HASTE:
    case BEAM_PETRIFY:
        rc = !mon->stasis();
        break;

    case BEAM_POLYMORPH:
        rc = mon->can_polymorph();
        break;

    case BEAM_DISPEL_UNDEAD:
        rc = bool(mon->holiness() & MH_UNDEAD);
        break;

    case BEAM_PAIN:
        rc = mon->res_negative_energy(intrinsic_only) < 3;
        break;

    case BEAM_AGONY:
        rc = !mon->res_torment();
        break;

    case BEAM_HIBERNATION:
        rc = mon->can_hibernate(false, intrinsic_only);
        break;

    case BEAM_PORKALATOR:
        rc = (mon->holiness() & MH_DEMONIC && mon->type != MONS_HELL_HOG)
              || (mon->holiness() & MH_NATURAL && mon->type != MONS_HOG)
              || (mon->holiness() & MH_HOLY && mon->type != MONS_HOLY_SWINE);
        break;

    case BEAM_SENTINEL_MARK:
        rc = false;
        break;

    case BEAM_MALIGN_OFFERING:
        rc = (mon->res_negative_energy(intrinsic_only) < 3);
        break;

    case BEAM_VIRULENCE:
        rc = (mon->res_poison() < 3);
        break;

    case BEAM_DRAIN_MAGIC:
        rc = mon->antimagic_susceptible();
        break;

    case BEAM_ENTROPIC_BURST:
    case BEAM_INNER_FLAME:
        rc = !(mon->is_summoned() && !mon->is_illusion() || mon->has_ench(ENCH_INNER_FLAME) 
                                  || mon->has_ench(ENCH_ENTROPIC_BURST));
        break;

    case BEAM_INFESTATION:
        rc = mons_gives_xp(*mon, you) && !mon->has_ench(ENCH_INFESTATION);
        break;

    case BEAM_VILE_CLUTCH:
        rc = !mons_aligned(&you, mon) && you.can_constrict(mon, false);
        break;

    default:
        break;
    }

    return rc;
}

bool enchant_actor_with_flavour(actor* victim, const actor *foe,
                                beam_type flavour, int powc)
{
    bolt dummy;
    dummy.flavour = flavour;
    dummy.ench_power = powc;
    dummy.set_agent(foe);
    dummy.animate = false;
    if (victim->is_player())
        dummy.affect_player_enchantment(false);
    else
        dummy.apply_enchantment_to_monster(victim->as_monster());
    return dummy.obvious_effect;
}

bool enchant_monster_invisible(monster* mon, const string &how)
{
    // Store the monster name before it becomes an "it". - bwr
    const string monster_name = mon->name(DESC_THE);
    const bool could_see = you.can_see(*mon);

    if (mon->has_ench(ENCH_INVIS) || !mon->add_ench(ENCH_INVIS))
        return false;

    if (could_see)
    {
        const bool is_visible = mon->visible_to(&you);

        // Can't use simple_monster_message(*) here, since it checks
        // for visibility of the monster (and it's now invisible).
        // - bwr
        mprf("%s %s%s",
             monster_name.c_str(),
             how.c_str(),
             is_visible ? " for a moment."
                        : "!");

        if (!is_visible && !mons_is_safe(mon))
            autotoggle_autopickup(true);
    }

    return true;
}

mon_resist_type bolt::try_enchant_monster(monster* mon, int &res_margin)
{
    // Early out if the enchantment is meaningless.
    if (!ench_flavour_affects_monster(flavour, mon))
        return MON_UNAFFECTED;

    // Check magic resistance.
    if (has_saving_throw())
    {
        if (mons_immune_magic(*mon))
            return MON_UNAFFECTED;

        // (Very) ugly things and shapeshifters will never resist
        // polymorph beams.
        if (flavour == BEAM_POLYMORPH
            && (mon->type == MONS_UGLY_THING
                || mon->type == MONS_VERY_UGLY_THING
                || mon->is_shapeshifter()))
        {
            ;
        }
        // Chaos effects don't get a resistance check to match melee chaos.
        else if (real_flavour != BEAM_CHAOS)
        {
            if (mon->check_res_magic(ench_power) > 0)
            {
                // Note only actually used by messages in this case.
                res_margin = mon->res_magic() - ench_power_stepdown(ench_power);
                return MON_RESIST;
            }
        }
    }

    return apply_enchantment_to_monster(mon);
}

static bool _cigotuvi(monster * mon, actor * agent)
{
    if (!mon->has_ench(ENCH_CIGOTUVI)
        && mon->add_ench(mon_enchant(ENCH_CIGOTUVI, 0, agent, (3 + random2(8)) * BASELINE_DELAY)))
    {
        if (you.can_see(*mon))
        {
            mprf("You infect %s with foul degeneration!", mon->name(DESC_THE).c_str());
            return true;
        }
    }
    return false;
}

mon_resist_type bolt::apply_enchantment_to_monster(monster* mon)
{
    // Gigantic-switches-R-Us
    switch (flavour)
    {
    case BEAM_TELEPORT:
        if (mon->no_tele())
            return MON_UNAFFECTED;
        if (mon->observable())
            obvious_effect = true;
        monster_teleport(mon, false);
        return MON_AFFECTED;

    case BEAM_BLINK:
        if (mon->no_tele())
            return MON_UNAFFECTED;
        if (mon->observable())
            obvious_effect = true;
        monster_blink(mon);
        return MON_AFFECTED;

    case BEAM_BLINK_CLOSE:
        if (mon->no_tele())
            return MON_UNAFFECTED;
        if (mon->observable())
            obvious_effect = true;
        blink_other_close(mon, source);
        return MON_AFFECTED;

    case BEAM_POLYMORPH:
        if (mon->polymorph(ench_power))
            obvious_effect = true;
        if (YOU_KILL(thrower))
        {
            const int level = 2 + random2(3);
            did_god_conduct(DID_DELIBERATE_MUTATING, level, god_cares());
        }
        return MON_AFFECTED;

    case BEAM_MALMUTATE:
    case BEAM_UNRAVELLED_MAGIC:
        if (mon->malmutate("")) // exact source doesn't matter
            obvious_effect = true;
        if (YOU_KILL(thrower))
        {
            const int level = 2 + random2(3);
            did_god_conduct(DID_DELIBERATE_MUTATING, level, god_cares());
        }
        return MON_AFFECTED;

    case BEAM_BANISH:
        mon->banish(agent());
        obvious_effect = true;
        return MON_AFFECTED;

    case BEAM_DISPEL_UNDEAD:
    {
        const int dam = damage.roll();
        if (you.see_cell(mon->pos()))
        {
            mprf("%s is dispelled%s",
                 mon->name(DESC_THE).c_str(),
                 attack_strength_punctuation(dam).c_str());
            obvious_effect = true;
        }
        mon->hurt(agent(), dam);
        return MON_AFFECTED;
    }

    case BEAM_PAIN:
    {
        const int dam = resist_adjust_damage(mon, flavour, damage.roll());
        if (dam)
        {
            if (you.see_cell(mon->pos()))
            {
                mprf("%s writhes in agony%s",
                     mon->name(DESC_THE).c_str(),
                     attack_strength_punctuation(dam).c_str());
                obvious_effect = true;
            }
            mon->hurt(agent(), dam, flavour);
            return MON_AFFECTED;
        }
        return MON_UNAFFECTED;
    }

    case BEAM_AGONY:
        torment_cell(mon->pos(), agent(), TORMENT_AGONY);
        obvious_effect = true;
        return MON_AFFECTED;

    case BEAM_DISINTEGRATION:   // disrupt/disintegrate
    {
        const int dam = damage.roll();
        if (you.see_cell(mon->pos()))
        {
            mprf("%s is blasted%s",
                 mon->name(DESC_THE).c_str(),
                 attack_strength_punctuation(dam).c_str());
            obvious_effect = true;
        }
        mon->hurt(agent(), dam, flavour);
        return MON_AFFECTED;
    }

    case BEAM_HIBERNATION:
        if (mon->can_hibernate())
        {
            if (simple_monster_message(*mon, " looks drowsy..."))
                obvious_effect = true;
            mon->put_to_sleep(agent(), ench_power, true);
            return MON_AFFECTED;
        }
        return MON_UNAFFECTED;

    case BEAM_SLOW:
        obvious_effect = do_slow_monster(*mon, agent(),
                                         ench_power * BASELINE_DELAY);
        return MON_AFFECTED;

    case BEAM_HASTE:
    {
        if (YOU_KILL(thrower))
            did_god_conduct(DID_HASTY, 6, god_cares());

        if (mon->stasis())
            return MON_AFFECTED;

        const int dur = (3 + ench_power + random2(ench_power)) * BASELINE_DELAY;

        if (!mon->has_ench(ENCH_HASTE)
            && !mon->is_stationary())
        {
            mon->add_ench(mon_enchant(ENCH_HASTE, 0, agent(), dur));
            if (!mons_is_immotile(*mon) && simple_monster_message(*mon, " seems to speed up.")) // Who put this in an if block? It's weird.
                obvious_effect = true;
        }
        return MON_AFFECTED;
    }

    case BEAM_MIGHT:
        if (!mon->has_ench(ENCH_MIGHT)
            && !mon->is_stationary()
            && mon->add_ench(ENCH_MIGHT))
        {
            if (simple_monster_message(*mon, " seems to grow stronger."))
                obvious_effect = true;
        }
        return MON_AFFECTED;

    case BEAM_BERSERK:
        if (!mon->berserk_or_insane())
        {
            // currently from potion, hence voluntary
            mon->go_berserk(true);
            // can't return this from go_berserk, unfortunately
            obvious_effect = you.can_see(*mon);
        }
        return MON_AFFECTED;

    case BEAM_HEALING:
        // No KILL_YOU_CONF, or we get "You heal ..."
        if (thrower == KILL_YOU || thrower == KILL_YOU_MISSILE)
        {
            const int pow = min(50, 3 + damage.roll());
            const int amount = pow + roll_dice(2, pow) - 2;
            if (heal_monster(*mon, amount))
                obvious_effect = true;
            msg_generated = true; // to avoid duplicate "nothing happens"
        }
        else if (mon->heal(3 + damage.roll()))
        {
            if (mon->hit_points == mon->max_hit_points)
            {
                if (simple_monster_message(*mon, "'s wounds heal themselves!"))
                    obvious_effect = true;
            }
            else if (simple_monster_message(*mon, " is healed somewhat."))
                obvious_effect = true;
        }
        return MON_AFFECTED;

    case BEAM_PETRIFY:
        if (mon->stasis()) 
            return MON_UNAFFECTED;

        apply_bolt_petrify(mon);
        return MON_AFFECTED;

    case BEAM_SPORE:
    case BEAM_CONFUSION:
    case BEAM_IRRESISTIBLE_CONFUSION:
        if (mon->check_clarity())
        {
            if (you.can_see(*mon))
                obvious_effect = true;
            return MON_AFFECTED;
        }
        {
            // irresistible confusion has a shorter duration and is weaker
            // against strong monsters
            int dur = ench_power;
            if (flavour == BEAM_IRRESISTIBLE_CONFUSION)
                dur = max(10, dur - mon->get_hit_dice());
            else
                dur = _ench_pow_to_dur(dur);

            if (mon->add_ench(mon_enchant(ENCH_CONFUSION, 0, agent(), dur)))
            {
                // FIXME: Put in an exception for things you won't notice
                // becoming confused.
                if (simple_monster_message(*mon, " appears confused."))
                    obvious_effect = true;
            }
        }
        return MON_AFFECTED;

    case BEAM_SLEEP:
        if (mons_just_slept(*mon))
            return MON_UNAFFECTED;

        mon->put_to_sleep(agent(), ench_power);
        if (simple_monster_message(*mon, " falls asleep!"))
            obvious_effect = true;

        return MON_AFFECTED;

    case BEAM_INVISIBILITY:
    {
        if (enchant_monster_invisible(mon, "flickers and vanishes"))
            obvious_effect = true;

        return MON_AFFECTED;
    }

    case BEAM_ENSLAVE:
        if (agent() && agent()->is_monster())
        {
            enchant_type good = (agent()->wont_attack()) ? ENCH_CHARM
                                                         : ENCH_HEXED;
            enchant_type bad  = (agent()->wont_attack()) ? ENCH_HEXED
                                                         : ENCH_CHARM;

            const bool could_see = you.can_see(*mon);
            if (agent()->mid == mon->mid)
            {
                // Random effects self-zap can cause attempting to enslave self.
                simple_monster_message(*mon, " appears momentarily confused.");
                return MON_UNAFFECTED;
            }
            if (mon->has_ench(bad))
            {
                obvious_effect = mon->del_ench(bad);
                return MON_AFFECTED;
            }
            if (simple_monster_message(*mon, " is enslaved!"))
                obvious_effect = true;
            mon->add_ench(mon_enchant(good, 0, agent()));
            if (!obvious_effect && could_see && !you.can_see(*mon))
                obvious_effect = true;
            return MON_AFFECTED;
        }

        // Being a puppet on magic strings is a nasty thing.
        // Mindless creatures shouldn't probably mind, but because of complex
        // behaviour of enslaved neutrals, let's disallow that for now.
        mon->attitude = ATT_HOSTILE;

        // XXX: Another hackish thing for Pikel's band neutrality.
        if (mons_is_mons_class(mon, MONS_PIKEL))
            pikel_band_neutralise();

        if (simple_monster_message(*mon, " is charmed."))
            obvious_effect = true;
        mon->add_ench(ENCH_CHARM);
        if (you.can_see(*mon))
            obvious_effect = true;
        return MON_AFFECTED;

    case BEAM_PORKALATOR:
    {
        // Monsters which use the ghost structure can't be properly
        // restored from hog form.
        if (mons_is_ghost_demon(mon->type))
            return MON_UNAFFECTED;

        monster orig_mon(*mon);
        if (monster_polymorph(mon, mon->holiness() & MH_DEMONIC ?
                      MONS_HELL_HOG : mon->holiness() & MH_HOLY ?
                      MONS_HOLY_SWINE : MONS_HOG))
        {
            obvious_effect = true;

            // Don't restore items to monster if it reverts.
            orig_mon.inv = mon->inv;

            // monsters can't cast spells in hog form either -doy
            mon->spells.clear();

            // For monster reverting to original form.
            mon->props[ORIG_MONSTER_KEY] = orig_mon;
        }

        return MON_AFFECTED;
    }

    case BEAM_INNER_FLAME:
        if (!mon->has_ench(ENCH_INNER_FLAME)
            && (!mon->is_summoned() || mon->is_illusion())
            && mon->add_ench(mon_enchant(ENCH_INNER_FLAME, 0, agent())))
        {
            if (simple_monster_message(*mon,
                                       (mon->body_size(PSIZE_BODY) > SIZE_BIG)
                                        ? " is filled with an intense inner flame!"
                                        : " is filled with an inner flame."))
            {
                obvious_effect = true;
            }
        }
        return MON_AFFECTED;

    case BEAM_ENTROPIC_BURST:
        if (!mon->has_ench(ENCH_ENTROPIC_BURST)
            && !mon->is_summoned()
            && mon->add_ench(mon_enchant(ENCH_ENTROPIC_BURST, 0, agent())))
        {
            if (simple_monster_message(*mon,
                (mon->body_size(PSIZE_BODY) > SIZE_BIG)
                ? " seems to glow with intense scintillating chaos!"
                : " seems to glow with chaos!"))
            {
                obvious_effect = true;
            }
        }
        return MON_AFFECTED;

    case BEAM_CHAOTIC_INFUSION:
        if (!mon->has_ench(ENCH_CHAOTIC_INFUSION)
            && !mon->is_summoned()
            && mon->add_ench(mon_enchant(ENCH_CHAOTIC_INFUSION, 1, agent())))
        {
            if (simple_monster_message(*mon, " is infused with chaotic energies!"))
                obvious_effect = true;
        }
        return MON_AFFECTED;

    case BEAM_DIMENSION_ANCHOR:
        if (!mon->has_ench(ENCH_DIMENSION_ANCHOR)
            && mon->add_ench(mon_enchant(ENCH_DIMENSION_ANCHOR, 0, agent(),
                                         random_range(20, 30) * BASELINE_DELAY)))
        {
            if (simple_monster_message(*mon, " is firmly anchored in space."))
                obvious_effect = true;
        }
        return MON_AFFECTED;

    case BEAM_VULNERABILITY:
        if (!mon->has_ench(ENCH_LOWERED_MR)
            && mon->add_ench(mon_enchant(ENCH_LOWERED_MR, 0, agent(),
                                         random_range(20, 30) * BASELINE_DELAY)))
        {
            if (you.can_see(*mon))
            {
                mprf("%s magical defenses are stripped away.",
                     mon->name(DESC_ITS).c_str());
                obvious_effect = true;
            }
        }
        return MON_AFFECTED;

    case BEAM_MALIGN_OFFERING:
    {
        const int dam = resist_adjust_damage(mon, flavour, damage.roll());
        if (dam)
        {
            _malign_offering_effect(mon, agent(), dam);
            obvious_effect = true;
            return MON_AFFECTED;
        }
        else
        {
            simple_monster_message(*mon, " is unaffected.");
            return MON_UNAFFECTED;
        }
    }

    case BEAM_VIRULENCE:
        if (!mon->has_ench(ENCH_POISON_VULN)
            && mon->add_ench(mon_enchant(ENCH_POISON_VULN, 0, agent(),
                                         random_range(20, 30) * BASELINE_DELAY)))
        {
            if (simple_monster_message(*mon,
                                       " grows more vulnerable to poison."))
            {
                obvious_effect = true;
            }
        }
        return MON_AFFECTED;

    case BEAM_AGILITY:
        if (!mon->has_ench(ENCH_AGILE)
            && !mon->is_stationary()
            && mon->add_ench(ENCH_AGILE))
        {
            if (simple_monster_message(*mon, " suddenly seems more agile."))
                obvious_effect = true;
        }
        return MON_AFFECTED;

    case BEAM_SAP_MAGIC:
        if (!SAP_MAGIC_CHANCE())
        {
            if (you.can_see(*mon))
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        if (!mon->has_ench(ENCH_SAP_MAGIC)
            && mon->add_ench(mon_enchant(ENCH_SAP_MAGIC, 0, agent())))
        {
            if (you.can_see(*mon))
            {
                mprf("%s seems less certain of %s magic.",
                     mon->name(DESC_THE).c_str(), mon->pronoun(PRONOUN_POSSESSIVE).c_str());
                obvious_effect = true;
            }
        }
        return MON_AFFECTED;

    case BEAM_DRAIN_MAGIC:
    {
        if (!mon->antimagic_susceptible())
            break;

        const int dur =
            random2(div_rand_round(ench_power, mon->get_hit_dice()) + 1)
                    * BASELINE_DELAY;
        mon->add_ench(mon_enchant(ENCH_ANTIMAGIC, 0,
                                  agent(), // doesn't matter
                                  dur));
        if (you.can_see(*mon))
        {
            mprf("%s magic leaks into the air.",
                 apostrophise(mon->name(DESC_THE)).c_str());
        }

        if (agent() && (agent()->type == MONS_EYE_OF_DRAINING
                        || agent()->type == MONS_GHOST_MOTH))
        {
            agent()->heal(dur / BASELINE_DELAY);
        }
        obvious_effect = true;
        break;
    }

    case BEAM_TUKIMAS_DANCE:
        cast_tukimas_dance(ench_power, mon);
        obvious_effect = true;
        break;

    case BEAM_CIGOTUVI:
        obvious_effect = _cigotuvi(mon, agent());
        break;

    case BEAM_SNAKES_TO_STICKS:
        stickify(agent(), mon);
        obvious_effect = true;
        break;

    case BEAM_RESISTANCE:
        if (!mon->has_ench(ENCH_RESISTANCE)
            && mon->add_ench(ENCH_RESISTANCE))
        {
            if (simple_monster_message(*mon, " suddenly seems more resistant."))
                obvious_effect = true;
        }
        return MON_AFFECTED;

    case BEAM_UNRAVELLING:
        if (!monster_is_debuffable(*mon))
            return MON_UNAFFECTED;

        debuff_monster(*mon);
        _unravelling_explode(*this);
        return MON_AFFECTED;

    case BEAM_INFESTATION:
    {
        const int dur = (5 + random2avg(ench_power / 2, 2)) * BASELINE_DELAY;
        int degree = 0;   // 0 for Hornet, 1 for Spark Wasp
        if (x_chance_in_y(ench_power - 65, 135))
            degree = 1;
        mon->add_ench(mon_enchant(ENCH_INFESTATION, degree, &you, dur));
        if (simple_monster_message(*mon, " is infested!"))
            obvious_effect = true;
        return MON_AFFECTED;
    }

    case BEAM_VILE_CLUTCH:
    {
        const int dur = (4 + random2avg(div_rand_round(ench_power, 10), 2))
            * BASELINE_DELAY;
        dprf("Vile clutch duration: %d", dur);
        mon->add_ench(mon_enchant(ENCH_VILE_CLUTCH, 0, &you, dur));
        obvious_effect = true;
        return MON_AFFECTED;
    }

    default:
        break;
    }

    return MON_AFFECTED;
}

// Extra range used on hit.
int bolt::range_used_on_hit() const
{
    int used = 0;

    // Non-beams can only affect one thing (player/monster).
    if (!pierce)
        used = BEAM_STOP;
    else if (is_enchantment() && name != "line pass")
        used = (flavour == BEAM_DIGGING ? 0 : BEAM_STOP);
    // Hellfire stops for nobody!
    else if (flavour == BEAM_DAMNATION)
        used = 0;
    // Generic explosion.
    else if (is_explosion || is_big_cloud())
        used = BEAM_STOP;
    // Lightning goes through things.
    else if (flavour == BEAM_ELECTRICITY)
        used = 0;
    else
        used = 1;

    // Assume we didn't hit, after all.
    if (is_tracer && source_id == MID_PLAYER && used > 0
        && hit < AUTOMATIC_HIT)
    {
        return 0;
    }

    if (in_explosion_phase)
        return used;

    return used;
}

// Information for how various explosions look & sound.
struct explosion_sfx
{
    // A message printed when the player sees the explosion.
    const char *seeMsg;
    // What the player hears when the explosion goes off unseen.
    const char *sound;
};

// A map from origin_spells to special explosion info for each.
const map<spell_type, explosion_sfx> spell_explosions = {
    { SPELL_HURL_HELLFIRE, {
        "The hellfire blast explodes!",
        "an accursed explosion",
    } },
    { SPELL_CALL_DOWN_DAMNATION, {
        "The pillar hellfire denotates!",
        "an accursed explosion",
    } },
    { SPELL_FIREBALL, {
        "The fireball explodes!",
        "an explosion",
    } },
    { SPELL_ORB_OF_ELECTRICITY, {
        "The orb of electricity explodes!",
        "a clap of thunder",
    } },
    { SPELL_FIRE_STORM, {
        "A raging storm of fire appears!",
        "a raging storm",
    } },
    { SPELL_MEPHITIC_CLOUD, {
        "The ball explodes into a vile cloud!",
        "a loud \'bang\'",
    } },
    { SPELL_GHOSTLY_FIREBALL, {
        "The ghostly flame explodes!",
        "the shriek of haunting fire",
    } },
    { SPELL_VIOLENT_UNRAVELLING, {
        "The enchantments explode!",
        "a sharp crackling", // radiation = geiger counter
    } },
    { SPELL_ICEBLAST, {
        "The mass of ice explodes!",
        "the clash of breaking glass",
    } },
    { SPELL_GHOSTLY_SACRIFICE, {
        "The ghostly flame explodes!",
        "the shriek of haunting fire",
    } },
    { SPELL_SLIME_SHARDS, { // Intentionally empty to prevent message spam this happens 8 times in a row afterall.
        "",
        "",
    } },
};

// Takes a bolt and refines it for use in the explosion function.
// Explosions which do not follow from beams bypass this function.
void bolt::refine_for_explosion()
{
    ASSERT(!special_explosion);

    string seeMsg;
    string hearMsg;

    if (ex_size == 0)
        ex_size = 1;
    glyph   = dchar_glyph(DCHAR_FIRED_BURST);

    // Assume that the player can see/hear the explosion, or
    // gets burned by it anyway.  :)
    msg_generated = true;

    if (item != nullptr)
    {
        seeMsg  = "The " + item->name(DESC_PLAIN, false, false, false)
                  + " explodes!";
        hearMsg = "You hear an explosion!";
    }
    else
    {
        const explosion_sfx *explosion = map_find(spell_explosions,
                                                  origin_spell);
        if (explosion)
        {
            seeMsg = explosion->seeMsg;
            hearMsg = make_stringf("You hear %s!", explosion->sound);
            if (real_flavour == BEAM_CHAOTIC)
            {
                if (origin_spell == SPELL_FIRE_STORM)
                    seeMsg = "A raging storm of chaos appears!";
                if (origin_spell == SPELL_FIREBALL)
                    seeMsg = "The chaotic sphere explodes!";
                if (origin_spell == SPELL_MEPHITIC_CLOUD)
                    seeMsg = "The ball explodes into a scintillating random clouds!";
            }
        }
        else
        {
            seeMsg  = "The beam explodes into a cloud of software bugs!";
            hearMsg = "You hear the sound of one hand!";
        }
    }

    if (origin_spell == SPELL_ORB_OF_ELECTRICITY)
    {
        colour     = LIGHTCYAN;
        ex_size    = 2;
    }

    if (!is_tracer && !seeMsg.empty() && !hearMsg.empty())
    {
        heard = player_can_hear(target);
        // Check for see/hear/no msg.
        if (you.see_cell(target) || target == you.pos())
            mpr(seeMsg);
        else
        {
            if (!heard)
                msg_generated = false;
            else
                mprf(MSGCH_SOUND, "%s", hearMsg.c_str());
        }
    }
}

typedef vector< vector<coord_def> > sweep_type;

static sweep_type _radial_sweep(int r)
{
    sweep_type result;

    // Center first.
    result.emplace_back(1, coord_def(0,0));

    for (int rad = 1; rad <= r; ++rad)
    {
        sweep_type::value_type work;

        for (int d = -rad; d <= rad; ++d)
        {
            // Don't put the corners in twice!
            if (d != rad && d != -rad)
            {
                work.emplace_back(-rad, d);
                work.emplace_back(+rad, d);
            }

            work.emplace_back(d, -rad);
            work.emplace_back(d, +rad);
        }
        result.push_back(work);
    }
    return result;
}

/** How much noise does an explosion this big make?
 *
 *  @param the size of the explosion (radius, not diamater)
 *  @returns how much noise it would make.
 */
int explosion_noise(int rad)
{
    return 10 + rad * 5;
}

#define MAX_EXPLOSION_RADIUS 9
// Returns true if we saw something happening.
bool bolt::explode(bool show_more, bool hole_in_the_middle)
{
    ASSERT(!special_explosion);
    ASSERT(!in_explosion_phase);
    ASSERT(ex_size >= 0);

    // explode() can be called manually without setting real_flavour.
    // FIXME: The entire flavour/real_flavour thing needs some
    // rewriting!
    if (real_flavour == BEAM_CHAOS
        || real_flavour == BEAM_RANDOM
        || real_flavour == BEAM_CRYSTAL)
    {
        flavour = real_flavour;
    }

    const int r = min(ex_size, MAX_EXPLOSION_RADIUS);
    in_explosion_phase = true;
    // being hit by bounces doesn't exempt you from the explosion (not that it
    // currently ever matters)
    hit_count.clear();

    if (is_sanctuary(pos()) && flavour != BEAM_VISUAL)
    {
        if (!is_tracer && you.see_cell(pos()) && !name.empty())
        {
            mprf(MSGCH_GOD, "By Zin's power, the %s is contained.",
                 name.c_str());
            return true;
        }
        return false;
    }

#ifdef DEBUG_DIAGNOSTICS
    if (!quiet_debug)
    {
        dprf(DIAG_BEAM, "explosion at (%d, %d) : g=%d c=%d f=%d hit=%d dam=%dd%d r=%d",
             pos().x, pos().y, glyph, colour, flavour, hit, damage.num, damage.size, r);
    }
#endif

    if (!is_tracer)
    {
        loudness = explosion_noise(r);

        // Not an "explosion", but still a bit noisy at the target location.
        if (origin_spell == SPELL_INFESTATION
            || origin_spell == SPELL_BORGNJORS_VILE_CLUTCH)
        {
            loudness = spell_effect_noise(origin_spell);
        }

        // Lee's Rapid Deconstruction can target the tiles on the map
        // boundary.
        const coord_def noise_position = clamp_in_bounds(pos());
        bool heard_expl = noisy(loudness, noise_position, source_id);

        heard = heard || heard_expl;

        if (heard_expl && !explode_noise_msg.empty() && !you.see_cell(pos()))
            mprf(MSGCH_SOUND, "%s", explode_noise_msg.c_str());
    }

    // Run DFS to determine which cells are influenced
    explosion_map exp_map;
    exp_map.init(INT_MAX);
    if (can_burn_trees())
        determine_affected_cells(exp_map, coord_def(), 0, r, true, true, false);
    else
        determine_affected_cells(exp_map, coord_def(), 0, r, true, true);

    // We get a bit fancy, drawing all radius 0 effects, then radius
    // 1, radius 2, etc. It looks a bit better that way.
    const vector< vector<coord_def> > sweep = _radial_sweep(r);
    const coord_def centre(9,9);

    // Draw pass.
    if (!is_tracer)
    {
        for (const auto &line : sweep)
        {
            bool pass_visible = false;
            for (const coord_def delta : line)
            {
                if (delta.origin() && hole_in_the_middle)
                    continue;

                if (exp_map(delta + centre) < INT_MAX)
                    pass_visible |= explosion_draw_cell(delta + pos());
            }
            if (pass_visible)
            {
                update_screen();
                scaled_delay(explode_delay);
            }
        }
    }

    // Affect pass.
    int cells_seen = 0;
    for (const auto &line : sweep)
    {
        for (const coord_def delta : line)
        {
            if (delta.origin() && hole_in_the_middle)
                continue;

            if (exp_map(delta + centre) < INT_MAX)
            {
                if (you.see_cell(delta + pos()))
                    ++cells_seen;

                explosion_affect_cell(delta + pos());

                if (beam_cancelled) // don't spam prompts
                    return false;
            }
        }
    }

    // Delay after entire explosion has been drawn.
    if (!is_tracer && cells_seen > 0 && show_more)
        scaled_delay(explode_delay * 3);

    return cells_seen > 0;
}

/**
 * Draw one tile of an explosion, if that cell is visible.
 *
 * @param p The cell to draw, in grid coordinates.
 * @return True if the cell was actually drawn.
 */
bool bolt::explosion_draw_cell(const coord_def& p)
{
    if (you.see_cell(p))
    {
        const coord_def drawpos = grid2view(p);
        // bounds check
        if (in_los_bounds_v(drawpos))
        {
#ifdef USE_TILE
            int dist = (p - source).rdist();
            tileidx_t tile = tileidx_bolt(*this);
            tiles.add_overlay(p, vary_bolt_tile(tile, dist));
#endif
#ifndef USE_TILE_LOCAL
            cgotoxy(drawpos.x, drawpos.y, GOTO_DNGN);
            put_colour_ch(colour == BLACK ? random_colour(true)
                                          : element_colour(colour, false, p),
                          dchar_glyph(DCHAR_EXPLOSION));
#endif
            return true;
        }
    }
    return false;
}

void bolt::explosion_affect_cell(const coord_def& p)
{
    // pos() = target during an explosion, so restore it after affecting
    // the cell.
    const coord_def orig_pos = target;

    fake_flavour();
    target = p;
    affect_cell();

    target = orig_pos;
}

// Uses DFS
void bolt::determine_affected_cells(explosion_map& m, const coord_def& delta,
                                    int count, int r,
                                    bool stop_at_statues, bool stop_at_walls,
                                    bool stop_at_trees)
{
    const coord_def centre(9,9);
    const coord_def loc = pos() + delta;

    // A bunch of tests for edge cases.
    if (delta.rdist() > centre.rdist()
        || delta.rdist() > r
        || count > 10*r
        || !map_bounds(loc)
        || is_sanctuary(loc) && flavour != BEAM_VISUAL)
    {
        return;
    }

    const dungeon_feature_type dngn_feat = grd(loc);

    bool at_wall = false;

    // Check to see if we're blocked by a wall or a tree. Can't use
    // feat_is_solid here, since that includes statues which are a separate
    // check, nor feat_is_opaque, since that excludes transparent walls, which
    // we want. -ebering
    // XXX: We could just include trees as wall features, but this currently
    // would have some unintended side-effects. Would be ideal to deal with
    // those and simplify feat_is_wall() to return true for trees. -gammafunk
    if (feat_is_wall(dngn_feat))
    {
        // Special case: explosion originates from rock/statue
        // (e.g. Lee's Rapid Deconstruction) - in this case, ignore
        // solid cells at the center of the explosion.
        if (stop_at_walls && !(delta.origin() && can_affect_wall(loc)))
            return;
        // But remember that we are at a wall.
        if (flavour != BEAM_DIGGING)
            at_wall = true;
    }

    if (feat_is_tree(dngn_feat) || feat_is_closed_door(dngn_feat))
    {
        if (!stop_at_trees)
            return;
        at_wall = true;
    }

    if (feat_is_solid(dngn_feat) && !feat_is_wall(dngn_feat)
        && !can_affect_wall(loc) && stop_at_statues)
    {
        return;
    }

    m(delta + centre) = min(count, m(delta + centre));

    // Now recurse in every direction.
    for (int i = 0; i < 8; ++i)
    {
        const coord_def new_delta = delta + Compass[i];

        if (new_delta.rdist() > centre.rdist())
            continue;

        // Is that cell already covered?
        if (m(new_delta + centre) <= count)
            continue;

        // If we were at a wall, only move to visible squares.
        coord_def caster_pos = actor_by_mid(source_id) ?
                                   actor_by_mid(source_id)->pos() :
                                   you.pos();

        if (at_wall && !cell_see_cell(caster_pos, loc + Compass[i], LOS_NO_TRANS))
            continue;

        int cadd = 5;
        // Circling around the center is always free.
        if (delta.rdist() == 1 && new_delta.rdist() == 1)
            cadd = 0;
        // Otherwise changing direction (e.g. looking around a wall) costs more.
        else if (delta.x * Compass[i].x < 0 || delta.y * Compass[i].y < 0)
            cadd = 17;

        determine_affected_cells(m, new_delta, count + cadd, r,
                                 stop_at_statues, stop_at_walls);
    }
}

static bool _cig_check(const monster * mon)
{
    if (mons_genus(mon->mons_species()) == MONS_PULSATING_LUMP)
        return false;
    if (mon->is_insubstantial())
        return false;

    return (bool)(mon->holiness() & (MH_NATURAL | MH_UNDEAD));
}

// Returns true if the beam is harmful ((mostly) ignoring monster
// resists) -- mon is given for 'special' cases where,
// for example, "Heal" might actually hurt undead, or
// "Holy Word" being ignored by holy monsters, etc.
//
// Only enchantments should need the actual monster type
// to determine this; non-enchantments are pretty
// straightforward.
bool bolt::nasty_to(const monster* mon) const
{
    // Cleansing flame.
    if (flavour == BEAM_HOLY)
        return mon->res_holy_energy() < 3;

    // The orbs are made of pure disintegration energy. This also has the side
    // effect of not stopping us from firing further orbs when the previous one
    // is still flying.
    if (flavour == BEAM_DISINTEGRATION || flavour == BEAM_DEVASTATION
        || flavour == BEAM_ICY_DEVASTATION || flavour == BEAM_CHAOTIC_DEVASTATION)
        return mon->type != MONS_ORB_OF_DESTRUCTION && mon->type != MONS_ORB_OF_CHAOS;

    if (name == "icy shards" && mon->is_icy())
        return false;

    // Take care of other non-enchantments.
    if (!is_enchantment())
        return true;

    // Positive effects.
    if (nice_to(monster_info(mon)))
        return false;

    switch (flavour)
    {
        case BEAM_DIGGING:
            return false;
        case BEAM_INNER_FLAME:
        case BEAM_ENTROPIC_BURST:
            // Co-aligned inner flame is fine.
            return !mons_aligned(mon, agent());
        case BEAM_TELEPORT:
            // Friendly and good neutral monsters don't mind being teleported.
            return !mon->wont_attack();
        case BEAM_INFESTATION:
        case BEAM_VILE_CLUTCH:
        case BEAM_SLOW:
        case BEAM_PETRIFY:
        case BEAM_POLYMORPH:
        case BEAM_DISPEL_UNDEAD:
        case BEAM_PAIN:
        case BEAM_AGONY:
        case BEAM_HIBERNATION:
            return ench_flavour_affects_monster(flavour, mon);
        case BEAM_TUKIMAS_DANCE:
            return tukima_affects(*mon); // XXX: move to ench_flavour_affects?
        case BEAM_SNAKES_TO_STICKS:
            return is_snake(*mon);
        case BEAM_UNRAVELLING:
            return monster_is_debuffable(*mon); // XXX: as tukima's
        case BEAM_CIGOTUVI:
            return _cig_check(mon);
        default:
            break;
    }

    // everything else is considered nasty by everyone
    return true;
}

// Return true if the bolt is considered nice by mon.
// This is not the inverse of nasty_to(): the bolt needs to be
// actively positive.
bool bolt::nice_to(const monster_info& mi) const
{
    // Polymorphing a (very) ugly thing will mutate it into a different
    // (very) ugly thing.
    if (flavour == BEAM_POLYMORPH)
    {
        return mi.type == MONS_UGLY_THING
               || mi.type == MONS_VERY_UGLY_THING;
    }

    if (flavour == BEAM_HASTE
        || flavour == BEAM_HEALING
        || flavour == BEAM_MIGHT
        || flavour == BEAM_AGILITY
        || flavour == BEAM_INVISIBILITY
        || flavour == BEAM_RESISTANCE
        || flavour == BEAM_CHAOTIC_INFUSION)
    {
        return true;
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////
// bolt
// TODO: Eventually it'd be nice to have a proper factory for these things
// (extended from setup_mons_cast() and zapping() which act as limited ones).

killer_type bolt::killer() const
{
    if (flavour == BEAM_BANISH)
        return KILL_BANISHED;

    switch (thrower)
    {
    case KILL_YOU:
    case KILL_YOU_MISSILE:
        return (flavour == BEAM_PETRIFY) ? KILL_YOU : KILL_YOU_MISSILE;

    case KILL_MON:
    case KILL_MON_MISSILE:
        return KILL_MON_MISSILE;

    case KILL_YOU_CONF:
        return KILL_YOU_CONF;

    default:
        return KILL_MON_MISSILE;
    }
}

void bolt::set_target(const dist &d)
{
    if (!d.isValid)
        return;

    target = d.target;

    chose_ray = d.choseRay;
    if (d.choseRay)
        ray = d.ray;

    if (d.isEndpoint)
        aimed_at_spot = true;
}

void bolt::setup_retrace()
{
    if (pos().x && pos().y)
        target = pos();

    swap(source, target);
    chose_ray        = false;
    affects_nothing  = true;
    aimed_at_spot    = true;
    extra_range_used = 0;
}

void bolt::set_agent(const actor *actor)
{
    // nullptr actor is fine by us.
    if (!actor)
        return;

    source_id = actor->mid;

    if (actor->is_player())
        thrower = KILL_YOU_MISSILE;
    else
        thrower = KILL_MON_MISSILE;
}

/**
 * Who caused this beam?
 *
 * @param ignore_reflection If true, look all the way back to the original
 *                          source; if false (the default), treat the latest
 *                          actor to reflect this as the source.
 * @returns The actor that can be treated as the source. May be null if
 *          it's a now-dead monster, or if neither the player nor a monster
 *          caused it (for example, divine retribution).
 */
actor* bolt::agent(bool ignore_reflection) const
{
    killer_type nominal_ktype = thrower;
    mid_t nominal_source = source_id;

    // If the beam was reflected report a different point of origin
    if (reflections > 0 && !ignore_reflection)
    {
        if (reflector == MID_PLAYER || source_id == MID_PLAYER)
            return &menv[YOU_FAULTLESS];
        nominal_source = reflector;
    }

    // Check for whether this is actually a dith shadow, not you
    if (monster* shadow = monster_at(you.pos()))
        if (shadow->type == MONS_PLAYER_SHADOW && nominal_source == MID_PLAYER)
            return shadow;

    if (YOU_KILL(nominal_ktype))
        return &you;
    else
        return actor_by_mid(nominal_source);
}

bool bolt::is_enchantment() const
{
    return flavour >= BEAM_FIRST_ENCHANTMENT
           && flavour <= BEAM_LAST_ENCHANTMENT;
}

string bolt::get_short_name() const
{
    if (!short_name.empty())
        return short_name;

    if (item != nullptr && item->defined())
    {
        return item->name(DESC_A, false, false, false, false,
                          ISFLAG_IDENT_MASK | ISFLAG_COSMETIC_MASK);
    }

    if (real_flavour == BEAM_RANDOM
        || real_flavour == BEAM_CHAOS
        || real_flavour == BEAM_CRYSTAL)
    {
        return _beam_type_name(real_flavour);
    }

    if (flavour == BEAM_FIRE
        && (origin_spell == SPELL_STICKY_FLAME
            || origin_spell == SPELL_STICKY_FLAME_RANGE))
    {
        return "sticky fire";
    }

    if (flavour == BEAM_ELECTRICITY && pierce)
        return "lightning";

    if (origin_spell == SPELL_BLINDING_SPRAY)
    {
        return "blinding venom";
    }

    if (name == "bolt of dispelling energy")
        return "dispelling energy";

    if (flavour == BEAM_NONE || flavour == BEAM_MISSILE
        || flavour == BEAM_MMISSILE)
    {
        return name;
    }

    return _beam_type_name(flavour);
}

static string _beam_type_name(beam_type type)
{
    switch (type)
    {
    case BEAM_NONE:                  return "none";
    case BEAM_MISSILE:               return "missile";
    case BEAM_MMISSILE:              return "magic missile";
    case BEAM_FIRE:                  return "fire";
    case BEAM_COLD:                  return "cold";
    case BEAM_WATER:                 return "water";
    case BEAM_MAGIC:                 return "magic";
    case BEAM_ELECTRICITY:           return "electricity";
    case BEAM_MEPHITIC:              return "noxious fumes";
    case BEAM_POISON:                return "weak poison";
    case BEAM_IRRADIATE:             return "mutagenic radiation";
    case BEAM_NEG:                   return "negative energy";
    case BEAM_ACID_WAVE:             return "caustic ooze";
    case BEAM_ACID:                  return "acid";
    case BEAM_MIASMA:                return "miasma";
    case BEAM_SPORE:                 return "spores";
    case BEAM_POISON_ARROW:          return "strong poison";
    case BEAM_DAMNATION:             return "hellfire";
    case BEAM_STICKY_FLAME:          return "sticky fire";
    case BEAM_STEAM:                 return "steam";
    case BEAM_ENERGY:                return "energy";
    case BEAM_HOLY:                  return "cleansing flame";
    case BEAM_FRAG:                  return "fragments";
    case BEAM_SILVER:                return "silver blast";
    case BEAM_SILVER_FRAG:           return "silver fragments";
    case BEAM_LAVA:                  return "magma";
    case BEAM_PARADOXICAL:           return "freezing flame";
    case BEAM_ICE:                   // fallthrough
    case BEAM_FREEZE:                return "ice";
    case BEAM_ICY_DEVASTATION:       // fallthrough
    case BEAM_CHAOTIC_DEVASTATION:   // fallthrough
    case BEAM_DEVASTATION:           return "devastation";
    case BEAM_RANDOM:                return "random";
    case BEAM_CHAOTIC:               // fallthrough
    case BEAM_CHAOS:                 return "chaos";
    case BEAM_ELDRITCH:              return "forbidden energy";
    case BEAM_CHAOS_ENCHANTMENT:     return "chaotic enchantment";
    case BEAM_ENTROPIC_BURST:        return "entropic burst";
    case BEAM_CHAOTIC_INFUSION:      return "infusion of chaos";
    case BEAM_SLOW:                  return "slow";
    case BEAM_HASTE:                 return "haste";
    case BEAM_MIGHT:                 return "might";
    case BEAM_HEALING:               return "healing";
    case BEAM_WAND_HEALING:          return "healing mist";
    case BEAM_FOG:                   return "fog";
    case BEAM_BUTTERFLY:             return "fairy dust";
    case BEAM_BLOOD:                 return "vampiric fog";
    case BEAM_CONFUSION:             return "confusion";
    case BEAM_INVISIBILITY:          return "invisibility";
    case BEAM_DIGGING:               return "digging";
    case BEAM_TELEPORT:              return "teleportation";
    case BEAM_POLYMORPH:             return "polymorph";
    case BEAM_MALMUTATE:             return "malmutation";
    case BEAM_ENSLAVE:               return "enslave";
    case BEAM_BANISH:                return "banishment";
    case BEAM_PAIN:                  return "pain";
    case BEAM_AGONY:                 return "agony";
    case BEAM_DISPEL_UNDEAD:         return "dispel undead";
    case BEAM_DISINTEGRATION:        return "disintegration";
    case BEAM_BLINK:                 return "blink";
    case BEAM_BLINK_CLOSE:           return "blink close";
    case BEAM_PETRIFY:               return "petrify";
    case BEAM_MAGIC_CANDLE:          return "magic candle";
    case BEAM_PORKALATOR:            return "porkalator";
    case BEAM_HIBERNATION:           return "hibernation";
    case BEAM_SLEEP:                 return "sleep";
    case BEAM_BERSERK:               return "berserk";
    case BEAM_VISUAL:                return "visual effects";
    case BEAM_TORMENT_DAMAGE:        return "torment damage";
    case BEAM_AIR:                   return "air";
    case BEAM_INNER_FLAME:           return "inner flame";
    case BEAM_PETRIFYING_CLOUD:      return "calcifying dust";
    case BEAM_ENSNARE:               return "magic web";
    case BEAM_SENTINEL_MARK:         return "sentinel's mark";
    case BEAM_DIMENSION_ANCHOR:      return "dimension anchor";
    case BEAM_VULNERABILITY:         return "vulnerability";
    case BEAM_MALIGN_OFFERING:       return "malign offering";
    case BEAM_VIRULENCE:             return "virulence";
    case BEAM_AGILITY:               return "agility";
    case BEAM_SAP_MAGIC:             return "sap magic";
    case BEAM_CRYSTAL:               return "crystal bolt";
    case BEAM_CRYSTAL_FIRE:          // Fallthrough
    case BEAM_CRYSTAL_ICE:           // Fallthrough
    case BEAM_CRYSTAL_SPEAR:         return "crystal spear";
    case BEAM_DRAIN_MAGIC:           return "drain magic";
    case BEAM_TUKIMAS_DANCE:         return "tukima's dance";
    case BEAM_CIGOTUVI:              return "cigotuvi's degeneration";
    case BEAM_SNAKES_TO_STICKS:      return "stickify";
    case BEAM_BOUNCY_TRACER:         return "bouncy tracer";
    case BEAM_DEATH_RATTLE:          return "breath of the dead";
    case BEAM_RESISTANCE:            return "resistance";
    case BEAM_UNRAVELLING:           return "unravelling";
    case BEAM_UNRAVELLED_MAGIC:      return "unravelled magic";
    case BEAM_SHARED_PAIN:           return "shared pain";
    case BEAM_IRRESISTIBLE_CONFUSION:return "confusion";
    case BEAM_INFESTATION:           return "infestation";
    case BEAM_VILE_CLUTCH:           return "vile clutch";
    case BEAM_ROT:                   return "vicious blight";
    case BEAM_WAND_RANDOM:           return "random effects"; // Shouldn't ever show up...

    case NUM_BEAMS:                  die("invalid beam type");
    }
    die("unknown beam type");
}

string bolt::get_source_name() const
{
    if (!source_name.empty())
        return source_name;
    const actor *a = agent();
    if (a)
        return a->name(DESC_A, true);
    return "";
}

/**
 * Can this bolt knock back an actor?
 *
 * The bolts that knockback flying actors or actors only when damage is dealt
 * will return true when conditions are met.
 *
 * @param act The target actor. Check if the actor is flying for bolts that
 *            knockback flying actors.
 * @param dam The damage dealt. If non-negative, check that dam > 0 for bolts
 *             like force bolt that only push back upon damage.
 * @return True if the bolt could knockback the actor, false otherwise.
*/
bool bolt::can_knockback(const actor &act, int dam) const
{
    if (act.is_stationary() || act.wearing_ego(EQ_BOOTS, SPARM_STURDY))
        return false;

    return origin_spell == SPELL_PRIMAL_WAVE
           || origin_spell == SPELL_FORCE_LANCE && dam
           || origin_spell == SPELL_MUSE_OAMS_AIR_BLAST && dam;
}

/**
 * Can this bolt pull an actor?
 *
 * If a bolt is capable of pulling actors and the given actor can be pulled,
 * return true.
 *
 * @param act The target actor. Check if the actor is non-stationary and not
 *            already adjacent.
 * @param dam The damage dealt. Check that dam > 0.
 * @return True if the bolt could pull the actor, false otherwise.
*/
bool bolt::can_pull(const actor &act, int dam) const
{
    if (act.is_stationary() || adjacent(source, act.pos())
        || act.wearing_ego(EQ_BOOTS, SPARM_STURDY))
        return false;

    return origin_spell == SPELL_HARPOON_SHOT && dam;
}

void clear_zap_info_on_exit()
{
    for (const zap_info &zap : zap_data)
    {
        delete zap.player_damage;
        delete zap.player_tohit;
        delete zap.monster_damage;
        delete zap.monster_tohit;
    }
}

int ench_power_stepdown(int pow)
{
    return stepdown_value(pow, 30, 40, 100, 120);
}

/// Translate a given ench power to a duration, in aut.
int _ench_pow_to_dur(int pow)
{
    // ~15 turns at 25 pow, ~21 turns at 50 pow, ~27 turns at 100 pow
    return stepdown(pow * BASELINE_DELAY, 70);
}

// Can a particular beam go through a particular monster?
// Fedhas worshipers can shoot through non-hostile plants,
// and players can shoot through their demonic guardians.
bool shoot_through_monster(const bolt& beam, const monster* victim)
{
    actor *originator = beam.agent();
    if (!victim || !originator)
        return false;

    bool origin_worships_fedhas;
    mon_attitude_type origin_attitude;
    if (originator->is_player())
    {
        origin_worships_fedhas = have_passive(passive_t::shoot_through_plants);
        origin_attitude = ATT_FRIENDLY;
    }
    else
    {
        monster* temp = originator->as_monster();
        if (!temp)
            return false;
        origin_worships_fedhas = (temp->god == GOD_FEDHAS
            || (temp->friendly()
                && have_passive(passive_t::shoot_through_plants)));
        origin_attitude = temp->attitude;
    }

    if (origin_worships_fedhas && fedhas_protects(victim))
        return true;
    
    bool player_shoots_thru = originator->is_player()
            && (testbits(victim->flags, MF_DEMONIC_GUARDIAN)
                || mons_is_avatar(victim->type)
                || mons_is_hepliaklqana_ancestor(victim->type)
                || mons_enslaved_soul(*victim));

    if (player_shoots_thru
           && !beam.is_enchantment()
           && beam.origin_spell != SPELL_CHAIN_LIGHTNING
           && (mons_atts_aligned(victim->attitude, origin_attitude) || victim->neutral()))
    {
        return true;
    }

    return false;
}

/**
 * Given some shield value, what is the chance that omnireflect will activate
 * on an AUTOMATIC_HIT attack?
 *
 * E.g., if 40 is returned, there is a SH in 40 chance of a given attack being
 * reflected.
 *
 * @param SH        The SH (shield) value of the omnireflect user.
 * @return          A denominator to the chance of omnireflect activating.
 */
int omnireflect_chance_denom(int SH)
{
    return SH + 20;
}

/// Set up a beam aiming from the given monster to their target.
bolt setup_targetting_beam(const monster &mons)
{
    bolt beem;

    beem.source    = mons.pos();
    beem.target    = mons.target;
    beem.source_id = mons.mid;

    return beem;
}
