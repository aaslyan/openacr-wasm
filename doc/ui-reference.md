# OpenACR UI Framework — Reference Guide

## Overview

The `ui` namespace defines a declarative text UI framework. All UI elements —
windows, widgets, data bindings, key mappings, styles — are ssim records.
The framework reads these records and renders an interactive terminal or
browser application.

This document covers every type in the framework with field descriptions,
usage examples, and rules.

---

## 1. Structure

### ui.Window

Top-level container. Every widget belongs to a window.

| Field | Type | Description |
|-------|------|-------------|
| `window` | Smallstr50 | Unique key |
| `title` | Smallstr100 | Window title |
| `rows` | i32 | Terminal height (default: 24) |
| `cols` | i32 | Terminal width (default: 80) |

```
ui.window  window:main  title:"Order Monitor"  rows:24  cols:80
```

### ui.Widget

Universal UI node. Everything visible is a widget.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `widget` | Smallstr50 | | Unique key |
| `p_window` | Window (Pkey) | | Owning window |
| `p_parent` | Widget (Pkey) | "" | Parent widget (empty = top-level) |
| `type` | WidgetType (Pkey) | | Widget type |
| `layout` | LayoutType (Pkey) | absolute | Layout mode for children |
| `row` | i32 | 0 | Row position (absolute layout) |
| `col` | i32 | 0 | Column position (absolute layout) |
| `w` | i32 | 0 | Width in columns |
| `h` | i32 | 0 | Height in rows |
| `zorder` | i32 | 0 | Overlap ordering |
| `visible` | bool | true | Widget is visible |
| `enabled` | bool | true | Widget accepts interaction |
| `focusable` | bool | false | Can receive keyboard focus |
| `selection` | SelectionMode (Pkey) | none | Selection: none, single, multi |
| `p_style` | Style (Pkey) | "" | Base style (empty = inherit) |
| `title` | Smallstr100 | | Panel header, tab label |
| `text` | Smallstr200 | | Static text (label, statusbar) |

**Rules:**
- `p_parent` must be in the same window
- `row`/`col`/`w`/`h` apply only when parent uses `absolute` layout
- Leaf widgets (label, separator) should not have children

### ui.WidgetType

```
label | table | tree | input | list | panel | tabs
statusbar | menu | separator | progressbar | canvas
```

### ui.LayoutType

```
absolute | vertical | horizontal | grid | stack | dock | split | form | flow
```

- `absolute`: children use explicit row/col/w/h
- `vertical`: children stack top to bottom
- `horizontal`: children flow left to right
- `grid`: children placed in grid cells
- `stack`: children overlap, ordered by zorder
- `dock`: children attach to edges (top/bottom/left/right/fill)
- `split`: container divided into resizable panes
- `form`: label-value layout
- `flow`: wrap-like sequential layout

### ui.LayoutCfg

Layout hints for widgets inside managed (non-absolute) containers.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `layout_cfg` | Smallstr50 | | Unique key |
| `p_widget` | Widget (Pkey) | | Widget this applies to |
| `min_w` | i32 | 0 | Minimum width (0 = no minimum) |
| `min_h` | i32 | 0 | Minimum height |
| `max_w` | i32 | 0 | Maximum width (0 = unlimited) |
| `max_h` | i32 | 0 | Maximum height |
| `flex` | i32 | 0 | Proportional expansion weight |
| `halign` | HAlign (Pkey) | left | Horizontal alignment |
| `valign` | VAlign (Pkey) | top | Vertical alignment |
| `padding` | i32 | 0 | Inner spacing |
| `gap` | i32 | 0 | Spacing between children |

Only meaningful when the parent widget's layout is not `absolute`.

---

## 2. Presentation

### ui.Style

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `style` | Smallstr50 | | Unique key |
| `fg` | Color (Pkey) | default | Foreground color |
| `bg` | Color (Pkey) | default | Background color |
| `bold` | bool | false | Bold text |
| `underline` | bool | false | Underline text |
| `border` | BorderStyle (Pkey) | none | Border drawing style |

```
ui.style  style:header    fg:cyan   bg:default  bold:Y  underline:N  border:none
ui.style  style:selected  fg:black  bg:cyan     bold:N  underline:N  border:none
```

### ui.StyleSlot

Maps a widget state to a style override.

| Field | Type | Description |
|-------|------|-------------|
| `style_slot` | Smallstr50 | Unique key |
| `p_widget` | Widget (Pkey) | Target widget |
| `state` | WidgetState (Pkey) | State trigger |
| `p_style` | Style (Pkey) | Style to apply |

