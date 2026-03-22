# OpenACR UI Framework — Design Document

## Vision

A fully data-driven UI framework where every aspect of the interface — structure,
layout, styling, data binding, events, and interaction — is defined as relational
ssim records. The same UI definition renders on terminal (ANSI), browser (WASM/Canvas),
or any future backend. No hand-written UI code — just data.

## Core Principle

**A widget declares what it is, where it lives, how it looks, what data it reflects,
what actions it emits, and how it reacts to state changes.**

All six aspects are expressed as ssim records. The renderer is a thin loop that
reads the widget tree and draws it. The event system is a typed message pipeline.
Adding a new widget, changing a layout, or binding to different data is a one-line
ssim edit — no recompilation needed.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Input Layer                                  │
│  Terminal: raw keypress → TranslateKey()                           │
│  Browser:  DOM event    → JS handler                               │
│  Replay:   file/stream  → read UiMsg                               │
│  Test:     programmatic → construct UiMsg                          │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ UiMsg (typed binary message)
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      Message Pipeline                               │
│                                                                     │
│  TranslateKey(ch) → KeyMap lookup → UiMsg { type, widget, ... }    │
│                                                                     │
│  DispatchMsg(msg) → switch(msg.type) {                             │
│      NAVIGATE  → HandleNavigate()   // pure state update           │
│      FOCUS     → HandleFocus()      // pure state update           │
│      EXPAND    → HandleExpand()     // pure state update           │
│      QUIT      → HandleQuit()       // set running=false           │
│      ...                                                            │
│  }                                                                  │
│                                                                     │
│  LogMsg(msg) → msg_log[] ring buffer (replay, debug, test)         │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ State change
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       UI State                                      │
│                                                                     │
│  Widget tree (loaded from ui.widget ssim)                          │
│  Focus index, selection per widget, expanded nodes                  │
│  All state is in-memory AMC data structures                        │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ Render()
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      Output Layer                                   │
│  Terminal: walk widget tree → ANSI escape sequences                 │
│  Browser:  walk widget tree → Canvas/DOM draw calls                 │
│  Test:     walk widget tree → assert on positions/content          │
└─────────────────────────────────────────────────────────────────────┘
```

**Key insight**: Message handlers are pure state functions — no I/O, no rendering,
no platform dependencies. This makes them testable, replayable, and portable.

## Three Layers

The schema explicitly models three orthogonal concerns:

### 1. Structural Layer — What exists?

| Ctype | Purpose |
|-------|---------|
| `ui.Window` | Top-level container (rows, cols, title) |
| `ui.Widget` | Universal widget (type, position, parent, visibility) |
| `ui.WidgetType` | Enum: label, table, tree, input, list, panel, tabs, statusbar, menu, separator, progressbar, canvas |
| `ui.LayoutType` | Enum: absolute, vertical, horizontal, grid, stack, dock, split, form, flow |

### 2. Presentation Layer — How does it look?

| Ctype | Purpose |
|-------|---------|
| `ui.Style` | Visual properties: fg/bg color, bold, underline, border |
| `ui.StyleSlot` | State-based style override: widget + state → style |
| `ui.WidgetState` | Enum: normal, focused, selected, disabled, editing, error |
| `ui.Color` | Enum: default, black, red, green, yellow, blue, magenta, cyan, white |
| `ui.BorderStyle` | Enum: none, single, double, rounded, ascii |
| `ui.Align` | Enum: left, center, right, top, bottom, stretch |
| `ui.Column` | Table column definition: field, title, width, alignment, sortable, hidden, editable |
| `ui.LayoutCfg` | Managed layout hints: flex, min/max size, padding, gap, alignment |

### 3. Behavior Layer — How does it interact?

| Ctype | Purpose |
|-------|---------|
| `ui.Binding` | Connects widget to data: source ctype + field, kind, direction |
| `ui.BindingKind` | Enum: value, collection, tree, command, property |
| `ui.BindingDir` | Enum: read, write, readwrite |
| `ui.KeyMap` | Maps key → action per widget (or global) |
| `ui.ActionType` | Enum: insert, delete, navigate_next/prev, edit, toggle_expand, select, scroll, focus, quit, activate, cancel, submit, open, close, copy, paste, refresh, search, page_up/down, home, end, sort, filter, expand_all, collapse_all |
| `ui.Command` | Parameterized action: action + target widget + argument |
| `ui.TableCfg` | Table config: selectable, header |
| `ui.TreeCfg` | Tree config: indent, show_lines, child_field, label_field, expanded_default |
| `ui.InputCfg` | Input config: editable, maxlen |

## Message Protocol (uimsg)

All user interactions are modeled as typed binary messages following the OpenACR
`ams` (Algo Messaging System) pattern.

### Header

```
uimsg.MsgHeader
  type:u32      — message type ID (dmmeta.typefld)
  length:u32    — message length in bytes (dmmeta.lenfld)
