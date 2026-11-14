# Item Creation / Types

> [!NOTE]
>
> This page is currently under-construction and was recently split off from `JSON INFO`

### Generic Items

```json
"type": "GENERIC",                // Defines this as some generic item
"id": "socks",                    // Unique ID. Must be one continuous word, use underscores if necessary
"name": {
    "ctxt": "clothing",           // Optional translation context. Useful when a string has multiple meanings that need to be translated differently in other languages.
    "str": "pair of socks",       // The name appearing in the examine box.  Can be more than one word separated by spaces
    "str_pl": "pairs of socks"    // Optional. If a name has an irregular plural form (i.e. cannot be formed by simply appending "s" to the singular form), then this should be specified. "str_sp" can be used if the singular and plural forms are the same
},
"conditional_names": [ {          // Optional list of names that will be applied in specified conditions (see Conditional Naming section for more details).
    "type": "COMPONENT_ID",       // The condition type.
    "condition": "leather",       // The condition to check for.
    "name": { "str": "pair of leather socks", "str_pl": "pairs of leather socks" } // Name field, same rules as above.
} ],
"container": "null",             // What container (if any) this item should spawn within
"repairs_like": "scarf",          // If this item does not have recipe, what item to look for a recipe for when repairing it.
"color": "blue",                 // Color of the item symbol.
"symbol": "[",                   // The item symbol as it appears on the map. Must be a Unicode string exactly 1 console cell width.
"looks_like": "rag",              // hint to tilesets if this item has no tile, use the looks_like tile
"description": "Socks. Put 'em on your feet.", // Description of the item
"ascii_picture": "ascii_socks", // Id of the asci_art used for this item
"phase": "solid",                            // (Optional, default = "solid") What phase it is
"spill_field": "fd_water",                   // (Optional) For liquids dropped onto ordinary ground, create this field before any drop_action runs.
"weight": "350 g",                           // Weight, weight in grams, mg and kg can be used - "50 mg", "5 g" or "5 kg". For stackable items (ammo, comestibles) this is the weight per charge.
"volume": "250 ml",                          // Volume, volume in ml and L can be used - "50 ml" or "2 L". For stackable items (ammo, comestibles) this is the volume of stack_size charges.
"integral_volume": 0,                        // Volume added to base item when item is integrated into another (eg. a gunmod integrated to a gun). Volume in ml and L can be used - "50 ml" or "2 L". Can be negative to reduce parent volume. Clamped at 1% of parent base volume.
"integral_weight": 0,                        // Weight added to base item when item is integrated into another (eg. a gunmod integrated to a gun). Can be negative to reduce parent weight. Clamped at 1% of parent base weight.
"rigid": false,                              // For non-rigid items volume (and for worn items encumbrance) increases proportional to contents
"insulation": 1,                             // (Optional, default = 1) If container or vehicle part, how much insulation should it provide to the contents
"price": 100,                                // Used when bartering with NPCs. For stackable items (ammo, comestibles) this is the price for stack_size charges. Can use string "cent" "USD" or "kUSD".
"price_postapoc": "1 USD",                   // Same as price but represent value post cataclysm. Can use string "cent" "USD" or "kUSD".
"material": ["COTTON"],                      // Material types, can be as many as you want.  See materials.json for possible options
"weapon_category": [ "WEAPON_CAT1" ],        // (Optional) Weapon categories this item is in for martial arts.
"cutting": 0,                                // (Optional, default = 0) Cutting damage caused by using it as a melee weapon.  This value cannot be negative.
"bashing": 0,                                // (Optional, default = 0) Bashing damage caused by using it as a melee weapon.  This value cannot be negative.
"to_hit": 0,                                 // (Optional, default = 0) To-hit bonus if using it as a melee weapon (whatever for?)
"attacks": [                                 // (Optional) New attack statblock, WIP feature
  { "id": "BASH",                            // ID of the attack.  Attack with ID "DEFAULT" will be replaced by calculated data (this can be used to remove custom attacks on "copy-from" item)
    "to_hit": 1,                             // To-hit bonus of this attack
    "damage": { "values": [ { "damage_type": "bash", "amount": 50 } ] } }, // Damage of this attack, using `damage_instance` syntax (see below)
  { "id": "THRUST", "damage": { "values": [ { "damage_type": "stab", "amount": 45 } ] } }
],
"flags": ["VARSIZE"],                        // Indicates special effects, see json_flags.md
"environmental_protection_with_filter": 6,   // the resistance to environmental effects if an item (for example a gas mask) requires a filter to operate and this filter is installed. Used in combination with use_action 'GASMASK' and 'DIVE_TANK'
"magazine_well": 0,                          // Volume above which the magazine starts to protrude from the item and add extra volume
"magazines": [                               // Magazines types for each ammo type (if any) which can be used to reload this item
    [ "9mm", [ "glockmag" ] ]                // The first magazine specified for each ammo type is the default
    [ "45", [ "m1911mag", "m1911bigmag" ] ],
],
"crafting_speed_modifier": 1.0,              // Optional (default = 1.0). Multiplier applied to crafting speed when this item satisfies a tool or quality requirement.
"milling": {                                 // Optional. If given, the item can be milled in a water/wind mill.
  "into": "flour",                           // The item id of the product. Product MUST be something that uses charges.
  "conversion_rate": 4                       // Number of products per item consumed. At a conversion_rate of 4, 1 item is milled into 4 product. Only accepts integers.
},
"item_vars": {                               // Prepopulate Item Vars; Currently used by nothing other then mods and in game logic
  "test": "test"                             // Predominately lua mods will find this useful
}
"explode_in_fire": true,                     // Should the item explode if set on fire
"explosion": {                               // Physical explosion data
  "damage": 10,                              // Damage the explosion deals to player at epicenter. Damage is halved above 50% radius.
  "radius": 8,                               // Radius of the explosion. 0 means only the epicenter is affected.
  "fire": true,                              // Should the explosion leave fire
  "fragment_effect": [ {                     // Effects data of "shrapnel"
      "effect": "onfire",                    // Effect to apply (note that onfire has special hardcoded behaviour to check the target is flamable)
      "odds": 2,                             // One in x chance to apply this effect
      "min_turns": 4,                        // Min turn duration for effect
      "max_turns": 8                         // Max turn duration for effect
    }
  ],
  "fragment": {                              // Projectile data of "shrapnel". This projectile will hit every target in its range and field of view exactly once.
    "damage": {                              // Damage data of the shrapnel projectile.  Uses damage_instance syntax (see below)
      "damage_type": "acid",                 // Type of damage dealt.
      "amount": 10                           // Amount of damage dealt.
      "armor_penetration": 4                 // Amount of armor ignored. Applied per armor piece, not in total.
      "armor_multiplier": 2.5                // Multiplies remaining damage reduction from armor, applied after armor penetration (if present). Higher numbers make armor stop fragments from this explosion more effectively, lower numbers act as a percentage reduction in remaining armor.
    }
  }
},
"repair_difficulty": 2                       // Overrites recipe difficulty being used for repair difficulty
```

`spill_field` is intended for liquids. If a liquid item with a `spill_field` is dropped onto
ordinary ground, the game marks the liquid dirty when appropriate, creates the specified field,
and then still runs any `drop_action` the item defines. The liquid item is consumed: its charges
and item state are not stored in the field, and the puddle cannot be collected as liquid again.
Tiles with the `LIQUIDCONT` flag do not convert liquids to spill fields.

Spill visuals are volume-based rather than charge-count-based. The current implementation treats
each liter of liquid as one unit of visible spill intensity, with a minimum of one unit for any
non-zero spill. When additional liquid is poured into an existing connected spill of the same
field type, the spill deepens existing tiles first by raising field intensity, then expands
outward into adjacent passable tiles. The visible footprint is capped, so very large spills stop
growing visually while still consuming the poured liquid.

In practice, `spill_field` is usually many-to-one. Multiple related liquids can intentionally map
to the same visible field to avoid JSON bloat and unnecessary field type proliferation. For
example, `tea`, `coca_tea`, and `sweet_tea` can all spill as `fd_tea`, producing the same visible
`puddle of tea` field. A separate field type is only needed when the puddle has different effects
or appearance. See [Field Types](../map/field_type.md) for field properties.

#### damage_instance

```json
{
  "damage_type": "acid", // Type of damage dealt.
  "amount": 10, // Amount of damage dealt.
  "armor_penetration": 4, // Amount of armor ignored. Applied per armor piece, not in total.
  "armor_multiplier": 2.5 // Multiplies remaining damage reduction from armor, applied after armor penetration (if present). Higher numbers make armor more effective at protecting from this attack, lower numbers act as a percentage reduction in remaining armor.
}
```

### Ammo

```json
"type" : "AMMO",            // Defines this as ammo
...                         // same entries as above for the generic item.
                            // additional some ammo specific entries:
"ammo_type" : "shot",       // Determines what it can be loaded in
"damage" : 18,              // Ranged damage when fired
"prop_damage": 2,           // Multiplies the damage of weapon by amount (overrides damage field)
"pierce" : 0,               // Armor piercing ability when fired
"range" : 5,                // Range when fired
"dispersion" : 0,           // Inaccuracy of ammo, measured in quarter-degrees
"recoil" : 18,              // Recoil caused when firing
"count" : 25,               // Number of rounds that spawn together
"stack_size" : 50,          // (Optional) How many rounds are in the above-defined volume. If omitted, is the same as 'count'
"show_stats" : true,        // (Optional) Force stat display for combat ammo. (for projectiles lacking both damage and prop_damage)
"dont_recover_one_in": 1    // (Optional) 1 in x chance of not recovering the ammo (100 means you have a 99% chance of getting it back)
"drop": "nail",             // (Optional) Defines an object that drops at the projectile location at a 100% chance.
"drop_active": false        // (Optional) Whether the object starts active. Default is true.
"drop_count": 1,            // (Optional) Number of items to drop. For tools, this sets their charges. 
                             // If omitted, the drop amount defaults to the 'count' defined in its itype.
"shot": {                   // (Optional) Shot-pattern data for pellet-style ammo.
  "count": 12,             // Number of projectile attacks spawned by one round.
  "half_angle": 3          // Half-angle in degrees used for the pellet spread preview and pattern.
},
"effects" : ["COOKOFF", "SHOT"]
```

### Magazine

```json
"type": "MAGAZINE",              // Defines this as a MAGAZINE
...                              // same entries as above for the generic item.
                                 // additional some magazine specific entries:
"ammo_type": [ "40", "357sig" ], // What types of ammo this magazine can be loaded with
"capacity" : 15,                 // Capacity of magazine (in equivalent units to ammo charges)
"count" : 0,                     // Default amount of ammo contained by a magazine (set this for ammo belts)
"default_ammo": "556",           // If specified override the default ammo (optionally set this for ammo belts)
"reliability" : 8,               // How reliable this this magazine on a range of 0 to 10? (see GAME_BALANCE.md)
"reload_time" : 100,             // How long it takes to load each unit of ammo into the magazine
"mag_jam_mult": 1.25,            // Multiplier (1.0 or more, default 1.0) for the chance that the gun jams because the magazine is damaged. Higher is worse: a drum magazine might use 2. The jam chance per shot is roughly 0.027% for an undamaged magazine, 5% at damage 1, 24% at damage 2, 96% at damage 3 and 250% at damage 4, times this value, added to the gun's own chance (see `gun_jam_mult`). A jam costs the shot.
"linkage" : "ammolink"           // If set one linkage (of given type) is dropped for each unit of ammo consumed (set for disintegrating ammo belts)
"reloads_like": "light_minus_battery_cell" // If set, anything that takes that itype as a magazine will also take this itype
```

