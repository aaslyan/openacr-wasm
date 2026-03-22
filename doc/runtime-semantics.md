# Runtime Execution Semantics

## Event Pipeline

Every user interaction follows this exact flow:

```
1. Raw input (keypress, click, terminal resize)
2. KeyMap resolves key → ActionType or Command
3. Focused widget interprets action as semantic EventType
4. Trigger lookup: widget + event → p_command or msg_ctype
5. Message construction: build typed uimsg ctype
6. Dispatch: handler updates state/context/data
7. Refresh: affected views re-evaluate, dirty widgets repaint
```

### Example: button activate

```
1. User presses Enter while save_btn is focused
2. KeyMap: Enter → action:activate
3. Widget save_btn receives activate → EventType:activate
4. Trigger: save_btn + activate → p_command:save_current
5. Message: uimsg.CommandMsg { command:"save_current", arg:"" }
6. Dispatch: application handler saves data
7. Refresh: status bar updates
```

### Example: table row select

```
1. User presses Enter on orders table row
2. KeyMap: Enter → action:select
3. Widget orders receives select → EventType:select
4. Trigger: orders + select → msg_ctype:uimsg.SelectMsg
5. Message: uimsg.SelectMsg { widget:"orders", row_key:"order:123" }
6. Dispatch: context $selected_order = "order:123"
7. Refresh: fills_view re-evaluates (depends on $selected_order), fills table repaints
```

### Example: search input submit

```
1. User presses Enter in search input
2. KeyMap: Enter → action:submit
3. Widget search receives submit → EventType:submit
4. Trigger: search + submit → p_command:search_orders
5. Message: uimsg.CommandMsg { command:"search_orders", arg:$input_value }
6. Dispatch: context $search_text = arg, orders_view re-evaluates
7. Refresh: orders table repaints with filtered rows
```

### Example: tree node expand

```
1. User presses Enter on collapsed tree node
2. KeyMap: Enter → action:toggle_expand
3. Widget tree receives toggle_expand → EventType:expand
4. Trigger: tree + expand → msg_ctype:uimsg.ExpandMsg
5. Message: uimsg.ExpandMsg { widget:"tree", node_key:"dmmeta", expand:true }
6. Dispatch: node expansion state updated
7. Refresh: tree widget repaints with children visible
```

## KeyMap vs Trigger

These are two separate layers:

| Layer | Input | Output | Scope |
|-------|-------|--------|-------|
| **KeyMap** | Raw key name | ActionType or Command | Keyboard interpretation |
| **Trigger** | Widget + EventType | Message ctype or Command | Widget behavior declaration |

KeyMap answers: "what does this key do?"
Trigger answers: "what message does this widget event produce?"

A key press goes through KeyMap first, then the resulting action is delivered
to the focused widget, which may fire a Trigger.

## Trigger Firing Rules

1. When a widget receives a semantic event, the runtime looks up all
   `ui.Trigger` records where `p_widget` matches and `event` matches.

2. If `p_command` is non-empty, the runtime invokes that Command.
   The Command's `action`, `p_widget`, and `arg` fields define the behavior.
   A `uimsg.CommandMsg` is emitted.

3. If `p_command` is empty and `msg_ctype` is non-empty, the runtime
   constructs a message of that ctype and dispatches it.

4. If neither is set, the event is silently consumed (no-op trigger).

5. Multiple triggers per widget+event are allowed. They fire in
   declaration order. First matching trigger wins (no cascading).

6. If no trigger exists for a widget+event, the runtime falls back to
   default behavior (e.g., navigate_next moves selection, toggle_expand
   expands/collapses node).

## arg_expr Evaluation

The `arg_expr` field on Trigger maps runtime values into message fields.

Syntax (v1, intentionally simple):

```
field_name=$variable_name
```

Available variables:

| Variable | Meaning |
|----------|---------|
| `$selection` | Primary key of the currently selected row/node |
| `$input_value` | Current text in an input widget |
| `$widget` | Key of the widget that fired the event |
| `$path` | Current DataPath name |
| `$level` | Current navigation depth |

Example:

```
ui.trigger  trigger:orders.select  p_widget:orders  event:select
            msg_ctype:uimsg.SelectMsg  arg_expr:"row_key=$selection"
```

This constructs: `uimsg.SelectMsg { widget:"orders", row_key:<selected row key> }`

Complex expressions (arithmetic, string formatting, multi-field mapping)
are deferred to v2. v1 supports only simple `$variable` substitution.

## Message Construction Rules

1. Every user-visible action produces exactly one typed message.
2. Message `type` field is set from `dmmeta.msgtype` registration.
3. Message `length` field is set to the encoded struct size.
4. Widget key fields (`widget`, `node_key`, etc.) are symbolic strings
   matching `ui.Widget.widget` keys. The runtime validates they exist.
5. If a message references a nonexistent widget, it is dropped with a warning.
6. Messages are dispatched synchronously in v1 (no async queue).

## View Refresh Rules

1. After every dispatched message, the runtime checks which ContextVars changed.
2. For each changed ContextVar, find all Views whose ViewFilters reference it.
3. Re-evaluate those Views (apply filters, sorts, joins).
4. For each re-evaluated View, find all Bindings that reference it.
5. For each affected Binding, mark the bound widget as dirty.
6. Repaint dirty widgets.

In v1, full re-evaluation is acceptable. Incremental/dirty optimization
is a v2 concern (per spec review recommendation).

## Context Update Rules

1. ContextVars are updated by message handlers, not by widgets directly.
2. The handler for `SelectMsg` may update `$selected_X` based on a mapping
   defined in the application layer (not in the UI schema).
3. ContextVars are scoped to a Window. Cross-window context is not supported in v1.
4. Default values from `ContextVar.default_expr` are set on window load.

## State Lifetime

| State | Owned by | Persisted | Reset on |
|-------|----------|-----------|----------|
| Focus | Window | No | Tab/path switch |
| Selection | Widget | No | Data refresh |
| Expansion | Widget | No | Path switch |
| Scroll offset | Widget | No | Data refresh |
| Edit text | InputCfg | No | Submit/cancel |
| ContextVar values | Window | No | Window close |
| View cache | View | No | Context change |

All runtime state is ephemeral in v1. Persistence is a v2 concern.