```

### Message Types

| Type ID | Message | Fields | Purpose |
|---------|---------|--------|---------|
| 1 | `KeyPressMsg` | key:u8, modifiers:u8 | Raw keypress |
| 2 | `NavigateMsg` | widget, direction:i8 | Move selection up/down |
| 3 | `FocusMsg` | widget | Change focus to widget |
| 4 | `ScrollMsg` | widget, delta:i32 | Scroll content |
| 5 | `ExpandMsg` | widget, node_key, expand:bool | Expand/collapse tree node |
| 6 | `EditMsg` | widget, field, value | Edit a field value |
| 7 | `SelectMsg` | widget, row_key | Select a row |
| 8 | `InsertMsg` | ssim (Varlen) | Insert record via ssim tuple |
| 9 | `DeleteMsg` | ctype, key | Delete a record |
| 10 | `QuitMsg` | (none) | Exit application |
| 11 | `RefreshMsg` | (none) | Force redraw |
| 12 | `CommandMsg` | command, arg | Execute parameterized command |

### Properties

- **Binary dispatch**: AMC generates a switch on `MsgHeader.type` — zero-overhead routing
- **Fixed layout**: packed structs, no heap allocation, no parsing
- **Replayable**: message stream can be recorded to file, replayed for testing
- **Network-ready**: same binary messages work over WebSocket for remote UI
- **Portable**: same messages used by terminal (C++), browser (WASM), and test harness

### Future: Mouse Events

```
uimsg.MouseMsg — row:u16, col:u16, button:u8, action:u8 (click/release/move/scroll)
```

The click handler resolves (row, col) to a widget, then emits the appropriate
semantic message (FocusMsg, SelectMsg, ExpandMsg, etc.).

## Data Binding

The `ui.Binding` ctype connects a widget to application data.

```
ui.binding  binding:ns_tree   p_widget:ns_tree   source_ctype:dmmeta.Ns
            source_field:zd_ns  kind:tree  dir:read
