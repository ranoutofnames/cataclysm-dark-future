-- Plate and sub-location API: ItemPocket.is_ablative, item:get_ablative_items,
-- item:get_coverage(bp[, sbp]), SubBodyPartId, BodyPartTypeId:sub_parts()
local carrier = gapi.create_item(ItypeId.new("test_plate_carrier"), -1)

local slots = 0
for _, p in ipairs(carrier:get_pockets()) do
  if p:is_ablative() then
    slots = slots + 1
  end
end
test_data["slots"] = slots
test_data["plates_empty"] = #carrier:get_ablative_items()

-- the type's own pocket data says the same
local type_slots = 0
for _, p in ipairs(carrier:get_type():obj():pockets()) do
  if p:is_ablative() then
    type_slots = type_slots + 1
  end
end
test_data["type_slots"] = type_slots

local large = gapi.create_item(ItypeId.new("test_plate_large"), -1)
local medium = gapi.create_item(ItypeId.new("test_plate_medium"), -1)
test_data["large_ok"] = carrier:try_put_in(large)
test_data["medium_ok"] = carrier:try_put_in(medium)
local plates = carrier:get_ablative_items()
test_data["plates"] = #plates
test_data["first_plate"] = plates[1]:get_type():str()
test_data["second_plate"] = plates[2]:get_type():str()

-- an ordinary container has no plate slots
local rig = gapi.create_item(ItypeId.new("test_pocket_rig"), -1)
test_data["rig_plates"] = #rig:get_ablative_items()
local rig_slots = 0
for _, p in ipairs(rig:get_pockets()) do
  if p:is_ablative() then
    rig_slots = rig_slots + 1
  end
end
test_data["rig_slots"] = rig_slots

-- sub-locations of the torso
local torso = BodyPartTypeId.new("torso")
local parts = torso:sub_parts()
test_data["torso_parts"] = #parts
test_data["torso_part_1"] = parts[1]:str()
test_data["torso_part_2"] = parts[2]:str()
test_data["upper_max_coverage"] = parts[1]:obj().max_coverage
test_data["upper_parent"] = parts[1]:obj():parent():str()
test_data["upper_name"] = parts[1]:obj():name()
test_data["hand_parts"] = #BodyPartTypeId.new("hand_l"):sub_parts()
test_data["arm_parts"] = #BodyPartTypeId.new("arm_l"):sub_parts()

local upper = SubBodyPartId.new("torso_upper")
local lower = SubBodyPartId.new("torso_lower")
test_data["sub_id_valid"] = upper:is_valid()
test_data["sub_id_str"] = upper:str()

-- coverage: with and without a sub-location, by string id and by int id
local vest = gapi.create_item(ItypeId.new("test_sub_vest"), -1)
test_data["vest_torso"] = vest:get_coverage(torso)
test_data["vest_upper"] = vest:get_coverage(torso, upper)
test_data["vest_lower"] = vest:get_coverage(torso, lower)
test_data["vest_int_upper"] = vest:get_coverage(torso:int_id(), upper)
test_data["vest_int_torso"] = vest:get_coverage(torso:int_id())

-- an item with no sub-location data covers every sub-location at the body part's coverage
local plain = gapi.create_item(ItypeId.new("test_plain_vest"), -1)
test_data["plain_torso"] = plain:get_coverage(torso)
test_data["plain_upper"] = plain:get_coverage(torso, upper)
test_data["plain_lower"] = plain:get_coverage(torso, lower)