### Armor

Armor can be defined like this:

```json
"type" : "ARMOR",     // Defines this as armor
...                   // same entries as above for the generic item.
                      // additional some armor specific entries:
"covers" : ["FEET"],  // Where it covers.  Possible options are TORSO, HEAD, EYES, MOUTH, ARMS, HANDS, LEGS, FEET
"storage" : 0,        //  (Optional, default = 0) How many volume storage slots it adds
"warmth" : 10,        //  (Optional, default = 0) How much warmth clothing provides
"environmental_protection" : 0,  //  (Optional, default = 0) How much environmental protection it affords
"encumbrance" : 0,    // Base encumbrance (unfitted value)
"max_encumbrance" : 0,    // When a character is completely full of volume, the encumbrance of a non-rigid storage container will be set to this. Otherwise it'll be between the encumbrance and max_encumbrance following the equation: encumbrance + (max_encumbrance - encumbrance) * character volume.
"weight_capacity_bonus": "20 kg",    // (Optional, default = 0) Bonus to weight carrying capacity, can be negative. Strings must be used - "5000 g" or "5 kg"
"weight_capacity_modifier": 1.5, // (Optional, default = 1) Factor modifying base weight carrying capacity.
"coverage" : 80,      // What percentage of body part
"material_thickness" : 1,  // Thickness of material, in millimeter units (approximately).  Generally ranges between 1 - 5, more unusual armor types go up to 10 or more
"power_armor" : false, // If this is a power armor item (those are special).
"valid_mods" : ["steel_padded"], // List of valid clothing mods. Note that if the clothing mod doesn't have "restricted" listed, this isn't needed.
"resistance": { "cut": 0, "bullet": 1000 }, // If set, overrides usual resistance calculation. Values are for undamaged item, thickness affects scaling with damage - 1 thickness means no reduction from damage, 2 means it's halved on first damage, 10 means each level of damage decreases armor by 10%
"hearing_protection": 0,    // How much does this armor dampen sound hear by the wearer, in dB spl. 0 - 191. This will make all sounds harder to hear for the wearer, including deafening sounds. At a hearing_ability multiplier of one, heard sounds of 120dB+ can deafen the player, with guaranteed temporary deafness at 140dB+. Use this instead of the "DEAF" or "PARTIAL_DEAF" flags. ~40 dB spl total protection will protect against most gunfire, 80 will protect against almost any sound. Cumulative with other items with this quality.
"adv_hearing_protection": 0 // How much does this armor dampen deafening sounds heard by the wearer, in dB spl. 0 - 191. This will reduce the damaging volume of deafening sounds by its given amount without dampening other sounds.  Cumulative with other items with this quality.
"provided_slots": [ { "id": "INSERT_FLAG_ID_HERE", "amount": 1 } ] // Flag id to provide for wearing requirements
"consumed_slots": [ { "id": "INSERT_FLAG_ID_HERE", "amount": 1 } ] // Flag id to require for wearing requirements, automatically is this with 1 if there is one such instance.
```

Alternately, every item (book, tool, gun, even food) can be used as armor if it has armor_data:

```json
"type" : "TOOL",      // Or any other item type
...                   // same entries as for the type (e.g. same entries as for any tool),
"armor_data" : {      // additionally the same armor data like above
    "covers" : ["FEET"],
    "storage" : 0,
    "warmth" : 10,
    "environmental_protection" : 0,
    "encumbrance" : 0,
    "coverage" : 80,
    "material_thickness" : 1,
    "power_armor" : false
}
```

#### Armor Portion Data

For items that cover multiple body parts with different coverage or encumbrance values, use `armor_portion_data`. This allows defining separate values for each body part or group of body parts:

```json
"armor_portion_data": [
    { 
        "covers": [ "torso" ], 
        "coverage": 95, 
        "encumbrance": 15 
    },
    { 
        "covers": [ "arms", "legs" ], 
        "coverage": 80, 
        "encumbrance": 10,
        "max_encumbrance": 20
    }
]
```

Fields for each entry in `armor_portion_data`:

- `covers`: Array of body part IDs this entry applies to (e.g., "torso", "head", "eyes", "mouth", "arms", "hands", "legs", "feet")
- `coverage`: Percentage of body part area covered (0-100). Higher values mean better protection.
- `encumbrance`: How much the item encumbers the body part. Default is 0.
- `max_encumbrance`: Encumbrance when the character is at full storage volume. Default equals `encumbrance`.
- `encumbrance` as `[ min, max ]`: an array of two integers. The first value is the base encumbrance (`encumbrance`), the second the max encumbrance (`max_encumbrance`). When the pockets of this armor are completely full of items, the encumbrance of a non-rigid item is set to the max; otherwise it is between the two, following the volume of the non-rigid items. By default, the max is the encumbrance plus the non-rigid volume / 250 ml (DDA `ITEM.md:408-413`; CDF `item_factory.cpp:3124-3126`, default `:370-389`).
- `encumbrance_modifiers`: an array of `IMBALANCED`, `RESTRICTS_NECK`, `WELL_SUPPORTED` or `NONE`. Experimental feature for having the encumbrance generated by the item's weight instead of a fixed number. If you don't need any modifiers put `NONE`. It overrides `encumbrance`, so a given `encumbrance` is ignored (DDA `ITEM.md:414-416`, where it currently only works for head armor). CDF: the encumbrance is worked out from the item's weight when the item type is finalized, interpolated on the `encumbrance_per_weight` table of the first body part the portion covers (only `head` has one, see [sub body parts](../creatures/sub_body_parts.md)). `IMBALANCED` and `RESTRICTS_NECK` add 10, `WELL_SUPPORTED` takes 20 percent off, and the result is at least 1. A weight outside the table is a debugmsg and gives 100 (CDF `item_factory.cpp:3117-3123`, `:307-345`, `:380-386`).
- `layers`: the layer the armor portion occupies: `PERSONAL`, `SKINTIGHT`, `NORMAL`, `WAIST`, `OUTER`, `BELTED` or `AURA` (DDA `ITEM.md:421-423`). CDF reads and stores the list, but layering still uses the one layer of the item (the item flags `SKINTIGHT`, `OUTER`, `BELTED` and the others), so a portion's `layers` does not change layering yet (CDF `item_factory.cpp:3158-3159`, `itype.h:368-371`).

- `specifically_covers`: (Optional) Array of sub body part IDs (e.g. `torso_upper`, `arm_shoulder_l`, `head_throat`) this entry covers; see [sub body parts](../creatures/sub_body_parts.md). `coverage` then applies to each of those sub-locations. The coverage of the whole body part is derived from the entries: each one's coverage, weighted by the share of the body part its sub-locations make up. Encumbrance of such entries is summed. Entries that list sub-locations must name them in `covers` too (or leave `covers` out and the sub-locations' body parts are used).

When `armor_portion_data` is used, the top-level `covers`, `coverage`, `encumbrance`, and `max_encumbrance` fields should not be used as they are replaced by the portion data.

### Pet Armor

Pet armor can be defined like this:

```json
"type" : "PET_ARMOR",     // Defines this as armor
...                   // same entries as above for the generic item.
                      // additional some armor specific entries:
"storage" : 0,        //  (Optional, default = 0) How many volume storage slots it adds
"environmental_protection" : 0,  //  (Optional, default = 0) How much environmental protection it affords
"material_thickness" : 1,  // Thickness of material, in millimeter units (approximately).  Generally ranges between 1 - 5, more unusual armor types go up to 10 or more
"pet_bodytype": "dog",       // the body type of the pet that this monster will fit.  See MONSTERS.md
"max_pet_vol": "50 ml",   // the maximum volume of the pet that will fit into this armor. Volume in ml and L can be used - "50 ml" or "2 L".
"min_pet_vol": "50 ml",   // the minimum volume of the pet that will fit into this armor. Volume in ml and L can be used - "50 ml" or "2 L".
"power_armor" : false, // If this is a power armor item (those are special).
```

Alternately, every item (book, tool, gun, even food) can be used as armor if it has armor_data:

```json
"type" : "TOOL",      // Or any other item type
...                   // same entries as for the type (e.g. same entries as for any tool),
"pet_armor_data" : {      // additionally the same armor data like above
    "storage" : 0,
    "environmental_protection" : 0,
    "pet_bodytype": "dog",
    "max_pet_vol": "35000 ml",
    "min_pet_vol": "25000 ml",
    "material_thickness" : 1,
    "power_armor" : false
}
```

### Books

Books can be defined like this:

```json
"type" : "BOOK",      // Defines this as a BOOK
...                   // same entries as above for the generic item.
                      // additional some book specific entries:
"max_level" : 5,      // Maximum skill level this book will train to
"intelligence" : 11,  // Intelligence required to read this book without penalty
"time" : "35 m",          // Time a single read session takes. An integer will be read in minutes or a time string can be used.
"fun" : -2,           // Morale bonus/penalty for reading
"skill" : "computer", // Skill raised
"chapters" : 4,       // Number of chapters (for fun only books), each reading "consumes" a chapter. Books with no chapters left are less fun (because the content is already known to the character).
"required_level" : 2  // Minimum skill level required to learn
```

Alternately, every item (tool, gun, even food) can be used as book if it has book_data:

```json
"type" : "TOOL",      // Or any other item type
...                   // same entries as for the type (e.g. same entries as for any tool),
"book_data" : {       // additionally the same book data like above
    "max_level" : 5,
    "intelligence" : 11,
    "time" : "35 m",
    "fun" : -2,
    "skill" : "computer",
    "chapters" : 4,
    "use_action" : "MA_MANUAL", // The book_data can have use functions (see USE ACTIONS) that are triggered when the books has been read. These functions are not triggered by simply activating the item (like tools would).
    "required_level" : 2
}
```

Since many book names are proper names, it's often necessary to explicitly specify the plural forms.
The following is the game's convention on plural names of books:

1. For non-periodical books (textbooks, manuals, spellbooks, etc.),
   1. If the book's singular name is a proper name, then the plural name is
      `copies of (singular name)`. For example, the plural name of
      `Lessons for the Novice Bowhunter` is `copies of Lessons for the Novice Bowhunter`.
   2. Otherwise, the plural name is the usual plural of the singular name. For example, the plural
      name of `tactical baton defense manual` is `tactical baton defense manuals`
2. For periodicals (magazines and journals),
   1. If the periodical's singular name is a proper name, and doesn't end with "Magazine", "Weekly",
      "Monthly", etc., the plural name is `issues of (singular name)`. For example, the plural name
      of `Archery for Kids` is `issues of Archery for Kids`.
   2. Otherwise, the periodical's plural name is the usual plural of the singular name. For example,
      the plural name of `Crafty Crafter's Quarterly` is `Crafty Crafter's Quarterlies`.
3. For board games (represented internally as book items),
   1. If the board game's singular name is a proper name, the plural is `sets of (singular name)`.
      For example, the plural name of `Picturesque` is `sets of Picturesque`.
   2. Otherwise the plural name is the usual plural. For example, the plural of `deck of cards` is
      `decks of cards`.

#### Conditional Naming

The `conditional_names` field allows defining alternate names for items that will be displayed
instead of (or in addition to) the default name, when specific conditions are met. Take the
following (incomplete) definition for `sausage` as an example of the syntax:

