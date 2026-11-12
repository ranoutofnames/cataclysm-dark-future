#include "vehicle_part.h" // IWYU pragma: associated

#include "avatar.h"
#include "color.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "locations.h"
#include "map/map.h"
#include "messages.h"
#include "npc.h"
#include "string_formatter.h"
#include "translations.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather/weather.h"
#include "wheel_dimensions.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <set>

static const itype_id fuel_type_battery("battery");
static const itype_id fuel_type_none("null");

static const itype_id itype_battery("battery");
static const itype_id itype_muscle("muscle");

static const flag_id flag_NO_PAINT("NO_PAINT");

namespace {

auto vehicle_from_location(location<item>* loc) -> vehicle* {
    if (auto* const vloc = dynamic_cast<vehicle_item_location*>(loc)) {
        return vloc->vehicle_ptr();
    }
    return nullptr;
}

} // namespace

/*-----------------------------------------------------------------------------
 *                              VEHICLE_PART
 *-----------------------------------------------------------------------------*/
vehicle_part::vehicle_part(vehicle* veh)
    : id(vpart_id::NULL_ID()),
      hack_id(veh->get_next_hack_id()),
      base(new vehicle_base_item_location(veh, hack_id)),
      items(new vehicle_item_location(veh, hack_id)) {}

// This is a bit of a hack until vehicles are GOs
vehicle_part::vehicle_part()
    : id(vpart_id::NULL_ID()),
      base(new fake_item_location()),
      items(new fake_item_location()) {}

vehicle_part::vehicle_part(
    const vpart_id& vp, const tripoint_mnt_veh& dp, detached_ptr<item>&& obj, vehicle* veh)
    : mount(dp),
      id(vp),
      hack_id(veh->get_next_hack_id()),
      base(new vehicle_base_item_location(veh, hack_id)),
      items(new vehicle_item_location(veh, hack_id)) {
    base = std::move(obj);

    // Mark base item as being installed as a vehicle part
    base->set_flag(flag_VEHICLE);

    if (base->typeId() != vp->item) {
        debugmsg("incorrect vehicle part item, expected: %s, received: %s", vp->item.c_str(),
                 base->typeId().c_str());
    }
}

void vehicle_part::set_vehicle_hack(vehicle* veh) {
    hack_id = veh->get_next_hack_id();
    refresh_locations_hack(veh);
}

void vehicle_part::refresh_locations_hack(vehicle* veh) {
    base.set_loc_hack(new vehicle_base_item_location(veh, hack_id));
    items.set_loc_hack(new vehicle_item_location(veh, hack_id));
}

void vehicle_part::copy_static_from(const vehicle_part& source) {
    carry_names = source.carry_names;
    mount = source.mount;
    precalc = source.precalc;
    blood = source.blood;
    inside = source.inside;
    removed = source.removed;
    enabled = source.enabled;
    flags = source.flags;
    passenger_id = source.passenger_id;
    open = source.open;
    direction = source.direction;
    proxy_part_id = source.proxy_part_id;
    proxy_sym = source.proxy_sym;
    target = source.target;
    id = source.id;
    info_cache = source.info_cache;
    ammo_pref = source.ammo_pref;
    crew_id = source.crew_id;
    // hack_id is per-vehicle identity. Copying it duplicates IDs on spawned
    // vehicles and clobbers the destination during vector shift (erase/realloc),
    // which desyncs cargo locations from the part they belong to.
    part_color_ = source.part_color_;
}

// TODO!: This is a bit scuffed and will be until vehicles are game objects.
vehicle_part::vehicle_part(const vehicle_part& source, vehicle* veh): vehicle_part(veh) {
    copy_static_from(source);
    base = item::spawn(*source.base);
    for (const item* const& it : source.items) { items.push_back(item::spawn(*it)); }
}

vehicle_part::vehicle_part(vehicle_part&& source): vehicle_part() {
    vehicle* const veh = vehicle_from_location(source.base.get_loc_hack());
    copy_static_from(source);
    hack_id = source.hack_id;
    if (veh) { refresh_locations_hack(veh); }
    base = std::move(source.base);
    items = std::move(source.items);
}

auto vehicle_part::operator=(vehicle_part&& source) -> vehicle_part& {
    if (this == &source) { return *this; }
    // Drop destination cargo while this part's hack_id still matches its locations.
    items.clear();
    vehicle* veh = vehicle_from_location(source.base.get_loc_hack());
    if (!veh) { veh = vehicle_from_location(base.get_loc_hack()); }
    copy_static_from(source);
    hack_id = source.hack_id;
    if (veh) { refresh_locations_hack(veh); }
    base = std::move(source.base);
    items = std::move(source.items);
    return *this;
}

