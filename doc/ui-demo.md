# OpenACR UI Framework — Demo

## What is this?

A system where user interfaces are defined as text records, not code.
You describe what you want — widgets, data connections, key bindings, styles —
in simple key:value lines. The framework reads them and draws the UI.

Same definitions work on terminal and browser. Same data, different renderers.

## The 5-minute demo

### Step 1: Define a window

```
ui.window  window:main  title:"My App"  rows:24  cols:80
```

One line. A terminal window, 80 columns wide, 24 rows tall.

### Step 2: Add some widgets

```
ui.widget  widget:title   p_window:main  type:label      row:0  col:0  w:80  h:1   text:"Schema Browser"
ui.widget  widget:list    p_window:main  type:panel      row:2  col:0  w:80  h:20  selection:single  focusable:Y
ui.widget  widget:status  p_window:main  type:statusbar  row:23 col:0  w:80  h:1   text:"q:quit  j/k:navigate"
```

Three lines. A title at the top, a browsable list in the middle, a status bar
at the bottom.

### Step 3: Connect data

Tell the list widget what data to show:

```
ui.data_path  data_path:schema  comment:"Browse namespaces → types → fields"

ui.data_step  data_step:schema.0  p_path:schema  level:0  source_ctype:dmmeta.ns     label_field:ns     detail_field:comment  link_field:""
ui.data_step  data_step:schema.1  p_path:schema  level:1  source_ctype:dmmeta.ctype  label_field:ctype  detail_field:comment  link_field:.LL
ui.data_step  data_step:schema.2  p_path:schema  level:2  source_ctype:dmmeta.field  label_field:field  detail_field:arg      link_field:.RL
```

The `data_path` says: start with namespaces, drill into types, then into fields.
The `link_field` says how children relate to parents (`.LL` = left of first dot,
`.RL` = left of last dot).

### Step 4: Add keyboard navigation

```
ui.key_map  key_map:nav.down   p_widget:list  key:j      action:navigate_next
ui.key_map  key_map:nav.up     p_widget:list  key:k      action:navigate_prev
ui.key_map  key_map:nav.enter  p_widget:list  key:Enter  action:activate
ui.key_map  key_map:nav.back   p_widget:list  key:Esc    action:cancel
ui.key_map  key_map:global.q   p_widget:""    key:q      action:quit
```

Five lines. j/k to move, Enter to drill in, Esc to go back, q to quit.

### Step 5: Run it

```
$ acr_tui
```

```
Schema Browser
────────────────────────────────────────────────────────────────────────────────
 ▸ algo                          Basic types and functions
 ▸ algo_lib                      Support library for all executables
 ▸ amc                           Algo Model Compiler: generate code
 ▸ dmmeta                        Schema metadata
 ▸ dev                           Development tables
 ▸ ui                            Generic text UI framework
 ...
────────────────────────────────────────────────────────────────────────────────
 [1:schema]  86 items
```

Press `j` to move down, `Enter` to drill into a namespace:

```
 schema ▸ dmmeta ▸ [dmmeta.ctype]
────────────────────────────────────────────────────────────────────────────────
 ▸ Ctype                         Type definition
 ▸ Field                         Field definition
 ▸ Fconst                        Enum constant
 ▸ Ns                            Namespace
 ▸ Ssimfile                      Ssim data table
 ...
────────────────────────────────────────────────────────────────────────────────
 [1:schema]  128 items
```

Drill into a type to see its fields:

```
 schema ▸ dmmeta ▸ dmmeta.Ns ▸ [dmmeta.field]
────────────────────────────────────────────────────────────────────────────────
   ns                            algo.Smallstr50
   nstype                        algo.Smallstr50
   license                       algo.Smallstr50
   comment                       algo.Comment
────────────────────────────────────────────────────────────────────────────────
 [1:schema]  4 items
```

Press `Esc` to go back, any time.

## Now the interesting part

### Add a second data path — zero code change

Add these lines to the data files:

```
ui.data_path  data_path:build  comment:"Browse build targets → source files"

ui.data_step  data_step:build.0  p_path:build  level:0  source_ctype:dev.target   label_field:target   link_field:""
ui.data_step  data_step:build.1  p_path:build  level:1  source_ctype:dev.targsrc  label_field:targsrc  link_field:.LL
```

Restart. Press `2` to switch:

```
 [1:schema] [2:build]  66 items
────────────────────────────────────────────────────────────────────────────────
 ▸ acr
 ▸ amc
 ▸ abt
 ▸ acr_tui
 ...
```

Same binary, same code, different data. Press `1` to go back to schema browsing.

### Branching: multiple relationships at one level

When a target has source files AND dependencies AND loaded tables:

```
ui.data_step  data_step:build.1a  p_path:build  level:1  source_ctype:dev.targsrc    label_field:targsrc  link_field:.LL
ui.data_step  data_step:build.1b  p_path:build  level:1  source_ctype:dev.targdep    label_field:targdep  link_field:.LL
ui.data_step  data_step:build.1c  p_path:build  level:1  source_ctype:dmmeta.finput  label_field:field    link_field:.LL
```

Drill into a target and the browser asks which relationship to follow:

```
 build ▸ acr ▸ [choose relationship]
────────────────────────────────────────────────────────────────────────────────
 ▸ dev.targsrc                   5 items
 ▸ dev.targdep                   2 items
 ▸ dmmeta.finput                 45 items
```

### View modes: press `v`

Press `v` on any ctype to see its access path diagram (piped from `amc_vis`):

```
 / acr.FDb
 |Lary ctype------------>/ acr.FCtype
 |Thash ind_ctype------->|
 |Lary field-------------|----->/ acr.FField
 |                       |<-----|Upptr p_ctype
```

Press `v` again for raw ssim output (piped from `acr -t`).
Press `Esc` to return to list view.

## What you're looking at

### The data files

```
data/ui/window.ssim      — 1 record
data/ui/widget.ssim      — 3 records
data/ui/style.ssim       — 5 records
data/ui/key_map.ssim     — 14 records
data/ui/data_path.ssim   — 3 records
data/ui/data_step.ssim   — 14 records
```

That's the entire UI definition. ~40 lines of text.

### The binary

`acr_tui` is a 200KB native C++ binary. It loads all 24,000+ ssim records
from `data/`, reads the UI definition from `data/ui/`, and renders to terminal.
The same code compiles to WebAssembly for browser rendering.

### What's NOT in the binary

- No knowledge of `dmmeta.Ns` or `dmmeta.Ctype` or any specific data type
- No hardcoded columns, layouts, or key bindings
- No type-specific rendering logic

The binary is generic. The data files define everything.

## Adding styles

```
ui.style  style:header    fg:cyan   bg:default  bold:Y  border:none
ui.style  style:selected  fg:black  bg:cyan     bold:N  border:none
ui.style  style:panel     fg:default bg:default bold:N  border:single
ui.style  style:status    fg:white  bg:blue     bold:N  border:none
```

Widgets reference styles by name:

```
ui.widget  widget:title  ... p_style:header  ...
ui.widget  widget:status ... p_style:status  ...
```

Change `fg:cyan` to `fg:green` — the title turns green. No recompile.

State-based overrides:

```
ui.style_slot  style_slot:list.focused   p_widget:list  state:focused   p_style:focus_style
ui.style_slot  style_slot:list.selected  p_widget:list  state:selected  p_style:selected
```

The selected row gets the `selected` style. The focused widget gets the
`focus_style`. The renderer handles the rest.

## The schema at a glance

**35 types** define the entire UI framework:

**Structure** (what exists):
Window, Widget, WidgetType, LayoutType, LayoutCfg

**Presentation** (how it looks):
Style, StyleSlot, Color, BorderStyle, WidgetState, HAlign, VAlign

**Interaction** (how it behaves):
ActionType, Command, KeyMap, InputCfg, TableCfg, Column, TreeCfg, SelectionMode

**Data navigation** (what data to show):
DataPath, DataStep, Binding, BindingKind, BindingDir

**Views** (derived data for complex pages):
View, ViewSource, ViewField, ViewFilter, ViewSort, ContextVar,
JoinKind, FilterSource, SortDir

**Messages** (runtime events — separate `uimsg` namespace):
MsgHeader + 12 typed messages (KeyPress, Navigate, Focus, Scroll,
Expand, Edit, Select, Insert, Delete, Quit, Refresh, Command)

## What this means for your application

To build a UI for your data:

1. Define your data as ssim records (you probably already have)
2. Write `ui.data_path` + `ui.data_step` lines describing the navigation
3. Write `ui.widget` lines for the layout
4. Write `ui.key_map` lines for keyboard shortcuts
5. Run `acr_tui`

No C++ code. No JavaScript. No recompilation.

To build a filtered, multi-table page (like an order monitor):

1. Define `ui.view` with ViewSource, ViewField, ViewFilter, ViewSort
2. Define `ui.context_var` for page state (selected account, search text)
3. Bind widgets to views instead of raw data
4. The framework handles the filtering and re-evaluation

## Try it

```bash
cd openacr
bin/acr_tui
```

Press `j`/`k` to navigate, `Enter` to drill in, `Esc` to go back,
`1`/`2`/`3` to switch data paths, `v` to toggle view modes, `q` to quit.

Then open `data/ui/data_step.ssim` in a text editor, change a `source_ctype`,
restart, and see different data in the same UI.

That's the framework.