```json
{
  "name": "sausage",
  "conditional_names": [
    {
      "type": "FLAG",
      "condition": "CANNIBALISM",
      "name": "Mannwurst"
    },
    {
      "type": "COMPONENT_ID",
      "condition": "mutant",
      "name": { "str_sp": "sinister %s" }
    }
  ]
}
```

You can list as many conditional names for a given item as you want. Each conditional name must
consist of 3 elements:

1. The condition type:
   - `COMPONENT_ID` searches all the components of the item (and all of _their_ components, and so
     on) for an item with the condition string in their ID. The ID only needs to _contain_ the
     condition, not match it perfectly (though it is case sensitive). For example, supplying a
     condition `mutant` would match `mutant_meat`.
   - `FLAG` which checks if an item has the specified flag (exact match).
2. The condition you want to look for.
3. The name to use if a match is found. Follows all the rules of a standard `name` field, with valid
   keys being `str`, `str_pl`, and `ctxt`. You may use %s here, which will be replaced by the name
   of the item. Conditional names defined prior to this one are taken into account.

So, in the above example, if the sausage is made from mutant humanoid meat, and therefore both has
the `CANNIBALISM` flag, _and_ has a component with `mutant` in its ID:

1. First, the item name is entirely replaced with "Mannwurst" if singular, or "Mannwursts" if
   plural.
2. Next, it is replaced by "sinister %s", but %s is replaced with the name as it was before this
   step, resulting in "sinister Mannwurst" or "sinister Mannwursts".

NB: If `"str": "sinister %s"` was specified instead of `"str_sp": "sinister %s"`, the plural form
would be automatically created as "sinister %ss", which would become "sinister Mannwurstss" which is
of course one S too far. Rule of thumb: If you are using %s in the name, always specify an identical
plural form unless you know exactly what you're doing!

#### Color Key

When adding a new book, please use this color key:

- Magazines: `pink`
- “Paperbacks” Short enjoyment books (including novels): `light_cyan`
- “Hardbacks” Long enjoyment books (including novels): `light_blue`
- “Small textbook” Beginner level textbooks, guides and martial arts books: `green`
- “Large textbook” Advanced level textbooks and advanced guides: `blue`
- Religious books: `dark_gray`
- “Printouts” (including spiral-bound, binders, and similar) Technical documents, (technical?)
  protocols, (lab) journals, personal diaries: `light_green`
- Other reading material/non-books (use only if every other category does not apply): `light_gray`

A few exceptions to this color key may apply, for example for books that don’t are what they seem to
be. Never use `yellow` and `red`, those colors are reserved for sounds and infrared vision.

#### CBMs

CBMs can be defined like this:

```json
"type" : "BIONIC_ITEM",         // Defines this as a CBM
...                             // same entries as above for the generic item.
                                // additional some CBM specific entries:
"bionic_id" : "bio_advreactor", // ID of the installed bionic if not equivalent to "id"
"difficulty" : 11,              // Difficulty of installing CBM
"is_upgrade" : true             // Whether the CBM is an upgrade of another bionic.
"installation_data" : "AID_bio_advreactor" // ID of the item which allows for almost guaranteed installation of corresponding bionic.
```

### Comestibles

```json
"type" : "COMESTIBLE",      // Defines this as a COMESTIBLE
...                         // same entries as above for the generic item.
                            // additional some comestible specific entries:
"addiction_type" : "crack", // Addiction type
"spoils_in" : 0,            // A time duration: how long a comestible is good for. 0 = no spoilage.
"use_action" : "CRACK",     // What effects a comestible has when used, see special definitions below
"stim" : 40,                // Stimulant effect
"fatigue_mod": 3,           // How much fatigue this comestible removes. (Negative values add fatigue)
"radiation": 8,             // How much radiation you get from this comestible.
"comestible_type" : "MED",  // Comestible type, used for inventory sorting
"quench" : 0,               // Thirst quenched
"heal" : -2,                // Health effects (used for sickness chances)
"addiction_potential" : 80, // Ability to cause addictions
"monotony_penalty" : 0,     // (Optional, default: 2) Fun is reduced by this number for each one you've consumed in the last 48 hours.
                            // Can't drop fun below 0, unless the comestible also has the "NEGATIVE_MONOTONY_OK" flag.
"calories" : 0,             // Hunger satisfied (in kcal)
"nutrition" : 0,            // Hunger satisfied (OBSOLETE)
"tool" : "apparatus",       // Tool required to be eaten/drank
"charges" : 4,              // Number of uses when spawned
"stack_size" : 8,           // (Optional) How many uses are in the above-defined volume. If omitted, is the same as 'charges'
"fun" : 50                  // Morale effects when used
"cooks_like": "meat_cooked" // (Optional) If the item is used in a recipe, replaces it with its cooks_like
"parasites": 10,            // (Optional) Probability of becoming parasitised when eating
"contamination": [ { "disease": "bad_food", "probability": 5 } ],         // (Optional) List of diseases carried by this comestible and their associated probability. Values must be in the [0, 100] range.
```

### Containers

```json
"type": "CONTAINER",  // Defines this as a container
...                   // same data as for the generic item (see above).
"contains": 200,      // How much volume this container can hold
"seals": false,       // Can be resealed, this is a required for it to be used for liquids. (optional, default: false)
"watertight": false,  // Can hold liquids, this is a required for it to be used for liquids. (optional, default: false)
"preserves": false,   // Contents do not spoil. (optional, default: false)
```

Alternately, every item can be used as container:

```json
"type": "ARMOR",      // Any type is allowed here
...                   // same data as for the type
"container_data" : {  // The container specific data goes here.
    "contains": 200,
}
```

This defines a armor (you need to add all the armor specific entries), but makes it usable as
container. It could also be written as a generic item ("type": "GENERIC") with "armor_data" and
"container_data" entries.

### Pockets (`pocket_data`)

Every item stores its contents in pockets. Existing item JSON needs no change: when an item has no
`pocket_data`, the game builds its pockets from the legacy fields (see the table below). New
items may define pockets directly with an optional `pocket_data` array on any item type. The format
is a subset of Cataclysm: DDA's, so DDA pocket definitions load, minus the ignored keys listed
below. If an item has both `pocket_data` and legacy container, holster, bandolier or magazine
fields, `pocket_data` wins and `--check-mods` warns.

```json
"pocket_data": [
  {
    "pocket_type": "CONTAINER",          // CONTAINER (default), MAGAZINE, MAGAZINE_WELL, MOD, CORPSE or SOFTWARE
    "max_contains_volume": "2 L",        // total volume of the contents
    "max_contains_weight": "10 kg",      // total weight of the contents
    "max_item_volume": "500 ml",         // largest single item (optional)
    "min_item_volume": "10 ml",          // smallest single item (optional)
    "watertight": true,                  // can hold liquids
    "airtight": false,                   // can hold gases
    "rigid": false,                      // the pocket doesn't grow with its contents
    "holster": false,                    // holds exactly one item (or one stack)
    "open_container": false,             // contents spill when the item is put in another container
    "moves": 100,                        // base moves to take an item out
    "spoil_multiplier": 1.0,             // 0 stops spoilage
    "weight_multiplier": 1.0,
    "volume_multiplier": 1.0,
    "fire_protection": false,
    "transparent": false,                // stored for pocket settings
    "sealed_data": { "spoil_multiplier": 0.0 },  // the pocket starts sealed; opening it unseals it
    "flag_restriction": [ "SHEATH_KNIFE" ],      // only items with one of these flags
    "item_restriction": [ "stanag30" ],          // only these item types
    "ammo_restriction": { "223": 30 },           // only this ammo, up to this many rounds per type
    "id": "ammo",                                // CDF: optional name of the pocket, used by `firing_requirements`, `consumption_per_use` and the mod fields `consumption_mods` / `capacity_mods`
    "default_magazine": "stanag30",              // MAGAZINE_WELL: the magazine a new item spawns with (default: the first `item_restriction` id)
    "allowed_speedloaders": [ "speedloader_9mm" ], // ammo items that fill this pocket as a speedloader (see below)
    "skill_restriction": [ "pistol" ],           // CDF: only guns using one of these skills
    "magazine_well": "100 ml",                   // volume of the contents that may stick out of the item
    "description": "A zipped pocket."
  }
]
```

`ammo_restriction` replaces the volume and weight limits. `max_item_length` and `min_item_length`
are lengths such as `"30 cm"` (units `mm`, `cm`, `m`, `km`); see "Item length" below.
`volume_encumber_modifier` takes effect with worn storage. `extra_encumbrance`, `ripoff`, `activity_noise`, `inherits_flags`,
`material_restriction`, `allowed_speedloaders` and `forbidden` are DDA keys that are loaded into
`pocket_data`; some of them have no consumer yet. `MIGRATION` pockets can't be defined in JSON: every item creates one when something
doesn't fit in its other pockets, and the game moves such items out (to the parent container, the
carrier, or the ground) the next time the pocket layout changes.

A `MAGAZINE` pocket is an integral magazine: it takes the ammo named in `ammo_restriction`, up to the
given count per ammo type, and it is part of the item (a gun or tool with a built-in magazine). A
`MAGAZINE_WELL` pocket takes magazines instead: `item_restriction` lists the magazine item ids it
accepts, `flag_restriction` can match a flag such as `BATTERY_MEDIUM`, and `default_magazine` is the
magazine a new item gets. A gun or tool can have several pockets of both kinds (several wells, or a
well and an integral magazine); a pocket that other fields refer to needs an `id`, which
`firing_requirements`, `consumption_per_use` and the `consumption_mods` / `capacity_mods` of mods
name (see "Gun", "Gunmod" and "Tools"). The size flags `BATTERY_ULTRA_LIGHT`, `BATTERY_LIGHT`,
`BATTERY_MEDIUM` and `BATTERY_HEAVY` mark which battery wells an item fits in.

A battery well names the size flags it accepts and a default cell, and nothing else:

```json
"pocket_data": [
  {
    "pocket_type": "MAGAZINE_WELL",
    "flag_restriction": [ "BATTERY_LIGHT", "BATTERY_ULTRA_LIGHT" ],
    "default_magazine": "light_battery_cell"
  }
]
```

A battery well holds the capacity of the cell in it, so the tool's charges are the cell's `capacity` (CDF
cell sizes: `light_battery_cell` 100, `medium_battery_cell` 500, `heavy_battery_cell` 1000,
`heavy_plus_battery_cell` 1500). The item carries no `ammo`, `max_charges` or `initial_charges` of its own.

`allowed_speedloaders` lists the ammo items that can fill the pocket as a speedloader. An ammo item
with the flag `SPEEDLOADER` empties its rounds into the pocket only if all of them fit;
with `SPEEDLOADER_CLIP` the pocket doesn't need room for all of them at once.

When a flag has a `restriction` string in `flags.json`, item info uses it to say what the pocket
accepts (for example "knives" for `SHEATH_KNIFE`).

#### Plate slots (ablative pockets)

`"ablative": true` makes the pocket a plate slot, as in DDA:

```json
{ "pocket_type": "CONTAINER", "ablative": true, "flag_restriction": [ "ABLATIVE_LARGE" ],
  "max_contains_volume": "2 L", "max_contains_weight": "5 kg", "max_item_length": "33 cm",
  "moves": 800 }
```

- It holds exactly one item and takes only items matching its `flag_restriction` or
  `item_restriction` (an ablative pocket without either is an error).
- It is never worn storage. Automatic placement (picking up, `best_pocket`, overflow) skips it;
  a plate is inserted on purpose with the normal "insert into container" action, or placed by item
  spawning and migration. `moves` is the time that costs.
