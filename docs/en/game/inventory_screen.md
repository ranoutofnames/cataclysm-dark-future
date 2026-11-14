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

## Multi-select screens: marking, counts and containers

These behaviours belong to the screens where you mark several items: drop, pickup and item use. They follow Cataclysm: Dark Days Ahead (DDA).

- **Mark or unmark** (`TOGGLE_ENTRY`, default `l`, Right arrow) marks the highlighted item, or all of it if you pressed it on a stack. Pressing it again unmarks. If every unmarked item in the selection is a favorite, favorites are marked only when nothing else is left to mark.
- **Mark a specific amount** (`MARK_WITH_COUNT`, default `!`) asks for a number and marks that many of the highlighted stack. A number key does the same with the typed digit already filled in. Entering 0 toggles the mark as `TOGGLE_ENTRY` does. In item use the number keys now open the same prompt (they used to build up a count that the mark key then used).
- **Mark or unmark all non-favorite items** (`TOGGLE_NON_FAVORITE`, default `,`) marks every non-favorite item in the active column that you are not wearing. Pressing it again clears them. This replaces the older `DROP_NON_FAVORITE` action on these screens.
- **Increase or decrease the amount** (`INCREASE_COUNT`, `DECREASE_COUNT`) changes the marked amount of the highlighted stack by one. It never goes below 0 or above the size of the stack.
- **Containers**: marking a container covers what is inside it, at any depth. When you mark a container, any marked item inside it is unmarked, and marking an item inside an already marked container has no effect. Unmarking the container does not bring the contents back. Other items next to the container stay marked.
- **Drop preview**: the "will be dropped" list and counts of the drop screen are refreshed right after every mark, including the unmarking of contained items. This preview is CDF's own and DDA has no equivalent.
- **Previous and next column** use `PREV_COLUMN` and `NEXT_COLUMN` here as everywhere else. The old `LEFT` and `RIGHT` actions are no longer used on these screens.
- Switching between the category and container view keeps your marks.