vehicle_part::operator bool() const { return id != vpart_id::NULL_ID(); }

auto vehicle_part::get_base() const -> item& { return *base; }

auto vehicle_part::set_base(detached_ptr<item>&& new_base) -> detached_ptr<item> {
    return base.swap(std::move(new_base));
}

auto vehicle_part::properties_to_item() const -> detached_ptr<item> {
    // TODO!: the big check
    detached_ptr<item> tmp = item::spawn(*base);
    tmp->unset_flag(flag_VEHICLE);

    // Cables get special handling: their target coordinates need to remain stored, and if a
    // cable actually drops, it should be half-connected.
    // Tow cables are handled inside of invalidate_towing instead.
    // CDF: LINKUP1d, DDA vehicle.cpp:9091-9104 (vehicle::part_to_item). CDF has no
    // get_remote_part(here, vp); the remote part is looked up on the map at the part's stored
    // target, which is inside the cable's length and so inside the reality bubble.
    if (tmp->has_flag(flag_CABLE_SPOOL) && !tmp->has_flag(flag_TOW_CABLE)) {
        tmp->reset_link(false);
        // CDF: DDA's get_remote_part( here, vp ) walks the remote vehicle's loose parts for the
        // POWER_TRANSFER part at the stored target. The map lookup finds that vehicle; the part
        // is the one whose absolute position is this part's target.first (link_to() stores the
        // other end's part position there).
        const optional_vpart_position remote = get_map().veh_at(target.second);
        bool linked = false;
        if (remote) {
            vehicle& remote_veh = remote->vehicle();
            for (const vpart_reference& vp : remote_veh.get_all_parts()) {
                if (!vp.part().info().has_flag("POWER_TRANSFER")) { continue; }
                if (remote_veh.abs_part_location(vp.part_index()) != target.first) { continue; }
                if (tmp->link_to(remote_veh, vp.mount(), link_state::automatic).success()) {
                    tmp->link().t_abs_pos = target.second;
                    linked = true;
                }
                break;
            }
        }
        if (!linked) {
            // The linked vehicle can't be found, so this part shouldn't exist.
            tmp->set_flag(flag_NO_DROP);
        }
    }

    // force rationalization of damage values to the middle value of each damage level so
    // that parts will stack nicely
    tmp->set_damage(tmp->damage_level(4) * itype::damage_scale);
    return tmp;
}

void vehicle_part::add_item(detached_ptr<item>&& item) { items.push_back(std::move(item)); }

auto vehicle_part::name(bool with_prefix) const -> std::string {
    auto res = info().name();

    if (base->engine_displacement() > 0) {
        res.insert(0, string_format(_("%2.1fL "), base->engine_displacement() / 100.0));

    } else if (wheel_diameter() > 0) {
        res.insert(
            0,
            string_format(_("%s "), wheel_dimensions::format_for_display(wheel_diameter()).c_str()));
    }

    if (base->is_faulty()) { res += _(" (faulty)"); }

    if (base->has_var("contained_name")) {
        res += string_format(_(" holding %s"), base->get_var("contained_name"));
    }

    if (is_leaking()) { res += _(" (draining)"); }

    if (with_prefix) { res.insert(0, colorize(base->damage_symbol(), base->damage_color()) + " "); }
    return res;
}

auto vehicle_part::hp() const -> int {
    const int dur = info().durability;
    if (base->max_damage() > 0) {
        return dur - dur * base->damage() / base->max_damage();
    } else {
        return dur;
    }
}

auto vehicle_part::damage() const -> int { return base->damage(); }

auto vehicle_part::max_damage() const -> int { return base->max_damage(); }

auto vehicle_part::damage_level(int max) const -> int { return base->damage_level(max); }

auto vehicle_part::health_percent() const -> double {
    return 1.0 - static_cast<double>(base->damage()) / base->max_damage();
}

auto vehicle_part::damage_percent() const -> double {
    return static_cast<double>(base->damage()) / base->max_damage();
}

/** parts are considered broken at zero health */
auto vehicle_part::is_broken() const -> bool { return base->damage() >= base->max_damage(); }

auto vehicle_part::is_unavailable(const bool carried) const -> bool {
    return is_broken() || (has_flag(carried_flag) && carried);
}

