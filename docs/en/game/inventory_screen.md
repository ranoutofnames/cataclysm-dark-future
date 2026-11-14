# Inventory screen keys and actions

This page lists the keybinding actions of the inventory screens as CDF defines them in `data/raw/keybindings/keybindings.json`. The default keys follow Cataclysm: Dark Days Ahead (DDA). Every key can be rebound in the in-game keybindings menu; the context names below are the menu sections `INVENTORY` and `ADVANCED_INVENTORY`.

Each section below belongs to one feature of the inventory screens. A section is added when the feature that uses the action arrives; an action listed here without a feature section yet is defined but not read by the game yet.

## Item selector (INVENTORY context)

Screens that use the selector: inventory, drop (multidrop), item use, wield, wear and similar item menus. Pocket menu actions (`FAV_*`) are used only in the pocket settings menu and may repeat a key that the selector uses for something else.

| Action | Default key | What it does |
|---|---|---|
| `NEXT_COLUMN` | `h`, Left, `4`, Numpad 4 | Move to the next column. |
| `PREV_COLUMN` | Shift+Tab, Tab | Move to the previous column. |
| `TOGGLE_ENTRY` | `l`, Right, `6`, Numpad 6 | Select or unselect the highlighted item (multi-select screens). |
| `MARK_WITH_COUNT` | `!`, `1` | Select a specific amount of the highlighted item. |
| `INCREASE_COUNT` | `=`, `+` | Increase the selected amount for the highlighted item. |
| `DECREASE_COUNT` | `-` | Decrease the selected amount for the highlighted item. |
| `TOGGLE_NON_FAVORITE` | `,` | Select or unselect all non-favorite items (multidrop). |
| `DROP_NON_FAVORITE` | `N` | Older id for the same job as `TOGGLE_NON_FAVORITE`; moved off `,` so it no longer clashes. |
| `CATEGORY_SELECTION` | `t` | Toggle category selection mode. |
| `VIEW_CATEGORY_MODE` | `;` | Toggle the inventory view to show item categories (CDF only). |
| `SHOW_HIDE_CONTENTS` | `>` | Show or hide the contents of the highlighted container. |
| `SHOW_HIDE_CONTENTS_ALL` | none | Show or hide the contents of all containers. |
| `TOGGLE_SKIP_UNSELECTABLE` | none | Toggle skipping items that cannot be selected. |
| `INVENTORY_FILTER` | `/` | Set the item filter. |
| `EXAMINE` | `e` | Examine the highlighted item. |
| `EXAMINE_CONTENTS` | `o` | Examine the contents of the highlighted container. |
| `TOGGLE_FAVORITE` | `*` | Toggle the item as favorite. |
| `WIELD` | `w` | Wield the highlighted item. |
| `WEAR` | `W` | Wear the highlighted item. |
| `ORGANIZE_MENU` | `i` | Open the organize-contents menu. |
| `CONTAIN_MODE` | `U` | Toggle contain mode for unloaded items. |

Mouse actions of the selector:

| Action | Default input | What it does |
|---|---|---|
| `SELECT` | Left click | Highlight or select the entry under the cursor (shared mouse action). |
| `MOUSE_MOVE` | Mouse move | Highlight the entry under the cursor (shared mouse action). |
| `SCROLL_UP`, `SCROLL_DOWN` | Mouse wheel | Scroll the list. |
| `FAV_CONTEXT_MENU` | Left click (also Enter) | Open the context menu of the pocket menu entry. |

### Pocket menu actions (INVENTORY context)

`FAV_PRIORITY` `p`, `FAV_AUTO_PICKUP` `d`, `FAV_AUTO_UNLOAD` `u`, `FAV_ITEM` `i`, `FAV_CATEGORY` `c`, `FAV_WHITELIST` `w`, `FAV_WHITELIST_CONTENTS` `W`, `FAV_BLACKLIST` `b`, `FAV_SAVE_PRESET` `S`, `FAV_APPLY_PRESET` `A`, `FAV_DEL_PRESET` `D`, `FAV_CLEAR` `x`, `FAV_MOVE_ITEM` `m`, `FAV_CONTEXT_MENU` Enter or left click.

## Advanced inventory (ADVANCED_INVENTORY context)

