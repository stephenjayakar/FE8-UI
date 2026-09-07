# Armory workspace

The desktop inventory is organized around three questions: **who am I equipping,
what do we own, and what should happen to this item?** It uses the existing
ROM-backed snapshot rather than pretending to know gameplay rules the profile
cannot determine.

## Two views, one inventory

Open with `I`. The game is paused while the manager is open.

**By item** is a searchable, sortable index of carried and Supply equipment.
The left workbench identifies the selected recipient. **By unit** displays one
row per ally and all five slots, including empties and fixed equipment. Wider
windows keep a Supply list alongside the loadouts; the Supply bar opens the
item browser at smaller widths. Each view retains its own scroll position.
Click an ally to select the recipient; click an item only to inspect it.

In **By unit**, text search filters the unpinned roster by unit name, class or
carried equipment, using case-insensitive ANDed words. Units with empty
inventories still match their name/class. Matching rows retain all five slots;
type and usability chips highlight equipment without hiding those recipients.
Pinned loadouts remain visible regardless of the query, with their frozen page
and geometry unchanged. The footer counts matching units separately from pins;
no-match results show an empty state rather than leaving unrelated allies visible.

Typing, backspacing, Ctrl/Cmd+A and Clear filters reset the result scroll to the
first match without changing the selected recipient, source, comparison or Undo.
Drawing, scrolling, pinning and drag/drop all consume the same filtered snapshot
indices, resolved immediately into canonical address/slot targets. A hidden
source stays selected, and clearing the query restores the roster. Item-browser
and Supply search retain their existing item-based behavior.

Counts include matching carried items even when the item browser's remembered
scope is Supply. The board shows per-owner compatibility; the Supply
list and item-browser badges use the selected recipient. Inspection, comparison
baseline, query and filters are shared across views.

## Temporary pinned loadouts

In **By unit**, click **Pin** beside an ally's name to move their complete
loadout into a frozen section above the scrolling roster. Pinned allies retain
all five slots and the same base/total stat strip. They are not duplicated in
the roster below. **Unpin** returns one ally; **Unpin all** clears the section.
Pins are ordered by when they were added, not by the item-browser sort.

Wheel scrolling over either the pinned section or the other loadouts moves
only the unpinned roster. Supply still scrolls independently. More pins than
fit are retained: explicit previous/next buttons page the frozen section,
without changing the roster's scroll. At 640x480 a denser row keeps at least
one pinned and one scrolling loadout available. Resizing never silently unpins
an ally or puts a pinned row back into the scrolling list.

Pins survive item transfers, Undo, filters, density/zoom changes and switching
between By item / By unit within the open manager. Closing and reopening the
Armory starts with no pins; no favorite list or save-game data is written.
A pin is keyed by unit address **and character ID**, not a visual row index.
Snapshot refreshes discard missing/replaced characters rather than transferring
a pin to another character reusing the same unit slot.

Frozen and scrolling rows share the same drawing, stat-hover and canonical
endpoint mapping. Drag between either section, drop on a pinned ally to Give,
use its nearby swap picker, or drag its equipment to Supply normally. Pin and
paging controls are never inventory write targets, and pinning does not change
the current recipient, inspected item, comparison target, or undo history.

`inventory_pins` checks pixel-identical frozen sections before/after scrolling,
all five slots' canonical endpoints at multiple densities/scales, independent
Supply scrolling, overflow paging, filtering, transfers, fixed swap slots,
reused addresses, session reset, snapshot immutability and framebuffer canaries.
Both supplied-ROM desktop tests also drag from/to a frozen loadout after
scrolling and verify that two-step Undo restores all EWRAM byte-for-byte.
Reproducible fixture captures (no ROM required):

```sh
mkdir -p build/pin-captures
build/tests/test_inventory_pins build/pin-captures
```

## Unit stats

