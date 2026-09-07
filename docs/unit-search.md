# Unit search and pinned trading partners

In Armory's **By unit** view, text search now filters the unpinned roster. This
supersedes the earlier highlight-only search described in the original workspace
design. Searching for a hero, such as Marth, brings the matching loadout directly
below the pinned section. Typing, backspacing, Ctrl/Cmd+A, and clearing filters
reset the unpinned scroll position to the first match.

Pinned allies remain above the results even when they do not match the search.
Their order, page and frozen geometry do not change when searching. Pin a trading
partner, then search for another hero to move items between them without losing
either loadout. Pinning never changes the selected recipient or equipment.

Search is case-insensitive and accepts unit names, classes and carried equipment.
A unit with empty inventory still matches its name or class. Multiple words must
match the unit's identity or one carried item's combined item/owner/class/type
fields. All five slots remain visible on every matching unit, including empty
slots. Type and usability chips highlight equipment without hiding recipients.
The **By item** tab retains its existing item-filtering behavior.

No matches shows an explicit empty state; a matching pinned hero is not duplicated
in the scrolling results. Clearing the query restores the unpinned roster.
Filtering never reorders game memory or uses a filtered row as a write target.
Drag-and-drop resolves original unit addresses and item slots.

`inventory_unit_search` exercises names, classes, equipment, empty loadouts,
preserved pins, scroll reset, no matches, canonical drag targets, minimum-window
layout, DPI/zoom and framebuffer bounds. `inventory_pins` also verifies filtered
roster membership without moving the frozen section.
