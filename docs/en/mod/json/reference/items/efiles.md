# E-files

Electronic files, and the devices that store and read them. This is the Cataclysm: DDA e-file model
(DDA `doc/JSON/ITEM.md`, `doc/JSON/JSON_FLAGS.md`), ported to CDF; it replaced the BN digital media
system (`DIGITAL`, `DIGITAL_STORAGE`, `DIGITAL_READER`, `MC_*`, the tablet menu and the book
scanner). Related: [item_creation.md](item_creation.md) (pockets, `pocket_data`),
[recipes.md](recipes.md) (`book_learn`), [item_migration.md](item_migration.md).

An **e-file** is an ordinary item that may live inside an **e-device** and nowhere else (or, for
books and some items, also in the world). An **e-device** is an item with an `E_FILE_STORAGE` pocket
(a phone, a laptop, a USB drive, a memory card, a camera). Files are not counted by volume, weight or
length; they are counted by **memory** (`ememory`).

## Memory units

`ememory` values are strings with a unit: `"20 KB"`, `"512 MB"`, `"32 GB"`, `"2 TB"`. The units are
`KB`, `MB`, `GB` and `TB`. They are used for file sizes (`ememory_size`), device capacity
(`ememory_max`) and transfer rates (`etransfer_rate`, memory per second).

## The pocket: `E_FILE_STORAGE`

A device holds its files in a pocket of type `E_FILE_STORAGE`. The pocket has a size in memory,
`ememory_max`; volume, weight and length are not limits of this pocket (the DDA data gives it a
formal `1 ml` and `1 g` and a `weight_multiplier` of 0, so the files add nothing to the device):

```json
"pocket_data": [
  { "pocket_type": "MAGAZINE", "rigid": true, "ammo_restriction": { "battery": 56 } },
  {
    "pocket_type": "E_FILE_STORAGE",
    "rigid": true,
    "max_contains_volume": "1 ml",
    "max_contains_weight": "1 g",
    "weight_multiplier": 0.0,
    "ememory_max": "256 GB"    // required and above 0 for an E_FILE_STORAGE pocket
  }
]
```

Rules of the pocket (all in the game code, not data):

- It accepts only e-storable items: an item with the flag `E_STORABLE` or `E_STORABLE_EXCLUSIVE`, or
  a book (`BOOK` subtype). Anything else is refused with "only books, e-scannable items can go into
  e-file pocket".
- An item with `E_STORABLE_EXCLUSIVE` can be stored in an `E_FILE_STORAGE` pocket only; it is never
  put in a normal pocket.
- A file is stored if its `ememory_size` is not more than the remaining memory of the device
  (`ememory_max` minus the files in it). The sum is shown in the device name as "(X free)".
- The files stay in the device when the inventory overflows or the device is dropped: this pocket is
  never spilled, and does not take part in volume or weight.
- A device of the same type stacks with another only if both hold the same memory in use.
- Files saved in a pocket of this type are stored as `"pocket_type": 6` in the save file.
- `EBOOK` is an obsolete pocket type kept as a name only: no data uses it and it holds nothing. Both
  names are reachable from Lua as `PocketType.E_FILE_STORAGE` and `PocketType.EBOOK`.

## Files

A file is a normal item definition with a size and one of the file flags:

```json
{
  "id": "my_notes",
  "copy-from": "abstract_efile_copiable",
  "type": "ITEM",
  "subtypes": [ "GENERIC" ],
  "name": { "str": "my notes" },
  "description": "A text file.",
  "ememory_size": "20 KB"
}
```

- `ememory_size`: the size of one file. Books that do not give a size get `20 KB` per page
  (`pages_in_book`). Every item with `E_STORABLE` or `E_STORABLE_EXCLUSIVE` must have one.
- `E_STORABLE`: the item can be stored on a device (a paper book is such an item) and also exists as an
  ordinary item.
- `E_STORABLE_EXCLUSIVE`: the item exists only as a file.
- `E_COPIABLE`: the file can be copied to other devices, and the item (also a paper book) can be
  scanned onto a device. A book is copiable unless it is `generic` (a generic book represents a
  whole kind of book by item, for example a book of essays).
- `E_FILE_COLLECTION`: the item stands for a combinable collection of files (for example the photo
  gallery `efile_photos` and the recipe catalog `efile_recipes`); copying one merges its photos or
  recipes into the collection already on the target device.

The data has abstract templates to copy from: `abstract_efile` (storable), `abstract_efile_exclusive`,
`abstract_efile_copiable`, `abstract_efile_exclusive_copiable`, `abstract_efile_read` (already
browsed), `abstract_efile_recipes` and `abstract_software`; they are in
`data/json/items/software.json`. Items in the `e_files` category are files; items in the `software`
category are software (below).

A file is "browsed" once its device has been browsed (item var `browsed`); files of a device that
has not been browsed cannot be used. Files take `CAN_USE_IN_DARK` from the device they are stored
on: a book on a reader that can be used in the dark can be read in the dark.

## Devices

An e-device has:

- an `E_FILE_STORAGE` pocket (above);
- the tool fields `etransfer_rate` (transfer speed in memory per second), `e_port` (the connection
  type for fast transfer) and `e_ports_banned` (a string or array of connection types this device
  cannot use);
- optionally the use actions `E_FILE_DEVICE` (the manage files menu) and `EBOOKSAVE` (store books).
  Items without `E_FILE_DEVICE` (USB drives, memory cards) are plain storage; a phone, a laptop or a
  tablet has both actions.

```json
"use_action": [ "E_FILE_DEVICE", "EBOOKSAVE" ],
"e_port": "phone",
"etransfer_rate": "30 MB",
"e_ports_banned": [ "USB-A" ]
```

The memory of the devices follows DDA: smartphone 256 GB, e-ink tablet 32 GB, laptop 1 TB, camera 32
GB, USB drive 128 GB, memory card 64 GB, portable hard drive 2 TB. CDF-only devices have the size of
the closest DDA device (control laptop 1 TB, wrist computers 256 GB, `hard_drive` 2 TB).

### Manage files (`E_FILE_DEVICE`)

The use action opens a menu. Like any tool use action it needs the device's charges per use, and it is
refused for an NPC, while mounted, underwater, with the trait `CANNOT_USE_COMPUTERS` or `ILLITERATE`, or
with uncorrected `HYPEROPIC` sight (the screen cannot be read). A device is first **browsed** (10
seconds to start the device, then 15 seconds per file); only then its files are listed. The menu
offers:

- **Browse + move files from all devices** and **Browse devices**;
- **Read files on this device** and **Read files on external devices**: pick a file, which opens the
  use menu of that file (a book is read with the device as the e-reader; another file is used through
  its own `use_action`, which receives the file as its item; `VIEW_PHOTOS` and `VIEW_RECIPES` files
  show their photos and recipes this way);
- **Move files onto / off of this device** and **Copy files onto / off of this device**;
- **Process all map caches on this device** (the `efile_map` files);
- **Wipe files from devices**.

Transfers between two devices depend on their `e_port` and `e_ports_banned` (code:
`efile_activity_actor::edevices_compatible`): a device that bans the other's port cannot transfer with
it at all; two devices with the **same** `e_port` use a slow link; two devices with different ports,
neither banned by the other, transfer fast, at the smaller of their two `etransfer_rate`. The slow link
is 1 MB/s, or, when a third browsed storage item without a manage menu (a USB drive) is carried or
near, half of that item's own rate if that is faster. Moving files inside one device runs at 200 MB/s.
A file takes one second plus its size divided by the rate. The device spends one battery charge for
each four minutes of transfer, or each 32 files browsed.

### Store books (`EBOOKSAVE`)

