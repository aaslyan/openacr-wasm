# UI Framework Specification v1

## Purpose

This document defines a declarative text UI framework for schema-driven applications. The goal is to let developers describe windows, widgets, data bindings, derived views, filters, commands, styling, and runtime messages using metadata rather than hardcoded UI logic.

The framework is intended for terminal or text-based applications, but the abstractions are general enough to support other renderers later.

This specification is for implementation. It defines the conceptual model, required entities, invariants, runtime behavior, and recommended implementation boundaries.

---

## 1. Design Goals

The framework must support:

1. Declarative window and widget trees.
2. Separation of structure, presentation, behavior, and data derivation.
3. Binding widgets to raw data, derived views, or structural navigation paths.
4. Multi-table pages whose content depends on filters and window context.
5. Reusable commands and key mappings.
6. State-aware styling.
7. A binary runtime event protocol for dispatching user actions.
8. Validation of schema-level invariants.
9. Code generation from metadata.

The framework must avoid embedding business-specific query logic directly into widgets.

---

## 2. Architectural Layers

The system is divided into six layers.

### 2.1 Structural Layer

Defines what exists on screen.

Core concepts:

* Window
* Widget
* WidgetType
* LayoutType
* LayoutCfg

### 2.2 Presentation Layer

Defines how things look.

Core concepts:

* Style
* StyleSlot
* Color
* BorderStyle
* WidgetState

### 2.3 Interaction Layer

Defines how the user acts on the UI.

Core concepts:

* ActionType
* Command
* KeyMap
* InputCfg
* TableCfg
* TreeCfg

### 2.4 Data Navigation Layer

Defines structural browsing of relational or hierarchical data.

Core concepts:

* DataPath
* DataStep

### 2.5 View Layer

Defines presentation-ready derived datasets, including joins, filters, sorting, and projections.

Core concepts:

* View
* ViewSource
* ViewField
* ViewFilter
* ViewSort
* ContextVar

### 2.6 Runtime Event Layer

Defines binary protocol messages dispatched during interaction.

Core concepts:

* uimsg.MsgHeader
* concrete event messages such as KeyPressMsg, NavigateMsg, SelectMsg, EditMsg, CommandMsg

---

## 3. Core Concepts

## 3.1 Window

A Window is the top-level UI container.

Required responsibilities:

* Define terminal dimensions or logical viewport dimensions.
* Contain top-level widgets.
* Own context variables.
* Serve as the scope root for window-local views.

Required fields:

* `window`: unique key
* `title`: display title
* `rows`: terminal rows
* `cols`: terminal columns

Rules:

* A widget must belong to exactly one window.
* A window may contain zero or more top-level widgets.
* Context variables may be scoped to a window.

---

## 3.2 Widget

A Widget is the universal UI node.

Required responsibilities:

* Participate in a parent-child tree.
* Declare widget type.
* Declare layout behavior for its children.
* Provide display metadata.
* Bind to data, views, or commands indirectly.

Required fields:

* `widget`: unique key
* `p_window`: parent window
* `p_parent`: parent widget, empty only for top-level widgets
* `type`: widget type
* `layout`: child layout type
* `row`, `col`, `w`, `h`: absolute placement fields
* `zorder`
* `visible`
* `enabled`
* `selection`
* `p_style`
* `title`
* `text`
* `focusable`

Semantics:

* `row`, `col`, `w`, `h` are authoritative only when the parent uses `absolute` layout.
* When the parent uses a managed layout, size and alignment are controlled by `LayoutCfg`.
* `p_style` may be empty, in which case style is inherited from the nearest ancestor with a style.

Rules:

* `p_parent` must reference a widget in the same window.
* A widget may have children unless explicitly forbidden by its widget type.
* Leaf widgets may ignore `layout`.
* `focusable` should normally be true only for interactive widgets.

---

## 3.3 WidgetType

Supported widget types in v1:

* label
* table
* tree
* input
* list
* panel
* tabs
* statusbar
* menu
* separator
* progressbar
* canvas

Implementation note:
Not every renderer must support every type immediately, but unsupported types must fail validation or degrade explicitly.

---

## 3.4 LayoutType

Supported layout types in v1:

* absolute
* vertical
* horizontal
* grid
* stack
* dock
* split
* form
* flow

Semantics:

* `absolute`: child widgets use explicit `row`, `col`, `w`, `h`
* `vertical`: children stacked top to bottom
* `horizontal`: children placed left to right
* `grid`: children placed in grid cells
* `stack`: children overlap in same region, ordered by zorder or active state
* `dock`: children attached to top/bottom/left/right/fill regions
* `split`: container divided into resizable panes
* `form`: label/value style layout
* `flow`: wrap-like sequential layout

---

## 3.5 LayoutCfg

LayoutCfg defines managed-layout hints for a widget within its parent.

Required fields:

* `layout_cfg`: unique key
* `p_widget`
* `min_w`, `min_h`
* `max_w`, `max_h`
* `flex`
* `halign`
* `valign`
* `padding`
* `gap`

Note: implementation should not reuse a single mixed `Align` enum for all alignment concerns. v1 requires separate horizontal and vertical alignment semantics even if stored compactly internally.

Semantics:

* `flex` controls proportional expansion in managed layouts.
* `padding` is inner spacing for container content.
* `gap` is spacing between child widgets.
* `halign` and `valign` are placement hints when extra space exists.

Rules:

* LayoutCfg is meaningful only when parent layout is not `absolute`.
* A widget may have at most one LayoutCfg.

---

## 4. Presentation Layer

## 4.1 Color

Terminal color domain:

* default
* black
* red
* green
* yellow
* blue
* magenta
* cyan
* white

Renderers may extend this later, but v1 assumes the base color set.

## 4.2 BorderStyle

Supported border styles:

* none
* single
* double
* rounded
* ascii

## 4.3 WidgetState

Supported visual states:

* focused
* selected
* disabled
* editing
* error

## 4.4 Style

Style defines base visual appearance.

Required fields:

* `style`: unique key
* `fg`
* `bg`
* `bold`
* `underline`
* `border`

## 4.5 StyleSlot

StyleSlot maps widget state to a style override.

Required fields:

* `style_slot`: unique key
* `p_widget`
* `state`
* `p_style`

Semantics:

* Base style comes from widget inheritance.
* State style overlays base style when a widget enters that state.
* Multiple state styles may be combined only if renderer defines deterministic precedence. v1 default precedence is:

  1. disabled
  2. error
  3. editing
  4. focused
  5. selected

Rules:

* Each widget should have at most one StyleSlot per state.

---

## 5. Interaction Layer

## 5.1 ActionType

ActionType defines generic user intents.

Supported actions in v1:

* insert
* delete
* navigate_next
* navigate_prev
* edit
* toggle_expand
* select
* scroll_up
* scroll_down
* focus_next
* focus_prev
* quit
* activate
* cancel
* submit
* open
* close
* copy
* paste
* refresh
* search
* page_up
* page_down
* home
* end
* sort
* filter
* expand_all
* collapse_all

ActionType is a category, not a parameterized command.

## 5.2 Command

Command defines a named invokable action instance.

Required fields:

* `command`: unique key
* `action`
* `p_widget`: target widget or owning widget
* `arg`: optional argument payload

Examples:

* sort current table by `price`
* open tab `details`
* focus widget `schema_tree`
* filter current list by `active`

Rules:

* KeyMap should preferably target a Command, not only a raw ActionType, when arguments matter.

## 5.3 KeyMap

KeyMap binds keyboard input to actions or commands.

Required fields:

* `key_map`: unique key
* `p_widget`: optional widget scope, empty means global within window
* `key`
* `p_command` or `action`

v1 requirement:
Implementation must support global and widget-scoped key maps. If both exist for the same key, widget-scoped mapping wins.

## 5.4 InputCfg

Defines input widget behavior.

Required fields:

* `input_cfg`
* `p_widget`
* `editable`
* `maxlen`

## 5.5 TableCfg

Defines table widget behavior.

Required fields:

* `table_cfg`
* `p_widget`
* `selectable`
* `header`

## 5.6 Column

Defines a table column.

Required fields:

* `column`: unique key
* `p_widget`: parent table widget
* `field`: logical field name or projected view field
* `title`
* `w`
* `sortable`
* `halign`
* `hidden`
* `editable`