```
ui.style_slot  style_slot:tree.focused  p_widget:tree  state:focused  p_style:focus_style
```

**Precedence** (highest to lowest): disabled, error, editing, focused, selected.

### Enums

**Color:** `default | black | red | green | yellow | blue | magenta | cyan | white`

**BorderStyle:** `none | single | double | rounded | ascii`

**WidgetState:** `focused | selected | disabled | editing | error`

**HAlign:** `left | center | right`

**VAlign:** `top | middle | bottom`

---

## 3. Interaction

### ui.ActionType

Generic user intent categories:

```
insert | delete | navigate_next | navigate_prev | edit | toggle_expand
select | scroll_up | scroll_down | focus_next | focus_prev | quit
activate | cancel | submit | open | close | copy | paste | refresh
search | page_up | page_down | home | end | sort | filter
expand_all | collapse_all
```

### ui.Command

Named parameterized action.

| Field | Type | Description |
|-------|------|-------------|
| `command` | Smallstr50 | Unique key |
| `action` | ActionType (Pkey) | Action category |
| `p_widget` | Widget (Pkey) | Target widget |
| `arg` | Smallstr200 | Argument (column name, tab id, etc.) |

```
ui.command  command:sort_price  action:sort  p_widget:orders  arg:price
ui.command  command:focus_tree  action:focus p_widget:tree    arg:""
```

### ui.KeyMap

Maps keyboard input to actions or commands.

| Field | Type | Description |
|-------|------|-------------|
| `key_map` | Smallstr50 | Unique key |
| `p_widget` | Widget (Pkey) | Widget scope (empty = global) |
| `key` | Smallstr20 | Key name (j, k, Enter, Esc, Tab, Up, Down, etc.) |
| `action` | ActionType (Pkey) | Simple action |
| `p_command` | Command (Pkey) | Parameterized command (overrides action) |

```
ui.key_map  key_map:nav.down   p_widget:list  key:j      action:navigate_next  p_command:""
ui.key_map  key_map:sort.price p_widget:table key:s      action:""  p_command:sort_price
```

**Rules:**
- Widget-scoped binding wins over global for same key
- If `p_command` is set, it takes precedence over `action`
- At least one of `action` or `p_command` must be set

### Widget-specific config

**ui.TableCfg:**

| Field | Default | Description |
|-------|---------|-------------|
| `p_widget` | | Must be a `table` widget |
| `selectable` | true | Rows are selectable |
| `header` | true | Show column headers |

**ui.Column:**

| Field | Default | Description |
|-------|---------|-------------|
| `p_widget` | | Must be a `table` widget |
| `field` | | Field name (ViewField or raw source) |
| `title` | | Column header text |
| `w` | 10 | Column width |
| `sortable` | false | Column supports sorting |
| `align` | left | Horizontal alignment (HAlign) |
| `hidden` | false | Column is hidden |
| `editable` | false | Cell is editable |

**ui.TreeCfg:**

| Field | Default | Description |
|-------|---------|-------------|
| `p_widget` | | Must be a `tree` widget |
| `indent` | 2 | Indentation per level |
| `show_lines` | true | Draw tree connection lines |
| `child_field` | | Simple mode: field listing children |
| `p_data_path` | | General mode: DataPath traversal (wins over child_field) |
| `label_field` | | Field to display as node label |
| `expanded_default` | false | Nodes start expanded |

**ui.InputCfg:**

| Field | Default | Description |
|-------|---------|-------------|
| `p_widget` | | Must be an `input` widget |
| `editable` | true | Field is editable |
| `maxlen` | 0 | Max input length (0 = unlimited) |

---

## 4. Data Navigation

### ui.DataPath

Named route through relational data.

| Field | Description |
|-------|-------------|
| `data_path` | Unique key |
| `comment` | Description |

```
ui.data_path  data_path:schema  comment:"namespace → ctype → field → fconst"
```

### ui.DataStep

One level in a DataPath. Multiple steps at the same level create branches.

| Field | Description |
|-------|-------------|
| `data_step` | Unique key |
| `p_path` | Parent DataPath (Pkey) |
| `level` | Navigation depth (0 = root) |
| `source_ctype` | Ssimfile tag to display (e.g. `dmmeta.ns`) |
| `label_field` | Tuple attribute for display label |
| `detail_field` | Tuple attribute for detail text |
| `link_field` | Pathcomp filter expression (see below) |

**link_field syntax** (full spec in `doc/link-field-syntax.md`):