The recipient card in **By item** shows level, EXP, current/max HP and a
read-only grid of **Pow, Skl, Spd, Lck, Def, Res, Con and Mov**. **By unit**
shows the same stats under every ally's five slots, with level and EXP beside
the portrait. Stat strips select the ally and accept ally drops; they never
alias the item slot above. Both views retain all five slots at the minimum
640x480 size; the board fits two compact rows. No stats require opening details.

**Total stats** is now the default for the two SHA-verified supplied Archanea
and Sacred Echoes revisions. A private in-memory mGBA core restores the paused
state and executes each ROM's actual unit-stat getters. Equipped-weapon bonuses,
passive equipment, learned/personal/class/item skills and current penalties are
therefore included to the extent they affect that ROM's stat screen. The exact
ROM controls stacking, caps, equipment selection and conditional modifiers;
the frontend does not sum item data tables or double-count consumed boosters.

**Base stats** switches back to stored values. Hover any stat (including maximum
HP) for `Total 15 = Base 12 +3`. Increases are accented, reductions are red, and
unchanged values stay neutral. This is a net modifier, not an invented breakdown
by individual skill. The toggle does not change selection, equipment, undo or
emulated memory. Both views use the same totals, refreshed on opening, transfer
and undo rather than during painting. Current HP is not manufactured or healed.

**Base only** identifies unsupported ROMs or failed native evaluation; **Mixed
stats** identifies partial availability. Hover explains the exact unit's status;
no unverified number is labeled as a verified total. Pow still represents the
shared power field, not a guessed extra Magic stat. Base Con/Mov include their
existing permanent class/base/bonus extraction. The EXP-disabled sentinel is
`--`, not 255.

These are **current stat-screen totals, not combat forecasts**. Opponent-specific
skills, activation-chance procs, attack-phase bonuses, weapon might/hit/crit and
terrain/support battle calculations are not treated as permanent unit stats.
Battle-preview config is cleared only in the private core, while unit/map state
is preserved. No enemy or future battle is fabricated.

The evaluator never steps or restores the live core. It copies the ROM and the
raw serialized state into an independent core with no save-file attachment.
Every getter starts from that snapshot with interrupts/events suspended and a
bounded instruction/stack budget. Archanea's separate learned and item-skill
caches are invalidated in the copy to prevent stale bonuses after host transfers.
Only two exact SHA-1 revisions are enabled, including verification of the copied
ROM mapping; mapping changes invalidate the evaluator. Unexpected return values,
stale inventory/stat snapshots and invalid unit addresses fall back to base.

State restoration also exposed a pinned mGBA defect: the 16-bit unlicensed-cart
flags were loaded/stored with 32-bit operations, causing unaligned access and a
possible write into the adjacent SIO field. `cmake/MgbaStateCompatibility.cmake`
builds a two-line-corrected copy (LOAD_16/STORE_16) without dirtying or changing
the pinned submodule. The regression test checks the adjacent field and round
trip. No sanitizer suppression is used.

## Transfers without an inspector detour

* **Inline Give / Store** performs the common transfer in one click. Give uses
  the selected recipient's first empty slot. Store resolves the real free convoy
  slot, including a sparse convoy. An incompatible weapon's Give action reads
  **Carry**: transferability and weapon usability are independent.
* **Double-click an item-browser or left-workbench item** for its Give/Store
  action. Board single clicks inspect; drag there for direct transfers.
* **Drag onto any loadout slot** to move or swap, onto an ally to give, or onto
  the pinned Supply bar to store. No prior recipient change is required.
* **Full recipients** open a five-slot chooser beside the click or drop. The
  incoming item/recipient and each outgoing item's owner are shown before an
  exchange. Fixed destinations cannot be selected. This opens on release, not
  on a timed hover. The chooser clamps to the viewport, scales with the UI,
  consumes outside clicks, and dismisses on outside click, Escape or right-click.