- `volume_encumber_modifier` defaults to 0 (plates are armor, not stored volume).
- `sector` (`"any"` (default), `"front"`, `"back"`, `"left"`, `"right"`) names the side of the
  wearer the plate covers. It is read and stored for a future directional coverage model and has no
  effect yet.

An armor item can be a plate: give it `CANT_WEAR` and the size flag, and, with `non_functional` (an
item id) and `damage_verb` (default "is shattered"), say what it leaves behind when it is destroyed:

```json
"flags": [ "CANT_WEAR", "ABLATIVE_LARGE" ],
"non_functional": "destroyed_large_ceramic_plate",
"damage_verb": "makes a crunch, something has shifted"
```

The `non_functional` item takes the plate's place in the same slot, so it must carry the same size
flag (`ABLATIVE_LARGE` / `ABLATIVE_MEDIUM`) as the plate and be an armor item; the game reports a
leftover that doesn't. The leftover always goes back into the slot the plate was in. Without
`non_functional` a destroyed plate is simply removed, with the same "completely destroyed" message
as any armor.

Plates in a worn carrier mitigate damage before the carrier does, through the plate coverage
seam (`select_ablative_pocket`, `src/ablative_coverage.h`), which decides per hit which plate, if
any, takes it. Their coverage and encumbrance count for the carrier.

#### Pockets built from legacy fields

| Legacy source | Pocket |
|---|---|
| `container_data` / `"type": "CONTAINER"` | one CONTAINER pocket: volume `contains`, `watertight`, sealed from `seals`/`unseals_into`, `preserves` sets spoil multiplier 0, `rigid` from the item |
| `holster` use action | `multi` holster CONTAINER pockets: max item volume `max_volume`, min `min_volume`, weight `max_weight`, `flags` become `flag_restriction`, `skills` become `skill_restriction`, `draw_cost` becomes `moves` |
| `bandolier` use action | one CONTAINER pocket with `ammo_restriction` |
| MAGAZINE | one MAGAZINE pocket with `ammo_restriction` from the magazine's ammo and capacity |
| tool with `tool_ammo` (or `ammo`) and `max_charges`, and no `magazines` | one integral MAGAZINE pocket, `ammo_restriction` of `max_charges` per ammo type; the game also reports that the item needs `pocket_data` |
| gun or tool with `magazines` | one MAGAZINE_WELL pocket, `item_restriction` from the magazines |
| gun or tool that takes mods | one MOD pocket |
| corpse | one CORPSE pocket |
| `usb_drive` | one SOFTWARE pocket |

A gun's `clip_size` makes no pocket: a gun with an integral magazine defines a MAGAZINE pocket in
`pocket_data`. Armor `storage` isn't converted yet. A gunmod with `magazine_adaptor` replaces the magazines the
MAGAZINE_WELL accepts while it is installed.

### Melee

```json
"id": "hatchet",       // Unique ID. Must be one continuous word, use underscores if necessary
"symbol": ";",         // ASCII character used in-game
"color": "light_gray", // ASCII character color
"name": "hatchet",     // In-game name displayed
"description": "A one-handed hatchet. Makes a great melee weapon, and is useful both for cutting wood, and for use as a hammer.", // In-game description
"price": 95,           // Used when bartering with NPCs.  Can use string "cent" "USD" or "kUSD".
"material": ["iron", "wood"], // Material types.  See materials.json for possible options
"weight": 907,         // Weight, measured in grams
"volume": "1500 ml",   // Volume, volume in ml and L can be used - "50 ml" or "2 L"
"bashing": 12,         // Bashing damage caused by using it as a melee weapon
"cutting": 12,         // Cutting damage caused by using it as a melee weapon
"flags" : ["CHOP"],    // Indicates special effects
"to_hit": 1            // To-hit bonus if using it as a melee weapon
```

#### Melee `Weapon_category`