```

### Binding Kinds

| Kind | Behavior |
|------|----------|
| `value` | Widget displays a single scalar field value |
| `collection` | Widget iterates a list/pool (table rows) |
| `tree` | Widget renders hierarchical data (tree nodes) |
| `command` | Widget triggers an action when activated |
| `property` | Widget reflects a property (visibility, enabled, style) |

### Binding Direction

| Direction | Behavior |
|-----------|----------|
| `read` | Widget displays data (one-way) |
| `write` | Widget modifies data (one-way input) |
| `readwrite` | Widget displays and modifies data (two-way) |

### How Binding Works

1. Renderer encounters a table widget
2. Looks up `ui.Binding` for that widget → gets `source_ctype` and `source_field`
3. Iterates the bound collection using AMC-generated cursors
4. For each record, reads fields specified by `ui.Column` entries
5. Renders each cell at the correct position with the correct style

This is generic — the renderer doesn't know about `dmmeta.Ns` or `samp_mdb.User`.
It only knows "iterate this collection, display these fields."

## State-Based Styling

Instead of hardcoding focus/selection colors, the `ui.StyleSlot` ctype maps
widget state to style overrides:

```
ui.style_slot  style_slot:ns_tree.focused   p_widget:ns_tree  state:focused   p_style:focus_style
ui.style_slot  style_slot:ns_tree.selected  p_widget:ns_tree  state:selected  p_style:selected_style
ui.style_slot  style_slot:fields.focused    p_widget:fields   state:focused   p_style:focus_style
```

The renderer checks the widget's current state, looks up the StyleSlot, and applies
the matching style. Base style comes from `Widget.p_style`; state overrides come
from StyleSlot records.

## Layout System

Two modes:

### Absolute (default)

Widget specifies exact `row`, `col`, `w`, `h`. The renderer places it there.
Simple, predictable, works for fixed terminal layouts.

### Managed (vertical, horizontal, grid, etc.)

Widget specifies layout type on the container. Children use `ui.LayoutCfg` to
express flex weight, min/max size, alignment, padding, and gap. The layout engine
computes positions at render time.

```
ui.widget      widget:sidebar  layout:vertical  ...
ui.layout_cfg  layout_cfg:sidebar  p_widget:sidebar  padding:1  gap:0
```

Children of `sidebar` stack vertically with 1-cell padding. Their `row`/`col` are
computed, not specified.

## Parameterized Commands

Simple actions (navigate, quit) are enum values. Complex actions need parameters:

```
ui.command  command:sort_by_name     action:sort    p_widget:fields  arg:field
ui.command  command:focus_tree       action:focus   p_widget:ns_tree arg:""
ui.command  command:expand_dmmeta    action:open    p_widget:ns_tree arg:dmmeta
```

KeyMap can bind to a Command instead of a raw ActionType. The `CommandMsg` carries
the command name and argument, and the handler resolves it.

## Portability: Terminal and Browser

The same `ui.*` schema and `uimsg.*` protocol work on both platforms:

### Terminal (C++ native)
```
Input:    read(STDIN) → parse ANSI sequences → UiMsg
Engine:   DispatchMsg → state update (AMC data structures)
Render:   walk widgets → write ANSI escapes to stdout
```

### Browser (WASM)
```
Input:    DOM keydown/click → JS creates UiMsg → pass to WASM
Engine:   DispatchMsg → state update (same AMC code, compiled to WASM)
Render:   walk widgets → JS reads state → draw to Canvas or <pre>
```

The engine (DispatchMsg + handlers) is identical C++ on both platforms. Only the
input translator and renderer are platform-specific, and they're thin.

## Testing Strategy

Because interactions are messages, testing is:

1. **Construct a message stream**: `[NavigateMsg(dir=1), NavigateMsg(dir=1), ExpandMsg, ...]`
2. **Feed messages to DispatchMsg**: engine processes them, updates state
3. **Assert on state**: check which widget is focused, which row is selected, which nodes are expanded
4. **No terminal needed**: tests run headlessly as unit tests

The message log (`msg_log[]`) enables:
- **Record**: capture a live session as a message stream
- **Replay**: feed the stream back, verify identical state
- **Regression**: saved message streams become regression tests
- **Debug**: inspect what happened step by step

## Example: OpenACR Schema Browser

The `acr_tui` executable demonstrates the framework by browsing `dmmeta`:

### UI Layout (data/ui/widget.ssim)
```
ui.widget  widget:title      type:label      text:"OpenACR Schema Browser"
ui.widget  widget:ns_tree    type:tree       title:Namespaces     selection:single
ui.widget  widget:fields     type:table      title:Fields         selection:single
ui.widget  widget:status     type:statusbar  text:"q:quit j/k:nav Tab:focus Enter:expand"
```

### Data Binding (data/ui/binding.ssim)
```
ui.binding  binding:ns_tree   source_ctype:dmmeta.Ns     source_field:zd_ns    kind:tree
ui.binding  binding:fields    source_ctype:dmmeta.Field  source_field:zd_field kind:collection
```

### Key Mapping (data/ui/key_map.ssim)
```
ui.key_map  key_map:global.q        key:q      action:quit
ui.key_map  key_map:global.tab      key:Tab    action:focus_next
ui.key_map  key_map:ns_tree.j       key:j      action:navigate_next    p_widget:ns_tree
ui.key_map  key_map:ns_tree.enter   key:Enter  action:toggle_expand    p_widget:ns_tree
```

### What Happens

1. `acr_tui` starts, loads `ui.*` and `dmmeta.*` ssim files via `finput`
2. Renders: title bar, namespace tree (left), fields table (right), status bar
3. User presses `j` → `TranslateKey('j')` → keymap matches `ns_tree.j`
   → `NavigateMsg(widget:ns_tree, direction:1)` → `HandleNavigate()` → selected++
4. User presses `Enter` → `ExpandMsg(widget:ns_tree)` → `HandleExpand()`
   → expanded[idx] toggled → tree shows ctypes under namespace
5. User presses `Tab` → `FocusMsg` → focus moves to fields table
6. User presses `q` → `QuitMsg` → running=false → exit

Every interaction is a message. Every state change is traceable.

## Schema Summary

### ui namespace (ssimdb) — 21 ctypes

**Enums (10):**
- WidgetType (12 values), LayoutType (9), BorderStyle (5), Color (9)
- ActionType (29), SelectionMode (3), BindingKind (5), BindingDir (3)
- WidgetState (6), Align (6)

**Structs (11):**
- Window, Style, Widget, Binding, Column
- TableCfg, TreeCfg, InputCfg, KeyMap
- StyleSlot, Command, LayoutCfg

### uimsg namespace (protocol) — 13 ctypes

- MsgHeader (type:u32 + length:u32)
- 12 message types: KeyPress, Navigate, Focus, Scroll, Expand, Edit, Select, Insert, Delete, Quit, Refresh, Command

## What This Can Describe Today

- CRUD tools and admin panels
- Schema browsers and inspectors
- Tree/table browsers for any ssim data
- Config editors with form layouts
- Interactive data exploration tools
- Real-time dashboards (via data binding to live sources)

## What Needs Future Work

| Gap | Solution | When |
|-----|----------|------|
| Modal dialogs | `ui.Dialog` ctype with modal:bool | When needed |
| Submenus | `ui.MenuItem` with parent menu reference | When needed |
| Dynamic visibility | `ui.VisibilityRule` with condition expression | When needed |
| Computed text | `ui.Format` with template string + binding | When needed |
| Validation | `ui.ValidationRule` per input widget | When needed |
| Cell renderers | `ui.CellFormat` enum (text/number/bool/badge/bar) | When needed |
| Linked selection | `ui.Filter` connecting widget selections | When needed |
| Mouse events | `uimsg.MouseMsg` (row, col, button, action) | When needed |
| Output messages | `uimsg.ScreenCellMsg` for remote rendering | When needed |
| Async refresh | `uimsg.TimerMsg` with interval | When needed |

Each is one ssim table away. The schema is designed for incremental growth.

## Design Principles

1. **Data over code** — UI structure is ssim records, not C++ classes
2. **Messages over callbacks** — interactions are typed messages, not function pointers
3. **Pure handlers** — state updates have no side effects, no I/O
4. **Schema-driven** — adding a widget type or action is an ssim edit, not a code change
5. **Platform-agnostic engine** — same C++ compiles to native and WASM
6. **Incremental** — add new tables when needed, never break existing ones
7. **Testable** — message streams are the test interface
8. **Replayable** — every session is a reproducible sequence of messages