Rules:

* A Column must reference a widget of type `table`.
* Column ordering is stable by declaration order unless explicit order field is added later.

## 5.7 TreeCfg

Defines tree widget behavior.

Required fields:

* `tree_cfg`
* `p_widget`
* `indent`
* `show_lines`
* `child_field` or `p_data_path`
* `label_field`
* `expanded_default`

Semantics:

* `child_field` is the simple direct-hierarchy mode.
* `p_data_path` is the generalized structural traversal mode.
* A tree widget may use either direct hierarchy or path-driven hierarchy.

Rules:

* TreeCfg must reference a widget of type `tree`.
* If `p_data_path` is provided, it takes precedence over `child_field`.

---

## 6. Data Navigation Layer

## 6.1 DataPath

DataPath defines a named navigation path through relational or metadata structures.

Required fields:

* `data_path`: unique key
* `comment`

A DataPath is used when the UI must browse structure rather than consume a flat derived dataset.

Examples:

* namespace → ctype → field → fconst
* target → source file
* widget → column / keymap / binding

## 6.2 DataStep

DataStep defines one expansion rule inside a DataPath.

Required fields:

* `data_step`: unique key
* `p_path`
* `level`
* `source_ctype`
* `label_field`
* `detail_field`
* `link_field`

Semantics:

* Multiple DataSteps may exist at the same level.
* Same-level steps are alternative child expansions from the previous level.
* `link_field` expresses how rows at one level are related to the parent row.
* `label_field` provides display label.
* `detail_field` provides secondary detail text.

Implementation requirement:
`link_field` syntax must be centrally documented and validated. It must not remain a hidden convention.

---

## 7. View Layer

This layer is required for multi-table pages, filtered pages, and pages whose content depends on current context.

Widgets must not embed query logic directly when that logic represents page semantics.

## 7.1 View

View defines a named presentation-ready derived dataset.

Required fields:

* `view`: unique key
* `p_window`: optional window scope
* `base_ctype`: primary logical source
* `comment`

A View may represent:

* filtered subset of one table
* join across multiple tables
* master-detail projection
* search result set
* computed or denormalized presentation rows

## 7.2 ViewSource

ViewSource defines participating data sources for a View.

Required fields:

* `view_source`: unique key
* `p_view`
* `source_ctype`
* `alias`
* `join_kind`
* `join_expr`

Supported `join_kind` values in v1:

* base
* inner
* left
* right
* full
* cross

Rules:

* Exactly one ViewSource per View must have `join_kind=base`.
* All other ViewSources join relative to the accumulated dataset.

## 7.3 ViewField

ViewField defines projected fields visible to widgets.

Required fields:

* `view_field`: unique key
* `p_view`
* `field`
* `expr`
* `title`
* `datatype`

Examples:

* `field:account_name expr:Account.name`
* `field:notional expr:Order.qty * Order.price`
* `field:status_label expr:Order.status`

## 7.4 ViewFilter

ViewFilter defines predicates constraining a View.

Required fields:

* `view_filter`: unique key
* `p_view`
* `expr`
* `source`: fixed | context | widget | runtime
* `enabled`
* `priority`

Semantics:

* `fixed`: always applied by page design
* `context`: depends on window context variables
* `widget`: depends on another widget selection or state
* `runtime`: user-supplied ad hoc filter

Examples:

* `Order.account = $selected_account`
* `Order.status != "closed"`
* `Instrument.symbol contains $search_text`

## 7.5 ViewSort

ViewSort defines row ordering.

Required fields:

* `view_sort`: unique key
* `p_view`
* `expr`
* `dir`: asc | desc
* `priority`

## 7.6 ContextVar

ContextVar defines page/window state used by views.

Required fields:

* `context_var`: unique key
* `p_window`
* `name`
* `datatype`
* `default_expr`
* `comment`

Examples:

* selected account
* selected order
* search text
* active tab
* date range

Semantics:

* Context variables are part of the window state, not widget-local business logic.
* Views may reference ContextVars in filter expressions and computed fields.

---

## 8. Binding Layer

Binding connects widgets to data.

## 8.1 Binding

Required fields:

* `binding`: unique key
* `p_widget`
* `kind`
* `dir`
* exactly one of:

  * `source_ctype` + `source_field`
  * `p_data_path`
  * `p_view`
  * `expr`

Supported binding kinds in v1:

* value
* collection
* tree
* command
* property

Supported binding directions in v1:

* read
* write
* readwrite

Semantics by kind:

* `value`: scalar value for label, input, detail field
* `collection`: row set for table/list
* `tree`: hierarchical traversal for tree widget
* `command`: action-producing or command-consuming binding
* `property`: widget property such as visibility, enabled state, title, style role

Rules:

* A table widget should normally bind using `collection` to a View.
* A tree widget should normally bind using `tree` to a DataPath or direct child field.
* An input widget should normally bind using `value` or `property`.
* `readwrite` is valid only for editable widgets.

---

## 9. Window Context Model

Every window may have a runtime context store.

The context store contains:

* declared ContextVars
* current focused widget
* current selection per selectable widget
* transient runtime filters
* active tab/page state

Views may depend on context store values.

Widget selection changes must update context when configured to do so.

Example:

* selecting account row updates `$selected_account`
* orders_view re-evaluates automatically
* selecting an order updates `$selected_order`
* fills_view and details_view re-evaluate automatically

---

## 10. Runtime Message Protocol (`uimsg`)

The runtime message protocol represents normalized UI events.

## 10.1 MsgHeader

All messages begin with:

* `type: u32`
* `length: u32`

Invariant:
`MsgHeader.type` must equal the registered message type id for the payload ctype.

## 10.2 Required messages in v1

### Raw input

* `KeyPressMsg(key, modifiers)`

### Navigation and focus

* `NavigateMsg(widget, direction)`
* `FocusMsg(widget)`
* `ScrollMsg(widget, delta)`
* `ExpandMsg(widget, node_key, expand)`
* `SelectMsg(widget, row_key)`

### Editing and mutation

* `EditMsg(widget, field, value)`
* `InsertMsg(ssim)`
* `DeleteMsg(ctype, key)`

### Session and repaint

* `RefreshMsg()`
* `QuitMsg()`

### Semantic command dispatch

* `CommandMsg(command, arg)`

## 10.3 Protocol design rules

* `KeyPressMsg` is raw user input.
* Command resolution should happen after key mapping.
* `CommandMsg` is the preferred semantic action message for parameterized operations.
* CRUD messages are allowed in v1, but implementations may route them through domain command handlers rather than directly mutating storage.

## 10.4 Widget references in messages

The protocol may carry symbolic widget keys rather than direct typed references. This is acceptable because messages are transport objects, not schema objects. The runtime must validate that referenced widget keys exist.

---

## 11. Validation Rules

The implementation must validate at least the following invariants.

### 11.1 Structural invariants

* Every widget belongs to a valid window.
* Every non-top-level widget references a valid parent widget.
* Parent and child widgets must belong to the same window.
* No widget tree cycles are allowed.

### 11.2 Widget-type invariants

* `table` widgets should have TableCfg.
* `tree` widgets should have TreeCfg.
* `input` widgets should have InputCfg.
* `Column.p_widget` must reference a `table` widget.
* `TreeCfg.p_widget` must reference a `tree` widget.
* `InputCfg.p_widget` must reference an `input` widget.

### 11.3 Binding invariants

* Every Binding must reference a valid widget.
* Binding kind must match widget usage.
* A Binding must specify exactly one binding source mode.
* `readwrite` binding requires editable UI semantics.

### 11.4 View invariants

* Every View must have exactly one base ViewSource.
* Every ViewField must belong to a valid View.
* Every ViewFilter and ViewSort must belong to a valid View.
* Context-based filters may reference only declared ContextVars in scope.

### 11.5 Style invariants

* Every referenced style must exist.
* A widget may not have duplicate state-specific StyleSlots for the same state.

### 11.6 Message invariants

* Every registered message type id must be unique.
* Message payload length must match encoded length.

---

## 12. Runtime Behavior

## 12.1 Rendering pipeline

The renderer should operate in this order:

1. Load window and widget tree.
2. Resolve inherited styles.
3. Resolve context variables.
4. Evaluate views.
5. Resolve bindings.
6. Compute layout.
7. Apply widget states.
8. Render widgets.