auto vehicle_part::is_available(const bool carried) const -> bool {
    return !is_unavailable(carried);
}

auto vehicle_part::fuel_current() const -> itype_id {
    if (is_engine()) {
        if (ammo_pref.is_null()) {
            return info().fuel_type != itype_muscle ? info().fuel_type : itype_id::NULL_ID();
        } else {
            return ammo_pref;
        }
    }

    return itype_id::NULL_ID();
}

auto vehicle_part::fuel_set(const itype_id& fuel) -> bool {
    if (is_engine()) {
        for (const itype_id& avail : info().engine_fuel_opts()) {
            if (fuel == avail) {
                ammo_pref = fuel;
                return true;
            }
        }
    }
    return false;
}

auto vehicle_part::ammo_current() const -> itype_id {
    if (is_battery()) { return itype_battery; }

    if (is_tank() && !base->contents.empty()) { return base->contents.front().typeId(); }

    // CDF: REACTORPACK1: a reactor with a fuel pack seated reports the vehicle fuel id
    // (power.json fuel_type plut_cell), not the pack's plut_cell_charge contents, so every
    // vehicle site keyed on plut_cell (fuels_left, fuel_left, drain, slow_leak) keeps working.
    if (is_reactor() && base->magazine_current() != nullptr && !info().fuel_type.is_null()) {
        return info().fuel_type;
    }

    if (is_fuel_store(false) || is_turret()) { return base->ammo_current(); }

    return itype_id::NULL_ID();
}

auto vehicle_part::ammo_capacity() const -> int {
    if (is_tank()) { return ammo_current()->charges_per_volume(base->get_container_capacity()); }

    // CDF: REACTORPACK1: a well reactor reads its capacity from the seated pack, or from the
    // default magazine when the well is empty (item::ammo_capacity( true ), src/item.cpp:9422).
    if (has_fuel_pack_well()) { return base->ammo_capacity(true); }

    if (is_fuel_store(false) || is_turret()) { return base->ammo_capacity(); }

    return 0;
}

auto vehicle_part::ammo_remaining() const -> int {
    if (is_tank()) { return base->contents.empty() ? 0 : base->contents.back().charges; }

    if (is_fuel_store(false) || is_turret()) { return base->ammo_remaining(); }

    return 0;
}

auto vehicle_part::ammo_set(const itype_id& ammo, int qty) -> int {
    const itype* liquid = &*ammo;

    // We often check if ammo is set to see if tank is empty, if qty == 0 don't set ammo
    if (is_tank() && liquid->phase >= LIQUID && qty != 0) {
        base->contents.clear_items();
        const auto stack = units::legacy_volume_factor / std::max(liquid->stack_size, 1);
        const int limit = units::from_milliliter(ammo_capacity()) / stack;
        base->put_in(item::spawn(ammo, calendar::turn, qty > 0 ? std::min(qty, limit) : limit));
        return qty;
    }

    // CDF: REACTORPACK1: the spawn fill (vehicle.cpp:835-846) and the reactor tests call with
    // the vehicle fuel id plut_cell; the pack holds PLUT1's plut_cell_charge contents instead.
    // The default pack is inserted explicitly so the well takes the reactor pack and not "the
    // smallest that fits" (item::ammo_set, src/item.cpp:782-828).
    if (has_fuel_pack_well() && (ammo == info().fuel_type || ammo == base->ammo_default())) {
        const itype_id charge = base->ammo_default();
        if (!charge.is_null() && qty <= 0) {
            // qty 0 leaves the well empty: an empty spawn has no pack
            return 0;
        }
        if (base->magazine_current() == nullptr) {
            const itype_id default_pack = base->magazine_default();
            if (default_pack.is_null()) { return -1; }
            std::vector<item_pocket*> wells = base->get_pockets([](const item_pocket& p) {
                return p.is_type(pocket_type::MAGAZINE_WELL);
            });
            if (wells.empty()) { return -1; }
            if (!wells.front()->insert_item(item::spawn(default_pack, base->birthday())).success()) {
                return -1;
            }
        }
        base->ammo_set(charge, qty >= 0 ? qty : base->ammo_capacity(true));
        return base->ammo_remaining();
    }

    if (is_turret()) {
        base->ammo_set(ammo, qty);
        return base->ammo_remaining();
    }

    if (is_fuel_store()) {
        base->ammo_set(ammo, qty >= 0 ? qty : ammo_capacity());
        return base->ammo_remaining();
    }

    return -1;
}