The drag threshold is five logical points at any DPI/zoom. Sorting does not
change a gesture's address/slot source. Scrolling during dragging updates the
highlighted destination. Same-slot drops, identical-item exchanges, background
drops and fixed equipment do not write. Source changes invalidate the gesture;
actual writes also check expected source and destination values. Resize and
focus loss cancel dragging and pending swaps. The explicit Move / swap action
remains available for click-only operation.

Successful moves name the item, origin and destination in the footer; swaps
name both legs. The receiving slot highlights briefly. List scroll offsets are
not reset by the transfer, although a sort/filter may naturally reposition or
exclude the moved row. The inspector follows the destination, even when hidden
by a filter. Changing the recipient preserves scroll unless the recipient-only
usability filter changes the result set.

## Undo and state boundaries

Click **Undo** or press `U` to reverse the most recent of up to 32 transactions
in the paused session. The footer displays the remaining count. Every undo uses
the same guarded transaction with inverted expected values. A rejected undo
leaves the history intact; it never overwrites a changed item. Capacity eviction
removes only the oldest entry. Identical-item/no-op and failed moves do not
create history. Resume, opening a different ROM and state changes clear history.

`I` or Close returns to the game. Escape first leaves search or cancels the
current move before it closes the manager. All moving/browsing state is host UI
state, not part of the ROM's save format.

## Finding and understanding equipment

`/` focuses search. Item name, owner, owner class and type match case-insensitive
ANDed words. Enter/Escape leaves search. UTF-8 input and backspace are bounded;
Ctrl/Cmd+A clears the query. There is no general-purpose text selection or IME
preedit UI. Type chips show matching counts. **Usable by [recipient]** means the
known rank/lock/status predicate permits the weapon, not that a consumable has
been evaluated. The All/Supply toggle and Type/Name/Uses/Owner sorts remain;
clicking the active sortable header reverses it. Clear filters resets query,
type and usability together.

Labels explain **Can use**, **Needs D · has E**, **No lance proficiency**,
**Silenced**, **Personal weapon**, or **Restriction unknown**. A sole personal or
class whitelist is named only when that ID resolves in the current snapshot.
An absent named character is not guessed from the item name. The roster header
explicitly identifies the item whose compatibility is being previewed.

## Comparison and responsive layout

A carried weapon's **vs** control pins the exact comparison slot. Inspect a
candidate to compare raw might/hit/weight against that baseline. A swap hover
previews the actual destination slot instead. The owner and baseline item are
named. Hit/might losses are not painted as benefits; weight changes stay neutral.
No first-carried weapon is implicitly assumed to be equipped or the correct
replacement. These are catalog stat differences, not a skill-adjusted forecast.

At narrower sizes a 48-point detail summary replaces the permanently tall
inspector. Details expands a scrollable drawer over the workspace. Five slots,
a visible ally list, Supply access, cancellation and Undo remain available at
640x480 logical points. At larger item-view sizes the full inspector is inline.
`D` controls row density and `+`/`-`/`0` retain independent UI zoom and Retina
scaling. Draw and hit testing use the same logical geometry. Original bitmap
fallback tests remain in place. No new fonts, icons, framework or ROM assets are
shipped by this change.

## Architecture

`Fe8InventorySnapshot` and its canonical pool remain read-only. Filtered browser
indices and board `unit * 5 + slot` indices are ephemeral hit results, immediately
resolved into address/slot endpoints. They are never retained as write targets.
The popup keeps its canonical source, expected encoded item and recipient
address, so filter/order changes cannot redirect a swap.

`inventory_desktop` derives views, paints and resolves gestures. It never writes
game memory. `inventory_history` owns the bounded command history and invokes
`fe8_swap_inventory_endpoints` for both transfers and undo. `main.c` executes
those commands and refreshes the coherent snapshot after success. The existing
expected-value, address, profile and fixed-equipment protections remain the only
write path.

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

