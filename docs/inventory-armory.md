# Armory workspace

The desktop inventory is organized around three questions: **who am I equipping,
what do we own, and what should happen to this item?** It uses the existing
ROM-backed snapshot rather than pretending to know gameplay rules the profile
cannot determine.

## Workflow

Open with `I`. Choose a recipient in the left-hand roster. Clicking an item in
the army-wide browser or one of the recipient's five slots **only inspects it**.
The inspector stays pinned while searching, filtering, sorting, scrolling, or
changing recipient. Hover remains independent from the pending transfer.

* **Give / Store beside each item** performs the common transfer in one click.
  Give uses the selected recipient's first empty slot. Store moves their carried
  item to the actual free convoy slot. Fixed items and a full convoy are disabled.
* **Double-click an item** does the same without leaving its row: give to the
  selected recipient, or store when it already belongs to that recipient.
* **Drag an item** onto a loadout slot to move or swap, onto an ally in the roster
  to give, or onto the pinned supply destination to store. A five-point movement
  threshold separates inspection clicks from dragging at every DPI/UI scale.
  The source is an address/slot pair, never a filtered or sorted row number.
* **Full recipients** show Swap instead of Give and require an explicit loadout
  slot. Dropping on a full ally selects that ally and keeps destination choice
  open; it never silently replaces an item. The inspector's **Move / swap** is
  still available, but is no longer required for normal transfers.

Dropping outside a destination, onto the same slot, or onto fixed equipment does
not write. Escape, right-click, focus loss, and window resize cancel dragging.
Scrolling while dragging updates the highlighted destination. A changed source
item invalidates the gesture; successful transactions retain expected-value
checks and undo. Repeated clicks on an inline action do not apply it to the next
row after sorting changes the list.

`U` retains the existing single-step, expected-value-checked undo. Clicking a
unit name or class opens its ROM-backed help when no move is pending. The game
remains paused while the manager is open. `Close`, `I`, or `Esc` returns to it.
`Esc` first leaves search, then cancels a pending move, then closes the manager.

## Finding equipment

Search matches item names, owners, owner classes, and item types. Multiple words
are ANDed; matching is ASCII case-insensitive. `/` focuses search; Enter or Esc
leaves it. Text input is UTF-8 bounded and never falls through to the I/A/S/D/U
shortcuts. Backspace removes a whole UTF-8 code point. Ctrl/Cmd+A clears the query
for replacement. There is no general-purpose text selection or IME preedit UI.

Type chips expose every weapon/staff type plus Items. **Ready only** uses the
existing rank, lock, and status check for the selected recipient; it deliberately
does not guess consumable-use rules. The roster previews the inspected item's
compatibility with each recipient, independently of whether it can be moved.

The All items/Supply toggle and type/name/uses/owner sorts remain available.
Click an active sortable column to reverse it. Clear filters resets the query,
type, and Ready-only filter together. Filtering never changes game inventory
order or the canonical endpoints used for writes. A pinned source stays selected
even when a filter excludes it from the visible list.

## Layout and data boundaries

The recipient card uses the already decoded portrait, name, class, level, HP,
weapon ranks, and five inventory slots. The browser prioritizes identity, uses,
and ownership, progressively revealing combat columns as space permits. The
inspector shows raw item stats, ownership, durability, required rank, recipient
rank, and the ROM description. Its description region scrolls independently.
On larger windows it also compares might/hit/weight with the first carried
weapon, explicitly labeled **vs carried**; this is not an equipped-item claim,
a combat forecast, or an estimate of ROM-specific skill effects.

The inspector moves below the workspace in smaller windows. A 640x480 logical
minimum keeps all five slots, at least one recipient, an item list, transfer
actions, and status feedback visible. `D` changes row density. `+`/`-`/`0` retain
UI scaling independently of game zoom and Retina density. Drawing and pointer
mapping share one geometry model.

Architecture:

1. `Fe8InventorySnapshot` and the canonical pool are read-only inputs.
2. `fe8_inventory_desktop_visible` derives a filtered, optionally reversed index
   view. It never edits those inputs or treats a visible row as an endpoint.
3. `fe8_inventory_desktop_click` consumes browsing controls and resolves only
   Give/Store requests into canonical endpoints. The pointer gesture helpers
   resolve drops through that same action handler. Neither can write game memory. The existing `main.c` guarded transaction and undo remain the only
   write path; fixed items and changed expected values are still rejected.

No new frontend framework, dependency, hardcoded character list, ROM assets, or
save format is needed by the application.

## Archanae personal weapons

The exact supported Archanae ROM uses an additional weapon-lock table, separate
from retail FE8's rank and low-bit attribute locks. Its native predicate at
`0x08B3EA54` indexes the pointer table at `0x08B2BAE4` with the item's high
attribute byte. Entries contain a mode plus a zero-terminated character-ID
(mode 1) or class-ID (mode 3) whitelist. Modes 0 and 2 do not restrict usability
in this predicate. The catalog decodes those lists once into a bounded bitset;
all table badges, recipient previews, and Ready-only filtering use the same
check. Malformed/unsupported lists show Unknown rather than Ready.

Borderland Sword (`0xC2`) has lock index 10 and a character whitelist containing
only `0x36` (Athena). Marth is therefore Locked regardless of sword rank. This
is profile-scoped and ID-based, not a special case on a translated item name.
The profile remains SHA-verified; unrelated ROMs do not reinterpret these bits.

## Validation and reproducible captures

`inventory_workspace` exercises filtering, multi-token/owner queries, Ready
states, source pinning, explicit actions, fixed items, UTF-8 editing, sorting,
canonical endpoint mapping, scrolling, several density/DPI/zoom combinations,
snapshot immutability, and framebuffer/stride guards. It also checks quick
Give/Store, pointer jitter, drag destinations, source pinning through sorting,
full recipients, cancellation, fixed equipment, and stale source protection.
`fe8_catalog_locks` checks character/class lists, malformed data and profile
isolation. The optional Archanae integration test executes the ROM's native
predicate to confirm it rejects Marth and accepts Athena. Because Athena is not
in the early test roster, the positive case uses her real character record in
an isolated scratch unit, then restores the complete state. Both supplied-ROM
integration tests exercise quick Give/Store and drag Give/swap followed by undo
at 80–130% UI scale, checking exact RAM restoration. The previous desktop
regression matrix is retained with design-independent geometry assertions.

After building, make deterministic renderer captures without a ROM:

```sh
mkdir -p build/armory-captures
build/tests/test_inventory_workspace build/armory-captures
```

This produces overview, filtered, compact, and minimum-window PPM images. These
are deliberately synthetic inventories, not a user's save game. An optional
second argument supplies a local portrait-only fixture: for each of 12 units,
16 ABGR uint32 palette entries followed by 80x72 palette indices. Do not commit
that extracted input, any ROM, save, or font. The production app continues to
read portraits, text, and equipment from its normal ROM/profile pipeline.