void vehicle_part::ammo_unset() {
    if (is_tank()) {
        base->contents.clear_items();
    } else if (is_fuel_store()) {
        base->ammo_unset();
    }
}

auto vehicle_part::ammo_consume(int qty, map* here, const tripoint_bub_ms& pos) -> int {
    if (is_tank() && !base->contents.empty()) {
        const int res = std::min(ammo_remaining(), qty);
        item& liquid = base->contents.back();
        liquid.charges -= res;
        if (liquid.charges == 0) { base->contents.clear_items(); }
        return res;
    }
    return base->ammo_consume(qty, *here, pos, nullptr); // CDF: vehicle parts have no carrier (ADV-7)
}

auto vehicle_part::consume_energy(const itype_id& ftype, double energy_j) -> double {
    if (base->contents.empty() || !is_fuel_store()) { return 0.0f; }

    item& fuel = base->contents.back();
    if (fuel.typeId() == ftype) {
        assert(fuel.is_fuel());
        // convert energy density in MJ/L to J/ml
        const double energy_p_mL = fuel.fuel_energy() * 1000;
        const int ml_to_use = static_cast<int>(std::floor(energy_j / energy_p_mL));
        int charges_to_use = fuel.charges_per_volume(ml_to_use * 1_ml);

        if (!charges_to_use) { return 0.0; }
        if (charges_to_use >= fuel.charges) {
            charges_to_use = fuel.charges;
            base->contents.clear_items();
        } else {
            fuel.charges -= charges_to_use;
        }
        // TODO!: push up
        item& fuel_consumed = *item::spawn_temporary(ftype, calendar::turn, charges_to_use);
        return energy_p_mL * units::to_milliliter(fuel_consumed.volume(true));
    }
    return 0.0;
}

auto vehicle_part::can_reload(const item* obj) const -> bool {
    // first check part is not destroyed and can contain ammo
    if (!is_fuel_store()) { return false; }

    // CDF: REACTORPACK1: a reactor with a fuel well follows DDA's reactor answer
    // (DDA vehicle_part.cpp:426-428, 455-457): always refillable without an item, a pack that
    // the well allows, and loose cells only into a seated pack.
    if (has_fuel_pack_well()) {
        if (obj == nullptr || obj->is_null()) { return true; }
        if (base->magazine_compatible().count(obj->typeId()) > 0) { return true; }
        const item* pack = base->magazine_current();
        return pack != nullptr && pack->can_reload_with(*obj, false);
    }

    if (obj != nullptr && !obj->is_null()) {
        const itype_id obj_type = obj->typeId();
        if (is_reactor()) { return base->is_reloadable_with(obj_type); }

        // forbid filling tanks with solids or non-material things
        if (is_tank() && (obj->made_of(SOLID) || obj->made_of(PNULL))) {
            return false;
        }
        // forbid putting liquids, gasses, and plasma in things that aren't tanks
        else if (!obj->made_of(SOLID) && !is_tank()) {
            return false;
        }
        // prevent mixing of different ammo
        if (!ammo_current().is_null() && ammo_current() != obj_type) { return false; }
        // For storage with set type, prevent filling with different types
        if (info().fuel_type != fuel_type_none && info().fuel_type != obj_type) { return false; }
        // don't fill magazines with inappropriate fuel
        if (!is_tank() && !base->is_reloadable_with(obj_type)) { return false; }
    }

    return ammo_remaining() < ammo_capacity();
}

auto vehicle_part::has_fuel_pack_well() const -> bool {
    return is_reactor() && base->contents.has_pocket_type(pocket_type::MAGAZINE_WELL);
}

auto vehicle_part::insert_fuel_pack(Character& who, item& src) -> bool {
    if (!has_fuel_pack_well() || src.is_null() || !src.is_magazine()) { return false; }
    // the well's own item restriction decides which packs fit
    if (base->magazine_compatible().count(src.typeId()) == 0) { return false; }

    std::vector<item_pocket*> pockets = base->get_pockets([](const item_pocket&) {
        return true;
    });
    int well_index = -1;
    for (size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i]->is_type(pocket_type::MAGAZINE_WELL)) {
            well_index = static_cast<int>(i);
            break;
        }
    }
    if (well_index < 0) { return false; }

    // CDF: reload_targeted refuses the swap while the seated magazine is full
    // (src/item_pocket.cpp:2021-2028) and a full pack must be swappable, so the seated pack is
    // ejected by hand here; reload_targeted then fills the empty well (old pack to the carrier).
    if (item* seated = pockets[well_index]->magazine_current()) {
        detached_ptr<item> old = pockets[well_index]->remove_item(seated);
        base->on_contents_changed();
        if (old) {
            who.i_add(std::move(old), true, nullptr, nullptr, true, false);
        }
    }
    if (!base->reload(who, src, 1, well_index)) { return false; }
    return true;
}