`inventory_stats` records the actual stat label/value draws across both views,
unit changes, zero/255 values, the EXP sentinel, density, zoom and DPI. It checks
that stats cannot alias item hit targets and that selection, the snapshot and
framebuffer canaries remain unchanged. The ROM tests compare every unit field
and class ID against direct emulator-bus reads.

`inventory_loadouts` checks every board slot's canonical endpoint across window,
zoom, density and DPI combinations, filter/scope invariants, nearby picker
geometry, fixed destinations, stale sources, explicit/hover comparison and
explanations. Both board/drawer and modal painting check framebuffer tail and
stride canaries and snapshot immutability. `inventory_history` verifies bounded
multi-step rollback, capacity eviction, no-ops and expected-value rejection.
The earlier item-browser, drag, text, portrait and profile tests remain enabled.

The optional real-ROM desktop test advances each supplied unmodified game to
its actual roster. It exercises original quick transfers and undo at 80–130%,
then board drag, Supply drop, nearby swap and full session undo. It fills a
recipient only with existing transferable items, never manufactured equipment;
all EWRAM must match byte-for-byte after undo. Archanae's native predicate is
still checked for Borderland Sword (Marth rejected, Athena accepted via her real
character record in an isolated scratch unit with complete state restoration).

Build-time ROM inputs are local-only `FE8_ARCHANAE_ROM` and
`FE8_SACRED_ECHOES_ROM`. Native CI uses only deterministic synthetic inventories:

```sh
mkdir -p build/armory-captures
build/tests/test_inventory_workspace build/armory-captures
build/tests/test_inventory_loadouts build/armory-captures
```

The tests emit PPM captures for the item view, board, local swap chooser,
comparison, minimum window and drawer. The workspace test still accepts its
optional portrait-only fixture, but no extracted fixture, ROM, save or font is
committed. Local real-ROM captures are produced with:

```sh
build/tests/test_inventory_rom_dense /path/to/Archanea.gba archanae /tmp/archanea
```

That command also writes a local `.ss` state for SDL smoke testing. It must not
be published with the screenshots. Production rendering uses the same decoded
ROM-backed catalog and portraits as before.

## Effective-stat regression scenarios

`inventory_effective_stats` runs without a ROM in CI to test raw/effective
selection, unsupported-evaluator fallback and the mGBA state-field fix.
`effective_stats_archanae` and `effective_stats_sacred_echoes` are enabled by the
existing optional local ROM paths. They verify actual early-roster extraction,
Caeda's equipped Slim Lance (+3 Speed) and its removal/undo, a carried Fire Emblem
(+10 Luck), that an unconsumed Energy Ring gives no Power bonus, learned Fury
(+2 Power/Speed/Defense/Resistance), stale skill caches, rescue penalties,
Sacred Echoes Iron Shield (+4 Defense/-1 Speed) and Speed Ring (+10 Speed/+1 Mov).
Extra equipment/learned-skill cases are explicitly isolated RAM fixtures using
real ROM data; they are restored afterward and never written to a user's save.
Each evaluation must leave the live serialized CPU/RAM/timing state unchanged;
fixture rollback also checks EWRAM and IWRAM byte-for-byte. UI tests verify actual
Total/Base labels, values, HP, tooltip text, selection preservation and DPI/minimum
geometry. Optional native captures:

```sh
build/tests/test_inventory_effective_stats /path/to/Archanea.gba archanae /tmp/archanea
build/tests/test_inventory_effective_stats /path/to/SacredEchoes.gba sacred-echoes /tmp/echoes
```

Only screenshots and text validation logs should be shared; no ROMs, save states
or extracted assets are repository or CI inputs.

`inventory_unit_search` covers name/class/item queries, empty inventories,
case/whitespace/multi-token matching, no matches, query editing and scroll reset,
stable pinned layout/page, scope independence, filtered pinning and canonical
slot/drag targets across minimum size, density, zoom and DPI. Both supplied-ROM
desktop tests search actual unit names, transfer into a filtered loadout, and
verify that Undo restores all EWRAM byte-for-byte.
