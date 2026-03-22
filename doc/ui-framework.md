# OpenACR UI Framework

## The Problem

Building UIs today means writing code. Lots of it. Want a table? Write a table
component. Want a tree? Write a tree component. Want to change a column? Recompile.
Want the same UI in a browser? Rewrite everything in JavaScript.

OpenACR already proved that data structures don't need hand-written code — you
define them as relational records, and `amc` generates everything. We apply the
same idea to user interfaces.

## The Idea

**What if a UI was just data?**

```
ui.widget  widget:users  type:table  row:2  col:0  w:40  h:10  title:Users  selection:single
ui.column  column:users.name   p_widget:users  field:name   title:Name   w:15
ui.column  column:users.email  p_widget:users  field:email  title:Email  w:25
ui.binding binding:users  p_widget:users  source_ctype:myapp.User  source_field:zd_user  kind:collection
```

Four lines. That's a complete interactive table with selection, column headers,
and data binding to any AMC-generated type. No C++ UI code. No JavaScript.
Change the field names, the table shows different data. Add a column, it appears.
Remove one, it's gone. All without recompilation.

## How It Works

The framework has three parts:

**1. You describe the UI in ssim files** — windows, widgets, styles, key bindings,
data connections. This is the "what."

**2. A message pipeline processes input** — every keypress becomes a typed message.
Messages flow through a dispatcher that updates state. This is the "how."

**3. A thin renderer draws the result** — it reads the widget tree and current
state, outputs to terminal or browser. This is the "where."

The renderer is the only platform-specific part. Everything else — the widget
definitions, the event handling, the state management — is portable C++ that
compiles to both native and WebAssembly.

## A Concrete Example

We built `acr_tui` — a terminal application that browses OpenACR's own schema
(1600+ types, 5000+ fields). The entire UI is defined in 7 ssim files:

```
data/ui/window.ssim     — 1 record:  24x80 terminal window
data/ui/style.ssim      — 5 records: header, selected, panel, status, title
data/ui/widget.ssim     — 4 records: title label, namespace tree, fields table, status bar
data/ui/binding.ssim    — 2 records: tree → dmmeta.Ns, table → dmmeta.Field
data/ui/column.ssim     — 3 records: field name, arg type, reftype
data/ui/tree_cfg.ssim   — 1 record:  indent=2, show_lines, child_field
data/ui/key_map.ssim    — 7 records: j/k=navigate, Tab=focus, Enter=expand, q=quit
```

23 lines of data. The C++ renderer is generic — it doesn't know about namespaces
or fields. It just follows the bindings.

Want to browse `samp_mdb` instead of `dmmeta`? Change two binding records. Same
renderer, same key mappings, different data.

## Messages: Why Keypresses Aren't Actions

When you press `j`, the framework doesn't directly move the cursor. Instead:

1. Raw keypress `j` arrives
2. KeyMap lookup: `j` on widget `ns_tree` → action `navigate_next`
3. A `NavigateMsg` is created: `{type:2, widget:ns_tree, direction:1}`
4. The message is dispatched to `HandleNavigate()` — a pure function that increments `selected[focus_idx]`
5. The message is logged in a ring buffer
6. The renderer redraws

Why this indirection? Because messages are data. You can:

- **Record** a session: save the message stream to a file
- **Replay** it: feed the messages back, get identical behavior
- **Test** without a terminal: construct messages programmatically, assert on state
- **Remote control**: send messages over WebSocket from another machine
- **Undo**: reverse the message stream (for reversible actions)

The message types follow the OpenACR `ams` protocol pattern — packed binary structs
with a header containing type ID and length. AMC generates the dispatch switch.

```
uimsg.MsgHeader     type:u32  length:u32
uimsg.NavigateMsg   base:MsgHeader  widget:Smallstr50  direction:i8
uimsg.ExpandMsg     base:MsgHeader  widget:Smallstr50  node_key:Smallstr50  expand:bool
uimsg.EditMsg       base:MsgHeader  widget:Smallstr50  field:Smallstr50  value:Smallstr200
uimsg.InsertMsg     base:MsgHeader  ssim:char(Varlen)
uimsg.QuitMsg       base:MsgHeader
... (12 message types total)
```

