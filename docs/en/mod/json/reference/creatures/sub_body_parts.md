# Sub body parts

A body part can be split into sub-locations: the torso, each arm, each leg, the head, the eyes, the mouth, each hand and each foot. The ids are listed under [Ids](#ids). Armor can list the sub-locations it covers (`specifically_covers`, see [armor portion data](../items/item_creation.md)), and a hit rolls one sub-location of the body part it lands on, weighted by `max_coverage`.

The schema and ids are those of Cataclysm: DDA, so DDA data loads unchanged. Secondary (hanging and draped) locations are supported, see [Ids](#ids). `similar_bodypart` is not supported.

## `sub_body_part`

```json
{
  "id": "torso_upper",
  "type": "sub_body_part",
  "parent": "torso",
  "name": "upper torso",
  "name_multiple": "torso",
  "side": "both",
  "opposite": "torso_lower",
  "max_coverage": 60
}
```

| Field | Meaning |
|---|---|
| `id` | Unique id. |
| `parent` | The `body_part` this location belongs to. The body part must list it in `sub_parts`. |
| `name`, `name_multiple` | Display names (`name_multiple` is the plural, e.g. "shoulders"). |
| `side` | `left`, `right` or `both` (default `both`). |
| `opposite` | The location on the other side of the body (default: itself). |
| `max_coverage` | The share (0-100) of the parent this location makes up. A body part's primary locations should add up to 100. A hit chooses a location with this weight. |
| `secondary` | Default `false`. `true` for a hanging or draped location: it is rolled in its own pool and tested in a second absorb pass (see Armor). |
| `locations_under` | Secondary locations only: the primary locations the location hangs over. Each must be a primary `sub_body_part`; otherwise the game logs a `debugmsg`. |

## `body_part`

A body part lists its locations with `sub_parts`:

```json
"sub_parts": [ "torso_upper", "torso_lower" ]
```

A body part with no `sub_parts` is hit as a whole. In this data every real body part (torso, head, eyes, mouth, arms, hands, legs and feet) has `sub_parts`.

### `encumbrance_per_weight`

`encumbrance_per_weight` (head only in this data) is a list of weight points, linear in between, that a portion with `encumbrance_modifiers` reads its encumbrance from (see [armor portion data](../items/item_creation.md)):

```json
"encumbrance_per_weight": [
  { "weight": "1 mg", "encumbrance": 1 },
  { "weight": "300 g", "encumbrance": 4 },
  { "weight": "800 g", "encumbrance": 8 },
  { "weight": "1300 g", "encumbrance": 20 },
  { "weight": "1600 g", "encumbrance": 30 },
  { "weight": "2200 g", "encumbrance": 50 },
  { "weight": "5600 g", "encumbrance": 80 },
  { "weight": "10000 g", "encumbrance": 160 }
]
```

## Ids

Primary locations, with the `max_coverage` of each. The primary locations of a body part add up to 100.

| Body part | Sub-locations (max_coverage) |
|---|---|
| `torso` | `torso_upper` 60, `torso_lower` 40 |
| `arm_l` | `arm_shoulder_l` 20, `arm_upper_l` 40, `arm_elbow_l` 5, `arm_lower_l` 35 |
| `arm_r` | `arm_shoulder_r` 20, `arm_upper_r` 40, `arm_elbow_r` 5, `arm_lower_r` 35 |
| `leg_l` | `leg_hip_l` 20, `leg_upper_l` 40, `leg_knee_l` 5, `leg_lower_l` 35 |
| `leg_r` | `leg_hip_r` 20, `leg_upper_r` 40, `leg_knee_r` 5, `leg_lower_r` 35 |
| `head` | `head_forehead` 15, `head_crown` 25, `head_nape` 20, `head_throat` 20, `head_ear_l` 10, `head_ear_r` 10 |
| `eyes` | `eyes_left` 50, `eyes_right` 50 |
| `mouth` | `mouth_lips` 15, `mouth_nose` 15, `mouth_cheeks` 30, `mouth_chin` 40 |
| `hand_l` | `hand_wrist_l` 10, `hand_palm_l` 20, `hand_back_l` 20, `hand_fingers_l` 50 |
| `hand_r` | `hand_wrist_r` 10, `hand_palm_r` 20, `hand_back_r` 20, `hand_fingers_r` 50 |
| `foot_l` | `foot_sole_l` 25, `foot_arch_l` 25, `foot_toes_l` 10, `foot_ankle_l` 20, `foot_heel_l` 20 |
| `foot_r` | `foot_sole_r` 25, `foot_arch_r` 25, `foot_toes_r` 10, `foot_ankle_r` 20, `foot_heel_r` 20 |

Secondary locations (`secondary`: true) are rolled in their own pool for each body part. The `max_coverage` of each pool adds up to 100 here. Each lists in `locations_under` the primary locations it hangs over.

| Body part | Secondary sub-locations (max_coverage), `locations_under` |
|---|---|
| `torso` | `torso_neck` 10 (`torso_upper`), `torso_hanging_front` 35 (`torso_upper`, `torso_lower`), `torso_hanging_back` 45 (`torso_upper`, `torso_lower`), `torso_waist` 10 (`leg_hip_r`, `leg_hip_l`) |
| `leg_l` | `leg_draped_l` 100 (`leg_hip_l`, `leg_upper_l`) |
| `leg_r` | `leg_draped_r` 100 (`leg_hip_r`, `leg_upper_r`) |

## Armor

An item without `specifically_covers` is unchanged: its coverage applies to every sub-location of the body parts it covers. An item with it covers only the listed sub-locations, each at the entry's `coverage`.

If two entries of one item name the same sub-location, that sub-location takes the higher of the two coverages (it is not added up). The coverage of the whole body part, which the rest of the game reads, is then the sum over its sub-locations of coverage times `max_coverage`, divided by the sum of the `max_coverage` of all its sub-locations: an item covering `torso_upper` at 100 and `torso_lower` at 80 covers the torso at 0.6 * 100 + 0.4 * 80 = 92. The result is rounded to the nearest whole number. Secondary locations are not in that sum: a portion that names only secondary locations takes the best coverage among them, unweighted.

Armor only conflicts with armor that shares sub-locations with it on its layer. The strapped layer also has hanging sub-locations, marked `"secondary": true`, for things hung around the neck, on the front or on the back. An item without `specifically_covers` covers every primary sub-location of the body parts it covers (DDA says every sub-location; the secondary ones need a sub-portion of their own in CDF). A hit rolls the primary pool; armor with a sub-portion covering that body part is also tested at a secondary location. Flat armor (no `specifically_covers`) is absorbed once.

## Lua

`BodyPartTypeId:sub_parts()` returns the `SubBodyPartId`s of a body part (an empty list for parts that are hit as a whole; secondary locations are listed too). `SubBodyPartId:obj()` gives `str_id()`, `name()`, `name_multiple()`, `parent()`, `opposite()` and `max_coverage`.

`item:get_coverage( bp )` and `item:get_coverage( bp, sbp )` return an item's coverage of a body part, or of one of its sub-locations. `bp` is a `BodyPartTypeId` or a `BodyPartTypeIntId`. For items that don't list sub-locations the second form returns the body part's coverage.

```lua
local torso = BodyPartTypeId.new("torso")
for _, sub in ipairs(torso:sub_parts()) do
  print(sub:str(), sub:obj().max_coverage, vest:get_coverage(torso, sub))
end
```

`item:get_ablative_items()` lists the armor plates inserted into the item's plate slots, and `ItemPocket:is_ablative()` tells a plate slot from an ordinary pocket (so does `PocketData:is_ablative()` for an item type's pockets).
