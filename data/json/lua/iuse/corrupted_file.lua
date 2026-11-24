local M = {}
local ghost_id = MonsterTypeId.new("mon_signal_maw")

-- CDF: E1-10c. The BN digital-media system gave every file an identity (DIGITAL_FILE_ID) and kept the
-- spent ones in the mod storage.  The e-file engine has no file identity: a file that was opened is
-- marked on the file itself (copies of a spent file stay spent, a copy made before opening is a new
-- file that can still be opened once).
local SPENT_VAR = "corrupted_file_spent"

---@param params ItemUseParams
function M.open(params)
  if params.item:get_var_str(SPENT_VAR, "") ~= "" then
    gapi.add_msg(MsgType.info, locale.gettext("The preview is gone.  There is only static."))
    return 0
  end

  local creature = gapi.place_monster_around(ghost_id, params.user:get_pos_ms(), 1)
  if not creature then
    gapi.add_msg(
      MsgType.warning,
      locale.gettext("A pale figure presses against the screen from the inside, but cannot find room to emerge.")
    )
    return 0
  end

  params.item:set_var_str(SPENT_VAR, "1")
  creature.friendly = 0
  params.user:mod_moves(-100)
  gapi.add_msg(
    MsgType.bad,
    locale.gettext(
      "A pale face flashes across the screen.  A wavering human shape pulls itself free, trailing static.  The device falls silent.  The corrupted file is gone."
    )
  )
  return 1
end

return M