Every possible user interaction has a message type. The handler for each message
is a pure state function — no I/O, no rendering, no platform dependencies.

## Data Binding: The Generic Part

The magic is in `ui.Binding`. It tells a widget where its data comes from:

```
ui.binding  binding:users  p_widget:users  source_ctype:myapp.User  source_field:zd_user  kind:collection  dir:read
```

The renderer sees this and knows: iterate the `zd_user` linked list, for each
record read the fields specified by `ui.Column`, display them in the table cells.

It works for any AMC ctype. The renderer doesn't import `myapp.User` — it uses
AMC-generated Print/Read functions and field metadata to render generically.

Binding kinds:

| Kind | What it means |
|------|---------------|
| `value` | Display a single field (label showing "Users: 42") |
| `collection` | Iterate a list (table rows, list items) |
| `tree` | Navigate parent-child hierarchy (tree nodes) |
| `command` | Trigger an action (button click) |
| `property` | Reflect a widget property (visibility, enabled) |

## Styling: State-Aware

A widget has a base style. But focused widgets look different from unfocused ones.
Selected rows look different from unselected. Instead of hardcoding this:

```
ui.style       style:normal    fg:default  bg:default  bold:N  border:single
ui.style       style:focused   fg:cyan     bg:default  bold:Y  border:single
ui.style       style:selected  fg:black    bg:cyan     bold:N  border:none

ui.style_slot  style_slot:tree.focused   p_widget:tree  state:focused   p_style:focused
ui.style_slot  style_slot:tree.selected  p_widget:tree  state:selected  p_style:selected
```

The renderer checks widget state, looks up the StyleSlot, applies the right colors.
Add an `error` state? Define the style, add a StyleSlot. No code change.

## Layout: Two Modes

**Absolute**: widget says `row:5 col:10 w:40 h:10`. The renderer puts it there.
Simple and predictable for terminal layouts.

**Managed**: container says `layout:vertical`. Children use `ui.LayoutCfg` for
flex weights, min/max sizes, padding, gaps. The engine computes positions.

Both modes use the same Widget type. The LayoutCfg is a separate table —
only widgets in managed containers need it.

## Portability: Terminal and Browser

The same schema, same messages, same engine run on both:

```
Terminal (C++)                          Browser (WASM)
─────────────────                       ──────────────────
read(stdin)                             DOM keydown event
  → parse ANSI escape                    → JS event handler
  → TranslateKey()                       → construct UiMsg
  → UiMsg                                → pass to WASM
  → DispatchMsg()  ◄── same C++ ──►     → DispatchMsg()
  → state update                         → state update
  → Render()                              → Render()
  → ANSI to stdout                        → Canvas/DOM draw
```

The engine (middle box) is identical compiled code. Input translation and rendering
are thin wrappers — ~100 lines each.

We proved this works: the WASM demo at `wasm/build/index.html` runs AMC-generated
C++ in the browser with live data sync. The UI framework extends this to full
interactive applications.

## What You Can Build With This

**Today** (implemented):
- Schema browsers (`acr_tui` browsing `dmmeta`)
- Data explorers for any AMC namespace
- CRUD interfaces with table/tree/form widgets
- Terminal dashboards with live data

**With minimal additions** (one ssim table each):
- Linked views: selecting in tree filters the table
- Modal dialogs and popups
- Search/filter bars
- Cell-level formatting (numbers right-aligned, booleans as checkboxes)
- Mouse support (click to focus, select, expand)

**The key property**: each addition is a new ssim table, not a rewrite.
The existing widgets, bindings, and messages continue to work unchanged.

## The Full Schema

### ui namespace — 21 types

**Structure**: Window, Widget, WidgetType (12 values), LayoutType (9 values)

**Presentation**: Style, StyleSlot, WidgetState (6 values), Color (9), BorderStyle (5), Align (6), Column, LayoutCfg