| Weapon Category | Description                                                                              |
| --------------- | ---------------------------------------------------------------------------------------- |
| FIST_WEAPONS    | Handheld weapons used to supplement fists in martial arts.                               |
| ---             | ---                                                                                      |
| STILETTOS       | Short, tapering 'blade' fixed onto a handle, has no cutting edge                         |
| KNIVES          | Short blade fixed onto a handle, for cutting or as weapon.                               |
| SHORT_SWORDS    | One handed sword of length between a large knife and a 'proper' sword.                   |
| 1H_SWORDS       | Sword meant to be wielded with one hand.                                                 |
| 2H_SWORDS       | Sword meant to be wielded with both hands.                                               |
| DUELING_SWORDS  | Swords with thin profiles typically meant for stabbing.                                  |
| BIONIC_SWORDS   | Swords integrated into the body via bionics.                                             |
| ---             | ---                                                                                      |
| SAPS            | Very short length of typically flexible material, with a weighted tip.                   |
| BATONS          | Thin, balanced rod of strong material.                                                   |
| TONFAS          | Unique looking T-shaped baton believed to originate from China.                          |
| CLUBS           | Rod with a thicker striking head. May be one or two-handed.                              |
| QUARTERSTAVES   | Long pole wielded with both hands.                                                       |
| MACES           | Short one-handed weapon with a striking head firmly attached to a short handle.          |
| MORNINGSTARS    | Typically two-handed weapon with a striking head firmly attached to a long handle.       |
| FLAILS          | Striking head attached to handle by flexible rope/chain.                                 |
| 1H_HAMMERS      | Hammers meant to be wielded with a single hand.                                          |
| 2H_HAMMERS      | Hammers meant to be wielded with both hands.                                             |
| ---             | ---                                                                                      |
| HAND_AXES       | Axe with a short handle, typically wielded in one hand, ocassionally thrown.             |
| 1H_AXES         | Axes meant to be wielded with one hand, typically with a handle longer than the handaxe. |
| 2H_AXES         | Axes meant to be wielded with two hands.                                                 |
| ---             | ---                                                                                      |
| WHIPS           | Flexible tool used to strike at range.                                                   |
| ---             | ---                                                                                      |
| 1H_HOOKED       | One handed weapon with hooking capability. (and isn't an axe or hammer)                  |
| ---             | ---                                                                                      |
| HOOKED_POLES    | Polearm with hooked end (Like a shepherd's crook)                                        |
| SPEARS          | Polearm with a long shaft and a sharp tip made of hard material.                         |
| PIKES           | Very long spear that can only be wielded in two hands, very unwieldy.                    |
| GLAIVES         | Polearm with a single-edged blade mounted on the end.                                    |

| Weapon Styles   | Description                                   |
| --------------- | --------------------------------------------- |
| MEDIEVAL_SWORDS | Swords associated with European culture.      |
| JAPANESE_SWORDS | Swords associated with Japanese culture.      |
| BIONIC_WEAPONRY | Weapons integrated into the body via bionics. |

### Gun

Guns can be defined like this:

```json
"type": "GUN",             // Defines this as a GUN
...                        // same entries as above for the generic item.
                           // additional some gun specific entries:
"skill": "pistol",         // Skill used for firing
"ammo": [ "357", "38" ],   // Ammo types accepted for reloading
"ranged_damage": 0,        // Ranged damage when fired
"range": 0,                // Range when fired
"dispersion": 32,          // Inaccuracy of gun, measured in quarter-degrees
// When sight_dispersion and aim_speed are present in a gun mod, the aiming system picks the "best"
// sight to use for each aim action, which is the fastest sight with a dispersion under the current
// aim threshold.
"sight_dispersion": 10,    // Inaccuracy of gun derived from the sight mechanism, also in quarter-degrees
"aim_speed": 3,            // A measure of how quickly the player can aim, in moves per point of dispersion.
"recoil": 0,               // Recoil caused when firing, in quarter-degrees of dispersion.
"durability": 8,           // Resistance to damage/rusting, also determines misfire chance
"blackpowder_tolerance": 8,// One in X chance to get clogged up (per shot) when firing blackpowder ammunition (higher is better). Optional, default is 8.
"min_cycle_recoil": 0,     // Minimum ammo recoil for gun to be able to fire more than once per attack.
"burst": 5,                // Number of shots fired in burst mode
"modes": [                 // Firing modes on this gun; cannot be combined with `burst`
  [ "DEFAULT", "semi-auto", 1 ], // Mode id (generally used for NPC choices: AUTO, BURST or DEFAULT), the name shown in the menu, number of rounds fired
  [ "AUTO", "auto", 4 ]
],
"handling": 15,            // Modifier in how quick the gun recovers from recoil. 20 is baseline, below that is worse and above is better. Scales with '2 - 2.0 => 0' by default, for example 15 handling would be '2 - 1.5 => 0.5' increase to recoil and 30 would be '2 - 3.0 => -1' reduction to recoil
"clip_size": 100,          // Maximum amount of ammo that can be loaded
"ups_charges": 0,          // Additionally to the normal ammo (if any), a gun can require some charges from an UPS. This also works on mods. Attaching a mod with ups_charges will add/increase ups drain on the weapon.
"energy_drain": "2 kJ",    // Electric energy a shot uses, additionally to the normal ammo (if any). Gunmods change it with `energy_drain_modifier` and `energy_drain_multiplier`. CDF also keeps `ups_charges` for UPS charges.
"gun_jam_mult": 1.25,      // Multiplier (0 or more, default 1.0) for the chance that the gun jams because it is damaged. Higher is worse. The jam chance per shot is roughly 0.05% for an undamaged gun, 3% at damage 1, 15% at damage 2, 45% at damage 3 and 80% at damage 4, times this value, added to the magazine's chance (see `mag_jam_mult`). A jam costs the shot. 0 removes the damage-based chance of the gun.
"ammo_to_fire" 1,          // Amount of ammo used per shot, separate from any UPS cost that may be given to the weapon.
// The legacy item flags `FIRE_20`, `FIRE_50`, and `FIRE_100` are still permitted and will override `ammo_to_fire` if present.
"reload": 450,             // Amount of time to reload, 100 = 1 second = 1 "turn". Default 100.
"firing_requirements": {   // Optional. What each firing mode takes from which pocket (see "Pockets"), for guns with several ammo or power pockets
  "DEFAULT": [ { "pocket": "ammo", "qty": 1 }, { "pocket": "battery", "qty": 5 } ],
  "BURST":   [ { "pocket": "ammo", "qty": 3 }, { "pocket": "battery", "qty": 15 } ]
},                         // `pocket` is a `pocket_data` `id` on a MAGAZINE_WELL pocket or on an integral MAGAZINE pocket with `ammo_restriction`; `qty` is 1 or more. Every mode in `modes` needs an entry, and each key must be one of the modes. Cannot be combined with non-zero `ups_charges` or `energy_drain`, or a non-default `ammo_to_fire`. A mode a gunmod adds with `mode_modifier` uses the gunmod's `mode_firing_requirements`, or the DEFAULT cost if it has none.
"reload_noise_volume": 6   // [DEPRECIATED] How loud is reloading the gun, in tile distance. This is depreciated, use reload_noise_volume_dB instead. This value will be converted to an appropiate dB volume if provided.
"reload_noise_volume_dB": 40, // How loud is reloading the gun, in dB spl @1 meter reference. Default 40dB. Normal conversation is ~60dB, deafening sounds are 120dB+
"built_in_mods": ["m203"], // An array of mods that will be integrated in the weapon using the IRREMOVABLE tag.
"default_mods": ["m203"]   // An array of mods that will be added to a weapon on spawn.
"barrel_volume": "30 mL",  // Amount of volume lost when the barrel is sawn. Approximately 250 ml per IRL inch is a decent approximation.
"barrel_length": "30 mL",  // Depreciated alias of barrel_volume, which should be used instead for clarity.
"valid_mod_locations": [ [ "accessories", 4 ], [ "grip", 1 ] ],  // The valid locations for gunmods and the mount of slots for that location.
"loudness_modifier": 4,    // Optional field increasing or decreasing base ammo loudness, measured in dB spl.
"speed": 100               // Optional field increasing or decreasing base ammo speed in meters per second. Speed of sound is taken at 343 meters per second.
```

Alternately, every item (book, tool, armor, even food) can be used as gun if it has gun_data:

```json
"type": "TOOL",      // Or any other item type
...                   // same entries as for the type (e.g. same entries as for any tool),
"gun_data" : {        // additionally the same gun data like above
    "skill": ...,
    "recoil": ...,
    ...
}
```

#### Ranged `Weapon_category`

| Weapon Category   | Description                                                                                                                 |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------- |
| BOWS              | Elastic launching device for long-shafted projectiles.                                                                      |
| S_XBOWS           | Elastic launching device mounted on a frame to be triggered, pistol sized.                                                  |
| M_XBOWS           | Elastic launching device mounted on a frame to be triggered.                                                                |
| SLINGSHOTS        | Elastic, handheld launching device typically used for small round projectiles.                                              |
| SLINGS            | Projectile weapon using a cradle connected to two retention cords, used to fling blunt projectiles.                         |
| ---               | ---                                                                                                                         |
| PISTOLS           | Handgun with a chamber integral to the gun barrel. In-game, any handgun that isn't a revolver goes here.                    |
| REVOLVERS         | Repeating handgun with a revolving cylinder containing multiple chambers.                                                   |
| SUBMACHINE_GUNS   | Magazine fed automatic carbine designed to fire handgun cartridges.                                                         |
| RIFLES            | Long barrelled firearms designed for more accurate shooting.                                                                |
| MACHINE_GUNS      | Fully automatic autoloading firearm designed for sustained fire.                                                            |
| GATLING_GUNS      | Rapid firing multi barrel firearm.                                                                                          |
| SHOTGUNS          | Long barreled firearm generally designed to fire shotshells.                                                                |
| ---               | ---                                                                                                                         |
| GRENADE_LAUNCHERS | Firearm designed to propel large caliber projectile typically loaded with warhead of some kind (smoke, gas, explosive, etc) |
| ROCKET_LAUNCHERS  | Firearm that propels unguided, rocket-propelled projectile.                                                                 |
| ---               | ---                                                                                                                         |
| FLAMETHROWERS     | Ranged incendiary device designed to propel a controllable jet of fire.                                                     |
| WATER_CANNONS     | It fires water at your enemies.                                                                                             |
| SPRAY_GUNS        | It spews chemicals at your enemies.                                                                                         |

| Action category | Description                                                                                                       |
| --------------- | ----------------------------------------------------------------------------------------------------------------- |
| 1SHOT           | Ranged weapon with at least one barrel but no loading system/magazine.                                            |
| AUTOLOADING     | Ranged weapon with autoloading mechanisms like blowback, gas-operated, or recoil operated systems.                |
| MANUAL_ACTION   | Ranged weapon using manual actions like bolt/pump/lever.                                                          |
| ENERGY_WEAPONS  | Weapon designed to utilize focused energy (sonic, electromagnetic waves, particle beams, etc). Both Ranged/Melee. |
| MAGNETIC        | Weapon that propels payload via electromagnetism.                                                                 |
| PNEUMATIC       | Ranged weapon that propels payload via compressed air.                                                            |
| ELASTIC         | Ranged weapon that propels payload via elastic band.                                                              |

### Gunmod

Gun mods can be defined like this:

```json
"type": "GUNMOD",              // Defines this as a GUNMOD
...                            // Same entries as above for the generic item.
                               // Additionally some gunmod specific entries:
"location": "stock",           // Mandatory. Where is this gunmod is installed?
"mod_targets": [ "crossbow" ], // Optional. What specific weapons can this gunmod be used with?
"mod_target_category": [ [ "BOWS" ] ], // Optional. Weapon category requirements. Inner arrays are AND, outer array is OR. E.g. [["RIFLES","AUTOLOADING"]] = RIFLE AND AUTOLOADING; [["PISTOLS"],["REVOLVERS"]] = PISTOL OR REVOLVER.
"mod_exclusions": [ "laser_rifle" ], // Optional. What specific weapons can't this gunmod be used with?
"mod_exclusion_category": [ [ "ENERGY_WEAPONS" ] ], // Optional. Excluded weapon categories. Same logic as mod_target_category - matching any inner array prevents installation.
"acceptable_ammo": [ "9mm" ],  // Optional filter restricting mod to guns with those base (before modifiers) ammo types
"install_time": "30 s",        // Optional time installation takes. Installation is instantaneous if unspecified. An integer will be read as moves or a time string can be used.
"ammo_modifier": [ "57" ],     // Optional field which if specified modifies parent gun to use these ammo types
"magazine_adaptor": [ [ "223", [ "stanag30" ] ] ], // Optional. Array of [ammotype, [...magazines]] pairs. Overrides the weapon's compatible magazines for that ammo type.
"pocket_mods": [ { "pocket_type": "MAGAZINE_WELL", "item_restriction": [ "ai_338mag", "ai_338mag_10" ] } ], // Optional. Changes the pockets of the host: same syntax as `pocket_data`; pockets of type MAGAZINE and MAGAZINE_WELL replace the host's magazine wells, pockets of type CONTAINER are added. For MAGAZINE and MAGAZINE_WELL, give `ammo_modifier` and `magazine_adaptor` too so the ammo type moves along. Also read for `"type": "TOOLMOD"`.
"consumption_mods": [ { "pocket": "power", "multiply": 0.5 }, { "pocket": "ammo", "set": 2 } ], // Optional (GUNMOD or TOOLMOD). Changes how much the host takes per shot or use from a pocket, by `pocket_data` `id`. `set` (1 or more) replaces the quantity; `multiply` (more than 0) scales it. Of all installed mods the smallest `set` wins, then the product of all `multiply` applies.
"capacity_mods": [ { "pocket": "ammo", "multiply": 1.5 } ], // Optional (GUNMOD or TOOLMOD). Scales the ammo capacity of an integral MAGAZINE pocket of the host, by `pocket_data` `id`; `multiply` is more than 0. A MAGAZINE_WELL pocket is ignored (its capacity is the loaded magazine's). Several mods on one pocket multiply together.
"mode_modifier": [ [ "AUTO", "auto", 5 ] ],       // Optional. Array of [mode_id, mode_name, burst_size, [...flags]?] arrays. Adds firing modes to the weapon. Optional flags array can include "MELEE", "REACH_ATTACK", etc.
"mode_firing_requirements": { "REGULATED": [ { "pocket": "ammo", "qty": 1 }, { "pocket": "power", "qty": 1 } ] }, // Optional. What the modes added by `mode_modifier` take from which pocket of the host gun, keyed by mode id; `pocket` is a `pocket_data` `id` of the host. A mode added without an entry costs the host's DEFAULT. Not the same as the gun's own `firing_requirements`.
"hide_modes": [ "DEFAULT" ],               // Optional. Firing-mode ids removed from the final merged mode set when this gunmod is installed. Global, id-based kill switch (can also hide a mode another mod added). To replace a host mode, add a new id via `mode_modifier` and hide the original. A gunmod cannot both add and hide the same id.
"damage_modifier": -1,         // Optional field increasing or decreasing base gun damage
"dispersion_modifier": 15,     // Optional field increasing or decreasing base gun dispersion
"loudness_modifier": 4,        // Optional field increasing or decreasing base ammo loudness, measured in dB spl.
"speed": 100,                  // Optional field increasing or decreasing base ammo speed in meters per second. Speed of sound is taken at 343 meters per second.
"speed": 100,                  // Optional field increasing or decreasing base ammo speed in meters per second. Speed of sound is taken at 343 meters per second.
"range_modifier": 2,           // Optional field increasing or decreasing base gun range
"recoil_modifier": -100,       // Optional field increasing or decreasing base gun recoil
"ups_charges_modifier": 200,   // Optional field increasing or decreasing base gun UPS consumption (per shot) by adding given value
"ups_charges_multiplier": 2.5, // Optional field increasing or decreasing base gun UPS consumption (per shot) by multiplying by given value
"energy_drain_modifier": "200 kJ", // Optional field increasing or decreasing base gun energy consumption (`energy_drain`, per shot) by adding given value. This addition is not multiplied by `energy_drain_multiplier`.
"energy_drain_multiplier": 2.5,   // Optional field increasing or decreasing base gun energy consumption (`energy_drain`, per shot) by multiplying by given value.
"ammo_to_fire_modifier": 200,   // Optional field increasing or decreasing amount of main ammo consumed per shot by adding given value
"ammo_to_fire_multiplier": 2.5, // Optional field increasing or decreasing main ammo consumed per shot by multiplying by given value
"reload_modifier": -10,        // Optional field increasing or decreasing base gun reload time in percent
"min_str_required_mod": 14,    // Optional field increasing or decreasing minimum strength required to use gun
"weight_multiplier": 0.75,     // Optional field increasing or decreasing the weight of the parent gun by multiplying. 1.0 means no change, 0.75 means 25% lighter.
"volume_multiplier": 0.67,     // Optional field increasing or decreasing the volume of the parent gun by multiplying. 1.0 means no change, 0.67 means ~33% reduction. Replaces the deprecated COLLAPSIBLE_STOCK flag.
```

Alternately, every item (book, tool, armor, even food) can be used as a gunmod if it has
gunmod_data:

```json
"type": "TOOL",       // Or any other item type
...                   // same entries as for the type (e.g. same entries as for any tool),
"gunmod_data" : {
    "location": ...,
    "mod_targets": ...,
    ...
}
```

A `"type": "TOOLMOD"` item reads the fields that are not gun-specific: `ammo_modifier`,
`acceptable_ammo`, `magazine_adaptor`, `pocket_mods`, `consumption_mods` and `capacity_mods`.

### Batteries

```json
"type": "BATTERY",    // Defines this as a BATTERY
...                   // Same entries as above for the generic item
                      // Additionally some battery specific entries:
"max_energy": "30 kJ" // Mandatory. Maximum energy quantity the battery can hold
```

### Tools

```json
"id": "torch_lit",    // Unique ID. Must be one continuous word, use underscores if necessary
"type": "TOOL",       // Defines this as a TOOL
"symbol": "/",        // ASCII character used in-game
"color": "brown",     // ASCII character color
"name": "torch (lit)", // In-game name displayed
"description": "A large stick, wrapped in gasoline soaked rags. This is burning, producing plenty of light", // In-game description
"price": 0,           // Used when bartering with NPCs.  Can use string "cent" "USD" or "kUSD".
"material": "wood",   // Material types.  See materials.json for possible options
"techniques": [ "FLAMING" ], // Combat techniques used by this tool
"flags": "FIRE",      // Indicates special effects
"weight": 831,        // Weight, measured in grams
"volume": "1500 ml",  // Volume, volume in ml and L can be used - "50 ml" or "2 L"
"bashing": 12,        // Bashing damage caused by using it as a melee weapon
"cutting": 0,         // Cutting damage caused by using it as a melee weapon
"to_hit": 3,          // To-hit bonus if using it as a melee weapon
"max_charges": 75,    // Maximum charges tool can hold
"initial_charges": 75, // Charges when spawned
"rand_charges": [10, 15, 25], // Randomize the charges when spawned. This example has a 50% chance of rng(10, 15) charges and a 50% chance of rng(15, 25) (The endpoints are included)
"sub": "hotplate",    // optional; this tool has the same functions as another tool
"charge_factor": 5,   // this tool uses charge_factor charges for every charge required in a recipe; intended for tools that have a "sub" field but use a different ammo that the original tool
"charges_per_use": 1, // Charges consumed per tool use
"consumption_per_use": [ { "pocket": "oxygen", "qty": 2 }, { "pocket": "fuel", "qty": 1 } ], // Optional. What one use takes from which pocket, for tools with several ammo or power pockets. `pocket` is a `pocket_data` `id` on a MAGAZINE_WELL pocket or on an integral MAGAZINE pocket with `ammo_restriction`; `qty` is 1 or more. Cannot be combined with `charges_per_use` or `power_draw`.
"legacy_charges_per_use_factor": 1, // Optional, default 1. Integer, 1 or more; a lower value is reported as a definition error by the item check. Set on multimag tools converted from a legacy `charges_per_use=N` (N>1) so existing recipes that pass raw charge counts get translated to uses (uses = qty / factor). Non-divisible requests trigger a debugmsg and consume nothing.
"turns_per_charge": 20, // Charges consumed over time, deprecated in favor of power_draw
"power_draw": 50,       // Energy consumption rate in mW
"ups_eff_mult": 2,       // Multiplier for UPS efficiency
"ups_recharge_rate": 10,       // The power output for UPS recharging of items
"tool_ammo": [ "NULL" ],  // Ammo types used for reloading. CDF: the Bright Nights key "ammo" is still read as an alias for it
"revert_to": "torch_done", // Transforms into item when charges are expended
"use_action": "firestarter" // Action performed when tool is used, see special definition below
```

### Seed Data

Every item type can have optional seed data, if the item has seed data, it's considered a seed and
can be planted:

```json
"seed_data" : {
    "fruit": "weed", // The item id of the fruits that this seed will produce.
    "seeds": false, // (optional, default is true). If true, harvesting the plant will spawn seeds (the same type as the item used to plant). If false only the fruits are spawned, no seeds.
    "fruit_div": 2, // (optional, default is 1). Final amount of fruit charges produced is divided by this number. Works only if fruit item is counted by charges.
    "byproducts": ["withered", "straw_pile"], // A list of further items that should spawn upon harvest.
    "harvest_inherit_flags": ["HIDDEN_POISON", "HIDDEN_HALLU"], // Optional, default: []. Flags present on the planted item to copy to harvested fruits and seeds, excluding byproducts. Works with any required_terrain_flag.
    "harvest_inherit_poison": true, // Optional, default: false. Copy the planted item's poison value to harvested fruits and seeds, excluding byproducts. Independent of flag inheritance.
    "plant_name": "sunflower", // The name of the plant that grows from this seed. This is only used as information displayed to the user.
    "grow" : 91 // A time duration: how long it takes for a plant to fully mature. Based around a 91 day season length (roughly a real world season) to give better accuracy for longer season lengths
                // Note that growing time is later converted based upon the season_length option, basing it around 91 is just for accuracy purposes
                // A value 91 means 3 full seasons, a value of 30 would mean 1 season.
    "required_terrain_flag": "PLANTABLE" // A tag that terrain and furniture would need to have in order for the seed to be plantable there.
					 // Default is "PLANTABLE", and using this will cause any terain the plant is wrown on to turn into dirt once the plant is planted, unless furniture is used.
					 // Using any other tag will not turn the terrain into dirt.
}
```

### Brewing Data

Every item type can have optional brewing data, if the item has brewing data, it can be placed in a
vat and will ferment into a different item type.

Currently only vats can only accept and produce liquid items.

```json
"brewable" : {
    "time": 3600, // A time duration: how long the fermentation will take.
    "result": "beer" // The id of the result of the fermentation.
}
```

### Relic Data

Defines various (semi-)magical properties of items. See RELICS.md for

### Artifact Data

Artifacts are getting deprecated in favor of relic, avoid using them.

#### `Effects_carried`

(optional, default: empty list)

Effects of the artifact when it's in the inventory (main inventory, wielded, or worn) of the player.

Possible values (see src/enums.h for an up-to-date list):

- `AEP_STR_UP` Strength + 4
- `AEP_DEX_UP` Dexterity + 4
- `AEP_PER_UP` Perception + 4
- `AEP_INT_UP` Intelligence + 4
- `AEP_ALL_UP` All stats + 2
- `AEP_SPEED_UP` +20 speed
- `AEP_IODINE` Reduces radiation
- `AEP_SNAKES` Summons friendly snakes when you're hit
- `AEP_INVISIBLE` Makes you invisible
- `AEP_CLAIRVOYANCE` See through walls
- `AEP_SUPER_CLAIRVOYANCE` See through walls to a great distance
- `AEP_STEALTH` Your steps are quieted
- `AEP_EXTINGUISH` May extinguish nearby flames
- `AEP_GLOW` Four-tile light source
- `AEP_PSYSHIELD` Protection from fear paralyze attack
- `AEP_RESIST_ELECTRICITY` Protection from electricity
- `AEP_CARRY_MORE` Increases carrying capacity by 200
- `AEP_SAP_LIFE` Killing non-zombie monsters may heal you
- `AEP_HUNGER` Increases hunger
- `AEP_THIRST` Increases thirst
- `AEP_SMOKE` Emits smoke occasionally
- `AEP_EVIL` Addiction to the power
- `AEP_SCHIZO` Mimicks schizophrenia
- `AEP_RADIOACTIVE` Increases your radiation
- `AEP_MUTAGENIC` Mutates you slowly
- `AEP_ATTENTION` Draws netherworld attention slowly
- `AEP_STR_DOWN` Strength - 3
- `AEP_DEX_DOWN` Dex - 3
- `AEP_PER_DOWN` Per - 3
- `AEP_INT_DOWN` Int - 3
- `AEP_ALL_DOWN` All stats - 2
- `AEP_SPEED_DOWN` -20 speed
- `AEP_FORCE_TELEPORT` Occasionally force a teleport
- `AEP_MOVEMENT_NOISE` Makes noise when you move
- `AEP_BAD_WEATHER` More likely to experience bad weather
- `AEP_SICK` Decreases health over time

#### `effects_worn`

(optional, default: empty list)

Effects of the artifact when it's worn (it must be an armor item to be worn).

Possible values are the same as for effects_carried.

#### `effects_wielded`

(optional, default: empty list)

Effects of the artifact when it's wielded.

Possible values are the same as for effects_carried.

#### `effects_activated`

(optional, default: empty list)

Effects of the artifact when it's activated (which require it to have a `"use_action": "ARTIFACT"`
and it must have a non-zero max_charges value).

Possible values (see src/artifact.h for an up-to-date list):

- `AEA_STORM` Emits shock fields
- `AEA_FIREBALL` Targeted
- `AEA_ADRENALINE` Adrenaline rush
- `AEA_MAP` Maps the area around you
- `AEA_BLOOD` Shoots blood all over
- `AEA_FATIGUE` Creates interdimensional fatigue
- `AEA_ACIDBALL` Targeted acid
- `AEA_PULSE` Destroys adjacent terrain
- `AEA_HEAL` Heals minor damage
- `AEA_CONFUSED` Confuses all monsters in view
- `AEA_ENTRANCE` Chance to make nearby monsters friendly
- `AEA_BUGS` Chance to summon friendly insects
- `AEA_TELEPORT` Teleports you
- `AEA_LIGHT` Temporary light source
- `AEA_GROWTH` Grow plants, a la triffid queen
- `AEA_HURTALL` Hurts all monsters!
- `AEA_FUN` Temporary morale bonus
- `AEA_SPLIT` Split between good and bad
- `AEA_RADIATION` Spew radioactive gas
- `AEA_PAIN` Increases player pain
- `AEA_MUTATE` Chance of mutation
- `AEA_PARALYZE` You lose several turns
- `AEA_FIRESTORM` Spreads minor fire all around you
- `AEA_ATTENTION` Attention from sub-prime denizens
- `AEA_TELEGLOW` Teleglow disease
- `AEA_NOISE` Loud noise
- `AEA_SCREAM` Noise & morale penalty
- `AEA_DIM` Darkens the sky slowly
- `AEA_FLASH` Flashbang
- `AEA_VOMIT` User vomits
- `AEA_SHADOWS` Summon shadow creatures
- `AEA_STAMINA_EMPTY` Empties most of the player's stamina gauge

### Software Data

Every item type can have software data, it does not have any behavior:

```json
"software_data" : {
    "type": "USELESS", // unused
    "power" : 91 // unused
}
```

### Fuel data

Every item type can have fuel data that determines how much horse power it produces per unit
consumed. Currently, gasses and plasmas cannot really be fuels.

If a fuel has the PERPETUAL flag, engines powered by it never use any fuel. This is primarily
intended for the muscle pseudo-fuel, but mods may take advantage of it to make perpetual motion
machines.

```json
"fuel" : {
    "energy": 34.2,               // battery charges per mL of fuel. batteries have energy 1
                                 // is also MJ/L from https://en.wikipedia.org/wiki/Energy_density
                                 // assumes stacksize 250 per volume 1 (250mL). Multiply
                                 // by 250 / stacksize * volume for other stack sizes and
                                 // volumes
   "pump_terrain": "t_gas_pump", // optional. terrain id for the fuel's pump, if any.
   "explosion_data": {           // optional for fuels that can cause explosions
        "chance_hot": 2,         // 1 in chance_hot of explosion when attacked by HEAT weapons
        "chance_cold": 5,        // 1 in chance_cold of explosion when attacked by other weapons
        "factor": 1.0,           // explosion factor - larger numbers create more powerful explosions
        "fiery": true,           // true for fiery explosions
        "size_factor": 0.1       // size factor - larger numbers make the remaining fuel increase explosion power more
    }
}
```

### Use Actions

The contents of use_action fields can either be a string indicating a built-in function to call when
the item is activated (defined in iuse.cpp), or one of several special definitions that invoke a
more structured function.

All object defined use actions support the following two types.

```jsonc
"use_action": {
  "menu_text": "xyz", // What string is shown in the activate menu
  // Unique key for the iuse, defaults to the `type`
  // Note: This should only be used on repeated type definitions
  // WARN: This does not work on `repair_item` iuse actors -> they have their own special `item_action_type`
  "internal_name": "test"
}
```

```json
"use_action": {
    "type": "transform",  // The type of method, in this case one that transforms the item.
    "target": "gasoline_lantern_on", // The item to transform to.
    "active": true,       // Whether the item is active once transformed.
    "msg": "You turn the lamp on.", // Message to display when activated.
    "need_fire": 1,                 // Whether fire is needed to activate.
    "need_fire_msg": "You need a lighter!", // Message to display if there is no fire.
    "transform_charges": 1,         // Number of charges used by item when it transforms.
    "need_charges": 1,                      // Number of charges the item needs to transform. Just a check, nothing is consumed.
    "need_charges_msg": "The lamp is empty.", // Message to display if there aren't enough charges.
    "need_worn": true,                        // Whether the item needs to be worn to be transformed, is false by default.
    "need_wielding": false,             // Whether the item needs to be wielded to be transformed, false by default.
    "need_dry": false,                  // Whether the item cannot transform while submerged in water, false by default.
    "target_charges" : 3, // Number of charges the transformed item has.
    "rand_target_charges": [10, 15, 25], // Randomize the charges the transformed item has. This example has a 50% chance of rng(10, 15) charges and a 50% chance of rng(15, 25) (The endpoints are included)
    "set_timer": false,   // Query the player on activation how many charges the transformed item should have, e.g. C-4. False by default.
    "container" : "jar",  // Container holding the target item.
    "moves" : 500         // Moves required to transform the item in excess of a normal action.
},
"use_action": {
    "type": "explosion", // An item that explodes when it runs out of charges.
    "sound_volume": 0, // Volume of a sound the item makes every turn.
    "sound_msg": "Tick.", // Message describing sound the item makes every turn.
    "no_deactivate_msg": "You've already pulled the %s's pin, try throwing it instead.", // Message to display if the player tries to activate the item, prevents activation from succeeding if defined.
    "explosion": { // Optional: physical explosion data
        // Specified like `"explosion"` field in generic items
    },
    "draw_explosion_radius" : 5, // How large to draw the radius of the explosion.
    "draw_explosion_color" : "ltblue", // The color to use when drawing the explosion.
    "do_flashbang" : true, // Whether to do the flashbang effect.
    "flashbang_player_immune" : true, // Whether the player is immune to the flashbang effect.
    "fields_radius": 3, // The radius of spread for fields produced.
    "fields_type": "fd_tear_gas", // The type of fields produced.
    "fields_min_density": 3,
    "fields_max_density": 3,
    "emp_blast_radius": 4,
    "scrambler_blast_radius": 4
},
"use_action": {
    "type": "change_scent", // Change the scent type of the user.
    "scent_typeid": "sc_fetid", // The scenttype_id of the new scent.
    "charges_to_use": 2, // Charges consumed when the item is used.  (Default: 1)
    "scent_mod": 150, // Modifier added to the scent intensity.  (Default: 0)
    "duration": "6 m", // How long does the effect last.
    "effects": [ { "id": "fetid_goop", "duration": 360, "bp": "TORSO" } ], // List of effects with their id, duration, and bodyparts
    "waterproof": true, // Is the effect waterproof.  (Default: false)
    "moves": 500 // Number of moves required in the process.
},
"use_action": {
    "type": "unfold_vehicle", // Transforms the item into a vehicle.
    "vehicle_name": "bicycle", // Vehicle name to create.
    "unfold_msg": "You painstakingly unfold the bicycle and make it ready to ride.", // Message to display when transforming.
    "full_battery": true, // If set to true, vehicle will be created with full fuel and batteries. It's recommended you only use this if the vehicle being deployed is made entirely from foldable parts, as those will convert into a generic folded vehicle item that tracks its status when taken down.  (Default: false)
    "moves": 500 // Number of moves required in the process.
},
"use_action" : {
    "type" : "consume_drug", // A drug the player can consume.
    "activation_message" : "You smoke your crack rocks.  Mother would be proud.", // Message, ayup.
    "effects" : { "high": 15 }, // Effects and their duration.
    "stat_adjustments": {"hunger" : -10}, // Adjustment to make to player stats.
    "fields_produced" : {"cracksmoke" : 2}, // Fields to produce, mostly used for smoke.
    "charges_needed" : { "fire" : 1 }, // Charges to use in the process of consuming the drug.
    "tools_needed" : { "apparatus" : -1 }, // Tool needed to use the drug.
    "fake_item" : "fake_ecig", // Item used to fake addiction and copy other consumption effects from.
    "lightweight_mod" : 1.2, // Drug duration modifier if the user has LIGHTWEIGHT trait.
    "tolerance_mod" : .8, // Drug duration modifier if the user has a tolerance.
    "tolerance_lightweight_effected": true, // Should this drugs time duration be effected by tolerance/lightweight?
    "too_much_threshold": 10, // Number of minutes to use for the too much calculation.
    "addiction_type_too_much": [["cig", "nicotine"]], // Link an effect to an addiction. Used for 'you feel gross... too much' message
    "lit_item": "cigar_lit", // Defines an item to turn into for smoking_duration minutes.
    "smoking_duration": 12, // Number of minutes the lit_item will remain lit, afterwards it will turn into the extinguished item.
    "used_up_item": "", // Item to give once used, alternative to lit_item which should be used in tandem with smoking_duration.
    "do_weed_msg": false, // Should weed_msg(player) be called after using the item?
    "snippet_category": "killer_withdrawal", // Chance (1 in 5) to say a random snippet from this category upon use. Like do_weed_msg, but unhardcoded.
    "snippet_chance": 5, // Chance (1 in snippet_chance) to make the player think a snippet.
    "moves": 50 // Number of moves required in the process, default value is 100.
},
"use_action": {
    "type": "place_monster", // place a turret / manhack / whatever monster on the map
    "monster_id": "mon_manhack", // monster id, see monsters.json
    "difficulty": 4, // difficulty for programming it (manhacks have 4, turrets 6, ...)
    "hostile_msg": "It's hostile!", // (optional) message when programming the monster failed and it's hostile.
    "friendly_msg": "Good!", // (optional) message when the monster is programmed properly and it's friendly.
    "place_randomly": true, // if true: places the monster randomly around the player, if false: let the player decide where to put it (default: false)
    "skill1": "throw", // Id of a skill, higher skill level means more likely to place a friendly monster.
    "skill2": "unarmed", // Another id, just like the skill1. Both entries are optional.
    "moves": 60 // how many move points the action takes.
},
"use_action": {
    "type": "place_npc", // place npc of specific class on the map
    "npc_class_id": "true_foodperson", // npc class id, see npcs/classes.json
    "summon_msg": "You summon a food hero!", // (optional) message when summoning the npc.
    "place_randomly": true, // if true: places npc randomly around the player, if false: let the player decide where to put it (default: false)
    "moves": 50 // how many move points the action takes.
},
"use_action": {
    "type": "ups_based_armor", // Armor that can be activated and uses power from an UPS, needs additional json code to work
    "activate_msg": "You activate your foo.", // Message when the player activates the item.
    "deactive_msg": "You deactivate your foo.", // Message when the player deactivates the item.
    "out_of_power_msg": "Your foo runs out of power and deactivates itself." // Message when the UPS runs out of power and the item is deactivated automatically.
}
"use_action" : {
    "type" : "delayed_transform", // Like transform, but it will only transform when the item has a certain age
    "transform_age" : 600, // The minimal age of the item. Items that are younger wont transform. In turns (60 turns = 1 minute)
    "not_ready_msg" : "The yeast has not been done The yeast isn't done culturing yet." // A message, shown when the item is not old enough
},
"use_action": {
    "type": "firestarter", // Start a fire, like with a lighter.
    "moves_cost": 15 // Number of moves it takes to start the fire.
},
"use_action": {
    "type": "extended_firestarter", // Start a fire (like with magnifying glasses or a fire drill). This action can take many turns, not just some moves like firestarter, it can also be canceled (firestarter can't).
    "need_sunlight": true // Whether the character needs to be in direct sunlight, e.g. to use magnifying glasses.
},
"use_action": {
    "type": "inscribe", // Inscribe a message on an item or on the ground.
    "on_items": true, // Whether the item can inscribe on an item.
    "on_terrain": false, // Whether the item can inscribe on the ground.
    "material_restricted": true, // Whether the item can only inscribe on certain item materials. Not used when inscribing on the ground.
    "material_whitelist": [ // List of material ids (not item ids!) that can be inscribed on.
        "wood",             // Only used when inscribing on an item, and only when material_restricted is true.
        "plastic",          // The list here is the default that is used when no explicit list is given.
        "glass",
        "chitin",
        "iron",
        "steel",
        "silver"
    ]
},
"use_action": {
    "type": "cauterize", // Cauterize the character.
    "flame": true // If true, the character needs 4 charges of fire (e.g. from a lighter) to do this action, if false, the charges of the item itself are used.
},
"use_action": {
    "type": "enzlave" // Make a zlave.
},
"use_action": {
    "type": "fireweapon_off", // Activate a fire based weapon.
    "target_id": "firemachete_on", // The item type to transform this item into.
    "success_message": "Your No. 9 glows!", // A message that is shows if the action succeeds.
    "failure_message": "", // A message that is shown if the action fails, for whatever reason. (Optional, if not given, no message will be printed.)
    "lacks_fuel_message": "Out of fuel", // Message that is shown if the item has no charges.
    "noise": 0, // The noise it makes to active the item, Optional, 0 means no sound at all.
    "moves": 0, // The number of moves it takes the character to even try this action (independent of the result).
    "success_chance": 0 // How likely it is to succeed the action. Default is to always succeed. Try numbers in the range of 0-10.
},
"use_action": {
    "type": "fireweapon_on", // Function for active (burning) fire based weapons.
    "noise_chance": 1, // The chance (one in X) that the item will make a noise, rolled on each turn.
    "noise": 0, // The sound volume it makes, if it makes a noise at all. If 0, no sound is made, but the noise message is still printed.
    "held_message": "Your No. 9 hisses.", // The message / sound description (if noise is > 0), that appears when the item makes a sound and player is wielding it.
    "noise_message": "hsss.", // Sound description when heard from another tile, e.g. wielded by an NPC.
    "voluntary_extinguish_message": "Your No. 9 goes dark.", // Message that appears when the item is turned of by player.
    "charges_extinguish_message": "Out of ammo!", // Message that appears when the item runs out of charges.
    "water_extinguish_message": "Your No. 9 hisses in the water and goes out.", // Message that appears if the character walks into water and the fire of the item is extinguished.
    "auto_extinguish_chance": 0, // If > 0, this is the (one in X) chance that the item goes out on its own.
    "auto_extinguish_message": "Your No. 9 cuts out!" // Message that appears if the item goes out on its own (only required if auto_extinguish_chance is > 0).
    "noise_variant": "fireweapon_lit" // sound variant for soundpacks, pair with id `tool`
},
"use_action": {
    "type": "musical_instrument", // The character plays an instrument (this item) while walking around.
    "speed_penalty": 10, // This is subtracted from the characters speed.
    "volume": 12, // Volume of the sound of the instrument.
    "fun": -5, // Together with fun_bonus, this defines how much morale the character gets from playing the instrument. They get `fun + fun_bonus * <character-perception>` morale points out of it. Both values and the result may be negative.
    "fun_bonus": 2,
    "description_frequency": 20, // Once every Nth turn, a randomly chosen description (from the that array) is displayed.
    "player_descriptions": [
        "You play a little tune on your flute.",
        "You play a beautiful piece on your flute.",
        "You play a piece on your flute that sounds harmonious with nature."
    ]
},
"use_action": {
    "type": "holster", // Holster or draw a weapon
    "holster_prompt": "Holster item", // Prompt to use when selecting an item
    "holster_msg": "You holster your %s", // Message to show when holstering an item
    "max_volume": "1500 ml", // Maximum volume of each item that can be holstered. Volume in ml and L can be used - "50 ml" or "2 L".
    "min_volume": "750 ml",  // Minimum volume of each item that can be holstered or 1/3 max_volume if unspecified. volume in ml and L can be used - "50 ml" or "2 L".
    "max_weight": 2000, // Maximum weight of each item. If unspecified no weight limit is imposed
    "multi": 1, // Total number of items that holster can contain
    "draw_cost": 10, // Base move cost per unit volume when wielding the contained item
    "skills": ["pistol", "shotgun"], // Guns using any of these skills can be holstered
    "flags": ["SHEATH_KNIFE", "SHEATH_SWORD"] // Items with any of these flags set can be holstered
},
"use_action": {
    "type": "bandolier", // Store ammo and later reload using it
    "capacity": 10, // Total number of rounds that can be stored
    "ammo": [ "shot", "9mm" ], // What types of ammo can be stored?
},
"use_action": {
    "type": "reveal_map", // reveal specific terrains on the overmap
    "radius": 180, // radius around the player where things are revealed. A single overmap is 180x180 tiles.
    "terrain": ["s_gun", "hiway", "road"], // ids of overmap terrain types that should be revealed (as many as you want).
    "terrain_view": ["s_gun"], // (optional) ids of overmap terrain types that should be shown on a prompt after using the map (as many as you want, ideally a subset of terrain).
    "terrain_view_exclude": ["hiway", "road"], // (optional) ids of overmap terrain types that should NOT be shown on the prompt after using the map (as many as you want, ideally a subset of terrain_view).
    "message": "You add roads and tourist attractions to your map." // Displayed after the revelation.
},
"use_action": {
    "type" : "heal",        // Heal damage, possibly some statuses
    "limb_power" : 10,      // How much hp to restore when healing limbs? Mandatory value
    "head_power" : 7,       // How much hp to restore when healing head? If unset, defaults to 0.8 * limb_power.
    "torso_power" : 15,     // How much hp to restore when healing torso? If unset, defaults to 1.5 * limb_power.
    "bleed" : 0.4,          // Chance to remove bleed effect.
    "bite" : 0.95,          // Chance to remove bite effect.
    "infect" : 0.1,         // Chance to remove infected effect.
    "move_cost" : 250,      // Cost in moves to use the item.
    "long_action" : true,   // Is using this item a long action. Setting this to true will divide move cost by (first aid skill + 1).
    "limb_scaling" : 1.2,   // How much extra limb hp should be healed per first aid level. Defaults to 0.25 * limb_power.
    "head_scaling" : 1.0,   // How much extra limb hp should be healed per first aid level. Defaults to (limb_scaling / limb_power) * head_power.
    "torso_scaling" : 2.0,  // How much extra limb hp should be healed per first aid level. Defaults to (limb_scaling / limb_power) * torso_power.
    "effects" : [           // Effects to apply to patient on finished healing. Same syntax as in consume_drug effects.
        { "id" : "pkill1", "duration" : 120 }
    ],
    "used_up_item" : "rag_bloody" // Item produced on successful healing. If the healing item is a tool, it is turned into the new type. Otherwise a new item is produced.
},
"use_action": {
    "type": "place_trap", // places a trap
    "allow_underwater": false, // (optional) allow placing this trap when the player character is underwater
    "allow_under_player": false, // (optional) allow placing the trap on the same square as the player character (e.g. for benign traps)
    "needs_solid_neighbor": false, // (optional) trap must be placed between two solid tiles (e.g. for tripwire).
    "needs_neighbor_terrain": "t_tree", // (optional, default is empty) if non-empty: a terrain id, the trap must be placed adjacent to that terrain.
    "outer_layer_trap": "tr_blade", // (optional, default is empty) if non-empty: a trap id, makes the game place a 3x3 field of traps. The center trap is the one defined by "trap", the 8 surrounding traps are defined by this (e.g. tr_blade for blade traps).
    "bury_question": "", // (optional) if non-empty: a question that will be asked if the player character has a shoveling tool and the target location is diggable. It allows to place a buried trap. If the player answers the question (e.g. "Bury the X-trap?") with yes, the data from the "bury" object will be used.
    "bury": { // If the bury_question was answered with yes, data from this entry will be used instead of outer data.
         // This json object should contain "trap", "done_message", "practice" and (optional) "moves", with the same meaning as below.
    },
    "trap": "tr_engine", // The trap to place.
    "done_message": "Place the beartrap on the %s.", // The message that appears after the trap has been placed. %s is replaced with the terrain name of the place where the trap has been put.
    "practice": 4, // How much practice to the "traps" skill placing the trap gives.
    "moves": 10 // (optional, default is 100): the move points that are used by placing the trap.
},
"use_action": {
  {
    "type": "repair_item",                // Repair items. Skill, tool quality, and dexterity is checked against how hard that item is to craft/dissemble (which may be modified with repairs_like).
    "item_action_type": "repair_fabric",  // Points to an item_action JSON entry that determines the action's name in the use menu. Vanilla examples include repair_fabric and repair_metal.
    "materials": [                        // What materials can be repaired by this item. Materials.json defines what item is consumed when repairing items of that material.
      "cotton",
      "leather",
      "nylon",
      "wool",
      "fur",
      "faux_fur",
      "nomex",
      "kevlar",
      "neoprene",
      "gutskin"
    ],
    "skill": "tailor",    // What skill determines chance of success vs. risk of damaging the item further.
    "tool_quality": 3,    // Bonus from tool, 1.<X> times multiplier on skill.  With 8 Dex, 10 skill plus 2 or more tool_quality allows any item in the game to be fully reinforced.
    "cost_scaling": 0.1,  // Reduces or increases how much raw material is needed per successful repair action, also affected by the item's volume.
    "move_cost": 800      // How long between each roll for success or failure, 100 moves is 1 turn.
  }
},
"use_action": {
    "type": "sew_advanced",  // Modify clothing
    "materials": [           // materials to deal with. Power armor can be targeted when its material matches.
        "cotton",
        "leather"
    ],
    "skill": "tailor",       // Skill used.
    "clothing_mods": [       // Clothing mods to deal with.
        "leather_padded",
        "kevlar_padded"
    ]
},
"use_action": {
    "type": "multicooker",  // Crafts an item over time without requiring the player to sit there and watch
    "do_hallu": false, //Enables multicooker hallucinations and spawning of the evil multicooker when interacting
    "charges_to_start": 50, // Static cost per item crafted
    "charges_per_minute": 1, // Cost per minute of craft time
    "time_mult": 1, //Multiplier for craft time, 2 is twice as long, 0.5 is half as long, 0 is instant
    "recipes": [ "water_clean" ], //Any recipie ident (crafted item + its suffix, e.g water_clean_using_water_purifier) in this list can be crafted
    "subcategories": [ "CSC_FOOD_MEAT", "CSC_FOOD_VEGGI", "CSC_FOOD_PASTA" ], //Any item in these subcategories can be crafted
    "temporary_tools": [ "hotplate", "tongs", "toolset", "pot" ] //Temporary items spawned when crafting to allow for innate qualities
},
"use_action": { 
    "type": "cast_spell",       // Casts a spell based on ID using item charges or, if the flag USE_PLAYER_ENERGY is on the item, the energy source defined in the spell
    "spell_id": "magus_escape", // The ID of the spell to be casted
    "no_fail": true,            // Whether you can fail the cast
    "level": 10,                // The level its cast at
    "need_worn": true,          // if you need to wear it to cast the spell
    "need_wielding": true       // if you need to wield it to cast the spell
},
"use_action": {
    "type": "paint_stuff",      // Paints terrain or vehicles using ammo
    "charge_cost": 0            // Number of charges to use
},
"use_action": {
    "type": "paint_stuff_cfg",  // Configures how it paints using paint stuff
    "color_swap": true          // Allow the color to be swapped to any other named color
}
"use_action": {
  "type": "passivenoise", // Emits noise periodically, used mainly with active items such as chainsaws
  "noise": 90, // Noise in decibels
  "noise_chance": 10, // Percent chance to trigger per turn
  "held_message": "The chainsaws rumble." // Message to display if player is holding the item
  "noise_message": "a chainsaw rumbling." // Sound string when heard from another tile
  "noise_variant": "chainsaw" // sound variant for soundpacks, pair with id `tool`
}
```

### Random Descriptions

Any item with a "snippet_category" entry will have random descriptions, based on that snippet
category:

```json
"snippet_category": "newspaper",
```

The item descriptions are taken from snippets, which can be specified like this (the value of
category must match the snippet_category in the item definition):

```json
{
  "type": "snippet",
  "category": "newspaper",
  "id": "snippet-id", // id is optional, it's used when the snippet is referenced in the item list of professions
  "text": "your flavor text"
}
```

or several snippets at once:

```json
{
  "type": "snippet",
  "category": "newspaper",
  "text": [
    "your flavor text",
    "more flavor",
    // entries can also bo of this form to have a id to reference that specific snippet.
    { "id": "snippet-id", "text": "another flavor text" }
  ],
  "text": ["your flavor text", "another flavor text", "more flavor"]
}
```

Multiple snippets for the same category are possible and actually recommended. The game will select
a random one for each item of that type.

One can also put the snippets directly in the item definition:

```json
"snippet_category": [ "text 1", "text 2", "text 3" ],
```

This will automatically create a snippet category specific to that item and populate that category
with the given snippets. The format also support snippet ids like above.

## Item length

`longest_side` (a length such as `"84 cm"`, DDA-compatible) is the length of an item's longest side.
It is resolved per item type in this order, first match wins:

1. an explicit `longest_side` in the item's own JSON;
2. a CDF `item_overlay` entry (below);
3. a category floor, `max(floor, derived)`: pistol 20 cm, SMG 45 cm, shotgun/rifle 80 cm,
   launcher 90 cm, bow/archery 100 cm; `SHEATH_SWORD` 90 cm; `SPEAR`, `SHEATH_SPEAR`, `POLEARM`,
   `REACH_ATTACK` 150 cm; `REACH3` 200 cm;
4. derived: the side of a cube of the volume of one unit, `round(cbrt(volume in ml))` cm.

Pockets refuse items longer than `max_item_length` or shorter than `min_item_length`. When
`max_item_length` is not given, container pockets default to `cbrt(capacity) cm * sqrt(2)` (per
DDA, no extra factor); magazine well pockets and the pockets BN synthesizes for holster actions
have an explicit limit of 2 km. Soft items (made of a soft material) have a length of 0 mm while
they are empty, and an item that is not soft is as long as its longest side or its longest
content. A gun is as long as `longest_side` plus the longest of its mods; set
`integral_longest_side` on a gun mod to say how far it sticks out of the gun.

`item_overlay` objects set whitelisted fields on existing items without redefining them; later
objects override earlier ones. Currently `longest_side` and `pocket_data`:

```json
{ "type": "item_overlay", "id": "2x4", "longest_side": "122 cm" }
```

`data/json/items/cdf_overlay_lengths.json` is generated from DDA by `tools/cdf_port_dda_overlays.py`.

A `pocket_data` overlay (an array, same schema as an item's `pocket_data`) gives a garment its worn
storage layout. A garment's own `pocket_data` works too and wins over an overlay. `storage` now
becomes pockets: the DDA layout from `item_overlay` where one exists, otherwise a main pocket of the
`storage` volume (plus a one-item strap pocket at 10 L or more). Existing mods need no edits.

The game tags these pockets `carry`: they are the character's carrying storage. Things the
character picks up go into the best fitting `carry` pocket of a worn garment (or the hidden body
storage that bionics, mutations and `CARRY_STORAGE` enchantments add to). You can write the key
yourself on a pocket: `"carry": true` makes a pocket of any item (for example a backpack with
several compartments) carrying storage, and `"carry": false` keeps a pocket for the item's own use
(a holster, a magazine). Per-pocket keys such as `max_item_length` and `volume_encumber_modifier`
apply as usual. `item::get_storage()` returns the volume of the non-holster `carry` pockets.
Items that do not fit any pocket (too long, too big) are refused with a reason; overflow when
picking up or starting the game is dropped at the character's feet.
`data/json/items/cdf_overlay_garment_pockets.json` is generated from DDA by
`tools/cdf_port_dda_overlays.py --pockets`.