The use action lets you scan books (items that are `E_COPIABLE` and not yet on the device) you own,
carried or on the ground within reach (6 tiles), onto the device. It is refused for the traits
`CANNOT_USE_COMPUTERS`, uncorrected `HYPEROPIC`, underwater, and when you cannot see well enough ("You
can't see to do that!"). The scan takes five seconds per page and one use of the battery per 120 pages;
it stops when the device runs out of batteries ("ran out of batteries") or is gone, and a book that
is gone is skipped. The physical book stays, and the file is a copy of it (a book of a type that is
already stored is not offered again).

### Reading

A book file is read through the **Read files** entries of the manage files menu, through the use of the
stored book itself, and by NPCs ordered to study from an e-book ("Please study from an e-book.").
Reading with an e-reader costs one battery use at the start and one every five minutes, and stops when
the e-reader is empty ("ran out of batteries") or the book is no longer there. (The **Read** key
lists carried books only; it does not list the books stored on a device.)

## Software

Software is a file in the `software` item category (`abstract_software`, `E_STORABLE_EXCLUSIVE`).

- A requirement or a recipe can ask for software like for any item; the software is found on a
  carried e-device (and on a nearby one for recipes). CDF recipes also use software as a tool with a
  count of `-1` ("present, nothing is used"): `utility_software_standard`, `math_software_standard`,
  `medical_software_standard`, `hacking_standard`.
- A mission with a software item goal (`MGOAL_FIND_ITEM`) is complete when the software is on a
  carried device and, when handed in, **removes the whole device** that carries the software (DDA
  `consume_software_container`), not only the file.
- A computer terminal downloads software into the `E_FILE_STORAGE` pocket of a device with enough
  free memory ("Electronic storage device with %s free required!").

## Recipes from files

A **book file** (`BOOK` subtype, like the martial arts videos and the 15 recipe and blueprint files:
`digital_3d_printing_guide`, `digital_cooking_recipe_archive`, `digital_science_recipe_archive`,
`digital_tailoring_recipe_archive`, `digital_bartender_drinks_list`, `digital_electronic_blueprints`,
`digital_chemical_synthesis_sheet`, `digital_workshop_tool_plans`, `digital_basic_trap_blueprints`,
`digital_mechanical_trap_blueprints`, `digital_advanced_trap_blueprints`,
`digital_blade_blueprints`, `digital_piercing_weapon_blueprints`, `digital_archery_blueprints`,
`digital_firearm_blueprints`) gives the recipes that name it in their `book_learn`:

```json
{
  "id": "digital_cooking_recipe_archive",
  "copy-from": "digital_instruction",
  "type": "ITEM",
  "subtypes": [ "BOOK" ],
  "name": { "str": "saved cooking recipe" },
  "description": "A saved recipe from a cooking website.",
  "ememory_size": "1 MB"
}
```

```json
{ "type": "recipe", "result": "V8", "book_learn": [ [ "digital_cooking_recipe_archive", 2 ] ] }
```

A book has no recipe list of its own: the recipe says which books teach it, and at which skill
level (the number is the skill level needed; `book_learn` with a level above 0 needs `skill_used`).
A powered device that holds the file gives the character those recipes while it is carried or
nearby, without teaching them. Note: CDF raises every item to at least 1 ml, so a `BOOK` file also gets
an `uncraft_book` recipe (`recipe_dictionary.cpp` tests the volume where DDA tests the weight); this is
true of every book file and a stored file is not offered for disassembly.

The recipe catalog `efile_recipes` is a different mechanism (DDA `memory_card` block): a collection
file whose recipes are chosen when it is generated, from `recipes_categories`, `recipes_level_min`,
`recipes_level_max` and `recipes_amount` (the catalog gets 1 to `recipes_amount` of the matching
recipes, chosen when the catalog is generated; `secret_recipes` allows recipes flagged `SECRET`;
`recipes_chance` and `data_chance` are loaded as in DDA, but nothing reads them). Copying a
catalog to a device merges the recipes into the catalog already there. The use action `VIEW_RECIPES`
shows them.

## Photos

The camera (use action `CAMERA`) takes photos and stores them in the collection file `efile_photos`
in the camera's `E_FILE_STORAGE` pocket; if the camera cannot hold more, it prints "Your camera cannot
hold any more photos.". Copying a gallery merges photos. `VIEW_PHOTOS` opens the photos of a gallery;
viewing them improves morale, with the shared photo morale rule (a ten-minute cooldown).

## Genome samples

`digital_genome_sample` is a 4 TB file. It is bigger than the biggest ordinary device (2 TB), so it
fits only the genome drives (`genome_drive*`, 8 TB), which are not limited to genome data: their
pocket can store any file. The cloning syringe and the DNA editor take samples from, and store samples
in, the E_FILE pockets of carried devices; a partial sample keeps its progress in its item vars
(`specimen_sample`, `specimen_sample_progress`, `specimen_name`) and shows "[n/m]" in its name.

## Generating content

Spawn files with the contents of an item group: the group names a device, and `contents-group` fills its
`E_FILE_STORAGE` pocket:

```json
{ "item": "usb_drive", "prob": 7, "contents-group": "efiles_science" }
{ "item": "memory_card", "contents-item": "digital_cooking_show" }
```

The standard file groups are in `data/json/itemgroups/efiles.json` (`efiles_science`,
`efiles_software`, `efiles_medical`, `efiles_school`, `efiles_contraband`, `efiles_scrap`,
`civilian_portable_efiles`, `civilian_smartphone_efiles`, `civilian_laptop_efiles`,
`civilian_einktablet_efiles`) and `data/json/itemgroups/digital_media.json`. The groups that
spawn loose storage media with their files are `efile_storage_media` and
`efile_storage_media_encrypted`. A file that is stored in a pocket other than `E_FILE_STORAGE` is
moved to `MIGRATION` pocket, which is a bug: the sweep test `efile_sweep_no_item_group_puts_a_file_in_a_migration_pocket`
checks every item group.

## Replaced by this system

| BN digital media | E-files |
| --- | --- |
| flags `DIGITAL`, `DIGITAL_STORAGE`, `DIGITAL_READER`, `DIGITAL_MUSIC`, `GENOME_DRIVE`, `MC_*` | `E_FILE_STORAGE` pocket, `E_STORABLE*`, `E_COPIABLE` (the old flags no longer exist) |
| use action `EINKTABLETPC` and its menus | `E_FILE_DEVICE`, `EBOOKSAVE`, `VIEW_PHOTOS`, `VIEW_RECIPES` |
| book scanning (`ACT_SCAN_BOOK`, "Scan book") | `EBOOKSAVE` (`ACT_EBOOKSAVE`, reach 6 tiles) |
| item vars `digital_recipes`, `digital_random_recipes`, `digital_content`, `digital_reader_ports`, `digital_storage_ports` | `book_learn` on the recipe, item groups with `contents-group`, `e_port` / `e_ports_banned` |
| encrypted storage, music cooldowns, synthetic photo counters | removed (DDA has none) |
| `sd_memory_card`, `mobile_memory_card*`, `software_train_data` | migrated to `memory_card` and `train_data` (`data/json/obsoletion/migration.json`) |
| the `ebook_lua` mod | removed (the engine does what it did) |
