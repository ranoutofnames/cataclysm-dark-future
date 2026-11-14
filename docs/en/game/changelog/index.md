# Releases

These are an incomplete list compiled for convenience, see
[releases](https://github.com/cataclysmbn/Cataclysm-BN/releases) for all changes.

Starts with
[revision 10614 (just before pockets)](https://github.com/cataclysmbn/Cataclysm-BN/commit/8cea0fce70c87ea93c6fd4e409c68558e24ce42e)
of [CleverRaven/Cataclysm-DDA.](https://github.com/CleverRaven/Cataclysm-DDA)

## Not yet released (Cataclysm: Dark Future)

- MBR vests and Old Guard IOTVs are replaced by DDA's plate carriers. Plates are separate items that go
  into the carrier's plate slots, and old vests convert automatically.
- Plate carriers and other MOLLE webbing take attachable pouches (`PALS` items) that add their own pockets.
  A pouch keeps what is in it when attached or detached.
- The DDA ammo screen is the only reload screen now. The old BN reload menu is gone. The default-ammo
  key opens the same screen: keyboard only, with count keys, multi-select and per-well headers.
- The unload key opens an unload screen with an Auto or Manual destination. The contain-mode key
  switches between them.
- The ammo selector lists ammo and liquid nested in carried containers, in containers on the map and
  in vehicles.
- Unloading a gun or tool with several magazines takes all of them, and the integral ammo too. With
  several sources, a picker asks which one to unload.
- An insert into a gun with no pockets keeps the magazine. The magazine is no longer destroyed.
- New option `UNLOAD_RAS_WEAPON` (on by default) and key `-` (`TOGGLE_UNLOAD_RAS_WEAPON`): unload a
  bow or other RAS weapon after you cancel a shot.
- Cables use the link-up model, and the BN cable items and actions are gone. A cable can attach to a
  vehicle battery, an electric grid connector, a UPS, a solar pack or a Cable Charger System CBM, and
  cable extensions can be added.
- The reactor well takes removable plutonium packs (`plut_pack_reactor`).
- Tools no longer keep direct charges: a battery tool reloads through its battery pocket, like a gun.
  `cash_card` keeps direct charges.
- Guns jam with DDA's damage-based model.
- The carried bar in the sidebar, the trade pane and the pickup header counts the carry pockets only.
- Footwear: BN's "already wearing footwear" refusal is removed, as in DDA.
- Starting-kit items that cannot be worn are carried, not destroyed. Starting kits wear with DDA's
  armor layers.
- `box_large` holds 7 L of items.
- Hand sub-locations (wrist, palm, back and fingers) on gloves, watches, rings and bracelets.
- Foot sub-locations (sole, arch, toes, ankle and heel) on foot armor.
- Torso and leg sub-locations: neck, hanging front and back, waist and draped legs. Eyes and mouth
  are split into their own sub-locations.
- New armor ids that match DDA's are added.
- Picking up something too heavy or too large cancels the pickup silently.
