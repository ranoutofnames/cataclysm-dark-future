# Sub body parts

A body part can be split into sub-locations: the upper and lower torso, the shoulder, upper arm, elbow and forearm of each arm, the hip, thigh, knee and lower leg of each leg, and six locations of the head. Armor can list the sub-locations it covers (`specifically_covers`, see [armor portion data](../items/item_creation.md)), and a hit rolls one sub-location of the body part it lands on, weighted by `max_coverage`.

The schema and ids are those of Cataclysm: DDA, so DDA data loads unchanged. Only the primary locations are supported: hanging (`secondary`) locations, `locations_under` and `similar_bodypart` are not.

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
| `secondary` | Reserved. Must be false. |

## `body_part`

A body part lists its locations with `sub_parts`:

```json
"sub_parts": [ "torso_upper", "torso_lower" ]
```

Body parts with no `sub_parts` (hands, feet, eyes, mouth) are hit as a whole.

## Ids

| Body part | Sub-locations (max_coverage) |
|---|---|
| `torso` | `torso_upper` 60, `torso_lower` 40 |
| `arm_l`, `arm_r` | `arm_shoulder_l/r` 20, `arm_upper_l/r` 40, `arm_elbow_l/r` 5, `arm_lower_l/r` 35 |
| `leg_l`, `leg_r` | `leg_hip_l/r` 20, `leg_upper_l/r` 40, `leg_knee_l/r` 5, `leg_lower_l/r` 35 |
| `head` | `head_forehead` 15, `head_crown` 25, `head_nape` 20, `head_throat` 20, `head_ear_l` 10, `head_ear_r` 10 |

## Armor

An item without `specifically_covers` is unchanged: its coverage applies to every sub-location of the body parts it covers. An item with it covers only the listed sub-locations, each at the entry's `coverage`.

If two entries of one item name the same sub-location, that sub-location takes the higher of the two coverages (it is not added up). The coverage of the whole body part, which the rest of the game reads, is then the sum over its sub-locations of coverage times `max_coverage`, divided by the sum of the `max_coverage` of all its sub-locations: an item covering `torso_upper` at 100 and `torso_lower` at 80 covers the torso at 0.6 * 100 + 0.4 * 80 = 92.

## Lua

`BodyPartTypeId:sub_parts()` returns the `SubBodyPartId`s of a body part (an empty list for parts that are hit as a whole). `SubBodyPartId:obj()` gives `str_id()`, `name()`, `name_multiple()`, `parent()`, `opposite()` and `max_coverage`.

`item:get_coverage( bp )` and `item:get_coverage( bp, sbp )` return an item's coverage of a body part, or of one of its sub-locations. `bp` is a `BodyPartTypeId` or a `BodyPartTypeIntId`. For items that don't list sub-locations the second form returns the body part's coverage.

```lua
local torso = BodyPartTypeId.new("torso")
for _, sub in ipairs(torso:sub_parts()) do
  print(sub:str(), sub:obj().max_coverage, vest:get_coverage(torso, sub))
end
```

`item:get_ablative_items()` lists the armor plates inserted into the item's plate slots, and `ItemPocket:is_ablative()` tells a plate slot from an ordinary pocket (so does `PocketData:is_ablative()` for an item type's pockets).