| Expression | Meaning | Example: `dmmeta.Ns.ns` |
|---|---|---|
| `.LL` | Left of first dot | `dmmeta` |
| `.LR` | Right of first dot | `Ns.ns` |
| `.RL` | Left of last dot | `dmmeta.Ns` |
| `.RR` | Right of last dot | `ns` |
| `/LL` | Left of first slash | (for slash-separated keys) |
| `""` | No filter (root level) | |

**Branching:** Multiple DataSteps at the same level create alternative
relationships. The browser shows a choice menu.

---

## 5. Data Binding

### ui.Binding

Connects a widget to its data source.

| Field | Description |
|-------|-------------|
| `binding` | Unique key |
| `p_widget` | Target widget (Pkey) |
| `kind` | value, collection, tree, command, property |
| `dir` | read, write, readwrite |
| `source_ctype` | Direct mode: ctype name |
| `source_field` | Direct mode: field/llist name |
| `p_data_path` | Path mode: DataPath for traversal |
| `p_view` | View mode: derived dataset |
| `expr` | Expression mode: computed value |

**Exactly one** source mode must be active:
- Direct: `source_ctype` + `source_field`
- Path: `p_data_path`
- View: `p_view`
- Expression: `expr`

---

## 6. Views

For complex pages with filtering, joining, and derived columns.

### ui.View

| Field | Description |
|-------|-------------|
| `view` | Unique key |
| `p_window` | Optional window scope |
| `base_ctype` | Primary source ctype |
| `comment` | Description |

### ui.ViewSource

Join sources for a view. Exactly one must have `join_kind:base`.

| Field | Description |
|-------|-------------|
| `view_source` | Unique key |
| `p_view` | Parent view |
| `source_ctype` | Source ctype |
| `alias` | Alias for expressions |
| `join_kind` | base, inner, left, right, full, cross |
| `join_expr` | Join condition (empty for base) |

### ui.ViewField

Projected fields visible to widgets.

| Field | Description |
|-------|-------------|
| `view_field` | Unique key |
| `p_view` | Parent view |
| `field` | Logical field name |
| `expr` | Source expression (e.g. `Order.qty * Order.price`) |
| `title` | Display title |
| `datatype` | Type hint |

### ui.ViewFilter

| Field | Description |
|-------|-------------|
| `view_filter` | Unique key |
| `p_view` | Parent view |
| `expr` | Filter expression |
| `source` | fixed, context, widget, runtime |
| `enabled` | Filter is active (default: true) |
| `priority` | Evaluation order (lower = first) |

### ui.ViewSort

| Field | Description |
|-------|-------------|
| `view_sort` | Unique key |
| `p_view` | Parent view |
| `expr` | Sort expression |
| `dir` | asc, desc |
| `priority` | Sort order (lower = first) |

### ui.ContextVar

Window-scoped state variables used by views and filters.

| Field | Description |
|-------|-------------|
| `context_var` | Unique key |
| `p_window` | Owning window |
| `name` | Variable name (e.g. `selected_account`) |
| `datatype` | Type hint |
| `default_expr` | Default value expression |
| `comment` | Description |

---

## 7. Messages (uimsg)

Binary runtime event protocol. All messages have a `MsgHeader` with
`type:u32` and `length:u32`.

| Type ID | Message | Key fields |
|---------|---------|------------|
| 1 | KeyPressMsg | key, modifiers |
| 2 | NavigateMsg | widget, direction |
| 3 | FocusMsg | widget |
| 4 | ScrollMsg | widget, delta |
| 5 | ExpandMsg | widget, node_key, expand |
| 6 | EditMsg | widget, field, value |
| 7 | SelectMsg | widget, row_key |
| 8 | InsertMsg | ssim (varlen) |
| 9 | DeleteMsg | ctype, key |
| 10 | QuitMsg | (none) |
| 11 | RefreshMsg | (none) |
| 12 | CommandMsg | command, arg |

Messages carry symbolic widget keys (strings), not typed references.
The runtime validates that referenced widgets exist.

---

## 8. Validation

Run `bin/ui-validate` to check all invariants:

- **Referential integrity** (via `acr -check`)
- **Widget tree**: valid window, valid parent, same window, no cycles
- **Widget-type configs**: Column→table, TreeCfg→tree, InputCfg→input
- **Binding exclusivity**: exactly one source mode
- **KeyMap**: action or command (not neither)
- **ViewSource**: exactly one base per view
- **Style references**: all referenced styles exist, no duplicate state slots
