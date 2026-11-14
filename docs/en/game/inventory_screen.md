## Insert picker and pocket denial text

The "Put item into ..." picker (`game_menus::inv::insert_items`, DDA `game_menus::inv::holster`) is built on
`inventory_insert_selector`, the drop selector with the DDA stats header and the title "ITEMS TO INSERT".
A row for an item that cannot go in is greyed out and shows the reason given by the pocket code, for
example "ablative pocket already contains a plate" when both large plate slots of a US ballistic vest are full,
or "holster does not accept this item type or form factor" when no pocket takes the item. The generic
"item can't be stored there" appears only when the pocket gave no text. There is no swap action.
