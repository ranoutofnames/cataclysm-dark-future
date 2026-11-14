# Inventory screen keys and actions

This page lists the keybinding actions of the inventory screens as CDF defines them in `data/raw/keybindings/keybindings.json`. The default keys follow Cataclysm: Dark Days Ahead (DDA). Every key can be rebound in the in-game keybindings menu; the context names below are the menu sections `INVENTORY` and `ADVANCED_INVENTORY`.

Each section below belongs to one feature of the inventory screens. A section is added when the feature that uses the action arrives; an action listed here without a feature section yet is defined but not read by the game yet.

## Selector core: collapsing, hide all, filter reset, skipping, highlight

These behaviours belong to the item selector (the `INVENTORY` context). They follow Cataclysm: Dark Days Ahead (DDA).

- **Show or hide contents** (`SHOW_HIDE_CONTENTS`, default `>`) collapses or expands the pockets of the highlighted row itself. The row stays in the list; only what is inside it hides. In the carried list, a worn garment or bag that carries other items is a "holder" row: press the key on the holder row to hide all the carried rows under it, and press it again to bring them back. Pressing it on a carried container only collapses that container.
- **Show or hide all contents** (`SHOW_HIDE_CONTENTS_ALL`, no default key) cycles through three states for every column: contents shown, contents hidden, then back to each container's own setting. The state is saved with the game's UI settings, so it is the same the next time the screen opens. A chevron coloured green or red shows that the override is active.
- **Reset filter** (`RESET_FILTER`) clears the filter text and shows the whole list again.
- **Toggle skipping of unselectable items** (`TOGGLE_SKIP_UNSELECTABLE`, no default key) controls whether the cursor skips rows that cannot be chosen (greyed out). In CDF the cursor skips them by default. Turn the skip off to stop on a greyed row and see why it is unavailable.
- **Next and previous column** (`NEXT_COLUMN`, `PREV_COLUMN`) move between the columns of the selector. The older `LEFT` and `RIGHT` actions are no longer used by the plain selector; the multi-select screens (drop, item use, pickup) still use `RIGHT` to mark an item until their keys are updated.
- **Parent and child highlight**: when a container is highlighted, the items inside it are drawn in an inverted colour, and the container that holds it is drawn in a bright inverted colour.