auto vehicle_part::remove_fuel_pack() -> detached_ptr<item> {
    if (!has_fuel_pack_well()) { return detached_ptr<item>(); }
    std::vector<item_pocket*> wells = base->get_pockets([](const item_pocket& p) {
        return p.is_type(pocket_type::MAGAZINE_WELL);
    });
    if (wells.empty() || wells.front()->magazine_current() == nullptr) {
        return detached_ptr<item>();
    }
    detached_ptr<item> pack = wells.front()->remove_item(wells.front()->magazine_current());
    base->on_contents_changed();
    return pack;
}

void vehicle_part::process_contents(const tripoint_bub_ms& pos, const bool e_heater, int turns) {
    // for now we only care about processing food containers since things like
    // fuel don't care about temperature yet
    if (base->is_food_container()) {
        temperature_flag flag = temperature_flag::TEMP_NORMAL;
        if (e_heater) { flag = temperature_flag::TEMP_HEATER; }

        const vpart_info vpinfo = info();
        if (enabled && vpinfo.has_flag(VPFLAG_FRIDGE)) {
            flag = temperature_flag::TEMP_FRIDGE;
        } else if (enabled && vpinfo.has_flag(VPFLAG_FREEZER)) {
            flag = temperature_flag::TEMP_FREEZER;
        }

        base = item::process(base.release(), nullptr, pos, false, turns, flag);
    }
}

auto vehicle_part::fill_with(detached_ptr<item>&& liquid, int qty) -> detached_ptr<item> {
    if (!is_tank() || !can_reload(&*liquid)) { return std::move(liquid); }

    return base->fill_with(std::move(liquid), qty);
}

auto vehicle_part::faults() const -> const std::set<fault_id>& { return base->faults; }

auto vehicle_part::faults_potential() const -> std::set<fault_id> {
    return base->faults_potential();
}

auto vehicle_part::fault_set(const fault_id& f) -> bool {
    if (!faults_potential().contains(f)) { return false; }
    base->faults.insert(f);
    return true;
}

auto vehicle_part::wheel_area() const -> int { return info().wheel_area(); }

/** Get wheel diameter (millimeters) or return 0 if part is not wheel */
auto vehicle_part::wheel_diameter() const -> int {
    return base->is_wheel() ? base->type->wheel->diameter : 0;
}

/** Get wheel width (millimeters) or return 0 if part is not wheel */
auto vehicle_part::wheel_width() const -> int {
    return base->is_wheel() ? base->type->wheel->width : 0;
}

auto vehicle_part::crew() const -> npc* {
    if (is_broken() || !crew_id.is_valid()) { return nullptr; }

    npc* const res = g->critter_by_id<npc>(crew_id);
    if (!res) { return nullptr; }
    return res->is_player_ally() ? res : nullptr;
}

auto vehicle_part::set_crew(const npc& who) -> bool {
    if (who.is_dead_state() || !(who.is_walking_with() || who.is_player_ally())) { return false; }
    if (is_broken() || (!is_seat() && !is_turret())) { return false; }
    crew_id = who.getID();
    return true;
}

void vehicle_part::unset_crew() { crew_id = character_id(); }

void vehicle_part::reset_target(const tripoint_abs_ms& pos) {
    target.first = pos;
    target.second = pos;
}

auto vehicle_part::is_engine() const -> bool { return info().has_flag(VPFLAG_ENGINE); }

auto vehicle_part::is_light() const -> bool {
    const auto& vp = info();
    return vp.has_flag(VPFLAG_CONE_LIGHT) || vp.has_flag(VPFLAG_WIDE_CONE_LIGHT)
        || vp.has_flag(VPFLAG_HALF_CIRCLE_LIGHT) || vp.has_flag(VPFLAG_CIRCLE_LIGHT)
        || vp.has_flag(VPFLAG_AISLE_LIGHT) || vp.has_flag(VPFLAG_DOME_LIGHT)
        || vp.has_flag(VPFLAG_ATOMIC_LIGHT);
}

auto vehicle_part::is_fuel_store(bool skip_broke) const -> bool {
    if (skip_broke && is_broken()) { return false; }
    return is_tank() || base->is_magazine() || is_reactor();
}