| Action | Default key | What it does |
|---|---|---|
| `PAGE_DOWN`, `PAGE_UP` | Page Down, Page Up | Page through the list. |
| `UP`, `DOWN` | `k`/Up, `j`/Down | Move the highlight. |
| `LEFT`, `RIGHT` | `h`/Left, `l`/Right | Select the left or right pane. |
| `TOGGLE_TAB` | Tab | Toggle between the two panes. |
| `TOGGLE_VEH` | `v`, `V` | Toggle vehicle storage. |
| `TOGGLE_AUTO_PICKUP` | `p` | Toggle auto-pickup for the item. |
| `EXAMINE` | `e`, `E` | Examine the item. |
| `EXAMINE_CONTENTS` | `o` | Examine the items in the highlighted container. |
| `SORT` | `s`, `S` | Change the sorting mode. |
| `MOVE_SINGLE_ITEM` | Enter | Move a single item. |
| `MOVE_VARIABLE_ITEM` | `m` | Move an amount of the item. |
| `MOVE_ITEM_STACK` | `M` | Move the item stack. |
| `MOVE_ALL_ITEMS` | `,` | Move all items. |
| `UNLOAD_CONTAINER` | `u`, `U` | Unload the item from its container. |
| `ITEMS_PARENT` | `x`, `X` | Exit the container to its parent. |
| `CATEGORY_SELECTION` | `t` | Toggle category selection mode. |
| `TOGGLE_FAVORITE` | `*` | Toggle the item as favorite. |
| `TOGGLE_FILTER_AUTORESET` | `T` | Toggle the AIM filter autoreset (CDF only). |
| `ITEMS_UP`, `ITEMS_DOWN` | `<`, `>` | Go up or down a level (CDF only). |
| `ITEMS_*` (location keys) | `0`-`9`, `i`, `a`, `d`, `c`, `w` | Select the source: a map tile, inventory, surroundings, dragged container, container or worn items. |

## Mouse actions by screen

Mouse input in the selector is defined here in `keybindings.json`; the screens that read it are added by later sections.

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

## Examining contents and the insert header

These behaviours belong to the item selector and to the insert menu. They follow Cataclysm: Dark Days Ahead (DDA).

- **Examine contents** (`EXAMINE_CONTENTS`, default `o`) opens a two-pane window for the highlighted container: its contents are listed on the left and the item info of the highlighted item is shown on the right. Move with the usual list keys; Page Up and Page Down scroll the item info; `>` shows or hides the contents of a nested container; Escape or Enter closes the window and returns to the list, which is rebuilt if you collapsed or expanded something. If the highlighted item has no contents, the normal item info opens instead.
- **Insert menu header**: when you choose items to put into a container (the insert action), the header shows the container's used and total volume and weight, its largest free space, and a line with the volume and weight of the items you have marked. Items that cannot go in are greyed out with the reason, for example a plate when every large plate slot of a vest is full.
- **Container pick header**: a menu that picks one item from inside a container shows the same used and total space of that container.

## Mouse and drag and drop in the item picker

The menus where you pick a single item (and the unload menu) take the mouse as well as the keyboard.

- **Hover**: moving the mouse over a row highlights it. Nothing is chosen until you click.
- **Click**: a left click on a row chooses that item, the same as pressing its letter. A click just left of a row that has contents (on the arrow) opens or closes the contents instead.
- **Wheel**: the mouse wheel moves the highlight a page up or down, in every item menu (picker, drop, pickup and so on).
- **Drag and drop**: in menus that allow it, press the left button on an item, drag to another item and release. The "Select pocket" menu opens, listing the pockets of the item you dropped on; pick one to store the dragged item there. Releasing on the item you started from is an ordinary click. Menus that do not allow dragging just treat the release as a click.

The drag start uses the new key name `MOUSE_LEFT_PRESSED` (action `CLICK_AND_DRAG`, inventory context). Mouse support is meant for the graphical (tiles) build.

## The pickup menu

The multipickup menu (pick up from your tile or from the tiles around you) now behaves like the item menus of Cataclysm: DDA. It still hands the same pickup list to the game, so what is picked up, the carry checks and the messages are unchanged.

- **Hint line**: the top of the menu lists the keys for wield, wear, expand, expand all, examine, mark with a count, raise or lower the count and "type a number then mark". The keys shown are your own bindings.
- **Contents**: a container lying on the ground can be expanded, and its contents are marked like other items. Marking a container covers what is inside it.
- **Nothing marked**: confirming with no item marked is refused with a popup that names the mark key.
- **Header**: the usual carried volume and weight line is followed by a "Selected" line with the volume and weight of what you have marked (a partly marked stack counts only the part you marked).
- **Wield and wear**: the highlighted item is wielded or worn at once and removed from the list, the highlight moves to the next item and the menu stays open. The menu closes when nothing is left.
