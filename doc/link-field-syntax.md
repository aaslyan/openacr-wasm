# DataStep.link_field Syntax

## Purpose

`link_field` in `ui.DataStep` specifies how records at one navigation level
relate to the selected record at the previous level. It uses OpenACR's
`algo::Pathcomp` syntax to extract a prefix from the child record's primary
key and match it against the parent record's key.

## Syntax

The `link_field` value is a Pathcomp expression applied to the child record's
primary key. The result is compared to the parent record's selected key.

### Dot-separated keys (e.g. `dmmeta.Ns`, `dmmeta.Ns.ns`)

| Expression | Meaning | Example input | Result |
|---|---|---|---|
| `.LL` | Left of first dot | `dmmeta.Ns` | `dmmeta` |
| `.LR` | Right of first dot | `dmmeta.Ns` | `Ns` |
| `.RL` | Left of last dot | `dmmeta.Ns.ns` | `dmmeta.Ns` |
| `.RR` | Right of last dot | `dmmeta.Ns.ns` | `ns` |

### Slash-separated keys (e.g. `dmmeta.Ns.ns/active`)

| Expression | Meaning | Example input | Result |
|---|---|---|---|
| `/LL` | Left of first slash | `ui.Color.color/red` | `ui.Color.color` |
| `/LR` | Right of first slash | `ui.Color.color/red` | `red` |

### Empty link_field

Empty string means no filtering — all records of the source ctype are shown.
Used for root-level steps where there is no parent.

## Usage in DataStep

```
ui.data_step  data_step:schema.0  level:0  source_ctype:dmmeta.ns    link_field:""
ui.data_step  data_step:schema.1  level:1  source_ctype:dmmeta.ctype link_field:.LL
ui.data_step  data_step:schema.2  level:2  source_ctype:dmmeta.field link_field:.RL
ui.data_step  data_step:schema.3  level:3  source_ctype:dmmeta.fconst link_field:/LL
```

Navigation flow:
1. Level 0: show all `dmmeta.ns` records (no filter)
2. User selects `dmmeta` → breadcrumb[0] = `dmmeta`
3. Level 1: show `dmmeta.ctype` where `Pathcomp(pkey, ".LL") == "dmmeta"`
   - `dmmeta.Ns` → `.LL` = `dmmeta` → match
   - `algo.Smallstr50` → `.LL` = `algo` → no match
4. User selects `dmmeta.Ns` → breadcrumb[1] = `dmmeta.Ns`
5. Level 2: show `dmmeta.field` where `Pathcomp(pkey, ".RL") == "dmmeta.Ns"`
   - `dmmeta.Ns.ns` → `.RL` = `dmmeta.Ns` → match
   - `dmmeta.Ctype.ctype` → `.RL` = `dmmeta.Ctype` → no match
6. User selects `dmmeta.Ns.ns` → breadcrumb[2] = `dmmeta.Ns.ns`
7. Level 3: show `dmmeta.fconst` where `Pathcomp(pkey, "/LL") == "dmmeta.Ns.ns"`
   - `dmmeta.Ns.ns/active` → `/LL` = `dmmeta.Ns.ns` → match

## Validation Rules

- `link_field` must be empty for level 0 steps
- `link_field` must be non-empty for level > 0 steps
- `link_field` must be a valid Pathcomp expression: one of `.LL`, `.LR`, `.RL`, `.RR`, `/LL`, `/LR`, `/RL`, `/RR`
- The result of applying `link_field` to child keys must match the type of keys at the parent level

## Display Rules

When rendering items, the label is shortened using the complement of the
link expression:
- If `link_field` is `.LL`, display the `.LR` part (strip namespace prefix)
- If `link_field` is `.RL`, display the `.RR` part (strip parent prefix)
- If `link_field` is `/LL`, display the `/LR` part (strip field prefix)
