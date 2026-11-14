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
