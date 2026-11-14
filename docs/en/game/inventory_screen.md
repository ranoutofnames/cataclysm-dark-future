## Advanced inventory: containers (UI5)

The advanced inventory (`/`) shows the contents of a container in its own pane, the way Cataclysm: Dark Days Ahead does. Each pane has its own container, so the left pane can be inside one bag while the right pane is inside another.

- **Enter a container.** Select a bag, box or other item with a pocket in either pane and press the container key (`C`, `ITEMS_CONTAINER`). The pane lists everything in the item, stacked like the items on a square. The header shows how much the container holds and how much it can hold (weight and volume).
- **Containers in containers.** Select a container in the container pane and press `C` again to go one level deeper.
- **Step back out.** `X` (`ITEMS_PARENT`) steps out one level: from a bag in a bag to the outer bag, from the outer bag to the square or the inventory view it was found in, and the container you just left is selected in that list. From a container carried in a garment's pocket or in the body storage, `X` goes straight to the inventory view. The `X` in the key grid at the top of the pane is lit while the pane shows a container. If the other pane shows the container one level up, the panes swap places instead.
- **Moving items.** Moving into a container checks its free volume and free weight capacity (the pane asks the container, so what it already holds counts). You cannot move a container into itself or into something it holds. You cannot move items out of a container you carry into your inventory, as they are already yours; move them to a square instead.
- **Unload a container.** `u` (`UNLOAD_CONTAINER`) takes the contents of the selected container out and drops them on the square the other pane shows, or at your feet. Liquids and favorites stay in the container.
- **Examine contents.** `o` (`EXAMINE_CONTENTS`) opens the contents examiner for the selected item. An item with no contents shows the normal item description instead.
- **Scrolling.** `Home` and `End` jump to the first and last row of the pane. Page up and page down move by a page, stop at the first or last row, and wrap around when the pane is already there.

Unchanged and still CDF-only: the grab-vehicle and vehicle-storage toggles, `ITEMS_UP`/`ITEMS_DOWN` (the squares above and below), the reset-filter-on-close switch and the carried-weight and volume header of the inventory and worn panes.