**Behavior**: Binding, BindingKind (5), BindingDir (3), KeyMap, ActionType (29 values), Command, TableCfg, TreeCfg, InputCfg, SelectionMode (3)

### uimsg namespace — 13 types

MsgHeader + 12 messages: KeyPress, Navigate, Focus, Scroll, Expand, Edit, Select, Insert, Delete, Quit, Refresh, Command

### Widget fields

```
widget      Smallstr50      Primary key
p_window    ui.Window       Parent window (Pkey)
p_parent    ui.Widget       Parent widget for nesting (Pkey)
type        ui.WidgetType   Widget type (Pkey)
layout      ui.LayoutType   Layout mode for children (Pkey, default: absolute)
row, col    i32             Position (absolute mode)
w, h        i32             Size
zorder      i32             Overlap ordering
visible     bool            Visibility (default: true)
enabled     bool            Interaction enabled (default: true)
focusable   bool            Can receive focus (default: false)
selection   SelectionMode   Selection behavior (none/single/multi)
p_style     ui.Style        Base style (Pkey)
title       Smallstr100     Widget title
text        Smallstr200     Static text content
```

## Design Principles

1. **Data, not code** — the UI is ssim records, the code is generic
2. **Messages, not callbacks** — every interaction is a typed, loggable, replayable record
3. **Pure handlers** — state updates have no side effects
4. **One schema, many renderers** — terminal, browser, test harness
5. **Grow by addition** — new tables extend, never break existing ones

## Review Notes

The architectural direction is strong. The most valuable property is the clean
split between declarative UI data, pure message handling, and thin rendering
backends. That gives the framework portability, replayability, and a very good
test surface.

The main risk is over-indirection. When structure, layout, style, binding,
commands, and interaction are all data-driven, the system can become difficult
to author and debug unless the missing semantics are made explicit.

### Recommended Improvements

1. **Add a small typed expression system**

Direct bindings are a good base, but real screens need computed visibility,
enabled state, derived labels, formatting, filters, and conditional styling.
These should be handled by one constrained expression model rather than many
special-purpose tables.

2. **Define stable widget identity and state lifetime**

Focus, selection, expansion, edit state, and scroll position should be modeled
with explicit ownership rules. The framework should define which state is
ephemeral, which is persisted, and how row or node identity survives refreshes
and reordering.

3. **Separate intent from effects**

The message pipeline is the right model for user intent. Side effects such as
file I/O, clipboard access, network calls, timers, and notifications should
flow through a separate effect queue so state handlers remain pure.

4. **Plan for dependency tracking**

A fully generic renderer is simple and correct, but eventually expensive. The
design should leave room for invalidation, dirty propagation, and partial
recompute so updates only touch affected widgets.

5. **Make one layout model dominant**

Supporting both absolute and managed layouts is practical, but the framework
will be easier to author if one managed layout model is the default for most
screens and absolute positioning is reserved for exceptional cases.

6. **Treat large-data behavior as a first-class concern**

Tables and trees eventually need viewport-based rendering, lazy expansion,
incremental search, and efficient scrolling. These should be part of the design
contract, not just later optimizations.

7. **Promote validation and form semantics earlier**

Once editable widgets exist, the framework needs parse state, dirty state,
touched state, validation errors, submit behavior, and cancel behavior. Those
are core interaction semantics, not optional additions.

8. **Version the schema and message protocol**

If UI definitions are data and message streams are replayable, compatibility
rules should be explicit. The framework should define versioning, migration,
and rejection rules for both `ui.*` and `uimsg.*`.

9. **Invest in inspection and authoring tools**

This architecture depends heavily on tooling. A UI inspector, message trace
viewer, schema linter, and hot-reload path for `ssim` edits will matter as much
as new widget types.

### Overall Assessment

The concept is correct. The next improvements should focus less on adding more
widgets and more on making the system easier to reason about at scale:

- expression evaluation
- effect handling
- state identity rules
- inspection and lint tooling