auto vehicle_part::is_tank() const -> bool { return base->is_watertight_container(); }

auto vehicle_part::is_battery() const -> bool {
    return base->is_magazine() && base->ammo_types().contains(ammotype("battery"));
}

auto vehicle_part::is_reactor() const -> bool { return info().has_flag(VPFLAG_REACTOR); }

auto vehicle_part::is_perpetual_power_source() const -> bool {
    return info().has_flag("PERPETUAL") && info().epower > 0;
}

auto vehicle_part::is_leaking() const -> bool {
    return health_percent() <= 0.5 && (is_tank() || is_battery() || is_reactor());
}

auto vehicle_part::is_turret() const -> bool { return base->is_gun(); }

auto vehicle_part::is_seat() const -> bool { return info().has_flag("SEAT"); }

auto vehicle_part::info() const -> const vpart_info& {
    if (!info_cache) { info_cache = &id.obj(); }
    return *info_cache;
}

void vehicle::set_hp(vehicle_part& pt, int qty) {
    if (qty == pt.info().durability || pt.info().durability <= 0) {
        pt.base->set_damage(0);

    } else if (qty == 0) {
        pt.base->set_damage(pt.base->max_damage());

    } else {
        pt.base->set_damage(
            pt.base->max_damage() - pt.base->max_damage() * qty / pt.info().durability);
    }
}

auto vehicle::mod_hp(vehicle_part& pt, int qty, damage_type dt) -> bool {
    if (pt.info().durability > 0) {
        return pt.base->mod_damage(-(pt.base->max_damage() * qty / pt.info().durability), dt);
    } else {
        return false;
    }
}

auto vehicle::can_enable(const vehicle_part& pt, bool alert) const -> bool {
    if (std::ranges::none_of(parts, [&pt](const vehicle_part& e) { return &e == &pt; })
        || pt.removed) {
        debugmsg("Cannot enable removed or non-existent part");
    }

    if (pt.is_broken()) { return false; }

    // Disallow running a planter underground for now
    if (pt.info().has_flag("PLANTER")
        && (!warm_enough_to_plant(g->u.abs_pos()) || bub_ms_location().z() < 0)) {
        if (alert) { add_msg(m_bad, _("It is too cold to plant anything now.")); }
        return false;
    }

    // TODO: check fuel for combustion engines

    if (pt.info().epower < 0 && fuel_left(fuel_type_battery, true) <= 0) {
        if (alert) { add_msg(m_bad, _("Insufficient power to enable %s"), pt.name()); }
        return false;
    }

    return true;
}

auto vehicle::assign_seat(vehicle_part& pt, const npc& who) -> bool {
    if (!pt.is_seat() || !pt.set_crew(who)) { return false; }

    // NPC's can only be assigned to one seat in the vehicle
    for (auto& e : parts) {
        if (&e == &pt) {
            // skip this part
            continue;
        }

        if (e.is_seat()) {
            const npc* n = e.crew();
            if (n && n->getID() == who.getID()) { e.unset_crew(); }
        }
    }

    return true;
}

auto vehicle_part::carried_name() const -> std::string {
    if (carry_names.empty()) { return std::string(); }
    return carry_names.top().substr(name_offset);
}

auto vehicle_part::get_color(bool ignore_default) const -> RGBColorPair {
    if (ignore_default) { return part_color_; }

    const auto [def_bg, def_fg] = info().default_color;
    const auto [set_bg, set_fg] = part_color_;
    return RGBColorPair{
        set_bg == RGBColor{} ? def_bg : set_bg, set_fg == RGBColor{} ? def_fg : set_fg};
}

void vehicle_part::set_color(const RGBColor& bg, const RGBColor& fg) {
    part_color_ = RGBColorPair{.bg = bg, .fg = fg};

    if (base->type->has_flag(flag_NO_PAINT)) { return; }

    auto& vars = base->item_vars();
    if (bg == fg) {
        vars.set<RGBColor>(TINT_COLOR_VAR_NAME, bg);
        vars.erase(TINT_COLOR_FG_VAR_NAME);
        vars.erase(TINT_COLOR_BG_VAR_NAME);
    } else {
        vars.erase(TINT_COLOR_VAR_NAME);
        vars.set<RGBColor>(TINT_COLOR_FG_VAR_NAME, fg);
        vars.set<RGBColor>(TINT_COLOR_BG_VAR_NAME, bg);
    }
}