## 12.2 Event pipeline

The runtime should operate in this order:

1. Receive raw input.
2. Emit `KeyPressMsg`.
3. Resolve active widget and widget scope.
4. Apply widget-scoped KeyMap, else global KeyMap.
5. Convert input to `CommandMsg` or direct semantic message.
6. Update context and widget state.
7. Re-evaluate affected views.
8. Re-render dirty widgets.

## 12.3 State transitions

At minimum the runtime must track:

* focused widget
  n- selected rows/nodes
* expanded tree nodes
* edit state for inputs/tables
* active filters
* current context variable values

---

## 13. Recommended Developer Responsibilities

The dev team should implement the system in these modules.

### 13.1 Schema loader

Reads metadata records and builds typed in-memory model.

### 13.2 Validator

Checks all invariants before runtime starts.

### 13.3 View engine

Evaluates View, ViewSource, ViewField, ViewFilter, ViewSort against backing tables or generated records.

### 13.4 DataPath navigator

Expands structural navigation using DataPath and DataStep.

### 13.5 Binding resolver

Connects widgets to View, DataPath, scalar values, or property expressions.

### 13.6 Layout engine

Computes placement for absolute and managed layouts.

### 13.7 Renderer

Draws widgets, borders, text, tables, trees, and state overlays.

### 13.8 Input/event dispatcher

Maps keypresses to commands and emits normalized messages.

### 13.9 Context store

Maintains window-local state and triggers dependent re-evaluation.

---

## 14. Example Page Model

Example page: Order Monitor

Window context:

* `selected_account`
* `selected_order`
* `search_text`
* `active_tab`

Views:

* `accounts_view`: active accounts
* `orders_view`: orders filtered by selected account and search text
* `fills_view`: fills filtered by selected order
* `order_detail_view`: selected order joined with account and instrument

Widgets:

* left tree or list: accounts
* top table: orders
* bottom table: fills
* right panel: order detail labels/fields
* status bar: counts and mode text

Interaction:

* selecting account updates `selected_account`
* selecting order updates `selected_order`
* search input updates `search_text`
* orders and fills re-evaluate automatically

This is the target style of usage the framework must support.

---

## 15. v1 Scope vs Later Extensions

### In v1

Required:

* windows
* widget tree
* styles and state overlays
* table/tree/input configs
* commands and keymaps
* bindings
* data paths
* views, filters, sorts, context vars
* runtime event protocol
* validation

### Later extensions

Not required in v1 but compatible with the model:

* modal dialogs
* popup overlays
* richer colors/themes
* checkbox/radio/select widgets
* async data loading
* per-cell styling rules
* validation rules on input values
* formulas/macros for expressions
* mouse support
* virtualization for large tables/trees

---

## 16. Non-Negotiable Design Principles

1. Widgets must not own business query logic.
2. Page semantics belong in Views and ContextVars.
3. Structural browsing belongs in DataPath/DataStep.
4. Actions are categories; Commands are concrete invocations.
5. Messages are runtime transport objects, not schema references.
6. Validation is mandatory, not optional.
7. References must be typed wherever possible in schema.
8. The same metadata must support code generation, validation, and runtime execution.

---

## 17. Deliverable Expected from Dev Team

The dev team should deliver:

1. A loader for the metadata schema.
2. A validator enforcing the rules in this spec.
3. A runtime library implementing rendering, layout, view evaluation, bindings, and message dispatch.
4. A small demo app proving:

   * one table bound to a View
   * one tree bound to a DataPath
   * one search input bound to a ContextVar
   * one status bar using state-aware styling
   * keyboard navigation and command dispatch
5. A documented mapping from metadata to generated C++ runtime structures.

---

## 18. Final Summary

This framework is a declarative text UI system with explicit support for:

* widget hierarchy
* presentation and state styling
* commands and key maps
* structural navigation
* derived multi-table views
* filters and sorting
* window context
* normalized runtime messages

The critical architectural decision is the separation between:

* `DataPath` for structural browsing
* `View` for presentation-ready derived data
* `ContextVar` for page/window state

That separation keeps widgets simple and makes complex pages implementable without hardcoded per-widget data logic.

